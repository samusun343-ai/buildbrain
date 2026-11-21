/* test_tk5.c - audit M6: Material-parity additions.
 * Button variants + row icon layout + text align, slider range,
 * indeterminate progress + spinner, chip, badge, tab bar, menu
 * icons, toast/snackbar, pointer long-press, floating overlays
 * (+FAB), overlay-only paint, textfield supporting/error text,
 * textview line tap, grid spans, listview multi-select,
 * bottom bar, color picker, v2 device-fit theme tokens.
 * Every check below drives the public API and observes state or
 * painted pixels - no internal-field poking except where the
 * header documents the field as the API (none needed here). */
#include "tk.h"
#include "textcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, #cond); failures++; } \
} while (0)

static bool col_eq(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
static tk_color px(tk_canvas *c, int x, int y)
{
    return tk_canvas_pixel(c, x, y);
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
static void click_widget(tk_root *r, tk_widget *w)
{
    tk_rect q = tk_widget_rect(w);
    send_click(r, q.x + q.w / 2, q.y + q.h / 2);
}
static void send_key(tk_root *r, tk_key k)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN; ev.key = k;
    (void)tk_root_event(r, &ev);
}
static void solid_icon(uint32_t *buf, int n, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t v = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    for (int i = 0; i < n; i++) buf[i] = v;
}
/* root with a single widget in a vertical box, laid out 800x480 */
static tk_root *single_root(tk_widget *w, tk_widget **box_out)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_add_child(box, w);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    if (box_out) *box_out = box;
    return r;
}
static tk_canvas *paint_full(tk_root *r)
{
    tk_canvas *c = tk_canvas_new(800, 480);
    tk_root_paint(r, c, tk_theme_default());
    return c;
}

/* ---------- button variants ---------- */
static void test_button_variants(void)
{
    const tk_theme *t = tk_theme_default();
    /* FILLED: accent face away from the centred label */
    tk_widget *b = tk_button_new("OK");
    tk_button_set_variant(b, TK_BTN_FILLED);
    CHECK(tk_button_variant_of(b) == TK_BTN_FILLED);
    tk_root *r = single_root(b, NULL);
    tk_canvas *c = paint_full(r);
    CHECK(col_eq(px(c, 30, 24), t->accent));
    tk_canvas_free(c); tk_root_free(r);

    /* TONAL: container face */
    b = tk_button_new("OK");
    tk_button_set_variant(b, TK_BTN_TONAL);
    r = single_root(b, NULL);
    c = paint_full(r);
    CHECK(col_eq(px(c, 30, 24), t->container));
    tk_canvas_free(c); tk_root_free(r);

    /* OUTLINED: no fill (bg shows), border on the edge */
    b = tk_button_new("OK");
    tk_button_set_variant(b, TK_BTN_OUTLINED);
    r = single_root(b, NULL);
    c = paint_full(r);
    CHECK(col_eq(px(c, 30, 24), t->bg));
    CHECK(col_eq(px(c, 0, 24), t->border));
    tk_canvas_free(c); tk_root_free(r);

    /* TEXT: no fill, no border */
    b = tk_button_new("OK");
    tk_button_set_variant(b, TK_BTN_TEXT);
    r = single_root(b, NULL);
    c = paint_full(r);
    CHECK(col_eq(px(c, 30, 24), t->bg));
    CHECK(col_eq(px(c, 0, 24), t->bg));
    tk_canvas_free(c); tk_root_free(r);

    /* FILLED + icon in ROW layout: the icon IS drawn (the apps'
     * SS6-1 complaint was that the accent face dropped it) */
    uint32_t icon[24 * 24];
    solid_icon(icon, 24 * 24, 255, 0, 0);
    b = tk_button_new("開く");
    tk_button_set_icon(b, icon, 24, 24);
    tk_button_set_icon_layout(b, TK_BTN_ICON_ROW);
    CHECK(tk_button_icon_layout_of(b) == TK_BTN_ICON_ROW);
    tk_button_set_variant(b, TK_BTN_FILLED);
    r = single_root(b, NULL);
    c = paint_full(r);
    /* face is accent at the left edge... */
    CHECK(col_eq(px(c, 30, 24), t->accent));
    /* ...and somewhere in the middle band the red icon appears */
    bool saw_red = false;
    for (int x = 100; x < 700 && !saw_red; x++)
        for (int y = 8; y < 40; y += 4) {
            tk_color p = px(c, x, y);
            if (p.r == 255 && p.g == 0 && p.b == 0) saw_red = true;
        }
    CHECK(saw_red);
    tk_canvas_free(c); tk_root_free(r);

    /* ROW measure: wider than the label alone, one control tall */
    b = tk_button_new("開く");
    tk_button_set_icon(b, icon, 24, 24);
    tk_button_set_icon_layout(b, TK_BTN_ICON_ROW);
    tk_size s = tk_measure(b, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s.h == t->control_h);
    CHECK(s.w > 24 + tk_text_width(t, "開く"));
    tk_widget_free(b);
}

/* ---------- button text align ---------- */
static int canvas_diff_count(tk_canvas *a, tk_canvas *b)
{
    int n = 0;
    for (int y = 0; y < 480; y += 3)
        for (int x = 0; x < 800; x += 3)
            if (!col_eq(px(a, x, y), px(b, x, y))) n++;
    return n;
}
static void test_button_align(void)
{
    tk_widget *b = tk_button_new("左寄せテスト");
    tk_root *r = single_root(b, NULL);
    tk_canvas *center = paint_full(r);
    tk_button_set_text_align(b, 0); /* start */
    CHECK(tk_button_text_align(b) == 0);
    tk_canvas *start = paint_full(r);
    CHECK(canvas_diff_count(center, start) > 0);
    tk_button_set_text_align(b, 2); /* end */
    tk_canvas *end = paint_full(r);
    CHECK(canvas_diff_count(center, end) > 0);
    CHECK(canvas_diff_count(start, end) > 0);
    tk_button_set_text_align(b, 1); /* back to center = legacy */
    tk_canvas *center2 = paint_full(r);
    CHECK(canvas_diff_count(center, center2) == 0);
    tk_canvas_free(center); tk_canvas_free(start);
    tk_canvas_free(end); tk_canvas_free(center2);
    tk_root_free(r);
}

/* ---------- slider range ---------- */
static void test_slider_range(void)
{
    tk_widget *s = tk_slider_new(0.0f);
    tk_root *r = single_root(s, NULL);
    CHECK(!tk_slider_has_range(s));
    tk_slider_set_range(s, 0, 100, 10);
    CHECK(tk_slider_has_range(s));
    tk_slider_set_value_range(s, 37);
    CHECK(tk_slider_value_range(s) == 40.0f); /* quantised */
    tk_slider_set_value_range(s, 250);
    CHECK(tk_slider_value_range(s) == 100.0f); /* clamped */
    CHECK(tk_slider_value(s) == 1.0f);         /* fraction view */
    tk_slider_set_value_range(s, 0);
    /* pointer drag snaps to the step grid: 37% -> 40 */
    tk_rect q = tk_widget_rect(s);
    send_ptr(r, TK_EV_POINTER_DOWN, q.x + (int)(q.w * 0.37f), q.y + q.h / 2);
    send_ptr(r, TK_EV_POINTER_UP, q.x + (int)(q.w * 0.37f), q.y + q.h / 2);
    CHECK(tk_slider_value_range(s) == 40.0f);
    /* arrow keys step by fstep in range mode */
    CHECK(tk_focus(r, s));
    send_key(r, TK_KEY_RIGHT);
    CHECK(tk_slider_value_range(s) == 50.0f);
    send_key(r, TK_KEY_LEFT);
    CHECK(tk_slider_value_range(s) == 40.0f);
    /* continuous range (step 0): no quantisation */
    tk_slider_set_range(s, -1.0f, 1.0f, 0);
    tk_slider_set_value_range(s, 0.333f);
    CHECK(tk_slider_value_range(s) > 0.32f &&
          tk_slider_value_range(s) < 0.34f);
    /* clearing the range returns to the plain fraction form */
    tk_slider_clear_range(s);
    CHECK(!tk_slider_has_range(s));
    tk_root_free(r);
}

/* ---------- indeterminate progress + spinner ---------- */
static void test_indeterminate(void)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *p = tk_progress_new(0.25f);
    tk_widget *sp = tk_spinner_new(40);
    tk_add_child(box, p);
    tk_add_child(box, sp);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_root_tick(r, 1000);
    tk_progress_set_indeterminate(p, true);
    CHECK(tk_progress_is_indeterminate(p));
    tk_canvas *a = paint_full(r);
    /* advance time half a sweep period (600ms of 1200ms): the
     * segment and the spinner arc must have moved */
    for (int i = 1; i <= 6; i++) {
        tk_root_tick(r, 1000 + (uint32_t)i * 100);
        tk_canvas *tmp = tk_canvas_new(800, 480);
        (void)tk_root_paint_damage(r, tmp, tk_theme_default());
        tk_canvas_free(tmp);
    }
    tk_canvas *b = paint_full(r);
    CHECK(canvas_diff_count(a, b) > 0);
    tk_canvas_free(a); tk_canvas_free(b);
    /* leaving indeterminate mode restores the value bar */
    tk_progress_set_indeterminate(p, false);
    CHECK(!tk_progress_is_indeterminate(p));
    CHECK(tk_progress_value(p) == 0.25f);
    tk_root_free(r);
}

/* ---------- chip ---------- */
static int chip_clicks, chip_closes;
static void on_chip_click(tk_widget *w, void *ud)
{ (void)w; (void)ud; chip_clicks++; }
static void on_chip_close(tk_widget *w, void *ud)
{ (void)w; (void)ud; chip_closes++; }
static void test_chip(void)
{
    const tk_theme *t = tk_theme_default();
    /* filter chip toggles + fills with the container colour */
    tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "画像");
    tk_chip_set_on_click(ch, on_chip_click, NULL);
    tk_root *r = single_root(ch, NULL);
    chip_clicks = 0;
    click_widget(r, ch);
    CHECK(tk_chip_selected(ch));
    CHECK(chip_clicks == 1);
    tk_canvas *c = paint_full(r);
    tk_rect q = tk_widget_rect(ch);
    CHECK(col_eq(px(c, q.x + q.w - 6, q.y + q.h / 2), t->container));
    tk_canvas_free(c);
    click_widget(r, ch);
    CHECK(!tk_chip_selected(ch));
    tk_root_free(r);

    /* input chip: body tap = click, trailing zone = close */
    ch = tk_chip_new(TK_CHIP_INPUT, "タグ");
    tk_chip_set_on_click(ch, on_chip_click, NULL);
    tk_chip_set_on_close(ch, on_chip_close, NULL);
    r = single_root(ch, NULL);
    chip_clicks = 0; chip_closes = 0;
    q = tk_widget_rect(ch);
    send_click(r, q.x + 10, q.y + q.h / 2);
    CHECK(chip_clicks == 1 && chip_closes == 0);
    send_click(r, q.x + q.w - 8, q.y + q.h / 2);
    CHECK(chip_clicks == 1 && chip_closes == 1);
    tk_root_free(r);

    /* assist chip: click only, never selects */
    ch = tk_chip_new(TK_CHIP_ASSIST, "追加");
    tk_chip_set_on_click(ch, on_chip_click, NULL);
    r = single_root(ch, NULL);
    chip_clicks = 0;
    click_widget(r, ch);
    CHECK(chip_clicks == 1 && !tk_chip_selected(ch));
    tk_root_free(r);
}

/* ---------- badge ---------- */
static void test_badge(void)
{
    tk_widget *bd = tk_badge_new(0);
    tk_size s = tk_measure(bd, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s.w == 0 && s.h == 0); /* hidden */
    tk_badge_set_count(bd, -1);
    s = tk_measure(bd, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s.w == 8 && s.h == 8); /* dot */
    tk_badge_set_count(bd, 7);
    CHECK(tk_badge_count(bd) == 7);
    s = tk_measure(bd, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s.h == 19 && s.w > 12);
    tk_badge_set_count(bd, 150);
    tk_size s2 = tk_measure(bd, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s2.w > s.w); /* "99+" is wider than "7" */
    /* paint: the error-red dot appears (centred in the widget) */
    tk_badge_set_count(bd, -1);
    tk_root *r = single_root(bd, NULL);
    tk_canvas *c = paint_full(r);
    tk_rect bq = tk_widget_rect(bd);
    tk_color dot = px(c, bq.x + bq.w / 2, bq.y + bq.h / 2);
    CHECK(dot.r == 186 && dot.g == 26 && dot.b == 26);
    tk_canvas_free(c);
    tk_root_free(r);
}

/* ---------- tab bar ---------- */
static int tab_sel_fires;
static void on_tab(tk_widget *w, void *ud)
{ (void)w; (void)ud; tab_sel_fires++; }
static void test_tabbar(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *tb = tk_tabbar_new();
    tk_tabbar_add(tb, "曲");
    tk_tabbar_add(tb, "アルバム");
    tk_tabbar_add(tb, "アーティスト");
    tk_tabbar_set_on_select(tb, on_tab, NULL);
    CHECK(tk_tabbar_selected(tb) == 0);
    tk_root *r = single_root(tb, NULL);
    tk_rect q = tk_widget_rect(tb);
    CHECK(q.h == t->control_h); /* label-only bar height */
    tab_sel_fires = 0;
    /* tap the middle segment */
    send_click(r, q.x + q.w / 2, q.y + q.h / 2);
    CHECK(tk_tabbar_selected(tb) == 1);
    CHECK(tab_sel_fires == 1);
    /* the active indicator sits under the selected segment */
    tk_canvas *c = paint_full(r);
    int seg_cx = q.x + q.w / 2;
    CHECK(col_eq(px(c, seg_cx, q.y + q.h - 2), t->accent));
    /* ...and not under an unselected one */
    CHECK(!col_eq(px(c, q.x + q.w / 6, q.y + q.h - 2), t->accent));
    tk_canvas_free(c);
    /* programmatic select + badge bookkeeping */
    tk_tabbar_select(tb, 2);
    CHECK(tk_tabbar_selected(tb) == 2);
    tk_tabbar_set_item_badge(tb, 0, 5);
    tk_tabbar_set_item_badge(tb, 0, 0);
    /* keyboard walk */
    CHECK(tk_focus(r, tb));
    send_key(r, TK_KEY_LEFT);
    CHECK(tk_tabbar_selected(tb) == 1);
    tk_root_free(r);
}

/* ---------- menu icons ---------- */
static void test_menu_icons(void)
{
    uint32_t icon[24 * 24];
    solid_icon(icon, 24 * 24, 0, 200, 0);
    /* a long label, so the content width clears the menu's
     * 80px minimum-width floor and the icon slot is measurable */
    tk_widget *plain = tk_menu_new();
    tk_menu_add_item(plain, "ファイルを開く", 1);
    tk_size s1 = tk_measure(plain, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    tk_widget *with = tk_menu_new();
    tk_menu_add_item(with, "ファイルを開く", 1);
    tk_menu_set_item_icon(with, 1, icon, 24, 24);
    tk_size s2 = tk_measure(with, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s2.w >= s1.w + 24); /* the icon slot widens the menu */
    /* popup + paint: the green icon is on the first row */
    tk_widget *top = tk_label_new("");
    tk_root *r = tk_root_new(top);
    tk_root_layout(r, 800, 480);
    tk_menu_popup(r, with, 10, 10);
    tk_canvas *c = paint_full(r);
    tk_color g = px(c, 10 + 8 + 12, 10 + 24);
    CHECK(g.g == 200 && g.r == 0);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(plain);
}

/* ---------- toast / snackbar ---------- */
static int count_col(tk_canvas *c, tk_color col, int x0, int x1,
                     int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (col_eq(px(c, x, y), col)) n++;
    return n;
}
static void test_toast(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *top = tk_label_new("");
    tk_root *r = tk_root_new(top);
    tk_root_layout(r, 800, 480);
    tk_root_tick(r, 2000);
    CHECK(!tk_toast_visible(r));
    tk_toast_show(r, "保存しました", 0); /* default 4000ms */
    CHECK(tk_toast_visible(r));
    /* M13 premise correction: the toast now fades in over
     * the first 150 ms on a live-clock root (the M13 toast
     * fade), so the full-opacity pixel check runs after the
     * fade-in has completed */
    tk_root_tick(r, 2000 + 150);
    tk_canvas *c = paint_full(r);
    /* inverse pill near the bottom centre */
    CHECK(count_col(c, t->text, 250, 550, 410, 460) > 200);
    tk_canvas_free(c);
    tk_root_tick(r, 2000 + 3999);
    CHECK(tk_toast_visible(r));
    tk_root_tick(r, 2000 + 4000);
    CHECK(!tk_toast_visible(r));
    /* a second show replaces the first */
    tk_toast_show(r, "一つ目", 1000);
    tk_toast_show(r, "二つ目", 1000);
    CHECK(tk_toast_visible(r));
    tk_toast_hide(r);
    CHECK(!tk_toast_visible(r));
    tk_root_free(r);
}
static int snack_fires;
static void on_snack(tk_widget *w, void *ud)
{ (void)w; (void)ud; snack_fires++; }
static void test_snackbar(void)
{
    tk_widget *top = tk_label_new("");
    tk_root *r = tk_root_new(top);
    tk_root_layout(r, 800, 480);
    tk_root_tick(r, 3000);
    snack_fires = 0;
    tk_snackbar_show(r, "削除しました", "元に戻す", on_snack, NULL, 0);
    CHECK(tk_snackbar_visible(r));
    /* action zone: right end of the bar (bar: x16 w560 y408 h56) */
    send_click(r, 540, 436);
    CHECK(snack_fires == 1);
    CHECK(!tk_snackbar_visible(r));
    /* body tap dismisses without firing */
    tk_snackbar_show(r, "削除しました", "元に戻す", on_snack, NULL, 0);
    send_click(r, 100, 436);
    CHECK(snack_fires == 1);
    CHECK(!tk_snackbar_visible(r));
    /* indefinite stays until dismissed */
    tk_snackbar_show(r, "処理中", NULL, NULL, NULL,
                     TK_SNACKBAR_INDEFINITE);
    tk_root_tick(r, 3000 + 60000);
    CHECK(tk_snackbar_visible(r));
    tk_snackbar_hide(r);
    CHECK(!tk_snackbar_visible(r));
    /* timed expiry */
    tk_snackbar_show(r, "短い", NULL, NULL, NULL, 1500);
    tk_root_tick(r, 3000 + 60000 + 1499);
    CHECK(tk_snackbar_visible(r));
    tk_root_tick(r, 3000 + 60000 + 1500);
    CHECK(!tk_snackbar_visible(r));
    tk_root_free(r);
}

/* ---------- long-press ---------- */
static int lp_fires, btn_clicks;
static bool on_hook(void *ud, tk_event *ev)
{
    (void)ud;
    if (ev->type == TK_EV_LONGPRESS) { lp_fires++; return true; }
    return false;
}
static void on_btn(tk_widget *w, void *ud)
{ (void)w; (void)ud; btn_clicks++; }
static void test_longpress(void)
{
    tk_widget *b = tk_button_new("長押し");
    tk_button_set_on_click(b, on_btn, NULL);
    tk_widget_set_event_hook(b, on_hook, NULL);
    tk_root *r = single_root(b, NULL);
    tk_rect q = tk_widget_rect(b);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    lp_fires = 0; btn_clicks = 0;
    tk_root_tick(r, 1000);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    tk_root_tick(r, 1000 + TK_LONGPRESS_MS - 1);
    CHECK(lp_fires == 0);
    tk_root_tick(r, 1000 + TK_LONGPRESS_MS);
    CHECK(lp_fires == 1);
    /* fires once only */
    tk_root_tick(r, 1000 + TK_LONGPRESS_MS + 500);
    CHECK(lp_fires == 1);
    /* the release after a long-press is NOT a click */
    send_ptr(r, TK_EV_POINTER_UP, cx, cy);
    CHECK(btn_clicks == 0);
    /* a plain tap still clicks, and moving away cancels the press */
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    send_ptr(r, TK_EV_POINTER_UP, cx, cy);
    CHECK(btn_clicks == 1);
    lp_fires = 0;
    tk_root_tick(r, 5000);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    send_ptr(r, TK_EV_POINTER_MOVE, cx + TK_LONGPRESS_SLOP + 5, cy);
    tk_root_tick(r, 5000 + TK_LONGPRESS_MS + 100);
    CHECK(lp_fires == 0);
    send_ptr(r, TK_EV_POINTER_UP, cx + TK_LONGPRESS_SLOP + 5, cy);
    tk_root_free(r);
}

/* ---------- floating overlay + FAB + paint_overlays ---------- */
static int fab_clicks;
static void on_fab(tk_widget *w, void *ud)
{ (void)w; (void)ud; fab_clicks++; }
static void test_floating_fab(void)
{
    const tk_theme *t = tk_theme_default();
    uint32_t icon[24 * 24];
    solid_icon(icon, 24 * 24, 255, 255, 255);
    tk_widget *top = tk_label_new("");
    tk_root *r = tk_root_new(top);
    tk_root_layout(r, 800, 480);
    tk_widget *fab = tk_fab_new(icon, 24, 24, 0);
    CHECK(fab != NULL);
    tk_button_set_on_click(fab, on_fab, NULL);
    tk_root_add_floating(r, fab, 3, 16, 16); /* bottom-right */
    CHECK(tk_root_is_floating(r, fab));
    tk_rect q = tk_widget_rect(fab);
    CHECK(q.w == TK_FAB_DEFAULT_SIZE && q.h == TK_FAB_DEFAULT_SIZE);
    CHECK(q.x == 800 - TK_FAB_DEFAULT_SIZE - 16);
    CHECK(q.y == 480 - TK_FAB_DEFAULT_SIZE - 16);
    /* the FAB face is the accent fill (sampled inside the
     * fully-round outline, above the centred icon) */
    tk_canvas *c = paint_full(r);
    CHECK(col_eq(px(c, q.x + q.w / 2, q.y + 6), t->accent));
    tk_canvas_free(c);
    /* overlay-only paint: FAB present, tree area untouched */
    c = tk_canvas_new(800, 480);
    tk_canvas_clear(c, t->bg);
    tk_root_paint_overlays(r, c, t);
    CHECK(col_eq(px(c, q.x + q.w / 2, q.y + 6), t->accent));
    CHECK(col_eq(px(c, 400, 200), t->bg));
    tk_canvas_free(c);
    /* clickable through the overlay hit path */
    fab_clicks = 0;
    click_widget(r, fab);
    CHECK(fab_clicks == 1);
    tk_root_remove_floating(r, fab);
    CHECK(!tk_root_is_floating(r, fab));
    tk_widget_free(fab);
    tk_root_free(r);
}

/* ---------- textfield supporting / error ---------- */
static void test_textfield_support(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *f = tk_textfield_new("");
    tk_size s0 = tk_measure(f, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s0.h == t->control_h);
    tk_textfield_set_supporting_text(f, "半角英数で入力");
    CHECK(tk_textfield_supporting_text(f) != NULL);
    tk_size s1 = tk_measure(f, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s1.h == s0.h + tk_line_height_st(t, TK_TEXT_CAPTION) + 4);
    /* error state: the classic outline turns danger */
    tk_textfield_set_error(f, true);
    CHECK(tk_textfield_is_error(f));
    tk_root *r = single_root(f, NULL);
    tk_canvas *c = paint_full(r);
    tk_rect q = tk_widget_rect(f);
    CHECK(col_eq(px(c, q.x, q.y + 10), t->danger));
    tk_canvas_free(c);
    tk_root_free(r);
    /* clearing the text shrinks the measure back */
    tk_textfield_set_supporting_text(f, NULL);
    tk_size s2 = tk_measure(f, (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
    CHECK(s2.h == s0.h);
    tk_widget_free(f);
}

/* ---------- textview line tap ---------- */
static int tapped_line;
static void on_line(tk_widget *w, int line, void *ud)
{ (void)w; (void)ud; tapped_line = line; }
static void test_textview_line_tap(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *tv = tk_textview_new("一行目\n二行目\n三行目");
    tk_textview_set_on_line_tap(tv, on_line, NULL);
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_add_child(box, tv);
    tk_set_flex(tv, 1);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(tv);
    int lh = tk_line_height(t);
    tapped_line = -1;
    send_ptr(r, TK_EV_POINTER_DOWN, q.x + 20, q.y + 4 + lh + lh / 2);
    CHECK(tapped_line == 1); /* second visual line */
    send_ptr(r, TK_EV_POINTER_UP, q.x + 20, q.y + 4 + lh + lh / 2);
    send_ptr(r, TK_EV_POINTER_DOWN, q.x + 20, q.y + 4 + lh / 2);
    CHECK(tapped_line == 0);
    send_ptr(r, TK_EV_POINTER_UP, q.x + 20, q.y + 4 + lh / 2);
    tk_root_free(r);
}

/* ---------- grid spans ---------- */
static void test_grid_span(void)
{
    tk_widget *g = tk_grid_new(4, 8, 8, 8);
    tk_widget *cells[5];
    const char *names[5] = { "1", "2", "3", "0", "." };
    for (int i = 0; i < 5; i++) {
        cells[i] = tk_button_new(names[i]);
        tk_add_child(g, cells[i]);
    }
    tk_grid_set_span(cells[3], 2, 1); /* the wide "0" key */
    int cs = 0, rs = 0;
    tk_grid_span(cells[3], &cs, &rs);
    CHECK(cs == 2 && rs == 1);
    tk_root *r = tk_root_new(g);
    tk_root_layout(r, 400, 200);
    tk_rect r1 = tk_widget_rect(cells[0]);
    tk_rect r0 = tk_widget_rect(cells[3]);
    tk_rect rd = tk_widget_rect(cells[4]);
    /* the span-2 key could not fit in row 0's last column, so it
     * starts row 1 and is about two columns wide */
    CHECK(r0.y > r1.y);
    CHECK(r0.w > r1.w + 40);
    /* the next key follows it on the same row */
    CHECK(rd.y == r0.y && rd.x > r0.x);
    tk_root_free(r);
}

/* ---------- listview multi-select ---------- */
static void test_listview_multi(void)
{
    const char *items[3] = { "a", "b", "c" };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 3);
    tk_listview_set_multi_select(lv, true);
    CHECK(tk_listview_multi_select(lv));
    tk_root *r = single_root(lv, NULL);
    tk_rect q = tk_widget_rect(lv);
    const tk_theme *t = tk_theme_default();
    int rh = tk_listview_row_h(lv, t);
    send_click(r, q.x + 40, q.y + rh / 2);
    CHECK(tk_listview_checked_count(lv) == 1);
    CHECK(tk_listview_row_checked(lv, 0));
    CHECK(tk_listview_selected(lv) == 0);
    send_click(r, q.x + 40, q.y + rh * 2 + rh / 2);
    CHECK(tk_listview_checked_count(lv) == 2);
    CHECK(tk_listview_selected(lv) == 2);
    send_click(r, q.x + 40, q.y + rh / 2); /* toggle row 0 off */
    CHECK(tk_listview_checked_count(lv) == 1);
    CHECK(!tk_listview_row_checked(lv, 0));
    tk_root_free(r);
}

/* ---------- bottom bar ---------- */
static int cell_clicks;
static void on_cell(tk_widget *w, void *ud)
{ (void)w; (void)ud; cell_clicks++; }
static void test_bottombar(void)
{
    const tk_theme *t = tk_theme_default();
    uint32_t icon[24 * 24];
    solid_icon(icon, 24 * 24, 10, 10, 10);
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *fill = tk_label_new("");
    tk_widget *bar = tk_bottombar_new();
    tk_add_child(box, fill);
    tk_set_flex(fill, 1);
    tk_add_child(box, bar);
    tk_bottombar_add_cell(bar, icon, 24, 24, "開く", on_cell, NULL);
    tk_bottombar_add_cell(bar, icon, 24, 24, "コピー", on_cell, NULL);
    tk_bottombar_add_cell(bar, icon, 24, 24, "削除", on_cell, NULL);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_rect bq = tk_widget_rect(bar);
    CHECK(bq.y + bq.h == 480);        /* sits at the bottom */
    CHECK(bq.h >= 60 && bq.h <= 80);  /* envfit SS3: 64-76 class */
    tk_widget *cell1 = tk_bottombar_cell(bar, 1);
    CHECK(cell1 != NULL);
    cell_clicks = 0;
    click_widget(r, cell1);
    CHECK(cell_clicks == 1);
    /* selection paints the tonal container face on the cell
     * (mode 0, opt-in since M23 made the navigation pill the
     * default form) */
    tk_bottombar_set_indicator_mode(bar, 0);
    tk_bottombar_set_selected(bar, 1);
    CHECK(tk_bottombar_selected(bar) == 1);
    tk_canvas *c = paint_full(r);
    tk_rect cq = tk_widget_rect(cell1);
    CHECK(col_eq(px(c, cq.x + 3, cq.y + 3), t->container));
    tk_canvas_free(c);
    /* badge on a cell */
    tk_bottombar_set_cell_badge(bar, 2, 3);
    tk_widget *cell2 = tk_bottombar_cell(bar, 2);
    CHECK(tk_button_badge(cell2) == 3);
    c = paint_full(r);
    tk_rect c2 = tk_widget_rect(cell2);
    bool saw_badge_red = false;
    for (int x = c2.x + c2.w - 30; x < c2.x + c2.w; x++)
        for (int y = c2.y; y < c2.y + 18; y++) {
            tk_color p = px(c, x, y);
            if (p.r == 186 && p.g == 26 && p.b == 26)
                saw_badge_red = true;
        }
    CHECK(saw_badge_red);
    tk_canvas_free(c);
    tk_root_free(r);
}

/* ---------- color picker ---------- */
static int cp_fires; static uint32_t cp_value; static bool cp_chosen;
static void on_cp(tk_widget *dlg, uint32_t rgba, bool chosen, void *ud)
{
    (void)dlg; (void)ud;
    cp_fires++; cp_value = rgba; cp_chosen = chosen;
}
static void test_colorpicker(void)
{
    tk_widget *top = tk_label_new("");
    tk_root *r = tk_root_new(top);
    tk_root_layout(r, 800, 480);
    tk_widget *dlg = tk_colorpicker_new("色の選択", 0x2196F3FFu);
    CHECK(tk_colorpicker_value(dlg) == 0x2196F3FFu);
    tk_colorpicker_set_on_result(dlg, on_cp, NULL);
    tk_colorpicker_show(r, dlg);
    CHECK(tk_dialog_is_shown(r, dlg));
    /* children: [grid, preview, button row]; grid child 0 is the
     * first palette swatch (Material red 500) */
    tk_widget *grid = tk_child_at(dlg, 0);
    CHECK(grid != NULL && tk_child_count(grid) == 24);
    tk_widget *sw0 = tk_child_at(grid, 0);
    click_widget(r, sw0);
    CHECK(tk_colorpicker_value(dlg) == 0xF44336FFu);
    /* OK delivers the pick and closes */
    tk_widget *row = tk_child_at(dlg, 2);
    tk_widget *ok = tk_child_at(row, 1);
    cp_fires = 0;
    click_widget(r, ok);
    CHECK(cp_fires == 1 && cp_chosen);
    CHECK(cp_value == 0xF44336FFu);
    CHECK(!tk_dialog_is_shown(r, dlg));
    tk_widget_free(dlg);
    tk_root_free(r);
}

/* ---------- v2 device-fit theme tokens ---------- */
static void test_theme_v2_fit(void)
{
    /* AUDIT SS5/SS20: at the corrected ~179ppi premise,
     * 48dp (7.62mm) = 53.8 -> 54px and the 12sp caption
     * floor = 13px (M22; the 187ppi values were 56/14);
     * the v2 (product) theme carries the device-fit
     * values, the classic theme is unchanged */
    CHECK(tk_theme_v2.control_h == 54);
    CHECK(tk_theme_v2.font_caption == 13);
    CHECK(tk_theme_v2.row_h >= 54);
    CHECK(tk_theme_light.control_h == 48);
}

/* ---------- M22: WinUI dark ladder under RGB565 ---------- */
/* The panel's display path is RGB565 (AUDIT SS21). The M21
 * audit found the WinUI dark surface ladder collapsing to
 * two 565 steps; M22 nudged the white-overlay alphas so
 * each meaning-bearing step lands on its own step. This
 * pins the ladder with the audit's own method: blend each
 * token over the theme bg, quantize round-to-nearest. */
static tk_color eff565_over(tk_color fg, tk_color bg)
{
    tk_color o;
    o.r = (uint8_t)((fg.r * fg.a + bg.r * (255 - fg.a) + 127) / 255);
    o.g = (uint8_t)((fg.g * fg.a + bg.g * (255 - fg.a) + 127) / 255);
    o.b = (uint8_t)((fg.b * fg.a + bg.b * (255 - fg.a) + 127) / 255);
    o.a = 255;
    return o;
}
static unsigned q565(tk_color c)
{
    unsigned r5 = (unsigned)(c.r * 31 + 127) / 255;
    unsigned g6 = (unsigned)(c.g * 63 + 127) / 255;
    unsigned b5 = (unsigned)(c.b * 31 + 127) / 255;
    return (r5 << 11) | (g6 << 5) | b5;
}
static void test_winui_dark_ladder565(void)
{
    const tk_theme *t = &tk_theme_winui_dark;
    unsigned bg = q565(t->bg);
    unsigned s_alt = q565(eff565_over(t->surface_alt, t->bg));
    unsigned surf = q565(eff565_over(t->surface, t->bg));
    unsigned card = q565(eff565_over(t->card_bg, t->bg));
    unsigned sel = q565(eff565_over(t->selection, t->bg));
    unsigned subt = q565(eff565_over(t->subtle, t->bg));
    unsigned cfill = q565(eff565_over(t->control_fill, t->bg));
    unsigned cfill2 = q565(eff565_over(t->control_fill2, t->bg));
    unsigned cfdis = q565(eff565_over(t->control_fill_dis, t->bg));
    unsigned bord = q565(eff565_over(t->border, t->bg));
    unsigned chigh = q565(eff565_over(t->container_highest, t->bg));
    unsigned layer = q565(eff565_over(t->layer, t->bg));
    /* the ladder's steps, as designed in tk_core.c */
    CHECK(bg == 0x2104);
    CHECK(cfdis == 0x2925);
    CHECK(s_alt == 0x2945 && layer == 0x2945);
    CHECK(surf == 0x2965);
    CHECK(card == 0x3186 && sel == 0x3186 && subt == 0x3186);
    CHECK(cfill == 0x31a6);
    CHECK(cfill2 == 0x39a7);
    CHECK(bord == 0x39c7);
    CHECK(chigh == 0x39e7);
    /* the nesting pairs that carry meaning stay distinct */
    CHECK(surf != card && surf != subt && surf != sel);
    CHECK(surf != cfill && card != cfill);
    CHECK(bg != s_alt && surf != s_alt);
    CHECK(bord != card && bord != cfill && chigh != bord);
    CHECK(cfdis != surf && cfdis != s_alt);
    CHECK(cfill2 != card && layer != surf);
}

int main(void)
{
    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    test_button_variants();
    test_button_align();
    test_slider_range();
    test_indeterminate();
    test_chip();
    test_badge();
    test_tabbar();
    test_menu_icons();
    test_toast();
    test_snackbar();
    test_longpress();
    test_floating_fab();
    test_textfield_support();
    test_textview_line_tap();
    test_grid_span();
    test_listview_multi();
    test_bottombar();
    test_colorpicker();
    test_theme_v2_fit();
    test_winui_dark_ladder565();
    printf("tk5: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
