/* tk_anim.c - M5 animation engine.
 *
 * A deliberately small engine for a framebuffer device with no
 * GPU and a slow single core (DESIGN constraints, UI-REDESIGN
 * section on animation):
 *
 * - Animations interpolate ONE integer value with an easing
 *   function; the consumer's callback applies it (a knob
 *   position, a surface offset, ...). No property system, no
 *   per-widget style animation.
 * - Time-based, driven by tk_root_tick(): the value depends on
 *   the wall clock, never on frame count, so a coarse main loop
 *   (50 ms polls) degrades to fewer intermediate frames over the
 *   same 150-200 ms, never to a slower or longer animation.
 * - Frame budget: consumers repaint through the damage path, so
 *   an animation frame costs only its swept rect. The session
 *   loop can poll tk_root_anim_active() and shorten its wait
 *   while something is moving (target ~30 fps on device).
 * - tk_root_set_animations_enabled(root, false) is the settings
 *   kill switch UI-REDESIGN requires: tk_anim_start() then
 *   delivers the final value synchronously and returns NULL.
 *
 * Ownership: an animation is owned by its root. The handle stays
 * valid until the animation completes, is cancelled, or the root
 * is freed. `ud` is borrowed; in-toolkit animations pass the
 * widget itself and cancel from the widget free path.
 */
#include "tk_internal.h"

struct tk_anim {
    tk_anim *next;
    int from, to;
    uint32_t start_ms, dur_ms;
    tk_ease_fn ease;
    tk_anim_cb cb;
    tk_anim_done_cb done;
    void *ud;
    bool dead;      /* completed or cancelled; swept by advance */
};

float tk_ease_linear(float t) { return t; }

/* M18: the AOSP framework interpolators (platform/frameworks/
 * base core/res/res/interpolator, fetched 2026-11-20):
 * decelerate_quad is f(t) = 1-(1-t)^2, accelerate_quad is
 * f(t) = t^2. The toast enter/exit pair uses them (see
 * tk_core.c's toast fade). */
float tk_ease_decelerate_quad(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u;
}

float tk_ease_accelerate_quad(float t) { return t * t; }

float tk_ease_out_cubic(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float tk_ease_in_out_cubic(float t)
{
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}

/* M12: cubic-bezier easing, evaluated the way the reference
 * frameworks define it: the curve's x is the time fraction,
 * y is the progress fraction, so y(x) needs the parameter s
 * with x(s) = x solved first. Newton iterations from s = x
 * with a bisection bracket as the fallback; a fixed small
 * iteration count, no allocation (the engine runs per frame
 * on a 128 MB device). */
float tk_bezier_y(float x1, float y1, float x2, float y2,
                      float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    float lo = 0.0f, hi = 1.0f, s = x;
    for (int i = 0; i < 8; i++) {
        float u = 1.0f - s;
        float xs = 3.0f * u * u * s * x1 + 3.0f * u * s * s * x2
                   + s * s * s;
        float err = xs - x;
        if (err > -0.0005f && err < 0.0005f) break;
        if (err > 0.0f) hi = s; else lo = s;
        float dxs = 3.0f * u * u * x1 + 6.0f * u * s * (x2 - x1)
                    + 3.0f * s * s * (1.0f - x2);
        float ns = dxs > 0.0005f ? s - err / dxs : (lo + hi) * 0.5f;
        s = ns > lo && ns < hi ? ns : (lo + hi) * 0.5f;
    }
    float u = 1.0f - s;
    return 3.0f * u * u * s * y1 + 3.0f * u * s * s * y2
           + s * s * s;
}

float tk_ease_standard(float t)
{
    return tk_bezier_y(0.2f, 0.0f, 0.0f, 1.0f, t);
}

float tk_ease_standard_decelerate(float t)
{
    return tk_bezier_y(0.0f, 0.0f, 0.0f, 1.0f, t);
}

float tk_ease_emphasized_decelerate(float t)
{
    return tk_bezier_y(0.05f, 0.7f, 0.1f, 1.0f, t);
}

void tk_anim_mode_pair(bool large, uint32_t *dur_ms,
                       tk_ease_fn *ease)
{
    const tk_theme *t = tk_theme_default();
    if (t && t->style == 2) {
        /* WinUI (Microsoft Learn, Timing and easing; Motion in
         * Windows): ControlFastAnimationDuration 167 ms /
         * ControlNormalAnimationDuration 250 ms, Direct
         * Entrance curve cubic-bezier(0,0,0,1) */
        *dur_ms = large ? 250 : 167;
        *ease = tk_ease_standard_decelerate;
    } else if (large) {
        /* M3: medium2 = 300 ms, Emphasized decelerate */
        *dur_ms = 300;
        *ease = tk_ease_emphasized_decelerate;
    } else {
        /* M3: short4 = 200 ms, Standard decelerate */
        *dur_ms = 200;
        *ease = tk_ease_standard_decelerate;
    }
}

void tk_anim_selection_pair(uint32_t *dur_ms, tk_ease_fn *ease)
{
    const tk_theme *t = tk_theme_default();
    if (t && t->style == 2) {
        /* WinUI: the Switch pairing (M12) - 167 ms Direct
         * Entrance cubic-bezier(0,0,0,1) */
        *dur_ms = 167;
        *ease = tk_ease_standard_decelerate;
    } else {
        /* M3 motion tokens: "Selection controls have a short
         * duration of 200ms with Standard easing" */
        *dur_ms = 200;
        *ease = tk_ease_standard;
    }
}

void tk_root_set_animations_enabled(tk_root *r, bool on)
{
    if (r) r->anims_enabled = on;
}

bool tk_root_animations_enabled(const tk_root *r)
{
    return !r || r->anims_enabled;
}

bool tk_root_anim_active(const tk_root *r)
{
    if (!r) return false;
    for (tk_anim *a = r->anims; a; a = a->next)
        if (!a->dead) return true;
    return false;
}

tk_anim *tk_anim_start(tk_root *r, int from, int to, uint32_t dur_ms,
                       tk_ease_fn ease, tk_anim_cb cb,
                       tk_anim_done_cb done, void *ud)
{
    if (!r || !cb) return NULL;
    if (!r->anims_enabled || dur_ms == 0 || from == to) {
        /* reduced motion / degenerate: land on the final value
         * synchronously so state stays exact */
        cb(ud, to);
        if (done) done(ud);
        return NULL;
    }
    tk_anim *a = calloc(1, sizeof(*a));
    if (!a) { cb(ud, to); if (done) done(ud); return NULL; }
    a->from = from; a->to = to;
    a->start_ms = r->anim_now;
    a->dur_ms = dur_ms;
    a->ease = ease ? ease : tk_ease_linear;
    a->cb = cb; a->done = done; a->ud = ud;
    a->next = r->anims;
    r->anims = a;
    return a;
}

void tk_anim_cancel(tk_anim *a)
{
    if (a) a->dead = true; /* swept by the next advance/free */
}

void tk_anims_advance(tk_root *r, uint32_t now_ms)
{
    if (!r) return;
    r->anim_now = now_ms;
    /* deliver frames; callbacks may start or cancel animations
     * (cancellation only marks; starting prepends - neither
     * invalidates this walk) */
    for (tk_anim *a = r->anims; a; a = a->next) {
        if (a->dead) continue;
        uint32_t el = now_ms - a->start_ms; /* wrap-safe */
        float t = a->dur_ms ? (float)el / (float)a->dur_ms : 1.0f;
        if (t >= 1.0f) {
            a->dead = true;
            a->cb(a->ud, a->to);
            if (a->done) a->done(a->ud);
        } else {
            float e = a->ease(t);
            int v = a->from + (int)((a->to - a->from) * e + 0.5f);
            a->cb(a->ud, v);
        }
    }
    /* sweep the dead */
    tk_anim **pp = &r->anims;
    while (*pp) {
        if ((*pp)->dead) {
            tk_anim *d = *pp;
            *pp = d->next;
            free(d);
        } else {
            pp = &(*pp)->next;
        }
    }
}

void tk_anims_free_all(tk_root *r)
{
    if (!r) return;
    tk_anim *a = r->anims;
    while (a) { tk_anim *n = a->next; free(a); a = n; }
    r->anims = NULL;
}

/* ---- overlay slides -------------------------------------------- */
/* M12: generalized. The whole subtree travels with the face
 * (pre-M12 only the surface's own rect moved, so a dialog's
 * children sat at the final position while its face was still
 * travelling). One axis per slide; slide_cur is the offset
 * currently applied, so every frame shifts by the delta and
 * interruptions stay consistent. Damage is the band swept
 * between the previous and the new position. */

static void slide_shift(tk_widget *w, int dx, int dy)
{
    w->rect.x += dx;
    w->rect.y += dy;
    for (size_t i = 0; i < w->n_children; i++)
        slide_shift(w->children[i], dx, dy);
}

static void slide_damage(tk_widget *w, int delta)
{
    tk_root *r = tk_widget_root(w);
    if (!r || delta == 0) return;
    tk_rect band = w->rect;
    if (w->slide_axis_x) {
        if (delta < 0) band.x += delta;
        band.w += delta < 0 ? -delta : delta;
        band.x -= 1; band.y -= 1; band.w += 2; band.h += 2;
    } else {
        if (delta < 0) band.y += delta;
        band.h += delta < 0 ? -delta : delta;
        band.x -= 1; band.y -= 1; band.w += 2; band.h += 2;
    }
    tk_root_invalidate_rect(r, band);
}

static void slide_cb(void *ud, int value)
{
    tk_widget *w = ud;
    int delta = value - w->slide_cur;
    if (delta == 0) return;
    slide_damage(w, delta);
    if (w->slide_axis_x) slide_shift(w, delta, 0);
    else slide_shift(w, 0, delta);
    w->slide_cur = value;
}

static void slide_land(tk_widget *w)
{
    /* land exactly on the canonical position */
    if (w->slide_axis_x) {
        int d = w->slide_end_x - w->rect.x;
        if (d) slide_shift(w, d, 0);
    } else {
        int d = w->slide_end_y - w->rect.y;
        if (d) slide_shift(w, 0, d);
    }
    w->slide_cur = 0;
}

static void slide_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    slide_land(w);
    tk_widget_invalidate(w);
}

static void slide_in_common(tk_root *r, tk_widget *w, int dx,
                            int dy, uint32_t dur_ms,
                            tk_ease_fn ease)
{
    if (!r || !w || (dx == 0 && dy == 0)) return;
    /* No tick has ever reached this root: there is no live clock
     * to animate against (headless one-shot use), so the surface
     * simply appears at its final rect. */
    if (r->anim_now == 0) return;
    if (w->anim_handle) { tk_anim_cancel(w->anim_handle); w->anim_handle = NULL; }
    if (w->slide_cur != 0) {
        /* a previous slide was interrupted: land it first so
         * the new slide starts from the canonical rect */
        slide_damage(w, -w->slide_cur);
        if (w->slide_axis_x) slide_shift(w, -w->slide_cur, 0);
        else slide_shift(w, 0, -w->slide_cur);
        w->slide_cur = 0;
    }
    w->slide_axis_x = dx != 0;
    w->slide_end_x = w->rect.x;
    w->slide_end_y = w->rect.y;
    int off = w->slide_axis_x ? dx : dy;
    w->slide_span = off < 0 ? -off : off;
    slide_shift(w, dx, dy);
    w->slide_cur = off;
    slide_damage(w, off);
    w->anim_handle = tk_anim_start(r, off, 0, dur_ms, ease,
                                   slide_cb, slide_done, w);
    /* a NULL handle means tk_anim_start already delivered the
     * final value synchronously (reduced motion): slide_cb /
     * slide_done have landed the surface */
}

void tk_overlay_slide_in(tk_root *r, tk_widget *w, int dy)
{
    uint32_t dur; tk_ease_fn ease;
    tk_anim_mode_pair(false, &dur, &ease);
    slide_in_common(r, w, 0, dy, dur, ease);
}

void tk_overlay_slide_in_dur(tk_root *r, tk_widget *w, int dy,
                             uint32_t dur_ms, tk_ease_fn ease)
{
    slide_in_common(r, w, 0, dy, dur_ms, ease);
}

void tk_overlay_slide_in_x(tk_root *r, tk_widget *w, int dx,
                           uint32_t dur_ms, tk_ease_fn ease)
{
    slide_in_common(r, w, dx, 0, dur_ms, ease);
}

static void slide_out_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    /* damage the final visual position: the caller's removal
     * damages the restored rect, and without this the last
     * frame's pixels would survive as a remnant strip */
    {
        tk_root *r = tk_widget_root(w);
        if (r) tk_root_invalidate_rect(r, w->rect);
    }
    /* restore the canonical geometry first; the caller's done
     * (which removes the surface) runs on a clean rect */
    if (w->slide_cur != 0) {
        if (w->slide_axis_x) slide_shift(w, -w->slide_cur, 0);
        else slide_shift(w, 0, -w->slide_cur);
        w->slide_cur = 0;
    }
    tk_anim_done_cb d = w->slide_done_cb;
    void *dud = w->slide_done_ud;
    w->slide_done_cb = NULL;
    w->slide_done_ud = NULL;
    tk_widget_invalidate(w);
    if (d) d(dud);
}

void tk_overlay_slide_out(tk_root *r, tk_widget *w, int dx, int dy,
                          uint32_t dur_ms, tk_ease_fn ease,
                          tk_anim_done_cb done, void *ud)
{
    if (!r || !w || (dx == 0 && dy == 0)) { if (done) done(ud); return; }
    if (r->anim_now == 0 || !r->anims_enabled) {
        /* no live clock / reduced motion: close synchronously */
        if (done) done(ud);
        return;
    }
    if (w->anim_handle) { tk_anim_cancel(w->anim_handle); w->anim_handle = NULL; }
    if (w->slide_cur != 0) {
        /* an entrance was interrupted: land it first so the
         * exit starts from the canonical rect */
        slide_damage(w, -w->slide_cur);
        if (w->slide_axis_x) slide_shift(w, -w->slide_cur, 0);
        else slide_shift(w, 0, -w->slide_cur);
        w->slide_cur = 0;
    }
    w->slide_axis_x = dx != 0;
    w->slide_end_x = w->rect.x;
    w->slide_end_y = w->rect.y;
    int off = w->slide_axis_x ? dx : dy;
    w->slide_span = off < 0 ? -off : off;
    w->slide_done_cb = done;
    w->slide_done_ud = ud;
    w->anim_handle = tk_anim_start(r, 0, off, dur_ms, ease,
                                   slide_cb, slide_out_done, w);
    /* NULL handle: the final value and slide_out_done were
     * already delivered synchronously */
}
