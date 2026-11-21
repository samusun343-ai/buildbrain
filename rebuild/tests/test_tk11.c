/* test_tk11.c - M12: the D (animation) viewpoint.
 *
 * Every animation the toolkit wires is verified against the
 * mode's spec pairing, with the values the engine's own
 * integer formula must deliver:
 *
 *   value(t) = from + (int)((to - from) * ease(t) + 0.5f)
 *
 * Sources (fetched 2026-11-20, see REPORT-toolkit-m12):
 * - M3 motion tokens (m3.material.io/styles/motion/easing-
 *   and-duration/tokens-specs): "Selection controls have a
 *   short duration of 200ms with Standard easing"; Standard =
 *   cubic-bezier(0.2, 0, 0, 1), Standard decelerate =
 *   (0, 0, 0, 1), Emphasized decelerate = (0.05, 0.7, 0.1, 1);
 *   durations short4 = 200ms, medium2 = 300ms.
 * - WinUI (Microsoft Learn, Timing and easing / Motion in
 *   Windows): ControlFastAnimationDuration = 167ms,
 *   ControlNormalAnimationDuration = 250ms, Direct Entrance
 *   easing cubic-bezier(0, 0, 0, 1).
 *
 * Pairings under test:
 *   Switch knob      M3 200ms Standard / WinUI 167ms (0,0,0,1)
 *   Dialog, Menu     M3 200ms / WinUI 167ms, (0,0,0,1), in+out
 *   Drawer, Sheet    M3 300ms Emphasized decel / WinUI 250ms
 *   Snackbar (M3)    200ms Standard decelerate entrance;
 *                    the WinUI InfoBar form has NO entrance
 *   Progress sweep   1200ms linear phase, self-restarting
 *
 * The bezier reference below is an independent dense
 * parametric scan - it shares no code with the library's
 * Newton solver, so the comparison is a real cross-check. */
#include "tk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; \
        printf("FAIL %d: %s\n", __LINE__, #cond); } \
} while (0)

static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
}
static tk_color px(tk_canvas *c, int x, int y)
{
    return tk_canvas_pixel(c, x, y);
}
static tk_color blend(tk_color fg, tk_color bg)
{
    int a = fg.a;
    tk_color o;
    o.r = (uint8_t)((fg.r * a + bg.r * (255 - a)) / 255);
    o.g = (uint8_t)((fg.g * a + bg.g * (255 - a)) / 255);
    o.b = (uint8_t)((fg.b * a + bg.b * (255 - a)) / 255);
    o.a = 255;
    return o;
}
/* M13: TK11_DARK=1 re-runs the whole suite with every WinUI
 * selection substituted by tk_theme_winui_dark - the formal
 * dark re-run M12 left undone for this suite (same mechanism
 * as tk10's TK10_DARK). tk11_set is defined BEFORE the macro
 * so its own call reaches the real function. Expected values
 * in this suite are computed from the theme tokens, so they
 * follow the substitution. */
static void tk11_set(const tk_theme *t) { tk_theme_set_default(t); }
static const tk_theme *tk11_map(const tk_theme *t)
{
    if (t == &tk_theme_winui && getenv("TK11_DARK"))
        return &tk_theme_winui_dark;
    return t;
}
#define tk_theme_set_default(t) tk11_set(tk11_map(t))

static tk_root *root_with(tk_widget *w)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_add_child(box, tk_label_new(" "));
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
    tk_add_child(row, tk_label_new("    "));
    tk_add_child(row, w);
    tk_add_child(box, row);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    return r;
}
static tk_canvas *paint_full(tk_root *r)
{
    tk_canvas *c = tk_canvas_new(800, 480);
    tk_root_paint(r, c, tk_theme_default());
    return c;
}
static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = ty; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
static void send_click(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_DOWN, x, y);
    send_ptr(r, TK_EV_POINTER_UP, x, y);
}

/* Independent cubic-bezier reference: dense parametric scan
 * (20001 samples, keep the y whose x is closest to t). The
 * token curves are monotonic in x, so this is well-defined. */
static float ref_bezier(float x1, float y1, float x2, float y2,
                        float t)
{
    float best = 0.0f, bd = 1e9f;
    for (int i = 0; i <= 20000; i++) {
        float u = (float)i / 20000.0f, v = 1.0f - u;
        float x = 3.0f * v * v * u * x1 + 3.0f * v * u * u * x2
                  + u * u * u;
        float y = 3.0f * v * v * u * y1 + 3.0f * v * u * u * y2
                  + u * u * u;
        float d = x > t ? x - t : t - x;
        if (d < bd) { bd = d; best = y; }
    }
    return best;
}
static float ref_standard(float t)
{
    return ref_bezier(0.2f, 0.0f, 0.0f, 1.0f, t);
}
static float ref_std_dec(float t)
{
    return ref_bezier(0.0f, 0.0f, 0.0f, 1.0f, t);
}
static float ref_emph_dec(float t)
{
    return ref_bezier(0.05f, 0.7f, 0.1f, 1.0f, t);
}
/* the engine's exact integer delivery formula */
static int anim_val(int from, int to, float e)
{
    return from + (int)((to - from) * e + 0.5f);
}

/* ---------- A. tk_anim engine contract -------------------- */
static int rec_vals[64];
static int rec_n, rec_done;
static void rec_cb(void *ud, int v)
{
    (void)ud;
    if (rec_n < 64) rec_vals[rec_n] = v;
    rec_n++;
}
static void rec_done_cb(void *ud) { (void)ud; rec_done++; }

static void test_engine(void)
{
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    /* linear progress + exact end + done exactly once */
    rec_n = 0; rec_done = 0;
    tk_root_tick(r, 1000);
    tk_anim *a = tk_anim_start(r, 0, 1000, 200, tk_ease_linear,
                               rec_cb, rec_done_cb, NULL);
    CHECK(a != NULL);
    CHECK(rec_n == 0); /* nothing before the first tick */
    tk_root_tick(r, 1050);
    CHECK(rec_n == 1 && rec_vals[0] == 250);
    tk_root_tick(r, 1100);
    CHECK(rec_n == 2 && rec_vals[1] == 500);
    tk_root_tick(r, 1199);
    CHECK(rec_n == 3 && rec_vals[2] == 995);
    tk_root_tick(r, 1200);
    CHECK(rec_n == 4 && rec_vals[3] == 1000); /* exact final */
    CHECK(rec_done == 1);
    tk_root_tick(r, 1500);
    CHECK(rec_n == 4 && rec_done == 1); /* nothing after done */
    /* wall-clock basis: the value at an absolute time does not
     * depend on how many ticks delivered frames on the way */
    int last_a = -1, last_b = -1, n_a = 0;
    {
        tk_root *ra = root_with(tk_label_new("x"));
        rec_n = 0; rec_done = 0;
        tk_root_tick(ra, 1000);
        tk_anim_start(ra, 0, 1000, 200, tk_ease_linear,
                      rec_cb, rec_done_cb, NULL);
        for (uint32_t t = 1010; t <= 1100; t += 10)
            tk_root_tick(ra, t);
        n_a = rec_n;
        last_a = rec_vals[rec_n - 1];
        tk_root_free(ra);
    }
    {
        tk_root *rb = root_with(tk_label_new("x"));
        rec_n = 0; rec_done = 0;
        tk_root_tick(rb, 1000);
        tk_anim_start(rb, 0, 1000, 200, tk_ease_linear,
                      rec_cb, rec_done_cb, NULL);
        tk_root_tick(rb, 1100);
        last_b = rec_vals[rec_n - 1];
        CHECK(rec_n == 1); /* one frame only */
        tk_root_free(rb);
    }
    CHECK(n_a == 10);
    CHECK(last_a == 500 && last_b == 500);
    /* cancel: no further values, no done */
    rec_n = 0; rec_done = 0;
    tk_root_tick(r, 2000);
    a = tk_anim_start(r, 0, 1000, 200, tk_ease_linear,
                      rec_cb, rec_done_cb, NULL);
    tk_root_tick(r, 2100);
    CHECK(rec_n == 1 && rec_vals[0] == 500);
    tk_anim_cancel(a);
    tk_root_tick(r, 2300);
    CHECK(rec_n == 1 && rec_done == 0);
    /* interruption contract the widgets rely on: a new anim
     * started from the last delivered value continues from
     * there, with the engine's rounding */
    rec_n = 0; rec_done = 0;
    tk_root_tick(r, 3000);
    a = tk_anim_start(r, 0, 1000, 200, tk_ease_linear,
                      rec_cb, rec_done_cb, NULL);
    tk_root_tick(r, 3100);
    CHECK(rec_vals[rec_n - 1] == 500);
    tk_anim_cancel(a);
    tk_anim_start(r, 500, 0, 200, tk_ease_linear,
                  rec_cb, rec_done_cb, NULL);
    tk_root_tick(r, 3150);
    CHECK(rec_vals[rec_n - 1] == 376); /* 500-(int)(125-.5) */
    tk_root_tick(r, 3300);
    CHECK(rec_vals[rec_n - 1] == 0 && rec_done == 1);
    /* reduced motion: final value + done, synchronously */
    tk_root_set_animations_enabled(r, false);
    rec_n = 0; rec_done = 0;
    a = tk_anim_start(r, 0, 1000, 200, tk_ease_linear,
                      rec_cb, rec_done_cb, NULL);
    CHECK(a == NULL);
    CHECK(rec_n == 1 && rec_vals[0] == 1000 && rec_done == 1);
    tk_root_set_animations_enabled(r, true);
    tk_root_free(r);
    tk_widget_free(host);
}

/* ---------- B. easing curves vs the reference ------------- */
static void test_eases(void)
{
    for (int i = 0; i <= 10; i++) {
        float t = i / 10.0f;
        CHECK(fabsf(tk_ease_standard(t) - ref_standard(t))
              < 0.004f);
        CHECK(fabsf(tk_ease_standard_decelerate(t)
                    - ref_std_dec(t)) < 0.004f);
        CHECK(fabsf(tk_ease_emphasized_decelerate(t)
                    - ref_emph_dec(t)) < 0.004f);
    }
    /* endpoints are exact, curves are monotonic */
    CHECK(tk_ease_standard(0.0f) == 0.0f);
    CHECK(tk_ease_standard(1.0f) == 1.0f);
    CHECK(tk_ease_standard_decelerate(0.0f) == 0.0f);
    CHECK(tk_ease_emphasized_decelerate(1.0f) == 1.0f);
    float prev = -1.0f;
    for (int i = 0; i <= 20; i++) {
        float v = tk_ease_standard(i / 20.0f);
        CHECK(v >= prev);
        prev = v;
    }
    /* the pre-M12 cubics keep their closed forms */
    CHECK(fabsf(tk_ease_out_cubic(0.5f) - 0.875f) < 0.0001f);
    CHECK(fabsf(tk_ease_in_out_cubic(0.25f) - 0.0625f)
          < 0.0001f);
}

/* ---------- C. Switch knob (both modes) ------------------- */
/* centre of the knob blob on the track's middle row */
static int knob_centre(tk_root *r, tk_rect q, tk_color target,
                       int tw)
{
    tk_canvas *c = paint_full(r);
    int cy = q.y + q.h / 2;
    int first = -1, last = -1;
    for (int x = q.x; x < q.x + tw; x++)
        if (col_near_c(px(c, x, cy), target, 2)) {
            if (first < 0) first = x;
            last = x;
        }
    tk_canvas_free(c);
    return first < 0 ? -1 : (first + last) / 2;
}
/* M22: the M3 knob's expected centre at anim permille v -
 * the diameter rides knob_pos (18 -> 27px), so the travel
 * (track 61 - insets 8 - diameter) and the centre offset
 * both follow the diameter at this frame */
static int m3_knob_centre(int x, int v)
{
    float pos = v / 1000.0f;
    int skd = 18 + (int)(9.0f * pos + 0.5f);
    return x + 4 + (int)(pos * (61 - 8 - skd) + 0.5f) + skd / 2;
}

static void switch_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 167 : 200;
    tk_widget *sw = tk_switch_new("", false);
    tk_root *r = root_with(sw);
    tk_rect q = tk_widget_rect(sw);
    /* the painted OFF knob: the M3 knob is the opaque surface
     * colour; the WinUI knob is the strong-fill token blended
     * over the painted track fill (itself blended over the
     * page), derived from the tokens like tk9/tk10 do */
    tk_color off_knob = t->surface;
    if (winui) {
        tk_color strong = t->strong_fill.a ? t->strong_fill
                                           : t->text_dim;
        tk_color track = t->control_fill.a
                             ? blend(t->control_fill, t->bg)
                             : t->bg;
        off_knob = blend(strong, track);
    }
    tk_color on_knob = winui ? t->on_accent : t->surface;
    int tw = winui ? 54 : 61; /* M21: M3 track converted;
                               * M22: WinUI track 54 at x1.344 */
    int cx0 = knob_centre(r, q, off_knob, tw);
    CHECK(cx0 > 0);
    tk_root_tick(r, 1000);
    tk_switch_set_on(sw, true);
    /* no tick yet: the knob has not moved */
    CHECK(knob_centre(r, q, on_knob, tw) == cx0);
    tk_root_tick(r, 1000 + dur / 4);
    {
        float e = winui ? ref_std_dec(0.25f) : ref_standard(0.25f);
        int v = anim_val(0, 1000, e);
        int want = winui
            ? q.x + 6 + (int)(v / 1000.0f * 26.0f + 0.5f) + 8
            : m3_knob_centre(q.x, v);
        int got = knob_centre(r, q, on_knob, tw);
        CHECK(abs(got - want) <= 2);
    }
    tk_root_tick(r, 1000 + dur / 2);
    {
        float e = winui ? ref_std_dec(0.5f) : ref_standard(0.5f);
        int v = anim_val(0, 1000, e);
        int want = winui
            ? q.x + 6 + (int)(v / 1000.0f * 26.0f + 0.5f) + 8
            : m3_knob_centre(q.x, v);
        int got = knob_centre(r, q, on_knob, tw);
        CHECK(abs(got - want) <= 2);
    }
    tk_root_tick(r, 1000 + dur);
    int cx1 = knob_centre(r, q, on_knob, tw);
    CHECK(cx1 > cx0);
    tk_root_tick(r, 1000 + dur + 500);
    CHECK(knob_centre(r, q, on_knob, tw) == cx1); /* settled */
    /* reversal mid-flight: the new flight starts from the
     * value the interrupted flight had delivered (the
     * engine's permille), so the position 10ms after the
     * reversal is the engine formula applied from there -
     * for the front-loaded decelerate curve that is a real
     * step, but never a snap to either end */
    tk_root_tick(r, 3000);
    tk_switch_set_on(sw, false);
    tk_root_tick(r, 3000 + dur / 2);
    int mid = knob_centre(r, q, off_knob, tw);
    CHECK(mid > cx0 && mid < cx1);
    {
        float e_half = winui ? ref_std_dec(0.5f)
                             : ref_standard(0.5f);
        int v_mid = anim_val(1000, 0, e_half);
        float e_step = winui ? ref_std_dec(10.0f / dur)
                             : ref_standard(10.0f / dur);
        int v2 = anim_val(v_mid, 1000, e_step);
        int want = winui
            ? q.x + 6 + (int)(v2 / 1000.0f * 26.0f + 0.5f) + 8
            : m3_knob_centre(q.x, v2);
        tk_switch_set_on(sw, true);
        tk_root_tick(r, 3000 + dur / 2 + 10);
        int resumed = knob_centre(r, q, on_knob, tw);
        CHECK(abs(resumed - want) <= 2);
        CHECK(resumed > mid - 2 && resumed < cx1);
    }
    tk_root_tick(r, 3000 + dur / 2 + 10 + dur);
    CHECK(knob_centre(r, q, on_knob, tw) == cx1);
    /* reduced motion: the knob lands synchronously */
    tk_root_set_animations_enabled(r, false);
    tk_switch_set_on(sw, false);
    CHECK(knob_centre(r, q, off_knob, tw) == cx0);
    tk_root_set_animations_enabled(r, true);
    tk_root_free(r);
    tk_widget_free(sw);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_switch_anim(void)
{
    switch_flow(&tk_theme_v2, false);
    switch_flow(&tk_theme_winui, true);
}

/* ---------- D. Dialog slide in/out (both modes) --------- */
static tk_color dialog_face(const tk_theme *t)
{
    return t->style == 1
               ? blend(t->container_high, blend(t->smoke, t->bg))
               : t->surface;
}
static void dialog_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 167 : 200; /* the small pair */
    tk_color face = dialog_face(t);
    /* root A: no tick ever - the canonical rect and the
     * settled title-glyph position */
    tk_rect q0;
    int glyph_top0;
    {
        tk_widget *host = tk_label_new("主");
        tk_root *ra = root_with(host);
        tk_widget *d = tk_dialog_new("確認");
        tk_dialog_show(ra, d);
        q0 = tk_widget_rect(d);
        tk_canvas *c = paint_full(ra);
        glyph_top0 = -1;
        for (int y = q0.y; y < q0.y + q0.h && glyph_top0 < 0; y++)
            for (int x = q0.x + 8; x < q0.x + q0.w - 8; x++)
                if (col_near_c(px(c, x, y), blend(t->text, face),
                               16)) {
                    glyph_top0 = y;
                    break;
                }
        tk_canvas_free(c);
        tk_root_free(ra);
        tk_widget_free(host);
        tk_widget_free(d);
    }
    CHECK(q0.w > 0 && q0.h > 24);
    CHECK(glyph_top0 > q0.y);
    /* root B: the flight */
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    tk_widget *d = tk_dialog_new("確認");
    tk_root_tick(r, 1000);
    tk_dialog_show(r, d);
    tk_rect q = tk_widget_rect(d);
    CHECK(q.y == q0.y + 12); /* entrance starts at +12 */
    CHECK(q.x == q0.x && q.w == q0.w && q.h == q0.h);
    tk_root_tick(r, 1000 + dur / 4);
    q = tk_widget_rect(d);
    CHECK(abs(q.y - (q0.y + anim_val(12, 0, ref_std_dec(0.25f))))
          <= 1);
    /* the title glyphs travel with the face (subtree shift);
     * the scan starts at the face's current top so neither
     * the host page's own glyphs (a near-identical blend over
     * the page bg) nor the face's own top border row (1px
     * above the rect in the WinUI form) can be mistaken for
     * the title */
    {
        tk_canvas *c = paint_full(r);
        int gt = -1;
        for (int y = q.y; y < 480 && gt < 0; y++)
            for (int x = q0.x + 8; x < q0.x + q0.w - 8; x++)
                if (col_near_c(px(c, x, y), blend(t->text, face),
                               16)) {
                    gt = y;
                    break;
                }
        CHECK(abs(gt - (glyph_top0 + (q.y - q0.y))) <= 2);
        tk_canvas_free(c);
    }
    tk_root_tick(r, 1000 + dur);
    q = tk_widget_rect(d);
    CHECK(q.y == q0.y); /* landed exactly */
    /* exit, with a live clock: slides back and is removed
     * only at the end of the flight */
    int pcx = q0.x + q0.w / 2, pcy = q0.y + q0.h - 6;
    tk_root_tick(r, 2000);
    tk_dialog_close(r, d);
    tk_root_tick(r, 2000 + dur / 4);
    q = tk_widget_rect(d);
    CHECK(abs(q.y - (q0.y + anim_val(0, 12, ref_std_dec(0.25f))))
          <= 1);
    {
        tk_canvas *c = paint_full(r);
        CHECK(col_near_c(px(c, pcx, pcy), face, 3)); /* present */
        tk_canvas_free(c);
    }
    tk_root_tick(r, 2000 + dur + 50);
    {
        tk_canvas *c = paint_full(r);
        CHECK(col_near_c(px(c, pcx, pcy), t->bg, 2)); /* gone */
        tk_canvas_free(c);
    }
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(d);
    /* root C: close before any tick after the show - the
     * live-loop gate keeps it synchronous */
    {
        tk_widget *h2 = tk_label_new("主");
        tk_root *rc = root_with(h2);
        tk_widget *d2 = tk_dialog_new("確認");
        tk_root_tick(rc, 1000);
        tk_dialog_show(rc, d2);
        tk_dialog_close(rc, d2);
        tk_canvas *c = paint_full(rc);
        CHECK(col_near_c(px(c, pcx, pcy), t->bg, 2));
        tk_canvas_free(c);
        tk_root_free(rc);
        tk_widget_free(h2);
        tk_widget_free(d2);
    }
    /* root D: reduced motion - the exit is synchronous even
     * with a live clock */
    {
        tk_widget *h3 = tk_label_new("主");
        tk_root *rd = root_with(h3);
        tk_widget *d3 = tk_dialog_new("確認");
        tk_root_tick(rd, 1000);
        tk_dialog_show(rd, d3);
        tk_root_tick(rd, 1500);
        tk_root_set_animations_enabled(rd, false);
        tk_dialog_close(rd, d3);
        tk_canvas *c = paint_full(rd);
        CHECK(col_near_c(px(c, pcx, pcy), t->bg, 2));
        tk_canvas_free(c);
        tk_root_free(rd);
        tk_widget_free(h3);
        tk_widget_free(d3);
    }
    tk_theme_set_default(&tk_theme_v2);
}
static void test_dialog_anim(void)
{
    dialog_flow(&tk_theme_v2, false);
    dialog_flow(&tk_theme_winui, true);
}

/* ---------- E. Menu entrance (both modes) ----------------- */
static void menu_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    uint32_t dur = winui ? 167 : 200; /* the small pair */
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    tk_widget *menu = tk_menu_new();
    tk_menu_add_item(menu, "開く", 1);
    tk_menu_add_item(menu, "閉じる", 2);
    tk_root_tick(r, 1000);
    tk_menu_popup(r, menu, 100, 100);
    CHECK(tk_menu_is_open(r, menu));
    tk_rect q = tk_widget_rect(menu);
    CHECK(q.y == 100 - 6); /* entrance starts at -6 */
    tk_root_tick(r, 1000 + dur / 4);
    q = tk_widget_rect(menu);
    CHECK(abs(q.y - (100 + anim_val(-6, 0, ref_std_dec(0.25f))))
          <= 1);
    tk_root_tick(r, 1000 + dur);
    q = tk_widget_rect(menu);
    CHECK(q.y == 100);
    CHECK(tk_menu_is_open(r, menu));
    /* dismissal is instant (no exit slide): this is why the
     * Menu D cell is graded on the entrance only */
    send_click(r, 700, 400);
    CHECK(!tk_menu_is_open(r, menu));
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(menu);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_menu_anim(void)
{
    menu_flow(&tk_theme_v2, false);
    menu_flow(&tk_theme_winui, true);
}

/* ---------- F. Drawer slide + scrim (both modes) ---------- */
/* The panel's visible run from the left edge, measured as the
 * pixels that differ from the scrim colour currently on
 * screen (sampled at x=750, beyond any panel). One measure
 * for both modes: the WinUI panel is a translucent layer
 * composited over the scrim, so its painted colour moves
 * with the scrim's alpha and cannot be matched against a
 * fixed token colour; the M3 panel is an opaque fill. */
static int panel_run(tk_canvas *c, int row)
{
    tk_color scrim = px(c, 750, row);
    int x = 0;
    while (x < 750 && !col_near_c(px(c, x, row), scrim, 2)) x++;
    return x;
}
static void drawer_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 250 : 300; /* the large pair */
    float (*ease)(float) = winui ? ref_std_dec : ref_emph_dec;
    float e_half = ease(0.5f);
    /* the run misses the M3 panel's rounded far corner at
     * this row (3px); the WinUI panel is square and its 1px
     * border still differs from the scrim, so nothing is
     * missed there */
    int cut = winui ? 0 : 3;
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    tk_widget *dr = tk_drawer_new("メニュー");
    tk_drawer_add_item(dr, "ホーム", 1, NULL, 0, 0);
    tk_drawer_add_item(dr, "設定", 2, NULL, 0, 0);
    tk_root_tick(r, 1000);
    tk_drawer_show(r, dr, true);
    tk_rect q = tk_widget_rect(dr);
    int row = q.y + q.h - 10;
    {
        tk_canvas *c = paint_full(r);
        CHECK(panel_run(c, row) == 0); /* fully off-screen */
        CHECK(col_near_c(px(c, 750, row), t->bg, 2));
        tk_canvas_free(c);
    }
    tk_root_tick(r, 1000 + dur / 2);
    int run_mid;
    {
        tk_canvas *c = paint_full(r);
        run_mid = panel_run(c, row);
        tk_canvas_free(c);
    }
    tk_root_tick(r, 1000 + dur + 100);
    int run_settled;
    {
        tk_canvas *c = paint_full(r);
        run_settled = panel_run(c, row);
        CHECK(col_near_c(px(c, 750, row), blend(t->smoke, t->bg),
                         3));
        tk_canvas_free(c);
    }
    int pw_est = run_settled + cut;
    CHECK(run_settled > 100);
    CHECK(abs(run_mid - (pw_est + anim_val(-pw_est, 0, e_half)))
          <= 6);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(dr);
    /* fresh root for the scrim's mid-flight alpha + the exit,
     * so the clock only moves forward */
    {
        tk_widget *h2 = tk_label_new("主");
        tk_root *r2 = root_with(h2);
        tk_widget *d2 = tk_drawer_new("メニュー");
        tk_drawer_add_item(d2, "ホーム", 1, NULL, 0, 0);
        tk_drawer_add_item(d2, "設定", 2, NULL, 0, 0);
        tk_root_tick(r2, 1000);
        tk_drawer_show(r2, d2, true);
        tk_root_tick(r2, 1000 + dur / 2);
        int off = anim_val(-pw_est, 0, e_half);
        tk_canvas *c = paint_full(r2);
        tk_color sc = t->smoke;
        sc.a = (uint8_t)(sc.a * (pw_est + off) / pw_est);
        CHECK(col_near_c(px(c, 750, row), blend(sc, t->bg), 4));
        tk_canvas_free(c);
        tk_root_tick(r2, 1000 + dur + 100);
        CHECK(tk_drawer_is_shown(r2, d2));
        /* exit with a live clock */
        tk_root_tick(r2, 3000);
        tk_drawer_close(r2, d2);
        tk_root_tick(r2, 3000 + dur / 2);
        c = paint_full(r2);
        CHECK(abs(panel_run(c, row)
                  - (pw_est + anim_val(0, -pw_est, e_half))) <= 6);
        tk_canvas_free(c);
        CHECK(tk_drawer_is_shown(r2, d2)); /* still travelling */
        tk_root_tick(r2, 3000 + dur + 100);
        CHECK(!tk_drawer_is_shown(r2, d2));
        tk_root_free(r2);
        tk_widget_free(h2);
        tk_widget_free(d2);
    }
    tk_theme_set_default(&tk_theme_v2);
}
static void test_drawer_anim(void)
{
    drawer_flow(&tk_theme_v2, false);
    drawer_flow(&tk_theme_winui, true);
}

/* ---------- G. Sheet slide in/out (both modes) ------------ */
static int dismiss_n;
static void on_sheet_dismiss(tk_widget *w, void *ud)
{
    (void)w; (void)ud; dismiss_n++;
}
static tk_rect sheet_canonical(const tk_theme *theme, int edge)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    tk_widget *sh = tk_sheet_new();
    tk_add_child(sh, tk_button_new("中身"));
    tk_sheet_set_edge(sh, edge);
    tk_sheet_show(r, sh, true); /* anim_now == 0: no slide */
    tk_rect q = tk_widget_rect(sh);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(sh);
    return q;
}
static void sheet_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    uint32_t dur = winui ? 250 : 300; /* the large pair */
    float (*ease)(float) = winui ? ref_std_dec : ref_emph_dec;
    float e_half = ease(0.5f);
    tk_rect q0 = sheet_canonical(theme, 0);
    CHECK(q0.w > 0 && q0.h > 0);
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    tk_widget *sh = tk_sheet_new();
    tk_add_child(sh, tk_button_new("中身"));
    tk_sheet_set_on_dismiss(sh, on_sheet_dismiss, NULL);
    dismiss_n = 0;
    tk_root_tick(r, 1000);
    tk_sheet_show(r, sh, true);
    tk_rect q = tk_widget_rect(sh);
    CHECK(q.y == q0.y + q0.h); /* starts fully below */
    tk_root_tick(r, 1000 + dur / 2);
    q = tk_widget_rect(sh);
    CHECK(abs(q.y - (q0.y + anim_val(q0.h, 0, e_half))) <= 2);
    tk_root_tick(r, 1000 + dur + 50);
    q = tk_widget_rect(sh);
    CHECK(q.y == q0.y);
    CHECK(tk_sheet_is_shown(r, sh));
    /* exit with a live clock: on_dismiss fires at the end */
    tk_root_tick(r, 3000);
    tk_sheet_close(r, sh);
    tk_root_tick(r, 3000 + dur / 2);
    q = tk_widget_rect(sh);
    CHECK(abs(q.y - (q0.y + anim_val(0, q0.h, e_half))) <= 2);
    CHECK(dismiss_n == 0);
    CHECK(tk_sheet_is_shown(r, sh));
    tk_root_tick(r, 3000 + dur + 50);
    CHECK(!tk_sheet_is_shown(r, sh));
    CHECK(dismiss_n == 1);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(sh);
    /* end (side) sheet: the same flight on the x axis */
    tk_rect e0 = sheet_canonical(theme, 1);
    {
        tk_widget *h2 = tk_label_new("主");
        tk_root *r2 = root_with(h2);
        tk_widget *s2 = tk_sheet_new();
        tk_add_child(s2, tk_button_new("中身"));
        tk_sheet_set_edge(s2, 1);
        tk_root_tick(r2, 1000);
        tk_sheet_show(r2, s2, true);
        tk_rect q2 = tk_widget_rect(s2);
        CHECK(q2.x == e0.x + e0.w);
        tk_root_tick(r2, 1000 + dur / 2);
        q2 = tk_widget_rect(s2);
        CHECK(abs(q2.x - (e0.x + anim_val(e0.w, 0, e_half)))
              <= 2);
        tk_root_tick(r2, 1000 + dur + 50);
        q2 = tk_widget_rect(s2);
        CHECK(q2.x == e0.x);
        tk_root_free(r2);
        tk_widget_free(h2);
        tk_widget_free(s2);
    }
    /* scrim tap routes through the same animated close */
    {
        tk_widget *h3 = tk_label_new("主");
        tk_root *r3 = root_with(h3);
        tk_widget *s3 = tk_sheet_new();
        tk_add_child(s3, tk_button_new("中身"));
        tk_sheet_set_on_dismiss(s3, on_sheet_dismiss, NULL);
        dismiss_n = 0;
        tk_root_tick(r3, 1000);
        tk_sheet_show(r3, s3, true);
        tk_root_tick(r3, 1000 + dur + 100);
        send_click(r3, 400, 40); /* scrim above the panel */
        CHECK(tk_sheet_is_shown(r3, s3)); /* travelling, not cut */
        tk_root_tick(r3, 1000 + dur + 100 + dur + 50);
        CHECK(!tk_sheet_is_shown(r3, s3));
        CHECK(dismiss_n == 1);
        tk_root_free(r3);
        tk_widget_free(h3);
        tk_widget_free(s3);
    }
    tk_theme_set_default(&tk_theme_v2);
}
static void test_sheet_anim(void)
{
    sheet_flow(&tk_theme_v2, false);
    sheet_flow(&tk_theme_winui, true);
}

/* ---------- H. Progress sweep timing (both modes) --------- */
static int accent_left(tk_root *r, tk_rect q, tk_color accent)
{
    tk_canvas *c = paint_full(r);
    int cy = q.y + q.h / 2, found = -1;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, cy), accent, 2)) {
            found = x;
            break;
        }
    tk_canvas_free(c);
    return found;
}
static int accent_count_row(tk_root *r, tk_rect q, tk_color accent)
{
    tk_canvas *c = paint_full(r);
    int cy = q.y + q.h / 2, n = 0;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, cy), accent, 2)) n++;
    tk_canvas_free(c);
    return n;
}
static void progress_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    bool m3 = t->style == 1;
    tk_widget *p = tk_progress_new(0.0f);
    tk_root *r = root_with(p);
    tk_rect q = tk_widget_rect(p);
    CHECK(q.w > 40);
    int seg = q.w * 2 / 5;
    int travel = q.w + seg;
    tk_root_tick(r, 1000);
    tk_progress_set_indeterminate(p, true); /* sweep from 1000 */
    if (m3) {
        /* M13: the M3 form is the two-bar keyframed cycle
         * (2000 ms; the precise geometry is cross-checked
         * against an independent keyframe evaluation in
         * test_tk12). Here: the cycle is empty at phase 0
         * (both bars off-track), populated mid-cycle, and
         * wraps at 2000 ms - not at the old 1200 ms. */
        CHECK(accent_count_row(r, q, t->accent) == 0);
        tk_root_tick(r, 1500); /* phase 250: secondary enters */
        CHECK(accent_left(r, q, t->accent) == q.x);
        tk_root_tick(r, 2200); /* +1200ms: NOT wrapped yet */
        CHECK(accent_count_row(r, q, t->accent) > 0);
        tk_root_tick(r, 3000); /* +2000ms: wrap - empty again */
        CHECK(accent_count_row(r, q, t->accent) == 0);
        tk_root_tick(r, 3500); /* phase 250 of the 2nd cycle */
        CHECK(accent_left(r, q, t->accent) == q.x);
        tk_root_free(r);
        tk_widget_free(p);
        goto determinate;
    }
    /* phase 500 at +600ms: segment left edge at the formula */
    tk_root_tick(r, 1600);
    int want = q.x - seg + (int)(0.5f * (float)travel);
    CHECK(abs(accent_left(r, q, t->accent) - want) <= 2);
    /* phase 750 at +900ms */
    tk_root_tick(r, 1900);
    want = q.x - seg + (int)(0.75f * (float)travel);
    CHECK(abs(accent_left(r, q, t->accent) - want) <= 2);
    /* wrap at +1200ms: the phase restarts - the segment has
     * fully exited, so the track row is empty ... */
    tk_root_tick(r, 2200);
    CHECK(accent_count_row(r, q, t->accent) == 0);
    /* ... and +100ms later it re-enters flush at the left */
    tk_root_tick(r, 2300);
    CHECK(accent_left(r, q, t->accent) == q.x);
    /* one full period after the phase-500 probe, same place */
    tk_root_tick(r, 2800);
    want = q.x - seg + (int)(0.5f * (float)travel);
    CHECK(abs(accent_left(r, q, t->accent) - want) <= 2);
    tk_root_free(r);
    tk_widget_free(p);
determinate:
    /* determinate: no sweep - the fill is static across ticks */
    {
        tk_widget *p2 = tk_progress_new(0.5f);
        tk_root *r2 = root_with(p2);
        tk_rect q2 = tk_widget_rect(p2);
        tk_root_tick(r2, 1000);
        int l1 = accent_left(r2, q2, t->accent);
        int n1 = accent_count_row(r2, q2, t->accent);
        tk_root_tick(r2, 1700);
        CHECK(accent_left(r2, q2, t->accent) == l1);
        CHECK(accent_count_row(r2, q2, t->accent) == n1);
        CHECK(n1 > q2.w / 3); /* roughly half the track */
        tk_root_free(r2);
        tk_widget_free(p2);
    }
    /* reduced motion: the sweep never starts, the phase is
     * static (an empty track at phase 0) at any tick */
    {
        tk_widget *p3 = tk_progress_new(0.0f);
        tk_root *r3 = root_with(p3);
        tk_rect q3 = tk_widget_rect(p3);
        tk_root_set_animations_enabled(r3, false);
        tk_root_tick(r3, 1000);
        tk_progress_set_indeterminate(p3, true);
        tk_root_tick(r3, 1600);
        int n = accent_count_row(r3, q3, t->accent);
        tk_root_tick(r3, 2600);
        CHECK(accent_count_row(r3, q3, t->accent) == n);
        tk_root_free(r3);
        tk_widget_free(p3);
    }
    tk_theme_set_default(&tk_theme_v2);
}
static void test_progress_anim(void)
{
    progress_flow(&tk_theme_v2);
    progress_flow(&tk_theme_winui);
}

/* ---------- I. Snackbar entrance (M3) / none (WinUI) ------ */
static int bar_top(tk_root *r, tk_color face)
{
    tk_canvas *c = paint_full(r);
    int top = -1;
    for (int y = 0; y < 480 && top < 0; y++)
        for (int x = 24; x < 100; x++)
            if (col_near_c(px(c, x, y), face, 2)) {
                top = y;
                break;
            }
    tk_canvas_free(c);
    return top;
}
static void test_snackbar_anim(void)
{
    /* M3 form: 200ms Standard-decelerate entrance slide */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    int top0, span;
    {
        tk_widget *host = tk_label_new("主");
        tk_root *ra = root_with(host);
        tk_snackbar_show(ra, "保存しました", NULL, NULL, NULL,
                         60000);
        top0 = bar_top(ra, t->inverse_surface);
        CHECK(top0 > 0);
        tk_canvas *c = paint_full(ra);
        int bot = -1;
        for (int y = 479; y >= 0 && bot < 0; y--)
            for (int x = 24; x < 100; x++)
                if (col_near_c(px(c, x, y), t->inverse_surface,
                               2)) {
                    bot = y;
                    break;
                }
        tk_canvas_free(c);
        span = (bot - top0 + 1) + 16;
        tk_root_free(ra);
        tk_widget_free(host);
    }
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_root_tick(r, 1000);
        tk_snackbar_show(r, "保存しました", NULL, NULL, NULL,
                         60000);
        /* at the start the bar is fully below the screen */
        CHECK(bar_top(r, t->inverse_surface) == -1);
        tk_root_tick(r, 1050);
        CHECK(abs(bar_top(r, t->inverse_surface)
                  - (top0 + anim_val(span, 0, ref_std_dec(0.25f))))
              <= 3);
        tk_root_tick(r, 1100);
        CHECK(abs(bar_top(r, t->inverse_surface)
                  - (top0 + anim_val(span, 0, ref_std_dec(0.5f))))
              <= 3);
        tk_root_tick(r, 1200);
        CHECK(bar_top(r, t->inverse_surface) == top0);
        tk_root_free(r);
        tk_widget_free(host);
    }
    /* WinUI form (InfoBar): no entrance motion - the canvas
     * is pixel-identical before and after the entrance window */
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_root_tick(r, 1000);
        tk_snackbar_show_sev(r, "保存しました", TK_SNACK_INFO,
                             NULL, NULL, NULL, 60000);
        CHECK(tk_snackbar_visible(r));
        tk_canvas *c1 = paint_full(r);
        tk_root_tick(r, 2000);
        tk_canvas *c2 = paint_full(r);
        CHECK(memcmp(tk_canvas_pixels(c1), tk_canvas_pixels(c2),
                     800 * 480 * sizeof(uint32_t)) == 0);
        tk_canvas_free(c1);
        tk_canvas_free(c2);
        tk_root_free(r);
        tk_widget_free(host);
    }
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    test_engine();
    test_eases();
    test_switch_anim();
    test_dialog_anim();
    test_menu_anim();
    test_drawer_anim();
    test_sheet_anim();
    test_progress_anim();
    test_snackbar_anim();
    printf("tk11: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
