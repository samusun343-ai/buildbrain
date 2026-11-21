/* test_tk9.c - M10 state-pixel coverage.
 *
 * tk7 pinned dimensions and the rest forms per mode; tk8
 * pinned the M7-M9 component wave. This suite closes the
 * B-viewpoint gap the M9 audit recorded: for each component
 * the full state set (rest / pressed / focused / disabled /
 * error / selected) is painted in BOTH modes and probed at
 * the pixel level against the state forms of the references:
 *
 *   - Material 3 state layers + disabled opacities
 *     (m3.material.io/foundations/interaction/states,
 *     fetched 2026-11-20): pressed layer = the content's
 *     on-colour at 10%; disabled content = on-surface 38%,
 *     disabled container = on-surface 12%.
 *   - Fluent state fills (Microsoft Learn theme colour
 *     token table, fetched 2026-11-20): AccentFill
 *     Secondary/Tertiary/Disabled, ControlFill Secondary/
 *     Tertiary/Disabled, the strong stroke/fill pair,
 *     TextFillColorDisabled, SystemFillColorAttention.
 *
 * Expected values are computed from the theme tokens with
 * the canvas's own blend arithmetic, so the same checks run
 * unchanged under the light and dark theme of a mode. */
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; \
        printf("FAIL %d: %s\n", __LINE__, #cond); } \
} while (0)

static bool col_eq(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
}
static tk_color px(tk_canvas *c, int x, int y)
{
    return tk_canvas_pixel(c, x, y);
}
/* src (with its alpha) composited over an opaque dst - the
 * canvas's blend arithmetic, for expected pixel values */
static tk_color blend(tk_color s, tk_color d)
{
    int a = s.a, ia = 255 - a;
    tk_color o = { (uint8_t)((s.r * a + d.r * ia) / 255),
                   (uint8_t)((s.g * a + d.g * ia) / 255),
                   (uint8_t)((s.b * a + d.b * ia) / 255), 255 };
    return o;
}
static tk_color with_a(tk_color col, int a)
{
    col.a = (uint8_t)a;
    return col;
}
static int count_near(tk_canvas *c, tk_rect r, tk_color want,
                      int tol)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            if (col_near_c(px(c, x, y), want, tol)) n++;
    return n;
}

static tk_root *root_with(tk_widget *w)
{
    /* the child is inset by spacer labels: the focus ring is
     * drawn 2px OUTSIDE the widget rect, so a widget flush
     * with the canvas edge would clip it away */
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
static void send_ptr(tk_root *r, int x, int y, bool down)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = down ? TK_EV_POINTER_DOWN : TK_EV_POINTER_UP;
    ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
static void send_move(tk_root *r, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_MOVE; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
/* the focus ring: the root paints a 2px stroke on the
 * rect inflated by 1 (paint_focus_ring in tk_core.c); the
 * WinUI outer colour is the FocusStrokeColorOuter pair
 * (near-black in light, white in dark) and its inner stroke
 * is the M10-corrected FocusStrokeInner (white in light) */
static void check_focus_ring_x(tk_root *r, tk_widget *w,
                               bool on_edge)
{
    const tk_theme *t = tk_theme_default();
    tk_rect q = tk_widget_rect(w);
    CHECK(tk_focus(r, w));
    tk_canvas *c = paint_full(r);
    int my = q.y + q.h / 2;
    if (t->style == 2) {
        tk_color outer = t->dark
            ? (tk_color){ 255, 255, 255, 255 }
            : (tk_color){ 0, 0, 0, 228 };
        tk_color want = blend(outer, t->bg);
        CHECK(col_near_c(px(c, q.x - 1, my), want, 4));
        CHECK(col_near_c(px(c, q.x + q.w, my), want, 4));
        if (!t->dark) {
            tk_color white = { 255, 255, 255, 255 };
            CHECK(col_eq(px(c, q.x, my), white));
        }
    } else if (on_edge) {
        /* the M3 filled field's ring sits on its own edge */
        CHECK(col_near_c(px(c, q.x, my), t->focus, 4));
        CHECK(col_near_c(px(c, q.x + q.w - 1, my), t->focus,
                         4));
    } else {
        CHECK(col_near_c(px(c, q.x - 1, my), t->focus, 4));
        CHECK(col_near_c(px(c, q.x + q.w, my), t->focus, 4));
    }
    tk_canvas_free(c);
}
static void check_focus_ring(tk_root *r, tk_widget *w)
{
    check_focus_ring_x(r, w, false);
}

/* ---------- A. Button states, both modes ---------- */
static void test_button_states(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, &tk_theme_winui };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        /* FILLED: rest = accent, pressed = the mode's accent
         * state form, disabled = the mode's disabled form */
        tk_widget *b = tk_button_new("実行");
        tk_button_set_variant(b, TK_BTN_FILLED);
        tk_root *r = root_with(b);
        tk_rect q = tk_widget_rect(b);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, q.y + q.h / 2), t->accent));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, q.y + q.h / 2, true);
        c = paint_full(r);
        if (t->style == 1) {
            /* M3 pressed: on-primary state layer at 10% */
            tk_color want = blend(with_a(t->on_accent, 26),
                                  t->accent);
            CHECK(col_near_c(px(c, q.x + 6, q.y + q.h / 2),
                             want, 3));
        } else {
            /* Fluent pressed: AccentFillColorTertiary =
             * the accent at 80% over the page */
            tk_color want = blend(with_a(t->accent, 204), t->bg);
            CHECK(col_near_c(px(c, q.x + 6, q.y + q.h / 2),
                             want, 3));
        }
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, q.y + q.h / 2, false);
        check_focus_ring(r, b);
        tk_set_enabled(b, false);
        c = paint_full(r);
        if (t->style == 1) {
            /* M3 disabled: container on-surface 12% */
            tk_color want = blend(with_a(t->text, 31), t->bg);
            CHECK(col_near_c(px(c, q.x + 6, q.y + q.h / 2),
                             want, 3));
            /* content: on-surface 38% over the container */
            tk_color gwant = blend(with_a(t->text, 97), want);
            CHECK(count_near(c, q, gwant, 24) > 4);
        } else {
            /* Fluent disabled: AccentFillColorDisabled face,
             * TextOnAccentFillColorDisabled content */
            tk_color want = blend(t->accent_dis, t->bg);
            CHECK(col_near_c(px(c, q.x + 6, q.y + q.h / 2),
                             want, 3));
            tk_color gwant = blend(t->on_accent_dis, want);
            CHECK(count_near(c, q, gwant, 24) > 4);
        }
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);

        /* OUTLINED pressed + TEXT disabled transparency */
        tk_widget *o = tk_button_new("枠");
        tk_button_set_variant(o, TK_BTN_OUTLINED);
        r = root_with(o);
        q = tk_widget_rect(o);
        send_ptr(r, q.x + q.w / 2, q.y + q.h / 2, true);
        c = paint_full(r);
        if (t->style == 1) {
            tk_color want = blend(with_a(t->accent, 26), t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 4),
                             want, 3));
        } else {
            tk_color want = blend(t->control_fill3, t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 4),
                             want, 3));
        }
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, q.y + q.h / 2, false);
        tk_root_free(r);
        tk_widget_free(o);

        tk_widget *tb = tk_button_new("字");
        tk_button_set_variant(tb, TK_BTN_TEXT);
        tk_set_enabled(tb, false);
        r = root_with(tb);
        q = tk_widget_rect(tb);
        c = paint_full(r);
        /* a text button has no container in any state: the
         * interior stays the page colour */
        CHECK(col_eq(px(c, q.x + 4, q.y + 4), t->bg));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tb);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- B. Checkbox / Radio states, both modes ------ */
static void test_check_radio_states(void)
{
    /* --- M3 (style 1) --- */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *cb = tk_checkbox_new("項目", true);
        tk_root *r = root_with(cb);
        tk_rect q = tk_widget_rect(cb);
        /* M22: probe the real 21px painted box; the glyph
         * probe sits on the long arm of the material3-ratio
         * check path (arm midpoint in box coordinates) */
        tk_rect box = { q.x, q.y + (q.h - 21) / 2, 21, 21 };
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, box.x + 12, box.y + 10),
                     t->on_accent));
        tk_canvas_free(c);
        /* disabled checked: the 38% container form */
        tk_set_enabled(cb, false);
        c = paint_full(r);
        tk_color face = blend(with_a(t->text, 97), t->bg);
        CHECK(col_near_c(px(c, box.x + 2, box.y + 2), face, 3));
        CHECK(col_eq(px(c, box.x + 12, box.y + 10), t->surface));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(cb);
        /* disabled unchecked: surface box, 38% outline */
        cb = tk_checkbox_new("項目", false);
        r = root_with(cb);
        q = tk_widget_rect(cb);
        box = (tk_rect){ q.x, q.y + (q.h - 21) / 2, 21, 21 };
        tk_set_enabled(cb, false);
        c = paint_full(r);
        CHECK(col_eq(px(c, box.x + 10, box.y + 10), t->surface));
        /* the outline is painted over the box's own surface
         * fill */
        CHECK(col_near_c(px(c, box.x, box.y + 10),
                         blend(with_a(t->text, 97), t->surface),
                         4));
        tk_canvas_free(c);
        tk_set_enabled(cb, true);
        check_focus_ring(r, cb);
        tk_root_free(r);
        tk_widget_free(cb);
        /* radio disabled checked: ring + dot at 38% */
        tk_widget *rd = tk_radio_new("項目", 1, 1);
        tk_radio_select(rd);
        r = root_with(rd);
        q = tk_widget_rect(rd);
        tk_rect rbox = { q.x, q.y + (q.h - 18) / 2, 18, 18 };
        tk_set_enabled(rd, false);
        c = paint_full(r);
        tk_color dis = with_a(t->text, 97);
        /* the dot is the 38% form over the surface disc */
        CHECK(col_near_c(px(c, rbox.x + 9, rbox.y + 9),
                         blend(dis, t->surface), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(rd);
    }
    /* --- WinUI (style 2) --- */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *cb = tk_checkbox_new("項目", false);
        tk_root *r = root_with(cb);
        tk_rect q = tk_widget_rect(cb);
        tk_rect box = { q.x, q.y + (q.h - 28) / 2, 28, 28 };
        tk_color face = blend(t->control_fill, t->bg);
        tk_canvas *c = paint_full(r);
        CHECK(col_near_c(px(c, box.x + 14, box.y + 14), face, 3));
        /* the unchecked outline is the STRONG stroke */
        CHECK(col_near_c(px(c, box.x + 14, box.y),
                         blend(t->strong_stroke, face), 4));
        tk_canvas_free(c);
        /* disabled: ControlFillColorDisabled in both states,
         * the glyph in TextFillColorDisabled */
        tk_set_enabled(cb, false);
        c = paint_full(r);
        tk_color dface = blend(t->control_fill_dis, t->bg);
        CHECK(col_near_c(px(c, box.x + 14, box.y + 14),
                         dface, 3));
        tk_canvas_free(c);
        tk_checkbox_set_checked(cb, true);
        c = paint_full(r);
        CHECK(col_near_c(px(c, box.x + 3, box.y + 3), dface, 3));
        tk_color gwant = blend(t->text_dis, dface);
        CHECK(count_near(c, box, gwant, 24) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(cb);
        /* radio: strong ring, disabled strong pair */
        tk_widget *rd = tk_radio_new("項目", 1, 1);
        tk_radio_select(rd);
        r = root_with(rd);
        q = tk_widget_rect(rd);
        tk_rect rbox = { q.x, q.y + (q.h - 28) / 2, 28, 28 };
        c = paint_full(r);
        CHECK(col_eq(px(c, rbox.x + 14, rbox.y + 14),
                     t->accent));
        tk_canvas_free(c);
        tk_set_enabled(rd, false);
        c = paint_full(r);
        CHECK(col_near_c(px(c, rbox.x + 14, rbox.y + 14),
                         blend(t->strong_fill_dis, dface), 4));
        CHECK(col_near_c(px(c, rbox.x + 14, rbox.y),
                         blend(t->strong_stroke_dis, dface), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(rd);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- C. WinUI Switch state forms (light + dark) -- */
static void test_switch_winui_states(void)
{
    const tk_theme *themes[2] = { &tk_theme_winui,
                                  &tk_theme_winui_dark };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *sw = tk_switch_new("項目", false);
        tk_root *r = root_with(sw);
        tk_rect q = tk_widget_rect(sw);
        int cy = q.y + q.h / 2;
        tk_color tf = blend(t->control_fill, t->bg);
        tk_canvas *c = paint_full(r);
        /* OFF enabled: strong outline + strong knob */
        CHECK(col_near_c(px(c, q.x + 28, cy - 13),
                         blend(t->strong_stroke, tf), 4));
        CHECK(col_near_c(px(c, q.x + 14, cy),
                         blend(t->strong_fill, tf), 4));
        tk_canvas_free(c);
        /* OFF disabled: the disabled strong pair */
        tk_set_enabled(sw, false);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + 28, cy - 13),
                         blend(t->strong_stroke_dis, tf), 4));
        CHECK(col_near_c(px(c, q.x + 14, cy),
                         blend(t->strong_fill_dis, tf), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sw);
        /* ON disabled: ControlFillColorDisabled track */
        sw = tk_switch_new("項目", true);
        r = root_with(sw);
        q = tk_widget_rect(sw);
        cy = q.y + q.h / 2;
        tk_set_enabled(sw, false);
        c = paint_full(r);
        tk_color df = blend(t->control_fill_dis, t->bg);
        CHECK(col_near_c(px(c, q.x + 52, cy), df, 3)); /* M22: 54px track */
        CHECK(col_near_c(px(c, q.x + 42, cy),
                         blend(t->strong_fill_dis, df), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sw);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D. Slider disabled forms, both modes -------- */
static void test_slider_states(void)
{
    /* M3: active 38%, inactive 12%, thumb 38% */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *s = tk_slider_new(0.5f);
        tk_root *r = root_with(s);
        tk_rect q = tk_widget_rect(s);
        int cy = q.y + q.h / 2;
        tk_set_enabled(s, false);
        tk_canvas *c = paint_full(r);
        tk_color ina = blend(with_a(t->text, 31), t->bg);
        /* M21: the spans are adjacent segments, not layers
         * - the active span composites over the page, not
         * over the inactive span's 12% */
        tk_color act = blend(with_a(t->text, 97), t->bg);
        CHECK(col_near_c(px(c, q.x + q.w / 4, cy), act, 4));
        CHECK(col_near_c(px(c, q.x + 3 * q.w / 4, cy), ina, 4));
        /* the thumb is the 38% form over the active span
         * (probed left of centre: the centre pixel itself
         * rides on the inactive side of the boundary) */
        CHECK(col_near_c(px(c, q.x + q.w / 2 - 7, cy),
                         blend(with_a(t->text, 97), act), 5));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
        /* Range form: the span between the handles only */
        s = tk_slider_new(0.75f);
        tk_slider_set_values2(s, 0.25f, 0.75f);
        tk_set_enabled(s, false);
        r = root_with(s);
        q = tk_widget_rect(s);
        cy = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w / 2, cy), act, 4));
        CHECK(col_near_c(px(c, q.x + q.w / 8, cy), ina, 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
    }
    /* WinUI: both spans and the thumb take the
     * strong-fill disabled gray (M21: the M10 form took
     * ControlFillColorDisabled, which was invisible on
     * the page background) */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *s = tk_slider_new(0.5f);
        tk_root *r = root_with(s);
        tk_rect q = tk_widget_rect(s);
        int cy = q.y + q.h / 2;
        tk_set_enabled(s, false);
        tk_canvas *c = paint_full(r);
        tk_color df = blend(t->strong_fill_dis, t->bg);
        CHECK(col_near_c(px(c, q.x + q.w / 4, cy), df, 3));
        CHECK(col_near_c(px(c, q.x + 3 * q.w / 4, cy), df, 3));
        CHECK(col_near_c(px(c, q.x + q.w / 2, cy),
                         blend(t->strong_fill_dis, df), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- E. TextField states, both modes ------------- */
static bool tf_bad_cb(tk_widget *field, const char *text,
                      void *ud)
{
    (void)field; (void)text; (void)ud;
    return false;
}
static void test_textfield_states(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, &tk_theme_winui };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *f = tk_textfield_new("");
        tk_textfield_set_placeholder(f, "名前");
        tk_root *r = root_with(f);
        tk_rect q = tk_widget_rect(f);
        tk_canvas *c = paint_full(r);
        /* placeholder: present, at the dim role, never at
         * full text strength */
        tk_rect zone = { q.x, q.y, 140, q.h };
        if (!t->dark) {
            CHECK(count_near(c, zone, t->text, 8) == 0);
            CHECK(count_near(c, zone, blend(t->text_dim,
                                            t->style == 2
                                                ? blend(t->control_fill, t->bg)
                                                : t->surface_alt),
                             24) > 4);
        } else {
            CHECK(count_near(c, zone, t->text, 8) == 0);
        }
        tk_canvas_free(c);
        /* disabled face */
        tk_set_enabled(f, false);
        c = paint_full(r);
        if (t->style == 1) {
            tk_color want = blend(with_a(t->text, 31), t->bg);
            CHECK(col_near_c(px(c, q.x + q.w - 8, q.y + q.h / 2),
                             want, 3));
        } else {
            tk_color want = blend(t->control_fill_dis, t->bg);
            CHECK(col_near_c(px(c, q.x + q.w - 8, q.y + q.h / 2),
                             want, 3));
        }
        tk_canvas_free(c);
        tk_set_enabled(f, true);
        check_focus_ring_x(r, f, true);
        tk_root_free(r);
        tk_widget_free(f);
        /* error state: the danger role marks the field */
        f = tk_textfield_new("");
        tk_textfield_set_validator(f, tf_bad_cb, NULL);
        tk_textfield_set_error_message(f, "不正です");
        tk_textfield_set_text(f, "x");
        tk_textfield_validate(f);
        r = root_with(f);
        q = tk_widget_rect(f);
        c = paint_full(r);
        /* scan the field's frame band for the danger colour */
        tk_rect band = { q.x, q.y, q.w, q.h + 24 };
        CHECK(count_near(c, band, t->danger, 6) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(f);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- F. Chip states, both modes ------------------ */
static void test_chip_states(void)
{
    /* M3 filter chip */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
        tk_chip_set_selected(ch, true);
        tk_root *r = root_with(ch);
        tk_rect q = tk_widget_rect(ch);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 6, q.y + q.h / 2),
                     t->container));
        /* the check strokes are the accent */
        tk_rect lz = { q.x, q.y, 30, q.h };
        CHECK(count_near(c, lz, t->accent, 8) > 2);
        tk_canvas_free(c);
        /* disabled selected: 12% container, 38% content */
        tk_set_enabled(ch, false);
        c = paint_full(r);
        tk_color face = blend(with_a(t->text, 31), t->bg);
        CHECK(col_near_c(px(c, q.x + q.w - 6, q.y + q.h / 2),
                         face, 3));
        tk_color content = blend(with_a(t->text, 97), face);
        CHECK(count_near(c, q, content, 24) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ch);
        /* disabled unselected: no fill, 12% outline */
        ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
        tk_set_enabled(ch, false);
        r = root_with(ch);
        q = tk_widget_rect(ch);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 6, q.y + q.h / 2),
                     t->bg));
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                         blend(with_a(t->text, 31), t->bg), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ch);
    }
    /* WinUI chip (the ToggleButton form) */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
        tk_chip_set_selected(ch, true);
        tk_set_enabled(ch, false);
        tk_root *r = root_with(ch);
        tk_rect q = tk_widget_rect(ch);
        tk_canvas *c = paint_full(r);
        /* M11 correction: the ToggleButton form's CHECKED
         * disabled pair is AccentFillColorDisabled +
         * TextOnAccentFillColorDisabled (themeresources,
         * fetched 2026-11-20); M10 pinned the unchecked pair
         * here by mistake */
        tk_color face = blend(t->accent_dis, t->bg);
        CHECK(col_near_c(px(c, q.x + q.w - 6, q.y + q.h / 2),
                         face, 3));
        tk_color content = blend(t->on_accent_dis, face);
        CHECK(count_near(c, q, content, 24) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ch);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- G. ListView states, both modes -------------- */
static void test_listview_states(void)
{
    const char *items[] = { "甲", "乙", "丙" };
    /* WinUI: subtle selection + accent bar, surface_alt mark */
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *lv = tk_listview_new();
        tk_listview_set_items(lv, items, 3);
        tk_listview_select(lv, 1);
        tk_listview_set_marked(lv, 2);
        tk_root *r = root_with(lv);
        tk_rect q = tk_widget_rect(lv);
        int rh = t->row_h;
        tk_canvas *c = paint_full(r);
        tk_color selfill = blend(t->subtle, t->surface);
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + rh + rh / 2),
                         selfill, 3));
        CHECK(col_eq(px(c, q.x + 1, q.y + rh + rh / 2),
                     t->accent));
        CHECK(col_eq(px(c, q.x + q.w / 2, q.y + 2 * rh + rh / 2),
                     t->surface_alt));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(lv);
    }
    /* M3: selection pill, header row, checkable rows,
     * disabled text */
    tk_theme_set_default(&tk_theme_v2);
    t = tk_theme_default();
    {
        tk_widget *lv = tk_listview_new();
        tk_listview_set_items(lv, items, 3);
        tk_listview_select(lv, 0);
        tk_root *r = root_with(lv);
        tk_rect q = tk_widget_rect(lv);
        int rh = t->row_h;
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 2 * t->unit, q.y + rh / 2),
                     t->selection));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(lv);
        /* checkable: the checked row's box is the accent */
        lv = tk_listview_new();
        tk_listview_set_items(lv, items, 3);
        tk_listview_set_checkable(lv, true);
        tk_listview_set_row_checked(lv, 0, true);
        r = root_with(lv);
        q = tk_widget_rect(lv);
        c = paint_full(r);
        tk_rect boxzone = { q.x + 2 * t->unit, q.y, 18, rh };
        CHECK(count_near(c, boxzone, t->accent, 6) > 20);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(lv);
        /* header rows read in the dim caption role and never
         * take the selection fill */
        lv = tk_listview_new();
        tk_listview_set_items(lv, items, 3);
        tk_listview_set_header(lv, 0, true);
        tk_listview_select(lv, 0);
        r = root_with(lv);
        q = tk_widget_rect(lv);
        c = paint_full(r);
        tk_rect hrow = { q.x, q.y, q.w, rh };
        CHECK(count_near(c, hrow, t->selection, 4) == 0);
        CHECK(count_near(c, hrow, t->text_dim, 16) > 4);
        tk_canvas_free(c);
        /* disabled: item text drops to the dim role */
        tk_set_enabled(lv, false);
        c = paint_full(r);
        tk_rect row1 = { q.x, q.y + rh, q.w, rh };
        CHECK(count_near(c, row1, t->text, 8) == 0);
        CHECK(count_near(c, row1, t->text_dim, 16) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(lv);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- H. Menu states, both modes ------------------ */
static void test_menu_states(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, &tk_theme_winui };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_widget *mn = tk_menu_new();
        tk_menu_add_item(mn, "有効な項目", 1);
        tk_menu_add_item(mn, "無効な項目", 2);
        tk_menu_add_check_item(mn, "印付き項目", 3, true);
        tk_menu_set_item_enabled(mn, 2, false);
        tk_menu_popup(r, mn, 100, 100);
        tk_root_layout(r, 800, 480);
        tk_rect q = tk_widget_rect(mn);
        int ih = q.h / 3;
        tk_canvas *c = paint_full(r);
        /* menu face (probed inside the bottom-left, clear
         * of the item text and the top border): the WinUI
         * flyout face is the card background role */
        tk_color face = t->style == 2
            ? blend(t->card_bg, t->bg) : t->surface;
        /* probed at the disabled row's right end: pure face
         * (no wash, no text, no corner arc) */
        CHECK(col_near_c(px(c, q.x + q.w - 6, q.y + ih + ih / 2),
                         face, 3));
        /* disabled item: no full-strength text pixels in its
         * row; the disabled role's pixels are present */
        tk_rect drow = { q.x, q.y + ih, q.w, ih };
        CHECK(count_near(c, drow, t->text, 8) == 0);
        tk_color disrole = t->style == 2
            ? blend(t->text_dis, face) : t->text_dim;
        CHECK(count_near(c, drow, disrole, 24) > 4);
        /* checked item: the check glyph at the left (M21:
         * the pre-M21 form was a filled accent square; the
         * M3 run of the old accent pin in fact counted the
         * focus ring, not the mark). At rest the WinUI
         * glyph takes the secondary text brush (the
         * template's MenuFlyoutSubItemChevron); the M3
         * glyph takes the content colour. */
        tk_rect crow = { q.x, q.y + 2 * ih, 30, ih };
        tk_color rest_glyph = t->style == 2
            ? blend(t->text_dim, face) : t->text;
        CHECK(count_near(c, crow, rest_glyph, 8) > 2);
        /* hover (the touch-mode stand-in): the flyout opens
         * with the first item washed; moving the pointer to
         * the third item moves the wash with it */
        tk_color wash = t->style == 2
            ? blend(t->subtle, face) : t->selection;
        CHECK(col_near_c(px(c, q.x + q.w - 30, q.y + ih / 2),
                         wash, 3));
        tk_canvas_free(c);
        send_move(r, q.x + q.w / 2, q.y + 2 * ih + ih / 2);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w - 30, q.y + ih / 2),
                         face, 3));
        CHECK(col_near_c(px(c, q.x + q.w - 30,
                            q.y + 2 * ih + ih / 2), wash, 3));
        /* M21: on the hover row the WinUI check glyph
         * switches to the item foreground (the template's
         * PointerOver state re-colours CheckGlyph with the
         * item foreground brush), rendered over the row
         * wash; M3 is the content colour in both states */
        tk_color hov_glyph = t->style == 2
            ? blend(t->text, wash) : t->text;
        CHECK(count_near(c, crow, hov_glyph, 8) > 2);
        tk_canvas_free(c);
        tk_menu_close(r, mn);
        tk_root_free(r);
        tk_widget_free(host);
        tk_widget_free(mn);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- I. TabBar / BottomBar / Segmented ----------- */
static void test_tab_family(void)
{
    /* TabBar, M3: the active segment carries the accent
     * (label + indicator), inactive segments the dim role */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *tb = tk_tabbar_new();
        tk_tabbar_add(tb, "概要");
        tk_tabbar_add(tb, "詳細");
        tk_tabbar_add(tb, "履歴");
        tk_root *r = root_with(tb);
        tk_rect q = tk_widget_rect(tb);
        tk_canvas *c = paint_full(r);
        int seg = q.w / 3;
        tk_rect t0 = { q.x, q.y, seg, q.h };
        tk_rect t1 = { q.x + seg, q.y, seg, q.h };
        CHECK(count_near(c, t0, t->accent, 8) > 2);
        CHECK(count_near(c, t1, t->accent, 8) == 0);
        CHECK(count_near(c, t1, t->text_dim, 16) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tb);
    }
    /* TabBar, WinUI light + dark: the active tab is raised
     * to the surface colour over the layer strip */
    const tk_theme *themes[2] = { &tk_theme_winui,
                                  &tk_theme_winui_dark };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        t = tk_theme_default();
        tk_widget *tb = tk_tabbar_new();
        tk_tabbar_add(tb, "概要");
        tk_tabbar_add(tb, "詳細");
        tk_root *r = root_with(tb);
        tk_rect q = tk_widget_rect(tb);
        tk_canvas *c = paint_full(r);
        int seg = q.w / 2;
        CHECK(col_eq(px(c, q.x + 4, q.y + 3), t->surface));
        CHECK(col_near_c(px(c, q.x + seg + 4, q.y + 3),
                         blend(t->layer, t->bg), 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tb);
        /* Segmented: the selected segment is the selection
         * colour in both WinUI themes */
        tk_widget *sg = tk_segmented_new(false);
        tk_segmented_add(sg, "日");
        tk_segmented_add(sg, "週");
        tk_segmented_add(sg, "月");
        tk_segmented_select(sg, 1);
        r = root_with(sg);
        q = tk_widget_rect(sg);
        c = paint_full(r);
        int sw3 = q.w / 3;
        CHECK(col_eq(px(c, q.x + sw3 + sw3 / 2, q.y + q.h / 2),
                     t->accent));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sg);
    }
    /* Segmented, M3: same selection role */
    tk_theme_set_default(&tk_theme_v2);
    t = tk_theme_default();
    {
        tk_widget *sg = tk_segmented_new(false);
        tk_segmented_add(sg, "日");
        tk_segmented_add(sg, "週");
        tk_segmented_select(sg, 0);
        tk_root *r = root_with(sg);
        tk_rect q = tk_widget_rect(sg);
        tk_canvas *c = paint_full(r);
        /* the M3 selected container role is the theme's
         * container token */
        CHECK(col_eq(px(c, q.x + q.w / 4, q.y + q.h / 2),
                     t->container));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sg);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- J. Date / Time picker segment states -------- */
static void test_picker_states(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, &tk_theme_winui };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *dp = tk_datepicker_new(2026, 11, 20);
        tk_root *r = root_with(dp);
        tk_rect q = tk_widget_rect(dp);
        tk_canvas *c = paint_full(r);
        /* segments size to their content (the year segment
         * is the widest): the active segment's interior at
         * its left edge takes the selection colour */
        int cy = q.y + q.h / 2;
        CHECK(col_near_c(px(c, q.x + 6, cy),
                         blend(t->selection, t->bg), 3));
        /* the first inactive segment starts where the
         * surface_alt run begins on the centre line */
        int altx = -1;
        for (int x = q.x + 8; x < q.x + q.w; x++)
            if (col_eq(px(c, x, cy), t->surface_alt)) {
                altx = x; break;
            }
        CHECK(altx > q.x + 40);
        /* the active segment's stepper triangles sit just
         * left of that boundary, above and below centre */
        tk_rect sz = { altx - 22, q.y, 20, q.h };
        CHECK(count_near(c, sz, t->text, 32) > 0);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(dp);
        tk_widget *tp = tk_timepicker_new(false);
        r = root_with(tp);
        q = tk_widget_rect(tp);
        c = paint_full(r);
        cy = q.y + q.h / 2;
        CHECK(col_near_c(px(c, q.x + 6, cy),
                         blend(t->selection, t->bg), 3));
        altx = -1;
        for (int x = q.x + 8; x < q.x + q.w; x++)
            if (col_eq(px(c, x, cy), t->surface_alt)) {
                altx = x; break;
            }
        CHECK(altx > q.x + 40);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tp);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- K. Toast + Snackbar severities -------------- */
static void test_transient_states(void)
{
    /* Toast, M3: inverse surface with inverse content */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_toast_show(r, "保存しました", 60000);
        tk_canvas *c = paint_full(r);
        tk_rect strip = { 0, 480 - 140, 800, 140 };
        CHECK(count_near(c, strip, t->inverse_surface, 4) > 50);
        CHECK(count_near(c, strip, t->inverse_on_surface,
                         16) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(host);
    }
    /* Snackbar, WinUI: all four severity roles */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_rect probe = { 16, 480 - 69 - 16, 560, 69 };
        const struct { int sev; tk_color bg; tk_color disc; }
        cases[4] = {
            { TK_SNACK_SUCCESS, t->sys_ok_bg, t->sys_ok },
            { TK_SNACK_WARNING, t->sys_warn_bg, t->sys_warn },
            { TK_SNACK_ERROR, t->sys_err_bg,  t->danger },
            { TK_SNACK_INFO, t->sys_attn_bg, t->sys_attn },
        };
        for (int i = 0; i < 4; i++) {
            tk_snackbar_show_sev(r, "通知", cases[i].sev,
                                 NULL, NULL, NULL, 0);
            tk_canvas *c = paint_full(r);
            CHECK(col_eq(px(c, probe.x + 34 - 6,
                            probe.y + probe.h / 2 - 6),
                         cases[i].disc));
            tk_color bgwant = cases[i].bg.a == 255
                ? cases[i].bg : blend(cases[i].bg, t->bg);
            CHECK(col_near_c(px(c, probe.x + 120,
                                probe.y + probe.h - 6),
                             bgwant, 3));
            tk_canvas_free(c);
            tk_snackbar_hide(r);
        }
        tk_root_free(r);
        tk_widget_free(host);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- L. WinUI card / dialog / drawer / sheet ----- */
static void test_winui_surfaces(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *cd = tk_card_new(TK_CARD_ELEVATED);
        tk_root *r = root_with(cd);
        tk_rect q = tk_widget_rect(cd);
        tk_canvas *c = paint_full(r);
        tk_color face = blend(t->card_bg, t->bg);
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + q.h / 2),
                         face, 3));
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                         blend(t->control_stroke, face), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(cd);
    }
    /* modeless + fullscreen dialog faces in the dark theme */
    tk_theme_set_default(&tk_theme_winui_dark);
    t = tk_theme_default();
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_widget *d = tk_dialog_new("確認");
        tk_dialog_show_modeless(r, d);
        tk_root_layout(r, 800, 480);
        tk_rect q = tk_widget_rect(d);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w / 2, q.y + q.h - 6),
                     t->surface));
        tk_canvas_free(c);
        tk_dialog_close(r, d);
        tk_widget *d2 = tk_dialog_new("全画面");
        tk_dialog_set_fullscreen(d2, true);
        tk_dialog_show(r, d2);
        tk_root_layout(r, 800, 480);
        c = paint_full(r);
        CHECK(col_eq(px(c, 400, 470), t->surface));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(host);
    }
    /* drawer + sheet faces, dark theme */
    {
        tk_theme_set_default(&tk_theme_winui_dark);
        t = tk_theme_default();
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_widget *dr = tk_drawer_new("見出し");
        tk_drawer_show(r, dr, true);
        tk_root_layout(r, 800, 480);
        tk_rect q = tk_widget_rect(dr);
        tk_canvas *c = paint_full(r);
        tk_color smoke = t->smoke.a ? t->smoke
                                    : (tk_color){ 0, 0, 0, 77 };
        tk_color dwant = blend(t->layer, blend(smoke, t->bg));
        CHECK(col_near_c(px(c, q.x + 24, q.y + q.h - 12),
                         dwant, 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(host);
        host = tk_label_new("主");
        r = root_with(host);
        tk_widget *sh = tk_sheet_new();
        tk_sheet_show(r, sh, true);
        tk_root_layout(r, 800, 480);
        q = tk_widget_rect(sh);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + q.h - 12),
                         blend(t->layer, t->bg), 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(host);
    }
    /* carousel page dots, WinUI */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        static const uint32_t dot8[64] = { 0 };
        tk_widget *car = tk_carousel_new();
        tk_carousel_add(car, "一", dot8, 8, 8);
        tk_carousel_add(car, "二", dot8, 8, 8);
        tk_carousel_add(car, "三", dot8, 8, 8);
        tk_root *r = root_with(car);
        tk_rect q = tk_widget_rect(car);
        tk_canvas *c = paint_full(r);
        tk_color tile = t->container_high.a
            ? t->container_high : t->surface_alt;
        tk_color tilepx = tile.a == 255 ? tile
                                        : blend(tile, t->bg);
        CHECK(col_near_c(px(c, q.x + 40, q.y + 20), tilepx,
                         3));
        tk_color band = { 0, 0, 0, 110 };
        CHECK(col_near_c(px(c, q.x + 40, q.y + q.h - 17),
                         blend(band, tilepx), 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(car);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- M. WinUI tree / textview / appbar ----------- */
static void test_winui_browse_surfaces(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *tv = tk_treeview_new();
        int a = tk_treeview_add(tv, -1, "親");
        tk_treeview_add(tv, a, "子");
        tk_treeview_select(tv, a);
        tk_root *r = root_with(tv);
        tk_rect q = tk_widget_rect(tv);
        tk_canvas *c = paint_full(r);
        tk_color face1 = px(c, q.x + q.w - 4,
                            q.y + t->row_h + t->row_h / 2);
        tk_color sel0 = px(c, q.x + q.w - 4,
                           q.y + t->row_h / 2);
        CHECK(!col_eq(sel0, face1));
        CHECK(col_near_c(sel0, blend(t->selection, face1),
                         3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tv);
    }
    {
        tk_widget *tv = tk_textview_new("本文");
        tk_root *r = root_with(tv);
        tk_rect q = tk_widget_rect(tv);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 12, q.y + q.h - 12),
                     t->surface));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tv);
    }
    {
        tk_widget *ab = tk_appbar_new("題名");
        tk_root *r = root_with(ab);
        tk_rect q = tk_widget_rect(ab);
        tk_canvas *c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w - 10, q.y + 4),
                         blend(t->layer, t->bg), 3));
        tk_rect tz = { q.x, q.y, 200, q.h };
        CHECK(count_near(c, tz, t->text, 16) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ab);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- N. Badge + FAB menu ------------------------- */
static void test_badge_fabmenu(void)
{
    tk_theme_set_default(&tk_theme_v2);
    {
        tk_widget *bd = tk_badge_new(3);
        tk_root *r = root_with(bd);
        tk_rect q = tk_widget_rect(bd);
        tk_canvas *c = paint_full(r);
        tk_color err = { 186, 26, 26, 255 };
        CHECK(count_near(c, q, err, 6) > 10);
        tk_color white = { 255, 255, 255, 255 };
        CHECK(count_near(c, q, white, 8) > 0);
        tk_canvas_free(c);
        tk_badge_set_count(bd, 0);
        c = paint_full(r);
        CHECK(count_near(c, q, err, 6) == 0);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(bd);
    }
    /* FAB menu items in the WinUI mode: surface faces */
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    {
        static const uint32_t ic[64] = { 0 };
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_widget *fm = tk_fabmenu_new();
        tk_fabmenu_add_item(fm, ic, 8, 8, "追加", NULL, NULL);
        tk_fabmenu_add_item(fm, ic, 8, 8, "編集", NULL, NULL);
        tk_fabmenu_show(r, fm);
        tk_root_layout(r, 800, 480);
        tk_canvas *c = paint_full(r);
        tk_rect zone = { 800 - 260, 480 - 320, 260, 320 };
        CHECK(count_near(c, zone, t->surface, 4) > 100);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(host);
        tk_widget_free(fm);
    }
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    /* glyph pixel checks need the real font installed
     * (without one, draw_text paints nothing by design -
     * see tk.h; same contract as test_tk7) */
    tk_font *f = tk_font_load_bundled();
    CHECK(f != NULL);
    if (f) tk_font_install(f);
    test_button_states();
    test_check_radio_states();
    test_switch_winui_states();
    test_slider_states();
    test_textfield_states();
    test_chip_states();
    test_listview_states();
    test_menu_states();
    test_tab_family();
    test_picker_states();
    test_transient_states();
    test_winui_surfaces();
    test_winui_browse_surfaces();
    test_badge_fabmenu();
    printf("tk9: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
