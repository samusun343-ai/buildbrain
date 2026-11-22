/* test_tk15.c - M16 verification: the D cells closed by
 * invariance evidence or by the new nav-indicator motion,
 * the Slider value indicator (E), the tk_anim foundation's
 * behaviour contract under BOTH themes (C), and the WinUI
 * E remainders for IconButton/FAB and ListView.
 *
 * Reference facts relied on here, fetched 2026-11-20:
 * - Snackbar with an action: Android Developers, Snackbar
 *   API reference - "Snackbars with an action should not
 *   timeout or self-dismiss until the user performs
 *   another action"; Microsoft Learn, InfoBar - the
 *   notification "will remain in the content area until
 *   closed by the user". (The implementation fix is in
 *   tk_core.c; its test lives in test_tk14.c, M16 edit.)
 * - ItemsControl.ItemContainerTransitions (Microsoft
 *   Learn): the default template's ItemContainerTransitions
 *   already contains AddDeleteThemeTransition,
 *   ContentThemeTransition, ReorderThemeTransition and
 *   EntranceThemeTransition - so the WinUI ListView DOES
 *   define item animations; #19 WinUI D stays open (no
 *   test here claims it).
 * - The M3 navigation indicator (NavigationBar /
 *   NavigationRail in Compose) animates the indicator's
 *   colour/alpha between items - a fade, not a slide;
 *   the WinUI NavigationView selection indicator slides.
 *   Both motions are driven here by one displayed
 *   position (ind_pos) over the mode's selection pair
 *   (tk_anim_selection_pair: M3 200 ms Standard / WinUI
 *   167 ms Direct Entrance).
 * - TextView/TextBlock, the WinUI TextBox and the
 *   AppBar/CommandBar define no time-based animation in
 *   their default forms: the D evidence is pixel
 *   stability across a live clock (the M15 #26 WinUI D
 *   precedent). The Android TextField is NOT in this
 *   group: the M3 form's floating-label transition has
 *   no counterpart in this implementation's placeholder
 *   form, so that cell stays open.
 */
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static tk_canvas *paint_full(tk_root *r)
{
    tk_canvas *c = tk_canvas_new(800, 480);
    tk_root_paint(r, c, tk_theme_default());
    return c;
}
static tk_root *root_of(tk_widget *w)
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
static tk_color px(tk_canvas *c, int x, int y)
{
    return tk_canvas_pixel(c, x, y);
}
static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
}
static int count_near(tk_canvas *c, tk_rect q, int rr, int gg, int bb,
                      int tol)
{
    int n = 0;
    for (int y = q.y; y < q.y + q.h; y++)
        for (int x = q.x; x < q.x + q.w; x++) {
            tk_color p = px(c, x, y);
            if (abs(p.r - rr) <= tol && abs(p.g - gg) <= tol &&
                abs(p.b - bb) <= tol)
                n++;
        }
    return n;
}
static int glyphs_near(tk_canvas *c, tk_rect q, tk_color col)
{
    return count_near(c, q, col.r, col.g, col.b, 40);
}
static int diff_count(tk_canvas *a, tk_canvas *b, tk_rect q)
{
    int n = 0;
    for (int y = q.y; y < q.y + q.h; y++)
        for (int x = q.x; x < q.x + q.w; x++) {
            tk_color p = px(a, x, y), s = px(b, x, y);
            if (p.r != s.r || p.g != s.g || p.b != s.b) n++;
        }
    return n;
}
static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event e = { 0 };
    e.type = ty; e.x = x; e.y = y;
    tk_root_event(r, &e);
}
static void solid_icon(uint32_t *buf, int n, int rr, int gg, int bb)
{
    for (int i = 0; i < n; i++)
        buf[i] = 0xFF000000u | ((uint32_t)rr << 16) |
                 ((uint32_t)gg << 8) | (uint32_t)bb;
}

/* ---------- D invariance: no time-based animation --------- */
/* With a live clock running, frames at +0 / +500 / +2500 ms
 * must be pixel-identical over the widget's rect: the
 * reference default form defines no time-driven motion
 * for these widgets. */
static void still_check(tk_widget *w, tk_root *r)
{
    tk_rect q = tk_widget_rect(w);
    tk_root_tick(r, 1000);
    tk_canvas *c1 = paint_full(r);
    tk_root_tick(r, 1500);
    tk_canvas *c2 = paint_full(r);
    CHECK(diff_count(c1, c2, q) == 0);
    tk_root_tick(r, 3500);
    tk_canvas *c3 = paint_full(r);
    CHECK(diff_count(c1, c3, q) == 0);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    tk_canvas_free(c3);
}
static void still_flow(const tk_theme *theme, int which)
{
    tk_theme_set_default(theme);
    tk_widget *w = NULL;
    if (which == 0) w = tk_textview_new("本文テキストです。\n二行目。");
    else if (which == 1) w = tk_textfield_new("値123");
    else w = tk_appbar_new("タイトル");
    tk_root *r = root_of(w);
    still_check(w, r);
    tk_root_free(r);
    tk_widget_free(w);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_still(void)
{
    /* #14 TextView: both modes */
    still_flow(&tk_theme_v2, 0);
    still_flow(&tk_theme_winui, 0);
    /* #13 TextField: WinUI only (see the header note) */
    still_flow(&tk_theme_winui, 1);
    /* #36 AppBar: both modes */
    still_flow(&tk_theme_v2, 2);
    still_flow(&tk_theme_winui, 2);
}

/* ---------- NavRail indicator motion (D, #30) ------------- */
static void rail_d_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *rail = tk_navrail_new();
    tk_navrail_add(rail, "ホーム", ic, 24, 24);
    tk_navrail_add(rail, "検索", ic, 24, 24);
    tk_navrail_add(rail, "設定", ic, 24, 24);
    tk_root *r = root_of(rail);
    tk_rect q = tk_widget_rect(rail);
    /* settled references, painted with no live clock */
    tk_navrail_select(rail, 2);
    tk_canvas *c_set = paint_full(r);
    tk_navrail_select(rail, 0);
    tk_canvas *c_start = paint_full(r);
    /* animate 0 -> 2 over the selection pair */
    tk_root_tick(r, 1000);
    tk_navrail_select(rail, 2);
    tk_root_tick(r, 1080); /* mid-flight in both modes */
    tk_canvas *c_mid = paint_full(r);
    CHECK(diff_count(c_mid, c_set, q) > 0);
    CHECK(diff_count(c_mid, c_start, q) > 0);
    /* the middle item's cell carries indicator evidence at
     * mid-flight, which neither settled state has: the
     * WinUI accent bar slides through it, the M3 pill
     * crossfades through it */
    tk_rect mid_cell = { q.x, q.y + 8 + 64, q.w, 64 };
    if (t->style == 2) {
        /* the accent bar's top edge sits strictly between
         * its two settled positions while sliding */
        int y0 = -1, y1 = -1, ym = -1;
        for (int y = q.y; y < q.y + q.h && ym < 0; y++)
            for (int x = q.x; x < q.x + 3; x++) {
                tk_color p = px(c_mid, x, y);
                if (abs(p.r - t->accent.r) <= 30 &&
                    abs(p.g - t->accent.g) <= 30 &&
                    abs(p.b - t->accent.b) <= 30) { ym = y; break; }
            }
        for (int y = q.y; y < q.y + q.h && y0 < 0; y++) {
            tk_color p = px(c_start, q.x + 1, y);
            if (abs(p.r - t->accent.r) <= 30 &&
                abs(p.g - t->accent.g) <= 30 &&
                abs(p.b - t->accent.b) <= 30) y0 = y;
        }
        for (int y = q.y; y < q.y + q.h && y1 < 0; y++) {
            tk_color p = px(c_set, q.x + 1, y);
            if (abs(p.r - t->accent.r) <= 30 &&
                abs(p.g - t->accent.g) <= 30 &&
                abs(p.b - t->accent.b) <= 30) y1 = y;
        }
        CHECK(y0 >= 0 && y1 > y0);
        CHECK(ym > y0 && ym < y1);
    } else {
        CHECK(diff_count(c_mid, c_start, mid_cell) > 20);
        CHECK(diff_count(c_mid, c_set, mid_cell) > 20);
    }
    /* completion lands exactly on the settled frame */
    tk_root_tick(r, 1400);
    tk_canvas *c_end = paint_full(r);
    CHECK(diff_count(c_end, c_set, q) == 0);
    tk_canvas_free(c_set);
    tk_canvas_free(c_start);
    tk_canvas_free(c_mid);
    tk_canvas_free(c_end);
    tk_root_free(r);
    tk_widget_free(rail);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_rail_d(void)
{
    rail_d_flow(&tk_theme_v2);
    rail_d_flow(&tk_theme_winui);
}

/* ---------- BottomBar indicator motion (D, #29) ----------- */
static void bar_d_flow(const tk_theme *theme, int mode)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *bar = tk_bottombar_new();
    tk_bottombar_set_indicator_mode(bar, mode);
    tk_bottombar_add_cell(bar, ic, 24, 24, "ホーム", NULL, NULL);
    tk_bottombar_add_cell(bar, ic, 24, 24, "検索", NULL, NULL);
    tk_bottombar_add_cell(bar, ic, 24, 24, "設定", NULL, NULL);
    tk_root *r = root_of(bar);
    tk_rect q = tk_widget_rect(bar);
    tk_widget *cell1 = tk_bottombar_cell(bar, 1);
    tk_rect cq = tk_widget_rect(cell1);
    tk_bottombar_set_selected(bar, 2);
    tk_canvas *c_set = paint_full(r);
    tk_bottombar_set_selected(bar, 0);
    tk_canvas *c_start = paint_full(r);
    tk_root_tick(r, 1000);
    tk_bottombar_set_selected(bar, 2);
    tk_root_tick(r, 1080);
    tk_canvas *c_mid = paint_full(r);
    CHECK(diff_count(c_mid, c_set, q) > 0);
    CHECK(diff_count(c_mid, c_start, q) > 0);
    if (t->style == 2) {
        /* the accent bar slides through the middle cell */
        CHECK(count_near(c_mid, cq, t->accent.r, t->accent.g,
                         t->accent.b, 30) > 5);
        CHECK(count_near(c_start, cq, t->accent.r, t->accent.g,
                         t->accent.b, 30) == 0);
    } else {
        /* the M3 face crossfades through the middle cell */
        CHECK(diff_count(c_mid, c_start, cq) > 20);
        CHECK(diff_count(c_mid, c_set, cq) > 20);
    }
    tk_root_tick(r, 1400);
    tk_canvas *c_end = paint_full(r);
    CHECK(diff_count(c_end, c_set, q) == 0);
    tk_canvas_free(c_set);
    tk_canvas_free(c_start);
    tk_canvas_free(c_mid);
    tk_canvas_free(c_end);
    tk_root_free(r);
    tk_widget_free(bar);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_bar_d(void)
{
    bar_d_flow(&tk_theme_v2, 1);   /* M3 pill form */
    bar_d_flow(&tk_theme_v2, 0);   /* M3 tonal form */
    bar_d_flow(&tk_theme_winui, 1); /* WinUI slide form */
}

/* ---------- Slider value indicator (E, #9) ---------------- */
static void slider_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_color ibg = t->inverse_surface.a ? t->inverse_surface
                                        : t->text;
    tk_color ifg = t->inverse_surface.a ? t->inverse_on_surface
                                        : t->bg;
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *sp = tk_label_new(" ");
    tk_widget_set_min_size(sp, 10, 90);
    tk_add_child(host, sp);
    tk_widget *s = tk_slider_new(0.5f);
    tk_widget_set_min_size(s, 400, 56);
    tk_add_child(host, s);
    tk_root *r = tk_root_new(host);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(s);
    int tx = q.x + q.w / 2, ty = q.y + q.h / 2;
    tk_rect bubble = { tx - 28, q.y - 55, 56, 51 };
    tk_canvas *c0 = paint_full(r);
    CHECK(count_near(c0, bubble, ibg.r, ibg.g, ibg.b, 30) == 0);
    send_ptr(r, TK_EV_POINTER_DOWN, tx, ty);
    tk_canvas *c1 = paint_full(r);
    /* the indicator face and its "50%" text render while
     * the single slider is dragged (previously Range-only);
     * the glyph probe is the bubble's central band, where
     * only the value text can paint the foreground role */
    tk_rect band = { tx - 28, bubble.y + bubble.h / 2 - 8, 56, 16 };
    CHECK(count_near(c1, bubble, ibg.r, ibg.g, ibg.b, 30) > 200);
    CHECK(glyphs_near(c1, band, ifg) > 5);
    send_ptr(r, TK_EV_POINTER_UP, tx, ty);
    tk_canvas *c2 = paint_full(r);
    CHECK(count_near(c2, bubble, ibg.r, ibg.g, ibg.b, 30) == 0);
    tk_canvas_free(c0);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}
static void slider_e_range_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_color ibg = t->inverse_surface.a ? t->inverse_surface
                                        : t->text;
    tk_color ifg = t->inverse_surface.a ? t->inverse_on_surface
                                        : t->bg;
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *sp = tk_label_new(" ");
    tk_widget_set_min_size(sp, 10, 90);
    tk_add_child(host, sp);
    tk_widget *s = tk_slider_new(0.0f);
    tk_slider_set_range(s, 0.0f, 100.0f, 1.0f);
    tk_slider_set_value_range(s, 25.0f);
    tk_widget_set_min_size(s, 400, 56);
    tk_add_child(host, s);
    tk_root *r = tk_root_new(host);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(s);
    int tx = q.x + q.w / 4, ty = q.y + q.h / 2;
    tk_rect bubble = { tx - 28, q.y - 55, 56, 51 };
    send_ptr(r, TK_EV_POINTER_DOWN, tx, ty);
    tk_canvas *c1 = paint_full(r);
    /* the range form of the single slider shows the range
     * value ("25") in the same indicator */
    tk_rect band = { tx - 28, bubble.y + bubble.h / 2 - 8, 56, 16 };
    CHECK(count_near(c1, bubble, ibg.r, ibg.g, ibg.b, 30) > 200);
    CHECK(glyphs_near(c1, band, ifg) > 5);
    send_ptr(r, TK_EV_POINTER_UP, tx, ty);
    tk_canvas_free(c1);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_slider_e(void)
{
    slider_e_flow(&tk_theme_v2);
    slider_e_flow(&tk_theme_winui);
    slider_e_range_flow(&tk_theme_v2);
    slider_e_range_flow(&tk_theme_winui);
}

/* ---------- tk_anim foundation behaviour (C, #39) --------- */
/* tk11 verified the engine contract under the process
 * default theme (the Android light theme) only; the WinUI
 * C cell rested on the per-component flows. M16 runs the
 * contract itself under BOTH themes, plus the mode-pair
 * selection behaviour (a Switch knob lands at 200 ms in
 * the M3 mode and at 167 ms in the WinUI mode). */
static int rec_vals[64];
static int rec_n, rec_done, rec_last;
static void rec_cb(void *ud, int v)
{
    (void)ud;
    if (rec_n < 64) rec_vals[rec_n] = v;
    rec_n++;
    rec_last = v;
}
static void rec_done_cb(void *ud) { (void)ud; rec_done++; }
static void engine_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_of(host);
    rec_n = 0; rec_done = 0; rec_last = -1;
    tk_root_tick(r, 1000);
    tk_anim *a = tk_anim_start(r, 0, 1000, 200, tk_ease_linear,
                               rec_cb, rec_done_cb, NULL);
    CHECK(a != NULL);
    tk_root_tick(r, 1100);
    CHECK(rec_last == 500);
    tk_root_tick(r, 1199);
    CHECK(rec_done == 0);
    tk_root_tick(r, 1200);
    CHECK(rec_last == 1000);
    CHECK(rec_done == 1);
    tk_root_tick(r, 2000);
    CHECK(rec_done == 1); /* done fires exactly once */
    /* cancellation stops delivery cold */
    rec_n = 0; rec_done = 0; rec_last = -1;
    tk_anim *b = tk_anim_start(r, 0, 1000, 200, tk_ease_linear,
                               rec_cb, rec_done_cb, NULL);
    tk_root_tick(r, 2100);
    CHECK(rec_last == 500);
    tk_anim_cancel(b);
    tk_root_tick(r, 2300);
    CHECK(rec_last == 500);
    CHECK(rec_done == 0);
    /* wall-clock basis: two roots ticked at different
     * granularities agree at the same absolute time */
    tk_widget *h2 = tk_label_new("副");
    tk_root *r2 = root_of(h2);
    rec_n = 0; rec_last = -1;
    tk_root_tick(r2, 5000);
    tk_anim_start(r2, 0, 1000, 200, tk_ease_linear,
                  rec_cb, rec_done_cb, NULL);
    tk_root_tick(r2, 5150);
    int coarse = rec_last;
    tk_widget *h3 = tk_label_new("副");
    tk_root *r3 = root_of(h3);
    rec_n = 0; rec_last = -1;
    tk_root_tick(r3, 5000);
    tk_anim_start(r3, 0, 1000, 200, tk_ease_linear,
                  rec_cb, rec_done_cb, NULL);
    tk_root_tick(r3, 5050);
    tk_root_tick(r3, 5100);
    tk_root_tick(r3, 5150);
    CHECK(rec_last == coarse);
    CHECK(coarse == 750);
    tk_root_free(r);
    tk_root_free(r2);
    tk_root_free(r3);
    tk_widget_free(host);
    tk_widget_free(h2);
    tk_widget_free(h3);
    tk_theme_set_default(&tk_theme_v2);
}
static void pair_flow(const tk_theme *theme, int dur_ms)
{
    tk_theme_set_default(theme);
    /* the settled reference: flipped on a live clock and
     * ticked past the pair's duration, so the knob landed */
    tk_widget *s0 = tk_switch_new("切替", false);
    tk_root *r0 = root_of(s0);
    tk_root_tick(r0, 1000);
    tk_switch_set_on(s0, true);
    tk_root_tick(r0, 1000 + dur_ms + 50);
    tk_canvas *c_set = paint_full(r0);
    tk_rect q = tk_widget_rect(s0);
    tk_widget *s = tk_switch_new("切替", false);
    tk_root *r = root_of(s);
    tk_root_tick(r, 1000);
    tk_canvas *c_off = paint_full(r);
    tk_switch_set_on(s, true);
    /* mid-flight the knob is between the two settled
     * frames (at dur-1 the easing's integer delivery has
     * already rounded to the end value, so the midpoint
     * is the discriminating sample) */
    tk_root_tick(r, 1000 + dur_ms / 2);
    tk_canvas *c_pre = paint_full(r);
    CHECK(diff_count(c_pre, c_set, q) > 0);
    CHECK(diff_count(c_pre, c_off, q) > 0);
    tk_root_tick(r, 1000 + dur_ms);
    tk_canvas *c_at = paint_full(r);
    CHECK(diff_count(c_at, c_set, q) == 0); /* landed on time */
    tk_canvas_free(c_off);
    tk_canvas_free(c_set);
    tk_canvas_free(c_pre);
    tk_canvas_free(c_at);
    tk_root_free(r0);
    tk_root_free(r);
    tk_widget_free(s0);
    tk_widget_free(s);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_engine_c(void)
{
    engine_flow(&tk_theme_v2);
    engine_flow(&tk_theme_winui);
    pair_flow(&tk_theme_v2, 200);
    pair_flow(&tk_theme_winui, 167);
}

/* ---------- IconButton / FAB pixels, WinUI (E, #3) -------- */
static void iconbtn_e_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *host = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
    tk_widget *b = tk_button_new("");
    tk_button_set_icon(b, ic, 24, 24);
    tk_button_set_variant(b, TK_BTN_FILLED);
    tk_add_child(host, b);
    tk_widget *bl = tk_button_new("追加");
    tk_button_set_icon(bl, ic, 24, 24);
    tk_button_set_variant(bl, TK_BTN_FILLED);
    tk_add_child(host, bl);
    tk_widget *fab = tk_fab_new(ic, 24, 24, 56);
    tk_add_child(host, fab);
    tk_root *r = root_of(host);
    tk_canvas *c = paint_full(r);
    tk_rect qb = tk_widget_rect(b);
    tk_rect ql = tk_widget_rect(bl);
    tk_rect qf = tk_widget_rect(fab);
    /* the icon image renders in the WinUI form of all three */
    CHECK(count_near(c, qb, 255, 0, 255, 8) > 40);
    CHECK(count_near(c, ql, 255, 0, 255, 8) > 40);
    CHECK(count_near(c, qf, 255, 0, 255, 8) > 40);
    /* the icon+label button's label glyphs render too */
    CHECK(glyphs_near(c, ql, t->on_accent) +
          glyphs_near(c, ql, t->text) > 5);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- ListView row pixels, WinUI (E, #19) ----------- */
/* test_tk13.c already covers item icons + main/sub labels
 * under the WinUI themes; this flow completes the row
 * element classes: header text, the checked row's check
 * glyph and the selected row's label glyphs. */
static void listview_e_winui_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    const char *items[] = { "見出し", "項目1", "項目2" };
    const uint32_t *icons[] = { ic, ic, ic };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 3);
    tk_listview_set_item_icons(lv, icons, 24, 24, 3);
    tk_listview_set_header(lv, 0, true);
    tk_listview_set_checkable(lv, true);
    tk_listview_set_row_checked(lv, 2, true);
    tk_listview_select(lv, 1);
    tk_widget_set_min_size(lv, 500, 300);
    tk_root *r = root_of(lv);
    tk_rect q = tk_widget_rect(lv);
    int rh = tk_listview_row_h(lv, t);
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    /* header row: caption glyphs in the dim role. The
     * WinUI text roles are translucent (text_dim alpha
     * 158), so the glyphs are counted by contrast against
     * the row's own background, not by raw colour. The
     * header text starts at the row's left edge (header
     * rows reserve no icon/checkbox columns). */
    {
        tk_color bg = px(c, q.x + q.w - 4, q.y + 4);
        int bl = (bg.r + bg.g + bg.b) / 3, n = 0;
        for (int y = q.y; y < q.y + rh; y++)
            for (int x = q.x; x < q.x + 100; x++) {
                tk_color p = px(c, x, y);
                int l = (p.r + p.g + p.b) / 3;
                if (abs(l - bl) > 60) n++;
            }
        CHECK(n > 5);
    }
    /* the checked row's check: the accent box with the
     * on-accent core (same geometry as the v2 form). M20
     * fix: the fill sample sits inside the r3 corner arc;
     * (bx+1, by+1) is on the arc itself since the box
     * outline follows the rounded fill (it was accent only
     * under the pre-fix square stroke) */
    int bx = q.x + 2 * t->unit + 2, /* int14: LV_PAD_X */ by = q.y + 2 * rh + (rh - 18) / 2;
    CHECK(col_near_c(px(c, bx + 9, by + 9), t->on_accent, 2));
    CHECK(col_near_c(px(c, bx + 2, by + 2), t->accent, 2));
    /* the selected row's label glyphs render */
    CHECK(glyphs_near(c, (tk_rect){ q.x, q.y + rh, q.w, rh },
                      t->text) > 5);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(lv);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_listview_e_winui(void)
{
    listview_e_winui_flow(&tk_theme_winui);
    listview_e_winui_flow(&tk_theme_winui_dark);
}

int main(void)
{
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    test_still();
    test_rail_d();
    test_bar_d();
    test_slider_e();
    test_engine_c();
    iconbtn_e_winui();
    test_listview_e_winui();
    printf("tk15: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
