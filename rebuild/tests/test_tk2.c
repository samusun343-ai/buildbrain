/* test_tk2.c - M2 widget tests: Switch, ProgressBar, Separator,
 * SpinButton, ImageView, Grid, Box wrap, Tabs, Dialog (modal +
 * focus trap), Menu (+submenu), MenuBar, ComboBox, TreeView,
 * TextView (edit / wrap / kinsoku / read-only / scroll), and
 * ScrollView drag-scroll with click suppression and inertia.
 * Runs without a font installed: metrics are the documented
 * approximation (ASCII 8px, wide 16px at font_px=16). */
#include "tk.h"
#include <stdio.h>
#include <string.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int clicks, changes, selects, activates;
static void on_click(tk_widget *w, void *ud) { (void)w; (void)ud; clicks++; }
static void on_change(tk_widget *w, void *ud) { (void)w; (void)ud; changes++; }
static void on_select(tk_widget *w, void *ud) { (void)w; (void)ud; selects++; }
static void on_activate(tk_widget *w, void *ud) { (void)w; (void)ud; activates++; }

static void send_down(tk_root *r, int x, int y)
{
    tk_event ev = {0};
    ev.type = TK_EV_POINTER_DOWN; ev.x = x; ev.y = y;
    tk_root_event(r, &ev);
}
static void send_up(tk_root *r, int x, int y)
{
    tk_event ev = {0};
    ev.type = TK_EV_POINTER_UP; ev.x = x; ev.y = y;
    tk_root_event(r, &ev);
}
static void send_move(tk_root *r, int x, int y)
{
    tk_event ev = {0};
    ev.type = TK_EV_POINTER_MOVE; ev.x = x; ev.y = y;
    tk_root_event(r, &ev);
}
static void send_click(tk_root *r, int x, int y)
{
    send_down(r, x, y);
    send_up(r, x, y);
}
static void send_key(tk_root *r, tk_key key, const char *text, uint32_t mods)
{
    tk_event ev = {0};
    ev.type = TK_EV_KEY_DOWN; ev.key = key; ev.text = text;
    ev.modifiers = mods;
    tk_root_event(r, &ev);
}
static void send_text(tk_root *r, const char *text)
{
    tk_event ev = {0};
    ev.type = TK_EV_TEXT_INPUT; ev.text = text;
    tk_root_event(r, &ev);
}

int main(void)
{
    const tk_theme *theme = tk_theme_default();
    (void)theme;

    /* ============ Switch ============ */
    {
        changes = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *sw = tk_switch_new("wifi", false);
        tk_switch_set_on_change(sw, on_change, NULL);
        tk_add_child(box, sw);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 60);
        tk_rect rs = tk_widget_rect(sw);
        send_click(root, rs.x + 10, rs.y + rs.h / 2);
        CHECK(tk_switch_on(sw));
        CHECK(changes == 1);
        CHECK(tk_focus(root, sw));
        send_key(root, TK_KEY_SPACE, NULL, 0);
        CHECK(!tk_switch_on(sw));
        CHECK(changes == 2);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ ProgressBar + Separator ============ */
    {
        tk_widget *pb = tk_progress_new(0.25f);
        CHECK(tk_progress_value(pb) == 0.25f);
        tk_progress_set_value(pb, 1.5f);
        CHECK(tk_progress_value(pb) == 1.0f);
        tk_progress_set_value(pb, -1.0f);
        CHECK(tk_progress_value(pb) == 0.0f);
        tk_size sz = tk_measure(pb, (tk_constraints){0, 500, 0, 500});
        CHECK(sz.h == 20);
        tk_widget_free(pb);
        tk_widget *sep = tk_separator_new(false);
        sz = tk_measure(sep, (tk_constraints){0, 500, 0, 500});
        CHECK(sz.h == 2);
        tk_widget_free(sep);
        tk_widget *vsep = tk_separator_new(true);
        sz = tk_measure(vsep, (tk_constraints){0, 500, 0, 500});
        CHECK(sz.w == 2);
        tk_widget_free(vsep);
    }

    /* ============ SpinButton ============ */
    {
        changes = 0;
        tk_widget *fx = tk_fixed_new();
        tk_widget *sp = tk_spinbutton_new(5, 0, 10, 2);
        tk_spinbutton_set_on_change(sp, on_change, NULL);
        tk_set_fixed_rect(sp, (tk_rect){0, 0, 120, 36});
        tk_add_child(fx, sp);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 200, 100);
        /* button zone is the rightmost 36px; top half = +, bottom = - */
        send_down(root, 100, 9);
        CHECK(tk_spinbutton_value(sp) == 7);
        send_down(root, 100, 27);
        CHECK(tk_spinbutton_value(sp) == 5);
        CHECK(changes == 2);
        /* clamp at max via keyboard */
        CHECK(tk_focus(root, sp));
        for (int i = 0; i < 10; i++) send_key(root, TK_KEY_UP, NULL, 0);
        CHECK(tk_spinbutton_value(sp) == 10);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ ImageView: pixels + PPM load via stb_image ==== */
    {
        tk_widget *iv = tk_imageview_new();
        uint32_t px[4] = { 0xFFFF0000u, 0xFF00FF00u,
                           0xFF0000FFu, 0xFFFFFFFFu };
        CHECK(tk_imageview_set_pixels(iv, px, 2, 2));
        tk_size sz = tk_measure(iv, (tk_constraints){0, 500, 0, 500});
        CHECK(sz.w == 2 && sz.h == 2);
        tk_widget *fx = tk_fixed_new();
        tk_set_fixed_rect(iv, (tk_rect){0, 0, 4, 4}); /* scaled x2 */
        tk_add_child(fx, iv);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 100, 100);
        tk_canvas *cv = tk_canvas_new(100, 100);
        tk_root_paint(root, cv, theme);
        tk_color p00 = tk_canvas_pixel(cv, 0, 0);
        tk_color p33 = tk_canvas_pixel(cv, 3, 3);
        CHECK(p00.r == 255 && p00.g == 0 && p00.b == 0);
        CHECK(p33.r == 255 && p33.g == 255 && p33.b == 255);
        tk_canvas_free(cv);
        tk_root_free(root);
        tk_widget_free(fx);
        /* file load: write a 2x1 binary PPM and decode it */
        const char *path = "/tmp/tk2_test_img.ppm";
        FILE *fp = fopen(path, "wb");
        CHECK(fp != NULL);
        if (fp) {
            fputs("P6\n2 1\n255\n", fp);
            unsigned char rgb[6] = { 255, 0, 0, 0, 255, 0 };
            fwrite(rgb, 1, 6, fp);
            fclose(fp);
            tk_widget *iv2 = tk_imageview_new();
            CHECK(tk_imageview_load(iv2, path));
            CHECK(tk_imageview_image_w(iv2) == 2);
            CHECK(tk_imageview_image_h(iv2) == 1);
            tk_widget_free(iv2);
        }
        CHECK(!tk_imageview_load(iv, "/nonexistent/nope.png"));
    }

    /* ============ Grid container ============ */
    {
        tk_widget *grid = tk_grid_new(2, 0, 0, 0);
        tk_widget *cells[4];
        const char *names[] = { "A", "B", "C", "D" };
        for (int i = 0; i < 4; i++) {
            cells[i] = tk_label_new(names[i]);
            tk_add_child(grid, cells[i]);
        }
        tk_root *root = tk_root_new(grid);
        tk_root_layout(root, 200, 100);
        tk_rect r0 = tk_widget_rect(cells[0]);
        tk_rect r1 = tk_widget_rect(cells[1]);
        tk_rect r2 = tk_widget_rect(cells[2]);
        tk_rect r3 = tk_widget_rect(cells[3]);
        /* surplus is shared equally: cells tile the whole rect */
        CHECK(r0.x == 0 && r0.y == 0 && r0.w == 100 && r0.h == 50);
        CHECK(r1.x == 100 && r1.y == 0);
        CHECK(r2.x == 0 && r2.y == 50);
        CHECK(r3.x == 100 && r3.y == 50 && r3.w == 100 && r3.h == 50);
        tk_root_free(root);
        tk_widget_free(grid);
    }

    /* ============ Box wrap (flex wrap) ============ */
    {
        tk_widget *box = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_box_set_wrap(box, true);
        tk_widget *items[4];
        for (int i = 0; i < 4; i++) {
            items[i] = tk_label_new("aaaa"); /* 32px wide each */
            tk_add_child(box, items[i]);
        }
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 100, 200);
        /* 3 fit per row (96px), the 4th wraps to row 2 */
        CHECK(tk_widget_rect(items[2]).x == 64);
        CHECK(tk_widget_rect(items[2]).y == 0);
        CHECK(tk_widget_rect(items[3]).x == 0);
        CHECK(tk_widget_rect(items[3]).y == tk_line_height(theme));
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ Tabs ============ */
    {
        changes = 0;
        tk_widget *tabs = tk_tabs_new();
        tk_tabs_set_on_change(tabs, on_change, NULL);
        tk_widget *page1 = tk_label_new("page one");
        tk_widget *page2 = tk_label_new("page two");
        tk_tabs_add_page(tabs, "One", page1);
        tk_tabs_add_page(tabs, "Two", page2);
        tk_root *root = tk_root_new(tabs);
        tk_root_layout(root, 200, 200);
        CHECK(tk_tabs_active(tabs) == 0);
        CHECK(tk_widget_rect(page1).h == 200 - 48); /* tab bar 48 (UI-REDESIGN 2.2) */
        CHECK(tk_widget_rect(page2).w == 0); /* inactive: no area */
        /* click the second tab segment ("One" seg = 24+24 = 48 wide) */
        send_click(root, 48 + 10, 10);
        CHECK(tk_tabs_active(tabs) == 1);
        CHECK(changes == 1);
        CHECK(tk_widget_rect(page2).h == 200 - 48);
        CHECK(tk_widget_rect(page1).w == 0);
        /* hit test in the page area finds page2, not page1 */
        CHECK(tk_hit_test(tabs, 20, 100) == page2);
        tk_root_free(root);
        tk_widget_free(tabs);
    }

    /* ============ Dialog: modal + focus trap ============ */
    {
        clicks = 0;
        int dlg_clicks = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *under = tk_button_new("under");
        tk_button_set_on_click(under, on_click, NULL);
        tk_add_child(box, under);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 400, 300);
        tk_widget *dlg = tk_dialog_new("Confirm");
        tk_widget *ok = tk_button_new("OK");
        /* reuse clicks counter through a second callback slot */
        tk_button_set_on_click(ok, on_click, NULL);
        tk_add_child(dlg, ok);
        tk_dialog_show(root, dlg);
        CHECK(tk_dialog_is_shown(root, dlg));
        /* focus is trapped: the under button cannot take focus */
        CHECK(!tk_focus(root, under));
        tk_widget *f0 = tk_focused(root);
        CHECK(f0 == dlg || f0 == ok);
        tk_focus_next(root);
        tk_widget *f1 = tk_focused(root);
        CHECK(f1 == dlg || f1 == ok);
        tk_focus_next(root);
        CHECK(tk_focused(root) == f0); /* cycles within the dialog */
        /* click on the under button (outside the dialog): swallowed */
        tk_rect ru = tk_widget_rect(under);
        int before = clicks;
        send_click(root, ru.x + 5, ru.y + 5);
        CHECK(clicks == before);
        /* click the dialog's OK button: fires */
        tk_rect ro = tk_widget_rect(ok);
        CHECK(ro.w > 0 && ro.h > 0);
        send_click(root, ro.x + ro.w / 2, ro.y + ro.h / 2);
        CHECK(clicks == before + 1);
        (void)dlg_clicks;
        tk_dialog_close(root, dlg);
        CHECK(!tk_dialog_is_shown(root, dlg));
        CHECK(tk_focus(root, under)); /* trap released */
        tk_widget_free(dlg);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ Menu: select, check item, submenu, keys ====== */
    {
        selects = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_add_child(box, tk_label_new("host"));
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 400, 300);
        tk_widget *sub = tk_menu_new();
        tk_menu_add_item(sub, "file1", 3);
        tk_menu_set_on_select(sub, on_select, NULL);
        tk_widget *menu = tk_menu_new();
        tk_menu_add_item(menu, "Open", 1);
        tk_menu_add_check_item(menu, "Wrap", 2, false);
        tk_menu_add_separator(menu);
        tk_menu_add_submenu(menu, "Recent", sub);
        tk_menu_set_on_select(menu, on_select, NULL);
        tk_menu_popup(root, menu, 10, 10);
        CHECK(tk_menu_is_open(root, menu));
        /* rows (48px each): Open 10..58, Wrap 58..106, sep 106..115, Recent 115..163 */
        send_click(root, 20, 82); /* Wrap */
        CHECK(tk_menu_last_id(menu) == 2);
        CHECK(tk_menu_item_checked(menu, 2));
        CHECK(selects == 1);
        CHECK(!tk_menu_is_open(root, menu)); /* closed on select */
        /* submenu: hover Recent, then click file1 */
        tk_menu_popup(root, menu, 10, 10);
        send_move(root, 20, 139);
        CHECK(tk_menu_is_open(root, sub));
        tk_rect rsub = tk_widget_rect(sub);
        CHECK(rsub.w > 0 && rsub.h > 0);
        send_click(root, rsub.x + 10, rsub.y + 10);
        CHECK(tk_menu_last_id(sub) == 3);
        CHECK(!tk_menu_is_open(root, menu));
        CHECK(!tk_menu_is_open(root, sub));
        /* keyboard: Down + Enter activates the hovered item */
        tk_menu_popup(root, menu, 10, 10);
        CHECK(tk_focused(root) == menu);
        send_key(root, TK_KEY_DOWN, NULL, 0);   /* hover: Open -> Wrap */
        send_key(root, TK_KEY_ENTER, NULL, 0);
        CHECK(tk_menu_last_id(menu) == 2);
        CHECK(!tk_menu_item_checked(menu, 2)); /* toggled back */
        /* Escape closes */
        tk_menu_popup(root, menu, 10, 10);
        send_key(root, TK_KEY_ESCAPE, NULL, 0);
        CHECK(!tk_menu_is_open(root, menu));
        tk_widget_free(menu); /* also frees the submenu */
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ MenuBar ============ */
    {
        selects = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *bar = tk_menubar_new();
        tk_widget *filemenu = tk_menu_new();
        tk_menu_add_item(filemenu, "Quit", 7);
        tk_menu_set_on_select(filemenu, on_select, NULL);
        tk_menubar_add_menu(bar, "File", filemenu);
        tk_add_child(box, bar);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 300, 100);
        send_click(root, 16, 18); /* the File title */
        CHECK(tk_menu_is_open(root, filemenu));
        tk_rect rm = tk_widget_rect(filemenu);
        CHECK(rm.y == 48); /* below the bar (control_h 48) */
        send_click(root, rm.x + 10, rm.y + 10);
        CHECK(tk_menu_last_id(filemenu) == 7);
        CHECK(selects == 1);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ ComboBox ============ */
    {
        changes = 0;
        tk_widget *fx = tk_fixed_new();
        tk_widget *combo = tk_combobox_new();
        const char *items[] = { "alpha", "beta", "gamma" };
        tk_combobox_set_items(combo, items, 3);
        tk_combobox_set_on_change(combo, on_change, NULL);
        tk_set_fixed_rect(combo, (tk_rect){0, 0, 160, 36});
        tk_add_child(fx, combo);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 300);
        send_click(root, 20, 18);
        CHECK(tk_combobox_is_open(combo));
        /* dropdown rows start at y=36, each 32 high: beta at 68..100 */
        send_click(root, 10, 84);
        CHECK(tk_combobox_selected(combo) == 1);
        CHECK(changes == 1);
        CHECK(!tk_combobox_is_open(combo));
        /* keyboard stepping */
        CHECK(tk_focus(root, combo));
        send_key(root, TK_KEY_DOWN, NULL, 0);
        CHECK(tk_combobox_selected(combo) == 2);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ TreeView ============ */
    {
        selects = 0; activates = 0;
        tk_widget *tv = tk_treeview_new();
        int A = tk_treeview_add(tv, -1, "A");
        int A1 = tk_treeview_add(tv, A, "A1");
        int A2 = tk_treeview_add(tv, A, "A2");
        int A2a = tk_treeview_add(tv, A2, "A2a");
        int B = tk_treeview_add(tv, -1, "B");
        tk_treeview_set_on_select(tv, on_select, NULL);
        tk_treeview_set_on_activate(tv, on_activate, NULL);
        CHECK(tk_treeview_node_count(tv) == 5);
        CHECK(tk_treeview_visible_rows(tv) == 5);
        CHECK(tk_treeview_row_node(tv, 3) == A2a);
        CHECK(tk_treeview_node_depth(tv, A2a) == 2);
        tk_widget *fx = tk_fixed_new();
        tk_set_fixed_rect(tv, (tk_rect){0, 0, 240, 260}); /* 5 rows x 48 + margin */
        tk_add_child(fx, tv);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 300);
        /* collapse A2 via its expander (row 2, depth 1 -> x 26..44) */
        send_click(root, 30, 2 * 48 + 24);
        CHECK(tk_treeview_visible_rows(tv) == 4);
        CHECK(tk_treeview_row_node(tv, 3) == B);
        /* expand again */
        send_click(root, 30, 2 * 48 + 24);
        CHECK(tk_treeview_visible_rows(tv) == 5);
        /* select B by clicking its label (row 4) */
        send_click(root, 100, 4 * 48 + 24);
        CHECK(tk_treeview_selected(tv) == B);
        CHECK(selects >= 1);
        /* keyboard: Up moves to the previous visible node */
        CHECK(tk_focus(root, tv));
        send_key(root, TK_KEY_UP, NULL, 0);
        CHECK(tk_treeview_selected(tv) == A2a);
        send_key(root, TK_KEY_ENTER, NULL, 0);
        CHECK(activates == 1);
        /* Left collapses the selected node if it has children */
        tk_treeview_select(tv, A2);
        send_key(root, TK_KEY_LEFT, NULL, 0);
        CHECK(!tk_treeview_expanded(tv, A2));
        CHECK(tk_treeview_visible_rows(tv) == 4);
        (void)A1;
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ TextView: editing ============ */
    {
        changes = 0;
        tk_widget *fx = tk_fixed_new();
        tk_widget *tvv = tk_textview_new("ab\ncd");
        tk_textview_set_on_change(tvv, on_change, NULL);
        tk_set_fixed_rect(tvv, (tk_rect){0, 0, 200, 100});
        tk_add_child(fx, tvv);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 200);
        CHECK(tk_focus(root, tvv));
        send_text(root, "Z");                    /* at cursor 0 */
        char buf[256];
        tk_textview_text(tvv, buf, sizeof(buf));
        CHECK(strcmp(buf, "Zab\ncd") == 0);
        send_key(root, TK_KEY_ENTER, NULL, 0);   /* newline after Z */
        tk_textview_text(tvv, buf, sizeof(buf));
        CHECK(strcmp(buf, "Z\nab\ncd") == 0);
        send_key(root, TK_KEY_BACKSPACE, NULL, 0);
        tk_textview_text(tvv, buf, sizeof(buf));
        CHECK(strcmp(buf, "Zab\ncd") == 0);
        CHECK(changes >= 2);
        /* click places the cursor: line 1 ("cd"), near its start */
        send_down(root, theme->unit + 2, 4 + tk_line_height(theme) + 5);
        CHECK(tc_cursor(tk_textview_buffer(tvv)) == 4);
        /* Down arrow keeps the column on the next line */
        tk_widget *tv2 = tk_textview_new("one\ntwo\nthree");
        tk_set_fixed_rect(tv2, (tk_rect){0, 0, 200, 100});
        tk_widget *fx2 = tk_fixed_new();
        tk_add_child(fx2, tv2);
        tk_root *root2 = tk_root_new(fx2);
        tk_root_layout(root2, 300, 200);
        CHECK(tk_focus(root2, tv2));
        tc_move_to(tk_textview_buffer(tv2), 5, false); /* line1 col1 */
        send_key(root2, TK_KEY_DOWN, NULL, 0);
        CHECK(tc_cursor(tk_textview_buffer(tv2)) == 9); /* line2 col1 */
        send_key(root2, TK_KEY_UP, NULL, 0);
        CHECK(tc_cursor(tk_textview_buffer(tv2)) == 5);
        tk_root_free(root2);
        tk_widget_free(fx2);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ TextView: wrap + kinsoku ============ */
    {
        /* 30 ASCII chars in a 100px-wide view: wrap_w = 84 -> 10/line */
        tk_widget *tvw = tk_textview_new("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        tk_widget *fx = tk_fixed_new();
        tk_set_fixed_rect(tvw, (tk_rect){0, 0, 100, 200});
        tk_add_child(fx, tvw);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 200, 300);
        CHECK(tk_textview_visual_lines(tvw) == 3);
        tk_root_free(root);
        tk_widget_free(fx);
        /* kinsoku: "、" must not start a line. At wrap_w=80 (5 wide
         * chars per line) a naive wrap would put 、 at a line start;
         * the kinsoku wrap breaks one char earlier instead. */
        const char *jp = "あいうえお、かきくけこ";
        size_t starts[8];
        int nlines = 0;
        struct ctx { size_t *starts; int *n; } ctx = { starts, &nlines };
        /* collect via a file-scope helper through tk_text_wrap */
        extern size_t test_wrap_starts(const tk_theme *, const char *,
                                       int, size_t *, int);
        nlines = (int)test_wrap_starts(theme, jp, 80, starts, 8);
        CHECK(nlines == 3);
        CHECK(starts[0] == 0);
        CHECK(starts[1] == 12); /* line 2 starts at お (4 chars in) */
        CHECK(starts[2] == 27); /* line 3 starts at け */
        (void)ctx;
    }

    /* ============ TextView: read-only ============ */
    {
        tk_widget *fx = tk_fixed_new();
        tk_widget *tvv = tk_textview_new("readonly text");
        tk_textview_set_editable(tvv, false);
        tk_set_fixed_rect(tvv, (tk_rect){0, 0, 200, 100});
        tk_add_child(fx, tvv);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 200);
        CHECK(tk_focus(root, tvv));
        tc_move_to(tk_textview_buffer(tvv), 5, false);
        send_text(root, "X");
        char buf[128];
        tk_textview_text(tvv, buf, sizeof(buf));
        CHECK(strcmp(buf, "readonly text") == 0);
        send_key(root, TK_KEY_BACKSPACE, NULL, 0);
        tk_textview_text(tvv, buf, sizeof(buf));
        CHECK(strcmp(buf, "readonly text") == 0);
        /* navigation still works */
        send_key(root, TK_KEY_LEFT, NULL, 0);
        CHECK(tc_cursor(tk_textview_buffer(tvv)) == 4);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ TextView: scroll to cursor ============ */
    {
        char big[512] = "";
        for (int i = 0; i < 20; i++) {
            char line[16];
            snprintf(line, sizeof(line), "line%d\n", i);
            strcat(big, line);
        }
        tk_widget *fx = tk_fixed_new();
        tk_widget *tvv = tk_textview_new(big);
        tk_set_fixed_rect(tvv, (tk_rect){0, 0, 200, 88}); /* 4 rows */
        tk_add_child(fx, tvv);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 200);
        CHECK(tk_textview_visual_lines(tvv) == 21); /* 20 + trailing */
        tc_move(tk_textview_buffer(tvv), TC_MOVE_DOC_END, 1, false);
        tk_textview_ensure_cursor_visible(tvv);
        /* clicking the first visible row now lands past line 0 */
        send_down(root, theme->unit + 2, 6);
        CHECK(tc_cursor(tk_textview_buffer(tvv)) > 0);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ ScrollView: drag scroll + suppression + inertia */
    {
        tk_widget *sv = tk_scrollview_new();
        tk_widget *inner = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        for (int i = 0; i < 20; i++) {
            char label[16];
            snprintf(label, sizeof(label), "row %d", i);
            tk_add_child(inner, tk_label_new(label));
        }
        tk_scrollview_set_child(sv, inner);
        tk_root *root = tk_root_new(sv);
        tk_root_layout(root, 200, 105);
        /* drag up: finger 90 -> 60 -> 40 */
        send_down(root, 10, 90);
        send_move(root, 10, 60);
        send_move(root, 10, 40);
        send_up(root, 10, 40);
        CHECK(tk_scrollview_offset_y(sv) == 50);
        /* inertia: velocity from the last move keeps it scrolling,
         * decaying until tick reports rest */
        int off_at_release = tk_scrollview_offset_y(sv);
        bool moving = true;
        for (int i = 0; i < 200 && moving; i++)
            moving = tk_scrollview_tick(sv);
        CHECK(!moving);
        CHECK(tk_scrollview_offset_y(sv) > off_at_release);
        tk_root_free(root);
        tk_widget_free(sv);
    }
    {
        /* a drag that starts on a button inside a ScrollView must
         * scroll, and must NOT fire the button's click on release */
        clicks = 0;
        tk_widget *sv = tk_scrollview_new();
        tk_widget *inner = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *btn = tk_button_new("target");
        tk_button_set_on_click(btn, on_click, NULL);
        tk_add_child(inner, btn);
        for (int i = 0; i < 20; i++)
            tk_add_child(inner, tk_label_new("filler"));
        tk_scrollview_set_child(sv, inner);
        tk_root *root = tk_root_new(sv);
        tk_root_layout(root, 200, 105);
        tk_rect rb = tk_widget_rect(btn);
        int cx = rb.x + rb.w / 2, cy = rb.y + rb.h / 2;
        send_down(root, cx, cy);
        send_move(root, cx, cy - 30);
        send_move(root, cx, cy - 50);
        send_up(root, cx, cy - 50);
        CHECK(clicks == 0);
        CHECK(tk_scrollview_offset_y(sv) > 0);
        /* a plain click (no drag) still fires */
        tk_scrollview_scroll_to(sv, 0);
        tk_root_layout(root, 200, 105);
        send_click(root, cx, cy);
        CHECK(clicks == 1);
        tk_root_free(root);
        tk_widget_free(sv);
    }

    /* ============ Composition rendering (M2): per-segment
     * underlines in TextField, no bracket placeholder ============ */
    {
        tk_widget *tf = tk_textfield_new("ab");
        tk_widget *fx = tk_fixed_new();
        tk_set_fixed_rect(tf, (tk_rect){0, 0, 300, 40});
        tk_add_child(fx, tf);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 40);
        tc_move_to(tk_textfield_buffer(tf), 2, false);
        tk_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_COMPOSITION;
        ev.text = "かんじ";
        size_t segs[2] = { 3, 6 };   /* か / んじ */
        ev.seg_lens = segs;
        ev.seg_count = 2;
        ev.seg_active = 0;
        tk_dispatch(tf, &ev);
        int lh = tk_line_height(theme);
        int ty = (40 - lh) / 2;
        int cx0 = theme->unit + tk_text_width(theme, "ab");
        int w0 = tk_text_width(theme, "か");
        tk_canvas *cv = tk_canvas_new(300, 40);
        tk_root_paint(root, cv, theme);
        /* active segment: 2px accent underline (both rows) */
        tk_color p = tk_canvas_pixel(cv, cx0 + 2, ty + lh - 2);
        CHECK(p.r == theme->accent.r && p.g == theme->accent.g &&
              p.b == theme->accent.b);
        p = tk_canvas_pixel(cv, cx0 + 2, ty + lh - 1);
        CHECK(p.r == theme->accent.r && p.g == theme->accent.g &&
              p.b == theme->accent.b);
        /* inactive segment: only the 1px bottom row, in text_dim */
        p = tk_canvas_pixel(cv, cx0 + w0 + 2, ty + lh - 2);
        CHECK(p.r == theme->surface.r && p.g == theme->surface.g &&
              p.b == theme->surface.b);
        p = tk_canvas_pixel(cv, cx0 + w0 + 2, ty + lh - 1);
        CHECK(p.r == theme->text_dim.r && p.g == theme->text_dim.g &&
              p.b == theme->text_dim.b);
        /* candidate browsing: active segment gets selection bg */
        ev.seg_highlight = true;
        tk_dispatch(tf, &ev);
        tk_root_paint(root, cv, theme);
        p = tk_canvas_pixel(cv, cx0 + 2, ty + 2);
        CHECK(p.r == theme->selection.r && p.g == theme->selection.g &&
              p.b == theme->selection.b);
        /* the inactive segment does not */
        p = tk_canvas_pixel(cv, cx0 + w0 + 2, ty + 2);
        CHECK(p.r == theme->surface.r && p.g == theme->surface.g &&
              p.b == theme->surface.b);
        tk_canvas_free(cv);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    printf("tk2: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

/* helper for the kinsoku test: collect visual line starts */
static void collect_start(void *ud, size_t start, size_t len)
{
    (void)len;
    void **arr = ud;
    size_t *starts = arr[0];
    int *n = arr[1];
    if (*n < 8) starts[(*n)] = start;
    (*n)++;
}
size_t test_wrap_starts(const tk_theme *t, const char *s, int max_w,
                        size_t *starts, int cap)
{
    int n = 0;
    void *arr[2] = { starts, &n };
    (void)cap;
    size_t lines = tk_text_wrap(t, s, max_w, collect_start, arr);
    return lines;
}
