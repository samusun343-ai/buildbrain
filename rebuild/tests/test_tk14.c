/* test_tk14.c - M15 verification: the F (keyboard) paths
 * built this line, and the E/B/D cells that lacked per-mode
 * pixel evidence.
 *
 * Keyboard contracts verified here come from the primary
 * sources, fetched 2026-11-20:
 * - Modal sheet: developer.android.com, ModalBottomSheet-
 *   Properties - isFocusable defaults to true (the sheet
 *   receives key presses) and shouldDismissOnBackPress
 *   defaults to true; the WinUI Flyout's light dismiss
 *   includes the Esc key (Microsoft Learn, Flyout class).
 * - Drawer: the items are focusable controls in both
 *   references (NavigationViewItem; M3 drawer items), and
 *   the official Compose ModalNavigationDrawer sample
 *   returns keyboard focus to the invoker when the drawer
 *   closes (developer.android.com).
 * - FAB menu: the popup-menu keyboard model (this toolkit's
 *   Menu, #21, verified in M14) - focus on an item, arrows
 *   move, Esc dismisses and returns focus.
 * - Snackbar: m3.material.io snackbar guidelines ("They
 *   shouldn't interrupt the user experience") and the
 *   Android Developers Snackbar contract - the action is a
 *   button reached by moving the keyboard focus indicator
 *   through the page's interactive elements; showing the
 *   bar never steals focus, and Esc is not a dismiss key.
 * - WinUI ProgressBar indeterminate: Microsoft Learn,
 *   progress controls - the indeterminate state shows "a
 *   repeating pattern" / bars moving across the control;
 *   no keyframes are published, so the D evidence is the
 *   phase-driven pixel motion itself.
 * - WinUI InfoBar (the snackbar's WinUI form) has no
 *   entrance motion: the D evidence is pixel stability
 *   across a live clock.
 */
#include "tk.h"

#include <math.h>
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
/* pixels that differ between two same-size canvases */
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
static void send_down(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_DOWN, x, y);
}
static void send_up(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_UP, x, y);
}
static void key(tk_root *r, tk_key k)
{
    tk_event e = { 0 };
    e.type = TK_EV_KEY_DOWN; e.key = k;
    tk_root_event(r, &e);
}
static void key_mod(tk_root *r, tk_key k, uint32_t mod)
{
    tk_event e = { 0 };
    e.type = TK_EV_KEY_DOWN; e.key = k; e.modifiers = mod;
    tk_root_event(r, &e);
}
static int notify_count;
static void on_notify(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    notify_count++;
}
static void solid_icon(uint32_t *buf, int n, int rr, int gg, int bb)
{
    for (int i = 0; i < n; i++)
        buf[i] = 0xFF000000u | ((uint32_t)rr << 16) |
                 ((uint32_t)gg << 8) | (uint32_t)bb;
}

/* ---------- Drawer: keyboard operation (F) -------------------- */
static void drawer_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *ob = tk_button_new("外側");
    tk_add_child(host, ob);
    tk_root *r = root_of(host);
    tk_widget *d = tk_drawer_new("メニュー");
    tk_drawer_add_item(d, "ホーム", 10, NULL, 0, 0);
    tk_drawer_add_separator(d);
    tk_drawer_add_item(d, "設定", 20, NULL, 0, 0);
    tk_menu_set_item_enabled(d, 20, false);
    tk_drawer_add_item(d, "ヘルプ", 30, NULL, 0, 0);
    notify_count = 0;
    tk_drawer_set_on_select(d, on_notify, NULL);
    CHECK(tk_focus(r, ob));
    /* modal show: focus moves inside, the trap holds */
    tk_drawer_show(r, d, true);
    CHECK(tk_drawer_is_shown(r, d));
    CHECK(tk_focused(r) == d);
    CHECK(!tk_focus(r, ob));
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == d); /* the drawer is the only stop */
    /* the cursor starts on the first selectable item; Down
     * skips the separator and the disabled item (the rows:
     * headline 16..72, item rows of 64 from y=72, separator
     * 17 - so ヘルプ's row is 217..281, ホーム's 72..136) */
    tk_canvas *c1 = paint_full(r);
    key(r, TK_KEY_DOWN);
    tk_canvas *c2 = paint_full(r);
    CHECK(diff_count(c1, c2, (tk_rect){ 0, 217, 300, 64 }) > 5);
    CHECK(diff_count(c1, c2, (tk_rect){ 0, 72, 300, 64 }) > 5);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    /* Down at the last item stops (no wrap) */
    key(r, TK_KEY_DOWN);
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    CHECK(tk_drawer_last_id(d) == 30);
    CHECK(tk_drawer_selected(d) == 30);
    CHECK(!tk_drawer_is_shown(r, d)); /* modal pick dismisses */
    CHECK(tk_focused(r) == ob);       /* focus returns */
    /* Esc dismisses without a pick, focus returns */
    tk_drawer_show(r, d, true);
    CHECK(tk_focused(r) == d);
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_drawer_is_shown(r, d));
    CHECK(notify_count == 1);
    CHECK(tk_focused(r) == ob);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(d);
    /* standard (attached) form: a Tab stop in the layout;
     * Enter selects in place, Esc does not dismiss */
    tk_widget *hbox = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
    tk_widget *d2 = tk_drawer_new(NULL);
    tk_drawer_add_item(d2, "第一", 1, NULL, 0, 0);
    tk_drawer_add_item(d2, "第二", 2, NULL, 0, 0);
    tk_drawer_attach(hbox, d2);
    tk_widget *b2 = tk_button_new("隣");
    tk_add_child(hbox, b2);
    notify_count = 0;
    tk_drawer_set_on_select(d2, on_notify, NULL);
    tk_root *r2 = root_of(hbox);
    CHECK(tk_focus_next(r2));
    CHECK(tk_focused(r2) == d2);
    key(r2, TK_KEY_DOWN);
    key(r2, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    CHECK(tk_drawer_selected(d2) == 2);
    key(r2, TK_KEY_ESCAPE); /* persistent panel: no dismissal */
    CHECK(tk_drawer_selected(d2) == 2);
    CHECK(tk_focused(r2) == d2);
    tk_root_free(r2);
    tk_widget_free(hbox);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_drawer(void)
{
    drawer_flow(&tk_theme_v2);
    drawer_flow(&tk_theme_winui);
}

/* ---------- Sheet: modal focus + Esc (F) ---------------------- */
static void sheet_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *ob = tk_button_new("外側");
    tk_add_child(host, ob);
    tk_root *r = root_of(host);
    CHECK(tk_focus(r, ob));
    tk_widget *sh = tk_sheet_new();
    tk_widget *b1 = tk_button_new("第一");
    tk_widget *b2 = tk_button_new("第二");
    tk_add_child(sh, b1);
    tk_add_child(sh, b2);
    notify_count = 0;
    tk_sheet_set_on_dismiss(sh, on_notify, NULL);
    tk_sheet_show(r, sh, true);
    CHECK(tk_sheet_is_shown(r, sh));
    /* focus moves to the first focusable content; the trap
     * holds and Tab cycles the content only */
    CHECK(tk_focused(r) == b1);
    CHECK(!tk_focus(r, ob));
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b2);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b1);
    /* Esc (the Back equivalent) dismisses; on_dismiss fires
     * and the focus returns to the invoker */
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_sheet_is_shown(r, sh));
    CHECK(notify_count == 1);
    CHECK(tk_focused(r) == ob);
    tk_widget_free(sh);
    /* an empty modal sheet takes the focus itself */
    tk_widget *sh2 = tk_sheet_new();
    tk_sheet_show(r, sh2, true);
    CHECK(tk_focused(r) == sh2);
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_sheet_is_shown(r, sh2));
    CHECK(tk_focused(r) == ob);
    tk_widget_free(sh2);
    /* modeless: no focus steal, no Esc dismissal */
    tk_widget *sh3 = tk_sheet_new();
    tk_widget *b3 = tk_button_new("中身");
    tk_add_child(sh3, b3);
    tk_sheet_show(r, sh3, false);
    CHECK(tk_sheet_is_shown(r, sh3));
    CHECK(tk_focused(r) == ob);
    CHECK(tk_focus(r, b3));
    key(r, TK_KEY_ESCAPE);
    CHECK(tk_sheet_is_shown(r, sh3));
    tk_sheet_close(r, sh3);
    tk_widget_free(sh3);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_sheet(void)
{
    sheet_flow(&tk_theme_v2);
    sheet_flow(&tk_theme_winui);
}

/* ---------- FAB menu: keyboard model (F) + surface (B) -------- */
static int fab_counts[3];
static void on_fab_item(tk_widget *w, void *ud)
{
    (void)w;
    (*(int *)ud)++;
}
static void fabmenu_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *ob = tk_button_new("開く");
    tk_add_child(host, ob);
    tk_root *r = root_of(host);
    tk_widget *fm = tk_fabmenu_new();
    fab_counts[0] = fab_counts[1] = fab_counts[2] = 0;
    tk_fabmenu_add_item(fm, NULL, 0, 0, "追加", on_fab_item,
                        &fab_counts[0]);
    tk_fabmenu_add_item(fm, NULL, 0, 0, "編集", on_fab_item,
                        &fab_counts[1]);
    tk_fabmenu_add_item(fm, NULL, 0, 0, "削除", on_fab_item,
                        &fab_counts[2]);
    CHECK(tk_focus(r, ob));
    tk_fabmenu_show(r, fm);
    CHECK(tk_fabmenu_is_shown(r, fm));
    /* the focus sits on the first item */
    tk_widget *f0 = tk_focused(r);
    CHECK(f0 && f0 != ob);
    /* B: the surface - face colour in the padding corner,
     * item label glyphs, and the shadow silhouette beside
     * the box (painted against the menu hidden again) */
    tk_rect q = tk_widget_rect(fm);
    tk_canvas *c = paint_full(r);
    /* the face at the top centre (the corner arc leaves the
     * corner pixels themselves to the backdrop) */
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 3), t->surface, 8));
    CHECK(glyphs_near(c, q, t->text) +
          glyphs_near(c, q, t->accent) > 10);
    tk_canvas *c_now = paint_full(r);
    tk_fabmenu_hide(r, fm);
    tk_canvas *c_off = paint_full(r);
    CHECK(diff_count(c_now, c_off,
                     (tk_rect){ q.x + q.w, q.y, 4, q.h }) > 3);
    tk_canvas_free(c);
    tk_canvas_free(c_now);
    tk_canvas_free(c_off);
    CHECK(tk_focused(r) == ob); /* hide returned the focus */
    /* arrows move between items and wrap; Tab cycles too */
    tk_fabmenu_show(r, fm);
    CHECK(tk_focused(r) == f0);
    key(r, TK_KEY_DOWN);
    tk_widget *f1 = tk_focused(r);
    CHECK(f1 && f1 != f0);
    key(r, TK_KEY_DOWN);
    tk_widget *f2 = tk_focused(r);
    CHECK(f2 && f2 != f1 && f2 != f0);
    key(r, TK_KEY_DOWN); /* wraps to the first item */
    CHECK(tk_focused(r) == f0);
    key(r, TK_KEY_UP);   /* and back to the last */
    CHECK(tk_focused(r) == f2);
    key(r, TK_KEY_TAB);  /* Tab cycles inside the popup */
    CHECK(tk_focused(r) == f0);
    key_mod(r, TK_KEY_TAB, TK_MOD_SHIFT);
    CHECK(tk_focused(r) == f2);
    /* Enter on the first item fires it, closes, refocuses */
    key(r, TK_KEY_UP);
    CHECK(tk_focused(r) == f1);
    key(r, TK_KEY_UP);
    CHECK(tk_focused(r) == f0);
    key(r, TK_KEY_ENTER);
    CHECK(fab_counts[0] == 1);
    CHECK(!tk_fabmenu_is_shown(r, fm));
    CHECK(tk_focused(r) == ob);
    /* Esc dismisses without firing */
    tk_fabmenu_show(r, fm);
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_fabmenu_is_shown(r, fm));
    CHECK(fab_counts[0] == 1 && fab_counts[1] == 0 &&
          fab_counts[2] == 0);
    CHECK(tk_focused(r) == ob);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(fm);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_fabmenu(void)
{
    fabmenu_flow(&tk_theme_v2);
    fabmenu_flow(&tk_theme_winui);
}

/* ---------- Snackbar: action as a tab stop (F) + ring (B) ----- */
static int snack_fires;
static void on_snack_act(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    snack_fires++;
}
/* the bar rect follows snack_layout_rects: x=16, w=560,
 * M3 h=56 at y=408, WinUI (InfoBar) h=69 at y=395 */
static tk_rect snack_bar_rect(const tk_theme *t)
{
    /* M22: the bar heights follow the corrected premise
     * (48epx -> 65px / 48dp -> 54px, AUDIT SS20) */
    return t->style == 2 ? (tk_rect){ 16, 399, 560, 65 }
                         : (tk_rect){ 16, 410, 560, 54 };
}
static void snackbar_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *b1 = tk_button_new("第一");
    tk_widget *b2 = tk_button_new("第二");
    tk_add_child(host, b1);
    tk_add_child(host, b2);
    tk_root *r = root_of(host);
    CHECK(tk_focus(r, b1));
    snack_fires = 0;
    tk_snackbar_show(r, "保存しました", "元に戻す", on_snack_act,
                     NULL, TK_SNACKBAR_INDEFINITE);
    CHECK(tk_snackbar_visible(r));
    CHECK(tk_focused(r) == b1); /* showing never steals focus */
    /* traversal: page stops first, the action last, then wrap */
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b2);
    CHECK(tk_focus_next(r));
    tk_widget *standin = tk_focused(r);
    CHECK(standin && standin != b1 && standin != b2);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b1);
    CHECK(tk_focus_next(r));
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == standin);
    /* B: the focused action carries the focus ring - the
     * ring's pixels appear around the action's glyphs
     * (the action label takes the accent role in both
     * modes' paints) */
    {
        tk_rect bar = snack_bar_rect(t);
        tk_canvas *cf = paint_full(r);
        tk_color ac = t->style == 2 ? t->accent
            : (t->inverse_primary.a ? t->inverse_primary
                                    : t->accent);
        int gx0 = -1, gx1 = -1;
        for (int y = bar.y; y < bar.y + bar.h; y++)
            for (int x = bar.x; x < bar.x + bar.w; x++)
                if (col_near_c(px(cf, x, y), ac, 45)) {
                    if (gx0 < 0 || x < gx0) gx0 = x;
                    if (x > gx1) gx1 = x;
                }
        CHECK(gx0 > 0 && gx1 > gx0);
        CHECK(tk_focus(r, b1));
        tk_canvas *cu = paint_full(r);
        /* the ring strokes the action rect inflated by 1:
         * its top edge crosses y = bar.y + 7 and its left
         * edge x = (glyph left - 12) - 1 */
        tk_rect top = { gx0 - 16, bar.y + 4, gx1 - gx0 + 32, 6 };
        tk_rect left = { gx0 - 16, bar.y + 8, 6, bar.h - 16 };
        CHECK(diff_count(cf, cu, top) + diff_count(cf, cu, left) > 0);
        tk_canvas_free(cf);
        tk_canvas_free(cu);
        CHECK(tk_focus_next(r));
        CHECK(tk_focus_next(r));
        CHECK(tk_focused(r) == standin);
    }
    /* Esc is not a dismiss key in either reference */
    key(r, TK_KEY_ESCAPE);
    CHECK(tk_snackbar_visible(r));
    CHECK(tk_focused(r) == standin);
    /* Enter fires exactly like the pointer path: hide, then
     * the callback; the dead stop releases the focus */
    key(r, TK_KEY_ENTER);
    CHECK(snack_fires == 1);
    CHECK(!tk_snackbar_visible(r));
    CHECK(tk_focused(r) == NULL);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b1);
    /* Space fires too */
    tk_snackbar_show(r, "削除しました", "元に戻す", on_snack_act,
                     NULL, TK_SNACKBAR_INDEFINITE);
    CHECK(tk_focus_next(r));
    CHECK(tk_focus_next(r));
    key(r, TK_KEY_SPACE);
    CHECK(snack_fires == 2);
    CHECK(!tk_snackbar_visible(r));
    /* M16 contract: a bar WITH an action never times out,
     * even with an explicit short duration - it survives far
     * past the deadline with its focus stop alive (Android
     * Developers: snackbars with an action "should not
     * timeout or self-dismiss until the user performs
     * another action"). An explicit hide releases the focus.
     * (Pre-M16 this block pinned the opposite, violating
     * behaviour; see REPORT-toolkit-m16.md.) */
    tk_root_tick(r, 1000);
    tk_snackbar_show(r, "短い", "実行", on_snack_act, NULL, 100);
    CHECK(tk_focus(r, b1));
    CHECK(tk_focus_next(r));
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) != b1 && tk_focused(r) != b2);
    tk_root_tick(r, 1200);
    CHECK(tk_snackbar_visible(r));
    CHECK(tk_focused(r) != b1 && tk_focused(r) != b2);
    tk_root_tick(r, 60000);
    CHECK(tk_snackbar_visible(r));
    tk_snackbar_hide(r);
    CHECK(tk_focused(r) == NULL);
    /* a bar without an action adds no stop */
    tk_snackbar_show(r, "通知のみ", NULL, NULL, NULL,
                     TK_SNACKBAR_INDEFINITE);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b1);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b2);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == b1);
    tk_snackbar_hide(r);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_snackbar(void)
{
    snackbar_flow(&tk_theme_v2);
    snackbar_flow(&tk_theme_winui);
}

/* ---------- Snackbar WinUI form: no entrance motion (D) ------- */
static void test_snackbar_d_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_add_child(host, tk_button_new("外側"));
    tk_root *r = root_of(host);
    tk_root_tick(r, 1000); /* a live clock: motion would run */
    tk_snackbar_show(r, "情報です", "詳細", on_snack_act, NULL,
                     TK_SNACKBAR_INDEFINITE);
    tk_rect bar = { 16, 395, 560, 69 };
    tk_canvas *c1 = paint_full(r);
    tk_root_tick(r, 1500);
    tk_canvas *c2 = paint_full(r);
    CHECK(diff_count(c1, c2, bar) == 0);
    tk_root_tick(r, 3000);
    tk_canvas *c3 = paint_full(r);
    CHECK(diff_count(c1, c3, bar) == 0);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    tk_canvas_free(c3);
    tk_snackbar_hide(r);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- ProgressBar indeterminate, WinUI form (D) --------- */
static int accent_run(tk_canvas *c, tk_rect q, tk_color ac,
                        int *leftmost)
{
    int n = 0, lm = -1;
    int y = q.y + q.h / 2;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, y), ac, 30)) {
            n++;
            if (lm < 0) lm = x;
        }
    if (leftmost) *leftmost = lm;
    return n;
}
static void test_progress_d_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    tk_widget *p = tk_progress_new(0.0f);
    tk_progress_set_indeterminate(p, true);
    tk_widget_set_min_size(p, 400, 28);
    tk_root *r = root_of(p);
    tk_rect q = tk_widget_rect(p);
    int lm = -1;
    tk_root_tick(r, 1000);
    tk_canvas *c = paint_full(r); /* starts the sweep, phase 0 */
    CHECK(accent_run(c, q, t->accent, &lm) == 0);
    tk_canvas_free(c);
    tk_root_tick(r, 1250); /* +250 ms of the 1200 ms sweep */
    c = paint_full(r);
    int lm2 = -1;
    CHECK(accent_run(c, q, t->accent, &lm2) > 30);
    CHECK(lm2 >= q.x && lm2 < q.x + 40);
    tk_canvas_free(c);
    tk_root_tick(r, 1500); /* +500 ms: the segment travelled */
    c = paint_full(r);
    int lm3 = -1;
    CHECK(accent_run(c, q, t->accent, &lm3) > 30);
    CHECK(lm3 > lm2 + 40);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(p);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Button (elevated / toggle): icon + label (E) ----- */
static void button_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *b = tk_button_new("保存");
    tk_button_set_variant(b, TK_BTN_ELEVATED);
    tk_button_set_icon(b, ic, 24, 24);
    tk_button_set_icon_layout(b, TK_BTN_ICON_ROW);
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(glyphs_near(c, q, t->accent) +
          glyphs_near(c, q, t->text) > 8);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(b);
    /* toggle: the ON state changes the face; icon and label
     * persist through it */
    tk_widget *tb = tk_button_new("切替");
    tk_button_set_toggle(tb, true);
    tk_button_set_icon(tb, ic, 24, 24);
    tk_button_set_icon_layout(tb, TK_BTN_ICON_ROW);
    r = root_of(tb);
    q = tk_widget_rect(tb);
    tk_canvas *c1 = paint_full(r);
    tk_button_set_toggled(tb, true);
    tk_canvas *c2 = paint_full(r);
    CHECK(diff_count(c1, c2, q) > 20);
    CHECK(count_near(c2, q, 255, 0, 255, 8) > 40);
    CHECK(glyphs_near(c2, q, t->accent) +
          glyphs_near(c2, q, t->text) > 8);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    tk_root_free(r);
    tk_widget_free(tb);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_button_e(void)
{
    button_e_flow(&tk_theme_v2);
    button_e_flow(&tk_theme_winui);
    button_e_flow(&tk_theme_winui_dark);
}

/* ---------- Checkbox / Radio / Switch labels, WinUI (E) ------- */
static void toggles_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *cb = tk_checkbox_new("有効化", true);
    tk_widget *rd = tk_radio_new("選択肢", 7, 1);
    tk_widget *sw = tk_switch_new("電源", true);
    tk_add_child(box, cb);
    tk_add_child(box, rd);
    tk_add_child(box, sw);
    tk_root *r = root_of(box);
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, tk_widget_rect(cb), t->text) > 8);
    CHECK(glyphs_near(c, tk_widget_rect(rd), t->text) > 8);
    CHECK(glyphs_near(c, tk_widget_rect(sw), t->text) > 8);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_toggles_e_winui(void)
{
    toggles_e_flow(&tk_theme_winui);
    toggles_e_flow(&tk_theme_winui_dark);
}

/* ---------- Split button: label + chevron shape (E) ----------- */
static void split_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sp = tk_splitbutton_new("保存", on_notify, NULL);
    tk_widget *m = tk_menu_new();
    tk_menu_add_item(m, "名前を付けて保存", 1);
    tk_splitbutton_set_menu(sp, m);
    tk_root *r = root_of(sp);
    tk_widget *main_btn = tk_splitbutton_main(sp);
    tk_widget *arrow = tk_splitbutton_arrow(sp);
    tk_rect qa = tk_widget_rect(arrow);
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, tk_widget_rect(main_btn),
                      t->on_accent) > 10);
    /* the chevron's shape signature, taken in the segment's
     * central region (clear of the rounded border): a small
     * centred mark whose arms reach both upper quadrants -
     * a text run or a filled block would fail the bbox */
    tk_rect reg = { qa.x + 5, qa.y + 5, qa.w - 10, qa.h - 10 };
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1, n = 0;
    for (int y = reg.y; y < reg.y + reg.h; y++)
        for (int x = reg.x; x < reg.x + reg.w; x++)
            if (col_near_c(px(c, x, y), t->on_accent, 40)) {
                n++;
                if (x0 < 0 || x < x0) x0 = x;
                if (y0 < 0 || y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
    CHECK(n > 10);
    CHECK(x1 - x0 >= 7 && x1 - x0 <= 16);
    CHECK(y1 - y0 >= 4 && y1 - y0 <= 12);
    CHECK(abs((x0 + x1) / 2 - (qa.x + qa.w / 2)) <= 3);
    tk_rect qtl = { reg.x, reg.y, reg.w / 2, reg.h / 2 };
    tk_rect qtr = { reg.x + reg.w / 2, reg.y,
                    reg.w - reg.w / 2, reg.h / 2 };
    CHECK(glyphs_near(c, qtl, t->on_accent) > 0);
    CHECK(glyphs_near(c, qtr, t->on_accent) > 0);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sp);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_split_e(void)
{
    split_e_flow(&tk_theme_v2);
    split_e_flow(&tk_theme_winui);
}

/* ---------- Date / Time picker: digit glyphs (E) -------------- */
static void picker_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *dp = tk_datepicker_new(2024, 2, 29);
    tk_root *r = root_of(dp);
    tk_rect q = tk_widget_rect(dp);
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) +
          glyphs_near(c, q, t->text_dim) > 15);
    /* the active field's selection face */
    CHECK(count_near(c, q, t->selection.r, t->selection.g,
                     t->selection.b, 30) > 50);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(dp);
    tk_widget *tp = tk_timepicker_new(true);
    tk_timepicker_set_value(tp, 12, 34, 56);
    r = root_of(tp);
    q = tk_widget_rect(tp);
    c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) +
          glyphs_near(c, q, t->text_dim) > 15);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(tp);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_picker_e(void)
{
    picker_e_flow(&tk_theme_v2);
    picker_e_flow(&tk_theme_winui);
}

/* ---------- Navigation rail: item icon + label (E) ------------ */
static void rail_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic1[24 * 24], ic2[24 * 24];
    solid_icon(ic1, 24 * 24, 255, 0, 255);
    solid_icon(ic2, 24 * 24, 0, 255, 255);
    tk_widget *rail = tk_navrail_new();
    tk_navrail_add(rail, "ホーム", ic1, 24, 24);
    tk_navrail_add(rail, "設定", ic2, 24, 24);
    tk_root *r = root_of(rail);
    tk_rect q = tk_widget_rect(rail);
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(count_near(c, q, 0, 255, 255, 8) > 40);
    if (t->style == 2) {
        /* the WinUI form is the NavigationView compact pane:
         * items are icons only - no label glyphs at all */
        CHECK(glyphs_near(c, q, t->text) +
              glyphs_near(c, q, t->text_dim) == 0);
    } else {
        CHECK(glyphs_near(c, q, t->text) +
              glyphs_near(c, q, t->text_dim) > 8);
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(rail);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_rail_e(void)
{
    rail_e_flow(&tk_theme_v2);
    rail_e_flow(&tk_theme_winui);
}

/* ---------- Carousel: item images (E) + label band (B) -------- */
static void carousel_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    static uint32_t red[200 * 200], grn[200 * 200],
                    blu[200 * 200];
    solid_icon(red, 200 * 200, 255, 0, 0);
    solid_icon(grn, 200 * 200, 0, 200, 0);
    solid_icon(blu, 200 * 200, 0, 0, 255);
    tk_widget *car = tk_carousel_new();
    tk_carousel_add(car, "一", red, 200, 200);
    tk_carousel_add(car, "二", grn, 200, 200);
    tk_carousel_add(car, "三", blu, 200, 200);
    tk_widget_set_min_size(car, 600, 200);
    tk_root *r = root_of(car);
    tk_rect q = tk_widget_rect(car);
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 0, 30) > 300);
    tk_rect band = { q.x, q.y + q.h - 34, q.w, 34 };
    /* the label: white glyphs on the band */
    CHECK(glyphs_near(c, band, (tk_color){ 255, 255, 255, 255 }) > 3);
    /* B: the band itself is the on-image scrim - black at
     * 110 over the item's red reads as darkened red */
    CHECK(count_near(c, band, 145, 0, 0, 45) > 20);
    tk_canvas_free(c);
    tk_carousel_set_index(car, 1);
    c = paint_full(r);
    CHECK(count_near(c, q, 0, 200, 0, 30) > 300);
    CHECK(count_near(c, q, 255, 0, 0, 30) < 100);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(car);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_carousel_e(void)
{
    carousel_e_flow(&tk_theme_v2);
    carousel_e_flow(&tk_theme_winui);
}

/* ---------- TabBar / Segmented labels, WinUI (E) -------------- */
static void tabs_e_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    tk_widget *tb = tk_tabbar_new();
    tk_tabbar_add(tb, "タブ1");
    tk_tabbar_add(tb, "タブ2");
    tk_tabbar_add(tb, "タブ3");
    tk_root *r = root_of(tb);
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, tk_widget_rect(tb), t->text) +
          glyphs_near(c, tk_widget_rect(tb), t->text_dim) > 15);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(tb);
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "日");
    tk_segmented_add(sg, "週");
    tk_segmented_add(sg, "月");
    tk_segmented_select(sg, 0);
    r = root_of(sg);
    c = paint_full(r);
    CHECK(glyphs_near(c, tk_widget_rect(sg), t->text) +
          glyphs_near(c, tk_widget_rect(sg), t->text_dim) +
          glyphs_near(c, tk_widget_rect(sg), t->accent) > 15);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Long-press: the held state surface (B) ------------ */
static int lp14_count;
static bool lp14_hook(void *ud, tk_event *ev)
{
    (void)ud;
    if (ev->type == TK_EV_LONGPRESS) { lp14_count++; return true; }
    return false;
}
static void longpress_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *b = tk_button_new("長押し");
    tk_widget_set_event_hook(b, lp14_hook, NULL);
    lp14_count = 0;
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_canvas *c0 = paint_full(r);
    tk_root_tick(r, 1000);
    send_down(r, cx, cy);
    tk_canvas *c1 = paint_full(r);
    /* the press state is on the target while held */
    int d_down = diff_count(c0, c1, q);
    CHECK(d_down > 20);
    tk_root_tick(r, 1000 + TK_LONGPRESS_MS - 1);
    CHECK(lp14_count == 0);
    tk_root_tick(r, 1000 + TK_LONGPRESS_MS);
    CHECK(lp14_count == 1);
    tk_canvas *c2 = paint_full(r);
    /* at the threshold the held (pressed) face persists */
    int d_held = diff_count(c0, c2, q);
    CHECK(d_held > 20);
    send_up(r, cx, cy);
    /* M17: the release starts the state's exit transition
     * (the M3 ripple's 150 ms fade-out; the WinUI brush's
     * 83 ms return) - the pressed face is gone only once it
     * completes, so advance past both before comparing (what
     * little difference then remains is the hover layer
     * under the pointer that is still over the button). The
     * pre-M17 form of this check painted immediately after
     * the release, when the toolkit's pressed face still
     * switched off instantly. The brush transition starts
     * at the first paint after the state change (the face
     * is a paint-time blend), so paint once here - as a
     * real frame loop would - before advancing. */
    tk_canvas *c2b = paint_full(r);
    tk_canvas_free(c2b);
    tk_root_tick(r, 1000 + TK_LONGPRESS_MS + 250);
    tk_canvas *c3 = paint_full(r);
    /* the comparison baseline is the HELD frame, not the
     * DOWN frame: with the M17 brush transition the DOWN
     * frame itself still shows the rest face (the 83 ms
     * transition has just started), so d_down no longer
     * measures the pressed face */
    CHECK(diff_count(c0, c3, q) * 4 < d_held);
    tk_canvas_free(c0);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    tk_canvas_free(c3);
    tk_root_free(r);
    tk_widget_free(b);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_longpress_b(void)
{
    longpress_flow(&tk_theme_v2);
    longpress_flow(&tk_theme_winui);
}

/* ---------- Drag & drop: the drag shadow (B) ------------------ */
static int drop14_fires;
static void on_drop14(tk_widget *target, void *ud, const char *mime,
                      const void *data, size_t len, int x, int y)
{
    (void)target; (void)ud; (void)mime; (void)data; (void)len;
    (void)x; (void)y;
    drop14_fires++;
}
static void dnd_b_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *src = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget_set_min_size(src, 800, 150);
    tk_add_child(src, tk_label_new("ドラッグ元"));
    tk_widget *dst = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget_set_min_size(dst, 800, 150);
    tk_add_child(dst, tk_label_new("ドロップ先"));
    tk_add_child(box, src);
    tk_add_child(box, dst);
    tk_widget_set_drag_source(src, "text/plain", "DATA", 4,
                              "アイテム");
    tk_widget_set_on_drop(dst, on_drop14, NULL);
    drop14_fires = 0;
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    send_ptr(r, TK_EV_POINTER_DOWN, 400, 60);
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 100);
    CHECK(tk_drag_active(r));
    /* the shadow: a floating face in the mode's role with
     * the drag label's glyphs on it */
    tk_canvas *c = paint_full(r);
    tk_color face = t->style == 2
        ? (t->card_bg.a ? t->card_bg : t->surface)
        : (t->inverse_surface.a ? t->inverse_surface : t->text);
    tk_color fgc = t->style == 2 ? t->text
        : (t->inverse_on_surface.a ? t->inverse_on_surface
                                   : t->bg);
    tk_rect full = { 0, 0, 800, 480 };
    CHECK(count_near(c, full, face.r, face.g, face.b, 20) > 100);
    CHECK(glyphs_near(c, full, fgc) > 3);
    tk_canvas_free(c);
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 220);
    send_ptr(r, TK_EV_POINTER_UP, 400, 220);
    CHECK(!tk_drag_active(r));
    CHECK(drop14_fires == 1);
    tk_root_free(r);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_dnd_b(void)
{
    dnd_b_flow(&tk_theme_v2);
    dnd_b_flow(&tk_theme_winui);
}

/* ---------- Accessibility: exposed states track visuals (B) --- */
static const tk_a11y_node *find_node(const tk_a11y_node *ns,
                                     size_t n, const tk_widget *w)
{
    for (size_t i = 0; i < n; i++)
        if (ns[i].widget == w) return &ns[i];
    return NULL;
}
static void a11y_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *cb = tk_checkbox_new("確認", false);
    tk_widget *sw = tk_switch_new("電源", false);
    tk_widget *btn = tk_button_new("実行");
    tk_add_child(box, cb);
    tk_add_child(box, sw);
    tk_add_child(box, btn);
    tk_root *r = root_of(box);
    tk_a11y_node ns[16];
    size_t n = tk_a11y_collect(r, ns, 16);
    const tk_a11y_node *nc = find_node(ns, n, cb);
    const tk_a11y_node *nw = find_node(ns, n, sw);
    const tk_a11y_node *nb = find_node(ns, n, btn);
    CHECK(nc && strcmp(nc->state, "unchecked") == 0);
    CHECK(nw && strcmp(nw->state, "off") == 0);
    CHECK(nb && !nb->focused);
    tk_checkbox_set_checked(cb, true);
    tk_switch_set_on(sw, true);
    CHECK(tk_focus(r, btn));
    n = tk_a11y_collect(r, ns, 16);
    nc = find_node(ns, n, cb);
    nw = find_node(ns, n, sw);
    nb = find_node(ns, n, btn);
    CHECK(nc && strcmp(nc->state, "checked") == 0);
    CHECK(nw && strcmp(nw->state, "on") == 0);
    CHECK(nb && nb->focused);
    tk_root_free(r);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_a11y_b(void)
{
    a11y_flow(&tk_theme_v2);
    a11y_flow(&tk_theme_winui);
}

/* ---------- Thumbnails: the rendered gradient (B) ------------- */
static void thumb_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *iv = tk_imageview_new();
    CHECK(tk_imageview_load_thumbnail(iv,
          "tests/fixtures/grad320.jpg", 160, 120));
    tk_widget_set_min_size(iv, 160, 120);
    tk_root *r = root_of(iv);
    tk_rect q = tk_widget_rect(iv);
    tk_canvas *c = paint_full(r);
    /* grad320: R rises left->right, G rises top->bottom */
    CHECK(px(c, q.x + 6, q.y + q.h / 2).r < 90);
    CHECK(px(c, q.x + q.w - 7, q.y + q.h / 2).r > 170);
    CHECK(px(c, q.x + q.w / 2, q.y + 6).g < 90);
    CHECK(px(c, q.x + q.w / 2, q.y + q.h - 7).g > 170);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(iv);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_thumb_b(void)
{
    thumb_flow(&tk_theme_v2);
    thumb_flow(&tk_theme_winui);
}

/* ---------- AppBar (CommandBar): face + divider (B, WinUI) ---- */
static tk_color blend14(tk_color fg, tk_color bg)
{
    int a = fg.a;
    tk_color o;
    o.r = (uint8_t)((fg.r * a + bg.r * (255 - a)) / 255);
    o.g = (uint8_t)((fg.g * a + bg.g * (255 - a)) / 255);
    o.b = (uint8_t)((fg.b * a + bg.b * (255 - a)) / 255);
    o.a = 255;
    return o;
}
static void appbar_b_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *ab = tk_appbar_new("題名");
    tk_root *r = root_of(ab);
    tk_rect q = tk_widget_rect(ab);
    /* the layer role is translucent: the painted face is
     * the layer blended over the root background */
    tk_color face = t->layer.a ? blend14(t->layer, t->bg)
                               : t->surface_alt;
    tk_canvas *c = paint_full(r);
    CHECK(col_near_c(px(c, q.x + 4, q.y + 4), face, 8));
    CHECK(count_near(c, q, face.r, face.g, face.b, 8) >
          q.w * q.h / 2);
    /* the bottom divider: the border_line role (itself
     * translucent) over the face, sampled clear of the
     * title */
    tk_color div = blend14(t->border_line, face);
    CHECK(col_near_c(px(c, q.x + q.w - 4, q.y + q.h - 1),
                     div, 8));
    tk_canvas_free(c);
    /* the WinUI form has no scrolled-state face change
     * (that is the M3 form's state) */
    tk_appbar_set_scrolled(ab, true);
    c = paint_full(r);
    CHECK(col_near_c(px(c, q.x + 4, q.y + 4), face, 8));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(ab);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_appbar_b_winui(void)
{
    appbar_b_flow(&tk_theme_winui);
    appbar_b_flow(&tk_theme_winui_dark);
}

/* ---------- ColorPicker: swatches + preview + indicator (B) --- */
static int count_sat(tk_canvas *c, tk_rect q)
{
    int n = 0;
    for (int y = q.y; y < q.y + q.h; y++)
        for (int x = q.x; x < q.x + q.w; x++) {
            tk_color p = px(c, x, y);
            int mx = p.r > p.g ? (p.r > p.b ? p.r : p.b)
                               : (p.g > p.b ? p.g : p.b);
            int mn = p.r < p.g ? (p.r < p.b ? p.r : p.b)
                               : (p.g < p.b ? p.g : p.b);
            if (mx - mn > 60) n++;
        }
    return n;
}
static void test_colorpicker_b(void)
{
    tk_theme_set_default(&tk_theme_v2);
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_add_child(host, tk_button_new("外側"));
    tk_root *r = root_of(host);
    tk_widget *cp = tk_colorpicker_new("色の選択", 0xF44336FFu);
    tk_colorpicker_show(r, cp);
    tk_rect q = tk_widget_rect(cp);
    tk_canvas *c1 = paint_full(r);
    /* the preset grid's faces */
    CHECK(count_sat(c1, q) > 200);
    int red1 = count_near(c1, q, 0xF4, 0x43, 0x36, 12);
    int blu1 = count_near(c1, q, 0x21, 0x96, 0xF3, 12);
    CHECK(red1 > 30); /* swatch + preview of the current pick */
    tk_colorpicker_set_value(cp, 0x2196F3FFu);
    tk_canvas *c2 = paint_full(r);
    /* the selection indicator moved and the preview follows:
     * the dialog's red area shrinks to its swatch alone and
     * the blue area grows by the preview */
    CHECK(diff_count(c1, c2, q) > 20);
    CHECK(count_near(c2, q, 0x21, 0x96, 0xF3, 12) > blu1 + 20);
    CHECK(count_near(c2, q, 0xF4, 0x43, 0x36, 12) < red1 - 20);
    tk_canvas_free(c1);
    tk_canvas_free(c2);
    tk_dialog_close(r, cp);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(cp);
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    test_drawer();
    test_sheet();
    test_fabmenu();
    test_snackbar();
    test_snackbar_d_winui();
    test_progress_d_winui();
    test_button_e();
    test_toggles_e_winui();
    test_split_e();
    test_picker_e();
    test_rail_e();
    test_carousel_e();
    tabs_e_winui();
    test_longpress_b();
    test_dnd_b();
    test_a11y_b();
    test_thumb_b();
    test_appbar_b_winui();
    test_colorpicker_b();
    printf("tk14: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
