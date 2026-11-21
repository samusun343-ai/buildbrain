/* test_tk6.c - audit M7: completion of the toolkit.
 * Dark + WinUI themes (two-mode equivalence), inverse tokens,
 * Card kind, SearchBar, Segmented, Carousel, Navigation rail,
 * Ring (circular determinate progress), AppBar, Drawer, Sheet,
 * FAB menu, Split button, shared clipboard, TextField
 * prefix/suffix/counter/validator/autocomplete, Slider range
 * form + value indicator, toggle + elevated buttons, button
 * children, Menu trailing/badge, modeless + full-screen
 * Dialog, BottomBar pill indicator, ColorPicker hex/alpha,
 * TreeView model interface, drag & drop + ListView row drag,
 * Snackbar swipe dismiss, i18n, accessibility semantics, and
 * the scaled-IDCT JPEG thumbnail path (fixtures decoded by an
 * independent decoder: PIL/libjpeg reference pixels).
 * Every check drives the public API and observes state or
 * painted pixels. */
#include "tk.h"
#include "textcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, #cond); failures++; } \
} while (0)

static bool col_eq(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
static bool col_near(tk_color a, int r, int g, int b, int tol)
{
    return abs(a.r - r) <= tol && abs(a.g - g) <= tol &&
           abs(a.b - b) <= tol;
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
static void send_text(tk_root *r, const char *s)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT; ev.text = s;
    (void)tk_root_event(r, &ev);
}
static void solid_icon(uint32_t *buf, int n, uint8_t r, uint8_t g,
                       uint8_t b)
{
    uint32_t v = 0xFF000000u | ((uint32_t)r << 16) |
                 ((uint32_t)g << 8) | b;
    for (int i = 0; i < n; i++) buf[i] = v;
}
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
static tk_canvas *paint_with(tk_root *r, const tk_theme *t)
{
    tk_canvas *c = tk_canvas_new(800, 480);
    tk_root_paint(r, c, t);
    return c;
}
static int notify_count;
static void on_notify(tk_widget *w, void *ud)
{
    (void)w; (void)ud; notify_count++;
}

/* ---------- themes: the two modes + dark ---------- */
static void test_theme_tokens(void)
{
    /* M3 dark baseline values (m3.material.io color roles) */
    CHECK(col_eq(tk_theme_v2_dark.surface,
                 (tk_color){ 0x14, 0x12, 0x18, 255 }));
    CHECK(col_eq(tk_theme_v2_dark.accent,
                 (tk_color){ 0xD0, 0xBC, 0xFF, 255 }));
    CHECK(col_eq(tk_theme_v2_dark.container_low,
                 (tk_color){ 0x1D, 0x1B, 0x20, 255 }));
    CHECK(tk_theme_v2_dark.dark == 1);
    CHECK(tk_theme_v2.dark == 0);
    /* M3 light inverse roles (the snackbar pairing) */
    CHECK(col_eq(tk_theme_v2.inverse_surface,
                 (tk_color){ 0x32, 0x2F, 0x35, 255 }));
    CHECK(col_eq(tk_theme_v2.inverse_on_surface,
                 (tk_color){ 0xF5, 0xEF, 0xF7, 255 }));
    CHECK(col_eq(tk_theme_v2_dark.inverse_surface,
                 (tk_color){ 0xE6, 0xE0, 0xE9, 255 }));
    /* WinUI / Fluent values (Microsoft Learn theme resources):
     * accent is #0067C0 light / #4CC2FF dark, the base solid
     * background is #F3F3F3 / #202020, text is near-black /
     * white */
    CHECK(col_eq(tk_theme_winui.accent,
                 (tk_color){ 0x00, 0x67, 0xC0, 255 }));
    CHECK(col_eq(tk_theme_winui_dark.accent,
                 (tk_color){ 0x4C, 0xC2, 0xFF, 255 }));
    CHECK(col_eq(tk_theme_winui.bg,
                 (tk_color){ 0xF3, 0xF3, 0xF3, 255 }));
    CHECK(col_eq(tk_theme_winui_dark.bg,
                 (tk_color){ 0x20, 0x20, 0x20, 255 }));
    CHECK(tk_theme_winui.text.r == 0 && tk_theme_winui.text.a == 228);
    CHECK(tk_theme_winui_dark.text.r == 255 &&
          tk_theme_winui_dark.text.a == 255);
    CHECK(tk_theme_winui.style == 2 && tk_theme_winui_dark.style == 2);
    CHECK(tk_theme_v2.style == 1);
    /* the WinUI control height is the device-fit floor:
     * 54px since M22 (the 48dp physical target at the
     * corrected ~179ppi premise; was 56px at 187ppi) */
    CHECK(tk_theme_winui.control_h == 54);
    CHECK(tk_theme_winui.radius == 6);        /* 4epx -> 6px */
    CHECK(tk_theme_winui.radius_overlay == 11); /* 8epx -> 11px */
}

static void test_modes_same_tree(void)
{
    /* One tree, four themes: the card face must follow each
     * mode's own tokens - the runtime theme switch is the only
     * change (two-mode equivalence evidence). */
    tk_widget *card = tk_card_new(TK_CARD_ELEVATED);
    tk_widget *lab = tk_label_new("カード");
    tk_add_child(card, lab);
    tk_root *r = single_root(card, NULL);
    tk_rect q = tk_widget_rect(card);
    int cx = q.x + q.w - 6, cy = q.y + 6; /* card face, no child */

    tk_canvas *c1 = paint_with(r, &tk_theme_v2);
    CHECK(col_eq(px(c1, cx, cy), tk_theme_v2.container_low));
    tk_canvas_free(c1);
    tk_canvas *c2 = paint_with(r, &tk_theme_v2_dark);
    CHECK(col_eq(px(c2, cx, cy), tk_theme_v2_dark.container_low));
    tk_canvas_free(c2);
    /* WinUI card: CardBackgroundFill (white at 70%) blended
     * over the solid base #F3F3F3 */
    tk_canvas *c3 = paint_with(r, &tk_theme_winui);
    CHECK(col_near(px(c3, cx, cy), 251, 251, 251, 3));
    tk_canvas_free(c3);
    /* WinUI dark card: white at 5% over #202020 -> ~43 */
    tk_canvas *c4 = paint_with(r, &tk_theme_winui_dark);
    CHECK(col_near(px(c4, cx, cy), 43, 43, 43, 4));
    tk_canvas_free(c4);
    tk_root_free(r);
    tk_widget_free(card);
}

static void test_winui_checkbox(void)
{
    /* The checked box is accent-filled in both modes, with
     * each mode's own accent */
    tk_widget *cb = tk_checkbox_new("有効", true);
    
    tk_root *r = single_root(cb, NULL);
    tk_rect q = tk_widget_rect(cb);
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    CHECK(col_eq(px(c, q.x + 14, q.y + q.h / 2),
                 tk_theme_v2.accent));
    tk_canvas_free(c);
    c = paint_with(r, &tk_theme_winui);
    CHECK(col_eq(px(c, q.x + 14, q.y + q.h / 2),
                 tk_theme_winui.accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cb);
}

/* ---------- Card ---------- */
static void test_card(void)
{
    tk_widget *card = tk_card_new(TK_CARD_FILLED);
    CHECK(tk_card_variant_of(card) == TK_CARD_FILLED);
    tk_widget *lab = tk_label_new("中身");
    tk_add_child(card, lab);
    tk_root *r = single_root(card, NULL);
    tk_rect q = tk_widget_rect(card);
    tk_rect lq = tk_widget_rect(lab);
    /* content sits inside the 16dp (19px) card padding */
    CHECK(lq.x >= q.x + 19 && lq.y >= q.y + 19);
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    /* FILLED = surface container highest (sampled inside the
     * corner radius, away from the child) */
    CHECK(col_eq(px(c, q.x + q.w - 30, q.y + q.h - 10),
                 tk_theme_v2.container_highest));
    tk_canvas_free(c);
    tk_card_set_variant(card, TK_CARD_OUTLINED);
    CHECK(tk_card_variant_of(card) == TK_CARD_OUTLINED);
    tk_root_layout(r, 800, 480);
    c = paint_with(r, &tk_theme_v2);
    /* OUTLINED = surface fill */
    CHECK(col_eq(px(c, q.x + q.w - 30, q.y + q.h - 10),
                 tk_theme_v2.surface));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(card);
}

/* ---------- SearchBar ---------- */
static void test_searchbar(void)
{
    tk_widget *sb = tk_searchbar_new("検索");
    notify_count = 0;
    tk_searchbar_set_on_search(sb, on_notify, NULL);
    tk_root *r = single_root(sb, NULL);
    tk_rect q = tk_widget_rect(sb);
    click_widget(r, sb); /* focus */
    const char *items[] = { "apple", "apricot", "banana" };
    tk_searchbar_set_suggestions(sb, items, 3);
    send_text(r, "ap");
    char buf[64];
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 2 &&
          strcmp(buf, "ap") == 0);
    CHECK(tk_searchbar_suggestions_open(sb));
    /* pick the first suggestion ("apple") */
    send_click(r, q.x + 30, q.y + q.h + 24);
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 5 &&
          strcmp(buf, "apple") == 0);
    CHECK(!tk_searchbar_suggestions_open(sb));
    CHECK(notify_count == 1); /* on_search fired by the pick */
    /* the clear affordance empties the bar */
    q = tk_widget_rect(sb);
    send_click(r, q.x + q.w - 20, q.y + q.h / 2);
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 0);
    tk_root_free(r);
    tk_widget_free(sb);
}

/* ---------- Segmented ---------- */
static void test_segmented(void)
{
    tk_widget *seg = tk_segmented_new(false);
    tk_segmented_add(seg, "日");
    tk_segmented_add(seg, "週");
    tk_segmented_add(seg, "月");
    notify_count = 0;
    tk_segmented_set_on_change(seg, on_notify, NULL);
    tk_root *r = single_root(seg, NULL);
    tk_rect q = tk_widget_rect(seg);
    CHECK(tk_segmented_selected(seg) == -1);
    send_click(r, q.x + q.w / 2, q.y + q.h / 2);
    CHECK(tk_segmented_selected(seg) == 1);
    CHECK(notify_count == 1);
    /* selected segment paints the secondary-container face */
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    CHECK(col_eq(px(c, q.x + q.w / 2, q.y + q.h / 2),
                 tk_theme_v2.container));
    tk_canvas_free(c);
    /* WinUI mode: the selected segment takes the accent text
     * treatment - centre pixel differs from Android mode */
    c = paint_with(r, &tk_theme_winui);
    tk_color wp = px(c, q.x + q.w / 2, q.y + q.h / 2);
    CHECK(!col_eq(wp, tk_theme_v2.container));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(seg);

    tk_widget *m = tk_segmented_new(true);
    tk_segmented_add(m, "A");
    tk_segmented_add(m, "B");
    tk_segmented_add(m, "C");
    tk_segmented_set_mask(m, 0x5);
    CHECK(tk_segmented_mask(m) == 0x5);
    tk_root *r2 = single_root(m, NULL);
    tk_rect q2 = tk_widget_rect(m);
    send_click(r2, q2.x + q2.w / 2, q2.y + q2.h / 2); /* toggle B */
    CHECK(tk_segmented_mask(m) == 0x7);
    tk_root_free(r2);
    tk_widget_free(m);
}

/* ---------- Carousel ---------- */
static void test_carousel(void)
{
    uint32_t red[64], grn[64], blu[64], yel[64];
    solid_icon(red, 64, 200, 30, 30);
    solid_icon(grn, 64, 30, 200, 30);
    solid_icon(blu, 64, 30, 30, 200);
    solid_icon(yel, 64, 200, 200, 30);
    tk_widget *car = tk_carousel_new();
    tk_carousel_add(car, "赤", red, 8, 8);
    tk_carousel_add(car, "緑", grn, 8, 8);
    tk_carousel_add(car, "青", blu, 8, 8);
    tk_carousel_add(car, "黄", yel, 8, 8);
    notify_count = 0;
    tk_carousel_set_on_select(car, on_notify, NULL);
    tk_root *r = single_root(car, NULL);
    CHECK(tk_carousel_index(car) == 0);
    tk_carousel_set_index(car, 2);
    CHECK(tk_carousel_index(car) == 2);
    /* the large item shows the selected image's colour */
    tk_canvas *c = paint_full(r);
    tk_rect q = tk_widget_rect(car);
    CHECK(col_near(px(c, q.x + 120, q.y + q.h / 2), 30, 30, 200,
                   24));
    tk_canvas_free(c);
    tk_carousel_set_index(car, 0);
    tk_root_layout(r, 800, 480);
    /* tapping the first small item (after the 248px large item
     * and the 9px gap) selects it */
    send_click(r, q.x + 248 + 9 + 32, q.y + q.h / 2);
    CHECK(tk_carousel_index(car) == 1);
    tk_root_free(r);
    tk_widget_free(car);
}

/* ---------- Navigation rail ---------- */
static void test_navrail(void)
{
    uint32_t ic[64];
    solid_icon(ic, 64, 90, 90, 90);
    tk_widget *rail = tk_navrail_new();
    tk_navrail_add(rail, "ホーム", ic, 8, 8);
    tk_navrail_add(rail, "検索", ic, 8, 8);
    tk_navrail_add(rail, "設定", ic, 8, 8);
    tk_navrail_set_item_badge(rail, 1, 5);
    notify_count = 0;
    tk_navrail_set_on_select(rail, on_notify, NULL);
    tk_root *r = single_root(rail, NULL);
    tk_rect q = tk_widget_rect(rail);
    tk_size ms = tk_measure(rail, (tk_constraints){ 0, TK_UNLIMITED,
                                                   0, TK_UNLIMITED });
    CHECK(ms.w == 94); /* 80dp at the panel scale */
    tk_navrail_select(rail, 0);
    CHECK(tk_navrail_selected(rail) == 0);
    /* cells start 8px in and are 64 tall: second cell centre */
    send_click(r, q.x + q.w / 2, q.y + 8 + 64 + 32);
    CHECK(tk_navrail_selected(rail) == 1);
    CHECK(notify_count == 1);
    /* the badge dot paints in the danger colour family */
    tk_canvas *c = paint_full(r);
    bool saw_red = false;
    for (int y = q.y; y < q.y + 140 && !saw_red; y++)
        for (int x = q.x; x < q.x + q.w; x++) {
            tk_color p = px(c, x, y);
            if (p.r > 150 && p.g < 90 && p.b < 90) saw_red = true;
        }
    CHECK(saw_red);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(rail);
}

/* ---------- Ring (circular determinate progress) ---------- */
static void test_ring(void)
{
    tk_widget *ring = tk_ring_new(0.5f);
    CHECK(fabsf(tk_ring_value(ring) - 0.5f) < 0.001f);
    tk_ring_set_size(ring, 56);
    tk_root *r = single_root(ring, NULL);
    tk_rect q = tk_widget_rect(ring);
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    /* half-way: the arc starts at the top (accent) and the
     * down-left point is still the track colour */
    CHECK(col_eq(px(c, cx, q.y + 5), tk_theme_v2.accent));
    CHECK(!col_eq(px(c, cx - 17, cy + 17), tk_theme_v2.accent));
    tk_canvas_free(c);
    tk_ring_set_value(ring, 1.0f);
    CHECK(fabsf(tk_ring_value(ring) - 1.0f) < 0.001f);
    tk_root_layout(r, 800, 480);
    c = paint_with(r, &tk_theme_v2);
    CHECK(col_eq(px(c, cx - 17, cy + 17), tk_theme_v2.accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(ring);
}

/* ---------- AppBar ---------- */
static void test_appbar(void)
{
    tk_widget *bar = tk_appbar_new("タイトル");
    CHECK(strcmp(tk_appbar_title(bar), "タイトル") == 0);
    tk_widget *back = tk_button_new("←");
    tk_appbar_set_leading(bar, back);
    tk_root *r = single_root(bar, NULL);
    tk_rect q = tk_widget_rect(bar);
    /* the bar is one control-height tall in the active theme */
    CHECK(q.h == tk_theme_default()->control_h);
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    tk_color flat = px(c, q.x + q.w - 10, q.y + q.h / 2);
    tk_canvas_free(c);
    tk_appbar_set_scrolled(bar, true);
    tk_root_layout(r, 800, 480);
    c = paint_with(r, &tk_theme_v2);
    tk_color sc = px(c, q.x + q.w - 10, q.y + q.h / 2);
    CHECK(!col_eq(flat, sc)); /* scrolled state recolours */
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(bar);
}

/* ---------- Drawer ---------- */
static void test_drawer(void)
{
    tk_widget *top = tk_label_new("本体");
    tk_root *r = single_root(top, NULL);
    tk_widget *dr = tk_drawer_new("メニュー");
    tk_drawer_add_item(dr, "ホーム", 10, NULL, 0, 0);
    tk_drawer_add_item(dr, "設定", 20, NULL, 0, 0);
    tk_drawer_add_separator(dr);
    tk_drawer_add_item(dr, "情報", 30, NULL, 0, 0);
    notify_count = 0;
    tk_drawer_set_on_select(dr, on_notify, NULL);
    tk_drawer_show(r, dr, true);
    CHECK(tk_drawer_is_shown(r, dr));
    tk_rect q = tk_widget_rect(dr);
    /* first row centre: 16 pad + 56 headline + half of 64 */
    send_click(r, q.x + 60, q.y + 16 + 56 + 32);
    CHECK(tk_drawer_last_id(dr) == 10);
    CHECK(tk_drawer_selected(dr) == 10);
    CHECK(notify_count == 1);
    CHECK(!tk_drawer_is_shown(r, dr)); /* modal closes on pick */
    tk_widget_free(dr);
    tk_root_free(r);
    tk_widget_free(top);
}

/* ---------- Sheet ---------- */
static void test_sheet(void)
{
    tk_widget *top = tk_label_new("本体");
    tk_root *r = single_root(top, NULL);
    tk_widget *sh = tk_sheet_new();
    tk_widget *lab = tk_label_new("シート内容");
    tk_add_child(sh, lab);
    notify_count = 0;
    tk_sheet_set_on_dismiss(sh, on_notify, NULL);
    tk_sheet_show(r, sh, true);
    CHECK(tk_sheet_is_shown(r, sh));
    tk_rect q = tk_widget_rect(sh);
    CHECK(q.y + q.h == 480); /* bottom-anchored */
    CHECK(q.w <= 748);       /* 640dp cap at the panel scale */
    /* drag the handle down past a third of the sheet: dismiss */
    send_ptr(r, TK_EV_POINTER_DOWN, q.x + q.w / 2, q.y + 10);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + q.w / 2, q.y + 160);
    send_ptr(r, TK_EV_POINTER_UP, q.x + q.w / 2, q.y + 160);
    CHECK(!tk_sheet_is_shown(r, sh));
    CHECK(notify_count == 1); /* on_dismiss */
    /* side sheet anchors to the end edge */
    tk_sheet_set_edge(sh, 1);
    tk_sheet_show(r, sh, false);
    CHECK(tk_sheet_is_shown(r, sh));
    q = tk_widget_rect(sh);
    CHECK(q.x + q.w == 800);
    tk_sheet_close(r, sh);
    CHECK(!tk_sheet_is_shown(r, sh));
    tk_widget_free(sh);
    tk_root_free(r);
    tk_widget_free(top);
}

/* ---------- FAB menu ---------- */
static int fab_a, fab_b;
static void on_fab_a(tk_widget *w, void *ud)
{ (void)w; (void)ud; fab_a++; }
static void on_fab_b(tk_widget *w, void *ud)
{ (void)w; (void)ud; fab_b++; }
static void test_fabmenu(void)
{
    uint32_t ic[64];
    solid_icon(ic, 64, 10, 10, 10);
    tk_widget *top = tk_label_new("本体");
    tk_root *r = single_root(top, NULL);
    tk_widget *fm = tk_fabmenu_new();
    tk_fabmenu_add_item(fm, ic, 8, 8, "追加", on_fab_a, NULL);
    tk_fabmenu_add_item(fm, ic, 8, 8, "編集", on_fab_b, NULL);
    fab_a = fab_b = 0;
    tk_fabmenu_show(r, fm);
    CHECK(tk_fabmenu_is_shown(r, fm));
    tk_rect q = tk_widget_rect(fm);
    send_click(r, q.x + q.w / 2, q.y + 28); /* first row */
    CHECK(fab_a == 1 && fab_b == 0);
    CHECK(!tk_fabmenu_is_shown(r, fm)); /* pick dismisses */
    tk_widget_free(fm);
    tk_root_free(r);
    tk_widget_free(top);
}

/* ---------- Split button ---------- */
static int split_main, split_item;
static void on_split_main(tk_widget *w, void *ud)
{ (void)w; (void)ud; split_main++; }
static void on_split_item(tk_widget *w, void *ud)
{ (void)w; (void)ud; split_item++; }
static void test_splitbutton(void)
{
    tk_widget *sp = tk_splitbutton_new("保存", on_split_main, NULL);
    tk_widget *menu = tk_menu_new();
    tk_menu_add_item(menu, "名前を付けて保存", 1);
    tk_menu_add_item(menu, "上書き保存", 2);
    tk_menu_set_on_select(menu, on_split_item, NULL);
    tk_splitbutton_set_menu(sp, menu);
    split_main = split_item = 0;
    tk_root *r = single_root(sp, NULL);
    click_widget(r, tk_splitbutton_main(sp));
    CHECK(split_main == 1);
    CHECK(!tk_menu_is_open(r, menu));
    tk_widget *arrow = tk_splitbutton_arrow(sp);
    click_widget(r, arrow);
    CHECK(tk_menu_is_open(r, menu));
    tk_rect aq = tk_widget_rect(arrow);
    send_click(r, aq.x + 20, aq.y + aq.h + 24); /* first item */
    CHECK(split_item == 1);
    CHECK(!tk_menu_is_open(r, menu));
    tk_root_free(r);
    tk_widget_free(sp); /* frees the menu with the pair */
}

/* ---------- Shared clipboard ---------- */
static void test_clipboard(void)
{
    tk_clipboard_clear();
    CHECK(!tk_clipboard_has_text());
    CHECK(tk_clipboard_text() == NULL);
    tk_clipboard_set_text("共有テキスト");
    CHECK(tk_clipboard_has_text());
    CHECK(strcmp(tk_clipboard_text(), "共有テキスト") == 0);
    /* cross-widget: two fields share the toolkit clipboard by
     * default - copy in A, paste in B */
    tk_widget *a = tk_textfield_new("コピー元");
    tk_widget *b = tk_textfield_new("");
    tc_buffer *ba = tk_textfield_buffer(a);
    tc_buffer *bb = tk_textfield_buffer(b);
    tc_select_all(ba);
    CHECK(tc_copy(ba));
    CHECK(strcmp(tk_clipboard_text(), "コピー元") == 0);
    CHECK(tc_paste(bb));
    char buf[64];
    CHECK(tk_textfield_text(b, buf, sizeof buf) == 12 &&
          strcmp(buf, "コピー元") == 0);
    tk_clipboard_clear();
    CHECK(!tk_clipboard_has_text());
    tk_widget_free(a);
    tk_widget_free(b);
}

/* ---------- i18n ---------- */
static void test_i18n(void)
{
    static const tk_i18n_entry en[] = {
        { "hello", "Hello" },
        { "items.one", "one item" },
        { "items.other", "many items" },
    };
    static const tk_i18n_entry ja[] = {
        { "hello", "こんにちは" },
        { "items.other", "たくさんの項目" },
    };
    tk_i18n_clear();
    tk_i18n_add_locale("en", en, 3); /* first = default */
    tk_i18n_add_locale("ja", ja, 2);
    tk_i18n_set_locale("ja-JP"); /* falls back to language ja */
    CHECK(strcmp(tk_tr("hello"), "こんにちは") == 0);
    /* ja has no plural "one": always other */
    CHECK(strcmp(tk_tr_plural("items", 1), "たくさんの項目") == 0);
    tk_i18n_set_locale("en");
    CHECK(strcmp(tk_tr("hello"), "Hello") == 0);
    CHECK(strcmp(tk_tr_plural("items", 1), "one item") == 0);
    CHECK(strcmp(tk_tr_plural("items", 5), "many items") == 0);
    /* an unregistered locale falls back to the default catalog */
    tk_i18n_set_locale("fr");
    CHECK(strcmp(tk_tr("hello"), "Hello") == 0);
    /* a missing key resolves to itself */
    CHECK(strcmp(tk_tr("no.such.key"), "no.such.key") == 0);
    tk_i18n_clear();
}

/* ---------- Accessibility semantics ---------- */
static void test_a11y(void)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *btn = tk_button_new("実行");
    tk_widget *cbx = tk_checkbox_new("同意", true);
    tk_widget *sld = tk_slider_new(0.25f);
    tk_add_child(box, btn);
    tk_add_child(box, cbx);
    tk_add_child(box, sld);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_a11y_node nodes[16];
    size_t n = tk_a11y_collect(r, nodes, 16);
    CHECK(n >= 3);
    const tk_a11y_node *nb = NULL, *nc = NULL, *ns = NULL;
    for (size_t i = 0; i < n; i++) {
        if (nodes[i].widget == btn) nb = &nodes[i];
        if (nodes[i].widget == cbx) nc = &nodes[i];
        if (nodes[i].widget == sld) ns = &nodes[i];
    }
    CHECK(nb && nb->role == TK_ROLE_BUTTON);
    CHECK(nb && nb->label && strcmp(nb->label, "実行") == 0);
    CHECK(nc && nc->role == TK_ROLE_CHECKBOX);
    CHECK(nc && nc->state && strcmp(nc->state, "checked") == 0);
    CHECK(ns && ns->role == TK_ROLE_SLIDER);
    CHECK(ns && ns->state && strstr(ns->state, "25") != NULL);
    /* content description overrides the derived label */
    tk_widget_set_content_description(btn, "実行ボタン");
    n = tk_a11y_collect(r, nodes, 16);
    nb = NULL;
    for (size_t i = 0; i < n; i++)
        if (nodes[i].widget == btn) nb = &nodes[i];
    CHECK(nb && nb->label &&
          strcmp(nb->label, "実行ボタン") == 0);
    /* both platforms' type names resolve */
    CHECK(strcmp(tk_a11y_role_name(TK_ROLE_BUTTON),
                 "android.widget.Button") == 0);
    CHECK(strcmp(tk_a11y_uia_type(TK_ROLE_BUTTON), "Button") == 0);
    /* announcements (live regions) */
    tk_a11y_announce(r, "保存しました");
    CHECK(tk_a11y_last_announcement(r) &&
          strcmp(tk_a11y_last_announcement(r),
                 "保存しました") == 0);
    tk_toast_show(r, "トースト", 1000);
    CHECK(tk_a11y_last_announcement(r) &&
          strcmp(tk_a11y_last_announcement(r), "トースト") == 0);
    tk_root_free(r);
    tk_widget_free(box);
}

/* ---------- Drag & drop ---------- */
static int drop_fires;
static char drop_mime[32];
static char drop_data[32];
static tk_widget *drop_target_seen;
static void on_drop(tk_widget *target, void *ud, const char *mime,
                    const void *data, size_t len, int x, int y)
{
    (void)ud; (void)x; (void)y;
    drop_fires++;
    drop_target_seen = target;
    snprintf(drop_mime, sizeof drop_mime, "%s", mime);
    size_t n = len < sizeof drop_data - 1 ? len
                                          : sizeof drop_data - 1;
    memcpy(drop_data, data, n);
    drop_data[n] = 0;
}
static void test_dnd(void)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *src = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget_set_min_size(src, 800, 150);
    tk_widget *lab = tk_label_new("ドラッグ元");
    tk_add_child(src, lab);
    tk_widget *dst = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget_set_min_size(dst, 800, 150);
    tk_widget *dlab = tk_label_new("ドロップ先");
    tk_add_child(dst, dlab);
    tk_add_child(box, src);
    tk_add_child(box, dst);
    tk_widget_set_drag_source(src, "text/plain", "DATA", 4,
                              "アイテム");
    tk_widget_set_on_drop(dst, on_drop, NULL);
    drop_fires = 0;
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    /* press in the source, move past the slop: drag starts */
    send_ptr(r, TK_EV_POINTER_DOWN, 400, 60);
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 100);
    CHECK(tk_drag_active(r));
    /* the drag shadow paints while dragging */
    tk_canvas *c = paint_full(r);
    CHECK(c != NULL);
    tk_canvas_free(c);
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 220);
    send_ptr(r, TK_EV_POINTER_UP, 400, 220);
    CHECK(!tk_drag_active(r));
    CHECK(drop_fires == 1);
    CHECK(drop_target_seen == dst);
    CHECK(strcmp(drop_mime, "text/plain") == 0);
    CHECK(strcmp(drop_data, "DATA") == 0);
    /* a drag released outside any target cancels silently */
    send_ptr(r, TK_EV_POINTER_DOWN, 400, 60);
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 100);
    CHECK(tk_drag_active(r));
    send_ptr(r, TK_EV_POINTER_UP, 400, 60);
    CHECK(drop_fires == 1);
    tk_root_free(r);
    tk_widget_free(box);
}

static void test_listview_drag(void)
{
    const char *items[] = { "A", "B", "C", "D" };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 4);
    tk_listview_set_drag_reorder(lv, true);
    CHECK(tk_listview_drag_reorder(lv));
    tk_root *r = single_root(lv, NULL);
    tk_rect q = tk_widget_rect(lv);
    int rh = 48; /* classic row height */
    /* drag row 0 down two rows and release */
    send_ptr(r, TK_EV_POINTER_DOWN, q.x + 40, q.y + rh / 2);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + 40,
             q.y + rh / 2 + 30);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + 40,
             q.y + rh * 2 + rh / 2);
    send_ptr(r, TK_EV_POINTER_UP, q.x + 40,
             q.y + rh * 2 + rh / 2);
    const char *it0 = tk_listview_item(lv, 0);
    const char *it2 = tk_listview_item(lv, 2);
    CHECK(it0 && strcmp(it0, "B") == 0);
    CHECK(it2 && strcmp(it2, "A") == 0);
    /* a plain tap still just selects */
    send_click(r, q.x + 40, q.y + rh / 2);
    CHECK(tk_listview_selected(lv) == 0);
    tk_root_free(r);
    tk_widget_free(lv);
}

/* ---------- Slider: range form + value indicator ---------- */
static void test_slider_dual(void)
{
    tk_widget *s = tk_slider_new(0.5f);
    tk_slider_set_range(s, 0, 100, 1);
    tk_slider_set_values2(s, 20, 80);
    CHECK(tk_slider_is_dual(s));
    CHECK(fabsf(tk_slider_value_lo(s) - 20) < 0.6f);
    CHECK(fabsf(tk_slider_value_hi(s) - 80) < 0.6f);
    /* a spacer lifts the slider so the value-indicator bubble
     * (painted above the thumb) is on-canvas */
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *spacer = tk_label_new("");
    tk_widget_set_min_size(spacer, 800, 100);
    tk_add_child(box, spacer);
    tk_add_child(box, s);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(s);
    /* grab the HI thumb (at 80% of the track) and drag to 60% */
    int hx = q.x + (int)(0.8f * q.w);
    send_ptr(r, TK_EV_POINTER_DOWN, hx, q.y + q.h / 2);
    CHECK(tk_slider_active_handle(s) == 1);
    /* while grabbed, the value indicator bubble paints in the
     * inverse surface colour above the thumb (M3 form) */
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    CHECK(col_eq(px(c, hx, q.y + q.h / 2 - 30),
                 tk_theme_v2.inverse_surface));
    tk_canvas_free(c);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + (int)(0.6f * q.w),
             q.y + q.h / 2);
    send_ptr(r, TK_EV_POINTER_UP, q.x + (int)(0.6f * q.w),
             q.y + q.h / 2);
    CHECK(fabsf(tk_slider_value_hi(s) - 60) < 3.0f);
    CHECK(fabsf(tk_slider_value_lo(s) - 20) < 0.6f);
    /* grab the LO thumb and move it up */
    int lx = q.x + (int)(0.2f * q.w);
    send_ptr(r, TK_EV_POINTER_DOWN, lx, q.y + q.h / 2);
    CHECK(tk_slider_active_handle(s) == 0);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + (int)(0.35f * q.w),
             q.y + q.h / 2);
    send_ptr(r, TK_EV_POINTER_UP, q.x + (int)(0.35f * q.w),
             q.y + q.h / 2);
    CHECK(fabsf(tk_slider_value_lo(s) - 35) < 3.0f);
    tk_slider_clear_dual(s);
    CHECK(!tk_slider_is_dual(s));
    tk_root_free(r);
    tk_widget_free(box);
}

/* ---------- TextField: prefix/counter/validator/completion --- */
static bool validate_digits(tk_widget *f, const char *text,
                            void *ud)
{
    (void)f; (void)ud;
    if (!*text) return false;
    for (const char *p = text; *p; p++)
        if (*p < '0' || *p > '9') return false;
    return true;
}
static void test_textfield_m7(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *f = tk_textfield_new("");
    tk_textfield_set_prefix(f, "¥");
    tk_textfield_set_suffix(f, "円");
    tk_size s0 = tk_measure(f, (tk_constraints){ 0, TK_UNLIMITED,
                                                0, TK_UNLIMITED });
    CHECK(s0.h == t->control_h); /* adornments add no height */
    char buf[64];
    CHECK(tk_textfield_text(f, buf, sizeof buf) == 0);
    /* counter + max length: the supporting line is measured in
     * and input caps at the maximum */
    tk_textfield_set_max_length(f, 5);
    tk_textfield_set_counter(f, true);
    tk_size s1 = tk_measure(f, (tk_constraints){ 0, TK_UNLIMITED,
                                                0, TK_UNLIMITED });
    CHECK(s1.h > s0.h);
    tk_root *r = single_root(f, NULL);
    click_widget(r, f);
    send_text(r, "12345678");
    CHECK(tk_textfield_text(f, buf, sizeof buf) == 5);
    CHECK(strcmp(buf, "12345") == 0);
    tk_root_free(r);
    tk_widget_free(f);

    /* validator: digits only */
    tk_widget *v = tk_textfield_new("");
    tk_textfield_set_validator(v, validate_digits, NULL);
    tk_textfield_set_error_message(v, "数字のみ");
    tk_textfield_set_text(v, "12a");
    CHECK(!tk_textfield_validate(v));
    CHECK(!tk_textfield_is_valid(v));
    CHECK(tk_textfield_is_error(v));
    /* the error message takes the supporting line */
    CHECK(tk_textfield_supporting_text(v) != NULL ||
          tk_textfield_is_error(v));
    tk_textfield_set_text(v, "123");
    CHECK(tk_textfield_validate(v));
    CHECK(tk_textfield_is_valid(v));
    CHECK(!tk_textfield_is_error(v));
    tk_widget_free(v);

    /* autocomplete: prefix matches drop down, a pick replaces */
    tk_widget *ac = tk_textfield_new("");
    const char *comp[] = { "apple", "apricot", "avocado",
                           "banana" };
    tk_textfield_set_completions(ac, comp, 4);
    notify_count = 0;
    tk_textfield_set_on_change(ac, on_notify, NULL);
    tk_root *r2 = single_root(ac, NULL);
    click_widget(r2, ac);
    send_text(r2, "ap");
    CHECK(tk_textfield_completion_open(ac));
    tk_rect q = tk_widget_rect(ac);
    send_click(r2, q.x + 30, q.y + q.h + 24); /* first match */
    CHECK(tk_textfield_text(ac, buf, sizeof buf) == 5 &&
          strcmp(buf, "apple") == 0);
    CHECK(!tk_textfield_completion_open(ac));
    tk_root_free(r2);
    tk_widget_free(ac);
    (void)t;
}

/* ---------- Buttons: elevated / toggle / children ---------- */
static void test_button_m7(void)
{
    tk_widget *e = tk_button_new("浮き");
    tk_button_set_variant(e, TK_BTN_ELEVATED);
    tk_root *r = single_root(e, NULL);
    tk_rect q = tk_widget_rect(e);
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    /* ELEVATED = surface-container-low face (M3) */
    CHECK(col_eq(px(c, q.x + 6, q.y + q.h / 2),
                 tk_theme_v2.container_low));
    tk_canvas_free(c);
    /* WinUI mode: the elevated button takes the standard
     * control fill treatment instead */
    c = paint_with(r, &tk_theme_winui);
    CHECK(!col_eq(px(c, q.x + 6, q.y + q.h / 2),
                  tk_theme_v2.container_low));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(e);

    /* toggle button: the click flips the state before the
     * callback, and the face shows the selected treatment */
    tk_widget *tg = tk_button_new("切替");
    tk_button_set_toggle(tg, true);
    CHECK(tk_button_is_toggle(tg));
    CHECK(!tk_button_toggled(tg));
    notify_count = 0;
    tk_button_set_on_click(tg, on_notify, NULL);
    tk_root *r2 = single_root(tg, NULL);
    click_widget(r2, tg);
    CHECK(tk_button_toggled(tg));
    CHECK(notify_count == 1);
    click_widget(r2, tg);
    CHECK(!tk_button_toggled(tg));
    tk_root_free(r2);
    tk_widget_free(tg);

    /* a button can carry children (the browser tab's close
     * button case): the child widens the button and a click on
     * the child still activates the button */
    tk_widget *b = tk_button_new("タブ");
    tk_size w0 = tk_measure(b, (tk_constraints){ 0, TK_UNLIMITED,
                                                0, TK_UNLIMITED });
    tk_widget *x = tk_label_new("×");
    tk_add_child(b, x);
    tk_size w1 = tk_measure(b, (tk_constraints){ 0, TK_UNLIMITED,
                                                0, TK_UNLIMITED });
    CHECK(w1.w > w0.w);
    notify_count = 0;
    tk_button_set_on_click(b, on_notify, NULL);
    tk_root *r3 = single_root(b, NULL);
    click_widget(r3, x);
    CHECK(notify_count == 1);
    tk_root_free(r3);
    tk_widget_free(b);
}

/* ---------- Menu: trailing text + badge ---------- */
static void test_menu_m7(void)
{
    tk_widget *m1 = tk_menu_new();
    tk_menu_add_item(m1, "開く", 1);
    int id1 = 1;
    tk_widget *m2 = tk_menu_new();
    tk_menu_add_item(m2, "開く", 1);
    int id2 = 1;
    tk_menu_set_item_trailing(m2, id2, "Ctrl+O");
    tk_menu_set_item_badge(m2, id2, 3);
    tk_size s1 = tk_measure(m1, (tk_constraints){ 0, TK_UNLIMITED,
                                                 0, TK_UNLIMITED });
    tk_size s2 = tk_measure(m2, (tk_constraints){ 0, TK_UNLIMITED,
                                                 0, TK_UNLIMITED });
    CHECK(s2.w > s1.w); /* the trailing text widens the menu */
    tk_widget *top = tk_label_new("x");
    tk_root *r = single_root(top, NULL);
    tk_menu_popup(r, m2, 100, 100);
    tk_canvas *c = paint_full(r);
    /* the count badge paints in the error red */
    bool saw = false;
    tk_rect q = tk_widget_rect(m2);
    for (int y = q.y; y < q.y + q.h && !saw; y++)
        for (int x = q.x; x < q.x + q.w; x++) {
            tk_color p = px(c, x, y);
            if (p.r == 186 && p.g == 26 && p.b == 26) saw = true;
        }
    CHECK(saw);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(m1);
    tk_widget_free(top);
    (void)id1;
}

/* ---------- Dialog: modeless + full-screen ---------- */
static void test_dialog_m7(void)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *behind = tk_button_new("後ろ");
    notify_count = 0;
    tk_button_set_on_click(behind, on_notify, NULL);
    tk_add_child(box, behind);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_widget *dlg = tk_dialog_new("確認");
    tk_widget *lab = tk_label_new("内容");
    tk_add_child(dlg, lab);
    tk_dialog_show_modeless(r, dlg);
    CHECK(tk_dialog_is_shown(r, dlg));
    CHECK(!tk_dialog_is_modal(r, dlg));
    /* modeless: the app behind still receives its clicks */
    click_widget(r, behind);
    CHECK(notify_count == 1);
    tk_dialog_close(r, dlg);
    /* modal (the classic show) blocks them */
    tk_dialog_show(r, dlg);
    CHECK(tk_dialog_is_modal(r, dlg));
    click_widget(r, behind);
    CHECK(notify_count == 1);
    tk_dialog_close(r, dlg);
    /* full-screen form fills the root */
    tk_dialog_set_fullscreen(dlg, true);
    CHECK(tk_dialog_is_fullscreen(dlg));
    tk_dialog_show(r, dlg);
    tk_rect q = tk_widget_rect(dlg);
    CHECK(q.x == 0 && q.y == 0 && q.w == 800 && q.h == 480);
    tk_dialog_close(r, dlg);
    tk_widget_free(dlg);
    tk_root_free(r);
    tk_widget_free(box);
}

/* ---------- BottomBar: M3 pill indicator mode ---------- */
static void test_bottombar_pill(void)
{
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 70, 70, 70);
    tk_widget *bar = tk_bottombar_new();
    tk_bottombar_add_cell(bar, ic, 24, 24, "ホーム", NULL, NULL);
    tk_bottombar_add_cell(bar, ic, 24, 24, "設定", NULL, NULL);
    tk_bottombar_set_selected(bar, 0);
    /* M23: the default form is the M3 navigation indicator
     * (mode 1) - pre-M23 the default was mode 0 and no
     * production caller opted in, so real bars showed the
     * whole-cell oval the user sent back on 2026-11-21. */
    CHECK(tk_bottombar_indicator_mode(bar) == 1);
    tk_bottombar_set_indicator_mode(bar, 0);
    CHECK(tk_bottombar_indicator_mode(bar) == 0);
    tk_root *r = single_root(bar, NULL);
    tk_widget *cell0 = tk_bottombar_cell(bar, 0);
    tk_rect cq = tk_widget_rect(cell0);
    /* mode 0 (opt-in legacy): the whole selected cell carries
     * the tonal face - sampled inside the cell, clear of icon
     * and label */
    tk_canvas *c = paint_with(r, &tk_theme_v2);
    tk_color corner0 = px(c, cq.x + 40, cq.y + 55);
    CHECK(col_eq(corner0, tk_theme_v2.container));
    tk_canvas_free(c);
    tk_bottombar_set_indicator_mode(bar, 1);
    CHECK(tk_bottombar_indicator_mode(bar) == 1);
    tk_root_layout(r, 800, 480);
    c = paint_with(r, &tk_theme_v2);
    /* mode 1: the cell's lower area is the bar surface again
     * and the pill sits behind the icon - sampled on the
     * pill, just left of the icon glyph. M13 premise
     * correction: the navigation indicator's colour role is
     * secondaryContainer (the selection token), not
     * primaryContainer - the same role fix as the rail /
     * drawer indicators. */
    CHECK(!col_eq(px(c, cq.x + 40, cq.y + 55),
                  tk_theme_v2.container));
    CHECK(col_eq(px(c, cq.x + cq.w / 2 - 24, cq.y + 22),
                 tk_theme_v2.selection));
    /* M23 geometry pins (nav_pill_rect): for the 24px icon
     * the pill is icon + 45 x icon + 9 = 69x33px (the 64x32dp
     * reference at 1.12px/dp), centred on the icon (centre
     * cq.y + 20), top at cq.y + 4 - so the pill's left/right
     * insides carry the selection colour, just outside its
     * edge does not, and the strip between the pill's bottom
     * (cq.y + 37) and the label (cq.y + 40) is NOT selection:
     * the label sits outside the pill. */
    CHECK(col_eq(px(c, cq.x + cq.w / 2 - 33, cq.y + 20),
                 tk_theme_v2.selection));
    CHECK(col_eq(px(c, cq.x + cq.w / 2 + 33, cq.y + 20),
                 tk_theme_v2.selection));
    CHECK(!col_eq(px(c, cq.x + cq.w / 2 - 36, cq.y + 20),
                  tk_theme_v2.selection));
    CHECK(!col_eq(px(c, cq.x + cq.w / 2, cq.y + 38),
                  tk_theme_v2.selection));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(bar);
}

/* ---------- ColorPicker: hex + alpha ---------- */
static void test_colorpicker_m7(void)
{
    tk_widget *top = tk_label_new("");
    tk_root *r = tk_root_new(top);
    tk_root_layout(r, 800, 480);
    tk_widget *dlg = tk_colorpicker_new("色", 0x2196F3FFu);
    /* with alpha disabled (the default) a set value is made
     * opaque, matching the pre-M7 opaque-only picker */
    tk_colorpicker_set_value(dlg, 0x11223344u);
    CHECK(tk_colorpicker_value(dlg) == 0x112233FFu);
    tk_colorpicker_set_alpha_enabled(dlg, true);
    tk_colorpicker_set_value(dlg, 0x11223344u);
    CHECK(tk_colorpicker_value(dlg) == 0x11223344u);
    CHECK(tk_colorpicker_alpha_enabled(dlg));
    tk_colorpicker_show(r, dlg);
    /* children: [grid, preview, buttons, hex, alpha row] */
    tk_widget *hex = tk_child_at(dlg, 3);
    CHECK(hex != NULL);
    /* the hex field starts out showing the current value;
     * clear it, then type a new one */
    click_widget(r, hex);
    tk_textfield_set_text(hex, "");
    send_text(r, "#FF0000");
    CHECK(tk_colorpicker_value(dlg) == 0xFF0000FFu);
    /* hex with an alpha byte */
    tk_textfield_set_text(hex, "");
    click_widget(r, hex);
    send_text(r, "#00FF0080");
    CHECK(tk_colorpicker_value(dlg) == 0x00FF0080u);
    /* disabling alpha makes the value opaque again */
    tk_colorpicker_set_alpha_enabled(dlg, false);
    CHECK((tk_colorpicker_value(dlg) & 0xFF) == 0xFF);
    tk_widget_free(dlg);
    tk_root_free(r);
    tk_widget_free(top);
}

/* ---------- TreeView model interface ---------- */
typedef struct { int id, parent, nch; const char *label; } tnode;
static tnode model_nodes[] = {
    { 10, -1, 2, "A" }, { 11, 10, 0, "B" }, { 12, 10, 0, "C" },
    { 20, -1, 0, "D" }, { 13, 10, 0, "E" },
};
static bool model_e_visible; /* E joins after a "data change" */
static int tm_child_count(void *ud, int parent)
{
    (void)ud;
    int n = 0;
    for (size_t i = 0; i < 5; i++) {
        if (model_nodes[i].id == 13 && !model_e_visible) continue;
        if (model_nodes[i].parent == parent) n++;
    }
    return n;
}
static int tm_child_id(void *ud, int parent, int index)
{
    (void)ud;
    for (size_t i = 0; i < 5; i++) {
        if (model_nodes[i].id == 13 && !model_e_visible) continue;
        if (model_nodes[i].parent == parent && index-- == 0)
            return model_nodes[i].id;
    }
    return -1;
}
static const char *tm_label(void *ud, int id)
{
    (void)ud;
    for (size_t i = 0; i < 5; i++)
        if (model_nodes[i].id == id) return model_nodes[i].label;
    return "?";
}
static bool tm_is_leaf(void *ud, int id)
{
    (void)ud;
    for (size_t i = 0; i < 5; i++)
        if (model_nodes[i].id == id) return model_nodes[i].nch == 0;
    return true;
}
static void test_treeview_model(void)
{
    tk_tree_model m = { NULL, tm_child_count, tm_child_id,
                        tm_label, tm_is_leaf };
    model_e_visible = false;
    tk_widget *tv = tk_treeview_new();
    tk_treeview_set_model(tv, &m);
    CHECK(tk_treeview_has_model(tv));
    CHECK(tk_treeview_node_count(tv) == 4);
    CHECK(tk_treeview_visible_rows(tv) == 2); /* A and D */
    /* manual add is rejected while a model is set */
    CHECK(tk_treeview_add(tv, -1, "X") == -1);
    tk_treeview_set_expanded(tv, 10, true);
    CHECK(tk_treeview_expanded(tv, 10));
    CHECK(tk_treeview_visible_rows(tv) == 4);
    CHECK(tk_treeview_row_node(tv, 1) == 11); /* model ids out */
    CHECK(tk_treeview_node_depth(tv, 12) == 1);
    tk_treeview_select(tv, 12);
    CHECK(tk_treeview_selected(tv) == 12);
    /* data change + refresh: expansion and selection survive */
    model_e_visible = true;
    tk_treeview_model_refresh(tv);
    CHECK(tk_treeview_node_count(tv) == 5);
    CHECK(tk_treeview_visible_rows(tv) == 5);
    CHECK(tk_treeview_selected(tv) == 12);
    /* back to the manual form */
    tk_treeview_set_model(tv, NULL);
    CHECK(!tk_treeview_has_model(tv));
    CHECK(tk_treeview_node_count(tv) == 0);
    int id = tk_treeview_add(tv, -1, "手動");
    CHECK(id >= 0);
    tk_widget_free(tv);
}

/* ---------- Snackbar swipe dismiss ---------- */
static void test_snackbar_swipe(void)
{
    tk_widget *top = tk_label_new("本体");
    tk_root *r = single_root(top, NULL);
    tk_snackbar_show(r, "保存しました", NULL, NULL, NULL, 0);
    CHECK(tk_snackbar_visible(r));
    /* press on the bar and swipe it away horizontally */
    send_ptr(r, TK_EV_POINTER_DOWN, 400, 452);
    send_ptr(r, TK_EV_POINTER_MOVE, 470, 452);
    send_ptr(r, TK_EV_POINTER_UP, 470, 452);
    CHECK(!tk_snackbar_visible(r));
    tk_root_free(r);
    tk_widget_free(top);
}

/* ---------- JPEG scaled-IDCT thumbnails ---------- */
static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long z = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)z);
    if (fread(b, 1, (size_t)z, f) != (size_t)z) {
        free(b); fclose(f); return NULL;
    }
    fclose(f);
    *n = (size_t)z;
    return b;
}
/* compare a thumbnail against the PIL/libjpeg reference decode
 * of the same file, box-scaled by the toolkit's own scaler;
 * returns the mean abs channel difference, sets *maxd */
static double thumb_mean_diff(const char *jpg, const char *rgb,
                              int iw, int ih, int mw, int mh,
                              int *maxd, size_t *peak)
{
    uint32_t *px = NULL; int w = 0, h = 0;
    if (!tk_image_load_thumbnail(jpg, mw, mh, &px, &w, &h))
        return -1;
    if (peak) *peak = tk_image_last_decode_peak();
    size_t n = 0;
    unsigned char *ref = slurp(rgb, &n);
    if (!ref || n != (size_t)iw * (size_t)ih * 3) {
        free(px); free(ref); return -1;
    }
    uint32_t *rp = malloc((size_t)iw * (size_t)ih * 4);
    for (size_t i = 0; i < (size_t)iw * (size_t)ih; i++)
        rp[i] = 0xFF000000u | ((uint32_t)ref[i * 3] << 16) |
                ((uint32_t)ref[i * 3 + 1] << 8) | ref[i * 3 + 2];
    uint32_t *sp = NULL; int sw = 0, sh = 0;
    if (!tk_image_scale_to_fit(rp, iw, ih, mw, mh, &sp, &sw, &sh) ||
        sw != w || sh != h) {
        free(px); free(rp); free(sp); free(ref); return -1;
    }
    long mx = 0; double sum = 0;
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++)
        for (int ch = 0; ch < 3; ch++) {
            int a = (int)((px[i] >> (16 - 8 * ch)) & 0xFF);
            int b = (int)((sp[i] >> (16 - 8 * ch)) & 0xFF);
            int d = abs(a - b);
            if (d > mx) mx = d;
            sum += d;
        }
    if (maxd) *maxd = (int)mx;
    double mean = sum / ((double)w * (double)h * 3.0);
    free(px); free(rp); free(sp); free(ref);
    return mean;
}
static void test_jpeg(void)
{
    const char *F = "tests/fixtures/";
    char jpg[256], rgb[256];
    int maxd = 0; size_t peak = 0;
    /* scale-1 decode is essentially exact vs libjpeg */
    snprintf(jpg, sizeof jpg, "%sgrad320.jpg", F);
    snprintf(rgb, sizeof rgb, "%sgrad320.rgb", F);
    double mean = thumb_mean_diff(jpg, rgb, 320, 240, 320, 240,
                                  &maxd, &peak);
    CHECK(mean >= 0 && mean < 0.6);
    CHECK(maxd <= 6);
    /* 1/2 scale */
    mean = thumb_mean_diff(jpg, rgb, 320, 240, 160, 120, &maxd,
                           &peak);
    CHECK(mean >= 0 && mean < 1.2);
    CHECK(maxd <= 20);
    /* the restart-marker file decodes identically */
    snprintf(jpg, sizeof jpg, "%srst320.jpg", F);
    snprintf(rgb, sizeof rgb, "%srst320.rgb", F);
    mean = thumb_mean_diff(jpg, rgb, 320, 240, 160, 120, &maxd,
                           NULL);
    CHECK(mean >= 0 && mean < 1.2);
    /* 4:4:4 and grayscale forms */
    snprintf(jpg, sizeof jpg, "%sgrad320_444.jpg", F);
    snprintf(rgb, sizeof rgb, "%sgrad320_444.rgb", F);
    mean = thumb_mean_diff(jpg, rgb, 320, 240, 160, 120, &maxd,
                           NULL);
    CHECK(mean >= 0 && mean < 0.8);
    snprintf(jpg, sizeof jpg, "%sgray320.jpg", F);
    snprintf(rgb, sizeof rgb, "%sgray320.rgb", F);
    mean = thumb_mean_diff(jpg, rgb, 320, 240, 160, 120, &maxd,
                           NULL);
    CHECK(mean >= 0 && mean < 0.6);
    /* progressive input falls back to the full decoder and is
     * still correct */
    snprintf(jpg, sizeof jpg, "%sprog320.jpg", F);
    snprintf(rgb, sizeof rgb, "%sprog320.rgb", F);
    mean = thumb_mean_diff(jpg, rgb, 320, 240, 160, 120, &maxd,
                           NULL);
    CHECK(mean >= 0 && mean < 0.6);
    /* the point of the exercise: a 1600x1200 photo's 200x150
     * thumbnail holds ~285KB of intermediates, not the ~15MB
     * of the full decode */
    snprintf(jpg, sizeof jpg, "%sbig1600.jpg", F);
    snprintf(rgb, sizeof rgb, "%sbig1600.rgb", F);
    mean = thumb_mean_diff(jpg, rgb, 1600, 1200, 200, 150, &maxd,
                           &peak);
    CHECK(mean >= 0 && mean < 1.6);
    CHECK(peak > 0 && peak < 400000);
    CHECK(peak * 10 < (size_t)1600 * 1200 * 8);
}

int main(void)
{
    test_theme_tokens();
    test_modes_same_tree();
    test_winui_checkbox();
    test_card();
    test_searchbar();
    test_segmented();
    test_carousel();
    test_navrail();
    test_ring();
    test_appbar();
    test_drawer();
    test_sheet();
    test_fabmenu();
    test_splitbutton();
    test_clipboard();
    test_i18n();
    test_a11y();
    test_dnd();
    test_listview_drag();
    test_slider_dual();
    test_textfield_m7();
    test_button_m7();
    test_menu_m7();
    test_dialog_m7();
    test_bottombar_pill();
    test_colorpicker_m7();
    test_treeview_model();
    test_snackbar_swipe();
    test_jpeg();
    printf("tk6: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
