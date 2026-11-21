/* test_tk10.c - M11 state-pixel coverage, second wave.
 *
 * tk9 closed the first B-viewpoint tranche (FILLED/OUTLINED/
 * TEXT button forms, checkbox/radio/slider/textfield/chip
 * disabled forms). This suite takes the items REPORT-m10 SS4
 * left as "implemented, pixel-unverified" and the next tranche
 * of partial cells:
 *
 *   A. Button TONAL / ELEVATED pressed + disabled forms in
 *      both modes, plus the OUTLINED disabled outline and the
 *      TEXT pressed layer (completing row 1's state set).
 *   B. Toggle buttons: the WinUI checked forms (the M11 fix -
 *      ToggleButton themeresources, fetched 2026-11-20:
 *      checked = AccentFillColorDefault, checked pressed =
 *      AccentFillColorTertiary, checked disabled =
 *      AccentFillColorDisabled + TextOnAccentFillColor-
 *      Disabled) and the M3 per-variant toggle forms (buttons
 *      spec, fetched 2026-11-20).
 *   C. Radio unselected + disabled rings, both modes.
 *   D. TextField error geometry at exact pixels: the M3
 *      filled field's error underline span and the WinUI
 *      error outline, both derived from the painted canvas.
 *   E. Chip pressed forms, both modes.
 *   F. Progress determinate fill / track roles, both modes.
 *   G. Segmented (WinUI): unselected interior, the divider
 *      between unselected segments, the container outline.
 *   H. Card (M3): the three variant faces.
 *   I. Slider Range disabled forms (WinUI spans + thumbs,
 *      M3 thumbs).
 *   J. Toast in the WinUI mode (inverse pairing).
 *   K. SearchBar faces + placeholder / populated forms.
 *   L. Dialog (basic, modal): the M11 scrim + the face, both
 *      modes; TextView selection highlight, both modes.
 *
 * Expected values are computed from the theme tokens with
 * the canvas's own blend arithmetic, as in test_tk9. */
#include "tk.h"
#include "textcore.h"

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

/* M12: TK10_DARK=1 re-runs the whole suite with every WinUI
 * selection substituted by tk_theme_winui_dark - the formal
 * dark re-run M11 left undone (same mechanism as tk7's
 * TK7_DARK). tk10_set is defined BEFORE the macro so its own
 * call reaches the real function; the themes[] arrays route
 * through tk10_map directly. Expected values in this suite
 * are computed from the theme tokens, so they follow the
 * substitution. */
static void tk10_set(const tk_theme *t) { tk_theme_set_default(t); }
static const tk_theme *tk10_map(const tk_theme *t)
{
    if (t == &tk_theme_winui && getenv("TK10_DARK"))
        return &tk_theme_winui_dark;
    return t;
}
#define tk_theme_set_default(t) tk10_set(tk10_map(t))

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
static void send_ptr(tk_root *r, int x, int y, bool down)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = down ? TK_EV_POINTER_DOWN : TK_EV_POINTER_UP;
    ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}

/* ---------- A. TONAL / ELEVATED + remaining row-1 forms - */
static void test_button_variants2(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, tk10_map(&tk_theme_winui) };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        /* --- TONAL: rest / pressed / disabled --- */
        tk_widget *b = tk_button_new("濃淡");
        tk_button_set_variant(b, TK_BTN_TONAL);
        tk_root *r = root_with(b);
        tk_rect q = tk_widget_rect(b);
        int my = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        if (t->style == 1)
            CHECK(col_eq(px(c, q.x + 6, my), t->container));
        else
            CHECK(col_near_c(px(c, q.x + 6, my),
                             blend(t->subtle, t->bg), 3));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        if (t->style == 1) {
            /* pressed: the content (on-surface) state layer
             * at 10% over the tonal container */
            tk_color want = blend(with_a(t->text, 26),
                                  t->container);
            CHECK(col_near_c(px(c, q.x + 6, my), want, 3));
        } else {
            tk_color want = blend(t->control_fill3, t->bg);
            CHECK(col_near_c(px(c, q.x + 6, my), want, 3));
        }
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, false);
        tk_set_enabled(b, false);
        c = paint_full(r);
        if (t->style == 1) {
            tk_color face = blend(with_a(t->text, 31), t->bg);
            CHECK(col_near_c(px(c, q.x + 6, my), face, 3));
            tk_color gwant = blend(with_a(t->text, 97), face);
            CHECK(count_near(c, q, gwant, 24) > 4);
        } else {
            tk_color face = blend(t->control_fill_dis, t->bg);
            CHECK(col_near_c(px(c, q.x + 6, my), face, 3));
            tk_color gwant = blend(t->text_dis, face);
            CHECK(count_near(c, q, gwant, 24) > 4);
        }
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);

        /* --- ELEVATED: rest / pressed / disabled --- */
        b = tk_button_new("浮上");
        tk_button_set_variant(b, TK_BTN_ELEVATED);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        if (t->style == 1) {
            /* probed on the top row: the shadow silhouette
             * starts 2px lower, so this is face over page */
            tk_color want = blend(t->container_low, t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 1),
                             want, 3));
            /* the shadow itself: black at alpha 40 over the
             * page, peeking out below the face */
            tk_color sh = { 0, 0, 0, 40 };
            CHECK(col_near_c(px(c, q.x + q.w / 2,
                                q.y + q.h + 1),
                             blend(sh, t->bg), 3));
        } else {
            tk_color want = blend(t->control_fill, t->bg);
            CHECK(col_near_c(px(c, q.x + 6, my), want, 3));
            /* the standard button's stroke on its top edge */
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                             blend(t->control_stroke, want), 4));
        }
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        if (t->style == 1) {
            /* pressed: the primary state layer at 10% over
             * the low container */
            tk_color want = blend(with_a(t->accent, 26),
                                  blend(t->container_low,
                                        t->bg));
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 1),
                             want, 3));
        } else {
            tk_color want = blend(t->control_fill3, t->bg);
            CHECK(col_near_c(px(c, q.x + 6, my), want, 3));
        }
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, false);
        tk_set_enabled(b, false);
        c = paint_full(r);
        if (t->style == 1) {
            tk_color face = blend(with_a(t->text, 31), t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 1),
                             face, 3));
            tk_color gwant = blend(with_a(t->text, 97), face);
            CHECK(count_near(c, q, gwant, 24) > 4);
        } else {
            tk_color face = blend(t->control_fill_dis, t->bg);
            CHECK(col_near_c(px(c, q.x + 6, my), face, 3));
            tk_color gwant = blend(t->text_dis, face);
            CHECK(count_near(c, q, gwant, 24) > 4);
        }
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);

        /* --- OUTLINED disabled outline --- */
        b = tk_button_new("枠線");
        tk_button_set_variant(b, TK_BTN_OUTLINED);
        tk_set_enabled(b, false);
        r = root_with(b);
        q = tk_widget_rect(b);
        c = paint_full(r);
        if (t->style == 1) {
            /* the disabled outline is on-surface at the 12%
             * container opacity, over the page */
            tk_color want = blend(with_a(t->text, 31), t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                             want, 4));
        } else {
            tk_color want = blend(t->control_stroke, t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                             want, 4));
        }
        /* no container fill in the disabled outlined form
         * (probed inside the left edge, clear of the
         * centred label) */
        CHECK(col_eq(px(c, q.x + 6, q.y + q.h / 2), t->bg));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);

        /* --- TEXT pressed layer --- */
        b = tk_button_new("文字");
        tk_button_set_variant(b, TK_BTN_TEXT);
        r = root_with(b);
        q = tk_widget_rect(b);
        send_ptr(r, q.x + q.w / 2, q.y + q.h / 2, true);
        c = paint_full(r);
        if (t->style == 1) {
            tk_color want = blend(with_a(t->accent, 26),
                                  t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 4),
                             want, 3));
        } else {
            tk_color want = blend(t->control_fill3, t->bg);
            CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 4),
                             want, 3));
        }
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- B. Toggle button forms ---------------------- */
static void test_toggle_forms(void)
{
    /* --- WinUI: LEGACY toggle, the canonical creation --- */
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *b = tk_button_new("切替");
        tk_button_set_toggle(b, true);
        tk_root *r = root_with(b);
        tk_rect q = tk_widget_rect(b);
        int my = q.y + q.h / 2;
        /* unchecked rest / pressed / disabled: the standard
         * button pair (ToggleButton Background resources) */
        tk_canvas *c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + 6, my),
                         blend(t->control_fill, t->bg), 3));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + 6, my),
                         blend(t->control_fill3, t->bg), 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* unchecked + disabled on a fresh button (the first
         * one is still pressed; releasing it would complete
         * the click and toggle it on) */
        b = tk_button_new("切替");
        tk_button_set_toggle(b, true);
        tk_set_enabled(b, false);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + 6, my),
                         blend(t->control_fill_dis, t->bg),
                         3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        b = tk_button_new("切替");
        tk_button_set_toggle(b, true);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        /* checked: accent face, tertiary when pressed, the
         * accent disabled pair when disabled (M11 fix) */
        tk_button_set_toggled(b, true);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->accent));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + 6, my),
                         blend(with_a(t->accent, 204), t->bg),
                         3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* checked + disabled on a fresh button (a pointer UP
         * would complete the click and toggle it back off) */
        b = tk_button_new("切替");
        tk_button_set_toggle(b, true);
        tk_button_set_toggled(b, true);
        tk_set_enabled(b, false);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        tk_color face = blend(t->accent_dis, t->bg);
        CHECK(col_near_c(px(c, q.x + 6, my), face, 3));
        tk_color gwant = blend(t->on_accent_dis, face);
        CHECK(count_near(c, q, gwant, 24) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* the variant path shares the same checked painter */
        b = tk_button_new("濃淡");
        tk_button_set_variant(b, TK_BTN_TONAL);
        tk_button_set_toggle(b, true);
        tk_button_set_toggled(b, true);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->accent));
        tk_canvas_free(c);
        tk_set_enabled(b, false);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + 6, my),
                         blend(t->accent_dis, t->bg), 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
    }
    /* --- M3: per-variant toggle forms (buttons spec) --- */
    tk_theme_set_default(&tk_theme_v2);
    t = tk_theme_default();
    {
        /* LEGACY checked: the container face */
        tk_widget *b = tk_button_new("切替");
        tk_button_set_toggle(b, true);
        tk_button_set_toggled(b, true);
        tk_root *r = root_with(b);
        tk_rect q = tk_widget_rect(b);
        int my = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->container));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* FILLED toggle: unselected = surface container with
         * dim content, selected = the primary face */
        b = tk_button_new("充填");
        tk_button_set_variant(b, TK_BTN_FILLED);
        tk_button_set_toggle(b, true);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->container_mid));
        tk_color dimw = t->text_dim;
        CHECK(count_near(c, q, dimw, 16) > 4);
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        tk_color pwant = blend(with_a(t->text_dim, 26),
                               t->container_mid);
        CHECK(col_near_c(px(c, q.x + 6, my), pwant, 3));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, false);
        tk_button_set_toggled(b, true);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->accent));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* ELEVATED toggle selected = the primary face */
        b = tk_button_new("浮上");
        tk_button_set_variant(b, TK_BTN_ELEVATED);
        tk_button_set_toggle(b, true);
        tk_button_set_toggled(b, true);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->accent));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* TONAL toggle: unselected = tonal container,
         * selected = the selection (secondary container)
         * role, selected pressed adds the content layer,
         * selected disabled is the 12% / 38% framework */
        b = tk_button_new("濃淡");
        tk_button_set_variant(b, TK_BTN_TONAL);
        tk_button_set_toggle(b, true);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->container));
        tk_canvas_free(c);
        tk_button_set_toggled(b, true);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->selection));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        tk_color twant = blend(with_a(t->text, 26),
                               t->selection);
        CHECK(col_near_c(px(c, q.x + 6, my), twant, 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* selected + disabled, fresh (see the WinUI note) */
        b = tk_button_new("濃淡");
        tk_button_set_variant(b, TK_BTN_TONAL);
        tk_button_set_toggle(b, true);
        tk_button_set_toggled(b, true);
        tk_set_enabled(b, false);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        tk_color dface = blend(with_a(t->text, 31), t->bg);
        CHECK(col_near_c(px(c, q.x + 6, my), dface, 3));
        tk_color dgwant = blend(with_a(t->text, 97), dface);
        CHECK(count_near(c, q, dgwant, 24) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* OUTLINED toggle selected = the inverse pair */
        b = tk_button_new("枠線");
        tk_button_set_variant(b, TK_BTN_OUTLINED);
        tk_button_set_toggle(b, true);
        tk_button_set_toggled(b, true);
        r = root_with(b);
        q = tk_widget_rect(b);
        my = q.y + q.h / 2;
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, my), t->inverse_surface));
        CHECK(count_near(c, q, t->inverse_on_surface, 16) > 4);
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        tk_color owant = blend(with_a(t->inverse_on_surface,
                                      26),
                               t->inverse_surface);
        CHECK(col_near_c(px(c, q.x + 6, my), owant, 3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- C. Radio unselected + disabled -------------- */
static void test_radio_unselected_disabled(void)
{
    /* M3: surface disc, the 38% ring, no dot */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *rd = tk_radio_new("項目", 1, 0);
        tk_set_enabled(rd, false);
        tk_root *r = root_with(rd);
        tk_rect q = tk_widget_rect(rd);
        /* M21: the M3 ring is the converted 21px form */
        tk_rect box = { q.x, q.y + (q.h - 21) / 2, 21, 21 };
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, box.x + 10, box.y + 10), t->surface));
        CHECK(col_near_c(px(c, box.x + 10, box.y),
                         blend(with_a(t->text, 97), t->surface),
                         4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(rd);
    }
    /* WinUI: disabled fill, the disabled strong ring, no dot */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *rd = tk_radio_new("項目", 1, 0);
        tk_set_enabled(rd, false);
        tk_root *r = root_with(rd);
        tk_rect q = tk_widget_rect(rd);
        tk_rect box = { q.x, q.y + (q.h - 28) / 2, 28, 28 };
        tk_canvas *c = paint_full(r);
        tk_color dface = blend(t->control_fill_dis, t->bg);
        CHECK(col_near_c(px(c, box.x + 14, box.y + 14),
                         dface, 3));
        CHECK(col_near_c(px(c, box.x + 14, box.y),
                         blend(t->strong_stroke_dis, dface),
                         4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(rd);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D. TextField error geometry ----------------- */
static bool tf_bad_cb(tk_widget *field, const char *text,
                      void *ud)
{
    (void)field; (void)text; (void)ud;
    return false;
}
static tk_widget *error_field(void)
{
    tk_widget *f = tk_textfield_new("");
    tk_textfield_set_validator(f, tf_bad_cb, NULL);
    tk_textfield_set_error_message(f, "不正です");
    tk_textfield_set_text(f, "x");
    tk_textfield_validate(f);
    return f;
}
static void test_textfield_error_geometry(void)
{
    /* M3 filled: the error underline is a 2px danger rect at
     * the container's bottom, inset by half the height at
     * both ends; its geometry is derived from the canvas */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *f = error_field();
        tk_root *r = root_with(f);
        tk_rect q = tk_widget_rect(f);
        tk_canvas *c = paint_full(r);
        /* find the underline's top row: the widest danger run
         * in the middle third of the field */
        int urow = -1;
        for (int y = q.y; y < q.y + q.h && urow < 0; y++) {
            int n = 0;
            for (int x = q.x + q.w / 3; x < q.x + 2 * q.w / 3;
                 x++)
                if (col_near_c(px(c, x, y), t->danger, 6)) n++;
            if (n > q.w / 6) urow = y;
        }
        CHECK(urow > q.y);
        if (urow > q.y) {
            int fh = urow + 2 - q.y; /* the container height */
            int x0 = q.x + fh / 2, x1 = q.x + q.w - fh / 2;
            /* the span's exact extent on both rows */
            CHECK(col_eq(px(c, x0, urow), t->danger));
            CHECK(col_eq(px(c, x1 - 1, urow), t->danger));
            CHECK(col_eq(px(c, q.x + q.w / 2, urow + 1),
                         t->danger));
            /* just outside the span the container shows */
            CHECK(!col_near_c(px(c, x0 - 2, urow), t->danger,
                              6));
            CHECK(!col_near_c(px(c, x1 + 1, urow), t->danger,
                              6));
            CHECK(col_near_c(px(c, x0 - 2, urow),
                             t->surface_alt, 3));
            /* the error message sits below in the danger
             * role */
            tk_rect mzone = { q.x, urow + 2, q.w, q.y + q.h -
                                                 (urow + 2) };
            CHECK(count_near(c, mzone, t->danger, 16) > 4);
        }
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(f);
    }
    /* WinUI: the error outline is the danger stroke around
     * the whole container; a clean field keeps the control
     * stroke at the same pixels */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *f = error_field();
        tk_root *r = root_with(f);
        tk_rect q = tk_widget_rect(f);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w / 2, q.y), t->danger));
        /* the bottom stroke row: the first danger pixel on
         * the centre column scanning up from the widget's
         * bottom (the error message is left-aligned and
         * never reaches the centre column) */
        int last = -1;
        for (int y = q.y + q.h - 1; y > q.y && last < 0; y--)
            if (col_near_c(px(c, q.x + q.w / 2, y),
                           t->danger, 6))
                last = y;
        CHECK(last > q.y + 20);
        if (last > q.y + 20) {
            CHECK(col_eq(px(c, q.x + q.w / 2, last),
                         t->danger));
            CHECK(!col_near_c(px(c, q.x + q.w / 2, last + 1),
                              t->danger, 6));
        }
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(f);
        /* contrast: no error, no danger anywhere on the
         * frame */
        f = tk_textfield_new("");
        r = root_with(f);
        q = tk_widget_rect(f);
        c = paint_full(r);
        tk_rect frame = { q.x, q.y, q.w, q.h };
        CHECK(count_near(c, frame, t->danger, 6) == 0);
        tk_color cf = blend(t->control_fill, t->bg);
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                         blend(t->control_stroke, cf), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(f);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- E. Chip pressed forms ----------------------- */
static void test_chip_pressed(void)
{
    /* M17: the M3 pressed form is the state layer (the
     * content colour at the pressed opacity) over the rest
     * face - the ripple's settled form on this clockless
     * root. The pre-M17 expectations (a surface_alt wash
     * unselected, the bare container selected) pinned the
     * toolkit's approximation, not the spec form; they were
     * rewritten with the ripple's arrival, as the M16
     * snackbar contract test was rewritten when its contract
     * was corrected from the primary source. */
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
        tk_root *r = root_with(ch);
        tk_rect q = tk_widget_rect(ch);
        int my = q.y + q.h / 2;
        send_ptr(r, q.x + q.w / 2, my, true);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 6, my),
                     blend(with_a(t->text, 26), t->bg)));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, false);
        tk_chip_set_selected(ch, true);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 6, my),
                     blend(with_a(t->text, 26), t->container)));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ch);
    }
    /* WinUI (ToggleButton form): unchecked pressed =
     * ControlFillColorTertiary, checked pressed =
     * AccentFillColorTertiary */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    {
        tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
        tk_root *r = root_with(ch);
        tk_rect q = tk_widget_rect(ch);
        int my = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w - 6, my),
                         blend(t->control_fill, t->bg), 3));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w - 6, my),
                         blend(t->control_fill3, t->bg), 3));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, false);
        tk_chip_set_selected(ch, true);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 6, my), t->accent));
        tk_canvas_free(c);
        send_ptr(r, q.x + q.w / 2, my, true);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w - 6, my),
                         blend(with_a(t->accent, 204), t->bg),
                         3));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ch);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- F. Progress determinate forms --------------- */
static void test_progress_forms(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, tk10_map(&tk_theme_winui) };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *p = tk_progress_new(0.5f);
        tk_root *r = root_with(p);
        tk_rect q = tk_widget_rect(p);
        int my = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        tk_color trk = t->style == 1
            ? blend(t->container_highest, t->bg)
            : blend(t->strong_stroke, t->bg);
        CHECK(col_eq(px(c, q.x + q.w / 4, my), t->accent));
        CHECK(col_near_c(px(c, q.x + 3 * q.w / 4, my), trk,
                         3));
        tk_canvas_free(c);
        /* empty: all track; full: the fill reaches the end */
        tk_progress_set_value(p, 0.0f);
        c = paint_full(r);
        CHECK(col_near_c(px(c, q.x + q.w / 4, my), trk, 3));
        tk_canvas_free(c);
        tk_progress_set_value(p, 1.0f);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 8, my), t->accent));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(p);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- G. Segmented (WinUI) unselected forms ------- */
static void test_segmented_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "日");
    tk_segmented_add(sg, "週");
    tk_segmented_add(sg, "月");
    tk_segmented_select(sg, 0);
    tk_root *r = root_with(sg);
    tk_rect q = tk_widget_rect(sg);
    tk_canvas *c = paint_full(r);
    int sw = q.w / 3;
    /* an unselected segment has no fill: the page shows
     * through its interior (probed clear of the label) */
    CHECK(col_eq(px(c, q.x + sw + 4, q.y + 3), t->bg));
    CHECK(col_eq(px(c, q.x + 2 * sw + 4, q.y + 3), t->bg));
    /* the divider between the two unselected segments is
     * the border_line role (probed near the top, clear of
     * the centred labels) */
    CHECK(col_near_c(px(c, q.x + 2 * sw, q.y + 3),
                     blend(t->border_line, t->bg), 4));
    /* no divider is drawn next to the selected segment */
    CHECK(col_eq(px(c, q.x + sw, q.y + 3), t->bg));
    /* the container outline is the control stroke */
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                     blend(t->control_stroke, t->bg), 4));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- H. Card (M3) variant faces ------------------ */
static void test_card_m3(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    /* ELEVATED: container_low face (top row, clear of the
     * shadow which starts 3px lower) */
    tk_widget *cd = tk_card_new(TK_CARD_ELEVATED);
    tk_root *r = root_with(cd);
    tk_rect q = tk_widget_rect(cd);
    tk_canvas *c = paint_full(r);
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 1),
                     blend(t->container_low, t->bg), 3));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cd);
    /* FILLED: container_highest face */
    cd = tk_card_new(TK_CARD_FILLED);
    r = root_with(cd);
    q = tk_widget_rect(cd);
    c = paint_full(r);
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + q.h / 2),
                     blend(t->container_highest, t->bg), 3));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cd);
    /* OUTLINED: surface face, border_line outline */
    cd = tk_card_new(TK_CARD_OUTLINED);
    r = root_with(cd);
    q = tk_widget_rect(cd);
    c = paint_full(r);
    CHECK(col_eq(px(c, q.x + q.w / 2, q.y + q.h / 2),
                 t->surface));
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y),
                     blend(t->border_line, t->surface), 4));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cd);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- I. Slider Range disabled forms -------------- */
static void test_slider_range_disabled(void)
{
    /* WinUI: both spans and both thumbs take the
     * strong-fill disabled gray (M21: the M10 form took
     * ControlFillColorDisabled, invisible on the page) */
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    {
        tk_widget *s = tk_slider_new(0.75f);
        tk_slider_set_values2(s, 0.25f, 0.75f);
        tk_set_enabled(s, false);
        tk_root *r = root_with(s);
        tk_rect q = tk_widget_rect(s);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        tk_color df = blend(t->strong_fill_dis, t->bg);
        /* the centre is the selected span between the
         * thumbs: df itself (M21: the spans are adjacent
         * segments, so there is no second coat under it -
         * the M12 layered expectation no longer applies) */
        CHECK(col_near_c(px(c, q.x + q.w / 2, cy), df, 3));
        CHECK(col_near_c(px(c, q.x + q.w / 8, cy), df, 3));
        /* the lo thumb: the disabled strong fill over the
         * disabled track (probed left of its centre, as in
         * tk9) */
        CHECK(col_near_c(px(c, q.x + q.w / 4 - 7, cy),
                         blend(t->strong_fill_dis, df), 4));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
    }
    /* M3: the range thumbs are the 38% form over their
     * track spans (lo over the inactive span) */
    tk_theme_set_default(&tk_theme_v2);
    t = tk_theme_default();
    {
        tk_widget *s = tk_slider_new(0.75f);
        tk_slider_set_values2(s, 0.25f, 0.75f);
        tk_set_enabled(s, false);
        tk_root *r = root_with(s);
        tk_rect q = tk_widget_rect(s);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        tk_color ina = blend(with_a(t->text, 31), t->bg);
        CHECK(col_near_c(px(c, q.x + q.w / 4 - 7, cy),
                         blend(with_a(t->text, 97), ina), 5));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- J. Toast in the WinUI mode ------------------ */
static void test_toast_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_with(host);
    tk_toast_show(r, "保存しました", 60000);
    tk_canvas *c = paint_full(r);
    tk_rect strip = { 0, 480 - 140, 800, 140 };
    /* M12: the expectations are derived from the tokens, not
     * tuned to one theme: the face IS inverse_surface (a large
     * exact count), and the glyph cores are the ink blended
     * over the face with the ink's own alpha. The pre-M12 form
     * counted raw inverse_on_surface pixels, which only the
     * light theme satisfies (its ink alpha is 255); under
     * TK10_DARK both pre-M12 checks failed - and the first
     * also exposed that the toast did not actually paint the
     * inverse pair (see AUDIT §10). */
    CHECK(count_near(c, strip, t->inverse_surface, 4) > 50);
    CHECK(count_near(c, strip,
                     blend(t->inverse_on_surface,
                           t->inverse_surface), 16) > 4);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- K. SearchBar forms -------------------------- */
static void test_searchbar_forms(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, tk10_map(&tk_theme_winui) };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *sb = tk_searchbar_new("検索");
        tk_root *r = root_with(sb);
        tk_rect q = tk_widget_rect(sb);
        int my = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        /* the face, probed at the right end (clear of the
         * glyph, the placeholder and the clear affordance) */
        tk_color face = t->style == 1
            ? blend(t->container_high, t->bg)
            : blend(t->control_fill, t->bg);
        CHECK(col_near_c(px(c, q.x + q.w - 6, my), face, 3));
        /* the placeholder is present in the dim role and
         * never at full text strength (glyph cores are the
         * dim role composited over the face - the WinUI
         * text_dim token is translucent) */
        tk_rect zone = { q.x + 48, q.y, 200, q.h };
        CHECK(count_near(c, zone, t->text, 8) == 0);
        CHECK(count_near(c, zone, blend(t->text_dim, face),
                         24) > 4);
        tk_canvas_free(c);
        /* populated: the text takes the text role and the
         * clear affordance appears at the right end */
        tk_searchbar_set_text(sb, "abc");
        c = paint_full(r);
        CHECK(count_near(c, zone, blend(t->text, face),
                         16) > 4);
        tk_rect xzone = { q.x + q.w - 40, q.y, 32, q.h };
        CHECK(count_near(c, xzone, blend(t->text_dim, face),
                         24) > 0);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sb);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- L. Dialog scrim + face; TextView selection -- */
static void test_dialog_and_textview(void)
{
    const tk_theme *themes[2] = { &tk_theme_v2, tk10_map(&tk_theme_winui) };
    for (int m = 0; m < 2; m++) {
        tk_theme_set_default(themes[m]);
        const tk_theme *t = tk_theme_default();
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_widget *d = tk_dialog_new("確認");
        tk_dialog_show(r, d);
        tk_root_layout(r, 800, 480);
        tk_rect q = tk_widget_rect(d);
        tk_canvas *c = paint_full(r);
        /* the M11 modal scrim: the theme's smoke over the
         * page, in the corner the dialog does not cover */
        CHECK(col_near_c(px(c, 6, 6), blend(t->smoke, t->bg),
                         3));
        /* the face, probed at the bottom inside edge (clear
         * of the title band and the divider) */
        tk_color face = t->style == 1
            ? blend(t->container_high, blend(t->smoke, t->bg))
            : t->surface;
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + q.h - 6),
                         face, 3));
        /* the title glyphs ride on the face in the text
         * role, and the divider under the title band is
         * the border_line role over the face */
        tk_rect tband = { q.x, q.y, q.w, t->control_h };
        CHECK(count_near(c, tband, blend(t->text, face),
                         16) > 4);
        CHECK(col_near_c(px(c, q.x + q.w / 2,
                            q.y + t->control_h),
                         blend(t->border_line, face), 4));
        tk_canvas_free(c);
        tk_dialog_close(r, d);
        /* modeless: no scrim */
        tk_dialog_show_modeless(r, d);
        tk_root_layout(r, 800, 480);
        c = paint_full(r);
        CHECK(col_eq(px(c, 6, 6), t->bg));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(host);
        /* TextView: the selection wash lies under the
         * selected text and nowhere else */
        tk_widget *tv = tk_textview_new("hello world");
        r = root_with(tv);
        q = tk_widget_rect(tv);
        c = paint_full(r);
        tk_rect band = { q.x, q.y + 2, q.w, 30 };
        tk_color selw = blend(t->selection, t->surface);
        /* the unselected band may hold a few anti-aliased
         * glyph fringe pixels near the wash colour (the
         * WinUI selection is a 15-alpha overlay); the wash
         * itself covers the whole text span */
        int unsel = count_near(c, band, selw, 4);
        tk_canvas_free(c);
        tc_select_all(tk_textview_buffer(tv));
        c = paint_full(r);
        CHECK(count_near(c, band, selw, 4) > unsel + 200);
        /* the glyphs ride on the wash (in the WinUI mode
         * the text token is translucent, so the core is
         * the blend over the wash) */
        CHECK(count_near(c, band, blend(t->text, selw),
                         16) > 4);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(tv);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- M. WinUI leftovers (M12): TextView A, Dialog
 * modeless / full-screen A, toggle E / F. Runs under
 * TK10_DARK too (the theme is read through tk10_map). ------- */
static void send_key11(tk_root *r, tk_key k)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN; ev.key = k;
    (void)tk_root_event(r, &ev);
}
static void test_winui_leftovers(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    /* TextView A: the measured size follows the theme-token
     * formula - widest line + 2*unit (floor 120) by lines *
     * line_height + 8 - evaluated with the WinUI tokens */
    {
        tk_widget *tv = tk_textview_new("短い行\nもう少し長い行です");
        tk_root *r = root_with(tv);
        tk_rect q = tk_widget_rect(tv);
        int w1 = tk_text_width(t, "短い行");
        int w2 = tk_text_width(t, "もう少し長い行です");
        int widest = w1 > w2 ? w1 : w2;
        int want_w = widest + 2 * t->unit;
        if (want_w < 120) want_w = 120;
        CHECK(q.w == want_w);
        CHECK(q.h == 2 * tk_line_height(t) + 8);
        tk_root_free(r);
        tk_widget_free(tv);
    }
    /* Dialog A (modeless / full-screen): the modeless form
     * shares the modal geometry exactly; the full-screen
     * form fills the root */
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_with(host);
        tk_widget *d = tk_dialog_new("確認");
        tk_dialog_show(r, d);
        tk_rect modal = tk_widget_rect(d);
        tk_dialog_close(r, d);
        tk_dialog_show_modeless(r, d);
        tk_rect ml = tk_widget_rect(d);
        CHECK(ml.x == modal.x && ml.y == modal.y &&
              ml.w == modal.w && ml.h == modal.h);
        tk_dialog_close(r, d);
        tk_dialog_set_fullscreen(d, true);
        tk_dialog_show(r, d);
        tk_rect fs = tk_widget_rect(d);
        CHECK(fs.x == 0 && fs.y == 0 && fs.w == 800 &&
              fs.h == 480);
        tk_dialog_close(r, d);
        tk_root_free(r);
        tk_widget_free(host);
        tk_widget_free(d);
    }
    /* Toggle E / F: the label renders in the text role over
     * the rest face, and keyboard activation flips the
     * checked state */
    {
        tk_widget *b = tk_button_new("切替");
        tk_button_set_toggle(b, true);
        tk_root *r = root_with(b);
        tk_rect q = tk_widget_rect(b);
        tk_canvas *c = paint_full(r);
        CHECK(count_near(c, q,
                         blend(t->text,
                               blend(t->control_fill, t->bg)),
                         16) > 4);
        tk_canvas_free(c);
        CHECK(!tk_button_toggled(b));
        CHECK(tk_focus(r, b));
        send_key11(r, TK_KEY_SPACE);
        CHECK(tk_button_toggled(b));
        send_key11(r, TK_KEY_ENTER);
        CHECK(!tk_button_toggled(b));
        tk_root_free(r);
        tk_widget_free(b);
    }
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    if (getenv("TK10_DARK"))
        printf("tk10: TK10_DARK=1 - WinUI sections run under "
               "tk_theme_winui_dark\n");
    tk_font *f = tk_font_load_bundled();
    CHECK(f != NULL);
    if (f) tk_font_install(f);
    test_button_variants2();
    test_toggle_forms();
    test_radio_unselected_disabled();
    test_textfield_error_geometry();
    test_chip_pressed();
    test_progress_forms();
    test_segmented_winui();
    test_card_m3();
    test_slider_range_disabled();
    test_toast_winui();
    test_searchbar_forms();
    test_dialog_and_textview();
    test_winui_leftovers();
    printf("tk10: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
