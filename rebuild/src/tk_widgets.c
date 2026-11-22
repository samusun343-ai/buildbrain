/* tk_widgets.c - leaf widgets + per-kind measure/paint/event.
 * Containers are handled in tk_core.c. Widgets hold real state and
 * consume events; nothing here is a draw-only function.
 * M2 widgets live at the bottom of this file (tk_w2_* dispatch). */
#define _DEFAULT_SOURCE 1 /* dirent d_type (file dialog, M5) */
#include "tk_internal.h"
#include <math.h>
#include <stdio.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

/* M2 dispatch (implemented at the bottom of this file) */
tk_size tk_w2_measure(tk_widget *w, tk_constraints c);
void tk_w2_paint(tk_widget *w, tk_canvas *c, const tk_theme *t);
bool tk_w2_event(tk_widget *w, tk_root *root, tk_event *ev);
/* M5 pickers (bottom of file) */
#define TK_PK_STEPPER_W 28
int tk_pk_nfields(const tk_widget *w);
int tk_pk_field_value(const tk_widget *w, int f);
void tk_pk_cell_rect(const tk_widget *w, int f, tk_rect *out);
tk_rect tk_pk_stepper_rect(const tk_widget *w);
void tk_pk_draw_tri(tk_canvas *c, int cx, int cy, int dir, tk_color col);
void tk_pk_step(tk_widget *w, int dir);
/* shared: byte offset of the caret nearest to pixel x in a string */
size_t tk_offset_at_x(const tk_theme *t, const char *utf8, int x_px);
/* TextView helpers (bottom of file), used by the shared editor keys */
static bool tv_move_vertical(tk_widget *w, int dir, bool extend);
static void tv_ensure_visible(tk_widget *w);

/* The theme used for measure (widgets do not own a theme pointer in
 * M1; the root passes the theme at paint time, and measure uses the
 * default theme's metrics). */
static const tk_theme *metrics(void)
{
    /* M8 fix: this used to cache the first default theme for
     * the life of the process, so tk_theme_set_default() - the
     * documented mode switch - silently stopped affecting every
     * leaf measure after the first one. The two-mode requirement
     * switches themes at runtime; read the current default. */
    return tk_theme_default();
}

/* Style-aware text helpers (UI-REDESIGN P1-2): a widget's text is
 * measured and drawn at its text_style's size/weight, never at a
 * size hard-coded by the widget. */
int tk_w_text_w(const tk_widget *w, const tk_theme *t, const char *s)
{
    return tk_text_width_st(t, tk_widget_text_style(w), s);
}
int tk_w_text_lh(const tk_widget *w, const tk_theme *t)
{
    return tk_line_height_st(t, tk_widget_text_style(w));
}
void tk_w_draw_text(tk_canvas *c, const tk_widget *w, const tk_theme *t,
                    int x, int y, const char *s, tk_color col)
{
    tk_canvas_draw_text_st(c, t, tk_widget_text_style(w), x, y, s, col);
}

/* Copy `s` into buf, ellipsised so its rendered width at w's
 * text style fits max_w px. Icon cells (desktop / launcher /
 * results grids) have a fixed cell width; painting a long name
 * centred and untruncated overflows the cell on both sides and
 * bleeds across neighbouring cells and panel edges (observed:
 * "NetSurf Web Browser" in a 76px launcher cell). Real launchers
 * ellipsize icon labels to the cell, so the paint does too.
 * Truncation lands on a UTF-8 codepoint boundary. */
static void fit_label(char *buf, size_t bufsz, const tk_widget *w,
                      const tk_theme *t, const char *s, int max_w)
{
    if (!s) { buf[0] = 0; return; }
    if (tk_w_text_w(w, t, s) <= max_w) {
        snprintf(buf, bufsz, "%s", s);
        return;
    }
    static const char ell[] = "\xE2\x80\xA6"; /* U+2026 */
    int ew = tk_w_text_w(w, t, ell);
    size_t best = 0, i = 0;
    char tmp[512];
    while (s[i]) {
        size_t j = i + 1;
        while (s[j] && (s[j] & 0xC0) == 0x80) j++; /* UTF-8 tail */
        if (j >= sizeof(tmp)) break;
        memcpy(tmp, s, j);
        tmp[j] = 0;
        if (tk_w_text_w(w, t, tmp) + ew > max_w) break;
        best = j;
        i = j;
    }
    if (best + sizeof(ell) > bufsz) best = bufsz - sizeof(ell);
    memcpy(buf, s, best);
    memcpy(buf + best, ell, sizeof(ell));
}

/* ---------------------------------------------------------------- */
/* Constructors                                                      */
/* ---------------------------------------------------------------- */

static tk_widget *leaf(tk_widget_kind k, const char *text, bool focusable)
{
    tk_widget *w = tk_widget_new(k);
    if (!w) return NULL;
    w->text = tk_strdup(text);
    w->can_focus = focusable;
    return w;
}

/* Event sink state (TK_W_SINK): held in w->ext, released by the
 * core's generic ext free. The sink measures 0x0 (the measure and
 * paint defaults) and never takes focus; only its leaf-event case
 * does anything. */
typedef struct {
    tk_event_cb cb;
    void *ud;
} tk_sink_state;

tk_widget *tk_sink_new(tk_event_cb cb, void *ud)
{
    tk_widget *w = tk_widget_new(TK_W_SINK);
    if (!w) return NULL;
    tk_sink_state *sk = calloc(1, sizeof(*sk));
    if (!sk) { tk_widget_free(w); return NULL; }
    sk->cb = cb;
    sk->ud = ud;
    w->ext = sk;
    w->can_focus = false;
    return w;
}

tk_widget *tk_label_new(const char *text) { return leaf(TK_W_LABEL, text, false); }
void tk_label_set_align(tk_widget *w, int align)
{
    if (!w) return;
    w->label_align = align;
    tk_widget_invalidate(w);
}

void tk_label_set_text(tk_widget *w, const char *t)
{
    if (!w) return;
    if (!w->text && !t) return;
    if (w->text && t && strcmp(w->text, t) == 0) return;
    free(w->text); w->text = tk_strdup(t);
    tk_widget_invalidate(w); /* M5: setters auto-invalidate */
}
const char *tk_label_text(const tk_widget *w) { return w->text; }

tk_widget *tk_button_new(const char *label) { return leaf(TK_W_BUTTON, label, true); }
void tk_button_set_on_click(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}

void tk_button_set_icon(tk_widget *w, const uint32_t *px, int iw, int ih)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    free(w->image_px);
    w->image_px = NULL; w->image_w = 0; w->image_h = 0;
    if (px && iw > 0 && ih > 0) {
        uint32_t *copy = malloc((size_t)iw * (size_t)ih * sizeof(uint32_t));
        if (!copy) return;
        memcpy(copy, px, (size_t)iw * (size_t)ih * sizeof(uint32_t));
        w->image_px = copy; w->image_w = iw; w->image_h = ih;
    }
    tk_widget_invalidate(w);
}

bool tk_button_has_icon(const tk_widget *w)
{
    return w && w->kind == TK_W_BUTTON && w->image_px != NULL;
}

void tk_widget_set_text_color(tk_widget *w, tk_color c)
{
    if (!w) return;
    w->fg_override = c;
    w->has_fg_override = true;
    tk_widget_invalidate(w);
}

void tk_widget_clear_text_color(tk_widget *w)
{
    if (!w) return;
    w->has_fg_override = false;
    tk_widget_invalidate(w);
}

/* Nearest-neighbour blit of canvas-packed pixels into a dest rect
 * (the ImageView scaling rule, shared with the button icon). */
static void blit_image(tk_canvas *c, const uint32_t *px, int iw, int ih,
                       tk_rect d)
{
    if (!px || iw <= 0 || ih <= 0 || d.w <= 0 || d.h <= 0) return;
    for (int dy = 0; dy < d.h; dy++) {
        int sy = dy * ih / d.h;
        for (int dx = 0; dx < d.w; dx++) {
            int sx = dx * iw / d.w;
            uint32_t v = px[(size_t)sy * iw + sx];
            tk_color col = { (uint8_t)(v >> 16), (uint8_t)(v >> 8),
                             (uint8_t)v, (uint8_t)(v >> 24) };
            if (col.a == 255) tk_canvas_put_pixel(c, d.x + dx, d.y + dy, col);
            else if (col.a) tk_canvas_blend_pixel(c, d.x + dx, d.y + dy, col);
        }
    }
}
const char *tk_button_label(const tk_widget *w) { return w->text; }
void tk_button_set_label(tk_widget *w, const char *label)
{
    if (!w) return;
    if (!w->text && !label) return;
    if (w->text && label && strcmp(w->text, label) == 0) return;
    free(w->text);
    w->text = tk_strdup(label);
    /* post-M3 setters auto-invalidate (DESIGN SS13); the older
     * setters keep their historical no-invalidate behaviour. */
    tk_widget_invalidate(w);
}

void tk_button_set_accent(tk_widget *w, bool accent)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->accent_btn != accent) {
        w->accent_btn = accent;
        tk_widget_invalidate(w);
    }
}

void tk_button_set_flat(tk_widget *w, bool flat)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->btn_flat != flat) { w->btn_flat = flat; tk_widget_invalidate(w); }
}

void tk_button_set_icon_only(tk_widget *w, bool icon_only)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->btn_icon_only != icon_only) {
        w->btn_icon_only = icon_only;
        tk_widget_invalidate(w);
    }
}

void tk_button_set_face(tk_widget *w, const tk_color *face)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    bool has = face != NULL;
    if (w->btn_has_face == has &&
        (!has || (w->btn_face.r == face->r && w->btn_face.g == face->g &&
                  w->btn_face.b == face->b && w->btn_face.a == face->a)))
        return;
    w->btn_has_face = has;
    if (has) w->btn_face = *face;
    tk_widget_invalidate(w);
}

void tk_button_set_indicator(tk_widget *w, const tk_color *color)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    bool has = color != NULL;
    if (w->btn_has_indicator == has &&
        (!has || (w->btn_indicator.r == color->r &&
                  w->btn_indicator.g == color->g &&
                  w->btn_indicator.b == color->b &&
                  w->btn_indicator.a == color->a)))
        return;
    w->btn_has_indicator = has;
    if (has) w->btn_indicator = *color;
    tk_widget_invalidate(w);
}

void tk_button_set_corner_radius(tk_widget *w, int radius)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->btn_radius != radius) {
        w->btn_radius = radius;
        tk_widget_invalidate(w);
    }
}

/* --- M6: button variants, icon layout, text alignment, FAB --- */

void tk_button_set_variant(tk_widget *w, tk_button_variant v)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->btn_variant != (int)v) {
        w->btn_variant = (int)v;
        tk_widget_invalidate(w);
    }
}

tk_button_variant tk_button_variant_of(const tk_widget *w)
{
    return w ? (tk_button_variant)w->btn_variant : TK_BTN_LEGACY;
}

/* M7: toggle buttons - the checked state lives in `checked`
 * (shared with the checkbox, a different kind); a click flips
 * it BEFORE on_click fires (see the button event case). */
void tk_button_set_toggle(tk_widget *w, bool toggle)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->btn_toggle != toggle) {
        w->btn_toggle = toggle;
        tk_widget_invalidate(w);
    }
}
bool tk_button_is_toggle(const tk_widget *w)
{
    return w && w->kind == TK_W_BUTTON && w->btn_toggle;
}
bool tk_button_toggled(const tk_widget *w)
{
    return w && w->kind == TK_W_BUTTON && w->btn_toggle &&
           w->checked;
}
void tk_button_set_toggled(tk_widget *w, bool on)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->checked != on) {
        w->checked = on;
        tk_widget_invalidate(w);
    }
}

void tk_button_set_icon_layout(tk_widget *w, tk_button_icon_layout l)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->btn_icon_layout != (int)l) {
        w->btn_icon_layout = (int)l;
        tk_widget_invalidate(w);
    }
}

tk_button_icon_layout tk_button_icon_layout_of(const tk_widget *w)
{
    return w ? (tk_button_icon_layout)w->btn_icon_layout
             : TK_BTN_ICON_CELL;
}

void tk_button_set_text_align(tk_widget *w, int align)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (align < 0) align = 0;
    if (align > 2) align = 2;
    if (w->btn_text_align != align) {
        w->btn_text_align = align;
        tk_widget_invalidate(w);
    }
}

int tk_button_text_align(const tk_widget *w)
{
    return w ? w->btn_text_align : 1;
}

tk_widget *tk_fab_new(const uint32_t *icon_px, int iw, int ih, int size)
{
    tk_widget *w = tk_button_new("");
    if (!w) return NULL;
    if (size <= 0) size = TK_FAB_DEFAULT_SIZE;
    tk_button_set_variant(w, TK_BTN_FILLED);
    tk_button_set_icon_only(w, true);
    if (icon_px) tk_button_set_icon(w, icon_px, iw, ih);
    /* M13: the M3 FAB shape is a rounded square, not a circle:
     * corner radius 16dp at the regular size (12dp small,
     * 28dp large) = 19px (14 / 33) at this panel's scale. The
     * face roles are the FAB's own (see accent_button_face). */
    w->btn_fab = true;
    tk_button_set_corner_radius(w, size <= 48 ? 14
                                   : size >= 100 ? 33 : 19);
    tk_widget_set_min_size(w, size, size);
    return w;
}

void tk_widget_set_backdrop(tk_widget *w, bool floating, int radius)
{
    if (!w) return;
    w->backdrop = true;
    w->backdrop_shadow = floating;
    w->backdrop_radius = radius;
    tk_widget_invalidate(w);
}

void tk_widget_clear_backdrop(tk_widget *w)
{
    if (!w) return;
    if (w->backdrop) { w->backdrop = false; tk_widget_invalidate(w); }
}

void tk_widget_set_backdrop_face(tk_widget *w, const tk_color *col)
{
    if (!w) return;
    bool had = w->backdrop_has_face;
    if (col) {
        if (had && w->backdrop_face.r == col->r &&
            w->backdrop_face.g == col->g &&
            w->backdrop_face.b == col->b &&
            w->backdrop_face.a == col->a)
            return;
        w->backdrop_face = *col;
        w->backdrop_has_face = true;
    } else {
        if (!had) return;
        w->backdrop_has_face = false;
    }
    tk_widget_invalidate(w);
}

void tk_widget_set_text_shadow(tk_widget *w, bool shadow)
{
    if (!w) return;
    if (w->text_shadow != shadow) {
        w->text_shadow = shadow;
        tk_widget_invalidate(w);
    }
}
bool tk_button_is_accent(const tk_widget *w)
{
    return w && w->kind == TK_W_BUTTON && w->accent_btn;
}

/* M13: the selection-glyph transition (checkbox mark, radio
 * dot), wired like the Switch knob: the logical state flips
 * immediately, the DISPLAYED glyph fraction (glyph_pos, in
 * permille inside the animation engine) eases over the mode's
 * selection pair (M3 200 ms Standard / WinUI 167 ms Direct
 * Entrance - tk_anim_selection_pair). Without a live
 * animation clock the display snaps, as the Switch does. */
static void glyph_anim_cb(void *ud, int permille)
{
    tk_widget *w = ud;
    w->glyph_pos = permille / 1000.0f;
    tk_widget_invalidate(w);
}
static void glyph_anim_done(void *ud)
{
    ((tk_widget *)ud)->anim_handle = NULL;
}
static void glyph_set(tk_widget *w, bool on)
{
    tk_root *r = tk_widget_root(w);
    if (w->anim_handle) {
        tk_anim_cancel(w->anim_handle);
        w->anim_handle = NULL;
    }
    int from = (int)(w->glyph_pos * 1000.0f + 0.5f);
    int to = on ? 1000 : 0;
    if (!r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r) || from == to) {
        w->glyph_pos = on ? 1.0f : 0.0f;
        return;
    }
    uint32_t dur; tk_ease_fn ease;
    tk_anim_selection_pair(&dur, &ease);
    w->anim_handle = tk_anim_start(r, from, to, dur, ease,
                                   glyph_anim_cb, glyph_anim_done,
                                   w);
    if (!w->anim_handle) w->glyph_pos = on ? 1.0f : 0.0f;
}

/* M16: the nav-indicator transition shared by the BottomBar
 * and the NavRail. The logical selection flips immediately;
 * the DISPLAYED indicator position (ind_pos, in item units
 * inside the animation engine's permille) eases to it over
 * the mode's selection pair (tk_anim_selection_pair: M3
 * 200 ms Standard / WinUI 167 ms Direct Entrance). The
 * renderers differ per mode - the M3 form crossfades the
 * indicator between the two items, the WinUI form slides
 * it - both driven by this one position. Without a live
 * animation clock the display snaps, as the Switch knob
 * and the selection glyph do. */
static void ind_anim_cb(void *ud, int permille)
{
    tk_widget *w = ud;
    w->ind_pos = permille / 1000.0f;
    tk_widget_invalidate(w);
}
static void ind_anim_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    tk_widget_invalidate(w);
}
void tk_ind_anim_to(tk_widget *w, int to_idx)
{
    tk_root *r = tk_widget_root(w);
    if (w->anim_handle) {
        tk_anim_cancel(w->anim_handle);
        w->anim_handle = NULL;
    }
    int from = (int)(w->ind_pos * 1000.0f + 0.5f);
    int to = to_idx * 1000;
    if (!r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r) || from == to || to_idx < 0) {
        w->ind_pos = to_idx < 0 ? 0.0f : (float)to_idx;
        return;
    }
    uint32_t dur; tk_ease_fn ease;
    tk_anim_selection_pair(&dur, &ease);
    w->anim_handle = tk_anim_start(r, from, to, dur, ease,
                                   ind_anim_cb, ind_anim_done, w);
    if (!w->anim_handle) w->ind_pos = (float)to_idx;
}

/* ---------------------------------------------------------------- */
/* M17: the M3 ripple (Android mode) + the WinUI brush transition.
 *
 * Ripple geometry/timing follow the reference implementation
 * (androidx material-ripple RippleAnimation, fetched
 * 2026-11-20): on press a disc grows from the touch point -
 * radius from 0.3 x the largest dimension (the spec's 60%
 * diameter) to half the diagonal + 10dp (12px at the device
 * scale), FastOutSlowIn cubic-bezier(0.4, 0, 0.2, 1) over
 * 225 ms; the centre drifts from the touch point to the
 * component centre over the same 225 ms (linear); the alpha
 * reaches the pressed state-layer opacity (10%, the colour's
 * own alpha at the paint sites) over 75 ms (linear). On
 * release the disc fades out over 150 ms (linear); a release
 * during the expansion shows the full alpha for the rest of
 * the expansion first (the reference's draw-time rule), then
 * fades. The disc is clipped to the component's rounded rect
 * (bounded ripple). At full expansion the disc covers the
 * whole shape, so a held press is pixel-identical to the
 * uniform pressed layer it replaces; with no live animation
 * clock the ripple lands in that final form synchronously,
 * exactly like the toolkit's other animations.
 *
 * The brush transition is the WinUI button family's only
 * timed state animation (Button / ToggleButton /
 * AppBarButton themeresources, fetched 2026-11-20: state
 * changes are discrete keyframes except the
 * ContentPresenter's BackgroundTransition, a BrushTransition
 * of 83 ms): the DISPLAYED background colour blends linearly
 * to the new state colour over 83 ms. Foreground and border
 * keep switching instantly, as in the template. */

static int m17_f2i(float x)
{
    return (int)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

static void ripple_fade_done(void *ud)
{
    tk_widget *w = ud;
    w->ripple_anim = NULL;
    w->ripple_on = false;
    w->ripple_released = false;
    w->ripple_alpha = 0;
    tk_widget_invalidate(w);
}

static void ripple_fade_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->ripple_alpha = v;
    tk_widget_invalidate(w);
}

static void ripple_start_fade(tk_widget *w)
{
    tk_root *r = tk_widget_root(w);
    if (!r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r)) {
        w->ripple_on = false;
        w->ripple_released = false;
        w->ripple_alpha = 0;
        tk_widget_invalidate(w);
        return;
    }
    w->ripple_anim = tk_anim_start(r, w->ripple_alpha, 0, 150,
                                   tk_ease_linear, ripple_fade_cb,
                                   ripple_fade_done, w);
    /* a NULL handle means the fade was delivered
     * synchronously (ripple_fade_done already ran) */
}

static void ripple_expand_done(void *ud)
{
    tk_widget *w = ud;
    w->ripple_anim = NULL;
    w->ripple_cx = w->ripple_tx;
    w->ripple_cy = w->ripple_ty;
    w->ripple_radius = w->ripple_r1;
    w->ripple_alpha = 256;
    tk_widget_invalidate(w);
    if (!w->pressed) ripple_start_fade(w);
}

static void ripple_expand_cb(void *ud, int v)
{
    tk_widget *w = ud;
    float f = v / 1000.0f;
    /* FastOutSlowIn = cubic-bezier(0.4, 0, 0.2, 1) */
    float ef = tk_bezier_y(0.4f, 0.0f, 0.2f, 1.0f, f);
    w->ripple_radius = w->ripple_r0 +
        m17_f2i((float)(w->ripple_r1 - w->ripple_r0) * ef);
    w->ripple_cx = w->ripple_ox +
        m17_f2i((float)(w->ripple_tx - w->ripple_ox) * f);
    w->ripple_cy = w->ripple_oy +
        m17_f2i((float)(w->ripple_ty - w->ripple_oy) * f);
    /* alpha: 75 ms of the 225 ms expansion, linear; an early
     * release pins it to full (the reference's draw rule) */
    int a = m17_f2i(256.0f * f * 3.0f);
    if (a > 256) a = 256;
    if (w->ripple_released) a = 256;
    w->ripple_alpha = a;
    tk_widget_invalidate(w);
}

void tk_ripple_press(tk_widget *w, tk_root *r, int x, int y)
{
    if (!w || !w->enabled) return;
    const tk_theme *t = tk_theme_default();
    if (!t || t->style != 1) return; /* M3 mode only */
    if (w->ripple_anim) {
        tk_anim_cancel(w->ripple_anim);
        w->ripple_anim = NULL;
    }
    int W = w->rect.w, H = w->rect.h;
    int largest = W > H ? W : H;
    w->ripple_r0 = m17_f2i((float)largest * 0.3f);
    w->ripple_r1 = m17_f2i(sqrtf((float)(W * W + H * H)) * 0.5f)
                   + 12; /* + 10dp at the device scale */
    w->ripple_ox = x;
    w->ripple_oy = y;
    w->ripple_tx = w->rect.x + W / 2;
    w->ripple_ty = w->rect.y + H / 2;
    w->ripple_on = true;
    w->ripple_released = false;
    if (!r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r)) {
        /* no live clock: the final form, synchronously */
        w->ripple_static = true;
        w->ripple_cx = w->ripple_tx;
        w->ripple_cy = w->ripple_ty;
        w->ripple_radius = w->ripple_r1;
        w->ripple_alpha = 256;
        return;
    }
    w->ripple_static = false;
    w->ripple_cx = x;
    w->ripple_cy = y;
    w->ripple_radius = w->ripple_r0;
    w->ripple_alpha = 0;
    w->ripple_anim = tk_anim_start(r, 0, 1000, 225,
                                   tk_ease_linear,
                                   ripple_expand_cb,
                                   ripple_expand_done, w);
    /* a NULL handle means the expansion was delivered
     * synchronously (ripple_expand_done already ran) */
}

void tk_ripple_release(tk_widget *w, tk_root *r)
{
    (void)r;
    if (!w || !w->ripple_on) return;
    if (w->ripple_static) {
        w->ripple_on = false;
        w->ripple_alpha = 0;
        tk_widget_invalidate(w);
        return;
    }
    w->ripple_released = true;
    if (!w->ripple_anim)
        ripple_start_fade(w); /* held at full expansion */
    /* else the expansion is still running: its done hook
     * starts the fade (the reference finishes the expansion
     * first) */
}

/* The component's corner radii for the ripple clip: the
 * split button's asymmetric M3 corners (the same selection
 * btn_face_fill makes), the uniform radius otherwise. */
static void ripple_radii(const tk_widget *w, int rad, int out[4])
{
    out[0] = out[1] = out[2] = out[3] = rad;
    if (w->btn_split_side) {
        int in = w->btn_split_inner;
        if (w->btn_split_side == 1) {
            out[0] = rad; out[1] = in; out[2] = in; out[3] = rad;
        } else {
            out[0] = in; out[1] = rad; out[2] = rad; out[3] = in;
        }
    }
}

void tk_ripple_circle(tk_widget *w, tk_canvas *c, tk_rect r,
                      int rad, tk_color layer)
{
    if (!w->ripple_on || w->ripple_alpha <= 0) return;
    tk_color col = layer;
    col.a = (uint8_t)((int)layer.a * w->ripple_alpha / 256);
    if (!col.a) return;
    int rr[4];
    ripple_radii(w, rad, rr);
    tk_canvas_fill_circle_rrect4(c, r, rr[0], rr[1], rr[2],
                                 rr[3], (float)w->ripple_cx,
                                 (float)w->ripple_cy,
                                 (float)w->ripple_radius, col);
}

void tk_ripple_paint(tk_widget *w, tk_canvas *c, tk_rect r,
                     int rad, tk_color layer)
{
    if (!w->ripple_on) {
        /* no ripple running: the uniform pressed layer, the
         * pre-M17 form (btn_face_fill's shape selection) */
        if (w->btn_split_side) {
            int rr[4];
            ripple_radii(w, rad, rr);
            tk_canvas_fill_round_rect4(c, r, rr[0], rr[1],
                                       rr[2], rr[3], layer);
        } else {
            tk_canvas_fill_round_rect(c, r, rad, layer);
        }
        return;
    }
    tk_ripple_circle(w, c, r, rad, layer);
}

/* ---- WinUI brush transition (83 ms) ---- */

static bool m17_color_eq(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static tk_color m17_color_blend(tk_color a, tk_color b, int p)
{
    tk_color o;
    o.r = (uint8_t)(a.r + (b.r - a.r) * p / 1000);
    o.g = (uint8_t)(a.g + (b.g - a.g) * p / 1000);
    o.b = (uint8_t)(a.b + (b.b - a.b) * p / 1000);
    o.a = (uint8_t)(a.a + (b.a - a.a) * p / 1000);
    return o;
}

static void face_blend_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->face_blend = v;
    tk_widget_invalidate(w);
}

static void face_blend_done(void *ud)
{
    tk_widget *w = ud;
    w->face_anim = NULL;
    w->face_blend = 1000;
    tk_widget_invalidate(w);
}

tk_color tk_face_brush(tk_widget *w, const tk_theme *t,
                       tk_color target)
{
    if (!w || !t || t->style != 2) return target;
    tk_root *r = tk_widget_root(w);
    if (!r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r)) {
        w->face_from = w->face_to = target;
        w->face_blend = 1000;
        w->face_init = true;
        return target;
    }
    if (!w->face_init) {
        /* the first displayed brush is set, not transitioned
         * to (the template's initial state) */
        w->face_init = true;
        w->face_from = w->face_to = target;
        w->face_blend = 1000;
        return target;
    }
    if (!m17_color_eq(target, w->face_to)) {
        tk_color disp = m17_color_blend(w->face_from, w->face_to,
                                         w->face_blend);
        if (w->face_anim) {
            tk_anim_cancel(w->face_anim);
            w->face_anim = NULL;
        }
        w->face_from = disp;
        w->face_to = target;
        w->face_blend = 0;
        w->face_anim = tk_anim_start(r, 0, 1000, 83,
                                     tk_ease_linear,
                                     face_blend_cb,
                                     face_blend_done, w);
        if (!w->face_anim) { /* delivered synchronously */
            w->face_blend = 1000;
            return target;
        }
    }
    return m17_color_blend(w->face_from, w->face_to,
                            w->face_blend);
}

/* M17: scale a colour's alpha by a 0..255 factor (the menu
 * fade's per-token application). */
static tk_color m17_alpha(tk_color col, int alpha)
{
    if (alpha >= 255) return col;
    col.a = (uint8_t)((int)col.a * alpha / 255);
    return col;
}

tk_widget *tk_checkbox_new(const char *label, bool checked)
{
    tk_widget *w = leaf(TK_W_CHECKBOX, label, true);
    if (w) {
        w->checked = checked;
        w->glyph_pos = checked ? 1.0f : 0.0f;
    }
    return w;
}
bool tk_checkbox_checked(const tk_widget *w) { return w->checked; }
void tk_checkbox_set_checked(tk_widget *w, bool v)
{
    if (!w || w->checked == v) return;
    w->checked = v;
    glyph_set(w, v); /* M13: glyph transition */
    tk_widget_invalidate(w); /* M5 */
}
void tk_checkbox_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}

tk_widget *tk_radio_new(const char *label, int group_id, int value)
{
    tk_widget *w = leaf(TK_W_RADIO, label, true);
    if (w) { w->group_id = group_id; w->value = value; }
    return w;
}
int tk_radio_value(const tk_widget *w) { return w->value; }
bool tk_radio_selected(const tk_widget *w) { return w->selected; }

void tk_radio_select(tk_widget *w)
{
    if (!w) return;
    /* M5: invalidate exactly the radios whose state flips; the
     * change callback contract is unchanged (it fires even when
     * re-selecting the current radio). */
    bool changed = !w->selected;
    if (w->parent) {
        for (size_t i = 0; i < w->parent->n_children; i++) {
            tk_widget *s = w->parent->children[i];
            if (s->kind == TK_W_RADIO && s->group_id == w->group_id &&
                s->selected) {
                s->selected = false;
                glyph_set(s, false); /* M13: dot shrinks out */
                tk_widget_invalidate(s);
            }
        }
    }
    w->selected = true;
    if (changed) {
        glyph_set(w, true); /* M13: dot grows in */
        tk_widget_invalidate(w);
    }
    tk_invoke(w->cb1, w, w->cb1_ud);
}
void tk_radio_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}

/* M14: group inner navigation - the sibling radios in the
 * same parent with the same group_id, in child order.
 * Returns the next/previous enabled+visible member, or NULL
 * at the group's edge (the WinUI RadioButtons group does
 * not wrap) or when w is not in a group. */
static tk_widget *radio_group_step(tk_widget *w, int dir)
{
    if (!w || !w->parent) return NULL;
    tk_widget *prev = NULL, *next = NULL;
    bool seen = false;
    for (size_t i = 0; i < w->parent->n_children; i++) {
        tk_widget *s = w->parent->children[i];
        if (s->kind != TK_W_RADIO || s->group_id != w->group_id)
            continue;
        if (s == w) { seen = true; continue; }
        if (!s->visible || !s->enabled) continue;
        if (!seen) prev = s;
        else if (!next) next = s;
    }
    if (!seen) return NULL;
    return dir > 0 ? next : prev;
}

tk_widget *tk_slider_new(float value)
{
    tk_widget *w = leaf(TK_W_SLIDER, NULL, true);
    if (w) {
        w->fvalue = value < 0 ? 0 : (value > 1 ? 1 : value);
        w->sld_disp = w->fvalue;
    }
    return w;
}
float tk_slider_value(const tk_widget *w) { return w->fvalue; }

/* M13: the slider's DISPLAYED fractions. A programmatic set
 * animates the display (sld_disp / sld_disp2) toward the
 * logical values over the mode's small pair; the logical
 * value and the change callback stay immediate, exactly as
 * before. Pointer and keyboard sets are direct (the thumb is
 * under the finger / key), and any set without a live
 * animation clock snaps - the Switch's gating, reused. */
static void sld_anim_cb(void *ud, int permille)
{
    tk_widget *w = ud;
    float t = permille / 1000.0f;
    w->sld_disp = w->sld_from + (w->fvalue - w->sld_from) * t;
    w->sld_disp2 = w->sld_from2 + (w->fvalue2 - w->sld_from2) * t;
    tk_widget_invalidate(w);
}
static void sld_anim_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    w->sld_disp = w->fvalue;
    w->sld_disp2 = w->fvalue2;
    tk_widget_invalidate(w);
}
static void sld_display_sync(tk_widget *w, bool animate)
{
    /* M13: programmatic-change animation is opt-in per widget
     * (tk_slider_set_animate_values). Reflective consumers -
     * the shell's backend-synced volume/brightness sliders,
     * the player's seek bar, status meters - set values to
     * MIRROR external state; animating those breaks the
     * settled-frames-transfer-zero contract (a 200ms tail of
     * repaints after every sync) and lags the mirror. The
     * M5 scroll-shift opt-in is the same judgment. */
    if (!w->sld_anim_prog) animate = false;
    tk_root *r = tk_widget_root(w);
    if (w->anim_handle) {
        tk_anim_cancel(w->anim_handle);
        w->anim_handle = NULL;
    }
    if (!animate || !r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r) || w->sld_grabbed) {
        w->sld_disp = w->fvalue;
        w->sld_disp2 = w->fvalue2;
        return;
    }
    w->sld_from = w->sld_disp;
    w->sld_from2 = w->sld_disp2;
    if (w->sld_from == w->fvalue && w->sld_from2 == w->fvalue2)
        return;
    uint32_t dur; tk_ease_fn ease;
    tk_anim_mode_pair(false, &dur, &ease);
    w->anim_handle = tk_anim_start(r, 0, 1000, dur, ease,
                                   sld_anim_cb, sld_anim_done, w);
    if (!w->anim_handle) {
        w->sld_disp = w->fvalue;
        w->sld_disp2 = w->fvalue2;
    }
}

static void slider_set_value_impl(tk_widget *w, float v,
                                  bool animate)
{
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    if (v != w->fvalue) {
        w->fvalue = v;
        sld_display_sync(w, animate); /* M13 */
        tk_widget_invalidate(w); /* M5 */
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_slider_set_value(tk_widget *w, float v)
{
    slider_set_value_impl(w, v, true);
}
void tk_slider_set_animate_values(tk_widget *w, bool on)
{
    if (w) w->sld_anim_prog = on;
}
void tk_slider_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}

/* --- M6: slider range mode --------------------------------------
 * The fraction in fvalue stays the single source of truth (paint,
 * pointer and the fraction API are untouched); range mode only
 * changes how values are expressed at the API boundary and adds
 * step quantisation. fmin/fmax/fstep are shared with the
 * spinbutton, a different widget kind, so there is no clash. */
void tk_slider_set_range(tk_widget *w, float min, float max, float step)
{
    if (!w || w->kind != TK_W_SLIDER) return;
    if (max < min) { float t = min; min = max; max = t; }
    w->fmin = min; w->fmax = max; w->fstep = step;
    w->sld_range = true;
    tk_widget_invalidate(w);
}
void tk_slider_clear_range(tk_widget *w)
{
    if (!w || w->kind != TK_W_SLIDER) return;
    w->sld_range = false;
    tk_widget_invalidate(w);
}
bool tk_slider_has_range(const tk_widget *w)
{
    return w && w->kind == TK_W_SLIDER && w->sld_range;
}
float tk_slider_value_range(const tk_widget *w)
{
    if (!w) return 0;
    if (!w->sld_range) return w->fvalue;
    return w->fmin + w->fvalue * (w->fmax - w->fmin);
}
static void slider_set_value_range_impl(tk_widget *w, float v,
                                        bool animate)
{
    if (!w || w->kind != TK_W_SLIDER) return;
    if (!w->sld_range) { slider_set_value_impl(w, v, animate); return; }
    if (v < w->fmin) v = w->fmin;
    if (v > w->fmax) v = w->fmax;
    if (w->fstep > 0) {
        /* quantise to the step grid anchored at min */
        float n = (v - w->fmin) / w->fstep;
        long steps = (long)(n + 0.5f);
        v = w->fmin + (float)steps * w->fstep;
        if (v > w->fmax) v = w->fmax;
        if (v < w->fmin) v = w->fmin;
    }
    float span = w->fmax - w->fmin;
    slider_set_value_impl(w, span > 0 ? (v - w->fmin) / span : 0,
                          animate);
}
void tk_slider_set_value_range(tk_widget *w, float v)
{
    slider_set_value_range_impl(w, v, true);
}

/* --- M7: dual-handle Range slider --------------------------------
 * fvalue stays the HI fraction; fvalue2 is the LO fraction. The
 * public values are range units when range mode is on (the M6
 * convention), plain fractions otherwise. Handles clamp against
 * each other (lo <= hi); the active handle is the last one
 * touched, and its value indicator shows while it is dragged
 * or stepped. */
static float sld_to_frac(tk_widget *w, float v)
{
    if (!w->sld_range) return v < 0 ? 0 : (v > 1 ? 1 : v);
    if (v < w->fmin) v = w->fmin;
    if (v > w->fmax) v = w->fmax;
    if (w->fstep > 0) {
        float n = (v - w->fmin) / w->fstep;
        long steps = (long)(n + 0.5f);
        v = w->fmin + (float)steps * w->fstep;
        if (v > w->fmax) v = w->fmax;
        if (v < w->fmin) v = w->fmin;
    }
    float span = w->fmax - w->fmin;
    return span > 0 ? (v - w->fmin) / span : 0;
}

static void sld_set_frac(tk_widget *w, int which, float frac)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    w->sld_active = which;
    if (which == 0) {
        if (frac > w->fvalue) frac = w->fvalue;
        if (frac != w->fvalue2) {
            w->fvalue2 = frac;
            sld_display_sync(w, false); /* direct input (M13) */
            tk_widget_invalidate(w);
            tk_invoke(w->cb1, w, w->cb1_ud);
        }
    } else {
        if (frac < w->fvalue2) frac = w->fvalue2;
        /* pointer-driven: direct (M13), fires cb1 as before */
        slider_set_value_impl(w, frac, false);
    }
}

void tk_slider_set_values2(tk_widget *w, float lo, float hi)
{
    if (!w || w->kind != TK_W_SLIDER) return;
    float lf = sld_to_frac(w, lo);
    float hf = sld_to_frac(w, hi);
    if (lf > hf) { float t = lf; lf = hf; hf = t; }
    w->sld_dual = true;
    w->fvalue2 = lf;
    w->fvalue = hf;
    w->sld_active = 1;
    sld_display_sync(w, true); /* M13: programmatic - animated */
    tk_widget_invalidate(w);
}
void tk_slider_clear_dual(tk_widget *w)
{
    if (!w || w->kind != TK_W_SLIDER) return;
    w->sld_dual = false;
    w->sld_grabbed = false;
    tk_widget_invalidate(w);
}
bool tk_slider_is_dual(const tk_widget *w)
{
    return w && w->kind == TK_W_SLIDER && w->sld_dual;
}
float tk_slider_value_lo(const tk_widget *w)
{
    if (!w) return 0;
    if (!w->sld_range) return w->fvalue2;
    return w->fmin + w->fvalue2 * (w->fmax - w->fmin);
}
float tk_slider_value_hi(const tk_widget *w)
{
    return tk_slider_value_range(w);
}
int tk_slider_active_handle(const tk_widget *w)
{
    return w ? w->sld_active : 1;
}
void tk_slider_set_active_handle(tk_widget *w, int which)
{
    if (!w || w->kind != TK_W_SLIDER) return;
    w->sld_active = which ? 1 : 0;
    tk_widget_invalidate(w);
}

tk_widget *tk_textfield_new(const char *initial)
{
    tk_widget *w = leaf(TK_W_TEXTFIELD, NULL, true);
    if (!w) return NULL;
    w->buffer = tc_new();
    /* M7: the toolkit shared clipboard is the default provider
     * (an explicit per-widget provider still overrides) */
    if (w->buffer) tc_set_clipboard(w->buffer, &tk_shared_clipboard);
    w->tf_valid = true;  /* M7: valid until a validator says no */
    w->tf_threshold = 1; /* M7: the Android default */
    if (initial) tc_set_text(w->buffer, initial);
    return w;
}
tc_buffer *tk_textfield_buffer(tk_widget *w) { return w->buffer; }
void tk_textfield_set_text(tk_widget *w, const char *text)
{
    tc_set_text(w->buffer, text ? text : "");
    tk_widget_invalidate(w); /* M5 */
    tk_invoke(w->cb1, w, w->cb1_ud);
}
size_t tk_textfield_text(const tk_widget *w, char *out, size_t cap)
{
    return tc_text(w->buffer, out, cap);
}
void tk_textfield_set_placeholder(tk_widget *w, const char *text)
{
    if (!w) return;
    if (!w->placeholder && !text) return;
    if (w->placeholder && text && strcmp(w->placeholder, text) == 0) return;
    free(w->placeholder); w->placeholder = tk_strdup(text);
    tk_widget_invalidate(w); /* M5 */
}
void tk_textfield_set_clipboard(tk_widget *w, const tc_clipboard *cb)
{
    tc_set_clipboard(w->buffer, cb);
}
void tk_textfield_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}
void tk_textfield_set_password(tk_widget *w, bool password)
{
    if (!w || w->password == password) return;
    w->password = password;
    tk_widget_invalidate(w); /* M5: mask appears/disappears */
}
bool tk_textfield_is_password(const tk_widget *w)
{
    return w && w->password;
}

/* --- M6: supporting / error text --- */
void tk_textfield_set_supporting_text(tk_widget *w, const char *text)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    if (!w->support_text && !text) return;
    if (w->support_text && text && strcmp(w->support_text, text) == 0)
        return;
    free(w->support_text);
    w->support_text = tk_strdup(text);
    tk_widget_invalidate(w);
}
const char *tk_textfield_supporting_text(const tk_widget *w)
{
    return w ? w->support_text : NULL;
}
void tk_textfield_set_error(tk_widget *w, bool error)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    if (w->support_error != error) {
        w->support_error = error;
        tk_widget_invalidate(w);
    }
}
bool tk_textfield_is_error(const tk_widget *w)
{
    return w && w->support_error;
}

/* --- M7: prefix/suffix, counter + max length, validator,
 *     autocomplete (AUDIT section 4) --- */
void tk_textfield_set_prefix(tk_widget *w, const char *text)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    free(w->tf_prefix);
    w->tf_prefix = tk_strdup(text);
    tk_widget_invalidate(w);
}
void tk_textfield_set_suffix(tk_widget *w, const char *text)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    free(w->tf_suffix);
    w->tf_suffix = tk_strdup(text);
    tk_widget_invalidate(w);
}
void tk_textfield_set_max_length(tk_widget *w, int max_chars)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    w->tf_max_len = max_chars < 0 ? 0 : max_chars;
    tk_widget_invalidate(w);
}
int tk_textfield_max_length(const tk_widget *w)
{
    return w ? w->tf_max_len : 0;
}
void tk_textfield_set_counter(tk_widget *w, bool show)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    w->tf_counter = show;
    tk_widget_invalidate(w);
}
void tk_textfield_set_validator(tk_widget *w, tk_text_validator_cb cb,
                                void *ud)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    w->tf_validator = cb;
    w->tf_validator_ud = ud;
    w->tf_valid = true;
    tk_widget_invalidate(w);
}
void tk_textfield_set_error_message(tk_widget *w, const char *text)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    free(w->tf_error_msg);
    w->tf_error_msg = tk_strdup(text);
    tk_widget_invalidate(w);
}
bool tk_textfield_validate(tk_widget *w)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return true;
    if (!w->tf_validator) {
        w->tf_valid = true;
        return true;
    }
    char buf[4096];
    tc_text(w->buffer, buf, sizeof(buf));
    bool ok = w->tf_validator(w, buf, w->tf_validator_ud);
    /* the validation result IS the field's error state: the
     * M6 error flag drives tk_textfield_is_error() and the
     * outline, so keep the two in step */
    if (ok != w->tf_valid || w->support_error == ok) {
        w->tf_valid = ok;
        w->support_error = !ok;
        tk_widget_invalidate(w);
    }
    return ok;
}
bool tk_textfield_is_valid(const tk_widget *w)
{
    return !w || w->tf_valid;
}
void tk_textfield_set_completions(tk_widget *w,
                                  const char *const *items, size_t n)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    for (size_t i = 0; i < w->n_items; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL;
    w->n_items = 0;
    if (!items || !n) return;
    w->items = calloc(n, sizeof(char *));
    if (!w->items) return;
    for (size_t i = 0; i < n; i++) w->items[i] = tk_strdup(items[i]);
    w->n_items = n;
}
void tk_textfield_set_completion_threshold(tk_widget *w, int chars)
{
    if (!w || w->kind != TK_W_TEXTFIELD) return;
    w->tf_threshold = chars < 0 ? 0 : chars;
}
bool tk_textfield_completion_open(const tk_widget *w)
{
    return w && w->open_sub != NULL;
}

/* ---- password mask helpers (M3) --------------------------------
 * The mask glyph is U+25CF (3 bytes in UTF-8). Masking is a pure
 * display transform: one glyph per codepoint of the real text, so
 * character counts - and therefore caret/selection positions -
 * are preserved exactly. */
#define TK_MASK_GLYPH "\xE2\x97\x8F" /* U+25CF */
#define TK_MASK_CP 0x25CF
#define TK_MASK_LEN 3

static size_t utf8_cp_count(const char *s, size_t nbytes)
{
    size_t n = 0, off = 0;
    while (off < nbytes) {
        size_t cl = 0;
        (void)tc_utf8_decode(s + off, nbytes - off, &cl);
        if (!cl) cl = 1;
        off += cl;
        n++;
    }
    return n;
}

/* Write the masked form of src[0..nbytes) into dst (one mask glyph
 * per codepoint). dst must hold cp_count*TK_MASK_LEN + 1 bytes.
 * Returns the byte length written (excluding the NUL). */
static size_t mask_write(char *dst, const char *src, size_t nbytes)
{
    size_t n = utf8_cp_count(src, nbytes);
    for (size_t i = 0; i < n; i++)
        memcpy(dst + i * TK_MASK_LEN, TK_MASK_GLYPH, TK_MASK_LEN);
    dst[n * TK_MASK_LEN] = '\0';
    return n * TK_MASK_LEN;
}

/* tk_offset_at_x() with every character's advance replaced by the
 * mask glyph's advance: the caret nearest to x_px in the MASKED
 * layout, returned as a byte offset into the REAL string. */
static size_t masked_offset_at_x(const tk_theme *t, const char *utf8,
                                 int x_px)
{
    if (!utf8) return 0;
    if (x_px <= 0) return 0;
    int adv = tk_text_cp_width(t, TK_MASK_CP);
    size_t len = strlen(utf8), off = 0;
    int acc = 0;
    size_t best_off = 0;
    int best_diff = x_px; /* caret at 0 */
    while (off < len) {
        size_t cl = 0;
        (void)tc_utf8_decode(utf8 + off, len - off, &cl);
        if (cl == 0) cl = 1;
        acc += adv;
        int d = x_px > acc ? x_px - acc : acc - x_px;
        if (d < best_diff) { best_diff = d; best_off = off + cl; }
        off += cl;
    }
    return best_off;
}

tk_widget *tk_scrollview_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_SCROLLVIEW);
    if (w) w->can_focus = true;
    return w;
}

/* Scrollbar thumb geometry - the single source the leaf paint
 * and the core's shift fast path share, so both agree on which
 * strip pixels a scroll invalidates. */
tk_rect tk_sv_thumb_rect(const tk_widget *w, int scroll_y)
{
    tk_rect r = w->rect;
    int content_h = w->n_children ? w->children[0]->desired.h : 0;
    int th = content_h > 0 ? r.h * r.h / content_h : 0;
    if (th < 16) th = 16;
    if (th > r.h) th = r.h;
    int ty = r.y + (content_h - r.h > 0
                    ? scroll_y * (r.h - th) / (content_h - r.h) : 0);
    return (tk_rect){r.x + r.w - 6, ty, 4, th};
}

tk_rect tk_sv_thumb_zone(const tk_widget *w, int old_y, int new_y)
{
    tk_rect a = tk_sv_thumb_rect(w, old_y);
    tk_rect b = tk_sv_thumb_rect(w, new_y);
    int y0 = a.y < b.y ? a.y : b.y;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    /* widen to the full strip column: the fill under the thumb
     * covers the whole 8 px the scrollbar owns */
    return (tk_rect){w->rect.x + w->rect.w - 8, y0, 8, y1 - y0};
}
void tk_scrollview_set_child(tk_widget *sv, tk_widget *child)
{
    while (sv->n_children) tk_remove_child(sv, sv->children[0]);
    tk_add_child(sv, child);
}
int tk_scrollview_offset_y(const tk_widget *sv) { return sv->scroll_y; }

bool tk_scrollview_tick(tk_widget *sv)
{
    if (!sv || sv->vel_y == 0) return false;
    int before = sv->scroll_y;
    tk_scrollview_scroll_to(sv, sv->scroll_y + sv->vel_y);
    sv->vel_y = sv->vel_y * 4 / 5; /* friction */
    if (sv->vel_y == 0 || sv->scroll_y == before) {
        sv->vel_y = 0;
        return false;
    }
    return true;
}
void tk_scrollview_scroll_to(tk_widget *sv, int y)
{
    sv->scroll_y = y < 0 ? 0 : y;
    /* re-arrange the child with the new offset */
    if (sv->n_children) {
        tk_widget *ch = sv->children[0];
        int content_h = ch->desired.h;
        int max_off = content_h - sv->rect.h;
        if (max_off < 0) max_off = 0;
        if (sv->scroll_y > max_off) sv->scroll_y = max_off;
        tk_arrange(ch, (tk_rect){sv->rect.x, sv->rect.y - sv->scroll_y,
                                 sv->rect.w,
                                 content_h > sv->rect.h ? content_h : sv->rect.h});
    }
}

tk_widget *tk_listview_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_LISTVIEW);
    if (w) { w->can_focus = true; w->sel = -1; }
    return w;
}

void tk_listview_set_subitems(tk_widget *w, const char *const *subs,
                              size_t n)
{
    if (!w) return;
    for (size_t i = 0; i < w->n_subitems; i++) free(w->subitems[i]);
    free(w->subitems);
    w->subitems = NULL; w->n_subitems = 0;
    if (subs && n) {
        w->subitems = calloc(n, sizeof(char *));
        if (!w->subitems) return;
        for (size_t i = 0; i < n; i++)
            w->subitems[i] = subs[i] ? tk_strdup(subs[i]) : NULL;
        w->n_subitems = n;
    }
    tk_widget_invalidate(w);
}

void tk_listview_set_item_icons(tk_widget *w,
                                const uint32_t *const *icons,
                                int iw, int ih, size_t n)
{
    if (!w) return;
    free(w->item_icons);
    w->item_icons = NULL; w->n_item_icons = 0;
    w->item_icon_w = 0; w->item_icon_h = 0;
    if (icons && n && iw > 0 && ih > 0) {
        w->item_icons = calloc(n, sizeof(uint32_t *));
        if (!w->item_icons) return;
        for (size_t i = 0; i < n; i++)
            w->item_icons[i] = icons[i];
        w->n_item_icons = n;
        w->item_icon_w = iw; w->item_icon_h = ih;
    }
    tk_widget_invalidate(w);
}

/* Row height: two-line rows (main + sub) when any sub-text exists. */
static int lv_row_h(const tk_widget *w, const tk_theme *t)
{
    return w->n_subitems ? t->row_h2 : t->row_h;
}
int tk_listview_row_h(const tk_widget *w, const tk_theme *t)
{
    return w ? lv_row_h(w, t) : t->row_h;
}
void tk_listview_set_items(tk_widget *w, const char *const *items, size_t n)
{
    for (size_t i = 0; i < w->n_items; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL; w->n_items = 0;
    /* M5: row flags described the old items; drop them too */
    free(w->item_flags);
    w->item_flags = NULL;
    w->lv_last_check = -1;
    if (n) {
        w->items = calloc(n, sizeof(char *));
        if (!w->items) return;
        for (size_t i = 0; i < n; i++) w->items[i] = tk_strdup(items[i]);
        w->n_items = n;
    }
    if (w->sel >= (int)w->n_items) w->sel = w->n_items ? (int)w->n_items - 1 : -1;
    w->marked = -1; /* the marked row referred to the old items */
    tk_widget_invalidate(w); /* M5 */
}
size_t tk_listview_count(const tk_widget *w) { return w->n_items; }
const char *tk_listview_item(const tk_widget *w, int idx)
{
    if (!w || idx < 0 || (size_t)idx >= w->n_items) return NULL;
    return w->items[idx];
}
int tk_listview_selected(const tk_widget *w) { return w->sel; }
/* keep the selection inside the visible window */
static void lv_keep_sel_visible(tk_widget *w)
{
    if (w->sel < 0 || w->rect.h <= 0) return;
    int rows = w->rect.h / lv_row_h(w, metrics());
    if (rows <= 0) return;
    if (w->sel < w->top_index) w->top_index = w->sel;
    if (w->sel >= w->top_index + rows) w->top_index = w->sel - rows + 1;
}
void tk_listview_select(tk_widget *w, int idx)
{
    if (idx < -1) idx = -1;
    if (idx >= (int)w->n_items) idx = (int)w->n_items - 1;
    if (idx != w->sel) {
        w->sel = idx;
        lv_keep_sel_visible(w);
        tk_widget_invalidate(w); /* M5 */
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_listview_set_marked(tk_widget *w, int idx)
{
    if (!w) return;
    if (idx < -1) idx = -1;
    if (idx >= (int)w->n_items) idx = (int)w->n_items - 1;
    if (idx != w->marked) {
        w->marked = idx;
        tk_widget_invalidate(w); /* M5 */
    }
}
int tk_listview_marked(const tk_widget *w) { return w ? w->marked : -1; }

/* ---- M5: checkable rows + section headers ----------------------
 * Flags live in a parallel array allocated on first use and kept
 * sized to the item list by set_items / move_row. */
static bool lv_ensure_flags(tk_widget *w)
{
    if (w->item_flags) return true;
    if (!w->n_items) return false;
    w->item_flags = calloc(w->n_items, sizeof(uint8_t));
    return w->item_flags != NULL;
}
static bool lv_flag(const tk_widget *w, int idx, uint8_t f)
{
    return w && w->item_flags && idx >= 0 && idx < (int)w->n_items &&
           (w->item_flags[idx] & f) != 0;
}
void tk_listview_set_checkable(tk_widget *w, bool on)
{
    if (!w || w->lv_checkable == on) return;
    w->lv_checkable = on;
    tk_widget_invalidate(w);
}
bool tk_listview_checkable(const tk_widget *w)
{
    return w && w->lv_checkable;
}
void tk_listview_set_multi_select(tk_widget *w, bool on)
{
    if (!w || w->lv_multi == on) return;
    w->lv_multi = on;
    tk_widget_invalidate(w);
}
bool tk_listview_multi_select(const tk_widget *w)
{
    return w && w->lv_multi;
}
int tk_listview_checked_count(const tk_widget *w)
{
    if (!w) return 0;
    int n = 0;
    for (size_t i = 0; i < w->n_items; i++)
        if (lv_flag(w, (int)i, TK_LV_FLAG_CHECKED)) n++;
    return n;
}
bool tk_listview_row_checked(const tk_widget *w, int idx)
{
    return lv_flag(w, idx, TK_LV_FLAG_CHECKED);
}
void tk_listview_set_row_checked(tk_widget *w, int idx, bool on)
{
    if (!w || idx < 0 || idx >= (int)w->n_items) return;
    if (!lv_ensure_flags(w)) return;
    bool cur = (w->item_flags[idx] & TK_LV_FLAG_CHECKED) != 0;
    if (cur == on) return;
    if (on) w->item_flags[idx] |= TK_LV_FLAG_CHECKED;
    else    w->item_flags[idx] &= (uint8_t)~TK_LV_FLAG_CHECKED;
    tk_widget_invalidate(w);
}
void tk_listview_set_header(tk_widget *w, int idx, bool header)
{
    if (!w || idx < 0 || idx >= (int)w->n_items) return;
    if (!lv_ensure_flags(w)) return;
    bool cur = (w->item_flags[idx] & TK_LV_FLAG_HEADER) != 0;
    if (cur == header) return;
    if (header) w->item_flags[idx] |= TK_LV_FLAG_HEADER;
    else        w->item_flags[idx] &= (uint8_t)~TK_LV_FLAG_HEADER;
    tk_widget_invalidate(w);
}
bool tk_listview_is_header(const tk_widget *w, int idx)
{
    return lv_flag(w, idx, TK_LV_FLAG_HEADER);
}
int tk_listview_last_checked(const tk_widget *w)
{
    return w ? w->lv_last_check : -1;
}
void tk_listview_set_on_check(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (!w) return;
    w->cb3 = cb; w->cb3_ud = ud;
}
/* Toggle from the pointer path: flips the flag, records the row
 * for tk_listview_last_checked(), notifies on_check. */
static void lv_toggle_check(tk_widget *w, int idx)
{
    if (!lv_ensure_flags(w)) return;
    w->item_flags[idx] ^= TK_LV_FLAG_CHECKED;
    w->lv_last_check = idx;
    tk_widget_invalidate(w);
    tk_invoke(w->cb3, w, w->cb3_ud);
}
/* Next selectable (non-header) row from `from` in direction dir,
 * or `from` itself when there is none. */
static int lv_step_selectable(const tk_widget *w, int from, int dir)
{
    int i = from + dir;
    while (i >= 0 && i < (int)w->n_items &&
           lv_flag(w, i, TK_LV_FLAG_HEADER))
        i += dir;
    return (i >= 0 && i < (int)w->n_items) ? i : from;
}
bool tk_listview_move_row(tk_widget *w, int from, int to)
{
    if (!w) return false;
    if (from < 0 || to < 0 || from >= (int)w->n_items ||
        to >= (int)w->n_items)
        return false;
    if (from == to) return true;
    /* shift the pointers between from/to; strings are not copied */
    char *it = w->items[from];
    uint8_t fl = w->item_flags ? w->item_flags[from] : 0;
    if (from < to) {
        memmove(&w->items[from], &w->items[from + 1],
                (size_t)(to - from) * sizeof(char *));
        if (w->item_flags)
            memmove(&w->item_flags[from], &w->item_flags[from + 1],
                    (size_t)(to - from) * sizeof(uint8_t));
    } else {
        memmove(&w->items[to + 1], &w->items[to],
                (size_t)(from - to) * sizeof(char *));
        if (w->item_flags)
            memmove(&w->item_flags[to + 1], &w->item_flags[to],
                    (size_t)(from - to) * sizeof(uint8_t));
    }
    w->items[to] = it;
    if (w->item_flags) w->item_flags[to] = fl;
    /* the selection follows the selected item */
    int old_sel = w->sel;
    if (w->sel == from) w->sel = to;
    else if (from < w->sel && w->sel <= to) w->sel--;
    else if (to <= w->sel && w->sel < from) w->sel++;
    if (w->sel != old_sel) {
        lv_keep_sel_visible(w);
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
    tk_widget_invalidate(w);
    return true;
}

/* M7: row drag reorder (the event/paint halves live in the
 * ListView event and paint cases) */
void tk_listview_set_drag_reorder(tk_widget *w, bool on)
{
    if (!w || w->kind != TK_W_LISTVIEW) return;
    w->lv_drag_reorder = on;
    if (!on) w->lv_drag_row = -1;
    tk_widget_invalidate(w);
}
bool tk_listview_drag_reorder(const tk_widget *w)
{
    return w && w->kind == TK_W_LISTVIEW && w->lv_drag_reorder;
}

void tk_listview_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}
void tk_listview_set_on_activate(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb2 = cb; w->cb2_ud = ud;
}

/* ---------------------------------------------------------------- */
/* Measure                                                           */
/* ---------------------------------------------------------------- */

tk_size tk_leaf_measure(tk_widget *w, tk_constraints c)
{
    if (w->kind >= TK_W_CARD) return tk_m7_measure(w, c); /* M7 */
    const tk_theme *t = metrics();
    (void)c;
    switch (w->kind) {
    case TK_W_LABEL:
        return (tk_size){ tk_w_text_w(w, t, w->text), tk_w_text_lh(w, t) };
    case TK_W_BUTTON:
        if (w->image_px && w->btn_icon_only) /* icon centered only */
            return (tk_size){ w->image_w + t->unit, w->image_h + t->unit };
        if (w->image_px && w->btn_icon_layout == TK_BTN_ICON_ROW) {
            /* M6: icon left of label on one line */
            int tw = tk_w_text_w(w, t, w->text);
            int hh = t->control_h;
            int need = w->image_h + 2 * t->unit;
            if (need > hh) hh = need;
            return (tk_size){ w->image_w + (tw ? t->unit + tw : 0)
                              + 4 * t->unit, hh };
        }
        if (w->image_px) { /* icon cell: icon over label */
            int tw = tk_w_text_w(w, t, w->text);
            int wid = tw > w->image_w ? tw : w->image_w;
            return (tk_size){ wid + 2 * t->unit,
                              w->image_h + tk_w_text_lh(w, t) + 3 * t->unit };
        }
        return (tk_size){ tk_w_text_w(w, t, w->text) + 4 * t->unit,
                          t->control_h };
    case TK_W_CHECKBOX:
    case TK_W_RADIO:
        /* M8: the WinUI forms paint a 28px box (20epx at the
         * device scale) with the label at box + unit; the M3
         * forms paint 18px with the label at 20 + unit. The
         * measure must match the paint or a parent box leaves
         * the label 8px short in WinUI mode.
         * M21: the M3 box is 21px, the label at 23 + unit;
         * the classic theme keeps the historical 18px form.
         * M23: at the corrected ~179.2ppi premise the
         * material3 effective box (20dp, M21 SS5.3) converts
         * to 22.4px, so 21px is the retained value of AUDIT
         * (a-2)/(a-3), not a fresh conversion. */
        return (tk_size){ (t->style == 2 ? 28
                           : t->style == 1 ? 23 : 20) + t->unit +
                          tk_text_width(t, w->text), t->control_h };
    case TK_W_SLIDER:
        return (tk_size){ 120, t->control_h };
    case TK_W_TEXTFIELD: {
        int fh = t->control_h;
        /* M7: the supporting line is also measured in for the
         * character counter and a validator error message */
        bool tf_err = !w->tf_valid && w->tf_error_msg &&
                      w->tf_error_msg[0];
        if ((w->support_text && w->support_text[0]) ||
            w->tf_counter || tf_err)
            fh += tk_line_height_st(t, TK_TEXT_CAPTION) + 4;
        return (tk_size){ 160, fh };
    }
    case TK_W_LISTVIEW: {
        int rh = lv_row_h(w, t);
        int widest = 80;
        int icon_extra = w->n_item_icons ? w->item_icon_w + t->unit : 0;
        for (size_t i = 0; i < w->n_items; i++) {
            int tw = tk_text_width(t, w->items[i]) + 2 * t->unit + icon_extra;
            if (tw > widest) widest = tw;
            if (i < w->n_subitems && w->subitems[i]) {
                int sw = tk_text_width_st(t, TK_TEXT_CAPTION,
                                          w->subitems[i])
                         + 2 * t->unit + icon_extra;
                if (sw > widest) widest = sw;
            }
        }
        int h = (int)w->n_items * rh;
        if (h < rh * 3) h = rh * 3;
        return (tk_size){ widest, h };
    }
    case TK_W_SCROLLVIEW:
        return (tk_size){ 80, 80 }; /* refined in tk_core measure */
    case TK_W_OSK:
        return tk_osk_measure(w, c);
    default:
        return tk_w2_measure(w, c);
    }
}

/* ---------------------------------------------------------------- */
/* Paint                                                             */
/* ---------------------------------------------------------------- */

/* Draw the buffer's IME composition at (x, y) (y = top of the line
 * box) and return its width in px. Formal preedit rendering (M2):
 * the composition is plain text in the foreground colour; the
 * segmentation is carried by underlines - 1px dim under inactive
 * segments, 2px accent under the active segment - and while the
 * active segment is in candidate-browsing state it additionally
 * gets the selection background. This replaces both the old
 * TextField "text[composition]" bracket placeholder and the
 * TextView all-accent rendering. */
int tk_draw_composition(tk_canvas *c, const tk_theme *t, tc_buffer *b,
                        int x, int y, tk_color fg, bool mask)
{
    /* sized for the masked form: masking expands up to 3x */
    char comp[3 * 512];
    size_t clen = tc_composition(b, comp, 512);
    if (!clen) return 0;
    size_t lens[32];
    size_t nseg = tc_composition_segments(b, lens, 32);
    if (mask) {
        /* Password field (M3): mask the preedit too, one glyph per
         * codepoint. Segment byte lengths are rewritten in masked
         * space (codepoints x TK_MASK_LEN) so the underlines and
         * the active-segment highlight stay aligned with the mask
         * glyphs actually drawn. */
        char mcomp[3 * 512];
        size_t moff = 0, off = 0;
        for (size_t i = 0; i < nseg; i++) {
            size_t sl = lens[i];
            if (sl > clen - off) sl = clen - off;
            size_t n = utf8_cp_count(comp + off, sl);
            size_t room = (sizeof(mcomp) - 1 - moff) / TK_MASK_LEN;
            if (n > room) n = room;
            for (size_t k = 0; k < n; k++) {
                memcpy(mcomp + moff, TK_MASK_GLYPH, TK_MASK_LEN);
                moff += TK_MASK_LEN;
            }
            lens[i] = n * TK_MASK_LEN;
            off += sl;
        }
        memcpy(comp, mcomp, moff);
        comp[moff] = '\0';
        clen = moff;
    }
    int active = tc_composition_active(b);
    bool hl = tc_composition_highlight(b);
    int lh = tk_line_height(t);
    int cx = x;
    size_t off = 0;
    for (size_t i = 0; i < nseg && off < clen; i++) {
        size_t sl = lens[i];
        if (sl > clen - off) sl = clen - off;
        char piece[3 * 512];
        if (sl >= sizeof(piece)) sl = sizeof(piece) - 1;
        memcpy(piece, comp + off, sl);
        piece[sl] = '\0';
        int wpx = tk_text_width(t, piece);
        if ((int)i == active && hl)
            tk_canvas_fill_rect(c, (tk_rect){ cx, y, wpx, lh },
                                t->selection);
        tk_canvas_draw_text(c, t, cx, y, piece, fg);
        if ((int)i == active)
            tk_canvas_fill_rect(c, (tk_rect){ cx, y + lh - 2, wpx, 2 },
                                t->accent);
        else
            tk_canvas_fill_rect(c, (tk_rect){ cx, y + lh - 1, wpx, 1 },
                                t->text_dim);
        cx += wpx;
        off += sl;
    }
    return cx - x;
}

/* M6: paint the face for a non-legacy button variant and return
 * the content (icon/text) colour for it. FILLED = accent fill,
 * TONAL = container fill, OUTLINED = border only with accent
 * content, TEXT = no face with accent content. */
/* M10: the content colour of a disabled control. WinUI uses
 * TextFillColorDisabled; the M3 mode uses on-surface at the
 * 38% disabled content opacity (an alpha colour - the text
 * painter composites it over the face); classic keeps the
 * long-standing text_dim. */
static tk_color btn_disabled_fg(const tk_theme *t)
{
    if (t->style == 2 && t->text_dis.a) return t->text_dis;
    if (t->style == 1) { tk_color g = t->text; g.a = 97; return g; }
    return t->text_dim;
}

/* M10: paint the primary (accent) button face for the current
 * state and return the content colour. The state forms follow
 * the mode references:
 * - M3 (style 1): pressed = the on-primary state layer at the
 *   10% pressed opacity over primary; disabled = on-surface
 *   at the 12% disabled container opacity, content at 38%.
 * - WinUI (style 2): pressed = AccentFillColorTertiary (the
 *   accent at 80% over the surface beneath); disabled =
 *   AccentFillColorDisabled with TextOnAccentFillColor-
 *   Disabled content.
 * - classic (style 0): the pre-M10 forms (pressed darkens the
 *   accent toward black, disabled is surface_alt / text_dim). */
/* M13: fill a primary-face shape. A split button half in the
 * M3 mode gets the asymmetric corners of the M3 split button
 * spec (full outer corner, 4dp inner corner); everything else
 * takes the uniform radius as before. */
static void btn_face_fill(tk_widget *w, tk_canvas *c, tk_rect r,
                          int brad, const tk_theme *t, tk_color col)
{
    if (w->btn_split_side && t->style == 1) {
        int in = w->btn_split_inner;
        if (w->btn_split_side == 1)
            tk_canvas_fill_round_rect4(c, r, brad, in, in, brad,
                                       col);
        else
            tk_canvas_fill_round_rect4(c, r, in, brad, brad, in,
                                       col);
        return;
    }
    tk_canvas_fill_round_rect(c, r, brad, col);
}

static tk_color accent_button_face(tk_widget *w, tk_canvas *c,
                                   const tk_theme *t, tk_rect r,
                                   int brad)
{
    /* M13: the FAB's M3 face is primaryContainer with
     * onPrimaryContainer content (the FAB spec's container /
     * content roles) - NOT the filled button's primary face it
     * previously borrowed. The pressed form adds the content
     * colour's 10% state layer, the button family's pressed
     * opacity. M3 defines no disabled FAB form; the disabled
     * form follows the filled-button framework (on-surface
     * 12% container / 38% content), recorded in AUDIT S2. */
    if (w->btn_fab && t->style == 1) {
        tk_color fg = t->on_container.a ? t->on_container
                                        : t->text;
        if (!w->enabled) {
            tk_color f = t->text; f.a = 31;
            btn_face_fill(w, c, r, brad, t, f);
            return btn_disabled_fg(t);
        }
        btn_face_fill(w, c, r, brad, t, t->container);
        if (w->pressed || w->ripple_on) {
            /* M17: the pressed visual is the ripple (the
             * uniform layer when none is running) */
            tk_color o = fg; o.a = 26; /* pressed 10% */
            tk_ripple_paint(w, c, r, brad, o);
        }
        if (w->has_fg_override) return w->fg_override;
        return fg;
    }
    if (!w->enabled) {
        if (t->style == 2 && t->accent_dis.a) {
            btn_face_fill(w, c, r, brad, t,
                          tk_face_brush(w, t, t->accent_dis));
            return t->on_accent_dis.a ? t->on_accent_dis
                                      : t->text_dim;
        }
        if (t->style == 1) {
            tk_color f = t->text; f.a = 31; /* on-surface 12% */
            btn_face_fill(w, c, r, brad, t, f);
            return btn_disabled_fg(t);
        }
        btn_face_fill(w, c, r, brad, t, t->surface_alt);
        return t->text_dim;
    }
    if (t->style == 1 && (w->pressed || w->ripple_on)) {
        /* M17: the M3 pressed visual is the ripple (the
         * uniform layer when none is running) */
        btn_face_fill(w, c, r, brad, t, t->accent);
        tk_color o = t->on_accent; o.a = 26; /* pressed 10% */
        tk_ripple_paint(w, c, r, brad, o);
    } else if (w->pressed) {
        if (t->style == 2) {
            tk_color f = t->accent; f.a = 204; /* Accent Tertiary */
            btn_face_fill(w, c, r, brad, t,
                          tk_face_brush(w, t, f));
        } else {
            tk_color bgc = t->accent;
            bgc.r = (uint8_t)(bgc.r * 3 / 4);
            bgc.g = (uint8_t)(bgc.g * 3 / 4);
            bgc.b = (uint8_t)(bgc.b * 3 / 4);
            btn_face_fill(w, c, r, brad, t, bgc);
        }
    } else if (t->style == 2) {
        /* M17: the background brush runs the 83 ms
         * BrushTransition toward the state colour */
        btn_face_fill(w, c, r, brad, t,
                      tk_face_brush(w, t, t->accent));
    } else {
        btn_face_fill(w, c, r, brad, t, t->accent);
    }
    if (w->has_fg_override) return w->fg_override;
    return t->on_accent;
}

/* M11: the WinUI ToggleButton CHECKED face, shared by the
 * variant painter and the legacy text / icon forms. Per the
 * ToggleButton themeresources (fetched 2026-11-20): checked
 * = AccentFillColorDefault, checked pressed =
 * AccentFillColorTertiary (the accent at 80%), checked
 * disabled = AccentFillColorDisabled with
 * TextOnAccentFillColorDisabled content. M10 had the disabled
 * form as ControlFillColorDisabled + TextFillColorDisabled -
 * a spec role mixup (those are the UNCHECKED disabled pair). */
static tk_color winui_toggle_checked_face(tk_widget *w,
                                          tk_canvas *c,
                                          const tk_theme *t,
                                          tk_rect r, int brad)
{
    if (!w->enabled) {
        tk_canvas_fill_round_rect(c, r, brad,
            tk_face_brush(w, t,
                t->accent_dis.a ? t->accent_dis
                    : (t->control_fill_dis.a
                           ? t->control_fill_dis
                           : t->surface_alt)));
        return t->on_accent_dis.a ? t->on_accent_dis
                                  : btn_disabled_fg(t);
    }
    if (w->pressed) {
        tk_color f = t->accent; f.a = 204; /* Accent Tertiary */
        tk_canvas_fill_round_rect(c, r, brad,
                                  tk_face_brush(w, t, f));
    } else {
        tk_canvas_fill_round_rect(c, r, brad,
                                  tk_face_brush(w, t, t->accent));
    }
    if (w->has_fg_override) return w->fg_override;
    return t->on_accent;
}

static tk_color button_variant_face(tk_widget *w, tk_canvas *c,
                                    const tk_theme *t, tk_rect r,
                                    int brad)
{
    tk_color fg = t->text;
    bool tog_on = w->btn_toggle && w->checked;
    /* WinUI ToggleButton: the checked form is the accent fill
     * in every style (the Fluent toggle's on state) - the
     * state forms live in winui_toggle_checked_face (M11). */
    if (t->style == 2 && tog_on)
        return winui_toggle_checked_face(w, c, t, r, brad);
    switch (w->btn_variant) {
    case TK_BTN_ELEVATED: {
        if (t->style == 2) {
            /* the WinUI standard button: control fill + stroke.
             * M10 states: pressed = ControlFillColorTertiary,
             * disabled = ControlFillColorDisabled with
             * TextFillColorDisabled content */
            tk_color ef = !w->enabled
                ? (t->control_fill_dis.a ? t->control_fill_dis
                                         : t->control_fill)
                : w->pressed
                    ? (t->control_fill3.a ? t->control_fill3
                                          : t->control_fill)
                    : t->control_fill;
            tk_canvas_fill_round_rect(c, r, brad,
                tk_face_brush(w, t, ef.a ? ef : t->surface));
            tk_canvas_stroke_round_rect(c, r, brad, 1,
                t->control_stroke.a ? t->control_stroke : t->border);
            fg = w->enabled ? t->text : btn_disabled_fg(t);
            break;
        }
        if (t->style == 1 && tog_on)
            /* M11: the M3 elevated TOGGLE's selected form is
             * the primary face (buttons spec); the unselected
             * form is the low container below */
            return accent_button_face(w, c, t, r, brad);
        /* M3 elevated: surface-container-low + shadow
         * silhouette, primary content. M10 states (style 1):
         * pressed keeps the low container and adds the primary
         * state layer at 10%; disabled drops to the on-surface
         * 12% container with 38% content. Classic keeps the
         * pre-M10 pressed container. */
        tk_canvas_fill_round_rect(c,
            (tk_rect){ r.x + 1, r.y + 2, r.w, r.h }, brad,
            (tk_color){ 0, 0, 0, 40 });
        if (t->style == 1 && !w->enabled) {
            tk_color f = t->text; f.a = 31;
            tk_canvas_fill_round_rect(c, r, brad, f);
        } else {
            tk_color low = t->container_low.a ? t->container_low
                                              : t->surface;
            tk_canvas_fill_round_rect(c, r, brad,
                (w->pressed && t->style != 1) ? t->container : low);
            if (t->style == 1 && (w->pressed || w->ripple_on)) {
                tk_color o = t->accent; o.a = 26;
                tk_ripple_paint(w, c, r, brad, o);
            }
        }
        fg = w->enabled ? t->accent : btn_disabled_fg(t);
        break;
    }
    case TK_BTN_FILLED: {
        /* M11: the M3 filled TOGGLE's unselected form is not
         * the primary face - the buttons spec (fetched
         * 2026-11-20) gives Surface container with
         * On-surface-variant content; the selected form is
         * the primary face below. Pressed adds the content
         * state layer at 10%; disabled is the 12% / 38%
         * framework. */
        if (t->style == 1 && w->btn_toggle && !tog_on) {
            if (!w->enabled) {
                tk_color f = t->text; f.a = 31;
                tk_canvas_fill_round_rect(c, r, brad, f);
                return btn_disabled_fg(t);
            }
            tk_canvas_fill_round_rect(c, r, brad,
                t->container_mid.a ? t->container_mid
                                   : t->container);
            if (w->pressed || w->ripple_on) {
                tk_color o = t->text_dim; o.a = 26;
                tk_ripple_paint(w, c, r, brad, o);
            }
            if (w->has_fg_override) return w->fg_override;
            return t->text_dim;
        }
        /* M10: the shared primary-face painter (state forms
         * per mode, see accent_button_face) */
        return accent_button_face(w, c, t, r, brad);
    }
    case TK_BTN_TONAL: {
        /* M7 toggle: the checked form switches to the
         * secondary-container role (the theme's selection
         * token carries it). M10 states: WinUI (the subtle
         * form) pressed = the tertiary control fill, disabled
         * = ControlFillColorDisabled; M3 pressed = the content
         * state layer at 10% over the container, disabled =
         * on-surface 12% / 38%. */
        if (t->style == 2 && !tog_on) {
            tk_color bgc = !w->enabled
                ? (t->control_fill_dis.a ? t->control_fill_dis
                                         : t->surface_alt)
                : w->pressed
                    ? (t->control_fill3.a ? t->control_fill3
                                          : t->surface_alt)
                    : (t->subtle.a ? t->subtle : t->surface_alt);
            tk_canvas_fill_round_rect(c, r, brad,
                                      tk_face_brush(w, t, bgc));
        } else if (t->style == 1 && !w->enabled) {
            tk_color f = t->text; f.a = 31;
            tk_canvas_fill_round_rect(c, r, brad, f);
        } else {
            tk_color bgc = !w->enabled ? t->surface_alt
                           : tog_on ? t->selection : t->container;
            if (t->style == 0 && w->pressed && w->enabled)
                bgc = t->surface_alt;
            tk_canvas_fill_round_rect(c, r, brad, bgc);
            if (t->style == 1 && (w->pressed || w->ripple_on) &&
                w->enabled) {
                tk_color o = t->text; o.a = 26;
                tk_ripple_paint(w, c, r, brad, o);
            }
        }
        fg = w->enabled ? t->text : btn_disabled_fg(t);
        break;
    }
    case TK_BTN_OUTLINED: {
        if (tog_on && t->style == 1) {
            /* M11: the M3 outlined TOGGLE's selected form is
             * the inverse pair (buttons spec, fetched
             * 2026-11-20): inverseSurface container with
             * inverseOnSurface content, no outline; pressed
             * adds the content state layer at 10%; disabled
             * is the 12% / 38% framework */
            if (!w->enabled) {
                tk_color f = t->text; f.a = 31;
                tk_canvas_fill_round_rect(c, r, brad, f);
            } else {
                tk_canvas_fill_round_rect(c, r, brad,
                                          t->inverse_surface);
                if (w->pressed || w->ripple_on) {
                    tk_color o = t->inverse_on_surface; o.a = 26;
                    tk_ripple_paint(w, c, r, brad, o);
                }
            }
            fg = w->enabled ? t->inverse_on_surface
                            : btn_disabled_fg(t);
            break;
        }
        if (tog_on) /* M7 toggle: a tonal wash appears */
            tk_canvas_fill_round_rect(c, r, brad, t->container);
        else if (t->style == 1 && (w->pressed || w->ripple_on) &&
                 w->enabled) {
            /* M17: the M3 pressed visual is the ripple (the
             * uniform layer when none is running) */
            tk_color o = t->accent; o.a = 26;
            tk_ripple_paint(w, c, r, brad, o);
        }
        else if (t->style == 2) {
            /* M17: the pressed fill is the WinUI background
             * brush: it transitions (83 ms) between transparent
             * and the tertiary control fill */
            tk_color tgt = (w->pressed && w->enabled &&
                            t->control_fill3.a)
                               ? t->control_fill3
                               : (tk_color){ 0, 0, 0, 0 };
            tk_color bc = tk_face_brush(w, t, tgt);
            if (bc.a) tk_canvas_fill_round_rect(c, r, brad, bc);
        }
        else if (w->pressed && w->enabled)
            tk_canvas_fill_round_rect(c, r, brad,
                                      t->surface_alt);
        if (t->style == 2) {
            tk_canvas_stroke_round_rect(c, r, brad, 1,
                t->control_stroke.a ? t->control_stroke : t->border);
            fg = w->enabled ? t->text : btn_disabled_fg(t);
        } else {
            /* M10: the M3 disabled outline is on-surface at
             * the 12% disabled container opacity */
            tk_color oc = t->border;
            if (t->style == 1 && !w->enabled) {
                oc = t->text; oc.a = 31;
            }
            if (brad > 8)
                tk_canvas_stroke_round_rect(c, r, brad, 1, oc);
            else
                tk_canvas_stroke_rect(c, r, 1, oc);
            fg = w->enabled ? t->accent : btn_disabled_fg(t);
        }
        break;
    }
    case TK_BTN_TEXT: {
        if (tog_on && t->style == 1) {
            /* M11: no toggle text button exists in the spec,
             * but the toolkit's text toggle takes the same
             * inverse selected form as the outlined toggle */
            if (!w->enabled) {
                tk_color f = t->text; f.a = 31;
                tk_canvas_fill_round_rect(c, r, brad, f);
            } else {
                tk_canvas_fill_round_rect(c, r, brad,
                                          t->inverse_surface);
                if (w->pressed || w->ripple_on) {
                    tk_color o = t->inverse_on_surface; o.a = 26;
                    tk_ripple_paint(w, c, r, brad, o);
                }
            }
            fg = w->enabled ? t->inverse_on_surface
                            : btn_disabled_fg(t);
            break;
        }
        if (tog_on) /* M7 toggle: a tonal wash appears */
            tk_canvas_fill_round_rect(c, r, brad, t->container);
        else if (t->style == 1 && (w->pressed || w->ripple_on) &&
                 w->enabled) {
            /* M17: the M3 pressed visual is the ripple (the
             * uniform layer when none is running) */
            tk_color o = t->accent; o.a = 26;
            tk_ripple_paint(w, c, r, brad, o);
        }
        else if (t->style == 2) {
            /* M17: the pressed fill is the WinUI background
             * brush (83 ms), as in the outlined form */
            tk_color tgt = (w->pressed && w->enabled &&
                            t->control_fill3.a)
                               ? t->control_fill3
                               : (tk_color){ 0, 0, 0, 0 };
            tk_color bc = tk_face_brush(w, t, tgt);
            if (bc.a) tk_canvas_fill_round_rect(c, r, brad, bc);
        }
        else if (w->pressed && w->enabled)
            tk_canvas_fill_round_rect(c, r, brad,
                                      t->surface_alt);
        fg = w->enabled ? t->accent : btn_disabled_fg(t);
        break;
    }
    default:
        break;
    }
    if (w->has_fg_override && w->enabled) fg = w->fg_override;
    return fg;
}

/* M6: x origin of a button content block of width content_w
 * under the button's text alignment (0 start / 1 center / 2 end;
 * center is the pre-M6 behaviour). */
static int button_content_x(const tk_widget *w, const tk_theme *t,
                            tk_rect r, int content_w)
{
    if (w->btn_text_align == 0) return r.x + 2 * t->unit;
    if (w->btn_text_align == 2) return r.x + r.w - 2 * t->unit - content_w;
    return r.x + (r.w - content_w) / 2;
}

/* M7: one slider thumb centred at cx (the M4 clamp keeps the
 * circle inside the widget rect). Shared by the single and
 * dual-handle paints.
 * M9: the diameter is mode-specific - the WinUI thumb's outer
 * ellipse is 18epx at the device scale (Slider
 * themeresources, fetched in M8); the v2/classic form keeps
 * the 28px disc of the UI-STYLE-v2 design.
 * M22: at the re-derived x1.344 (AUDIT SS20) the WinUI thumb
 * is 24px (18epx = 24.2). `active` marks the grabbed handle
 * of a dual slider (the single form passes sld_grabbed). */
static void slider_thumb(tk_canvas *c, const tk_theme *t,
                         const tk_widget *w, tk_rect r, int cx,
                         bool active)
{
    int d = t->style == 2 ? 24 : 28;
    int kx = cx - d / 2;
    if (kx < r.x) kx = r.x;
    if (kx + d > r.x + r.w) kx = r.x + r.w - d;
    if (kx < r.x) kx = r.x;
    tk_rect tb = { kx, r.y + r.h / 2 - d / 2, d, d };
    int th = t->slider_track_h > 0 ? t->slider_track_h : 4;
    if (th > 4 && w->enabled) {
        tk_canvas_fill_round_rect(c, tb, d / 2, t->surface);
        tk_canvas_stroke_round_rect(c, tb, d / 2, 1, t->border);
        if (t->style != 2)
            tk_canvas_fill_round_rect(c,
                (tk_rect){kx + d / 2 - 6, r.y + r.h / 2 - 6,
                          12, 12}, 6, t->accent);
        else {
            /* M21: the WinUI thumb carries an accent inner
             * dot (12epx ellipse, rendered at the 0.86 rest
             * scale = 14px at the device scale). It was
             * skipped entirely, leaving a plain white disc
             * that vanished on light backgrounds.
             * M22: the dot's state scales are now modelled,
             * per the template values extracted in M21 SS5.1
             * (rest 0.86 / hover 1.167 / pressed 0.71 of the
             * 12epx dot; at x1.344: 14 / 19 / 11px). Pressed
             * applies to the grabbed handle; hover reads the
             * root's hover widget (either handle of a dual
             * slider takes it - the reference has no
             * first-party dual form to split it by). */
            int dsz = 14;
            if (active && w->pressed) dsz = 11;
            else {
                tk_root *rt = tk_widget_root(w);
                if (rt && rt->hover == w) dsz = 19;
            }
            tk_canvas_fill_round_rect(c,
                (tk_rect){kx + d / 2 - dsz / 2,
                          r.y + r.h / 2 - dsz / 2, dsz, dsz},
                dsz / 2, t->accent);
        }
    } else {
        /* M10 disabled thumb: M3 = on-surface at the 38%
         * disabled opacity; WinUI = ControlFillColorDisabled */
        tk_color df = t->border;
        if (!w->enabled && t->style == 1) {
            df = t->text; df.a = 97;
        } else if (!w->enabled && t->style == 2 &&
                   t->strong_fill_dis.a) {
            /* M21: the disabled thumb takes the strong fill
             * disabled gray, as the spans do - the
             * ControlFillColorDisabled wash was invisible */
            df = t->strong_fill_dis;
        }
        tk_canvas_fill_round_rect(c, tb, d / 2,
                                  w->enabled ? t->accent : df);
    }
}

/* M7/M16: the slider value indicator - a 48x44dp inverse
 * container above the active handle, shown while the handle
 * is dragged or stepped. v is the public value (range units
 * in range mode, a fraction otherwise). Factored out in M16
 * so the single form draws the same indicator as the Range
 * form (it was previously Range-only). */
static void slider_value_bubble(tk_canvas *c, const tk_theme *t,
                                tk_rect r, float frac, float v,
                                bool range)
{
    char vb[24];
    if (range) snprintf(vb, sizeof(vb), "%g", (double)v);
    else snprintf(vb, sizeof(vb), "%d%%", (int)(v * 100.0f + 0.5f));
    int acx = r.x + (int)(frac * (float)r.w);
    tk_rect bb = { acx - 28, r.y - 55, 56, 51 };
    if (bb.x < 0) bb.x = 0;
    if (bb.y < 0) bb.y = 0;
    tk_color ibg = t->inverse_surface.a ? t->inverse_surface
                                        : t->text;
    tk_color ifg = t->inverse_surface.a ? t->inverse_on_surface
                                        : t->bg;
    tk_canvas_fill_round_rect(c, bb, 8, ibg);
    int tw = tk_text_width(t, vb);
    tk_canvas_draw_text(c, t, bb.x + (bb.w - tw) / 2,
                        bb.y + (bb.h - tk_line_height(t)) / 2,
                        vb, ifg);
}

/* M20 fix: the check glyph for the 18px box language (the
 * CheckBox widget and the ListView checkable rows share it).
 * Two strokes of 2x2 blocks - the chip glyph's idiom - scaled
 * by gp around the box centre, the same growth the radio dot
 * uses. The pre-fix form filled a plain 10x10 square as the
 * "mark", which read as an empty frame: a checked box looked
 * unchecked next to the WinUI form's real check. */
static void check_glyph_arm(tk_canvas *c, float x0, float y0,
                            float x1, float y1, tk_color col)
{
    float dx = x1 - x0, dy = y1 - y0;
    int steps = (int)(sqrtf(dx * dx + dy * dy) + 0.5f);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int px = (int)(x0 + dx * (float)i / (float)steps + 0.5f);
        int py = (int)(y0 + dy * (float)i / (float)steps + 0.5f);
        tk_canvas_fill_rect(c, (tk_rect){ px - 1, py - 1, 2, 2 },
                            col);
    }
}

static void paint_check_glyph(tk_canvas *c, tk_rect box, float gp,
                              tk_color col)
{
    if (gp <= 0.0f) return;
    if (gp > 1.0f) gp = 1.0f;
    float cx = (float)box.x + (float)box.w / 2.0f;
    float cy = (float)box.y + (float)box.h / 2.0f;
    float sx = (float)box.w / 18.0f, sy = (float)box.h / 18.0f;
    /* full-size endpoints in 18px box coordinates.
     * M22: aligned to the material3 check path ratios
     * (Checkbox.kt, extracted in M21: left (0.2, 0.5),
     * cross (0.4, 0.7), right (0.8, 0.3) of the box, the
     * default flag-OFF values) - in 18px coordinates the
     * short arm (3.6,9)->(7.2,12.6), the long arm
     * (7.2,12.6)->(14.4,5.4). The pre-M22 endpoints
     * (4,9)->(7,12)->(13,5) sat up to 0.078 of the box
     * (~1.6px on the 21px box) inside the reference,
     * mostly on the long arm. The glyph is shared with
     * the WinUI form, whose reference mark is a font icon
     * (E73E) with no path coordinates in the template;
     * the shared check stays its equivalent there. */
    float ax = cx + (3.6f * sx - (float)box.w / 2.0f) * gp;
    float ay = cy + (9.0f * sy - (float)box.h / 2.0f) * gp;
    float vx = cx + (7.2f * sx - (float)box.w / 2.0f) * gp;
    float vy = cy + (12.6f * sy - (float)box.h / 2.0f) * gp;
    float ex = cx + (14.4f * sx - (float)box.w / 2.0f) * gp;
    float ey = cy + (5.4f * sy - (float)box.h / 2.0f) * gp;
    check_glyph_arm(c, ax, ay, vx, vy, col);
    check_glyph_arm(c, vx, vy, ex, ey, col);
}

/* M23: the M3 navigation indicator pill geometry, from the
 * reference (androidx material3 NavigationBar measure code +
 * the baseline M3 spec diagram): the pill wraps the ICON ONLY -
 * 64x32dp for the 24dp icon, i.e. the icon plus 20dp of
 * horizontal and 4dp of vertical padding per side, top edge
 * 4dp above the icon, CornerFull (radius = height/2), centred
 * on the icon; the label sits BELOW the pill, outside it.
 * At this panel's 1.12px/dp (AUDIT SS20): (20dp x2) rounds to
 * +45px of width, (4dp x2) to +9px of height, 4dp to 4px -
 * a 27px (24dp) icon gets the reference 72x36px pill.
 * (The current androidx dev branch has moved to the M3
 * Expressive 56dp width; the baseline form is what this bar
 * implements - Tall container - so 64dp stands. M23 report
 * SS5 records both.) */
static tk_rect nav_pill_rect(const tk_widget *cell, tk_rect r,
                             const tk_theme *t)
{
    int iw = cell->image_w > 0 ? cell->image_w : 27;
    int ih = cell->image_h > 0 ? cell->image_h : 27;
    int w = iw + 45, h = ih + 9;
    return (tk_rect){ r.x + (r.w - w) / 2, r.y + t->unit - 4, w, h };
}

void tk_leaf_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    if (w->kind >= TK_W_CARD) { tk_m7_paint(w, c, t); return; } /* M7 */
    tk_rect r = w->rect;
    tk_color fg = w->enabled ? t->text : t->text_dim;
    if (w->has_fg_override && w->enabled) fg = w->fg_override;
    switch (w->kind) {
    case TK_W_LABEL: {
        int lx = r.x;
        if (w->label_align != 0) {
            int tw = tk_w_text_w(w, t, w->text);
            if (tw < r.w)
                lx = w->label_align == 2 ? r.x + (r.w - tw)
                                         : r.x + (r.w - tw) / 2;
        }
        /* int12: a label stretched taller than its text (a box
         * row stretched by a taller sibling) used to pin the
         * glyphs to the rect's top edge, reading as vertical
         * misalignment against the sibling (blind round 3:
         * package "更新: 0件" vs the search field).  Center the
         * single text block vertically, like the M7 parts do. */
        int ly = r.y;
        int lh = tk_w_text_lh(w, t);
        if (lh > 0 && r.h > lh) ly = r.y + (r.h - lh) / 2;
        tk_w_draw_text(c, w, t, lx, ly, w->text, fg);
        break;
    }
    case TK_W_BUTTON: {
        /* UI-STYLE-v2: the theme may give buttons their own
         * radius (full/pill in v2); -1 keeps the classic radius */
        int brad = w->btn_radius >= 0 ? w->btn_radius
                   : t->radius_button >= 0 ? t->radius_button
                                           : t->radius;
        if (w->image_px && w->btn_icon_only) {
            /* icon-only (panel chrome): optional face, the icon
             * centered, optional running indicator along the
             * bottom edge. M6: a non-legacy variant supplies the
             * face (this is the FAB / filled-icon-button form). */
            if (w->btn_variant != TK_BTN_LEGACY) {
                (void)button_variant_face(w, c, t, r, brad);
            } else {
                if (w->btn_has_face)
                    tk_canvas_fill_round_rect(c, r, brad, w->btn_face);
                if (t->style == 1 && (w->pressed || w->ripple_on)) {
                    /* M17: the M3 pressed visual is the ripple
                     * (the uniform layer when none is running);
                     * the layer's shape is the full-round form */
                    tk_color o = t->text_dim; /* onSurfaceVariant */
                    o.a = 26; /* pressed 10% */
                    tk_ripple_paint(w, c, r, r.h / 2, o);
                } else if (t->style == 2) {
                    /* M13: the WinUI (AppBarButton) pressed
                     * form is the subtle fill; M17: it is the
                     * background brush, so it runs the 83 ms
                     * BrushTransition between transparent and
                     * the subtle fill */
                    tk_color tgt = w->pressed
                        ? (t->subtle.a ? t->subtle
                                       : t->surface_alt)
                        : (tk_color){ 0, 0, 0, 0 };
                    tk_color bc = tk_face_brush(w, t, tgt);
                    if (bc.a)
                        tk_canvas_fill_round_rect(c, r, t->radius,
                                                  bc);
                } else if (w->pressed) {
                    /* classic keeps the surface_alt wash */
                    tk_canvas_fill_round_rect(c, r, brad,
                                              t->surface_alt);
                }
            }
            if (w->split_menu) {
                /* M8: split button arrow - the chevron rotates
                 * 180 degrees while its menu is open (chev_pos
                 * is animated by tk_m7.c) */
                tk_canvas_draw_image_rot(c, w->image_px, w->image_w,
                    w->image_h,
                    (tk_rect){ r.x + (r.w - w->image_w) / 2,
                               r.y + (r.h - w->image_h) / 2,
                               w->image_w, w->image_h },
                    w->chev_pos * 180.0f);
            } else {
                blit_image(c, w->image_px, w->image_w, w->image_h,
                           (tk_rect){ r.x + (r.w - w->image_w) / 2,
                                      r.y + (r.h - w->image_h) / 2,
                                      w->image_w, w->image_h });
            }
            if (w->btn_has_indicator)
                tk_canvas_fill_rect(c,
                    (tk_rect){ r.x + brad, r.y + r.h - 3,
                               r.w - 2 * brad, 3 }, w->btn_indicator);
            break;
        }
        if (w->image_px && w->btn_icon_layout == TK_BTN_ICON_ROW) {
            /* M6: icon + label in a row (apps-envfit SS6-2) - the
             * primary bottom button / dialog action form. The
             * face comes from the variant, or from the legacy
             * accent/flat/neutral rules when variant is LEGACY. */
            tk_color cfg = fg;
            if (!w->enabled && t->style == 2 &&
                w->btn_variant == TK_BTN_LEGACY && !w->accent_btn)
                cfg = btn_disabled_fg(t); /* M10: TextFillDisabled */
            if (w->btn_variant != TK_BTN_LEGACY) {
                cfg = button_variant_face(w, c, t, r, brad);
            } else if (t->style == 2 && w->btn_toggle &&
                       w->checked) {
                /* M11: WinUI toggle checked face (icon-row
                 * form) - same painter as the text form */
                cfg = winui_toggle_checked_face(w, c, t, r, brad);
            } else if (w->accent_btn) {
                /* M10: the shared primary-face painter */
                cfg = accent_button_face(w, c, t, r, brad);
            } else if (w->btn_toggle && w->checked) {
                /* M15 fix: the LEGACY toggle's checked form
                 * (the container face) - the text path has had
                 * it since M7 and the WinUI icon-row branch
                 * since M11, but the M3 / classic icon-row
                 * neutral branch never consulted `checked`,
                 * so a checked toggle with an icon showed no
                 * state at all. The content keeps the neutral
                 * foreground, as in the text path. */
                tk_canvas_fill_round_rect(c, r, brad, t->container);
            } else if (w->btn_flat && !w->btn_has_face) {
                if (w->pressed)
                    tk_canvas_fill_round_rect(c, r, brad, t->surface_alt);
            } else {
                /* M10: same neutral state split as the text
                 * form (tertiary/disabled fills in WinUI, the
                 * M3 state layer in style 1) */
                tk_color bgc = w->btn_has_face ? w->btn_face
                             : w->pressed ? t->surface_alt : t->surface;
                if (!w->btn_has_face && t->style == 2) {
                    if (!w->enabled && t->control_fill_dis.a)
                        bgc = t->control_fill_dis;
                    else if (w->pressed && t->control_fill3.a)
                        bgc = t->control_fill3;
                }
                /* M17: in WinUI mode the fill is the
                 * background brush (83 ms transition) */
                tk_canvas_fill_round_rect(c, r, brad,
                                          tk_face_brush(w, t, bgc));
                if (!w->btn_has_face && t->style == 1 &&
                    (w->pressed || w->ripple_on) && w->enabled) {
                    tk_color o = t->accent; o.a = 26;
                    tk_ripple_paint(w, c, r, brad, o);
                }
                if (!w->btn_flat) {
                    if (brad > 8)
                        tk_canvas_stroke_round_rect(c, r, brad, 1, t->border);
                    else
                        tk_canvas_stroke_rect(c, r, 1, t->border);
                }
            }
            char lbl[512];
            fit_label(lbl, sizeof(lbl), w, t, w->text,
                      r.w - w->image_w - 5 * t->unit);
            int tw = tk_w_text_w(w, t, lbl);
            int gw = w->image_w + (tw ? t->unit + tw : 0);
            int gx = button_content_x(w, t, r, gw);
            blit_image(c, w->image_px, w->image_w, w->image_h,
                       (tk_rect){ gx, r.y + (r.h - w->image_h) / 2,
                                  w->image_w, w->image_h });
            if (tw)
                tk_w_draw_text(c, w, t, gx + w->image_w + t->unit,
                               r.y + (r.h - tk_w_text_lh(w, t)) / 2,
                               lbl, cfg);
            if (w->btn_has_indicator)
                tk_canvas_fill_rect(c,
                    (tk_rect){ r.x + brad, r.y + r.h - 3,
                               r.w - 2 * brad, 3 }, w->btn_indicator);
            break;
        }
        if (w->image_px) {
            /* icon cell (desktop icons): no card background or
             * border - just the icon, the label under it, and a
             * subtle wash while pressed. M16: a bottombar
             * selection face (btn_face_indicator with no custom
             * face) is painted by the bar itself
             * (tk_bottombar_paint_indicator), which owns the
             * indicator's animated position; the cell skips
             * its own copy. */
            bool bb_face = w->btn_face_indicator &&
                           !w->btn_has_face && !w->btn_face_container;
            if ((w->btn_has_face || w->btn_face_container ||
                 (w->btn_face_indicator && !bb_face)) &&
                w->btn_face_pill) {
                /* M7: the M3 navigation-bar indicator - a pill
                 * behind the icon only; in the WinUI mode the
                 * NavigationView form (subtle cell fill + the
                 * accent bar at the left edge). The pill colour
                 * is the stored face, or the paint theme's
                 * container token for a bottombar selection
                 * (btn_face_container). */
                if (t->style == 2) {
                    tk_canvas_fill_rect(c, r,
                        t->subtle.a ? t->subtle : t->surface_alt);
                    tk_canvas_fill_rect(c,
                        (tk_rect){ r.x, r.y + (r.h - 24) / 2, 3,
                                   24 }, t->accent);
                } else {
                    /* M13: a navigation indicator pill takes
                     * secondaryContainer (selection) in the M3
                     * mode - the pre-M13 code used
                     * primaryContainer here as in the rail /
                     * drawer indicators (same role error, fixed
                     * together). A TONAL face keeps
                     * primaryContainer. */
                    tk_rect pr = nav_pill_rect(w, r, t);
                    tk_canvas_fill_round_rect(c, pr, pr.h / 2,
                        w->btn_face_indicator ? t->selection
                            : w->btn_face_container ? t->container
                                                    : w->btn_face);
                }
            } else if (w->btn_has_face || w->btn_face_container ||
                       (w->btn_face_indicator && !bb_face))
                /* the non-pill (tonal) form keeps the
                 * primaryContainer face for both flags */
                tk_canvas_fill_round_rect(c, r, brad,
                    (w->btn_face_container || w->btn_face_indicator)
                        ? t->container : w->btn_face);
            if (w->pressed)
                tk_canvas_fill_round_rect(c, r, t->radius, t->surface_alt);
            blit_image(c, w->image_px, w->image_w, w->image_h,
                       (tk_rect){ r.x + (r.w - w->image_w) / 2,
                                  r.y + t->unit, w->image_w, w->image_h });
            char lbl[512];
            fit_label(lbl, sizeof(lbl), w, t, w->text, r.w - t->unit);
            int tw = tk_w_text_w(w, t, lbl);
            int lx = r.x + (r.w - tw) / 2;
            int ly = r.y + t->unit + w->image_h + t->unit;
            if (w->text_shadow) /* 1px dark offset copy (wallpaper) */
                tk_w_draw_text(c, w, t, lx + 1, ly + 1, lbl,
                               (tk_color){ 24, 24, 32, 255 });
            tk_w_draw_text(c, w, t, lx, ly, lbl, fg);
            if (w->btn_has_indicator)
                tk_canvas_fill_rect(c,
                    (tk_rect){ r.x + brad, r.y + r.h - 3,
                               r.w - 2 * brad, 3 }, w->btn_indicator);
            break;
        }
        if (w->btn_variant != TK_BTN_LEGACY) {
            /* M6: first-class variant, text-only form */
            tk_color tc = button_variant_face(w, c, t, r, brad);
            int tw = tk_w_text_w(w, t, w->text);
            tk_w_draw_text(c, w, t, button_content_x(w, t, r, tw),
                           r.y + (r.h - tk_w_text_lh(w, t)) / 2,
                           w->text, tc);
            if (w->btn_has_indicator)
                tk_canvas_fill_rect(c,
                    (tk_rect){ r.x + brad, r.y + r.h - 3,
                               r.w - 2 * brad, 3 }, w->btn_indicator);
            break;
        }
        if (w->accent_btn) {
            /* primary action: accent face, on_accent text.
             * M10: the state forms come from the shared
             * primary-face painter (mode spec forms; classic
             * keeps the darkened pressed / surface_alt
             * disabled forms inside it). */
            tk_color tc = accent_button_face(w, c, t, r, brad);
            int tw = tk_w_text_w(w, t, w->text);
            tk_w_draw_text(c, w, t, button_content_x(w, t, r, tw),
                           r.y + (r.h - tk_w_text_lh(w, t)) / 2,
                           w->text, tc);
            break;
        }
        tk_color leg_fg = fg;
        bool leg_fg_set = false;
        if (w->btn_toggle && w->checked && t->style == 2) {
            /* M11: the WinUI LEGACY toggle's checked form is
             * the ToggleButton accent face - the M7 branch
             * below excluded style 2, so the checked state
             * never rose above the neutral control fill */
            leg_fg = winui_toggle_checked_face(w, c, t, r, brad);
            leg_fg_set = true;
        } else if (w->btn_toggle && w->checked) {
            /* M7: the LEGACY toggle's checked form gains the
             * container face */
            tk_canvas_fill_round_rect(c, r, brad, t->container);
        } else if (w->btn_flat && !w->btn_has_face) {
            /* flat (header-bar style): no face or border at rest,
             * a wash while pressed */
            if (w->pressed)
                tk_canvas_fill_round_rect(c, r, brad, t->surface_alt);
        } else {
            /* M10 states for the neutral legacy face: WinUI
             * pressed = ControlFillColorTertiary, disabled =
             * ControlFillColorDisabled; M3 pressed = the
             * primary state layer at 10% over the rest face.
             * Classic keeps the surface_alt wash. */
            tk_color bgc = w->btn_has_face ? w->btn_face
                         : w->pressed ? t->surface_alt
                         : (t->style == 2 && t->control_fill.a)
                               ? t->control_fill : t->surface;
            if (!w->btn_has_face && t->style == 2) {
                if (!w->enabled && t->control_fill_dis.a)
                    bgc = t->control_fill_dis;
                else if (w->pressed && t->control_fill3.a)
                    bgc = t->control_fill3;
            }
            /* M17: in WinUI mode the fill is the background
             * brush (83 ms transition) */
            tk_canvas_fill_round_rect(c, r, brad,
                                      tk_face_brush(w, t, bgc));
            if (!w->btn_has_face && t->style == 1 &&
                (w->pressed || w->ripple_on) && w->enabled) {
                tk_color o = t->accent; o.a = 26;
                tk_ripple_paint(w, c, r, brad, o);
            }
            if (!w->btn_flat) {
                tk_color bc = (t->style == 2 && t->control_stroke.a)
                                  ? t->control_stroke : t->border;
                if (brad > 8)
                    tk_canvas_stroke_round_rect(c, r, brad, 1, bc);
                else
                    tk_canvas_stroke_rect(c, r, 1, bc);
            }
        }
        int tw = tk_w_text_w(w, t, w->text);
        tk_w_draw_text(c, w, t, button_content_x(w, t, r, tw),
                       r.y + (r.h - tk_w_text_lh(w, t)) / 2, w->text,
                       leg_fg_set ? leg_fg
                           : (!w->enabled && t->style == 2)
                                 ? btn_disabled_fg(t) : fg);
        if (w->btn_has_indicator)
            tk_canvas_fill_rect(c,
                (tk_rect){ r.x + brad, r.y + r.h - 3,
                           r.w - 2 * brad, 3 }, w->btn_indicator);
        break;
    }
    case TK_W_CHECKBOX: {
        if (t->style == 2) {
            /* WinUI CheckBox: 20epx box at the device scale
             * (28px, see the report's conversion table), 4epx
             * corners; checked = accent fill + check glyph.
             * M10 states (CheckBox themeresources + the Fluent
             * token table): the unchecked border is the STRONG
             * stroke (ControlStrongStrokeColorDefault), not the
             * faint control stroke; disabled = ControlFill-
             * ColorDisabled fill, the disabled strong stroke,
             * and TextFillColorDisabled for the glyph/label. */
            tk_rect box = { r.x, r.y + (r.h - 28) / 2, 28, 28 };
            if (w->checked && !w->enabled) {
                /* disabled checked: steady form (M10) */
                tk_color bf = t->control_fill_dis.a
                    ? t->control_fill_dis : t->surface_alt;
                tk_color gf = t->text_dis.a ? t->text_dis
                                            : t->text_dim;
                tk_canvas_fill_round_rect(c, box, 4, bf);
                for (int s = 0; s <= 5; s++)
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ box.x + 6 + s, box.y + 14 + s,
                                   3, 3 }, 1, gf);
                for (int s = 0; s <= 10; s++)
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ box.x + 11 + s, box.y + 19 - s,
                                   3, 3 }, 1, gf);
            } else if (w->enabled && w->glyph_pos > 0.0f) {
                /* M13: the checked form at glyph fraction gp -
                 * the unchecked base, the accent fill at
                 * alpha gp over it, the check at alpha gp.
                 * At gp = 1 this is pixel-identical to the
                 * pre-M13 steady checked form. */
                float gp = w->glyph_pos;
                tk_canvas_fill_round_rect(c, box, 4,
                    t->control_fill.a ? t->control_fill
                                      : t->surface);
                tk_color bf = t->accent;
                bf.a = (uint8_t)(255.0f * gp + 0.5f);
                tk_canvas_fill_round_rect(c, box, 4, bf);
                tk_color gf = t->on_accent;
                gf.a = (uint8_t)(255.0f * gp + 0.5f);
                for (int s = 0; s <= 5; s++)
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ box.x + 6 + s, box.y + 14 + s,
                                   3, 3 }, 1, gf);
                for (int s = 0; s <= 10; s++)
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ box.x + 11 + s, box.y + 19 - s,
                                   3, 3 }, 1, gf);
            } else {
                tk_canvas_fill_round_rect(c, box, 4,
                    !w->enabled && t->control_fill_dis.a
                        ? t->control_fill_dis
                        : (t->control_fill.a ? t->control_fill
                                             : t->surface));
                tk_canvas_stroke_round_rect(c, box, 4, 1,
                    !w->enabled
                        ? (t->strong_stroke_dis.a
                               ? t->strong_stroke_dis : t->border)
                        : (t->strong_stroke.a
                               ? t->strong_stroke
                               : (t->control_stroke.a
                                      ? t->control_stroke
                                      : t->border)));
            }
            tk_canvas_draw_text(c, t, r.x + 28 + t->unit,
                                r.y + (r.h - tk_line_height(t)) / 2,
                                w->text, fg);
            break;
        }
        /* M21: the M3 box is 21px (AUDIT (a-2): the 20dp
         * effective box converts to 22.4px at ~179.2ppi and
         * 21px is retained); the classic theme keeps 18px. */
        int cbs = t->style == 1 ? 21 : 18;
        tk_rect box = { r.x, r.y + (r.h - cbs) / 2, cbs, cbs };
        /* M10 states (style 1, M3): the disabled selected box
         * is on-surface at the 38% disabled opacity with the
         * surface-coloured mark; the disabled unselected
         * outline is on-surface at 38%. Classic unchanged. */
        bool dis1 = (t->style == 1 && !w->enabled);
        tk_color dis38 = t->text; dis38.a = 97;
        if (dis1) {
            tk_canvas_fill_round_rect(c, box, 3,
                w->checked ? dis38 : t->surface);
            tk_canvas_stroke_round_rect(c, box, 3, 1, dis38);
            if (w->checked)
                paint_check_glyph(c, box, 1.0f, t->surface);
        } else {
            /* M13: the checked form at glyph fraction gp -
             * the accent fill at alpha gp over the unchecked
             * base (fill order matches the pre-M13 paint:
             * fills, stroke, mark), the mark scaled by gp.
             * At gp = 1 the fill is pixel-identical to the
             * pre-M13 steady form. M20 fix: the mark itself
             * was a plain filled square until now; it is the
             * check glyph (paint_check_glyph), and the box
             * outline follows the r3 rounded fill instead of
             * a square stroke. */
            float gp = w->glyph_pos;
            tk_canvas_fill_round_rect(c, box, 3, t->surface);
            if (gp > 0.0f) {
                tk_color af = t->accent;
                af.a = (uint8_t)(255.0f * gp + 0.5f);
                tk_canvas_fill_round_rect(c, box, 3, af);
            }
            tk_canvas_stroke_round_rect(c, box, 3, 1, t->border);
            if (gp > 0.0f)
                paint_check_glyph(c, box, gp, t->on_accent);
        }
        tk_canvas_draw_text(c, t, r.x + cbs + 2 + t->unit,
                            r.y + (r.h - tk_line_height(t)) / 2, w->text, fg);
        break;
    }
    case TK_W_RADIO: {
        if (t->style == 2) {
            /* WinUI RadioButton: outer circle, accent ring +
             * dot when selected. M10 states: the unselected
             * ring is the strong stroke (as with CheckBox);
             * disabled = ControlFillColorDisabled fill, the
             * disabled strong stroke ring, and the disabled
             * strong fill for the glyph. */
            tk_rect box = { r.x, r.y + (r.h - 28) / 2, 28, 28 };
            tk_canvas_fill_round_rect(c, box, 14,
                !w->enabled && t->control_fill_dis.a
                    ? t->control_fill_dis
                    : (t->control_fill.a ? t->control_fill
                                         : t->surface));
            tk_canvas_stroke_round_rect(c, box, 14,
                (w->selected || w->glyph_pos > 0.0f) ? 2 : 1,
                (w->selected || w->glyph_pos > 0.0f)
                    ? (w->enabled ? t->accent
                                  : (t->strong_stroke_dis.a
                                         ? t->strong_stroke_dis
                                         : t->border))
                    : (!w->enabled
                           ? (t->strong_stroke_dis.a
                                  ? t->strong_stroke_dis : t->border)
                           : (t->strong_stroke.a
                                  ? t->strong_stroke
                                  : (t->control_stroke.a
                                         ? t->control_stroke
                                         : t->border))));
            if (w->selected || w->glyph_pos > 0.0f) {
                /* M8: the WinUI check glyph is a 12epx ellipse
                 * (RadioButtonCheckGlyphSize in the official
                 * themeresources) = 17px at the device scale.
                 * M13: the dot scales with the glyph fraction
                 * during the transition; at gp = 1 the form is
                 * the pre-M13 one. Disabled keeps the steady
                 * full-size dot. */
                int sz = w->enabled
                    ? (int)(17.0f * w->glyph_pos + 0.5f) : 17;
                if (sz > 0)
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ box.x + 6 + (17 - sz) / 2,
                                   box.y + 6 + (17 - sz) / 2,
                                   sz, sz }, sz / 2,
                        w->enabled ? t->accent
                                   : (t->strong_fill_dis.a
                                          ? t->strong_fill_dis
                                          : t->text_dim));
            }
            tk_canvas_draw_text(c, t, r.x + 28 + t->unit,
                                r.y + (r.h - tk_line_height(t)) / 2,
                                w->text, fg);
            break;
        }
        /* M21: the M3 ring is 21px with a 9px dot (AUDIT
         * (a-3): at ~179.2ppi the 20dp ring converts to
         * 22.4px and the dot to ~11.2px; 21 / 9 retained);
         * the classic theme keeps the 18px ring and 8px
         * dot. */
        int rbs = t->style == 1 ? 21 : 18;
        int rdsz = t->style == 1 ? 9 : 8;
        int rdoff = (rbs - rdsz) / 2;
        tk_rect box = { r.x, r.y + (r.h - rbs) / 2, rbs, rbs };
        /* M10 states (style 1, M3): disabled ring and dot are
         * on-surface at the 38% disabled opacity. */
        bool dis1 = (t->style == 1 && !w->enabled);
        tk_color dis38 = t->text; dis38.a = 97;
        tk_canvas_fill_round_rect(c, box, rbs / 2, t->surface);
        if (dis1)
            tk_canvas_stroke_round_rect(c, box, rbs / 2, 1, dis38);
        else
            /* M20 fix: the ring is a circle (the r9 fill);
             * the pre-fix code stroked a square over it, so
             * the enabled M3 radio read as a rounded square */
            tk_canvas_stroke_round_rect(c, box, rbs / 2, 1, t->border);
        if (w->selected || w->glyph_pos > 0.0f) {
            /* M13: the dot scales with the glyph fraction
             * during the transition; at gp = 1 (and for the
             * disabled steady form) it is the full-size dot */
            int sz = dis1 ? rdsz
                : (int)(rdsz * w->glyph_pos + 0.5f);
            if (sz > 0)
                tk_canvas_fill_round_rect(c,
                    (tk_rect){ box.x + rdoff + (rdsz - sz) / 2,
                               box.y + rdoff + (rdsz - sz) / 2, sz, sz },
                    sz / 2, dis1 ? dis38 : t->accent);
        }
        tk_canvas_draw_text(c, t, r.x + rbs + 2 + t->unit,
                            r.y + (r.h - tk_line_height(t)) / 2, w->text, fg);
        break;
    }
    case TK_W_SLIDER: {
        /* UI-REDESIGN 2.2: thumb a true circle of diameter 28,
         * kept fully inside the rect (the M4 clamp, generalised);
         * the widget's 48px height is the hit area. UI-STYLE-v2:
         * the track thickness is a theme token (4 classic, 16 in
         * v2 - the thick ChromeOS QS / M3 2024 track), and the v2
         * handle is a white circle with an accent centre. */
        int th = t->slider_track_h > 0 ? t->slider_track_h : 4;
        int track_y = r.y + r.h / 2 - th / 2;
        /* M10 disabled track (M3 slider tokens: active =
         * on-surface 38%, inactive = on-surface 12%; the
         * WinUI disabled pair is set in the M21 note below) */
        tk_color s_act = t->accent, s_inact = t->surface_alt;
        /* M21: the WinUI rail is a strong neutral fill (the
         * SliderTrackFill lineage - a mid-gray foreground
         * ramp in UWP, the strong family in Fluent, cf. the
         * ProgressBar track's ControlStrongStroke). The
         * surface_alt token (#F9F9F9) left the rail invisible
         * on the page background, and the disabled pair took
         * the near-white ControlFillColorDisabled the same
         * way; both now take the theme's strong-fill pair. */
        if (t->style == 2 && t->strong_fill.a)
            s_inact = t->strong_fill;
        if (!w->enabled && t->style == 1) {
            s_act = t->text; s_act.a = 97;
            s_inact = t->text; s_inact.a = 31;
        } else if (!w->enabled && t->style == 2 &&
                   t->strong_fill_dis.a) {
            s_act = s_inact = t->strong_fill_dis;
        }
        /* M13: the painted position is the DISPLAYED fraction
         * (sld_disp) - equal to fvalue at rest and after any
         * direct set; a programmatic set animates it */
        int fill = (int)(w->sld_disp * (float)r.w);
        /* M21: the inactive span is drawn only outside the
         * active span (the junctions sit under the thumbs),
         * so an alpha track colour no longer double-blends
         * where the active span overlays it - the disabled
         * forms, whose two spans share one alpha colour,
         * showed a step at the midpoint. */
        if (w->sld_dual) {
            int lo0 = (int)(w->sld_disp2 * (float)r.w);
            if (lo0 > 0)
                tk_canvas_fill_round_rect(c,
                    (tk_rect){r.x, track_y, lo0, th}, th / 2,
                    s_inact);
            if (fill < r.w)
                tk_canvas_fill_round_rect(c,
                    (tk_rect){r.x + fill, track_y, r.w - fill, th},
                    th / 2, s_inact);
        } else if (fill < r.w) {
            tk_canvas_fill_round_rect(c,
                (tk_rect){r.x + fill, track_y, r.w - fill, th},
                th / 2, s_inact);
        }
        if (w->sld_dual) {
            /* M7 Range form: only the span between the handles
             * is filled */
            int lo = (int)(w->sld_disp2 * (float)r.w);
            tk_canvas_fill_round_rect(c,
                (tk_rect){r.x + lo, track_y, fill - lo, th},
                th / 2, s_act);
        } else {
            tk_canvas_fill_round_rect(c, (tk_rect){r.x, track_y, fill, th},
                                      th / 2, s_act);
        }
        /* M6: discrete range sliders draw a stop indicator at
         * each step (M3 "stops" configuration) */
        if (w->sld_range && w->fstep > 0 && w->fmax > w->fmin) {
            int nsteps = (int)((w->fmax - w->fmin) / w->fstep + 0.5f);
            if (nsteps > 0 && nsteps <= 200) {
                for (int k = 0; k <= nsteps; k++) {
                    int sx = r.x + (int)((float)k / (float)nsteps *
                                         (float)r.w);
                    if (sx < r.x + 1) sx = r.x + 1;
                    if (sx > r.x + r.w - 2) sx = r.x + r.w - 2;
                    tk_canvas_fill_rect(c,
                        (tk_rect){ sx, track_y + th / 2 - 1, 2, 2 },
                        t->surface);
                }
            }
        }
        if (w->sld_dual) {
            slider_thumb(c, t, w, r,
                         r.x + (int)(w->sld_disp2 * (float)r.w),
                         w->sld_grabbed && w->sld_active == 0);
            slider_thumb(c, t, w, r, r.x + fill,
                         w->sld_grabbed && w->sld_active == 1);
            /* M7: the value indicator above the active
             * handle, shown while it is dragged or stepped */
            if (w->sld_grabbed) {
                float v = w->sld_active == 0
                              ? tk_slider_value_lo(w)
                              : tk_slider_value_hi(w);
                slider_value_bubble(c, t, r,
                                    w->sld_active == 0
                                        ? w->fvalue2 : w->fvalue,
                                    v, w->sld_range);
            }
        } else {
            slider_thumb(c, t, w, r, r.x + fill, w->sld_grabbed);
            /* M16: the single form draws the same value
             * indicator while dragged (the M3 slider value
             * indicator / the WinUI thumb-tooltip role); it
             * was previously drawn only for the Range form,
             * which left the single form with no value text
             * at all (AUDIT #9 E) */
            if (w->sld_grabbed)
                slider_value_bubble(c, t, r, w->fvalue,
                                    tk_slider_value_range(w),
                                    w->sld_range);
        }
        break;
    }
    case TK_W_TEXTFIELD: {
        /* M6: with supporting text set, the caption line lives
         * under the container; the container keeps the rest.
         * M7: the line also carries the character counter and
         * the validator's error message. */
        tk_rect fr = r;
        bool tf_err = !w->tf_valid && w->tf_error_msg &&
                      w->tf_error_msg[0];
        int support_h = 0;
        if ((w->support_text && w->support_text[0]) ||
            w->tf_counter || tf_err) {
            support_h = tk_line_height_st(t, TK_TEXT_CAPTION) + 4;
            if (fr.h > support_h) fr.h -= support_h;
        }
        /* UI-STYLE-v2: filled tonal pill (the M3 filled field /
         * search bar), no outline; classic keeps the outlined
         * surface field. WinUI mode: the TextBox form - control
         * fill, control stroke, control radius. */
        if (t->style == 2) {
            /* M10: the disabled TextBox face is
             * ControlFillColorDisabled */
            tk_color cf = !w->enabled && t->control_fill_dis.a
                            ? t->control_fill_dis
                            : (t->control_fill.a ? t->control_fill
                                                 : t->surface);
            tk_color cs = t->control_stroke.a ? t->control_stroke
                                              : t->border;
            tk_canvas_fill_round_rect(c, fr, t->radius, cf);
            tk_canvas_stroke_round_rect(c, fr, t->radius, 1,
                                        (w->support_error || tf_err)
                                            ? t->danger : cs);
        } else if (t->field_filled) {
            /* M10: the M3 disabled filled container is
             * on-surface at the 12% disabled opacity */
            if (t->style == 1 && !w->enabled) {
                tk_color df = t->text; df.a = 31;
                tk_canvas_fill_round_rect(c, fr, fr.h / 2, df);
            } else {
                tk_canvas_fill_round_rect(c, fr, fr.h / 2,
                                          t->surface_alt);
            }
            if (w->support_error || tf_err) /* M6: error underline */
                tk_canvas_fill_rect(c, (tk_rect){fr.x + fr.h / 2,
                    fr.y + fr.h - 2, fr.w - fr.h, 2}, t->danger);
        } else {
            tk_canvas_fill_round_rect(c, fr, t->radius, t->surface);
            tk_canvas_stroke_rect(c, fr, 1,
                                  (w->support_error || tf_err)
                                      ? t->danger : t->border);
        }
        /* M7: prefix (dim, inside the container) shifts the
         * text; the suffix sits right-aligned after the text */
        int tx0 = r.x + t->unit;
        int ty0 = fr.y + (fr.h - tk_line_height(t)) / 2;
        if (w->tf_prefix) {
            tk_canvas_draw_text(c, t, tx0, ty0, w->tf_prefix,
                                t->text_dim);
            tx0 += tk_text_width(t, w->tf_prefix) + t->unit / 2;
        }
        if (w->tf_suffix) {
            int sw = tk_text_width(t, w->tf_suffix);
            tk_canvas_draw_text(c, t, fr.x + fr.w - t->unit - sw,
                                ty0, w->tf_suffix, t->text_dim);
        }
        char buf[1024];
        tc_text(w->buffer, buf, sizeof(buf));
        char comp[512];
        size_t clen = tc_composition(w->buffer, comp, sizeof(comp));
        tk_canvas_push_clip(c, (tk_rect){fr.x + 2, fr.y, fr.w - 4, fr.h});
        int ty = fr.y + (fr.h - tk_line_height(t)) / 2;
        if (!clen && !buf[0] && w->placeholder) {
            /* the placeholder is not user input: never masked.
             * M8: it is also not text - both references draw it
             * in the dim/secondary role (M3 on-surface-variant,
             * WinUI placeholder text fill), not at full strength */
            tk_canvas_draw_text(c, t, tx0, ty,
                                w->placeholder, t->text_dim);
        } else if (!clen) {
            if (w->password) {
                char m[3 * 1024 + 1];
                mask_write(m, buf, strlen(buf));
                tk_canvas_draw_text(c, t, tx0, ty, m, fg);
            } else {
                tk_canvas_draw_text(c, t, tx0, ty, buf, fg);
            }
        } else {
            /* The composition belongs at the caret, not appended
             * at the end: draw prefix, segmented composition,
             * suffix. In password mode all three parts are masked
             * (one glyph per codepoint, so positions still line
             * up with the caret and the segment underlines). */
            size_t cur = tc_cursor(w->buffer);
            size_t blen = strlen(buf);
            if (cur > blen) cur = blen;
            int x = tx0;
            if (w->password) {
                char m[3 * 1024 + 1];
                mask_write(m, buf, cur);
                tk_canvas_draw_text(c, t, x, ty, m, fg);
                x += tk_text_width(t, m);
                x += tk_draw_composition(c, t, w->buffer, x, ty, fg, true);
                mask_write(m, buf + cur, blen - cur);
                tk_canvas_draw_text(c, t, x, ty, m, fg);
            } else {
                char pre[1024];
                size_t pl = cur < sizeof(pre) ? cur : sizeof(pre) - 1;
                memcpy(pre, buf, pl);
                pre[pl] = '\0';
                tk_canvas_draw_text(c, t, x, ty, pre, fg);
                x += tk_text_width(t, pre);
                x += tk_draw_composition(c, t, w->buffer, x, ty, fg, false);
                tk_canvas_draw_text(c, t, x, ty, buf + cur, fg);
            }
        }
        tk_canvas_pop_clip(c);
        if (support_h) {
            /* M7: a validator error replaces the supporting
             * text until the input is fixed (the M3 rule) */
            const char *line = tf_err ? w->tf_error_msg
                                      : w->support_text;
            if (line && line[0])
                tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                    fr.x + t->unit, fr.y + fr.h + 4, line,
                    (w->support_error || tf_err) ? t->danger
                                                 : t->text_dim);
            if (w->tf_counter) {
                char cnt[32];
                if (w->tf_max_len > 0)
                    snprintf(cnt, sizeof(cnt), "%d / %d",
                             (int)tc_count(w->buffer),
                             w->tf_max_len);
                else
                    snprintf(cnt, sizeof(cnt), "%d",
                             (int)tc_count(w->buffer));
                int cw = tk_text_width_st(t, TK_TEXT_CAPTION, cnt);
                tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                    fr.x + fr.w - t->unit - cw, fr.y + fr.h + 4,
                    cnt, t->text_dim);
            }
        }
        break;
    }
    case TK_W_SCROLLVIEW: {
        /* M5: on a shift fast-path frame the retained pixels are
         * already in place - repainting the background would erase
         * them. The surface is refilled only where new pixels are
         * about to be drawn: the exposed band and the scrollbar
         * strip; then the scrollbar thumb itself. */
        if (w->sv_fast_frame) {
            tk_canvas_fill_rect(c, w->sv_band, t->surface);
            tk_canvas_fill_rect(c, w->sv_strip, t->surface);
        } else {
            tk_canvas_fill_rect(c, r, t->surface);
        }
        /* scrollbar thumb */
        if (w->n_children) {
            int content_h = w->children[0]->desired.h;
            if (content_h > r.h && r.h > 0) {
                tk_rect th = tk_sv_thumb_rect(w, w->scroll_y);
                tk_canvas_fill_round_rect(c, th, 2, t->border);
            }
        }
        break;
    }
    case TK_W_LISTVIEW: {
        /* UI-STYLE-v2: the list is a rounded group card (radius
         * from the theme, soft outlineVariant border); classic
         * keeps the square surface + border */
        if (t->radius_list > 0) {
            tk_canvas_fill_round_rect(c, r, t->radius_list, t->surface);
            tk_canvas_stroke_round_rect(c, r, t->radius_list, 1,
                                        t->border_line);
        } else {
            tk_canvas_fill_rect(c, r, t->surface);
            tk_canvas_stroke_rect(c, r, 1, t->border);
        }
        int rh = lv_row_h(w, t);
        int rows = r.h / rh;
        int lh = tk_line_height(t);
        int lh_cap = tk_line_height_st(t, TK_TEXT_CAPTION);
        tk_canvas_push_clip(c, r);
        /* M5: row-level culling - rows outside the effective clip
         * (widget rect ∩ damage) are skipped outright instead of
         * issuing fills/text the canvas then discards per pixel. */
        tk_rect cb = tk_canvas_clip_bounds(c);
        for (int i = 0; i < rows; i++) {
            int idx = w->top_index + i;
            if (idx < 0 || idx >= (int)w->n_items) break;
            tk_rect row = { r.x, r.y + i * rh, r.w, rh };
            if (row.y + row.h <= cb.y) continue;
            if (row.y >= cb.y + cb.h) break;
            if (lv_flag(w, idx, TK_LV_FLAG_HEADER)) {
                /* M5 section header: caption-weight, dim, no pill,
                 * no selection - a label for the rows below it */
                tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                                       r.x + 2 * t->unit,
                                       row.y + (rh - lh_cap) / 2,
                                       w->items[idx],
                                       w->enabled ? t->text_dim : fg);
                continue;
            }
            if (t->style == 2) {
                /* WinUI ListView selection: subtle fill + the
                 * accent bar at the left edge */
                if (idx == w->marked && idx != w->sel)
                    tk_canvas_fill_rect(c, row, t->surface_alt);
                if (idx == w->sel) {
                    tk_canvas_fill_rect(c, row,
                                        t->subtle.a ? t->subtle
                                                    : t->selection);
                    tk_canvas_fill_rect(c,
                                        (tk_rect){ r.x, row.y, 3, rh },
                                        t->accent);
                }
            } else if (t->radius_list > 0) {
                /* UI-STYLE-v2: selection/marked are inset pills
                 * (the M3 navigation-indicator language) instead
                 * of full-bleed rows with an edge bar */
                tk_rect pill = { row.x + t->unit, row.y + 2,
                                 row.w - 2 * t->unit, rh - 4 };
                if (idx == w->marked && idx != w->sel)
                    tk_canvas_fill_round_rect(c, pill, pill.h / 2,
                                              t->surface_alt);
                if (idx == w->sel)
                    tk_canvas_fill_round_rect(c, pill, pill.h / 2,
                                              t->selection);
            } else {
                if (idx == w->marked && idx != w->sel)
                    tk_canvas_fill_rect(c, row, t->surface_alt);
                if (idx == w->sel)
                    tk_canvas_fill_rect(c, row, t->selection);
                /* Selection is a dual cue (UI-REDESIGN 2.2): the
                 * selection surface PLUS a 3px accent bar at the
                 * left edge - the surface alone is only ~1.5:1 on
                 * white and does not read as a boundary. The marked
                 * row uses the same bar (M4) so both states share
                 * one visual language. */
                if (idx == w->sel || idx == w->marked)
                    tk_canvas_fill_rect(c, (tk_rect){ r.x, row.y, 3, rh },
                                        t->accent);
            }
            int tx = r.x + 2 * t->unit;
            if (w->lv_checkable || w->lv_multi) {
                /* M5: same box language as the CheckBox widget,
                 * at the row's left edge. M21: the M3 box is
                 * 21px like the CheckBox widget's; other modes
                 * keep the 18px form this row paint has used. */
                int lbs = t->style == 1 ? 21 : 18;
                tk_rect box = { tx, row.y + (rh - lbs) / 2, lbs, lbs };
                bool on = lv_flag(w, idx, TK_LV_FLAG_CHECKED);
                tk_canvas_fill_round_rect(c, box, 3,
                                          on ? t->accent : t->surface);
                tk_canvas_stroke_round_rect(c, box, 3, 1,
                                            t->border);
                if (on)
                    paint_check_glyph(c, box, 1.0f, t->on_accent);
                tx += lbs + t->unit;
            }
            if (idx == w->marked) {
                tk_canvas_draw_text(c, t, tx,
                                    row.y + (rh - lh) / 2,
                                    "▶", t->accent);
                tx += tk_text_width(t, "▶ ");
            }
            const uint32_t *icon = NULL;
            if (w->n_item_icons && idx < (int)w->n_item_icons)
                icon = w->item_icons[idx];
            if (icon) {
                blit_image(c, icon, w->item_icon_w, w->item_icon_h,
                           (tk_rect){ tx, row.y + (rh - w->item_icon_h) / 2,
                                      w->item_icon_w, w->item_icon_h });
                tx += w->item_icon_w + t->unit;
            }
            const char *sub = (idx < (int)w->n_subitems)
                              ? w->subitems[idx] : NULL;
            if (sub) {
                int ty = row.y + (rh - lh - lh_cap) / 2;
                tk_canvas_draw_text(c, t, tx, ty, w->items[idx], fg);
                tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION, tx,
                                       ty + lh, sub,
                                       w->enabled ? t->text_dim : fg);
            } else {
                tk_canvas_draw_text(c, t, tx,
                                    row.y + (rh - lh) / 2,
                                    w->items[idx], fg);
            }
        }
        tk_canvas_pop_clip(c);
        /* Scroll affordance (M4): when the content is taller than
         * the viewport, draw a scrollbar thumb on the right edge
         * (same geometry as the ScrollView thumb). Without it a
         * clipped list (e.g. the launcher's app list) reads as if
         * the visible rows were all there is. */
        int content_h = (int)w->n_items * rh;
        if (content_h > r.h && r.h > 0 && rows > 0) {
            int th = r.h * r.h / content_h;
            if (th < 16) th = 16;
            if (th > r.h) th = r.h;
            int max_top = (int)w->n_items - rows;
            int ty = r.y;
            if (max_top > 0)
                ty = r.y + w->top_index * (r.h - th) / max_top;
            tk_canvas_fill_round_rect(c,
                (tk_rect){r.x + r.w - 6, ty, 4, th}, 2, t->border);
        }
        /* M7: row drag - the dragged row's ghost follows the
         * finger; an accent insertion line marks the landing
         * slot (the same shadow language as the root drag) */
        if (w->lv_drag_row >= 0 &&
            w->lv_drag_row < (int)w->n_items) {
            tk_rect ghost = { r.x + 4, w->lv_drag_y - rh / 2,
                              r.w - 8, rh };
            tk_canvas_fill_round_rect(c, ghost, 8, t->surface);
            tk_canvas_stroke_round_rect(c, ghost, 8, 1, t->border);
            tk_canvas_draw_text(c, t, ghost.x + 2 * t->unit,
                                ghost.y + (rh - lh) / 2,
                                w->items[w->lv_drag_row], fg);
            int slot = (w->lv_drag_y - r.y) / rh + w->top_index;
            if (slot < 0) slot = 0;
            if (slot > (int)w->n_items) slot = (int)w->n_items;
            int ly = r.y + (slot - w->top_index) * rh;
            tk_canvas_fill_rect(c, (tk_rect){ r.x, ly - 1, r.w, 3 },
                                t->accent);
        }
        break;
    }
    case TK_W_OSK:
        tk_osk_paint(w, c, t);
        break;
    default:
        tk_w2_paint(w, c, t);
        break;
    }
}

/* ---------------------------------------------------------------- */
/* Events                                                            */
/* ---------------------------------------------------------------- */

static void slider_set_from_x(tk_widget *w, int x)
{
    if (w->rect.w <= 0) return;
    float v = (float)(x - w->rect.x) / (float)w->rect.w;
    if (w->sld_dual) {
        /* M7: drag the active handle (chosen at press time) */
        float vv = w->sld_range ? w->fmin + v * (w->fmax - w->fmin)
                                : v;
        sld_set_frac(w, w->sld_active, sld_to_frac(w, vv));
        return;
    }
    if (w->sld_range && w->fstep > 0) {
        /* discrete: snap the drag to the step grid (direct -
         * pointer-driven, M13) */
        slider_set_value_range_impl(w,
            w->fmin + v * (w->fmax - w->fmin), false);
        return;
    }
    slider_set_value_impl(w, v, false); /* pointer-driven (M13) */
}

/* M7: after a text change in a TextField - enforce the max
 * length (input past the cap is truncated at the end) and clear
 * a validator error (the M3 rule: the error shows until the
 * input is fixed). */
static void tf_after_edit(tk_widget *w)
{
    if (w->kind != TK_W_TEXTFIELD) return;
    if (w->tf_max_len > 0) {
        while ((int)tc_count(w->buffer) > w->tf_max_len) {
            tc_move_to(w->buffer, tc_length(w->buffer), false);
            if (!tc_delete(w->buffer, -1)) break;
        }
    }
    /* an edit clears a validation error (the M3 rule: the
     * error text lasts until the input is fixed); a manually
     * set error (no validator) is left alone */
    if (!w->tf_valid) {
        w->tf_valid = true;
        if (w->tf_validator) w->support_error = false;
    }
}

/* Shared editor key handling for TextField (multiline=false) and
 * TextView (multiline=true), both backed by the same textcore. */
static bool text_edit_key(tk_widget *w, tk_event *ev, bool multiline)
{
    tc_buffer *b = w->buffer;
    bool changed = false;
    if (ev->type == TK_EV_COMPOSITION) {
        if (ev->text && ev->seg_count > 0 && ev->seg_lens)
            tc_set_composition_segments(b, ev->text, ev->seg_lens,
                                        (size_t)ev->seg_count,
                                        ev->seg_active, ev->seg_highlight);
        else
            tc_set_composition(b, ev->text);
        return true;
    }
    if (ev->type == TK_EV_TEXT_INPUT) {
        if (ev->text && *ev->text) {
            tc_set_composition(b, NULL);
            tc_insert(b, ev->text);
            changed = true;
        }
        if (changed) {
            tf_after_edit(w);
            tk_invoke(w->cb1, w, w->cb1_ud);
        }
        return changed;
    }
    if (ev->type != TK_EV_KEY_DOWN) return false;
    bool shift = (ev->modifiers & TK_MOD_SHIFT) != 0;
    bool ctrl = (ev->modifiers & TK_MOD_CTRL) != 0;
    switch (ev->key) {
    /* M14 fix: the arrow-key contract of both reference
     * platforms - plain arrows move by cluster, Shift extends
     * the selection by cluster, Ctrl moves by word and
     * Ctrl+Shift extends by word. The pre-M14 mapping made
     * Shift alone jump words (and Ctrl alone did nothing
     * word-wise), which no reference editor does. */
    case TK_KEY_LEFT:  tc_move(b, ctrl ? TC_MOVE_WORD : TC_MOVE_CLUSTER, -1, shift); break;
    case TK_KEY_RIGHT: tc_move(b, ctrl ? TC_MOVE_WORD : TC_MOVE_CLUSTER, 1, shift); break;
    case TK_KEY_HOME:  tc_move(b, TC_MOVE_LINE_START, -1, shift); break;
    case TK_KEY_END:   tc_move(b, TC_MOVE_LINE_END, 1, shift); break;
    case TK_KEY_UP:
        if (!multiline) return false;
        tv_move_vertical(w, -1, shift);
        break;
    case TK_KEY_DOWN:
        if (!multiline) return false;
        tv_move_vertical(w, 1, shift);
        break;
    case TK_KEY_PAGE_UP:
        if (!multiline) return false;
        for (int i = 0; i < 10; i++) tv_move_vertical(w, -1, shift);
        break;
    case TK_KEY_PAGE_DOWN:
        if (!multiline) return false;
        for (int i = 0; i < 10; i++) tv_move_vertical(w, 1, shift);
        break;
    case TK_KEY_BACKSPACE: changed = tc_delete(b, -1); break;
    case TK_KEY_DELETE:    changed = tc_delete(b, 1); break;
    case TK_KEY_ENTER:
        if (multiline) { tc_insert(b, "\n"); changed = true; break; }
        return false; /* single-line: let the app handle submit */
    default:
        if (ctrl && ev->text) {
            char ch = ev->text[0];
            if (ch == 'a' || ch == 'A') { tc_select_all(b); return true; }
            if (ch == 'c' || ch == 'C') return tc_copy(b);
            if (ch == 'x' || ch == 'X') { changed = tc_cut(b); break; }
            if (ch == 'v' || ch == 'V') { changed = tc_paste(b); break; }
            if (ch == 'z' || ch == 'Z') { changed = tc_undo(b); break; }
            if (ch == 'y' || ch == 'Y') { changed = tc_redo(b); break; }
            return false;
        }
        if (ev->text && *ev->text && !ctrl) {
            tc_insert(b, ev->text);
            changed = true;
        } else {
            return false;
        }
        break;
    }
    if (changed) {
        tf_after_edit(w);
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
    return true;
}

/* M7: the SearchBar edits through the very same path as a
 * TextField (shared with tk_m7.c via tk_internal.h). */
bool tk_text_edit_key(tk_widget *w, tk_event *ev, bool multiline)
{
    return text_edit_key(w, ev, multiline);
}

void tk_widget_set_event_hook(tk_widget *w, tk_event_cb cb, void *ud)
{
    if (!w) return;
    w->ev_hook = cb; w->ev_hook_ud = ud;
}

bool tk_leaf_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    (void)root;
    /* M6: the generic hook sees the event first (long-press and
     * app gestures); consuming it stops the widget's own handler */
    if (w->ev_hook && w->ev_hook(w->ev_hook_ud, ev)) return true;
    if (w->kind >= TK_W_CARD) return tk_m7_event(w, root, ev); /* M7 */
    if (!w->enabled) return false;
    switch (w->kind) {
    case TK_W_BUTTON:
        if (ev->type == TK_EV_POINTER_DOWN && tk_rect_contains(w->rect, ev->x, ev->y)) {
            w->pressed = true;
            tk_ripple_press(w, root, ev->x, ev->y); /* M17 */
            return true;
        }
        if (ev->type == TK_EV_POINTER_UP) {
            bool was = w->pressed; w->pressed = false;
            tk_ripple_release(w, root); /* M17 */
            if (was && root && root->suppress_click)
                return true; /* the gesture became a drag-scroll */
            if (was && tk_rect_contains(w->rect, ev->x, ev->y)) {
                /* M7: a toggle button flips BEFORE on_click */
                if (w->btn_toggle) {
                    w->checked = !w->checked;
                    tk_widget_invalidate(w);
                }
                tk_invoke(w->cb1, w, w->cb1_ud);
                return true;
            }
            return was;
        }
        if (ev->type == TK_EV_KEY_DOWN &&
            (ev->key == TK_KEY_ENTER || ev->key == TK_KEY_SPACE)) {
            if (w->btn_toggle) {
                w->checked = !w->checked;
                tk_widget_invalidate(w);
            }
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        return false;
    case TK_W_CHECKBOX:
        if (ev->type == TK_EV_POINTER_UP && tk_rect_contains(w->rect, ev->x, ev->y)) {
            /* M13: through the setter, so the glyph
             * transition runs (the direct field flips here
             * previously bypassed it) */
            tk_checkbox_set_checked(w, !w->checked);
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN && ev->key == TK_KEY_SPACE) {
            tk_checkbox_set_checked(w, !w->checked);
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        return false;
    case TK_W_RADIO:
        if ((ev->type == TK_EV_POINTER_UP && tk_rect_contains(w->rect, ev->x, ev->y)) ||
            (ev->type == TK_EV_KEY_DOWN && ev->key == TK_KEY_SPACE)) {
            if (!w->selected) tk_radio_select(w);
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN &&
            (ev->key == TK_KEY_LEFT || ev->key == TK_KEY_RIGHT ||
             ev->key == TK_KEY_UP || ev->key == TK_KEY_DOWN)) {
            /* M14: arrow keys move within the group and
             * selection follows focus (Microsoft Learn,
             * radio-button guidelines: "Selection follows
             * focus"; Ctrl+arrow moves focus only, Space
             * then selects; the group does not wrap). */
            int dir = (ev->key == TK_KEY_RIGHT ||
                       ev->key == TK_KEY_DOWN) ? 1 : -1;
            tk_widget *tgt = radio_group_step(w, dir);
            if (tgt) {
                if (root) tk_focus(root, tgt);
                if (!(ev->modifiers & TK_MOD_CTRL))
                    tk_radio_select(tgt);
            }
            return true;
        }
        return false;
    case TK_W_SLIDER:
        if (ev->type == TK_EV_POINTER_DOWN && tk_rect_contains(w->rect, ev->x, ev->y)) {
            w->pressed = true;
            /* M16: the grab state belongs to both forms - it
             * was set only inside the dual branch, so the
             * single form never tracked a drag at all (its
             * value indicator could never show) */
            w->sld_grabbed = true;
            if (w->sld_dual) {
                /* M7: grab the nearer handle */
                float fx = w->rect.w > 0
                               ? (float)(ev->x - w->rect.x) /
                                     (float)w->rect.w
                               : 0;
                float dl = fx - w->fvalue2;
                float dh = fx - w->fvalue;
                if (dl < 0) dl = -dl;
                if (dh < 0) dh = -dh;
                w->sld_active = dl <= dh ? 0 : 1;
            }
            slider_set_from_x(w, ev->x);
            return true;
        }
        if (ev->type == TK_EV_POINTER_MOVE && w->pressed) {
            slider_set_from_x(w, ev->x);
            return true;
        }
        if (ev->type == TK_EV_POINTER_UP) {
            bool was = w->pressed; w->pressed = false;
            w->sld_grabbed = false;
            return was;
        }
        if (ev->type == TK_EV_FOCUS_OUT) {
            w->sld_grabbed = false;
            return false;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            /* M14: the full keyboard contract. M3 sliders
             * accessibility (m3.material.io): arrows step by
             * one value/stop, Home/End set the first/last
             * value. The APG slider pattern adds Up/Down as
             * step keys and PageUp/PageDown as a large step
             * (the WinUI LargeChange convention: 10x the
             * arrow step). Keyboard steps are direct (M13). */
            int dir = 0, mag = 0; /* mag: 0 step, 1 page, 2 end */
            switch (ev->key) {
            case TK_KEY_LEFT:  case TK_KEY_DOWN: dir = -1; break;
            case TK_KEY_RIGHT: case TK_KEY_UP:   dir = 1;  break;
            case TK_KEY_PAGE_DOWN: dir = -1; mag = 1; break;
            case TK_KEY_PAGE_UP:   dir = 1;  mag = 1; break;
            case TK_KEY_HOME: dir = -1; mag = 2; break;
            case TK_KEY_END:  dir = 1;  mag = 2; break;
            default: return false;
            }
            if (w->sld_dual) {
                /* step the active handle; the value indicator
                 * shows while stepping (M7). Steps are in the
                 * units of tk_slider_value_lo/hi; the handle
                 * clamps against the other handle, and its
                 * Home/End are its own ends of travel. */
                w->sld_grabbed = true;
                float lo = tk_slider_value_lo(w);
                float hi = tk_slider_value_hi(w);
                float tgt;
                if (mag == 2) {
                    if (w->sld_active == 0)
                        tgt = dir < 0
                                  ? (w->sld_range ? w->fmin : 0.0f)
                                  : hi;
                    else
                        tgt = dir < 0
                                  ? lo
                                  : (w->sld_range ? w->fmax : 1.0f);
                } else {
                    float step;
                    if (w->sld_range && w->fstep > 0)
                        step = w->fstep;
                    else if (w->sld_range)
                        step = 0.05f * (w->fmax - w->fmin);
                    else
                        step = 0.05f;
                    float cur = w->sld_active == 0 ? lo : hi;
                    tgt = cur + dir * step * (mag ? 10.0f : 1.0f);
                }
                sld_set_frac(w, w->sld_active, sld_to_frac(w, tgt));
                return true;
            }
            /* M16: single-form steps mark the grab too, as
             * the dual form's do (the indicator shows while
             * stepping; FOCUS_OUT / POINTER_UP clear it) */
            w->sld_grabbed = true;
            if (mag == 2) {
                if (w->sld_range)
                    slider_set_value_range_impl(
                        w, dir < 0 ? w->fmin : w->fmax, false);
                else
                    slider_set_value_impl(w, dir < 0 ? 0.0f : 1.0f,
                                          false);
            } else if (w->sld_range && w->fstep > 0) {
                slider_set_value_range_impl(
                    w, tk_slider_value_range(w) +
                           dir * w->fstep * (mag ? 10.0f : 1.0f),
                    false);
            } else {
                slider_set_value_impl(
                    w, w->fvalue + dir * 0.05f * (mag ? 10.0f : 1.0f),
                    false);
            }
            return true;
        }
        return false;
    case TK_W_TEXTFIELD:
        if (ev->type == TK_EV_POINTER_DOWN && tk_rect_contains(w->rect, ev->x, ev->y)) {
            /* place the cursor at the caret nearest the click,
             * measured with real font metrics when a font is set.
             * Password mode measures in the masked layout (what
             * the user actually sees) and maps back to a byte
             * offset in the real text. M7: the prefix shifts the
             * text right, so it comes off the click position. */
            const tk_theme *t = metrics();
            char buf[4096];
            tc_text(w->buffer, buf, sizeof(buf));
            int rel_x = ev->x - w->rect.x - t->unit;
            if (w->tf_prefix)
                rel_x -= tk_text_width(t, w->tf_prefix) +
                         t->unit / 2;
            size_t off = w->password
                ? masked_offset_at_x(t, buf, rel_x)
                : tk_offset_at_x(t, buf, rel_x);
            tc_move_to(w->buffer, off, (ev->modifiers & TK_MOD_SHIFT) != 0);
            return true;
        }
        if (ev->type == TK_EV_FOCUS_OUT) {
            /* M7: the validator runs on focus loss, and the
             * completion dropdown does not outlive the field */
            if (w->open_sub && root) tk_menu_close(root, w->open_sub);
            if (w->tf_validator) tk_textfield_validate(w);
            return false;
        }
        if (ev->type == TK_EV_KEY_DOWN && ev->key == TK_KEY_ESCAPE &&
            w->open_sub) {
            if (root) tk_menu_close(root, w->open_sub);
            return true;
        }
        {
            /* M7: refresh the completion dropdown only when
             * the edit actually changed the text (caret moves
             * must not reopen a dismissed dropdown) */
            char before[512], after[512];
            tc_text(w->buffer, before, sizeof(before));
            bool handled = text_edit_key(w, ev, false);
            tc_text(w->buffer, after, sizeof(after));
            if (handled && root && strcmp(before, after) != 0)
                tk_m7_suggest_refresh(root, w);
            return handled;
        }
    case TK_W_SCROLLVIEW:
        if (ev->type == TK_EV_WHEEL) {
            const tk_theme *t = metrics();
            tk_scrollview_scroll_to(w, w->scroll_y + ev->wheel_dy * t->row_h);
            return true;
        }
        /* One-finger drag scroll: DOWN is only recorded (children
         * still get their events through bubbling). Once the pointer
         * moves past the threshold, the gesture becomes a scroll:
         * the offset follows the finger, root->suppress_click stops
         * the child under the finger from firing on release, and the
         * release velocity feeds tk_scrollview_tick() inertia. */
        if (ev->type == TK_EV_POINTER_DOWN && tk_rect_contains(w->rect, ev->x, ev->y)) {
            w->ptr_down = true;
            w->dragging = false;
            w->drag_start_y = ev->y;
            w->last_y = ev->y;
            w->drag_start_scroll = w->scroll_y;
            w->vel_y = 0;
            return false;
        }
        if (ev->type == TK_EV_POINTER_MOVE && w->ptr_down) {
            int dy = ev->y - w->drag_start_y;
            if (!w->dragging && (dy > 6 || dy < -6)) {
                w->dragging = true;
                if (root) root->suppress_click = true;
            }
            if (w->dragging) {
                w->vel_y = -(ev->y - w->last_y);
                w->last_y = ev->y;
                tk_scrollview_scroll_to(w, w->drag_start_scroll - dy);
                return true;
            }
            w->last_y = ev->y;
            return false;
        }
        if (ev->type == TK_EV_POINTER_UP) {
            bool was_drag = w->dragging;
            w->ptr_down = false;
            w->dragging = false;
            if (!was_drag) w->vel_y = 0;
            return was_drag;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            const tk_theme *t = metrics();
            if (ev->key == TK_KEY_DOWN) { tk_scrollview_scroll_to(w, w->scroll_y + t->row_h); return true; }
            if (ev->key == TK_KEY_UP) { tk_scrollview_scroll_to(w, w->scroll_y - t->row_h); return true; }
            if (ev->key == TK_KEY_PAGE_DOWN) { tk_scrollview_scroll_to(w, w->scroll_y + w->rect.h); return true; }
            if (ev->key == TK_KEY_PAGE_UP) { tk_scrollview_scroll_to(w, w->scroll_y - w->rect.h); return true; }
        }
        return false;
    case TK_W_LISTVIEW: {
        const tk_theme *t = metrics();
        if (ev->type == TK_EV_POINTER_DOWN && tk_rect_contains(w->rect, ev->x, ev->y)) {
            int row = (ev->y - w->rect.y) / lv_row_h(w, t) + w->top_index;
            if (row >= 0 && row < (int)w->n_items) {
                /* M5: headers are labels, not targets */
                if (lv_flag(w, row, TK_LV_FLAG_HEADER)) return true;
                /* M7: with drag-reorder on, the press arms a
                 * row drag (the tap still selects as usual) */
                if (w->lv_drag_reorder) {
                    w->ptr_down = true;
                    w->i1 = row;
                    w->lv_drag_start_y = ev->y;
                    w->lv_drag_y = ev->y;
                }
                /* M6: multi-select - any tap on the row toggles
                 * its check (on_check fires via lv_toggle_check)
                 * and the selection follows the tap */
                if (w->lv_multi) {
                    lv_toggle_check(w, row);
                    tk_listview_select(w, row);
                    return true;
                }
                /* M5: a tap on the checkbox cell toggles the check
                 * and leaves the selection alone */
                if (w->lv_checkable &&
                    ev->x < w->rect.x + 2 * t->unit + 18 + t->unit) {
                    lv_toggle_check(w, row);
                    return true;
                }
                tk_listview_select(w, row);
                return true;
            }
            return false;
        }
        if (ev->type == TK_EV_POINTER_MOVE && w->lv_drag_reorder &&
            w->ptr_down) {
            if (w->lv_drag_row < 0 &&
                (ev->y - w->lv_drag_start_y > 10 ||
                 w->lv_drag_start_y - ev->y > 10)) {
                w->lv_drag_row = w->i1;
                if (root) root->suppress_click = true;
            }
            if (w->lv_drag_row >= 0) {
                w->lv_drag_y = ev->y;
                tk_widget_invalidate(w);
                return true;
            }
            return false;
        }
        if (ev->type == TK_EV_POINTER_UP && w->lv_drag_reorder) {
            bool was_down = w->ptr_down;
            w->ptr_down = false;
            if (w->lv_drag_row >= 0) {
                int rh = lv_row_h(w, t);
                int slot = (ev->y - w->rect.y) / rh + w->top_index;
                if (slot < 0) slot = 0;
                if (slot >= (int)w->n_items)
                    slot = (int)w->n_items - 1;
                int from = w->lv_drag_row;
                w->lv_drag_row = -1;
                if (slot != from)
                    tk_listview_move_row(w, from, slot);
                tk_widget_invalidate(w);
                return true;
            }
            (void)was_down;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            if (ev->key == TK_KEY_DOWN) {
                /* M5: navigation steps over section headers */
                tk_listview_select(w, lv_step_selectable(w, w->sel, 1));
                return true;
            }
            if (ev->key == TK_KEY_UP) {
                tk_listview_select(w, lv_step_selectable(w, w->sel, -1));
                return true;
            }
            if (ev->key == TK_KEY_ENTER) {
                tk_invoke(w->cb2, w, w->cb2_ud); return true;
            }
        }
        if (ev->type == TK_EV_WHEEL) {
            int nt = w->top_index + ev->wheel_dy;
            if (nt < 0) nt = 0;
            if (nt >= (int)w->n_items) nt = (int)w->n_items - 1;
            if (nt < 0) nt = 0;
            w->top_index = nt;
            return true;
        }
        return false;
    }
    case TK_W_OSK:
        return tk_osk_event(w, root, ev);
    case TK_W_SINK: {
        /* IME output events only; everything else falls through
         * so the sink stays transparent to pointer/focus traffic. */
        if (ev->type != TK_EV_TEXT_INPUT && ev->type != TK_EV_COMPOSITION &&
            ev->type != TK_EV_KEY_DOWN && ev->type != TK_EV_KEY_UP)
            return false;
        tk_sink_state *sk = w->ext;
        if (!sk || !sk->cb) return false;
        return sk->cb(sk->ud, ev);
    }
    default:
        return tk_w2_event(w, root, ev);
    }
}

/* ================================================================ */
/* M2 widgets: Switch, ProgressBar, Separator, ImageView,           */
/* SpinButton, Tabs, Dialog, Menu, MenuBar, ComboBox, TextView,     */
/* TreeView. Dispatched from tk_leaf_* defaults (tk_w2_*).          */
/* ================================================================ */

size_t tk_offset_at_x(const tk_theme *t, const char *utf8, int x_px)
{
    if (!utf8) return 0;
    if (x_px <= 0) return 0;
    size_t len = strlen(utf8), off = 0;
    int acc = 0;
    size_t best_off = 0;
    int best_diff = x_px; /* caret at 0 */
    while (off < len) {
        size_t cl;
        uint32_t cp = tc_utf8_decode(utf8 + off, len - off, &cl);
        if (cl == 0) cl = 1;
        acc += tk_text_cp_width(t, cp);
        int d = x_px > acc ? x_px - acc : acc - x_px;
        if (d < best_diff) { best_diff = d; best_off = off + cl; }
        off += cl;
    }
    return best_off;
}

/* ---------------- Switch ---------------- */

tk_widget *tk_switch_new(const char *label, bool on)
{
    tk_widget *w = leaf(TK_W_SWITCH, label, true);
    if (w) { w->checked = on; w->knob_pos = on ? 1.0f : 0.0f; }
    return w;
}
bool tk_switch_on(const tk_widget *w) { return w->checked; }

/* M5: knob slide. The logical state flips immediately; the
 * displayed knob position eases to it when the widget is
 * attached to a root with animations enabled, and snaps
 * otherwise (detached trees, reduced motion).
 * M12: duration / easing are the mode's selection-control
 * pair - M3: 200 ms with Standard easing ("Selection controls
 * have a short duration of 200ms with Standard easing", M3
 * motion tokens); WinUI: ControlFastAnimationDuration 167 ms
 * with the Direct Entrance curve cubic-bezier(0,0,0,1)
 * (Microsoft Learn, Timing and easing / Motion in Windows). */
static void switch_knob_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->knob_pos = v / 1000.0f;
    tk_widget_invalidate(w);
}
static void switch_knob_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    w->knob_pos = w->checked ? 1.0f : 0.0f;
    tk_widget_invalidate(w);
}
static void switch_start_knob_anim(tk_widget *w)
{
    tk_root *r = tk_widget_root(w);
    if (!r || !tk_root_animations_enabled(r)) {
        w->knob_pos = w->checked ? 1.0f : 0.0f;
        return;
    }
    if (w->anim_handle) { tk_anim_cancel(w->anim_handle); w->anim_handle = NULL; }
    int from = (int)(w->knob_pos * 1000.0f + 0.5f);
    int to = w->checked ? 1000 : 0;
    const tk_theme *t = tk_theme_default();
    uint32_t dur = t && t->style == 2 ? 167 : 200;
    tk_ease_fn ease = t && t->style == 2
                          ? tk_ease_standard_decelerate
                          : tk_ease_standard;
    w->anim_handle = tk_anim_start(r, from, to, dur, ease,
                                   switch_knob_cb, switch_knob_done, w);
}

void tk_switch_set_on(tk_widget *w, bool v)
{
    if (w->checked != v) {
        w->checked = v;
        switch_start_knob_anim(w);
        tk_widget_invalidate(w);
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_switch_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}

/* ---------------- ProgressBar ---------------- */

tk_widget *tk_progress_new(float value)
{
    tk_widget *w = leaf(TK_W_PROGRESS, NULL, false);
    if (w) {
        w->fvalue = value < 0 ? 0 : (value > 1 ? 1 : value);
        w->prog_disp = w->fvalue;
    }
    return w;
}
float tk_progress_value(const tk_widget *w) { return w->fvalue; }

/* M13: the M3 determinate transition - 250 ms with
 * cubic-bezier(0.4, 0, 0.6, 1) (material-web's
 * _linear-progress.scss: "$_determinate-duration: 250ms;
 * $_determinate-easing: cubic-bezier(0.4, 0, 0.6, 1)",
 * "transition settings match MDC"). The logical value stays
 * immediate; the DISPLAYED fraction (prog_disp) transitions.
 * Other modes keep the instant set (the WinUI ProgressBar
 * has no value transition). Gating mirrors the Switch: no
 * live animation clock, no transition. */
static float ease_prog_det(float t)
{
    return tk_bezier_y(0.4f, 0.0f, 0.6f, 1.0f, t);
}
static void prog_disp_cb(void *ud, int permille)
{
    tk_widget *w = ud;
    w->prog_disp = w->prog_from +
        (w->fvalue - w->prog_from) * (permille / 1000.0f);
    tk_widget_invalidate(w);
}
static void prog_disp_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    w->prog_disp = w->fvalue;
    tk_widget_invalidate(w);
}
void tk_progress_set_animate_values(tk_widget *w, bool on)
{
    if (w) w->prog_anim_values = on;
}
void tk_progress_set_value(tk_widget *w, float v)
{
    if (!w) return;
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    if (v != w->fvalue) {
        w->fvalue = v;
        tk_root *r = tk_widget_root(w);
        const tk_theme *t = tk_theme_default();
        if (w->anim_handle) {
            tk_anim_cancel(w->anim_handle);
            w->anim_handle = NULL;
        }
        if (t && t->style == 1 && r && r->anim_now != 0 &&
            tk_root_animations_enabled(r) && !w->prog_indet &&
            w->prog_anim_values) {
            w->prog_from = w->prog_disp;
            w->anim_handle = tk_anim_start(r, 0, 1000, 250,
                ease_prog_det, prog_disp_cb, prog_disp_done, w);
            if (!w->anim_handle) w->prog_disp = v;
        } else {
            w->prog_disp = v;
        }
        tk_widget_invalidate(w); /* M5 */
    }
}

/* --- M6: indeterminate sweep, shared by the linear bar and the
 * circular spinner. The phase (permille) advances over a 1200ms
 * period through a self-restarting animation; frames exist only
 * while the root's animation engine runs them, and a detached
 * widget simply paints its static phase. --- */
static void prog_phase_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->prog_phase = v % 1000;
    tk_widget_invalidate(w);
}
static void prog_sweep_start(tk_widget *w); /* below */
static void prog_phase_done(void *ud)
{
    tk_widget *w = ud;
    w->prog_anim = NULL;
    if (w->prog_indet || w->kind == TK_W_SPINNER)
        prog_sweep_start(w); /* loop while indeterminate */
}
static void prog_sweep_start(tk_widget *w)
{
    if (w->prog_anim) return;
    tk_root *r = tk_widget_root(w);
    if (!r || !tk_root_animations_enabled(r)) return;
    /* M13: the M3 indeterminate cycle is 2000 ms
     * ($_indeterminate-duration: 2s in the MDC-matching
     * material-web source); the classic / WinUI sweep keeps
     * the toolkit's 1200 ms period, and so does the spinner
     * in every mode (it is not the linear indicator). */
    const tk_theme *t = tk_theme_default();
    uint32_t dur = (w->kind == TK_W_PROGRESS && t &&
                    t->style == 1) ? 2000 : 1200;
    w->prog_anim = tk_anim_start(r, w->prog_phase, w->prog_phase + 1000,
                                 dur, tk_ease_linear,
                                 prog_phase_cb, prog_phase_done, w);
}

/* M13: the M3 indeterminate keyframes, verbatim from
 * material-web's _linear-progress.scss ("the numbers here
 * come directly from the mdc implementation"). Each entry is
 * a keyframe (position, value) plus the cubic-bezier of the
 * segment FROM that keyframe (the CSS rule: a keyframe
 * block's animation-timing-function governs the segment that
 * starts there; a block without one inherits the animation's
 * linear timing, written here as (0,0,1,1)). The bars are
 * 100% of the track wide, offset by a base left of -145.167%
 * (primary) / -54.8889% (secondary); the visible segment is
 * [base + translate(p), + scale(p) * 100%]. */
typedef struct { float p, v, e[4]; } tk_kf;
static float kf_eval(const tk_kf *kfs, int n, float p)
{
    if (p <= kfs[0].p) return kfs[0].v;
    for (int i = 0; i + 1 < n; i++) {
        if (p <= kfs[i + 1].p) {
            float x = (p - kfs[i].p) / (kfs[i + 1].p - kfs[i].p);
            float y = tk_bezier_y(kfs[i].e[0], kfs[i].e[1],
                                  kfs[i].e[2], kfs[i].e[3], x);
            return kfs[i].v + (kfs[i + 1].v - kfs[i].v) * y;
        }
    }
    return kfs[n - 1].v;
}
static const tk_kf KF_PRI_TR[] = {
    { 0.0f,    0.0f,     { 0.0f, 0.0f, 1.0f, 1.0f } },
    { 0.20f,   0.0f,     { 0.5f, 0.0f, 0.701732f, 0.495819f } },
    { 0.5915f, 83.6714f, { 0.302435f, 0.381352f, 0.55f,
                           0.956352f } },
    { 1.0f,    200.611f, { 0.0f, 0.0f, 1.0f, 1.0f } },
};
static const tk_kf KF_PRI_SC[] = {
    { 0.0f,    0.08f,     { 0.0f, 0.0f, 1.0f, 1.0f } },
    { 0.3665f, 0.08f,     { 0.334731f, 0.12482f, 0.785844f,
                            1.0f } },
    { 0.6915f, 0.661479f, { 0.06f, 0.11f, 0.6f, 1.0f } },
    { 1.0f,    0.08f,     { 0.0f, 0.0f, 1.0f, 1.0f } },
};
static const tk_kf KF_SEC_TR[] = {
    { 0.0f,    0.0f,     { 0.15f, 0.0f, 0.515058f, 0.409685f } },
    { 0.25f,   37.6519f, { 0.31033f, 0.284058f, 0.8f,
                           0.733712f } },
    { 0.4835f, 84.3862f, { 0.4f, 0.627035f, 0.6f, 0.902026f } },
    { 1.0f,    160.278f, { 0.0f, 0.0f, 1.0f, 1.0f } },
};
static const tk_kf KF_SEC_SC[] = {
    { 0.0f,    0.08f,     { 0.205028f, 0.057051f, 0.57661f,
                            0.453971f } },
    { 0.1915f, 0.457104f, { 0.152313f, 0.196432f, 0.648374f,
                            1.00432f } },
    { 0.4415f, 0.72796f,  { 0.257759f, -0.003163f, 0.211762f,
                            1.38179f } },
    { 1.0f,    0.08f,     { 0.0f, 0.0f, 1.0f, 1.0f } },
};
static void prog_sweep_stop(tk_widget *w)
{
    if (!w->prog_anim) return;
    tk_root *r = tk_widget_root(w);
    if (r) tk_anim_cancel(w->prog_anim);
    w->prog_anim = NULL;
}

void tk_progress_set_indeterminate(tk_widget *w, bool on)
{
    if (!w || w->kind != TK_W_PROGRESS) return;
    if (w->prog_indet == on) return;
    w->prog_indet = on;
    if (on) prog_sweep_start(w);
    else prog_sweep_stop(w);
    tk_widget_invalidate(w);
}
bool tk_progress_is_indeterminate(const tk_widget *w)
{
    return w && w->prog_indet;
}

tk_widget *tk_spinner_new(int size)
{
    tk_widget *w = leaf(TK_W_SPINNER, NULL, false);
    if (w) {
        w->size_px = size > 0 ? size : 40;
        w->prog_indet = true;
    }
    return w;
}

/* ---------------- Separator ---------------- */

tk_widget *tk_separator_new(bool vertical)
{
    tk_widget *w = leaf(TK_W_SEPARATOR, NULL, false);
    if (w) w->vertical = vertical;
    return w;
}

/* ---------------- ImageView ---------------- */

tk_widget *tk_imageview_new(void) { return leaf(TK_W_IMAGEVIEW, NULL, false); }

bool tk_imageview_set_pixels(tk_widget *w, const uint32_t *px, int iw, int ih)
{
    if (!w || !px || iw <= 0 || ih <= 0) return false;
    uint32_t *copy = malloc((size_t)iw * (size_t)ih * sizeof(uint32_t));
    if (!copy) return false;
    memcpy(copy, px, (size_t)iw * (size_t)ih * sizeof(uint32_t));
    free(w->image_px);
    w->image_px = copy; w->image_w = iw; w->image_h = ih;
    tk_widget_invalidate(w); /* M5 */
    return true;
}

bool tk_imageview_load(tk_widget *w, const char *path)
{
    uint32_t *px = NULL; int iw = 0, ih = 0;
    if (!tk_image_load_rgba(path, &px, &iw, &ih)) return false;
    free(w->image_px);
    w->image_px = px; w->image_w = iw; w->image_h = ih;
    tk_widget_invalidate(w); /* M5 */
    return true;
}

/* M5: load decoded-and-downscaled (thumbnail memory footprint:
 * a 12 MP photo shown in a 96 px cell no longer costs 48 MB of
 * decoded pixels - it costs the cell's). */
bool tk_imageview_load_thumbnail(tk_widget *w, const char *path,
                                 int max_w, int max_h)
{
    uint32_t *px = NULL; int iw = 0, ih = 0;
    if (!tk_image_load_thumbnail(path, max_w, max_h, &px, &iw, &ih))
        return false;
    free(w->image_px);
    w->image_px = px; w->image_w = iw; w->image_h = ih;
    tk_widget_invalidate(w);
    return true;
}
int tk_imageview_image_w(const tk_widget *w) { return w->image_w; }
int tk_imageview_image_h(const tk_widget *w) { return w->image_h; }

/* ---------------- SpinButton ---------------- */

tk_widget *tk_spinbutton_new(float value, float min, float max, float step)
{
    tk_widget *w = leaf(TK_W_SPINBUTTON, NULL, true);
    if (!w) return NULL;
    w->fmin = min; w->fmax = max; w->fstep = step > 0 ? step : 1;
    w->fvalue = value < min ? min : (value > max ? max : value);
    return w;
}
float tk_spinbutton_value(const tk_widget *w) { return w->fvalue; }

/* ---- shared minimal text-entry buffer ops (M3) ------------------
 * ComboBox (editable) and SpinButton edit a plain char* in w->text
 * with the caret at w->edit_pos (a byte offset). These helpers are
 * deliberately tiny: no undo, no selection - the full editor is
 * textcore, used by TextField/TextView. */
static int textbuf_utf8_prev(const char *s, int pos)
{
    if (pos > 0) {
        pos--;
        while (pos > 0 && (s[pos] & 0xC0) == 0x80) pos--;
    }
    return pos;
}
static int textbuf_utf8_next(const char *s, int pos)
{
    int n = (int)strlen(s);
    if (pos < n) {
        pos++;
        while (pos < n && (s[pos] & 0xC0) == 0x80) pos++;
    }
    return pos;
}
static void textbuf_insert(tk_widget *w, const char *s)
{
    if (!w->text) { w->text = tk_strdup(""); w->edit_pos = 0; }
    if (!w->text) return;
    size_t cur = strlen(w->text), add = strlen(s);
    if (w->edit_pos < 0) w->edit_pos = 0;
    if (w->edit_pos > (int)cur) w->edit_pos = (int)cur;
    char *nt = realloc(w->text, cur + add + 1);
    if (!nt) return;
    w->text = nt;
    memmove(w->text + w->edit_pos + add, w->text + w->edit_pos,
            cur - (size_t)w->edit_pos + 1);
    memcpy(w->text + w->edit_pos, s, add);
    w->edit_pos += (int)add;
}
static void textbuf_erase(tk_widget *w, int dir) /* dir<0: backspace */
{
    if (!w->text) return;
    int len = (int)strlen(w->text);
    if (dir < 0) {
        if (w->edit_pos <= 0) return;
        int from = textbuf_utf8_prev(w->text, w->edit_pos);
        memmove(w->text + from, w->text + w->edit_pos,
                (size_t)(len - w->edit_pos) + 1);
        w->edit_pos = from;
    } else {
        if (w->edit_pos >= len) return;
        int to = textbuf_utf8_next(w->text, w->edit_pos);
        memmove(w->text + w->edit_pos, w->text + to,
                (size_t)(len - to) + 1);
    }
}
/* Handle caret/editing keys on w->text; true when the key was one. */
static bool textbuf_key(tk_widget *w, tk_event *ev)
{
    if (!w->text) return false;
    switch (ev->key) {
    case TK_KEY_BACKSPACE: textbuf_erase(w, -1); return true;
    case TK_KEY_DELETE:    textbuf_erase(w, 1);  return true;
    case TK_KEY_LEFT:
        w->edit_pos = textbuf_utf8_prev(w->text, w->edit_pos);
        return true;
    case TK_KEY_RIGHT:
        w->edit_pos = textbuf_utf8_next(w->text, w->edit_pos);
        return true;
    case TK_KEY_HOME: w->edit_pos = 0; return true;
    case TK_KEY_END:  w->edit_pos = (int)strlen(w->text); return true;
    default: return false;
    }
}

/* ---- SpinButton direct text entry (M3) ------------------------- */
static void spin_edit_begin(tk_widget *w)
{
    if (w->b2) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", (double)w->fvalue);
    free(w->text);
    w->text = tk_strdup(buf);
    w->edit_pos = w->text ? (int)strlen(w->text) : 0;
    w->b2 = true;
}
static void spin_edit_end(tk_widget *w, bool commit)
{
    if (!w->b2) return;
    w->b2 = false;
    if (commit && w->text) {
        char *end = NULL;
        float v = strtof(w->text, &end);
        if (end != w->text)
            tk_spinbutton_set_value(w, v); /* clamps + notifies */
    }
    free(w->text);
    w->text = NULL;
}

void tk_spinbutton_set_value(tk_widget *w, float v)
{
    if (w->b2) spin_edit_end(w, false); /* programmatic set wins */
    v = v < w->fmin ? w->fmin : (v > w->fmax ? w->fmax : v);
    if (v != w->fvalue) {
        w->fvalue = v;
        tk_widget_invalidate(w); /* M5 */
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_spinbutton_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}
static void spin_step(tk_widget *w, int dir)
{
    if (w->b2) spin_edit_end(w, true); /* step from the typed value */
    tk_spinbutton_set_value(w, w->fvalue + dir * w->fstep);
}

/* ---------------- Tabs ---------------- */

tk_widget *tk_tabs_new(void) { return tk_widget_new(TK_W_TABS); }

void tk_tabs_add_page(tk_widget *tabs, const char *title, tk_widget *page)
{
    if (!tabs || !page) return;
    char **nt = realloc(tabs->tab_titles,
                        (tabs->n_tab_titles + 1) * sizeof(char *));
    if (!nt) return;
    tabs->tab_titles = nt;
    tabs->tab_titles[tabs->n_tab_titles++] = tk_strdup(title);
    tk_add_child(tabs, page);
}
int tk_tabs_active(const tk_widget *w) { return w->active_tab; }
void tk_tabs_set_active(tk_widget *w, int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= (int)w->n_children) idx = (int)w->n_children - 1;
    if (idx < 0) return;
    if (idx != w->active_tab) {
        w->active_tab = idx;
        if (w->rect.w > 0) tk_arrange(w, w->rect); /* re-arrange pages */
        tk_widget_invalidate(w); /* M5 */
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_tabs_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}

/* ---------------- TabBar (M6) ----------------
 * Equal-width selector bar (M3 primary tabs): items in items[],
 * per-item borrowed icons in item_icons[] (shared storage with
 * the ListView), per-item badge counts in tab_badges[]. The
 * selection lives in sel; the indicator slide animation reuses
 * the generic anim engine through anim_handle. */
tk_widget *tk_tabbar_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_TABBAR);
    if (w) { w->can_focus = true; w->sel = -1; w->tab_ind_x = -1; }
    return w;
}

/* M13: the active indicator's left edge (widget-relative) for
 * segment idx, in the paint's geometry. */
static int tabbar_ind_x(const tk_widget *w, int idx)
{
    if (!w || w->n_items == 0 || w->rect.w <= 0) return -1;
    int seg_w = w->rect.w / (int)w->n_items;
    int sx = seg_w * idx;
    int sw = (idx + 1 == (int)w->n_items) ? w->rect.w - sx : seg_w;
    int iw2 = sw - 16;
    if (iw2 < 28) iw2 = 28;
    if (iw2 > sw) iw2 = sw;
    return sx + (sw - iw2) / 2;
}
static void tab_ind_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->tab_ind_x = v;
    tk_widget_invalidate(w);
}
static void tab_ind_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    w->tab_ind_x = -1; /* derive from the selection at rest */
    tk_widget_invalidate(w);
}
/* M13: slide the indicator to the (already updated) selection.
 * The selection itself stays immediate; without a live
 * animation clock the indicator snaps (tab_ind_x = -1). */
static void tabbar_ind_slide(tk_widget *w, int from_x)
{
    tk_root *r = tk_widget_root(w);
    int to_x = tabbar_ind_x(w, w->sel);
    if (w->anim_handle) {
        tk_anim_cancel(w->anim_handle);
        w->anim_handle = NULL;
    }
    if (!r || r->anim_now == 0 || !tk_root_animations_enabled(r) ||
        from_x < 0 || to_x < 0 || from_x == to_x) {
        w->tab_ind_x = -1;
        return;
    }
    uint32_t dur; tk_ease_fn ease;
    tk_anim_mode_pair(false, &dur, &ease);
    w->tab_ind_x = from_x;
    w->anim_handle = tk_anim_start(r, from_x, to_x, dur, ease,
                                   tab_ind_cb, tab_ind_done, w);
    if (!w->anim_handle) w->tab_ind_x = -1;
}
void tk_tabbar_add(tk_widget *w, const char *label)
{
    if (!w || w->kind != TK_W_TABBAR) return;
    char **ni = realloc(w->items, (w->n_items + 1) * sizeof(char *));
    if (!ni) return;
    w->items = ni;
    w->items[w->n_items] = tk_strdup(label);
    int *nb = realloc(w->tab_badges, (w->n_items + 1) * sizeof(int));
    if (nb) { w->tab_badges = nb; w->tab_badges[w->n_items] = 0; }
    w->n_items++;
    if (w->sel < 0) w->sel = 0;
    tk_widget_invalidate(w);
}
void tk_tabbar_set_item_icon(tk_widget *w, int idx,
                             const uint32_t *px, int iw, int ih)
{
    if (!w || w->kind != TK_W_TABBAR || idx < 0 ||
        idx >= (int)w->n_items)
        return;
    if ((size_t)idx >= w->n_item_icons) {
        const uint32_t **na = realloc((void *)w->item_icons,
                                      w->n_items * sizeof(uint32_t *));
        if (!na) return;
        for (size_t i = w->n_item_icons; i < w->n_items; i++) na[i] = NULL;
        w->item_icons = na;
        w->n_item_icons = w->n_items;
    }
    w->item_icons[idx] = px;
    w->item_icon_w = iw; w->item_icon_h = ih;
    tk_widget_invalidate(w);
}
void tk_tabbar_set_item_badge(tk_widget *w, int idx, int count)
{
    if (!w || w->kind != TK_W_TABBAR || idx < 0 ||
        idx >= (int)w->n_items || !w->tab_badges)
        return;
    if (w->tab_badges[idx] != count) {
        w->tab_badges[idx] = count;
        tk_widget_invalidate(w);
    }
}
int tk_tabbar_selected(const tk_widget *w)
{
    return w ? w->sel : -1;
}
void tk_tabbar_select(tk_widget *w, int idx)
{
    if (!w || w->kind != TK_W_TABBAR) return;
    if (idx < 0 || idx >= (int)w->n_items) return;
    if (w->sel != idx) {
        /* M13: capture the indicator's displayed position
         * before the selection moves, then slide it */
        int from_x = w->tab_ind_x >= 0 ? w->tab_ind_x
                     : (w->sel >= 0 ? tabbar_ind_x(w, w->sel) : -1);
        w->sel = idx;
        tabbar_ind_slide(w, from_x);
        tk_widget_invalidate(w);
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_tabbar_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (!w) return;
    w->cb1 = cb; w->cb1_ud = ud;
}

/* ---------------- Chip (M6) ---------------- */
tk_widget *tk_chip_new(tk_chip_kind kind, const char *label)
{
    tk_widget *w = leaf(TK_W_CHIP, label, true);
    if (w) w->chip_kind = (int)kind;
    return w;
}
void tk_chip_set_icon(tk_widget *w, const uint32_t *px, int iw, int ih)
{
    if (!w || w->kind != TK_W_CHIP) return;
    free(w->image_px);
    w->image_px = NULL; w->image_w = 0; w->image_h = 0;
    if (px && iw > 0 && ih > 0) {
        uint32_t *copy = malloc((size_t)iw * (size_t)ih * sizeof(uint32_t));
        if (!copy) return;
        memcpy(copy, px, (size_t)iw * (size_t)ih * sizeof(uint32_t));
        w->image_px = copy; w->image_w = iw; w->image_h = ih;
    }
    tk_widget_invalidate(w);
}
bool tk_chip_selected(const tk_widget *w)
{
    return w && w->chip_selected;
}
void tk_chip_set_selected(tk_widget *w, bool on)
{
    if (!w || w->kind != TK_W_CHIP) return;
    if (w->chip_selected != on) {
        w->chip_selected = on;
        /* M17: the leading check appears/disappears through
         * the selection glyph transition (the M3 chip's
         * AnimatedVisibility expand + fade, on the toolkit's
         * selection pair), like the checkbox's check */
        glyph_set(w, on);
        tk_widget_invalidate(w);
    }
}
void tk_chip_set_on_click(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (!w) return;
    w->cb1 = cb; w->cb1_ud = ud;
}
void tk_chip_set_on_close(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (!w) return;
    w->cb_close = cb; w->cb_close_ud = ud;
}

/* ---------------- Badge (M6) ---------------- */
tk_widget *tk_badge_new(int count)
{
    tk_widget *w = tk_widget_new(TK_W_BADGE);
    if (w) w->badge_count = count;
    return w;
}
void tk_badge_set_count(tk_widget *w, int count)
{
    if (!w || w->kind != TK_W_BADGE) return;
    if (w->badge_count != count) {
        w->badge_count = count;
        tk_widget_invalidate(w);
    }
}
int tk_badge_count(const tk_widget *w)
{
    return w ? w->badge_count : 0;
}
static int tabs_bar_h(const tk_theme *t) { (void)t; return TK_TAB_BAR_H; }
static int tabs_seg_x(const tk_theme *t, tk_widget *w, int idx, int *seg_w)
{
    int x = 0;
    for (int i = 0; i < idx && i < (int)w->n_tab_titles; i++)
        x += tk_text_width(t, w->tab_titles[i]) + 24;
    if (seg_w)
        *seg_w = (idx < (int)w->n_tab_titles)
                 ? tk_text_width(t, w->tab_titles[idx]) + 24 : 0;
    return x;
}

/* ---------------- Dialog ---------------- */

tk_widget *tk_dialog_new(const char *title)
{
    tk_widget *w = tk_widget_new(TK_W_DIALOG);
    if (!w) return NULL;
    w->text = tk_strdup(title);
    w->dir = TK_DIR_VERTICAL;
    w->spacing = 8;
    w->padding = 16;
    w->pad_x = 16; w->pad_y = 16;
    w->can_focus = true;
    return w;
}

/* ---------------- Menu ---------------- */

tk_widget *tk_menu_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_MENU);
    if (w) { w->can_focus = true; w->menu_last_id = -1; w->menu_hover = -1; }
    return w;
}

static tk_menu_item *menu_add(tk_widget *menu, const char *label, int id)
{
    tk_menu_item *ni = realloc(menu->menu_items,
                               (menu->n_menu_items + 1) * sizeof(tk_menu_item));
    if (!ni) return NULL;
    menu->menu_items = ni;
    tk_menu_item *it = &menu->menu_items[menu->n_menu_items++];
    memset(it, 0, sizeof(*it));
    it->label = tk_strdup(label);
    it->id = id;
    it->enabled = true;
    return it;
}
void tk_menu_add_item(tk_widget *menu, const char *label, int id)
{
    (void)menu_add(menu, label, id);
}
void tk_menu_add_check_item(tk_widget *menu, const char *label, int id,
                            bool checked)
{
    tk_menu_item *it = menu_add(menu, label, id);
    if (it) { it->checkable = true; it->checked = checked; }
}
void tk_menu_add_separator(tk_widget *menu)
{
    tk_menu_item *it = menu_add(menu, NULL, -1);
    if (it) it->separator = true;
}
void tk_menu_add_submenu(tk_widget *menu, const char *label,
                         tk_widget *submenu)
{
    tk_menu_item *it = menu_add(menu, label, -1);
    if (it) it->submenu = submenu;
}
static tk_menu_item *menu_find(tk_widget *menu, int id)
{
    for (size_t i = 0; i < menu->n_menu_items; i++)
        if (!menu->menu_items[i].separator && menu->menu_items[i].id == id)
            return &menu->menu_items[i];
    return NULL;
}
void tk_menu_set_item_enabled(tk_widget *menu, int id, bool enabled)
{
    tk_menu_item *it = menu_find(menu, id);
    if (it && it->enabled != enabled) {
        it->enabled = enabled;
        tk_widget_invalidate(menu); /* M5 */
    }
}

/* M7: menu item trailing text / badge */
void tk_menu_set_item_trailing(tk_widget *menu, int id,
                               const char *text)
{
    tk_menu_item *it = menu_find(menu, id);
    if (!it) return;
    free(it->trailing);
    it->trailing = tk_strdup(text);
    tk_widget_invalidate(menu);
}
void tk_menu_set_item_badge(tk_widget *menu, int id, int count)
{
    tk_menu_item *it = menu_find(menu, id);
    if (!it) return;
    it->badge = count;
    tk_widget_invalidate(menu);
}
void tk_menu_set_item_icon(tk_widget *menu, int id,
                           const uint32_t *px, int iw, int ih)
{
    tk_menu_item *it = menu_find(menu, id);
    if (!it) return;
    it->icon = px; it->icon_w = iw; it->icon_h = ih;
    tk_widget_invalidate(menu);
}
bool tk_menu_item_checked(const tk_widget *menu, int id)
{
    tk_menu_item *it = menu_find((tk_widget *)menu, id);
    return it ? it->checked : false;
}
int tk_menu_last_id(const tk_widget *menu) { return menu->menu_last_id; }
void tk_menu_set_on_select(tk_widget *menu, tk_notify_cb cb, void *ud)
{
    menu->cb1 = cb; menu->cb1_ud = ud;
}

static bool menu_item_selectable(const tk_menu_item *it)
{
    return !it->separator && it->enabled;
}
static int menu_first_selectable(tk_widget *menu)
{
    for (size_t i = 0; i < menu->n_menu_items; i++)
        if (menu_item_selectable(&menu->menu_items[i])) return (int)i;
    return -1;
}
/* M9: the menu item content inset is a spec dimension -
 * M3 menu item horizontal padding 12dp, WinUI
 * MenuFlyoutItem content margin 11epx (both from the M8
 * dimension audit's fetched sources). At the corrected
 * ~179.2ppi premise those convert to 13.4px and 14.8px
 * (x1.344); the implemented 14 / 16 are retained
 * (REPORT-toolkit-m23.md). The classic theme keeps the
 * unit grid. */
static int menu_inset(const tk_theme *t)
{
    if (t->style == 1) return 14;
    if (t->style == 2) return 16;
    return t->unit;
}

static int menu_row_h(tk_widget *menu, int idx, const tk_theme *t)
{
    /* Menus are transient popups, not list rows: they use the
     * control height instead of the theme row_h (sized for
     * settings/list rows). At row_h an 8-item menu overflows
     * a 428px-tall window; at control_h it fits, and the
     * height still meets the touch-target floor for a
     * finger-operated popup.
     * M22 collation (REPORT-toolkit-m22 SS6): the M3
     * reference item height is 48dp (MenuListItemContainer-
     * Height in material3's Menu.kt) = 53.8 -> 54px at the
     * corrected premise, which is exactly control_h in the
     * v2 themes since the floor change. The WinUI template
     * has no fixed item height - it is Padding (8+9epx)
     * plus the text line - so the shared control_h row is
     * a structural difference there, recorded in the
     * report; the classic theme keeps its 48px. */
    return menu->menu_items[idx].separator ? 9 : t->control_h;
}
static int menu_item_at_y(tk_widget *menu, const tk_theme *t, int y)
{
    int cy = menu->rect.y;
    for (size_t i = 0; i < menu->n_menu_items; i++) {
        int rh = menu_row_h(menu, (int)i, t);
        if (y >= cy && y < cy + rh) return (int)i;
        cy += rh;
    }
    return -1;
}
static int menu_item_y(tk_widget *menu, const tk_theme *t, int idx)
{
    int cy = menu->rect.y;
    for (int i = 0; i < idx; i++) cy += menu_row_h(menu, i, t);
    return cy;
}

/* M20: the menu transition's own timing and form, per mode.
 * M3 (material3 Menu.kt; the constants were re-fetched
 * 2026-11-21 - InTransitionDuration 120 ms /
 * OutTransitionDuration 75 ms, mirrored verbatim in
 * Microsoft's fluentui-android Menu.kt): entrance 120 ms,
 * exit 75 ms, one transition value driving alpha AND the
 * scale 0.8 <-> 1.0 about the anchor-side origin.
 * WinUI: the pop-in is the Direct Entrance / Direct Exit
 * pair (167 ms, Microsoft Learn "Motion in Windows" -
 * the toolkit's small-surface pair), and its form is
 * fade + translation (this toolkit's slide): the primary
 * sources carry NO scale - PopupThemeTransition has only
 * offset properties and PopInThemeAnimation is documented
 * as "combines opacity and translation" (Microsoft Learn,
 * re-fetched 2026-11-21). The scale is therefore driven
 * in the M3 form only; WinUI menus keep paint_scale at
 * its rest value. */
static void menu_trans_pair(bool exit_run, uint32_t *dur,
                            tk_ease_fn *ease)
{
    const tk_theme *t = tk_theme_default();
    if (t && t->style == 2) {
        tk_anim_mode_pair(false, dur, ease);
        return;
    }
    *dur = exit_run ? 75 : 120;
    *ease = tk_ease_standard_decelerate;
}

static bool menu_scale_form(void)
{
    const tk_theme *t = tk_theme_default();
    return !t || t->style != 2;
}

/* M17: the menu fade (entrance + exit share one animation
 * on paint_alpha). The exit defers the overlay removal -
 * and with it the focus return, which
 * tk_root_remove_overlay performs - until the fade
 * completes: the same structure as the dialog exit
 * (tk_dialog_close / tk_overlay_slide_out), whose deferral
 * M14 established as the correct focus order, so the M15
 * interference does not arise here.
 * M20: the same animation value also drives the M3 menu
 * transition's scale component (0.8 <-> 1.0 about the
 * anchor-side origin, material3 Menu.kt - re-fetched
 * 2026-11-21): paint_scale = 800 + alpha * 200 / 255, so
 * entrance and exit scale in lockstep with the fade,
 * exactly as the reference's single transition drives both.
 * The scale is rendered by the overlay painter's scaled
 * path (tk_core.c); at rest paint_scale is 1000 and the
 * menu paints directly, byte-identical to the M17 form. */
static void menu_fade_cb(void *ud, int v)
{
    tk_widget *m = ud;
    m->paint_alpha = v;
    /* M20: the scale follows the same value (M3 form only) */
    m->paint_scale = menu_scale_form() ? 800 + v * 200 / 255
                                       : 1000;
    tk_widget_invalidate(m);
}

static void menu_enter_done(void *ud)
{
    tk_widget *m = ud;
    m->fade_anim = NULL;
    m->paint_alpha = 255;
    m->paint_scale = 1000;
    tk_widget_invalidate(m);
}

static void menu_exit_done(void *ud)
{
    tk_widget *m = ud;
    m->fade_anim = NULL;
    m->menu_closing = false;
    tk_root *r = tk_widget_root(m);
    /* removal only: the auto_free hand-off stays with
     * tk_close_all_menus, exactly as tk_menu_close's
     * synchronous form leaves it */
    if (r) tk_root_remove_overlay(r, m);
}

static void menu_popup_at(tk_root *r, tk_widget *menu, int x, int y)
{
    int req_x = x, req_y = y; /* M20: pre-clamp popup point */
    tk_size d = tk_measure(menu, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    if (r->w > 0) {
        if (x + d.w > r->w) x = r->w - d.w;
        if (y + d.h > r->h) y = r->h - d.h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    menu->fixed_rect = (tk_rect){x, y, d.w, d.h};
    /* M20: the scale transform origin, per-mille of the menu
     * rect. The popup point is the anchor corner; a menu that
     * had to be pushed back on screen grows from the opposite
     * side instead (its anchor lies past the clamped edge).
     * Anchored callers refine the origin after the popup
     * (split button: right-aligned; submenus: the item). */
    menu->scale_ox = x < req_x ? 1000 : 0;
    menu->scale_oy = y < req_y ? 1000 : 0;
    menu->paint_scale = 1000;
    tk_root_add_overlay(r, menu);
    tk_arrange(menu, menu->fixed_rect);
    tk_widget_invalidate(menu); /* M3: popup area needs painting */
    /* M17: record the show time (the exit gate compares it
     * with the root clock, like the dialog's) and start the
     * entrance fade when a live clock is running */
    menu->show_clock = r->anim_now;
    menu->paint_alpha = 255;
    menu->menu_closing = false;
    if (menu->fade_anim) {
        tk_anim_cancel(menu->fade_anim);
        menu->fade_anim = NULL;
    }
    /* M20: the menu transition's own pair (menu_trans_pair:
     * M3 120 ms entrance with the scale, WinUI the 167 ms
     * pop-in pair); the slide shares it so the drop lands
     * with the fade instead of drifting past it */
    uint32_t dur;
    tk_ease_fn ease;
    menu_trans_pair(false, &dur, &ease);
    if (r->anim_now != 0 && tk_root_animations_enabled(r)) {
        menu->paint_alpha = 0;
        /* M20: the scale's low end (M3 form only) */
        menu->paint_scale = menu_scale_form() ? 800 : 1000;
        menu->fade_anim = tk_anim_start(r, 0, 255, (int)dur,
                                        ease, menu_fade_cb,
                                        menu_enter_done, menu);
        /* a NULL handle: delivered synchronously, the menu
         * is fully opaque already */
    }
    tk_overlay_slide_in_dur(r, menu, -6, dur, ease);
    menu->menu_hover = menu_first_selectable(menu);
    /* M14: remember who held the focus; closing the menu
     * returns it there (see tk_root_remove_overlay) */
    menu->focus_return = (r->focused && r->focused != menu)
                             ? r->focused : NULL;
    tk_focus(r, menu);
}

void tk_menu_popup(tk_root *r, tk_widget *menu, int x, int y)
{
    if (!r || !menu) return;
    tk_close_all_menus(r);
    menu_popup_at(r, menu, x, y);
}

void tk_menu_close_animated(tk_root *r, tk_widget *menu,
                            bool force)
{
    if (!r || !menu) return;
    if (menu->menu_closing) {
        if (!force) return; /* the exit is already running */
        /* complete the exit now (tk_close_all_menus and
         * re-popups take this path) */
        if (menu->fade_anim) {
            tk_anim_cancel(menu->fade_anim);
            menu->fade_anim = NULL;
        }
        menu->menu_closing = false;
        if (menu->open_sub) {
            tk_root_remove_overlay(r, menu->open_sub);
            menu->open_sub = NULL;
        }
        tk_root_remove_overlay(r, menu);
        return;
    }
    tk_split_on_menu_closed(menu); /* M8: split chevron home */
    if (menu->open_sub) {
        tk_root_remove_overlay(r, menu->open_sub);
        menu->open_sub = NULL;
    }
    bool live = r->anim_now > menu->show_clock &&
                tk_root_animations_enabled(r);
    if (!live || force) {
        tk_root_remove_overlay(r, menu);
        return;
    }
    /* M17: the exit run - fade out over the menu pair's exit
     * duration, then remove (menu_exit_done). The focus
     * return happens inside the removal, at the end of the
     * fade, as with the dialog exit. */
    menu->menu_closing = true;
    if (menu->fade_anim) { /* the entrance fade is running */
        tk_anim_cancel(menu->fade_anim);
        menu->fade_anim = NULL;
    }
    {
        uint32_t dur;
        tk_ease_fn ease;
        /* M20: the exit has its own sourced duration
         * (M3 75 ms / WinUI 167 ms) */
        menu_trans_pair(true, &dur, &ease);
        menu->fade_anim = tk_anim_start(r, menu->paint_alpha,
                                        0, (int)dur, ease,
                                        menu_fade_cb,
                                        menu_exit_done, menu);
    }
    /* a NULL handle: delivered synchronously, the menu is
     * already removed */
}

void tk_menu_close(tk_root *r, tk_widget *menu)
{
    tk_menu_close_animated(r, menu, false);
}

bool tk_menu_is_open(const tk_root *r, const tk_widget *menu)
{
    return tk_root_has_overlay(r, menu);
}

static void menu_close_sub(tk_root *r, tk_widget *menu)
{
    if (menu->open_sub) {
        tk_root_remove_overlay(r, menu->open_sub);
        menu->open_sub = NULL;
    }
}

static void menu_open_sub(tk_root *r, tk_widget *menu, int idx)
{
    tk_widget *sub = menu->menu_items[idx].submenu;
    if (!sub) return;
    if (menu->open_sub == sub) return;
    menu_close_sub(r, menu);
    int sx = menu->rect.x + menu->rect.w - 4;
    int sy = menu_item_y(menu, metrics(), idx);
    menu_popup_at(r, sub, sx, sy);
    /* M20: the submenu grows from its anchor item: origin at
     * the item's vertical centre on the submenu's left edge
     * (computed against the final, clamped rect) */
    if (sub->rect.h > 0) {
        int ih = menu_row_h(menu, idx, metrics());
        int oy = (sy + ih / 2 - sub->rect.y) * 1000 / sub->rect.h;
        sub->scale_ox = 0;
        sub->scale_oy = oy < 0 ? 0 : oy > 1000 ? 1000 : oy;
    }
    menu->open_sub = sub;
    sub->owner_ref = menu;
}

/* Activate item idx. May free the menu (combobox dropdowns), so
 * callers must not touch the menu after this returns. */
static bool menu_activate(tk_root *r, tk_widget *menu, int idx)
{
    if (idx < 0 || idx >= (int)menu->n_menu_items) return true;
    tk_menu_item *it = &menu->menu_items[idx];
    if (!menu_item_selectable(it)) return true;
    if (it->submenu) { menu_open_sub(r, menu, idx); return true; }
    menu->menu_last_id = it->id;
    if (it->checkable) it->checked = !it->checked;
    tk_notify_cb cb = menu->cb1;
    void *ud = menu->cb1_ud;
    tk_invoke(cb, menu, ud);
    tk_close_all_menus(r); /* may free `menu` (auto_free) */
    return true;
}

/* ---------------- MenuBar ---------------- */

tk_widget *tk_menubar_new(void) { return tk_widget_new(TK_W_MENUBAR); }

void tk_menubar_add_menu(tk_widget *bar, const char *title, tk_widget *menu)
{
    char **nt = realloc(bar->bar_titles, (bar->n_bar + 1) * sizeof(char *));
    if (!nt) return;
    bar->bar_titles = nt;
    tk_widget **nm = realloc(bar->bar_menus,
                             (bar->n_bar + 1) * sizeof(tk_widget *));
    if (!nm) return;
    bar->bar_menus = nm;
    bar->bar_titles[bar->n_bar] = tk_strdup(title);
    bar->bar_menus[bar->n_bar] = menu;
    bar->n_bar++;
}
static int menubar_title_at(tk_widget *bar, const tk_theme *t, int x)
{
    int cx = bar->rect.x + t->unit;
    for (size_t i = 0; i < bar->n_bar; i++) {
        int tw = tk_text_width(t, bar->bar_titles[i]) + 16;
        if (x >= cx && x < cx + tw) return (int)i;
        cx += tw;
    }
    return -1;
}

/* ---------------- ComboBox ---------------- */

tk_widget *tk_combobox_new(void)
{
    tk_widget *w = leaf(TK_W_COMBOBOX, NULL, true);
    if (w) w->sel = -1;
    return w;
}
void tk_combobox_set_items(tk_widget *w, const char *const *items, size_t n)
{
    for (size_t i = 0; i < w->n_items; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL; w->n_items = 0;
    if (n) {
        w->items = calloc(n, sizeof(char *));
        if (!w->items) return;
        for (size_t i = 0; i < n; i++) w->items[i] = tk_strdup(items[i]);
        w->n_items = n;
    }
    if (w->sel >= (int)w->n_items) w->sel = w->n_items ? (int)w->n_items - 1 : -1;
    tk_widget_invalidate(w); /* M5 */
}
int tk_combobox_selected(const tk_widget *w) { return w->sel; }

/* ---- ComboBox direct text entry (M3) ---------------------------- */
static void combo_sync_text(tk_widget *w)
{
    free(w->text);
    w->text = tk_strdup((w->sel >= 0 && w->sel < (int)w->n_items)
                        ? w->items[w->sel] : "");
    w->edit_pos = w->text ? (int)strlen(w->text) : 0;
}
static void combo_commit(tk_widget *w)
{
    if (!w->text) return;
    for (size_t i = 0; i < w->n_items; i++)
        if (strcmp(w->text, w->items[i]) == 0) {
            tk_combobox_select(w, (int)i); /* exact match selects */
            return;
        }
    /* custom value: no item selected, but the change is real */
    w->sel = -1;
    tk_invoke(w->cb1, w, w->cb1_ud);
}
void tk_combobox_set_editable(tk_widget *w, bool editable)
{
    if (!w || w->b2 == editable) return;
    w->b2 = editable;
    if (!editable && w->text) { free(w->text); w->text = NULL; }
    tk_widget_invalidate(w); /* M5 */
}
bool tk_combobox_is_editable(const tk_widget *w) { return w->b2; }
const char *tk_combobox_text(const tk_widget *w)
{
    if (w->text) return w->text;
    if (w->sel >= 0 && w->sel < (int)w->n_items) return w->items[w->sel];
    return "";
}
void tk_combobox_set_text(tk_widget *w, const char *text)
{
    if (!w) return;
    if (w->text && text && strcmp(w->text, text) == 0) return;
    free(w->text);
    w->text = tk_strdup(text ? text : "");
    w->edit_pos = w->text ? (int)strlen(w->text) : 0;
    tk_widget_invalidate(w); /* M5 */
}

void tk_combobox_select(tk_widget *w, int idx)
{
    if (idx < -1) idx = -1;
    if (idx >= (int)w->n_items) idx = (int)w->n_items - 1;
    if (idx != w->sel) {
        w->sel = idx;
        if (w->b2) combo_sync_text(w); /* text follows selection */
        tk_widget_invalidate(w); /* M5 */
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_combobox_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}
bool tk_combobox_is_open(const tk_widget *w) { return w->open_sub != NULL; }

static void combo_on_menu_select(tk_widget *menu, void *ud)
{
    (void)ud;
    tk_widget *combo = menu->owner_ref;
    if (!combo) return;
    combo->open_sub = NULL;
    int id = tk_menu_last_id(menu);
    if (id >= 0) tk_combobox_select(combo, id); /* syncs text if editable */
}

static void combo_open(tk_widget *w, tk_root *root)
{
    if (!root || w->open_sub || w->n_items == 0) return;
    /* M9: if this press already dismissed this combo's
     * dropdown (the root records the owner), the press is the
     * dropdown's toggle-off, not a fresh open */
    if (root->down_dismissed_owner == w) {
        root->down_dismissed_owner = NULL;
        return;
    }
    tk_widget *menu = tk_menu_new();
    if (!menu) return;
    for (size_t i = 0; i < w->n_items; i++)
        tk_menu_add_item(menu, w->items[i], (int)i);
    menu->auto_free = true;
    menu->owner_ref = w;
    menu->i1 = w->rect.w;
    tk_menu_set_on_select(menu, combo_on_menu_select, NULL);
    w->open_sub = menu;
    tk_menu_popup(root, menu, w->rect.x, w->rect.y + w->rect.h);
}

/* ---------------- TextView ---------------- */

typedef struct { size_t start, len; } tv_line;
typedef struct {
    tv_line *lines;
    size_t n, cap;
    char *text;              /* owned copy of the buffer content */
    int wrap_w;
} tv_lines;

static size_t tv_base;
static void tv_collect2(void *ud, size_t start, size_t len)
{
    tv_lines *L = ud;
    if (L->n >= L->cap) {
        size_t nc = L->cap ? L->cap * 2 : 16;
        tv_line *na = realloc(L->lines, nc * sizeof(tv_line));
        if (!na) return;
        L->lines = na; L->cap = nc;
    }
    L->lines[L->n].start = tv_base + start;
    L->lines[L->n].len = len;
    L->n++;
}

/* Build the visual (wrapped) lines of the buffer at wrap_w px.
 * Caller frees with tv_lines_free. */
static void tv_build(tk_widget *w, int wrap_w, tv_lines *L)
{
    memset(L, 0, sizeof(*L));
    L->wrap_w = wrap_w;
    size_t total = tc_length(w->buffer);
    L->text = malloc(total + 1);
    if (!L->text) return;
    tc_text(w->buffer, L->text, total + 1);
    size_t nlog = tc_line_count(w->buffer);
    for (size_t li = 0; li < nlog; li++) {
        size_t ls = 0, ll = 0;
        if (!tc_line_range(w->buffer, li, &ls, &ll)) continue;
        char *sub = malloc(ll + 1);
        if (!sub) continue;
        tc_range_text(w->buffer, ls, ls + ll, sub, ll + 1);
        tv_base = ls;
        tk_text_wrap(metrics(), sub, wrap_w, tv_collect2, L);
        free(sub);
    }
    if (L->n == 0) {         /* empty buffer: one empty visual line */
        tv_base = 0;
        tv_collect2(L, 0, 0);
    }
}
static void tv_lines_free(tv_lines *L)
{
    free(L->lines);
    free(L->text);
    memset(L, 0, sizeof(*L));
}
static int tv_wrap_w(tk_widget *w)
{
    int pw = w->rect.w > 0 ? w->rect.w : 320;
    int ww = pw - 2 * metrics()->unit;
    return ww < 40 ? 40 : ww;
}
/* Locate the visual line containing byte offset `cur`; also returns
 * the caret x in px within that line. */
static int tv_cursor_line(tk_widget *w, const tv_lines *L, size_t cur,
                          int *x_px)
{
    (void)w;
    const tk_theme *t = metrics();
    for (size_t i = 0; i < L->n; i++) {
        size_t s = L->lines[i].start, e = s + L->lines[i].len;
        if (cur > e) continue;
        if (cur == e && i + 1 < L->n && L->lines[i + 1].start == cur)
            continue;        /* wrap boundary: caret starts next line */
        if (x_px) {
            size_t n = cur > s ? cur - s : 0;
            char *sub = malloc(n + 1);
            if (sub) {
                memcpy(sub, L->text + s, n);
                sub[n] = '\0';
                *x_px = tk_text_width(t, sub);
                free(sub);
            } else *x_px = 0;
        }
        return (int)i;
    }
    if (x_px) *x_px = 0;
    return L->n ? (int)L->n - 1 : 0;
}

static void tv_ensure_visible(tk_widget *w)
{
    if (w->rect.h <= 0) return;
    tv_lines L;
    tv_build(w, tv_wrap_w(w), &L);
    int idx = tv_cursor_line(w, &L, tc_cursor(w->buffer), NULL);
    int rows = w->rect.h / tk_line_height(metrics());
    if (rows < 1) rows = 1;
    if (idx < w->top_index) w->top_index = idx;
    if (idx >= w->top_index + rows) w->top_index = idx - rows + 1;
    tv_lines_free(&L);
}

/* Byte offset of the text position nearest to root-space (x, y).
 * With clamp_visible, the row is clamped to the currently visible
 * rows - used while drag-selecting (M3), so the selection does not
 * run away past the viewport edge mid-gesture. */
static size_t tv_offset_at(tk_widget *w, int x, int y, bool clamp_visible)
{
    const tk_theme *t = metrics();
    tv_lines L;
    tv_build(w, tv_wrap_w(w), &L);
    size_t off = tc_length(w->buffer);
    if (L.n) {
        int lh = tk_line_height(t);
        int row = (y - w->rect.y - 4) / lh + w->top_index;
        if (clamp_visible) {
            int rows = w->rect.h / lh;
            if (rows < 1) rows = 1;
            if (row < w->top_index) row = w->top_index;
            if (row > w->top_index + rows - 1)
                row = w->top_index + rows - 1;
        }
        if (row < 0) row = 0;
        if (row >= (int)L.n) row = (int)L.n - 1;
        tv_line *ln = &L.lines[row];
        char *sub = malloc(ln->len + 1);
        if (sub) {
            memcpy(sub, L.text + ln->start, ln->len);
            sub[ln->len] = '\0';
            off = ln->start + tk_offset_at_x(t, sub,
                                             x - w->rect.x - t->unit);
            free(sub);
        } else {
            off = ln->start;
        }
    }
    tv_lines_free(&L);
    return off;
}

static bool tv_move_vertical(tk_widget *w, int dir, bool extend)
{
    tv_lines L;
    tv_build(w, tv_wrap_w(w), &L);
    if (L.n == 0) { tv_lines_free(&L); return false; }
    int x_px = 0;
    int idx = tv_cursor_line(w, &L, tc_cursor(w->buffer), &x_px);
    int target = idx + dir;
    if (target < 0) target = 0;
    if (target >= (int)L.n) target = (int)L.n - 1;
    if (target == idx) { tv_lines_free(&L); return false; }
    tv_line *tl = &L.lines[target];
    char *sub = malloc(tl->len + 1);
    if (!sub) { tv_lines_free(&L); return false; }
    memcpy(sub, L.text + tl->start, tl->len);
    sub[tl->len] = '\0';
    size_t rel = tk_offset_at_x(metrics(), sub, x_px);
    free(sub);
    tc_move_to(w->buffer, tl->start + rel, extend);
    tv_lines_free(&L);
    tv_ensure_visible(w);
    return true;
}

tk_widget *tk_textview_new(const char *initial)
{
    tk_widget *w = leaf(TK_W_TEXTVIEW, NULL, true);
    if (!w) return NULL;
    w->buffer = tc_new();
    if (w->buffer) tc_set_clipboard(w->buffer, &tk_shared_clipboard);
    w->editable = true;
    if (initial) tc_set_text(w->buffer, initial);
    return w;
}
tc_buffer *tk_textview_buffer(tk_widget *w) { return w->buffer; }
void tk_textview_set_text(tk_widget *w, const char *text)
{
    tc_set_text(w->buffer, text ? text : "");
    w->top_index = 0;
    tk_widget_invalidate(w); /* M5 */
    tk_invoke(w->cb1, w, w->cb1_ud);
}
size_t tk_textview_text(const tk_widget *w, char *out, size_t cap)
{
    return tc_text(w->buffer, out, cap);
}
void tk_textview_set_editable(tk_widget *w, bool editable)
{
    if (!w || w->editable == editable) return;
    w->editable = editable;
    tk_widget_invalidate(w); /* M5 */
}
bool tk_textview_is_editable(const tk_widget *w) { return w->editable; }
void tk_textview_set_clipboard(tk_widget *w, const tc_clipboard *cb)
{
    tc_set_clipboard(w->buffer, cb);
}
void tk_textview_set_on_line_tap(tk_widget *w, tk_line_cb cb, void *ud)
{
    if (!w) return;
    w->line_cb = cb; w->line_cb_ud = ud;
}
void tk_textview_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}
int tk_textview_visual_lines(const tk_widget *w)
{
    tv_lines L;
    tv_build((tk_widget *)w, tv_wrap_w((tk_widget *)w), &L);
    int n = (int)L.n;
    tv_lines_free(&L);
    return n;
}
void tk_textview_ensure_cursor_visible(tk_widget *w)
{
    tv_ensure_visible(w);
}

/* ---------------- TreeView ---------------- */

tk_widget *tk_treeview_new(void)
{
    tk_widget *w = leaf(TK_W_TREEVIEW, NULL, true);
    if (w) w->sel_id = -1;
    return w;
}

/* M7: model-interface helpers (defined with the model API
 * below, after the manual-form accessors) */
static int tree_index_of_model(const tk_widget *w, int mid);
static bool model_id_expanded(const tk_widget *w, int mid);
static void model_set_expanded(tk_widget *w, int mid, bool on);
int tk_treeview_add(tk_widget *w, int parent_id, const char *label)
{
    if (w->tree_model) return -1; /* M7: the model owns content */
    if (parent_id < -1 || parent_id >= (int)w->n_nodes) return -1;
    if (w->n_nodes >= w->cap_nodes) {
        size_t nc = w->cap_nodes ? w->cap_nodes * 2 : 16;
        tk_tree_node *na = realloc(w->nodes, nc * sizeof(tk_tree_node));
        if (!na) return -1;
        w->nodes = na; w->cap_nodes = nc;
    }
    int id = (int)w->n_nodes++;
    w->nodes[id].label = tk_strdup(label);
    w->nodes[id].parent = parent_id;
    w->nodes[id].expanded = true;
    tk_widget_invalidate(w); /* M5 */
    return id;
}
size_t tk_treeview_node_count(const tk_widget *w) { return w->n_nodes; }

typedef struct {
    tk_widget *w;
    int *ids, *depths;
    int n, cap;
} flat_ctx;
static void flat_walk(flat_ctx *ctx, int parent, int depth)
{
    tk_widget *w = ctx->w;
    for (size_t i = 0; i < w->n_nodes; i++) {
        if (w->nodes[i].parent != parent) continue;
        if (ctx->n >= ctx->cap) {
            int nc = ctx->cap ? ctx->cap * 2 : 16;
            int *ni = realloc(ctx->ids, (size_t)nc * sizeof(int));
            if (!ni) return;
            ctx->ids = ni;
            int *nd = realloc(ctx->depths, (size_t)nc * sizeof(int));
            if (!nd) return;
            ctx->depths = nd;
            ctx->cap = nc;
        }
        ctx->ids[ctx->n] = (int)i;
        ctx->depths[ctx->n] = depth;
        ctx->n++;
        if (w->nodes[i].expanded) flat_walk(ctx, (int)i, depth + 1);
    }
}
static int tree_flatten(tk_widget *w, int **ids, int **depths)
{
    flat_ctx ctx = { w, NULL, NULL, 0, 0 };
    flat_walk(&ctx, -1, 0);
    *ids = ctx.ids; *depths = ctx.depths;
    return ctx.n;
}
static bool tree_has_children(const tk_widget *w, int id)
{
    for (size_t i = 0; i < w->n_nodes; i++)
        if (w->nodes[i].parent == id) return true;
    return false;
}
int tk_treeview_visible_rows(const tk_widget *w)
{
    int *ids, *depths;
    int n = tree_flatten((tk_widget *)w, &ids, &depths);
    free(ids); free(depths);
    return n;
}
int tk_treeview_row_node(const tk_widget *w, int row)
{
    int *ids, *depths;
    int n = tree_flatten((tk_widget *)w, &ids, &depths);
    int out = (row >= 0 && row < n) ? ids[row] : -1;
    free(ids); free(depths);
    /* M7: with a model set, public ids are model ids */
    if (out >= 0 && w->tree_model && w->node_model_id &&
        (size_t)out < w->n_node_model_id)
        out = w->node_model_id[out];
    return out;
}
int tk_treeview_node_depth(const tk_widget *w, int node_id)
{
    /* M7: translate a model id to the materialised index */
    if (w->tree_model) {
        node_id = tree_index_of_model(w, node_id);
        if (node_id < 0) return 0;
    }
    int d = 0;
    for (int p = node_id; p >= 0 && p < (int)w->n_nodes; ) {
        p = w->nodes[p].parent;
        if (p >= 0) d++;
    }
    return d;
}
bool tk_treeview_expanded(const tk_widget *w, int node_id)
{
    if (w->tree_model) return model_id_expanded(w, node_id);
    if (node_id < 0 || node_id >= (int)w->n_nodes) return false;
    return w->nodes[node_id].expanded;
}
void tk_treeview_set_expanded(tk_widget *w, int node_id, bool expanded)
{
    if (w->tree_model) {
        /* M7: the id is a model id - record it in the set and
         * mirror it onto the materialised node */
        model_set_expanded(w, node_id, expanded);
        int idx = tree_index_of_model(w, node_id);
        if (idx >= 0) w->nodes[idx].expanded = expanded;
        tk_widget_invalidate(w);
        return;
    }
    if (node_id < 0 || node_id >= (int)w->n_nodes) return;
    if (w->nodes[node_id].expanded != expanded) {
        w->nodes[node_id].expanded = expanded;
        tk_widget_invalidate(w); /* M5 */
    }
}
int tk_treeview_selected(const tk_widget *w)
{
    if (!w || w->sel_id < 0) return -1;
    /* M7: report the model id while a model is set */
    if (w->tree_model && w->node_model_id &&
        (size_t)w->sel_id < w->n_node_model_id)
        return w->node_model_id[w->sel_id];
    return w->sel_id;
}
void tk_treeview_select(tk_widget *w, int node_id)
{
    if (w->tree_model) {
        /* M7: the public id is a model id */
        node_id = node_id < 0 ? -1
                              : tree_index_of_model(w, node_id);
        if (node_id < -1) return;
    }
    if (node_id < -1 || node_id >= (int)w->n_nodes) return;
    if (node_id != w->sel_id) {
        w->sel_id = node_id;
        tk_widget_invalidate(w); /* M5 */
        tk_invoke(w->cb1, w, w->cb1_ud);
    }
}
void tk_treeview_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb1 = cb; w->cb1_ud = ud;
}
void tk_treeview_set_on_activate(tk_widget *w, tk_notify_cb cb, void *ud)
{
    w->cb2 = cb; w->cb2_ud = ud;
}

/* ---------------- TreeView model interface (M7) ----------------
 * The Adapter form: the model is walked once into the same
 * nodes[] store the manual form uses (so paint, events and the
 * flatten walk are shared verbatim); a parallel array maps
 * each materialised index back to its model id, and expansion
 * state lives in a set of model ids so a refresh can preserve
 * it. Public ids are MODEL ids while a model is set. */
static int tree_index_of_model(const tk_widget *w, int mid)
{
    if (!w->node_model_id) return -1;
    for (size_t i = 0; i < w->n_nodes && i < w->n_node_model_id;
         i++)
        if (w->node_model_id[i] == mid) return (int)i;
    return -1;
}
static bool model_id_expanded(const tk_widget *w, int mid)
{
    for (size_t i = 0; i < w->n_model_expanded; i++)
        if (w->model_expanded[i] == mid) return true;
    return false;
}
static void model_set_expanded(tk_widget *w, int mid, bool on)
{
    if (on) {
        if (model_id_expanded(w, mid)) return;
        if (w->n_model_expanded == w->cap_model_expanded) {
            size_t nc = w->cap_model_expanded
                            ? w->cap_model_expanded * 2 : 16;
            int *na = realloc(w->model_expanded,
                              nc * sizeof(int));
            if (!na) return;
            w->model_expanded = na;
            w->cap_model_expanded = nc;
        }
        w->model_expanded[w->n_model_expanded++] = mid;
    } else {
        for (size_t i = 0; i < w->n_model_expanded; i++)
            if (w->model_expanded[i] == mid) {
                memmove(&w->model_expanded[i],
                        &w->model_expanded[i + 1],
                        (w->n_model_expanded - i - 1) * sizeof(int));
                w->n_model_expanded--;
                return;
            }
    }
}
static void tree_clear_nodes(tk_widget *w)
{
    for (size_t i = 0; i < w->n_nodes; i++) free(w->nodes[i].label);
    w->n_nodes = 0;
    w->n_node_model_id = 0;
    w->sel_id = -1;
}
static int tree_add_raw(tk_widget *w, int parent_idx,
                        const char *label, int mid)
{
    if (w->n_nodes >= w->cap_nodes) {
        size_t nc = w->cap_nodes ? w->cap_nodes * 2 : 16;
        tk_tree_node *na = realloc(w->nodes,
                                   nc * sizeof(tk_tree_node));
        if (!na) return -1;
        w->nodes = na;
        w->cap_nodes = nc;
    }
    if (w->n_node_model_id >= w->cap_node_model_id) {
        size_t nc = w->cap_node_model_id ? w->cap_node_model_id * 2
                                         : 16;
        int *na = realloc(w->node_model_id, nc * sizeof(int));
        if (!na) return -1;
        w->node_model_id = na;
        w->cap_node_model_id = nc;
    }
    int id = (int)w->n_nodes++;
    w->nodes[id].label = tk_strdup(label);
    w->nodes[id].parent = parent_idx;
    w->nodes[id].expanded = model_id_expanded(w, mid);
    w->node_model_id[w->n_node_model_id++] = mid;
    return id;
}
static void tree_model_walk(tk_widget *w, int parent_model,
                            int parent_idx)
{
    const tk_tree_model *m = w->tree_model;
    int n = m->child_count(m->ud, parent_model);
    for (int i = 0; i < n; i++) {
        int mid = m->child_id(m->ud, parent_model, i);
        if (mid == -1) continue;
        int idx = tree_add_raw(w, parent_idx, m->label(m->ud, mid),
                               mid);
        if (idx < 0) return;
        if (!m->is_leaf(m->ud, mid))
            tree_model_walk(w, mid, idx);
    }
}
static void tree_materialise(tk_widget *w)
{
    int sel_mid = -1;
    if (w->sel_id >= 0 && w->node_model_id &&
        (size_t)w->sel_id < w->n_node_model_id)
        sel_mid = w->node_model_id[w->sel_id];
    tree_clear_nodes(w);
    if (w->tree_model) tree_model_walk(w, -1, -1);
    if (sel_mid != -1) w->sel_id = tree_index_of_model(w, sel_mid);
    tk_widget_invalidate(w);
}
void tk_treeview_set_model(tk_widget *w, const tk_tree_model *model)
{
    if (!w || w->kind != TK_W_TREEVIEW) return;
    w->tree_model = model;
    if (!model) {
        /* back to the manual form: drop the materialisation
         * and the expansion set */
        tree_clear_nodes(w);
        w->n_model_expanded = 0;
        tk_widget_invalidate(w);
        return;
    }
    tree_materialise(w);
}
bool tk_treeview_has_model(const tk_widget *w)
{
    return w && w->tree_model != NULL;
}
void tk_treeview_model_refresh(tk_widget *w)
{
    if (!w || !w->tree_model) return;
    tree_materialise(w);
}
static int tree_row_of_node(tk_widget *w, int node_id)
{
    int *ids, *depths;
    int n = tree_flatten(w, &ids, &depths);
    int out = -1;
    for (int i = 0; i < n; i++)
        if (ids[i] == node_id) { out = i; break; }
    free(ids); free(depths);
    return out;
}

/* ---------------- OverflowBar ----------------
 * A horizontal bar whose children are never clipped: at arrange
 * time, children that do not fit are hidden and collected behind
 * a "»" button; the button pops a menu that invokes the hidden
 * children's own click callbacks. */

#define OB_ZONE_W 48 /* the "»" button width (touch target, 2.2) */

tk_widget *tk_overflowbar_new(int spacing, int padding)
{
    tk_widget *w = tk_widget_new(TK_W_OVERFLOWBAR);
    if (w) {
        w->spacing = spacing;
        w->padding = padding;
        w->ovf_from = 0;
    }
    return w;
}

bool tk_overflowbar_has_overflow(const tk_widget *w)
{
    if (!w) return false;
    for (size_t i = (size_t)(w->ovf_from < 0 ? 0 : w->ovf_from);
         i < w->n_children; i++)
        if (w->children[i]) return true;
    return false;
}

/* Measure a child even when the bar has hidden it (tk_measure
 * zeroes invisible widgets; the bar must keep knowing the real
 * desired size or a hidden child could never come back). */
static tk_size ob_child_size(tk_widget *ch)
{
    bool v = ch->visible;
    ch->visible = true;
    tk_size s = tk_measure(ch, (tk_constraints){0, TK_UNLIMITED,
                                               0, TK_UNLIMITED});
    ch->visible = v;
    return s;
}

tk_size tk_ob_measure(tk_widget *w, tk_constraints c)
{
    (void)c;
    int total_w = 0, max_h = 0, n = 0;
    for (size_t i = 0; i < w->n_children; i++) {
        /* every child is force-measured (ob_child_size), including
         * ones the bar itself hid - otherwise they could never
         * come back when the bar grows */
        tk_size s = ob_child_size(w->children[i]);
        total_w += s.w + (n ? w->spacing : 0);
        if (s.h > max_h) max_h = s.h;
        n++;
    }
    return (tk_size){ total_w + 2 * w->padding,
                      max_h + 2 * w->padding };
}

void tk_ob_arrange(tk_widget *w, tk_rect r)
{
    int inner_w = r.w - 2 * w->padding;
    if (inner_w < 0) inner_w = 0;
    /* natural total of app-visible children */
    int total = 0, nvis = 0;
    for (size_t i = 0; i < w->n_children; i++) {
        tk_widget *ch = w->children[i];
        bool app_vis = ch->visible || i >= (size_t)w->ovf_from;
        if (!app_vis) continue;
        tk_size s = ob_child_size(ch);
        total += s.w + (nvis ? w->spacing : 0);
        nvis++;
    }
    bool overflow = total > inner_w;
    int ovf = (int)w->n_children;
    if (overflow) {
        int used = 0;
        ovf = (int)w->n_children;
        for (size_t i = 0; i < w->n_children; i++) {
            tk_widget *ch = w->children[i];
            bool app_vis = ch->visible || i >= (size_t)w->ovf_from;
            if (!app_vis) continue;
            int need = ch->desired.w + (used ? w->spacing : 0);
            /* reserve the "»" zone from the first child on */
            if (used + need + w->spacing + OB_ZONE_W > inner_w) {
                ovf = (int)i;
                break;
            }
            used += need;
        }
    }
    w->ovf_from = ovf;
    int x = r.x + w->padding;
    for (size_t i = 0; i < w->n_children; i++) {
        tk_widget *ch = w->children[i];
        bool app_vis = ch->visible || (int)i >= w->ovf_from;
        if (!app_vis) continue;
        if ((int)i < ovf) {
            ch->visible = true;
            int chh = ch->desired.h;
            tk_arrange(ch, (tk_rect){x, r.y + (r.h - chh) / 2,
                                     ch->desired.w, chh});
            x += ch->desired.w + w->spacing;
        } else {
            ch->visible = false;
            ch->rect = (tk_rect){0, 0, 0, 0};
        }
    }
}

static tk_rect ob_zone(const tk_widget *w)
{
    return (tk_rect){ w->rect.x + w->rect.w - w->padding - OB_ZONE_W,
                      w->rect.y, OB_ZONE_W, w->rect.h };
}

static void ob_menu_select(tk_widget *menu, void *ud)
{
    tk_widget *bar = ud;
    int id = tk_menu_last_id(menu);
    if (!bar || id < 0 || id >= (int)bar->n_children) return;
    tk_widget *ch = bar->children[id];
    /* invoke the shed child's own click callback, exactly as a
     * click on the visible button would */
    tk_invoke(ch->cb1, ch, ch->cb1_ud);
}

static void ob_popup(tk_widget *bar, tk_root *root)
{
    if (!root) return;
    if (bar->ovf_menu) {
        tk_menu_close(root, bar->ovf_menu);
        tk_widget_free(bar->ovf_menu);
        bar->ovf_menu = NULL;
    }
    tk_widget *menu = tk_menu_new();
    if (!menu) return;
    for (size_t i = (size_t)bar->ovf_from; i < bar->n_children; i++) {
        tk_widget *ch = bar->children[i];
        tk_menu_add_item(menu, ch->text ? ch->text : "", (int)i);
    }
    tk_menu_set_on_select(menu, ob_menu_select, bar);
    bar->ovf_menu = menu;
    tk_rect z = ob_zone(bar);
    tk_menu_popup(root, menu, z.x + z.w, z.y + z.h);
}

/* ---------------- M2 measure ---------------- */

tk_size tk_w2_measure(tk_widget *w, tk_constraints c)
{
    const tk_theme *t = metrics();
    switch (w->kind) {
    case TK_W_OVERFLOWBAR:
        return tk_ob_measure(w, c);
    case TK_W_SWITCH:
        /* track is 52 wide (UI-REDESIGN 2.2); the WinUI form
         * paints a 54px track (40epx at the device scale) -
         * M8: measure the painted width in both modes.
         * M21: the M3 track is the converted 61px, and the
         * WinUI label gap is the template's 12epx column
         * spacing = 17px (the generic unit, 8px, put the
         * label ~9px closer than the reference).
         * M22: the WinUI track follows the re-derived epx
         * factor (40epx at x1.344 = 53.8 -> 54px, AUDIT SS20). */
        return (tk_size){ (t->style == 2 ? 54 + 17
                           : t->style == 1 ? 61 + t->unit
                                           : 52 + t->unit) +
                          tk_text_width(t, w->text),
                          t->control_h };
    case TK_W_PROGRESS: {
        /* M9: the bar's height is the spec's own - M3 linear
         * progress track 4dp (4.5px at the corrected
         * ~179.2ppi premise; implemented 5px), WinUI
         * ProgressBarMinHeight 3epx (4.0px at x1.344;
         * implemented 4px) (ProgressBar themeresources,
         * fetched 2026-11-20). The classic theme keeps the
         * chunky 20px bar. */
        return (tk_size){ 120, t->style == 1 ? 5
                               : t->style == 2 ? 4 : 20 };
    }
    case TK_W_DATEPICKER:
        return (tk_size){ tk_text_width(t, "0000") +
                          2 * tk_text_width(t, "00") +
                          6 * t->unit + TK_PK_STEPPER_W + 12,
                          t->control_h };
    case TK_W_TIMEPICKER:
        return (tk_size){ 3 * tk_text_width(t, "00") +
                          6 * t->unit + TK_PK_STEPPER_W + 12,
                          t->control_h };
    case TK_W_SEPARATOR:
        return w->vertical ? (tk_size){ 2, 24 } : (tk_size){ 24, 2 };
    case TK_W_IMAGEVIEW:
        if (w->image_px) return (tk_size){ w->image_w, w->image_h };
        return (tk_size){ 48, 48 };
    case TK_W_SPINBUTTON:
        return (tk_size){ 120, t->control_h };
    case TK_W_COMBOBOX: {
        int widest = 80;
        for (size_t i = 0; i < w->n_items; i++) {
            int tw = tk_text_width(t, w->items[i]) + 32 + 2 * t->unit;
            if (tw > widest) widest = tw;
        }
        if (w->b2 && w->text) { /* editable: fit the current text */
            int tw = tk_text_width(t, w->text) + 32 + 2 * t->unit;
            if (tw > widest) widest = tw;
        }
        return (tk_size){ widest, t->control_h };
    }
    case TK_W_MENUBAR: {
        int tw = 2 * t->unit;
        for (size_t i = 0; i < w->n_bar; i++)
            tw += tk_text_width(t, w->bar_titles[i]) + 16;
        return (tk_size){ tw, t->control_h };
    }
    case TK_W_MENU: {
        int widest = 80, h = 0;
        bool any_check = false;
        int icon_slot = 0;
        for (size_t i = 0; i < w->n_menu_items; i++) {
            if (w->menu_items[i].checkable) any_check = true;
            if (w->menu_items[i].icon &&
                w->menu_items[i].icon_w + 12 > icon_slot)
                icon_slot = w->menu_items[i].icon_w + 12;
        }
        for (size_t i = 0; i < w->n_menu_items; i++) {
            tk_menu_item *it = &w->menu_items[i];
            h += menu_row_h(w, (int)i, t);
            if (it->separator) continue;
            int tw = tk_text_width(t, it->label) + 2 * menu_inset(t)
                     + (any_check ? 20 : 0) + (it->submenu ? 16 : 0)
                     + icon_slot
                     /* M7: trailing text / badge grow the width */
                     + (it->trailing
                            ? tk_text_width(t, it->trailing) + 12
                            : 0)
                     + (it->badge ? 30 : 0);
            if (tw > widest) widest = tw;
        }
        if (w->i1 > widest) widest = w->i1;
        return (tk_size){ widest, h };
    }
    case TK_W_SPINNER:
        return (tk_size){ w->size_px > 0 ? w->size_px : 40,
                          w->size_px > 0 ? w->size_px : 40 };
    case TK_W_BADGE: {
        if (w->badge_count == 0) return (tk_size){ 0, 0 };
        if (w->badge_count < 0) return (tk_size){ 8, 8 }; /* dot */
        char nb[8];
        if (w->badge_count > 99) snprintf(nb, sizeof nb, "99+");
        else snprintf(nb, sizeof nb, "%d", w->badge_count);
        int tw = tk_text_width_st(t, TK_TEXT_CAPTION, nb);
        return (tk_size){ tw + 12, 19 }; /* 16dp pill: 17.9px at ~179.2ppi; 19 retained, AUDIT (a-8) */
    }
    case TK_W_CHIP: {
        int tw = tk_w_text_w(w, t, w->text);
        int iw = w->image_px ? w->image_w + t->unit : 0;
        if (!w->image_px && w->kind == TK_W_CHIP &&
            w->chip_kind == TK_CHIP_FILTER && w->chip_selected)
            iw = 18 + t->unit; /* check mark slot */
        int close_w = (w->chip_kind == TK_CHIP_INPUT) ? 28 : 0;
        /* M21: in the M3 form the height is the container
         * token - 32dp. At the corrected ~179.2ppi premise
         * that converts to 35.8px; the implemented 37px is
         * the retained value of AUDIT (a-7) (the earlier
         * 40px was a round-up past the conversion).
         * The WinUI and classic forms keep the shared 40px:
         * WinUI has no first-party Chip (its form here is
         * the ToggleButton-shaped stand-in tk7 pins), and
         * no reference conversion exists for it. See
         * AUDIT SS19. */
        return (tk_size){ tw + iw + close_w + 4 * t->unit,
                          t->style == 1 ? 37 : 40 };
    }
    case TK_W_TABBAR: {
        int tw = 0;
        bool any_icon = false;
        for (size_t i = 0; i < w->n_items; i++) {
            int iw = tk_text_width(t, w->items[i]) + 24;
            if (iw < 64) iw = 64;
            tw += iw;
            if (i < w->n_item_icons && w->item_icons[i]) any_icon = true;
        }
        /* M18: the M3 primary tab with icon+label has its own
         * container height - 64dp (PrimaryNavigationTabTokens,
         * fetched 2026-11-20), 71.7px at the corrected
         * ~179.2ppi premise; the implemented 75px is retained
         * (REPORT-toolkit-m23.md) - above the two-line row
         * the toolkit used before.
         * The text-only form stays at the control height
         * (48dp target) and the WinUI TabView form keeps the
         * toolkit rows. */
        return (tk_size){ tw, any_icon
                              ? (t->style == 1 ? 75 : t->row_h2)
                              : t->control_h };
    }
    case TK_W_TEXTVIEW: {
        int wrap_w = (c.max_w < TK_UNLIMITED / 2 ? c.max_w : 320)
                     - 2 * t->unit;
        if (wrap_w < 40) wrap_w = 40;
        tv_lines L;
        tv_build(w, wrap_w, &L);
        int widest = 0;
        for (size_t i = 0; i < L.n; i++) {
            char *sub = malloc(L.lines[i].len + 1);
            if (!sub) continue;
            memcpy(sub, L.text + L.lines[i].start, L.lines[i].len);
            sub[L.lines[i].len] = '\0';
            int tw = tk_text_width(t, sub);
            if (tw > widest) widest = tw;
            free(sub);
        }
        int h = (int)L.n * tk_line_height(t) + 8;
        tv_lines_free(&L);
        int wd = widest + 2 * t->unit;
        if (wd < 120) wd = 120;
        return (tk_size){ wd, h };
    }
    case TK_W_TREEVIEW: {
        int *ids, *depths;
        int n = tree_flatten(w, &ids, &depths);
        int widest = 80;
        for (int i = 0; i < n; i++) {
            int tw = depths[i] * 18 + 18
                     + tk_text_width(t, w->nodes[ids[i]].label)
                     + 2 * t->unit;
            if (tw > widest) widest = tw;
        }
        free(ids); free(depths);
        int h = n * t->row_h;
        if (h < t->row_h * 3) h = t->row_h * 3;
        return (tk_size){ widest, h };
    }
    default:
        return (tk_size){ 0, 0 };
    }
}

/* ---------------- M2 paint ---------------- */

void tk_w2_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    tk_rect r = w->rect;
    tk_color fg = w->enabled ? t->text : t->text_dim;
    int lh = tk_line_height(t);
    switch (w->kind) {
    case TK_W_SWITCH: {
        if (t->style == 2) {
            /* WinUI ToggleSwitch: 40x20epx pill at the device
             * scale (54x27 since M22: x1.344, AUDIT SS20),
             * outline when off, accent when on, the knob
             * riding knob_pos like the M3 form */
            int cy = r.y + r.h / 2;
            tk_rect tr = { r.x, cy - 13, 54, 27 };
            /* M9: the disabled form drops the accent entirely.
             * M10 roles (ToggleSwitch themeresources + the
             * Fluent token table): the off-state outline is
             * ControlStrongStrokeColorDefault, the knob is
             * ControlStrongFillColorDefault, the on-state knob
             * is the on-accent colour; disabled uses the
             * disabled strong pair and ControlFillColor-
             * Disabled for a checked track. (Pre-M10 the
             * outline/knob used text_dim, which coincidentally
             * matches the strong stroke in light only.) */
            if (w->checked && w->enabled) {
                tk_canvas_fill_round_rect(c, tr, 13, t->accent);
            } else if (w->checked) {
                if (t->control_fill_dis.a)
                    tk_canvas_fill_round_rect(c, tr, 13,
                                              t->control_fill_dis);
                tk_canvas_stroke_round_rect(c, tr, 13, 1,
                    t->strong_stroke_dis.a ? t->strong_stroke_dis
                                           : t->border);
            } else {
                if (t->control_fill.a)
                    tk_canvas_fill_round_rect(c, tr, 13,
                                              t->control_fill);
                tk_canvas_stroke_round_rect(c, tr, 13, 1,
                    w->enabled
                        ? (t->strong_stroke.a ? t->strong_stroke
                                              : t->text_dim)
                        : (t->strong_stroke_dis.a
                               ? t->strong_stroke_dis : t->border));
            }
            /* M8: the WinUI knob is 12epx centred in a 20epx
             * knob grid. M22: at x1.344 the knob is 16px and
             * its left edge travels 6 -> 32 over the 54px
             * track (6px insets at both ends). */
            int kx = r.x + 6 + (int)(w->knob_pos * 26.0f + 0.5f);
            tk_canvas_fill_round_rect(c,
                (tk_rect){ kx, cy - 8, 16, 16 }, 8,
                !w->enabled
                    ? (t->strong_fill_dis.a ? t->strong_fill_dis
                                            : t->text_dim)
                    : w->checked ? t->on_accent
                                 : (t->strong_fill.a
                                        ? t->strong_fill
                                        : t->text_dim));
            /* M21: label gap 17px (12epx column spacing),
             * matching the measure */
            tk_canvas_draw_text(c, t, r.x + 54 + 17,
                                r.y + (r.h - lh) / 2, w->text, fg);
            break;
        }
        /* UI-REDESIGN 2.2: track 52x32, knob diameter 24. State is
         * carried by position AND colour (never colour alone):
         * ON = accent track + knob right, OFF = surface_alt track
         * + knob left. The OFF knob gets a border-coloured backing
         * disc so it stays identifiable on the light track.
         * M21: the M3 form takes the converted token sizes -
         * track 61x37px, knob 28px (the pressed size below);
         * the classic theme keeps the 2.2 sizes. M23: at the
         * corrected ~179.2ppi premise the 52x32dp track
         * converts to 58x36px, so 61x37 is the retained
         * value of AUDIT (a-4).
         * M22: the M3 knob diameter is state-variable, as in
         * the reference (material3 Switch, extracted in M21
         * SS5.3: 16dp unselected, 24dp selected / with icon,
         * 28dp pressed; this toolkit's switch has no icon
         * form). At the corrected 1.12px/dp (AUDIT SS20):
         * 18 / 27 / 31px. The unpressed diameter rides
         * knob_pos between the off and on sizes, so it eases
         * with the slide; the pressed size applies while the
         * pointer is held (tracked in tk_w2_event). The
         * travel formula below keeps the 4px end insets for
         * any diameter. Classic keeps the fixed 24px. */
        int stw = t->style == 1 ? 61 : 52;
        int sth = t->style == 1 ? 37 : 32;
        int skd = 24;
        if (t->style == 1)
            skd = w->pressed ? 31
                  : 18 + (int)(9.0f * w->knob_pos + 0.5f);
        int cy = r.y + r.h / 2;
        /* M9: the disabled form fades per the M3 disabled
         * roles - the track drops to surfaceContainerHighest
         * and the knob to onSurface at ~38% (the disabled
         * content alpha), in both switch positions */
        tk_canvas_fill_round_rect(c, (tk_rect){r.x, cy - sth / 2, stw, sth},
                                  sth / 2,
                                  !w->enabled ? t->container_highest
                                              : w->checked ? t->accent
                                                           : t->surface_alt);
        /* M5: the knob rides knob_pos (animated); the OFF backing
         * disc follows the knob while it is on the left half. */
        int kx = r.x + 4 + (int)(w->knob_pos * (stw - 8 - skd) + 0.5f);
        if (w->knob_pos < 0.5f)
            tk_canvas_fill_round_rect(c,
                (tk_rect){kx - 1, cy - skd / 2 - 1, skd + 2, skd + 2},
                (skd + 2) / 2, t->border);
        tk_color knob = t->surface;
        if (!w->enabled) { knob = t->text; knob.a = 97; }
        tk_canvas_fill_round_rect(c, (tk_rect){kx, cy - skd / 2, skd, skd},
                                  skd / 2, knob);
        tk_canvas_draw_text(c, t, r.x + stw + t->unit,
                            r.y + (r.h - lh) / 2, w->text, fg);
        break;
    }
    case TK_W_DATEPICKER:
    case TK_W_TIMEPICKER: {
        /* M5 segmented control: one segment per field; the active
         * segment carries the selection surface; the stepper zone
         * at the right edge edits it */
        int rad = t->radius_button >= 0 ? t->radius_button : t->radius;
        int n = tk_pk_nfields(w);
        for (int f = 0; f < n; f++) {
            tk_rect cell;
            tk_pk_cell_rect(w, f, &cell);
            tk_canvas_fill_round_rect(c, cell, rad,
                f == w->pk_field ? t->selection : t->surface_alt);
            char buf[8];
            int v = tk_pk_field_value(w, f);
            if (w->kind == TK_W_DATEPICKER && f == 0)
                snprintf(buf, sizeof buf, "%04d", v);
            else
                snprintf(buf, sizeof buf, "%02d", v);
            int tw = tk_text_width(t, buf);
            tk_canvas_draw_text(c, t, cell.x + (cell.w - tw) / 2,
                                cell.y + (cell.h - lh) / 2, buf, fg);
        }
        tk_rect st = tk_pk_stepper_rect(w);
        tk_pk_draw_tri(c, st.x + st.w / 2, st.y + st.h / 4, -1, fg);
        tk_pk_draw_tri(c, st.x + st.w / 2, st.y + 3 * st.h / 4, 1, fg);
        break;
    }
    case TK_W_PROGRESS: {
        /* int10 (independent review B): paint the bar at its
         * spec height (the measure contract: 5px M3 / 4px
         * WinUI / 20px classic), centred in the arranged
         * rect. Filling a taller row rect painted a solid
         * slab - the recorder level meter in a 54px WinUI
         * row read as a 330px dark block (C5). */
        {
            int spec_h = t->style == 1 ? 5
                         : t->style == 2 ? 4 : 20;
            if (r.h > spec_h) {
                r.y += (r.h - spec_h) / 2;
                r.h = spec_h;
            }
        }
        int rad = r.h / 2 > 6 ? 6 : r.h / 2;
        /* M10 track roles: M3 = surfaceContainerHighest;
         * WinUI = ControlStrongStrokeColorDefault (the
         * ProgressBar themeresources Background brush - its
         * value was unobtainable in M9 and is now sourced
         * from the Fluent token table). Classic keeps
         * surface_alt. */
        tk_color trk = t->surface_alt;
        if (t->style == 1 && t->container_highest.a)
            trk = t->container_highest;
        else if (t->style == 2 && t->strong_stroke.a)
            trk = t->strong_stroke;
        tk_canvas_fill_round_rect(c, r, rad, trk);
        if (w->prog_indet) {
            if (!w->prog_anim) prog_sweep_start(w);
            if (t->style == 1) {
                /* M13: the M3 form - the two keyframed bars
                 * (see the KF_ tables). The phase is the
                 * 2000 ms cycle position. Both bars take the
                 * active-indicator colour, so their overlap is
                 * invisible; the secondary is painted last, as
                 * in the reference DOM order. */
                float p = w->prog_phase / 1000.0f;
                const tk_kf *trs[2] = { KF_PRI_TR, KF_SEC_TR };
                const tk_kf *scs[2] = { KF_PRI_SC, KF_SEC_SC };
                const float base[2] = { -145.167f, -54.8889f };
                for (int b = 0; b < 2; b++) {
                    float lp = base[b] + kf_eval(trs[b], 4, p);
                    float wp = kf_eval(scs[b], 4, p) * 100.0f;
                    int sx = r.x + (int)(lp / 100.0f * (float)r.w +
                        (lp >= 0 ? 0.5f : -0.5f));
                    int sw = (int)(wp / 100.0f * (float)r.w + 0.5f);
                    tk_rect s = { sx, r.y, sw, r.h };
                    tk_rect vis = tk_rect_intersect(s, r);
                    if (vis.w > 0)
                        tk_canvas_fill_round_rect(c, vis, rad,
                                                  t->accent);
                }
            } else {
                /* M6 indeterminate: a 40%-wide segment sweeps
                 * the track (phase driven by the sweep
                 * animation; a detached widget paints it
                 * statically) */
                int seg = r.w * 2 / 5;
                if (seg < 8) seg = 8;
                int travel = r.w + seg;
                int sx = r.x - seg +
                         (int)((w->prog_phase / 1000.0f) * (float)travel);
                tk_rect s = { sx, r.y, seg, r.h };
                tk_rect vis = tk_rect_intersect(s, r);
                if (vis.w > 0)
                    tk_canvas_fill_round_rect(c, vis, rad, t->accent);
            }
        } else {
            /* M13: the painted fraction is prog_disp - the
             * logical value at rest; in the M3 mode a
             * programmatic set transitions it (250 ms) */
            int fw = (int)(w->prog_disp * (float)r.w);
            if (fw > 0)
                tk_canvas_fill_round_rect(c, (tk_rect){r.x, r.y, fw, r.h},
                                          rad, t->accent);
        }
        break;
    }
    case TK_W_SPINNER: {
        /* M6: circular indeterminate - a 270-degree arc of dots
         * rotating with the sweep phase */
        if (!w->prog_anim) prog_sweep_start(w);
        int s = r.w < r.h ? r.w : r.h;
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        int rad = s / 2 - 3;
        if (rad >= 4) {
            float start = (w->prog_phase / 1000.0f) * 6.2831853f;
            for (int a = 0; a <= 270; a += 9) {
                float ang = start + a * 0.0174533f;
                int px = cx + (int)(rad * cosf(ang));
                int py = cy + (int)(rad * sinf(ang));
                tk_canvas_fill_round_rect(c,
                    (tk_rect){ px - 2, py - 2, 4, 4 }, 2, t->accent);
            }
        }
        break;
    }
    case TK_W_BADGE: {
        if (w->badge_count == 0) break;
        tk_color bgc = { 186, 26, 26, 255 }; /* M3 error role */
        if (w->badge_count < 0) {
            int d = r.w < r.h ? r.w : r.h;
            tk_canvas_fill_round_rect(c,
                (tk_rect){ r.x + (r.w - d) / 2, r.y + (r.h - d) / 2,
                           d, d }, d / 2, bgc);
        } else {
            char nb[8];
            if (w->badge_count > 99) snprintf(nb, sizeof nb, "99+");
            else snprintf(nb, sizeof nb, "%d", w->badge_count);
            tk_canvas_fill_round_rect(c, r, r.h / 2, bgc);
            int tw = tk_text_width_st(t, TK_TEXT_CAPTION, nb);
            int lh2 = tk_line_height_st(t, TK_TEXT_CAPTION);
            tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                r.x + (r.w - tw) / 2, r.y + (r.h - lh2) / 2, nb,
                (tk_color){ 255, 255, 255, 255 });
        }
        break;
    }
    case TK_W_CHIP: {
        /* M6: M3 chip - 8dp-radius container, outline at rest;
         * a selected filter chip fills with the tonal container
         * and shows a check mark in the leading slot. WinUI has
         * no chip: the mode renders the ToggleButton form
         * (control fill/stroke, accent when checked). */
        bool filled = w->chip_kind == TK_CHIP_FILTER && w->chip_selected;
        tk_color cfg = fg;
        /* M10 disabled forms, M11-corrected for WinUI: the
         * ToggleButton form's UNSELECTED disabled pair is
         * ControlFillColorDisabled + TextFillColorDisabled,
         * the SELECTED disabled pair is the accent pair (see
         * the style-2 branch below); M3 selected = on-surface
         * 12% container with 38% content, M3 unselected = the
         * 12% outline with 38% content. */
        if (t->style == 2) {
            /* M11: the chip's WinUI form models the
             * ToggleButton, so its state pair follows the
             * ToggleButton themeresources (fetched 2026-11-20):
             * unchecked pressed = ControlFillColorTertiary,
             * checked pressed = AccentFillColorTertiary,
             * checked disabled = AccentFillColorDisabled with
             * TextOnAccentFillColorDisabled content. M10 had
             * no pressed form and used the unchecked disabled
             * pair for the checked state as well. */
            if (!w->enabled) {
                if (filled) {
                    tk_canvas_fill_round_rect(c, r, t->radius,
                        tk_face_brush(w, t,
                            t->accent_dis.a ? t->accent_dis
                                : (t->control_fill_dis.a
                                       ? t->control_fill_dis
                                       : t->surface_alt)));
                    cfg = t->on_accent_dis.a ? t->on_accent_dis
                                             : btn_disabled_fg(t);
                } else {
                    tk_canvas_fill_round_rect(c, r, t->radius,
                        t->control_fill_dis.a ? t->control_fill_dis
                                              : t->surface_alt);
                    tk_canvas_stroke_round_rect(c, r, t->radius, 1,
                        t->strong_stroke_dis.a
                            ? t->strong_stroke_dis : t->border);
                    cfg = btn_disabled_fg(t);
                }
            } else if (filled) {
                /* M17: the ToggleButton form's background runs
                 * the 83 ms BrushTransition */
                if (w->pressed) {
                    tk_color pf = t->accent; pf.a = 204;
                    tk_canvas_fill_round_rect(c, r, t->radius,
                        tk_face_brush(w, t, pf));
                } else {
                    tk_canvas_fill_round_rect(c, r, t->radius,
                        tk_face_brush(w, t, t->accent));
                }
                cfg = t->on_accent;
            } else {
                tk_canvas_fill_round_rect(c, r, t->radius,
                    tk_face_brush(w, t,
                        w->pressed
                            ? (t->control_fill3.a
                                   ? t->control_fill3
                                   : t->surface_alt)
                            : (t->control_fill.a ? t->control_fill
                                                 : t->surface)));
                tk_canvas_stroke_round_rect(c, r, t->radius, 1,
                    t->control_stroke.a ? t->control_stroke
                                        : t->border);
            }
        } else if (t->style == 1 && !w->enabled) {
            tk_color df = t->text; df.a = 31;
            if (filled)
                tk_canvas_fill_round_rect(c, r, 9, df);
            else
                tk_canvas_stroke_round_rect(c, r, 9, 1, df);
            cfg = btn_disabled_fg(t);
        } else if (filled)
            tk_canvas_fill_round_rect(c, r, 9, t->container);
        else {
            /* M17: the M3 form no longer washes the pressed
             * chip with surface_alt - the state layer below
             * is the pressed visual (the wash remains the
             * classic theme's form) */
            if (w->pressed && t->style != 1)
                tk_canvas_fill_round_rect(c, r, 9, t->surface_alt);
            tk_canvas_stroke_round_rect(c, r, 9, 1, t->border);
        }
        if (t->style == 1 && w->enabled) {
            /* M17: the M3 pressed visual is the ripple disc in
             * the content colour - the disc IS the state layer
             * (chips have no uniform layer under it); with no
             * ripple running (a press the ripple did not see)
             * the uniform layer stands in, as on buttons */
            tk_color rl = cfg; rl.a = 26;
            if (w->ripple_on)
                tk_ripple_circle(w, c, r, 9, rl);
            else if (w->pressed)
                tk_canvas_fill_round_rect(c, r, 9, rl);
        }
        int x = r.x + 2 * t->unit;
        if (w->image_px) {
            blit_image(c, w->image_px, w->image_w, w->image_h,
                       (tk_rect){ x, r.y + (r.h - w->image_h) / 2,
                                  w->image_w, w->image_h });
            x += w->image_w + t->unit;
        } else if (filled || w->glyph_pos > 0.0f) {
            /* check mark: two strokes in the accent colour
             * (the on-accent colour in the WinUI toggle form;
             * M10: the disabled content colour when disabled).
             * M17: while the selection glyph transition runs
             * the check fades with it and its slot expands /
             * shrinks (the M3 chip's AnimatedVisibility); at
             * rest (glyph_pos 1 selected, 0 unselected) the
             * paint is exactly the pre-M17 form. */
            float gp = filled ? (w->glyph_pos > 0.0f
                                     ? w->glyph_pos : 1.0f)
                              : w->glyph_pos;
            int slot = (int)(gp * (float)(18 + t->unit) + 0.5f);
            tk_color chk = t->style == 2 ? cfg
                           : (!w->enabled && t->style == 1)
                                 ? cfg : t->accent;
            chk.a = (uint8_t)((float)chk.a * gp);
            bool clipped = gp < 0.999f;
            if (clipped)
                tk_canvas_push_clip(c, (tk_rect){ x, r.y,
                                                  slot, r.h });
            int mx = x + 2, my = r.y + r.h / 2;
            for (int i = 0; i < 5; i++)
                tk_canvas_fill_rect(c, (tk_rect){ mx + i,
                    my + (i < 2 ? i : 4 - i) - 1, 2, 2 }, chk);
            for (int i = 0; i < 8; i++)
                tk_canvas_fill_rect(c, (tk_rect){ mx + 4 + i,
                    my + 2 - (i * 2) / 3 - 1, 2, 2 }, chk);
            if (clipped) tk_canvas_pop_clip(c);
            x += slot;
        }
        tk_canvas_push_clip(c, (tk_rect){ x, r.y,
            r.w - (x - r.x) - (w->chip_kind == TK_CHIP_INPUT ? 28
                                                             : 2 * t->unit),
            r.h });
        tk_w_draw_text(c, w, t, x, r.y + (r.h - tk_w_text_lh(w, t)) / 2,
                       w->text, cfg);
        tk_canvas_pop_clip(c);
        if (w->chip_kind == TK_CHIP_INPUT) {
            /* trailing close affordance: an x glyph */
            int zx = r.x + r.w - 24, zy = r.y + r.h / 2;
            tk_canvas_draw_text(c, t, zx, zy - tk_line_height(t) / 2,
                                "\xC3\x97", t->text_dim); /* U+00D7 */
        }
        break;
    }
    case TK_W_TABBAR: {
        /* M6: M3 primary tabs - equal segments, optional icon
         * over the label, badge at the icon's corner, and the
         * 3px fully-rounded active indicator under the selected
         * segment (inset 2px, min length 24px at spec scale).
         * WinUI mode: the TabView/Pivot form - the strip sits
         * on the layer colour and the active tab is raised to
         * the surface colour over the same accent underline. */
        tk_canvas_fill_rect(c, r, t->style == 2
                                      ? (t->layer.a ? t->layer
                                                    : t->surface_alt)
                                      : t->surface);
        if (w->n_items) {
            int seg_w = r.w / (int)w->n_items;
            bool any_icon = false;
            for (size_t i = 0; i < w->n_items; i++)
                if (i < w->n_item_icons && w->item_icons[i])
                    any_icon = true;
            for (size_t i = 0; i < w->n_items; i++) {
                int sx = r.x + seg_w * (int)i;
                int sw = (i + 1 == w->n_items) ? r.x + r.w - sx : seg_w;
                bool active = (int)i == w->sel;
                if (active && t->style == 2)
                    tk_canvas_fill_rect(c,
                                        (tk_rect){ sx, r.y, sw, r.h },
                                        t->surface);
                tk_color col = active ? t->accent : t->text_dim;
                int lh2 = tk_line_height(t);
                int content_h = lh2 + (any_icon ? w->item_icon_h + 2 : 0);
                int ty = r.y + (r.h - content_h) / 2;
                if (any_icon && i < w->n_item_icons && w->item_icons[i]) {
                    int ix = sx + (sw - w->item_icon_w) / 2;
                    blit_image(c, w->item_icons[i], w->item_icon_w,
                               w->item_icon_h,
                               (tk_rect){ ix, ty, w->item_icon_w,
                                          w->item_icon_h });
                    if (w->tab_badges && w->tab_badges[i] != 0) {
                        char nb[8];
                        int bc = w->tab_badges[i];
                        if (bc > 99) snprintf(nb, sizeof nb, "99+");
                        else if (bc > 0) snprintf(nb, sizeof nb, "%d", bc);
                        else nb[0] = '\0';
                        tk_color bgc = { 186, 26, 26, 255 };
                        if (bc < 0) {
                            tk_canvas_fill_round_rect(c,
                                (tk_rect){ ix + w->item_icon_w - 4,
                                           ty - 2, 8, 8 }, 4, bgc);
                        } else {
                            int bw = tk_text_width_st(t,
                                         TK_TEXT_CAPTION, nb) + 10;
                            tk_canvas_fill_round_rect(c,
                                (tk_rect){ ix + w->item_icon_w - 6,
                                           ty - 4, bw, 16 }, 8, bgc);
                            tk_canvas_draw_text_st(c, t,
                                TK_TEXT_CAPTION,
                                ix + w->item_icon_w - 6 + 5, ty - 4,
                                nb, (tk_color){255, 255, 255, 255});
                        }
                    }
                    ty += w->item_icon_h + 2;
                }
                int tw = tk_text_width(t, w->items[i]);
                tk_canvas_draw_text_px(c, t, t->font_px, active,
                                       sx + (sw - tw) / 2, ty,
                                       w->items[i],
                                       active ? t->text : t->text_dim);
                (void)col;
                if (active) {
                    int iw2 = sw - 16;
                    if (iw2 < 28) iw2 = 28;
                    if (iw2 > sw) iw2 = sw;
                    /* M13: while the indicator slide runs, its
                     * left edge is the animated tab_ind_x */
                    int ix = w->tab_ind_x >= 0
                        ? r.x + w->tab_ind_x
                        : sx + (sw - iw2) / 2;
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ ix, r.y + r.h - 3, iw2, 3 }, 1,
                        t->accent);
                }
            }
        }
        tk_canvas_fill_rect(c, (tk_rect){r.x, r.y + r.h - 1, r.w, 1},
                            t->border_line);
        break;
    }
    case TK_W_SEPARATOR:
        /* separators are not control-identifying: they use the
         * weak border_line token, not border (UI-REDESIGN 2.2) */
        if (w->vertical)
            tk_canvas_fill_rect(c, (tk_rect){r.x + r.w / 2 - 1, r.y, 2, r.h},
                                t->border_line);
        else
            tk_canvas_fill_rect(c, (tk_rect){r.x, r.y + r.h / 2 - 1, r.w, 2},
                                t->border_line);
        break;
    case TK_W_IMAGEVIEW: {
        if (!w->image_px) {
            /* Generic fallback (UI-REDESIGN P1-5): ONE small
             * picture pictogram centred in the rect - never a
             * full-surface placeholder plate. Drawn with plain
             * rects: a frame, a sun disc and a mountain ridge. */
            int s = r.w < r.h ? r.w : r.h;
            if (s > 48) s = 48;
            if (s >= 16) {
                int ix = r.x + (r.w - s) / 2, iy = r.y + (r.h - s) / 2;
                tk_canvas_stroke_rect(c, (tk_rect){ix, iy, s, s}, 2,
                                      t->border);
                int sun = s / 5;
                tk_canvas_fill_round_rect(c,
                    (tk_rect){ix + s / 6, iy + s / 6, sun, sun},
                    sun / 2, t->border);
                /* ridge: two ascending runs of 2px-high steps */
                int base = iy + s - s / 5;
                int mid = ix + s * 3 / 5;
                for (int x = ix + 3; x < mid; x++) {
                    int top = base - (x - ix) * (s / 3) / (s * 3 / 5);
                    tk_canvas_fill_rect(c, (tk_rect){x, top, 1, base - top},
                                        t->border);
                }
                for (int x = mid; x < ix + s - 3; x++) {
                    int top = base - (ix + s - x) * (s / 5) / (s * 2 / 5);
                    tk_canvas_fill_rect(c, (tk_rect){x, top, 1, base - top},
                                        t->border);
                }
            }
            break;
        }
        blit_image(c, w->image_px, w->image_w, w->image_h, r);
        break;
    }
    case TK_W_SPINBUTTON: {
        tk_canvas_fill_round_rect(c, r, t->radius, t->surface);
        tk_canvas_stroke_rect(c, r, 1, t->border);
        char buf[64];
        const char *show = buf;
        if (w->b2 && w->text) show = w->text; /* M3: editing text */
        else snprintf(buf, sizeof(buf), "%g", (double)w->fvalue);
        tk_canvas_draw_text(c, t, r.x + t->unit,
                            r.y + (r.h - lh) / 2, show, fg);
        if (w->b2 && w->text) { /* M3 caret */
            int len = (int)strlen(show);
            int pos = w->edit_pos;
            if (pos < 0) pos = 0;
            if (pos > len) pos = len;
            char *pre = malloc((size_t)pos + 1);
            if (pre) {
                memcpy(pre, show, (size_t)pos);
                pre[pos] = '\0';
                int cx = tk_text_width(t, pre);
                tk_canvas_fill_rect(c,
                    (tk_rect){r.x + t->unit + cx,
                              r.y + (r.h - lh) / 2, 2, lh}, t->focus);
                free(pre);
            }
        }
        int bw = t->control_h;
        tk_rect zone = { r.x + r.w - bw, r.y, bw, r.h };
        tk_canvas_fill_rect(c, zone, t->surface_alt);
        tk_canvas_draw_text(c, t, zone.x + bw / 2 - 4,
                            r.y + r.h / 4 - lh / 2, "+", fg);
        tk_canvas_draw_text(c, t, zone.x + bw / 2 - 4,
                            r.y + 3 * r.h / 4 - lh / 2, "-", fg);
        break;
    }
    case TK_W_COMBOBOX: {
        tk_canvas_fill_round_rect(c, r, t->radius, t->surface);
        tk_canvas_stroke_rect(c, r, 1, t->border);
        const char *show = tk_combobox_text(w);
        tk_canvas_push_clip(c, (tk_rect){r.x + 2, r.y, r.w - 4, r.h});
        tk_canvas_draw_text(c, t, r.x + t->unit,
                            r.y + (r.h - lh) / 2, show, fg);
        tk_canvas_pop_clip(c);
        if (w->b2 && w->b1 && w->text) { /* M3 caret while editing */
            int len = (int)strlen(show);
            int pos = w->edit_pos;
            if (pos < 0) pos = 0;
            if (pos > len) pos = len;
            char *pre = malloc((size_t)pos + 1);
            if (pre) {
                memcpy(pre, show, (size_t)pos);
                pre[pos] = '\0';
                int cx = tk_text_width(t, pre);
                tk_canvas_fill_rect(c,
                    (tk_rect){r.x + t->unit + cx,
                              r.y + (r.h - lh) / 2, 2, lh}, t->focus);
                free(pre);
            }
        }
        tk_canvas_draw_text(c, t, r.x + r.w - 20,
                            r.y + (r.h - lh) / 2, "v", fg);
        break;
    }
    case TK_W_MENUBAR: {
        tk_canvas_fill_rect(c, r, t->surface_alt);
        int cx = r.x + t->unit;
        for (size_t i = 0; i < w->n_bar; i++) {
            tk_canvas_draw_text(c, t, cx + 8,
                                r.y + (r.h - lh) / 2, w->bar_titles[i], fg);
            cx += tk_text_width(t, w->bar_titles[i]) + 16;
        }
        break;
    }
    case TK_W_MENU: {
        /* menus are floating surfaces: overlay radius (2.2).
         * WinUI MenuFlyout: card background + stroke, subtle
         * hover. */
        /* M17: while the menu fade runs (entrance / exit),
         * every colour the surface paints is scaled by
         * paint_alpha; at rest paint_alpha is 255 and the
         * paint is exactly the pre-M17 form. (Item icons are
         * blits and do not fade - the painter has no alpha
         * path for RGB blits.) */
        const tk_theme *t_saved = t;
        tk_theme mscaled;
        if (w->paint_alpha < 255) {
            mscaled = *t;
            mscaled.card_bg = m17_alpha(mscaled.card_bg, w->paint_alpha);
            mscaled.surface = m17_alpha(mscaled.surface, w->paint_alpha);
            mscaled.control_stroke = m17_alpha(mscaled.control_stroke, w->paint_alpha);
            mscaled.border = m17_alpha(mscaled.border, w->paint_alpha);
            mscaled.border_line = m17_alpha(mscaled.border_line, w->paint_alpha);
            mscaled.subtle = m17_alpha(mscaled.subtle, w->paint_alpha);
            mscaled.selection = m17_alpha(mscaled.selection, w->paint_alpha);
            mscaled.text = m17_alpha(mscaled.text, w->paint_alpha);
            mscaled.text_dis = m17_alpha(mscaled.text_dis, w->paint_alpha);
            mscaled.text_dim = m17_alpha(mscaled.text_dim, w->paint_alpha);
            mscaled.accent = m17_alpha(mscaled.accent, w->paint_alpha);
            t = &mscaled;
        }
        if (t->style == 2) {
            tk_canvas_fill_round_rect(c, r, t->radius_overlay,
                t->card_bg.a ? t->card_bg : t->surface);
            tk_canvas_stroke_round_rect(c, r, t->radius_overlay, 1,
                t->control_stroke.a ? t->control_stroke : t->border);
        } else if (t->style == 1) {
            /* M21: the M3 menu container corner is the
             * extraSmall shape (4dp = 5px), not the dialog
             * overlay radius - at 28px the menu read as a
             * rounded blob against the mode's shape scale,
             * and its square stroke disagreed with the
             * rounded fill at the corners */
            tk_canvas_fill_round_rect(c, r, 5, t->surface);
            tk_canvas_stroke_round_rect(c, r, 5, 1, t->border);
        } else {
            tk_canvas_fill_round_rect(c, r, t->radius_overlay, t->surface);
            tk_canvas_stroke_rect(c, r, 1, t->border);
        }
        bool any_check = false;
        int icon_slot = 0;
        for (size_t i = 0; i < w->n_menu_items; i++) {
            if (w->menu_items[i].checkable) any_check = true;
            if (w->menu_items[i].icon &&
                w->menu_items[i].icon_w + 12 > icon_slot)
                icon_slot = w->menu_items[i].icon_w + 12;
        }
        int cy = r.y;
        for (size_t i = 0; i < w->n_menu_items; i++) {
            tk_menu_item *it = &w->menu_items[i];
            int rh = menu_row_h(w, (int)i, t);
            if (it->separator) {
                tk_canvas_fill_rect(c, (tk_rect){r.x + 6, cy + rh / 2,
                                                 r.w - 12, 1},
                                    t->border_line);
            } else {
                if ((int)i == w->menu_hover && it->enabled)
                    tk_canvas_fill_rect(c, (tk_rect){r.x, cy, r.w, rh},
                                        t->style == 2
                                            ? (t->subtle.a
                                                   ? t->subtle
                                                   : t->selection)
                                            : t->selection);
                /* M10: WinUI disabled items take
                 * TextFillColorDisabled */
                tk_color col = it->enabled ? t->text
                    : (t->style == 2 && t->text_dis.a
                           ? t->text_dis : t->text_dim);
                if (it->checkable && it->checked) {
                    /* M21: the checked mark is a check glyph -
                     * the form both references use (M3: the
                     * Check icon in the leading slot, content
                     * colour; WinUI: the ToggleMenuFlyoutItem
                     * FontIcon E73E). The pre-M21 form filled a
                     * 10x10 accent square, which read as a blob,
                     * not as "checked". The glyph box centres on
                     * the same slot the square occupied.
                     * Colour (WinUI, from the template): at rest
                     * the glyph takes the secondary text brush
                     * (MenuFlyoutSubItemChevron); it switches to
                     * the item foreground on the hover/pressed
                     * row. M3 keeps the content colour. */
                    tk_color gcol = col;
                    if (t->style == 2 && it->enabled &&
                        (int)i != w->menu_hover)
                        gcol = t->text_dim;
                    paint_check_glyph(c,
                        (tk_rect){ r.x + 2, cy + rh / 2 - 8, 16, 16 },
                        1.0f, gcol);
                }
                if (it->icon)
                    blit_image(c, it->icon, it->icon_w, it->icon_h,
                               (tk_rect){ r.x + menu_inset(t) +
                                              (any_check ? 20 : 0),
                                          cy + (rh - it->icon_h) / 2,
                                          it->icon_w, it->icon_h });
                tk_canvas_draw_text(c, t,
                    r.x + menu_inset(t) + (any_check ? 20 : 0) + icon_slot,
                    cy + (rh - lh) / 2, it->label, col);
                /* M7: trailing slot - badge then text, right-
                 * aligned ahead of the submenu arrow */
                int txr = r.x + r.w - menu_inset(t) - (it->submenu ? 16 : 0);
                if (it->trailing) {
                    int tw2 = tk_text_width(t, it->trailing);
                    tk_canvas_draw_text(c, t, txr - tw2,
                                        cy + (rh - lh) / 2,
                                        it->trailing, t->text_dim);
                    txr -= tw2 + 12;
                }
                if (it->badge < 0) {
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ txr - 8, cy + rh / 2 - 4, 8, 8 },
                        4, m17_alpha((tk_color){ 186, 26, 26, 255 },
                                     w->paint_alpha));
                } else if (it->badge > 0) {
                    char nb[8];
                    snprintf(nb, sizeof(nb), "%d",
                             it->badge > 99 ? 99 : it->badge);
                    int tw2 = tk_text_width_st(t, TK_TEXT_CAPTION,
                                               nb);
                    int bw = tw2 + 10 < 18 ? 18 : tw2 + 10;
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ txr - bw, cy + rh / 2 - 9, bw,
                                   18 },
                        9, m17_alpha((tk_color){ 186, 26, 26, 255 },
                                     w->paint_alpha));
                    tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                        txr - bw + (bw - tw2) / 2,
                        cy + rh / 2 -
                            tk_line_height_st(t, TK_TEXT_CAPTION) / 2,
                        nb, m17_alpha((tk_color){ 255, 255, 255,
                                                  255 },
                                      w->paint_alpha));
                }
                if (it->submenu)
                    tk_canvas_draw_text(c, t, r.x + r.w - 16,
                                        cy + (rh - lh) / 2, ">", col);
            }
            cy += rh;
        }
        t = t_saved; /* M17: undo the fade's theme scaling */
        break;
    }
    case TK_W_OVERFLOWBAR: {
        if (tk_overflowbar_has_overflow(w)) {
            tk_rect z = ob_zone(w);
            tk_canvas_fill_round_rect(c, z, t->radius, t->surface_alt);
            int tw = tk_text_width(t, "»");
            tk_canvas_draw_text(c, t, z.x + (z.w - tw) / 2,
                                z.y + (z.h - lh) / 2, "»", fg);
        }
        break;
    }
    case TK_W_TABS: {
        tk_canvas_fill_rect(c, r, t->surface);
        int bar_h = tabs_bar_h(t);
        tk_canvas_fill_rect(c, (tk_rect){r.x, r.y, r.w, bar_h},
                            t->surface_alt);
        int cx = r.x;
        for (size_t i = 0; i < w->n_tab_titles; i++) {
            int sw = 0;
            int sx = tabs_seg_x(t, w, (int)i, &sw);
            bool active = (int)i == w->active_tab;
            if (active) {
                tk_canvas_fill_rect(c, (tk_rect){r.x + sx, r.y, sw, bar_h},
                                    t->surface);
                /* selected tab: 3px accent underline (2.2) */
                tk_canvas_fill_rect(c, (tk_rect){r.x + sx, r.y + bar_h - 3,
                                                 sw, 3}, t->accent);
            }
            /* active title is bold, inactive titles are dimmed:
             * the current page must read without relying on the
             * underline alone */
            tk_canvas_draw_text_px(c, t, t->font_px, active,
                                   r.x + sx + 12,
                                   r.y + (bar_h - lh) / 2,
                                   w->tab_titles[i],
                                   active ? fg : t->text_dim);
            cx = sx + sw;
            (void)cx;
        }
        tk_canvas_fill_rect(c, (tk_rect){r.x, r.y + bar_h - 1, r.w, 1},
                            t->border_line);
        break;
    }
    case TK_W_DIALOG: {
        if (t->style == 2) {
            /* WinUI ContentDialog: solid background fill
             * (the theme's surface carries the dialog's solid
             * background colour), overlay radius, border */
            tk_canvas_fill_round_rect(c, r, t->radius_overlay,
                                      t->surface);
            tk_canvas_stroke_round_rect(c, r, t->radius_overlay, 1,
                t->control_stroke.a ? t->control_stroke : t->border);
        } else if (w->dlg_fullscreen) {
            /* M7: the M3 full-screen dialog is square-cornered
             * and fills the window (header 56dp) */
            tk_canvas_fill_rect(c, r, t->surface);
        } else {
            /* M3 basic dialog: 28dp corners, container-high */
            tk_canvas_fill_round_rect(c, r,
                t->style == 1 ? 33 : t->radius_overlay,
                t->style == 1
                    ? (t->container_high.a ? t->container_high
                                           : t->surface)
                    : t->surface);
            tk_canvas_stroke_rect(c, r, 1, t->border);
        }
        if (w->text)
            tk_canvas_draw_text_st(c, t, TK_TEXT_TITLE, r.x + w->padding,
                                   r.y + (tk_dialog_title_h(w, t)
                                          - tk_line_height_st(t, TK_TEXT_TITLE)) / 2,
                                   w->text, fg);
        tk_canvas_fill_rect(c,
            (tk_rect){r.x, r.y + tk_dialog_title_h(w, t), r.w, 1}, t->border_line);
        break;
    }
    case TK_W_TEXTVIEW: {
        tk_canvas_fill_round_rect(c, r, t->radius, t->surface);
        tk_canvas_stroke_rect(c, r, 1, t->border);
        tv_lines L;
        tv_build(w, tv_wrap_w(w), &L);
        int rows = r.h / lh;
        if (rows < 1) rows = 1;
        size_t lo = 0, hi = 0;
        bool has_sel = tc_has_selection(w->buffer);
        if (has_sel) tc_selection_range(w->buffer, &lo, &hi);
        tk_canvas_push_clip(c, (tk_rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2});
        for (int vi = w->top_index; vi < (int)L.n && vi < w->top_index + rows; vi++) {
            tv_line *ln = &L.lines[vi];
            int y = r.y + 4 + (vi - w->top_index) * lh;
            size_t s = ln->start, e = s + ln->len;
            if (has_sel && lo < e && hi > s) {
                size_t a = lo > s ? lo : s;
                size_t b = hi < e ? hi : e;
                char *pre = malloc(a - s + 1);
                char *mid = malloc(b - s + 1);
                if (pre && mid) {
                    memcpy(pre, L.text + s, a - s); pre[a - s] = '\0';
                    memcpy(mid, L.text + s, b - s); mid[b - s] = '\0';
                    int sx = tk_text_width(t, pre);
                    int ex = tk_text_width(t, mid);
                    tk_canvas_fill_rect(c,
                        (tk_rect){r.x + t->unit + sx, y, ex - sx, lh},
                        t->selection);
                }
                free(pre); free(mid);
            }
            char *sub = malloc(ln->len + 1);
            if (sub) {
                memcpy(sub, L.text + s, ln->len);
                sub[ln->len] = '\0';
                tk_canvas_draw_text(c, t, r.x + t->unit, y, sub, fg);
                free(sub);
            }
        }
        /* caret + composition, only while focused */
        if (w->b1) {
            int cx_px = 0;
            int cli = tv_cursor_line(w, &L, tc_cursor(w->buffer), &cx_px);
            if (cli >= w->top_index && cli < w->top_index + rows) {
                int y = r.y + 4 + (cli - w->top_index) * lh;
                tk_canvas_fill_rect(c,
                    (tk_rect){r.x + t->unit + cx_px, y, 2, lh}, t->focus);
                char comp[512];
                if (tc_composition(w->buffer, comp, sizeof(comp)) > 0)
                    tk_draw_composition(c, t, w->buffer,
                                     r.x + t->unit + cx_px + 2, y, fg,
                                     false);
            }
        }
        tk_canvas_pop_clip(c);
        /* scrollbar */
        if ((int)L.n > rows && r.h > 0) {
            int th = r.h * rows / (int)L.n;
            if (th < 16) th = 16;
            int max_top = (int)L.n - rows;
            int ty = r.y + (max_top > 0
                            ? w->top_index * (r.h - th) / max_top : 0);
            tk_canvas_fill_round_rect(c, (tk_rect){r.x + r.w - 6, ty, 4, th},
                                      2, t->border);
        }
        tv_lines_free(&L);
        break;
    }
    case TK_W_TREEVIEW: {
        tk_canvas_fill_rect(c, r, t->surface);
        tk_canvas_stroke_rect(c, r, 1, t->border);
        int *ids, *depths;
        int n = tree_flatten(w, &ids, &depths);
        int rows = r.h / t->row_h;
        tk_canvas_push_clip(c, r);
        for (int i = 0; i < rows; i++) {
            int row = w->top_index + i;
            if (row >= n) break;
            int id = ids[row];
            tk_rect rr = { r.x, r.y + i * t->row_h, r.w, t->row_h };
            if (id == w->sel_id) {
                tk_canvas_fill_rect(c, rr, t->selection);
                /* dual selection cue, as in ListView (2.2) */
                tk_canvas_fill_rect(c, (tk_rect){ r.x, rr.y, 3, t->row_h },
                                    t->accent);
            }
            int ix = r.x + t->unit + depths[row] * 18;
            if (tree_has_children(w, id))
                tk_canvas_draw_text(c, t, ix,
                                    rr.y + (t->row_h - lh) / 2,
                                    w->nodes[id].expanded ? "-" : "+", fg);
            tk_canvas_draw_text(c, t, ix + 18,
                                rr.y + (t->row_h - lh) / 2,
                                w->nodes[id].label, fg);
        }
        tk_canvas_pop_clip(c);
        free(ids); free(depths);
        break;
    }
    default:
        break;
    }
}

/* ---------------- M2 events ---------------- */

bool tk_w2_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    const tk_theme *t = metrics();
    if (!w->enabled) return false;
    switch (w->kind) {
    case TK_W_OVERFLOWBAR:
        if (ev->type == TK_EV_POINTER_UP &&
            tk_overflowbar_has_overflow(w) &&
            tk_rect_contains(ob_zone(w), ev->x, ev->y)) {
            ob_popup(w, root);
            return true;
        }
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y))
            return true; /* the bar's own surface consumes */
        return false;
    case TK_W_SWITCH:
        /* M22: the press is tracked so the M3 knob can take
         * its pressed diameter (28dp) while held; the toggle
         * still fires on UP, as before. */
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            w->pressed = true;
            tk_widget_invalidate(w);
            return true;
        }
        if (ev->type == TK_EV_POINTER_UP) {
            bool was = w->pressed; w->pressed = false;
            if (was) tk_widget_invalidate(w);
            if (tk_rect_contains(w->rect, ev->x, ev->y)) {
                tk_switch_set_on(w, !w->checked);
                return true;
            }
            return was;
        }
        if (ev->type == TK_EV_KEY_DOWN && ev->key == TK_KEY_SPACE) {
            tk_switch_set_on(w, !w->checked);
            return true;
        }
        return false;
    case TK_W_CHIP:
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            w->pressed = true;
            tk_ripple_press(w, root, ev->x, ev->y); /* M17 */
            return true;
        }
        if (ev->type == TK_EV_POINTER_UP) {
            bool was = w->pressed; w->pressed = false;
            tk_ripple_release(w, root); /* M17 */
            if (!was || !tk_rect_contains(w->rect, ev->x, ev->y))
                return was;
            if (root && root->suppress_click) return true;
            /* the trailing 28px is the input chip's close zone */
            if (w->chip_kind == TK_CHIP_INPUT &&
                ev->x >= w->rect.x + w->rect.w - 28) {
                if (w->cb_close) w->cb_close(w, w->cb_close_ud);
                return true;
            }
            if (w->chip_kind == TK_CHIP_FILTER)
                tk_chip_set_selected(w, !w->chip_selected);
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN &&
            (ev->key == TK_KEY_ENTER || ev->key == TK_KEY_SPACE)) {
            if (w->chip_kind == TK_CHIP_FILTER)
                tk_chip_set_selected(w, !w->chip_selected);
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        return false;
    case TK_W_TABBAR:
        if (ev->type == TK_EV_POINTER_UP &&
            tk_rect_contains(w->rect, ev->x, ev->y) && w->n_items) {
            if (root && root->suppress_click) return true;
            int idx = (ev->x - w->rect.x) * (int)w->n_items / w->rect.w;
            if (idx < 0) idx = 0;
            if (idx >= (int)w->n_items) idx = (int)w->n_items - 1;
            tk_tabbar_select(w, idx);
            return true;
        }
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y))
            return true;
        if (ev->type == TK_EV_KEY_DOWN) {
            if (ev->key == TK_KEY_LEFT) {
                tk_tabbar_select(w, w->sel > 0 ? w->sel - 1 : 0);
                return true;
            }
            if (ev->key == TK_KEY_RIGHT) {
                int last = (int)w->n_items - 1;
                tk_tabbar_select(w, w->sel < last ? w->sel + 1 : last);
                return true;
            }
        }
        return false;
    case TK_W_DATEPICKER:
    case TK_W_TIMEPICKER: {
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            tk_rect st = tk_pk_stepper_rect(w);
            if (tk_rect_contains(st, ev->x, ev->y)) {
                tk_pk_step(w, ev->y < w->rect.y + w->rect.h / 2 ? 1 : -1);
                return true;
            }
            int n = tk_pk_nfields(w);
            for (int f = 0; f < n; f++) {
                tk_rect cell;
                tk_pk_cell_rect(w, f, &cell);
                if (tk_rect_contains(cell, ev->x, ev->y)) {
                    if (w->pk_field != f) {
                        w->pk_field = f;
                        tk_widget_invalidate(w);
                    }
                    return true;
                }
            }
            return true;
        }
        if (ev->type == TK_EV_WHEEL) {
            tk_pk_step(w, ev->wheel_dy < 0 ? 1 : -1);
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            int n = tk_pk_nfields(w);
            switch (ev->key) {
            case TK_KEY_LEFT:
                w->pk_field = (w->pk_field + n - 1) % n;
                tk_widget_invalidate(w);
                return true;
            case TK_KEY_RIGHT:
                w->pk_field = (w->pk_field + 1) % n;
                tk_widget_invalidate(w);
                return true;
            case TK_KEY_UP:   tk_pk_step(w, 1);  return true;
            case TK_KEY_DOWN: tk_pk_step(w, -1); return true;
            default: break;
            }
        }
        return false;
    }
    case TK_W_SPINBUTTON: {
        if (ev->type == TK_EV_FOCUS_IN) {
            spin_edit_begin(w); /* M3: value becomes editable text */
            return false;
        }
        if (ev->type == TK_EV_FOCUS_OUT) {
            spin_edit_end(w, true); /* focus loss commits */
            return false;
        }
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            int bw = t->control_h;
            if (ev->x >= w->rect.x + w->rect.w - bw) {
                spin_step(w, ev->y < w->rect.y + w->rect.h / 2 ? 1 : -1);
                return true;
            }
            if (w->b2 && w->text) /* text zone: place the caret */
                w->edit_pos = (int)tk_offset_at_x(t, w->text,
                                                  ev->x - w->rect.x
                                                  - t->unit);
            return true;
        }
        if (ev->type == TK_EV_WHEEL) {
            spin_step(w, ev->wheel_dy < 0 ? 1 : -1);
            return true;
        }
        if (ev->type == TK_EV_TEXT_INPUT && ev->text) {
            if (!w->b2) {
                /* typing while not editing starts a FRESH entry
                 * (type-to-replace), it does not append to the
                 * displayed value */
                spin_edit_begin(w);
                if (w->text) { w->text[0] = '\0'; w->edit_pos = 0; }
            }
            for (const char *p = ev->text; *p; p++)
                if (strchr("0123456789.+-eE", *p)) {
                    char one[2] = { *p, '\0' };
                    textbuf_insert(w, one);
                }
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            if (ev->key == TK_KEY_UP) { spin_step(w, 1); return true; }
            if (ev->key == TK_KEY_DOWN) { spin_step(w, -1); return true; }
            if (!w->b2) return false;
            if (ev->key == TK_KEY_ENTER) {
                spin_edit_end(w, true);
                return true;
            }
            if (ev->key == TK_KEY_ESCAPE) {
                spin_edit_end(w, false);
                return true;
            }
            if (textbuf_key(w, ev)) return true;
            if (ev->text && ev->text[0] && ev->key == TK_KEY_NONE &&
                !(ev->modifiers & (TK_MOD_CTRL | TK_MOD_ALT))) {
                /* numeric field: accept only number characters and
                 * swallow the rest so keys do not leak through */
                for (const char *p = ev->text; *p; p++)
                    if (strchr("0123456789.+-eE", *p)) {
                        char one[2] = { *p, '\0' };
                        textbuf_insert(w, one);
                    }
                return true;
            }
        }
        return false;
    }
    case TK_W_COMBOBOX:
        if (!w->b2) { /* non-editable: M2 behavior */
            if (ev->type == TK_EV_POINTER_UP &&
                tk_rect_contains(w->rect, ev->x, ev->y)) {
                combo_open(w, root);
                return true;
            }
            if (ev->type == TK_EV_KEY_DOWN) {
                if (ev->key == TK_KEY_DOWN) {
                    tk_combobox_select(w, w->sel + 1); return true;
                }
                if (ev->key == TK_KEY_UP) {
                    tk_combobox_select(w, w->sel - 1); return true;
                }
                if (ev->key == TK_KEY_ENTER || ev->key == TK_KEY_SPACE) {
                    combo_open(w, root);
                    return true;
                }
            }
            return false;
        }
        /* editable (M3) */
        if (ev->type == TK_EV_FOCUS_IN) {
            w->b1 = true;
            if (!w->text) combo_sync_text(w);
            return false;
        }
        if (ev->type == TK_EV_FOCUS_OUT) { w->b1 = false; return false; }
        if (ev->type == TK_EV_POINTER_UP &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            if (ev->x >= w->rect.x + w->rect.w - 24) {
                combo_open(w, root); /* arrow zone */
                return true;
            }
            if (!w->text) combo_sync_text(w);
            w->edit_pos = (int)tk_offset_at_x(t, tk_combobox_text(w),
                                              ev->x - w->rect.x - t->unit);
            return true;
        }
        if (ev->type == TK_EV_TEXT_INPUT && ev->text) {
            if (!w->text) combo_sync_text(w);
            textbuf_insert(w, ev->text);
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            if (!w->text) combo_sync_text(w);
            if (ev->key == TK_KEY_ENTER) { combo_commit(w); return true; }
            if (ev->key == TK_KEY_ESCAPE) { combo_sync_text(w); return true; }
            if (ev->key == TK_KEY_DOWN) {
                tk_combobox_select(w, w->sel + 1); return true;
            }
            if (ev->key == TK_KEY_UP) {
                tk_combobox_select(w, w->sel - 1); return true;
            }
            if (textbuf_key(w, ev)) return true;
            if (ev->text && ev->text[0] &&
                (ev->key == TK_KEY_NONE || ev->key == TK_KEY_SPACE) &&
                !(ev->modifiers & (TK_MOD_CTRL | TK_MOD_ALT))) {
                textbuf_insert(w, ev->text);
                return true;
            }
        }
        return false;
    case TK_W_MENUBAR:
        if (ev->type == TK_EV_POINTER_UP &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            int idx = menubar_title_at(w, t, ev->x);
            if (idx >= 0 && root) {
                /* M9: a title toggles its menu - if this
                 * press already dismissed THIS menu (the root
                 * records it), the release does not reopen it */
                if (root->down_dismissed_menu == w->bar_menus[idx]) {
                    root->down_dismissed_menu = NULL;
                    return true;
                }
                int sx = 0;
                int tx = w->rect.x + t->unit;
                for (int i = 0; i < idx; i++)
                    tx += tk_text_width(t, w->bar_titles[i]) + 16;
                sx = tx;
                tk_menu_popup(root, w->bar_menus[idx], sx,
                              w->rect.y + w->rect.h);
                return true;
            }
        }
        return false;
    case TK_W_MENU: {
        /* M17: a menu in its exit run swallows input - it is
         * leaving, and its removal (with the focus return) is
         * already scheduled */
        if (w->menu_closing) return true;
        if (ev->type == TK_EV_POINTER_MOVE) {
            int idx = menu_item_at_y(w, t, ev->y);
            if (idx >= 0 && !w->menu_items[idx].separator) {
                if (idx != w->menu_hover) {
                    w->menu_hover = idx;
                    if (w->menu_items[idx].submenu && root)
                        menu_open_sub(root, w, idx);
                    else if (root)
                        menu_close_sub(root, w);
                }
            }
            return true;
        }
        if (ev->type == TK_EV_POINTER_DOWN)
            return tk_rect_contains(w->rect, ev->x, ev->y);
        if (ev->type == TK_EV_POINTER_UP) {
            if (!tk_rect_contains(w->rect, ev->x, ev->y)) {
                if (root) tk_menu_close(root, w);
                return true;
            }
            int idx = menu_item_at_y(w, t, ev->y);
            if (root) return menu_activate(root, w, idx);
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            if (ev->key == TK_KEY_ESCAPE) {
                if (root) tk_close_all_menus(root);
                return true;
            }
            if (ev->key == TK_KEY_DOWN || ev->key == TK_KEY_UP) {
                int dir = ev->key == TK_KEY_DOWN ? 1 : -1;
                int idx = w->menu_hover;
                for (size_t k = 0; k < w->n_menu_items; k++) {
                    idx += dir;
                    if (idx < 0) idx = (int)w->n_menu_items - 1;
                    if (idx >= (int)w->n_menu_items) idx = 0;
                    if (menu_item_selectable(&w->menu_items[idx])) break;
                }
                w->menu_hover = idx;
                return true;
            }
            if (ev->key == TK_KEY_ENTER || ev->key == TK_KEY_SPACE) {
                if (root) return menu_activate(root, w, w->menu_hover);
                return true;
            }
        }
        return false;
    }
    case TK_W_TABS:
        if (ev->type == TK_EV_POINTER_UP &&
            ev->y >= w->rect.y && ev->y < w->rect.y + tabs_bar_h(t) &&
            ev->x >= w->rect.x && ev->x < w->rect.x + w->rect.w) {
            for (size_t i = 0; i < w->n_tab_titles; i++) {
                int sw = 0;
                int sx = w->rect.x + tabs_seg_x(t, w, (int)i, &sw);
                if (ev->x >= sx && ev->x < sx + sw) {
                    tk_tabs_set_active(w, (int)i);
                    return true;
                }
            }
            return true;
        }
        return false;
    case TK_W_DIALOG:
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y))
            return true;     /* modal surface consumes its own clicks */
        return false;
    case TK_W_TEXTVIEW: {
        if (ev->type == TK_EV_FOCUS_IN) { w->b1 = true; return false; }
        if (ev->type == TK_EV_FOCUS_OUT) {
            w->b1 = false; w->dragging = false; return false;
        }
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            tc_move_to(w->buffer, tv_offset_at(w, ev->x, ev->y, false),
                       (ev->modifiers & TK_MOD_SHIFT) != 0);
            w->dragging = true; /* M3: drag selection follows */
            if (w->line_cb) {
                /* M6: report the visual line under the tap */
                tv_lines L;
                tv_build(w, tv_wrap_w(w), &L);
                int lh2 = tk_line_height(t);
                int line = w->top_index +
                           (ev->y - w->rect.y - 4) / (lh2 > 0 ? lh2 : 1);
                if (line < 0) line = 0;
                if (line >= (int)L.n) line = (int)L.n - 1;
                if (line < 0) line = 0;
                tv_lines_free(&L);
                w->line_cb(w, line, w->line_cb_ud);
            }
            return true;
        }
        if (ev->type == TK_EV_POINTER_MOVE && w->dragging) {
            tc_move_to(w->buffer, tv_offset_at(w, ev->x, ev->y, true),
                       true);
            return true;
        }
        if (ev->type == TK_EV_POINTER_UP && w->dragging) {
            w->dragging = false;
            return true;
        }
        if (ev->type == TK_EV_WHEEL) {
            tv_lines L;
            tv_build(w, tv_wrap_w(w), &L);
            int rows = w->rect.h / tk_line_height(t);
            if (rows < 1) rows = 1;
            int nt = w->top_index + ev->wheel_dy * 3;
            int max_top = (int)L.n - rows;
            if (max_top < 0) max_top = 0;
            if (nt < 0) nt = 0;
            if (nt > max_top) nt = max_top;
            w->top_index = nt;
            tv_lines_free(&L);
            return true;
        }
        if (!w->editable) {
            /* read-only: swallow anything that would modify text,
             * let navigation keys through to the shared handler */
            if (ev->type == TK_EV_TEXT_INPUT ||
                ev->type == TK_EV_COMPOSITION)
                return true;
            if (ev->type == TK_EV_KEY_DOWN) {
                if (ev->key == TK_KEY_BACKSPACE ||
                    ev->key == TK_KEY_DELETE || ev->key == TK_KEY_ENTER)
                    return true;
                if ((ev->modifiers & TK_MOD_CTRL) && ev->text) {
                    char ch = ev->text[0];
                    if (ch == 'x' || ch == 'X' || ch == 'v' || ch == 'V' ||
                        ch == 'z' || ch == 'Z' || ch == 'y' || ch == 'Y')
                        return true;
                }
                if (ev->text && *ev->text &&
                    !(ev->modifiers & TK_MOD_CTRL) &&
                    ev->key == TK_KEY_NONE)
                    return true;   /* printable key: ignore */
            }
        }
        if (ev->type == TK_EV_KEY_DOWN || ev->type == TK_EV_TEXT_INPUT ||
            ev->type == TK_EV_COMPOSITION) {
            bool handled = text_edit_key(w, ev, true);
            tv_ensure_visible(w);
            return handled;
        }
        return false;
    }
    case TK_W_TREEVIEW: {
        if (ev->type == TK_EV_POINTER_DOWN &&
            tk_rect_contains(w->rect, ev->x, ev->y)) {
            int row = (ev->y - w->rect.y) / t->row_h + w->top_index;
            int id = tk_treeview_row_node(w, row);
            if (id < 0) return true;
            /* M7: with a model, id is a model id; the child
             * check needs the materialised index */
            int idx = w->tree_model ? tree_index_of_model(w, id)
                                    : id;
            int ix = w->rect.x + t->unit
                     + tk_treeview_node_depth(w, id) * 18;
            if (ev->x >= ix && ev->x < ix + 18 &&
                idx >= 0 && tree_has_children(w, idx)) {
                tk_treeview_set_expanded(w, id,
                                         !tk_treeview_expanded(w, id));
                return true;
            }
            tk_treeview_select(w, id);
            return true;
        }
        if (ev->type == TK_EV_WHEEL) {
            int n = tk_treeview_visible_rows(w);
            int nt = w->top_index + ev->wheel_dy;
            if (nt < 0) nt = 0;
            if (nt >= n) nt = n - 1;
            if (nt < 0) nt = 0;
            w->top_index = nt;
            return true;
        }
        if (ev->type == TK_EV_KEY_DOWN) {
            int row = tree_row_of_node(w, w->sel_id);
            if (ev->key == TK_KEY_DOWN || ev->key == TK_KEY_UP) {
                int nr = row + (ev->key == TK_KEY_DOWN ? 1 : -1);
                int id = tk_treeview_row_node(w, nr);
                if (id >= 0) tk_treeview_select(w, id);
                return true;
            }
            if (ev->key == TK_KEY_RIGHT && w->sel_id >= 0) {
                /* M7: set_expanded takes the public (model) id */
                tk_treeview_set_expanded(w, tk_treeview_selected(w),
                                         true);
                return true;
            }
            if (ev->key == TK_KEY_LEFT && w->sel_id >= 0) {
                tk_treeview_set_expanded(w, tk_treeview_selected(w),
                                         false);
                return true;
            }
            if (ev->key == TK_KEY_ENTER) {
                tk_invoke(w->cb2, w, w->cb2_ud);
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

/* ================================================================ */
/* M5: date/time pickers + file dialog                               */
/* ================================================================ */

/* ---------------- Date/Time pickers ----------------
 * Segmented-control leaves: one segment per field, the active
 * segment carries the selection surface, and a stepper zone at
 * the right edge (upper half = +1, lower half = -1) edits it.
 * Keys: Left/Right choose the field, Up/Down step it, wheel
 * steps it. Fields wrap (month 12 -> 1) except the year, which
 * clamps to 1970..2100; the day clamps to the month length with
 * the full Gregorian leap rule. on_change (cb1) fires on every
 * user edit and on programmatic set_value changes. */
static int pk_days_in_month(int y, int m)
{
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1) m = 1;
    if (m > 12) m = 12;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return dm[m - 1];
}
int tk_pk_nfields(const tk_widget *w)
{
    if (w->kind == TK_W_DATEPICKER) return 3;
    return w->pk_seconds ? 3 : 2;
}
int tk_pk_field_value(const tk_widget *w, int f)
{
    if (w->kind == TK_W_DATEPICKER)
        return f == 0 ? w->pk_year : f == 1 ? w->pk_month : w->pk_day;
    return f == 0 ? w->pk_hour : f == 1 ? w->pk_min : w->pk_sec;
}
static void pk_clamp_day(tk_widget *w)
{
    int dim = pk_days_in_month(w->pk_year, w->pk_month);
    if (w->pk_day > dim) w->pk_day = dim;
    if (w->pk_day < 1) w->pk_day = 1;
}
static void pk_set_field(tk_widget *w, int f, int v)
{
    if (w->kind == TK_W_DATEPICKER) {
        if (f == 0) {
            w->pk_year = v < 1970 ? 1970 : v > 2100 ? 2100 : v;
        } else if (f == 1) {
            w->pk_month = ((v - 1) % 12 + 12) % 12 + 1;
            pk_clamp_day(w);
        } else {
            int dim = pk_days_in_month(w->pk_year, w->pk_month);
            w->pk_day = ((v - 1) % dim + dim) % dim + 1;
        }
    } else {
        if (f == 0)      w->pk_hour = ((v % 24) + 24) % 24;
        else if (f == 1) w->pk_min  = ((v % 60) + 60) % 60;
        else             w->pk_sec  = ((v % 60) + 60) % 60;
    }
}
void tk_pk_step(tk_widget *w, int dir)
{
    pk_set_field(w, w->pk_field, tk_pk_field_value(w, w->pk_field) + dir);
    tk_widget_invalidate(w);
    tk_invoke(w->cb1, w, w->cb1_ud);
}
void tk_pk_cell_rect(const tk_widget *w, int f, tk_rect *out)
{
    tk_rect r = w->rect;
    int area_w = r.w - TK_PK_STEPPER_W;
    int units = 0, start = 0;
    int n = tk_pk_nfields(w);
    for (int i = 0; i < n; i++) {
        int u = (w->kind == TK_W_DATEPICKER && i == 0) ? 2 : 1;
        if (i < f) start += u;
        units += u;
    }
    int fu = (w->kind == TK_W_DATEPICKER && f == 0) ? 2 : 1;
    int x0 = r.x + area_w * start / units;
    int x1 = r.x + area_w * (start + fu) / units;
    *out = (tk_rect){ x0 + 2, r.y + 2, (x1 - x0) - 4, r.h - 4 };
}
tk_rect tk_pk_stepper_rect(const tk_widget *w)
{
    tk_rect r = w->rect;
    return (tk_rect){ r.x + r.w - TK_PK_STEPPER_W, r.y,
                      TK_PK_STEPPER_W, r.h };
}
void tk_pk_draw_tri(tk_canvas *c, int cx, int cy, int dir, tk_color col)
{
    /* 13x8 triangle from stacked rows: no font-glyph dependency */
    for (int i = 0; i < 8; i++) {
        int hw = dir < 0 ? i : 7 - i;
        tk_canvas_fill_rect(c, (tk_rect){cx - hw, cy - 4 + i,
                                         2 * hw + 1, 1}, col);
    }
}

tk_widget *tk_datepicker_new(int year, int month, int day)
{
    tk_widget *w = leaf(TK_W_DATEPICKER, NULL, true);
    if (!w) return NULL;
    w->pk_year = year < 1970 ? 1970 : year > 2100 ? 2100 : year;
    w->pk_month = month < 1 ? 1 : month > 12 ? 12 : month;
    w->pk_day = day;
    pk_clamp_day(w);
    return w;
}
void tk_datepicker_value(const tk_widget *w, int *y, int *m, int *d)
{
    if (y) *y = w->pk_year;
    if (m) *m = w->pk_month;
    if (d) *d = w->pk_day;
}
void tk_datepicker_set_value(tk_widget *w, int y, int m, int d)
{
    if (!w) return;
    y = y < 1970 ? 1970 : y > 2100 ? 2100 : y;
    m = m < 1 ? 1 : m > 12 ? 12 : m;
    int dim = pk_days_in_month(y, m);
    d = d < 1 ? 1 : d > dim ? dim : d;
    if (y == w->pk_year && m == w->pk_month && d == w->pk_day) return;
    w->pk_year = y; w->pk_month = m; w->pk_day = d;
    tk_widget_invalidate(w);
    tk_invoke(w->cb1, w, w->cb1_ud);
}
void tk_datepicker_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (w) { w->cb1 = cb; w->cb1_ud = ud; }
}

tk_widget *tk_timepicker_new(bool show_seconds)
{
    tk_widget *w = leaf(TK_W_TIMEPICKER, NULL, true);
    if (w) w->pk_seconds = show_seconds;
    return w;
}
void tk_timepicker_value(const tk_widget *w, int *h, int *m, int *s)
{
    if (h) *h = w->pk_hour;
    if (m) *m = w->pk_min;
    if (s) *s = w->pk_sec;
}
void tk_timepicker_set_value(tk_widget *w, int h, int m, int s)
{
    if (!w) return;
    h = ((h % 24) + 24) % 24;
    m = ((m % 60) + 60) % 60;
    s = ((s % 60) + 60) % 60;
    if (h == w->pk_hour && m == w->pk_min && s == w->pk_sec) return;
    w->pk_hour = h; w->pk_min = m; w->pk_sec = s;
    tk_widget_invalidate(w);
    tk_invoke(w->cb1, w, w->cb1_ud);
}
void tk_timepicker_set_show_seconds(tk_widget *w, bool on)
{
    if (!w || w->pk_seconds == on) return;
    w->pk_seconds = on;
    if (w->pk_field > tk_pk_nfields(w) - 1)
        w->pk_field = tk_pk_nfields(w) - 1;
    tk_widget_invalidate(w);
}
void tk_timepicker_set_on_change(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (w) { w->cb1 = cb; w->cb1_ud = ud; }
}

/* ---------------- File dialog ----------------
 * A composed TK_W_DIALOG: path label, entry ListView, and a
 * Cancel / Select button row. Directories sort first (shown with
 * a trailing '/'), dotfiles are hidden, files are filtered by the
 * extension list (case-insensitive; empty list = all files). A
 * ".." entry leads to the parent. State lives in the dialog's
 * ext (released by the generic free path); entries are capped at
 * 256 per directory. */
#define TK_FD_MAX_ENTRIES 256
typedef struct {
    char dir[1024];
    char exts[8][16];
    int n_exts;
    uint8_t is_dir[TK_FD_MAX_ENTRIES];
    int n_entries;
    tk_widget *list;
    tk_widget *path_lbl;
    tk_filedialog_cb cb;
    void *cb_ud;
    char chosen[1280];
} tk_fd_state;

static bool fd_ext_match(const tk_fd_state *st, const char *name)
{
    if (st->n_exts == 0) return true;
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return false;
    for (int i = 0; i < st->n_exts; i++)
        if (strcasecmp(dot + 1, st->exts[i]) == 0) return true;
    return false;
}
typedef struct { char *name; bool is_dir; } fd_ent;
static int fd_ent_cmp(const void *a, const void *b)
{
    const fd_ent *x = a, *y = b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}
static void fd_join(char *out, size_t cap, const char *dir,
                    const char *name)
{
    if (strcmp(dir, "/") == 0) snprintf(out, cap, "/%s", name);
    else snprintf(out, cap, "%s/%s", dir, name);
}
void tk_filedialog_refresh(tk_widget *dlg)
{
    tk_fd_state *st = dlg ? dlg->ext : NULL;
    if (!st || !st->list) return;
    fd_ent *ents = calloc(TK_FD_MAX_ENTRIES, sizeof(fd_ent));
    if (!ents) return;
    int n = 0;
    bool root_dir = strcmp(st->dir, "/") == 0;
    if (!root_dir) {
        ents[n].name = tk_strdup("..");
        ents[n].is_dir = true;
        n++;
    }
    DIR *d = opendir(st->dir);
    if (d) {
        struct dirent *e;
        while (n < TK_FD_MAX_ENTRIES && (e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            bool isdir;
            if (e->d_type == DT_DIR) {
                isdir = true;
            } else if (e->d_type == DT_UNKNOWN) {
                char full[1280];
                struct stat sb;
                fd_join(full, sizeof full, st->dir, e->d_name);
                isdir = stat(full, &sb) == 0 && S_ISDIR(sb.st_mode);
            } else {
                isdir = false;
            }
            if (!isdir && !fd_ext_match(st, e->d_name)) continue;
            ents[n].name = tk_strdup(e->d_name);
            if (!ents[n].name) continue;
            ents[n].is_dir = isdir;
            n++;
        }
        closedir(d);
    }
    int off = root_dir ? 0 : 1; /* ".." stays pinned first */
    if (n - off > 1)
        qsort(ents + off, (size_t)(n - off), sizeof(fd_ent), fd_ent_cmp);
    const char **items = calloc((size_t)(n > 0 ? n : 1), sizeof(char *));
    char **disp = calloc((size_t)(n > 0 ? n : 1), sizeof(char *));
    st->n_entries = n;
    for (int i = 0; i < n; i++) {
        st->is_dir[i] = ents[i].is_dir ? 1 : 0;
        if (ents[i].is_dir && strcmp(ents[i].name, "..") != 0 && disp) {
            size_t len = strlen(ents[i].name);
            disp[i] = malloc(len + 2);
            if (disp[i]) {
                memcpy(disp[i], ents[i].name, len);
                disp[i][len] = '/';
                disp[i][len + 1] = '\0';
            }
            items[i] = disp[i] ? disp[i] : ents[i].name;
        } else {
            items[i] = ents[i].name;
        }
    }
    tk_listview_set_items(st->list, items, (size_t)n);
    for (int i = 0; i < n; i++) {
        free(ents[i].name);
        if (disp) free(disp[i]);
    }
    free(ents);
    free(items);
    free(disp);
    tk_label_set_text(st->path_lbl, st->dir);
    if (n > 0) tk_listview_select(st->list, off < n ? off : 0);
}
static void fd_navigate(tk_widget *dlg, const char *name)
{
    tk_fd_state *st = dlg->ext;
    if (strcmp(name, "..") == 0) {
        char *slash = strrchr(st->dir, '/');
        if (slash == st->dir) st->dir[1] = '\0';
        else if (slash) *slash = '\0';
    } else {
        char tmp[1024];
        fd_join(tmp, sizeof tmp, st->dir, name);
        snprintf(st->dir, sizeof st->dir, "%s", tmp);
    }
    tk_filedialog_refresh(dlg);
}
static void fd_finish(tk_widget *dlg, bool ok)
{
    tk_fd_state *st = dlg->ext;
    tk_root *r = tk_widget_root(dlg);
    const char *path = NULL;
    if (ok) {
        int sel = tk_listview_selected(st->list);
        if (sel < 0 || sel >= st->n_entries || st->is_dir[sel]) return;
        const char *name = tk_listview_item(st->list, sel);
        if (!name) return;
        fd_join(st->chosen, sizeof st->chosen, st->dir, name);
        path = st->chosen;
    }
    if (st->cb) st->cb(dlg, path, st->cb_ud);
    if (r && tk_dialog_is_shown(r, dlg)) tk_dialog_close(r, dlg);
}
static void fd_activate(tk_widget *list, void *ud)
{
    tk_widget *dlg = ud;
    tk_fd_state *st = dlg->ext;
    int sel = tk_listview_selected(list);
    if (sel < 0 || sel >= st->n_entries) return;
    if (st->is_dir[sel]) {
        const char *disp = tk_listview_item(list, sel);
        char name[256];
        snprintf(name, sizeof name, "%s", disp ? disp : "");
        size_t len = strlen(name);
        if (len && name[len - 1] == '/') name[len - 1] = '\0';
        fd_navigate(dlg, name);
    } else {
        fd_finish(dlg, true);
    }
}
static void fd_on_cancel(tk_widget *btn, void *ud)
{
    (void)btn;
    fd_finish(ud, false);
}
static void fd_on_ok(tk_widget *btn, void *ud)
{
    (void)btn;
    tk_widget *dlg = ud;
    tk_fd_state *st = dlg->ext;
    int sel = tk_listview_selected(st->list);
    if (sel >= 0 && sel < st->n_entries && st->is_dir[sel])
        fd_activate(st->list, dlg);
    else
        fd_finish(dlg, true);
}

tk_widget *tk_filedialog_new(const char *title, const char *start_dir,
                             const char *const *exts, size_t n_exts)
{
    tk_widget *dlg = tk_dialog_new(title);
    if (!dlg) return NULL;
    tk_fd_state *st = calloc(1, sizeof(*st));
    if (!st) { tk_widget_free(dlg); return NULL; }
    dlg->ext = st;
    snprintf(st->dir, sizeof st->dir, "%s",
             start_dir && start_dir[0] ? start_dir : "/");
    size_t dl = strlen(st->dir);
    while (dl > 1 && st->dir[dl - 1] == '/') st->dir[--dl] = '\0';
    st->n_exts = 0;
    if (exts) {
        for (size_t i = 0; i < n_exts && st->n_exts < 8; i++) {
            const char *e = exts[i];
            if (!e) continue;
            if (*e == '.') e++;
            if (!*e) continue;
            snprintf(st->exts[st->n_exts],
                     sizeof st->exts[st->n_exts], "%s", e);
            st->n_exts++;
        }
    }
    st->path_lbl = tk_label_new(st->dir);
    tk_add_child(dlg, st->path_lbl);
    st->list = tk_listview_new();
    tk_set_flex(st->list, 1);
    tk_listview_set_on_activate(st->list, fd_activate, dlg);
    tk_add_child(dlg, st->list);
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
    tk_widget *cancel = tk_button_new("キャンセル");
    tk_button_set_on_click(cancel, fd_on_cancel, dlg);
    tk_widget *ok = tk_button_new("選択");
    tk_button_set_accent(ok, true);
    tk_button_set_on_click(ok, fd_on_ok, dlg);
    tk_add_child(row, cancel);
    tk_add_child(row, ok);
    tk_add_child(dlg, row);
    tk_filedialog_refresh(dlg);
    return dlg;
}
void tk_filedialog_show(tk_root *r, tk_widget *dlg)
{
    if (!r || !dlg) return;
    tk_dialog_show(r, dlg);
    tk_fd_state *st = dlg->ext;
    if (st && st->list) (void)tk_focus(r, st->list);
}
void tk_filedialog_set_on_result(tk_widget *dlg, tk_filedialog_cb cb,
                                 void *ud)
{
    tk_fd_state *st = dlg ? dlg->ext : NULL;
    if (st) { st->cb = cb; st->cb_ud = ud; }
}
const char *tk_filedialog_dir(const tk_widget *dlg)
{
    tk_fd_state *st = dlg ? dlg->ext : NULL;
    return st ? st->dir : NULL;
}

/* ---------------- Button badge (M6) ---------------- */
void tk_button_set_badge(tk_widget *w, int count)
{
    if (!w || w->kind != TK_W_BUTTON) return;
    if (w->badge_count != count) {
        w->badge_count = count;
        tk_widget_invalidate(w);
    }
}
int tk_button_badge(const tk_widget *w)
{
    return w ? w->badge_count : 0;
}

/* ---------------- BottomBar (M6) ----------------
 * A composed horizontal box with a surface backdrop; cells are
 * flat icon-cell buttons sharing the width equally (flex 1).
 * M23: the DEFAULT selection form is the M3 navigation-bar
 * indicator (mode 1 - the icon-only pill; see nav_pill_rect) -
 * pre-M23 the default was the tonal whole-cell face and no
 * production caller ever opted into the pill, which is the
 * mismatch the user sent back on 2026-11-21. The tonal face
 * remains available as the explicit mode 0. */
tk_widget *tk_bottombar_new(void)
{
    /* tight outer padding: the cells carry their own internal
     * padding, and the apps-envfit SS3 budget for a bottom bar
     * is 64-76px total on this panel */
    tk_widget *w = tk_box_new(TK_DIR_HORIZONTAL, 8, 2);
    if (w) {
        tk_widget_set_backdrop(w, false, -1);
        w->i1 = 1; /* M23: navigation indicator form by default */
    }
    return w;
}
int tk_bottombar_add_cell(tk_widget *bar, const uint32_t *icon_px,
                          int iw, int ih, const char *label,
                          tk_notify_cb cb, void *ud)
{
    if (!bar) return -1;
    tk_widget *b = tk_button_new(label ? label : "");
    if (!b) return -1;
    if (icon_px) tk_button_set_icon(b, icon_px, iw, ih);
    tk_button_set_flat(b, true);
    b->btn_face_pill = bar->i1 == 1; /* M7: inherit the bar mode */
    if (cb) tk_button_set_on_click(b, cb, ud);
    tk_add_child(bar, b);
    tk_set_flex(b, 1);
    return (int)tk_child_count(bar) - 1;
}
tk_widget *tk_bottombar_cell(tk_widget *bar, int idx)
{
    return tk_child_at(bar, (size_t)(idx < 0 ? 0 : idx));
}
void tk_bottombar_set_selected(tk_widget *bar, int idx)
{
    if (!bar) return;
    if (bar->bb_selected == idx) return;
    int prev = bar->bb_selected;
    bar->bb_selected = idx;
    if (prev >= 0) {
        tk_widget *old = tk_child_at(bar, (size_t)prev);
        if (old) {
            old->btn_face_pill = bar->i1 == 1;
            old->btn_face_container = false;
            old->btn_face_indicator = false;
            tk_button_set_face(old, NULL);
        }
    }
    if (idx >= 0) {
        tk_widget *cell = tk_child_at(bar, (size_t)idx);
        if (cell) {
            cell->btn_face_pill = bar->i1 == 1;
            /* dynamic indicator face (see bb_apply_selection) */
            cell->btn_face_indicator = true;
            cell->btn_face_container = false;
            tk_button_set_face(cell, NULL);
        }
    }
    /* M16: the displayed indicator position follows the
     * selection - eased by tk_ind_anim_to when a live
     * animation clock is present, snapped otherwise (first
     * selection and deselection always snap) */
    if (prev < 0 || idx < 0)
        bar->ind_pos = idx < 0 ? 0.0f : (float)idx;
    tk_ind_anim_to(bar, idx);
    tk_widget_invalidate(bar);
}
int tk_bottombar_selected(const tk_widget *bar)
{
    return bar ? bar->bb_selected : -1;
}
void tk_bottombar_set_cell_badge(tk_widget *bar, int idx, int count)
{
    tk_widget *cell = tk_bottombar_cell(bar, idx);
    if (cell) tk_button_set_badge(cell, count);
}

/* M7: selection indicator form. The mode rides in the bar's
 * i1 (a menu field a box never uses): 0 = the tonal cell face
 * (pre-M7), 1 = the M3 pill behind the icon (the NavigationView
 * form in the WinUI mode - see the button's pill paint).
 * M23: mode 1 is the default (tk_bottombar_new) - it is the
 * reference NavigationBar form; mode 0 is opt-in legacy. */
static void bb_apply_selection(tk_widget *bar)
{
    int mode = bar->i1 == 1 ? 1 : 0;
    /* M16: a mode switch restyles the indicator in place;
     * the displayed position snaps to the logical selection */
    if (bar->anim_handle) {
        tk_anim_cancel(bar->anim_handle);
        bar->anim_handle = NULL;
    }
    bar->ind_pos = bar->bb_selected < 0 ? 0.0f
                                        : (float)bar->bb_selected;
    for (size_t i = 0; i < tk_child_count(bar); i++) {
        tk_widget *cell = tk_child_at(bar, i);
        if (!cell) continue;
        cell->btn_face_pill = mode == 1;
        /* M7: the selected cell's face is a paint-theme token,
         * NOT a colour baked from whichever theme was the
         * default when the selection last changed - a runtime
         * mode switch must recolour the bar without the app
         * re-applying the selection. The stored custom face is
         * cleared. M13: the token is the navigation indicator
         * role (btn_face_indicator - secondaryContainer in the
         * M3 mode), not primaryContainer. */
        cell->btn_face_indicator = (int)i == bar->bb_selected;
        cell->btn_face_container = false;
        tk_button_set_face(cell, NULL);
    }
}
void tk_bottombar_set_indicator_mode(tk_widget *bar, int mode)
{
    if (!bar) return;
    bar->i1 = mode == 1 ? 1 : 0;
    bb_apply_selection(bar);
}
int tk_bottombar_indicator_mode(const tk_widget *bar)
{
    return (bar && bar->i1 == 1) ? 1 : 0;
}

/* M16: the bar paints the selection indicator itself (called
 * from tk_paint after the bar's backdrop/leaf paint, before
 * its children) because the indicator's DISPLAYED position
 * (ind_pos) is a property of the bar, not of any one cell:
 * on a selection change the M3 forms crossfade the face
 * between the old and new cells (the Compose
 * NavigationBarItem indicator is a fade - the item content
 * colour/alpha animates, there is no sliding pill in the
 * reference), while the WinUI NavigationView form slides
 * the subtle fill + accent bar horizontally. At rest
 * (ind_pos integral) the pixels are exactly the pre-M16
 * per-cell faces the button paint used to draw. */
static tk_color ind_scale(tk_color col, float f)
{
    if (f >= 1.0f) return col;
    if (f <= 0.0f) { col.a = 0; return col; }
    col.a = (uint8_t)(col.a * f + 0.5f);
    return col;
}
void tk_bottombar_paint_indicator(tk_widget *bar, tk_canvas *c,
                                  const tk_theme *t)
{
    if (!bar || bar->kind != TK_W_BOX || !c || !t) return;
    int sel = bar->bb_selected;
    if (sel < 0 || (size_t)sel >= bar->n_children) return;
    bool any = bar->anim_handle != NULL;
    for (size_t i = 0; i < bar->n_children && !any; i++)
        any = bar->children[i]->btn_face_indicator;
    if (!any) return;
    float pos = bar->ind_pos;
    int lo = (int)pos;
    if (lo < 0) lo = 0;
    float frac = pos - (float)lo;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    if ((size_t)lo >= bar->n_children) return;
    if (t->style == 2) {
        /* WinUI: one fill + accent bar, slid horizontally */
        tk_rect r = bar->children[lo]->rect;
        int x = r.x;
        if (frac > 0.0f && (size_t)(lo + 1) < bar->n_children) {
            tk_rect nb = bar->children[lo + 1]->rect;
            x = r.x + (int)((nb.x - r.x) * frac + 0.5f);
        }
        tk_canvas_fill_rect(c, (tk_rect){ x, r.y, r.w, r.h },
                            t->subtle.a ? t->subtle : t->surface_alt);
        tk_canvas_fill_rect(c,
            (tk_rect){ x, r.y + (r.h - 24) / 2, 3, 24 }, t->accent);
        return;
    }
    /* M3 forms: crossfade between the lo and lo+1 cells */
    for (int pass = 0; pass < 2; pass++) {
        int idx = lo + pass;
        float f = pass == 0 ? 1.0f - frac : frac;
        if (f <= 0.001f || (size_t)idx >= bar->n_children) continue;
        tk_widget *cell = bar->children[idx];
        tk_rect r = cell->rect;
        if (cell->btn_face_pill) {
            tk_rect pr = nav_pill_rect(cell, r, t);
            tk_canvas_fill_round_rect(c, pr, pr.h / 2,
                                      ind_scale(t->selection, f));
        } else {
            int brad = cell->btn_radius >= 0 ? cell->btn_radius
                       : t->radius_button >= 0 ? t->radius_button
                                               : t->radius;
            tk_canvas_fill_round_rect(c, r, brad,
                                      ind_scale(t->container, f));
        }
    }
}

/* ---------------- ColorPicker dialog (M6) ----------------
 * A composed Dialog: a grid of preset swatches (buttons whose
 * face is the swatch colour; the current pick gets an indicator
 * ring via the button's indicator bar), a preview row, and
 * Cancel / OK. The palette is the Material baseline hues at a
 * mid shade plus the neutral ramp - fixed data, no heap state
 * beyond the ext block (same pattern as the file dialog). */
typedef struct {
    uint32_t value;         /* 0xRRGGBBAA */
    tk_colorpicker_cb cb;
    void *cb_ud;
    tk_widget *preview;     /* label used as the preview strip */
    tk_widget *swatches[24];
    int n_swatches;
    /* M7: custom hex entry + optional alpha slider */
    tk_widget *hex_field;   /* TextField, "#RRGGBB[AA]" */
    tk_widget *alpha_row;   /* box holding the alpha slider */
    tk_widget *alpha_slider;
    bool alpha_on;
    bool busy;              /* sync reentrancy guard */
} tk_cp_state;

static const uint32_t tk_cp_palette[24] = {
    0xF44336FF, 0xE91E63FF, 0x9C27B0FF, 0x673AB7FF,
    0x3F51B5FF, 0x2196F3FF, 0x03A9F4FF, 0x00BCD4FF,
    0x009688FF, 0x4CAF50FF, 0x8BC34AFF, 0xCDDC39FF,
    0xFFEB3BFF, 0xFFC107FF, 0xFF9800FF, 0xFF5722FF,
    0x795548FF, 0x9E9E9EFF, 0x607D8BFF, 0x000000FF,
    0x424242FF, 0x757575FF, 0xBDBDBDFF, 0xFFFFFFFF,
};

/* M7: re-sync the pick's views. src: 0 = everything (swatch
 * click, programmatic set), 1 = the change came from the hex
 * field (do not rewrite its text), 2 = from the alpha slider.
 * The setters fire their on_change, so each is wrapped in the
 * busy guard and the handlers below no-op under it. */
static void cp_apply(tk_widget *dlg, int src)
{
    tk_cp_state *st = dlg->ext;
    if (!st || !st->preview) return;
    tk_color col = { (uint8_t)(st->value >> 24),
                     (uint8_t)(st->value >> 16),
                     (uint8_t)(st->value >> 8),
                     (uint8_t)(st->value) };
    tk_widget_set_backdrop(st->preview, false, 4);
    tk_widget_set_backdrop_face(st->preview, &col);
    for (int i = 0; i < st->n_swatches; i++) {
        tk_color ring = { 255, 255, 255, 255 };
        tk_button_set_indicator(st->swatches[i],
            tk_cp_palette[i] == st->value ? &ring : NULL);
    }
    if (st->hex_field && src != 1) {
        char hx[16];
        if (st->alpha_on)
            snprintf(hx, sizeof(hx), "#%08X", st->value);
        else
            snprintf(hx, sizeof(hx), "#%06X", st->value >> 8);
        char cur[32];
        tk_textfield_text(st->hex_field, cur, sizeof(cur));
        if (strcmp(cur, hx) != 0) {
            st->busy = true;
            tk_textfield_set_text(st->hex_field, hx);
            st->busy = false;
        }
    }
    if (st->alpha_slider && src != 2) {
        st->busy = true;
        /* internal reflection of the picker's own edits:
         * direct, not animated (M13) */
        slider_set_value_impl(st->alpha_slider,
                              (float)(st->value & 0xFF) / 255.0f,
                              false);
        st->busy = false;
    }
}

static void cp_sync_preview(tk_widget *dlg) { cp_apply(dlg, 0); }

/* M7: parse "#RRGGBB" / "#RRGGBBAA" (the '#' is optional);
 * returns false for anything else. */
static bool cp_parse_hex(const char *s, uint32_t *out,
                         bool *has_alpha)
{
    if (!s) return false;
    if (*s == '#') s++;
    size_t n = strlen(s);
    if (n != 6 && n != 8) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    *has_alpha = n == 8;
    *out = n == 8 ? v : (v << 8) | 0xFFu;
    return true;
}

static void cp_on_hex(tk_widget *field, void *ud)
{
    tk_widget *dlg = ud;
    tk_cp_state *st = dlg->ext;
    if (!st || st->busy) return;
    char buf[32];
    tk_textfield_text(field, buf, sizeof(buf));
    uint32_t v;
    bool has_alpha;
    if (!cp_parse_hex(buf, &v, &has_alpha)) return;
    /* a 6-digit hex names an opaque colour (the 8-digit form
     * is how an alpha is specified); with alpha disabled the
     * value is opaque by definition */
    if (!has_alpha) v |= 0xFFu;
    st->value = v;
    cp_apply(dlg, 1);
    tk_widget_invalidate(dlg);
}

static void cp_on_alpha(tk_widget *slider, void *ud)
{
    tk_widget *dlg = ud;
    tk_cp_state *st = dlg->ext;
    if (!st || st->busy) return;
    uint32_t a = (uint32_t)(tk_slider_value(slider) * 255.0f + 0.5f);
    st->value = (st->value & 0xFFFFFF00u) | a;
    cp_apply(dlg, 2);
    tk_widget_invalidate(dlg);
}
static void cp_on_swatch(tk_widget *b, void *ud)
{
    tk_widget *dlg = ud;
    tk_cp_state *st = dlg->ext;
    for (int i = 0; i < st->n_swatches; i++)
        if (st->swatches[i] == b) {
            st->value = tk_cp_palette[i];
            break;
        }
    cp_sync_preview(dlg);
    tk_widget_invalidate(dlg);
}
static tk_root *cp_root_of(tk_widget *dlg) { return tk_widget_root(dlg); }
static void cp_on_ok(tk_widget *b, void *ud)
{
    (void)b;
    tk_widget *dlg = ud;
    tk_cp_state *st = dlg->ext;
    tk_root *r = cp_root_of(dlg);
    uint32_t v = st->value;
    tk_colorpicker_cb cb = st->cb;
    void *cud = st->cb_ud;
    if (r) tk_dialog_close(r, dlg);
    if (cb) cb(dlg, v, true, cud);
}
static void cp_on_cancel(tk_widget *b, void *ud)
{
    (void)b;
    tk_widget *dlg = ud;
    tk_cp_state *st = dlg->ext;
    tk_root *r = cp_root_of(dlg);
    tk_colorpicker_cb cb = st->cb;
    void *cud = st->cb_ud;
    if (r) tk_dialog_close(r, dlg);
    if (cb) cb(dlg, 0, false, cud);
}

tk_widget *tk_colorpicker_new(const char *title, uint32_t initial_rgba)
{
    tk_widget *dlg = tk_dialog_new(title);
    if (!dlg) return NULL;
    tk_cp_state *st = calloc(1, sizeof(*st));
    if (!st) return dlg;
    dlg->ext = st;
    st->value = initial_rgba;
    st->n_swatches = 24;
    tk_widget *grid = tk_grid_new(6, 8, 8, 4);
    for (int i = 0; i < st->n_swatches; i++) {
        uint32_t v = tk_cp_palette[i];
        tk_color col = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                         (uint8_t)(v >> 8), (uint8_t)(v) };
        tk_widget *b = tk_button_new("");
        tk_button_set_flat(b, true);
        tk_button_set_face(b, &col);
        tk_button_set_corner_radius(b, 8);
        tk_widget_set_min_size(b, 48, 40);
        tk_button_set_on_click(b, cp_on_swatch, dlg);
        st->swatches[i] = b;
        tk_add_child(grid, b);
    }
    tk_add_child(dlg, grid);
    st->preview = tk_label_new(" ");
    tk_widget_set_min_size(st->preview, 200, 28);
    tk_add_child(dlg, st->preview);
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
    tk_widget *cancel = tk_button_new("キャンセル");
    tk_button_set_variant(cancel, TK_BTN_TEXT);
    tk_button_set_on_click(cancel, cp_on_cancel, dlg);
    tk_widget *ok = tk_button_new("OK");
    tk_button_set_variant(ok, TK_BTN_FILLED);
    tk_button_set_on_click(ok, cp_on_ok, dlg);
    tk_add_child(row, cancel);
    tk_add_child(row, ok);
    tk_add_child(dlg, row);
    /* M7: custom hex entry + alpha slider rows, appended
     * AFTER the button row: the row must stay at child index
     * 2, which existing consumers address directly */
    st->hex_field = tk_textfield_new(NULL);
    tk_textfield_set_placeholder(st->hex_field, "#RRGGBB");
    tk_textfield_set_on_change(st->hex_field, cp_on_hex, dlg);
    tk_add_child(dlg, st->hex_field);
    st->alpha_row = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
    tk_widget *albl = tk_label_new("透明度");
    st->alpha_slider = tk_slider_new(1.0f);
    tk_slider_set_on_change(st->alpha_slider, cp_on_alpha, dlg);
    tk_add_child(st->alpha_row, albl);
    tk_add_child(st->alpha_row, st->alpha_slider);
    tk_set_flex(st->alpha_slider, 1);
    st->alpha_row->visible = false;
    tk_add_child(dlg, st->alpha_row);
    cp_sync_preview(dlg);
    return dlg;
}
void tk_colorpicker_show(tk_root *r, tk_widget *dlg)
{
    if (!r || !dlg) return;
    tk_dialog_show(r, dlg);
}
void tk_colorpicker_set_on_result(tk_widget *dlg, tk_colorpicker_cb cb,
                                  void *ud)
{
    tk_cp_state *st = dlg ? dlg->ext : NULL;
    if (st) { st->cb = cb; st->cb_ud = ud; }
}
uint32_t tk_colorpicker_value(const tk_widget *dlg)
{
    tk_cp_state *st = dlg ? dlg->ext : NULL;
    return st ? st->value : 0;
}

/* M7: programmatic set + alpha opt-in */
void tk_colorpicker_set_value(tk_widget *dlg, uint32_t rgba)
{
    tk_cp_state *st = dlg ? dlg->ext : NULL;
    if (!st) return;
    st->value = st->alpha_on ? rgba : (rgba | 0xFFu);
    cp_apply(dlg, 0);
    tk_widget_invalidate(dlg);
}
void tk_colorpicker_set_alpha_enabled(tk_widget *dlg, bool on)
{
    tk_cp_state *st = dlg ? dlg->ext : NULL;
    if (!st) return;
    st->alpha_on = on;
    if (st->alpha_row) st->alpha_row->visible = on;
    if (!on) st->value |= 0xFFu; /* back to opaque results */
    cp_apply(dlg, 0);
    tk_widget_invalidate(dlg);
}
bool tk_colorpicker_alpha_enabled(const tk_widget *dlg)
{
    tk_cp_state *st = dlg ? dlg->ext : NULL;
    return st && st->alpha_on;
}
