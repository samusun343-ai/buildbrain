/* test_tk4.c - M5: scroll-shift fast path (pixel equivalence with
 * the full repaint), animation engine + switch knob + overlay
 * slide, setter auto-invalidate sweep, tc_find, image thumbnails,
 * file dialog, date/time pickers, ListView composite rows
 * (checkable + headers), density tokens. */
#include "tk.h"
#include "textcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, #cond); failures++; } \
} while (0)

static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = ty; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
static void send_key(tk_root *r, tk_key k)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN; ev.key = k;
    (void)tk_root_event(r, &ev);
}
static void send_click(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_DOWN, x, y);
    send_ptr(r, TK_EV_POINTER_UP, x, y);
}

/* ---------- scroll shift equivalence ---------- */

static tk_widget *build_list_screen(tk_widget **sv_out,
                                    tk_widget **first_row_out)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *header = tk_label_new("ファイル一覧");
    tk_widget *sv = tk_scrollview_new();
    tk_widget *list = tk_box_new(TK_DIR_VERTICAL, 4, 4);
    tk_widget *first = NULL;
    for (int i = 0; i < 60; i++) {
        char b[64];
        snprintf(b, sizeof b, "ドキュメント %02d - 会議メモ.txt", i);
        tk_widget *l = tk_label_new(b);
        if (!first) first = l;
        tk_add_child(list, l);
    }
    tk_scrollview_set_child(sv, list);
    tk_add_child(box, header);
    tk_add_child(box, sv);
    tk_set_flex(sv, 1);
    *sv_out = sv;
    if (first_row_out) *first_row_out = first;
    return box;
}

static void test_scroll_shift(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *svA, *svB, *rowA, *rowB;
    tk_root *rA = tk_root_new(build_list_screen(&svA, &rowA));
    tk_root *rB = tk_root_new(build_list_screen(&svB, &rowB));
    tk_root_set_scroll_blit(rA, true);
    tk_root_layout(rA, 800, 480);
    tk_root_layout(rB, 800, 480);
    tk_canvas *cA = tk_canvas_new(800, 480);
    tk_canvas *cB = tk_canvas_new(800, 480);
    tk_fb *fA = tk_fb_open_mem(800, 480, 16);
    tk_fb *fB = tk_fb_open_mem(800, 480, 16);
    tk_root_paint_damage(rA, cA, t);
    tk_root_paint_damage(rB, cB, t);
    (void)tk_fb_flush_damage(fA, cA);
    (void)tk_fb_flush_damage(fB, cB);
    CHECK(memcmp(tk_canvas_pixels(cA), tk_canvas_pixels(cB),
                 800 * 480 * 4) == 0);

    /* scroll down 40, then 13 more, then back up 20: the shifted
     * canvas must stay pixel-identical to the full repaint */
    const int steps[3] = { 40, 53, 33 };
    for (int s = 0; s < 3; s++) {
        tk_scrollview_scroll_to(svA, steps[s]);
        tk_scrollview_scroll_to(svB, steps[s]);
        tk_widget_invalidate(svA);
        tk_widget_invalidate(svB);
        tk_root_paint_damage(rA, cA, t);
        tk_root_paint_damage(rB, cB, t);
        size_t bA = tk_fb_flush_damage(fA, cA);
        size_t bB = tk_fb_flush_damage(fB, cB);
        CHECK(memcmp(tk_canvas_pixels(cA), tk_canvas_pixels(cB),
                     800 * 480 * 4) == 0);
        if (s == 0) CHECK(bA < bB / 4); /* the shift ships a band */
    }

    /* content change inside the SV poisons the shift base: the
     * next scroll frame must fall back to a full repaint and
     * still be pixel-correct */
    tk_label_set_text(rowA, "差し替えられた行テキスト");
    tk_label_set_text(rowB, "差し替えられた行テキスト");
    tk_scrollview_scroll_to(svA, 90);
    tk_scrollview_scroll_to(svB, 90);
    tk_widget_invalidate(svA);
    tk_widget_invalidate(svB);
    tk_root_paint_damage(rA, cA, t);
    tk_root_paint_damage(rB, cB, t);
    (void)tk_fb_flush_damage(fA, cA);
    (void)tk_fb_flush_damage(fB, cB);
    CHECK(memcmp(tk_canvas_pixels(cA), tk_canvas_pixels(cB),
                 800 * 480 * 4) == 0);
    tk_root_free(rA); tk_root_free(rB);
    tk_canvas_free(cA); tk_canvas_free(cB);
    tk_fb_close(fA); tk_fb_close(fB);
}

/* ---------- paint culling equivalence ---------- */

static void test_culling(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 8);
    tk_widget *target = NULL;
    for (int i = 0; i < 40; i++) {
        char b[32];
        snprintf(b, sizeof b, "行 %d", i);
        tk_widget *l = tk_label_new(b);
        if (i == 20) target = l;
        tk_add_child(box, l);
    }
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 400, 900);
    tk_canvas *full = tk_canvas_new(400, 900);
    tk_root_paint_damage(r, full, t); /* drains: full repaint */
    /* second canvas starts blank; repaint only the target's
     * damage - its rect must come out pixel-identical */
    tk_canvas *part = tk_canvas_new(400, 900);
    tk_widget_invalidate(target);
    tk_root_paint_damage(r, part, t);
    tk_rect rr = tk_widget_rect(target);
    int same = 1;
    for (int y = rr.y; y < rr.y + rr.h && same; y++)
        for (int x = rr.x; x < rr.x + rr.w; x++) {
            tk_color a = tk_canvas_pixel(full, x, y);
            tk_color b = tk_canvas_pixel(part, x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b) { same = 0; break; }
        }
    CHECK(same);
    tk_root_free(r);
    tk_canvas_free(full); tk_canvas_free(part);
}

/* ---------- animation engine ---------- */

static int anim_vals[64], anim_n, anim_done_n;
static void anim_rec(void *ud, int v)
{
    (void)ud;
    if (anim_n < 64) anim_vals[anim_n++] = v;
}
static void anim_done_rec(void *ud) { (void)ud; anim_done_n++; }

static void test_animation(void)
{
    CHECK(tk_ease_linear(0.25f) == 0.25f);
    CHECK(tk_ease_out_cubic(0.0f) == 0.0f);
    CHECK(tk_ease_out_cubic(1.0f) == 1.0f);
    CHECK(tk_ease_in_out_cubic(0.0f) == 0.0f);
    CHECK(tk_ease_in_out_cubic(1.0f) == 1.0f);
    CHECK(tk_ease_out_cubic(0.5f) > 0.8f); /* fast start */

    tk_root *r = tk_root_new(tk_box_new(TK_DIR_VERTICAL, 0, 0));
    anim_n = 0; anim_done_n = 0;
    tk_root_tick(r, 1000);
    tk_anim *a = tk_anim_start(r, 0, 100, 160, tk_ease_out_cubic,
                               anim_rec, anim_done_rec, NULL);
    CHECK(a != NULL);
    CHECK(tk_root_anim_active(r));
    tk_root_tick(r, 1080); /* halfway in time */
    CHECK(anim_n >= 1);
    CHECK(anim_vals[anim_n - 1] > 60 && anim_vals[anim_n - 1] < 100);
    tk_root_tick(r, 1160);
    CHECK(anim_vals[anim_n - 1] == 100);
    CHECK(anim_done_n == 1);
    CHECK(!tk_root_anim_active(r));

    /* cancel: no further callbacks */
    anim_n = 0;
    a = tk_anim_start(r, 0, 50, 160, tk_ease_linear, anim_rec, NULL, NULL);
    tk_anim_cancel(a);
    tk_root_tick(r, 1400);
    CHECK(anim_n == 0);

    /* disabled: synchronous final value, NULL handle */
    tk_root_set_animations_enabled(r, false);
    anim_n = 0; anim_done_n = 0;
    a = tk_anim_start(r, 0, 77, 160, tk_ease_linear,
                      anim_rec, anim_done_rec, NULL);
    CHECK(a == NULL);
    CHECK(anim_n == 1 && anim_vals[0] == 77 && anim_done_n == 1);
    tk_root_free(r);
}

static void test_switch_knob(void)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *sw = tk_switch_new("Wi-Fi", false);
    tk_add_child(box, sw);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 300, 80);
    tk_canvas *c = tk_canvas_new(300, 80);
    tk_root_paint_damage(r, c, t);
    tk_rect wr = tk_widget_rect(sw);
    int cy = wr.y + wr.h / 2;
    /* OFF: knob centre is at the left; its pixel is the knob
     * surface colour */
    tk_color off_knob = tk_canvas_pixel(c, wr.x + 4 + 12, cy);
    CHECK(off_knob.r == t->surface.r && off_knob.g == t->surface.g);
    /* toggle with a live clock: knob has not arrived yet */
    tk_root_tick(r, 500);
    tk_switch_set_on(sw, true);
    tk_root_paint_damage(r, c, t);
    tk_color mid = tk_canvas_pixel(c, wr.x + 4 + 20 + 12, cy);
    CHECK(!(mid.r == t->surface.r && mid.g == t->surface.g &&
            mid.b == t->surface.b));
    /* after the duration the knob rests on the right */
    tk_root_tick(r, 500 + 200);
    tk_root_paint_damage(r, c, t);
    tk_color on_knob = tk_canvas_pixel(c, wr.x + 4 + 20 + 12, cy);
    CHECK(on_knob.r == t->surface.r && on_knob.g == t->surface.g &&
          on_knob.b == t->surface.b);
    /* animations disabled: the knob snaps immediately */
    tk_root_set_animations_enabled(r, false);
    tk_switch_set_on(sw, false);
    tk_root_paint_damage(r, c, t);
    tk_color snap = tk_canvas_pixel(c, wr.x + 4 + 12, cy);
    CHECK(snap.r == t->surface.r && snap.g == t->surface.g);
    tk_root_free(r);
    tk_canvas_free(c);
}

static void test_overlay_slide(void)
{
    /* no tick ever: the dialog lands at its final rect at once */
    tk_root *r1 = tk_root_new(tk_box_new(TK_DIR_VERTICAL, 0, 0));
    tk_root_layout(r1, 800, 480);
    tk_widget *d1 = tk_dialog_new("確認");
    tk_add_child(d1, tk_label_new("本文"));
    tk_dialog_show(r1, d1);
    int y_final = tk_widget_rect(d1).y;
    tk_dialog_close(r1, d1);

    /* live clock: the dialog starts 12px low and settles */
    tk_root *r2 = tk_root_new(tk_box_new(TK_DIR_VERTICAL, 0, 0));
    tk_root_layout(r2, 800, 480);
    tk_root_tick(r2, 1000);
    tk_widget *d2 = tk_dialog_new("確認");
    tk_add_child(d2, tk_label_new("本文"));
    tk_dialog_show(r2, d2);
    CHECK(tk_widget_rect(d2).y == y_final + 12);
    tk_root_tick(r2, 1000 + TK_ANIM_DURATION_MS + 40);
    CHECK(tk_widget_rect(d2).y == y_final);
    tk_root_free(r1); tk_root_free(r2);
    tk_widget_free(d1); tk_widget_free(d2);
}

/* ---------- setter auto-invalidate ---------- */

static void test_setters(void)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 4);
    tk_widget *label = tk_label_new("a");
    tk_widget *check = tk_checkbox_new("c", false);
    tk_widget *slider = tk_slider_new(0.25f);
    tk_widget *prog = tk_progress_new(0.1f);
    tk_widget *spin = tk_spinbutton_new(1, 0, 10, 1);
    tk_widget *tf = tk_textfield_new("x");
    tk_widget *lv = tk_listview_new();
    const char *items[3] = { "i0", "i1", "i2" };
    tk_listview_set_items(lv, items, 3);
    tk_widget *tabs = tk_tabs_new();
    tk_tabs_add_page(tabs, "p1", tk_label_new("1"));
    tk_tabs_add_page(tabs, "p2", tk_label_new("2"));
    tk_add_child(box, label); tk_add_child(box, check);
    tk_add_child(box, slider); tk_add_child(box, prog);
    tk_add_child(box, spin); tk_add_child(box, tf);
    tk_add_child(box, lv); tk_add_child(box, tabs);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 400, 800);
    tk_canvas *c = tk_canvas_new(400, 800);
    tk_root_paint_damage(r, c, tk_theme_default());
    CHECK(tk_root_damage_count(r) == 0);

#define SETTER_CHECK(expr) do { \
        expr; \
        CHECK(tk_root_damage_count(r) > 0); \
        tk_root_paint_damage(r, c, tk_theme_default()); \
    } while (0)
#define SETTER_NOOP(expr) do { \
        expr; \
        CHECK(tk_root_damage_count(r) == 0); \
    } while (0)

    SETTER_CHECK(tk_label_set_text(label, "b"));
    SETTER_NOOP(tk_label_set_text(label, "b"));
    SETTER_CHECK(tk_checkbox_set_checked(check, true));
    SETTER_NOOP(tk_checkbox_set_checked(check, true));
    SETTER_CHECK(tk_slider_set_value(slider, 0.5f));
    SETTER_NOOP(tk_slider_set_value(slider, 0.5f));
    SETTER_CHECK(tk_progress_set_value(prog, 0.9f));
    SETTER_NOOP(tk_progress_set_value(prog, 0.9f));
    SETTER_CHECK(tk_spinbutton_set_value(spin, 5));
    SETTER_NOOP(tk_spinbutton_set_value(spin, 5));
    SETTER_CHECK(tk_textfield_set_password(tf, true));
    SETTER_NOOP(tk_textfield_set_password(tf, true));
    SETTER_CHECK(tk_listview_select(lv, 2));
    SETTER_NOOP(tk_listview_select(lv, 2));
    SETTER_CHECK(tk_listview_set_marked(lv, 1));
    SETTER_NOOP(tk_listview_set_marked(lv, 1));
    SETTER_CHECK(tk_tabs_set_active(tabs, 1));
    SETTER_NOOP(tk_tabs_set_active(tabs, 1));
    SETTER_CHECK(tk_set_enabled(slider, false));
    SETTER_NOOP(tk_set_enabled(slider, false));

    tk_root_free(r);
    tk_canvas_free(c);
}

/* ---------- textcore search ---------- */

static void test_find(void)
{
    tc_buffer *b = tc_new();
    tc_set_text(b, "Hello 世界 hello WORLD 世界");
    CHECK(tc_find(b, "世界", 0, false) == 6);
    CHECK(tc_find(b, "世界", 7, false) == 25);
    CHECK(tc_find(b, "hello", 0, false) == 13);
    CHECK(tc_find(b, "hello", 0, true) == 0);   /* ASCII fold */
    CHECK(tc_find(b, "HELLO", 0, true) == 0);
    CHECK(tc_find(b, "不在", 0, false) == -1);
    CHECK(tc_find(b, "", 0, false) == -1);
    CHECK(tc_find(b, "Hello 世界 hello WORLD 世界!", 0, false) == -1);
    /* find_next walks and wraps */
    tc_move_to(b, 0, false);
    CHECK(tc_find_next(b, "世界", false) == 6);
    tc_move_to(b, 6, false);
    CHECK(tc_find_next(b, "世界", false) == 25);
    tc_move_to(b, 25, false);
    CHECK(tc_find_next(b, "世界", false) == 6); /* wrapped */
    tc_free(b);

    /* a match spanning the gap-buffer gap */
    tc_buffer *g = tc_new();
    tc_set_text(g, "0123456789ABCDEF");
    tc_move_to(g, 8, false);
    tc_insert(g, "Z");
    tc_delete_range(g, 8, 9); /* gap now sits around offset 8 */
    CHECK(tc_find(g, "789A", 0, false) == 7);
    CHECK(tc_find(g, "89AB", 0, false) == 8);
    tc_free(g);
}

/* ---------- image thumbnails ---------- */

static void test_image_scale(void)
{
    /* 4x4 opaque ramp -> 2x2 box averages */
    uint32_t px[16];
    for (int i = 0; i < 16; i++)
        px[i] = 0xFF000000u | ((uint32_t)(i * 16) << 16) |
                ((uint32_t)(i * 16) << 8) | (uint32_t)(i * 16);
    uint32_t *out = NULL; int ow = 0, oh = 0;
    CHECK(tk_image_scale_to_fit(px, 4, 4, 2, 2, &out, &ow, &oh));
    CHECK(ow == 2 && oh == 2);
    /* block (0,0): values 0,16,64,80 -> avg 40 */
    CHECK(((out[0] >> 16) & 0xFF) == 40);
    free(out); out = NULL;
    /* never upscales */
    CHECK(tk_image_scale_to_fit(px, 4, 4, 16, 16, &out, &ow, &oh));
    CHECK(ow == 4 && oh == 4);
    CHECK(memcmp(out, px, sizeof px) == 0);
    free(out); out = NULL;
    /* aspect: 8x4 into 4x4 -> 4x2 */
    uint32_t wide[32];
    for (int i = 0; i < 32; i++) wide[i] = 0xFF102030u;
    CHECK(tk_image_scale_to_fit(wide, 8, 4, 4, 4, &out, &ow, &oh));
    CHECK(ow == 4 && oh == 2);
    free(out); out = NULL;
    /* alpha weighting: half opaque red, half fully transparent
     * black -> red stays pure, alpha halves */
    uint32_t half[2] = { 0xFFFF0000u, 0x00000000u };
    CHECK(tk_image_scale_to_fit(half, 2, 1, 1, 1, &out, &ow, &oh));
    CHECK((out[0] >> 24) == 127);
    CHECK(((out[0] >> 16) & 0xFF) == 255);
    free(out);
}

/* ---------- pickers ---------- */

static int picker_changes;
static void on_picker(tk_widget *w, void *ud)
{
    (void)w; (void)ud; picker_changes++;
}

static void test_pickers(void)
{
    int y, m, d;
    tk_widget *dp = tk_datepicker_new(2024, 2, 29);
    tk_datepicker_value(dp, &y, &m, &d);
    CHECK(y == 2024 && m == 2 && d == 29);      /* leap year */
    tk_datepicker_set_value(dp, 2023, 2, 29);
    tk_datepicker_value(dp, &y, &m, &d);
    CHECK(y == 2023 && m == 2 && d == 28);      /* clamped */
    tk_datepicker_set_value(dp, 2024, 1, 31);
    tk_widget_free(dp);

    /* events: field selection + stepping + month-end clamp */
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    dp = tk_datepicker_new(2024, 1, 31);
    tk_datepicker_set_on_change(dp, on_picker, NULL);
    tk_add_child(box, dp);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 400, 80);
    CHECK(tk_focus(r, dp));
    picker_changes = 0;
    send_key(r, TK_KEY_RIGHT);                  /* field: month */
    send_key(r, TK_KEY_UP);                     /* Jan -> Feb */
    tk_datepicker_value(dp, &y, &m, &d);
    CHECK(m == 2 && d == 29);                   /* 31 clamped */
    CHECK(picker_changes == 1);
    send_key(r, TK_KEY_RIGHT);                  /* field: day */
    send_key(r, TK_KEY_UP);
    tk_datepicker_value(dp, &y, &m, &d);
    CHECK(d == 1);                              /* Feb 29 + 1 wraps */
    /* stepper zone: upper half steps the active field up */
    tk_rect wr = tk_widget_rect(dp);
    picker_changes = 0;
    send_click(r, wr.x + wr.w - 14, wr.y + wr.h / 4);
    CHECK(picker_changes == 1);
    tk_root_free(r);

    /* time picker wraps hours, honours seconds visibility */
    int hh, mm, ss;
    tk_widget *tp = tk_timepicker_new(true);
    tk_timepicker_set_value(tp, 23, 59, 59);
    tk_timepicker_value(tp, &hh, &mm, &ss);
    CHECK(hh == 23 && mm == 59 && ss == 59);
    tk_timepicker_set_value(tp, 24, 0, 0);
    tk_timepicker_value(tp, &hh, &mm, &ss);
    CHECK(hh == 0);
    tk_widget_free(tp);
}

/* ---------- ListView composite rows ---------- */

static int check_fires, check_row;
static void on_check_rec(tk_widget *w, void *ud)
{
    (void)ud;
    check_fires++;
    check_row = tk_listview_last_checked(w);
}

static void test_listview_composite(void)
{
    const char *items[4] = { "先頭", "区切り", "項目B", "項目C" };
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 4);
    tk_listview_set_header(lv, 1, true);
    tk_listview_set_checkable(lv, true);
    tk_listview_set_on_check(lv, on_check_rec, NULL);
    tk_add_child(box, lv);
    tk_set_flex(lv, 1);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 400, 400);
    const tk_theme *t = tk_theme_default();
    int rh = tk_listview_row_h(lv, t);
    tk_rect lr = tk_widget_rect(lv);
    CHECK(tk_listview_is_header(lv, 1));
    CHECK(!tk_listview_is_header(lv, 0));

    /* tap the header row: consumed, no selection */
    send_click(r, lr.x + 100, lr.y + rh + rh / 2);
    CHECK(tk_listview_selected(lv) == -1);
    /* tap row 0 body: selects */
    send_click(r, lr.x + 100, lr.y + rh / 2);
    CHECK(tk_listview_selected(lv) == 0);
    /* key DOWN steps over the header (row 1) to row 2 */
    CHECK(tk_focus(r, lv));
    send_key(r, TK_KEY_DOWN);
    CHECK(tk_listview_selected(lv) == 2);
    /* tap row 2's checkbox cell: toggles, selection stays */
    check_fires = 0; check_row = -1;
    send_click(r, lr.x + 2 * t->unit + 9, lr.y + 2 * rh + rh / 2);
    CHECK(tk_listview_row_checked(lv, 2));
    CHECK(check_fires == 1 && check_row == 2);
    CHECK(tk_listview_selected(lv) == 2);
    /* flags travel with move_row */
    CHECK(tk_listview_move_row(lv, 2, 3));
    CHECK(tk_listview_row_checked(lv, 3));
    CHECK(!tk_listview_row_checked(lv, 2));
    /* set_items resets flags */
    tk_listview_set_items(lv, items, 4);
    CHECK(!tk_listview_row_checked(lv, 3));
    CHECK(!tk_listview_is_header(lv, 1));
    tk_root_free(r);
}

/* ---------- file dialog ---------- */

static char fd_result[1400];
static int fd_calls;
static void on_fd(tk_widget *dlg, const char *path, void *ud)
{
    (void)dlg; (void)ud;
    fd_calls++;
    snprintf(fd_result, sizeof fd_result, "%s", path ? path : "(null)");
}

static void test_filedialog(void)
{
    const char *base = "/tmp/tk4fd";
    char cmd[256];
    snprintf(cmd, sizeof cmd,
             "rm -rf %s && mkdir -p %s/sub && "
             "touch %s/sub/inner.txt %s/a.txt %s/b.md %s/c.txt %s/.hidden",
             base, base, base, base, base, base, base);
    CHECK(system(cmd) == 0);

    const char *exts[1] = { "txt" };
    tk_widget *dlg = tk_filedialog_new("開く", base, exts, 1);
    CHECK(dlg != NULL);
    tk_filedialog_set_on_result(dlg, on_fd, NULL);
    tk_widget *list = tk_child_at(dlg, 1);
    CHECK(list != NULL);
    /* entries: "..", "sub/", "a.txt", "c.txt" (b.md filtered,
     * .hidden hidden, dirs first) */
    CHECK(tk_listview_count(list) == 4);
    CHECK(strcmp(tk_listview_item(list, 0), "..") == 0);
    CHECK(strcmp(tk_listview_item(list, 1), "sub/") == 0);
    CHECK(strcmp(tk_listview_item(list, 2), "a.txt") == 0);
    CHECK(strcmp(tk_listview_item(list, 3), "c.txt") == 0);

    tk_root *r = tk_root_new(tk_box_new(TK_DIR_VERTICAL, 0, 0));
    tk_root_layout(r, 800, 480);
    tk_filedialog_show(r, dlg);
    CHECK(tk_dialog_is_shown(r, dlg));
    /* activate "sub/" (selection starts on it after refresh) */
    tk_listview_select(list, 1);
    send_key(r, TK_KEY_ENTER);
    CHECK(strcmp(tk_filedialog_dir(dlg), "/tmp/tk4fd/sub") == 0);
    CHECK(tk_listview_count(list) == 2);        /* "..", "inner.txt" */
    /* back up via ".." */
    tk_listview_select(list, 0);
    send_key(r, TK_KEY_ENTER);
    CHECK(strcmp(tk_filedialog_dir(dlg), "/tmp/tk4fd") == 0);
    /* choose a.txt */
    fd_calls = 0; fd_result[0] = 0;
    tk_listview_select(list, 2);
    send_key(r, TK_KEY_ENTER);
    CHECK(fd_calls == 1);
    CHECK(strcmp(fd_result, "/tmp/tk4fd/a.txt") == 0);
    CHECK(!tk_dialog_is_shown(r, dlg));
    tk_root_free(r);
    tk_widget_free(dlg);
    snprintf(cmd, sizeof cmd, "rm -rf %s", base);
    int rc = system(cmd);
    (void)rc;
}

/* ---------- density ---------- */

static void test_density(void)
{
    tk_theme roomy = tk_theme_density(&tk_theme_v2, 1);
    CHECK(roomy.row_h == 67);        /* 54 * 5/4 (M22 floor) */
    CHECK(roomy.row_h2 == 87);       /* 70 * 5/4 */
    CHECK(roomy.control_h == 67);    /* 54 * 5/4 (M6 device-fit base) */
    CHECK(roomy.unit == tk_theme_v2.unit);   /* rhythm untouched */
    CHECK(roomy.font_px == tk_theme_v2.font_px);
    tk_theme same = tk_theme_density(&tk_theme_v2, 0);
    CHECK(same.row_h == tk_theme_v2.row_h);
    tk_theme max = tk_theme_density(&tk_theme_v2, 9); /* clamped */
    CHECK(max.row_h == 81);          /* 54 * 3/2 */
}

int main(void)
{
    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    test_scroll_shift();
    test_culling();
    test_animation();
    test_switch_knob();
    test_overlay_slide();
    test_setters();
    test_find();
    test_image_scale();
    test_pickers();
    test_listview_composite();
    test_filedialog();
    test_density();
    printf("tk4: %d passed, %d failed\n", checks - failures, failures);
    return failures ? 1 : 0;
}
