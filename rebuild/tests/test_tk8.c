/* test_tk8.c - audit M9: closing the recorded deltas and the
 * SS9 approximations.
 *
 * Every value checked here comes from a spec fetched on
 * 2026-11-20 (see REPORT-toolkit-m9.md SS0 for the source
 * list): M3 dialogs / navigation drawer / carousel spec
 * pages (m3.material.io), and the microsoft-ui-xaml
 * themeresources for ContentDialog, ToggleButton, InfoBar,
 * ProgressBar and ProgressRing. Device scale: dp x1.169
 * (187ppi), epx x1.43 - the SS2.2 conventions.
 *
 * Covered: dialog padding + width bands, app bar height +
 * scrolled container colour, WinUI slider thumb 18epx,
 * progress bar heights, ring continuous stroke + WinUI
 * no-track, switch disabled fades, menu content insets,
 * InfoBar severity form, split/menubar anchor toggle (the
 * M8 #5 C downgrade), carousel continuous sizing, SearchBar
 * caret-anchored composition, standard (in-layout) drawer,
 * and the same tree under all four themes. */
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
/* M10: the canvas blend arithmetic, for expected values */
static tk_color tk8_blend(tk_color s, tk_color d)
{
    int a = s.a, ia = 255 - a;
    tk_color o = { (uint8_t)((s.r * a + d.r * ia) / 255),
                   (uint8_t)((s.g * a + d.g * ia) / 255),
                   (uint8_t)((s.b * a + d.b * ia) / 255), 255 };
    return o;
}
static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
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
static int count_near(tk_canvas *c, tk_rect r, int cr, int cg, int cb,
                      int tol)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            if (col_near(px(c, x, y), cr, cg, cb, tol)) n++;
    return n;
}
static int notify_count;
static void on_notify(tk_widget *w, void *ud)
{
    (void)w; (void)ud; notify_count++;
}
static void solid_icon(uint32_t *px_, int n, int r, int g, int b)
{
    uint32_t v = 0xFF000000u | ((uint32_t)r << 16) |
                 ((uint32_t)g << 8) | (uint32_t)b;
    for (int i = 0; i < n; i++) px_[i] = v;
}

/* ---------- Dialog: spec padding + width bands ---------- */
static void test_dialog_spec(void)
{
    /* M3: padding 24dp = 28px, min width 280dp = 327.
     * WinUI ContentDialog: padding 24epx = 34px, min width
     * 320epx = 458, min height 184epx = 263. The padding is
     * observed as the content child's inset from the dialog's
     * left edge (a raw desired-width delta would mix in the
     * modes' different title font metrics). */
    tk_theme_set_default(&tk_theme_v2);
    {
        tk_widget *top = tk_label_new("");
        tk_root *r = tk_root_new(top);
        tk_root_layout(r, 800, 480);
        tk_widget *d = tk_dialog_new("題");
        tk_widget *lab = tk_label_new("短い本文");
        tk_add_child(d, lab);
        tk_dialog_show_modeless(r, d);
        tk_rect q = tk_widget_rect(d);
        CHECK(q.w == 327); /* M3 min width 280dp */
        CHECK(tk_widget_rect(lab).x - q.x == 28);
        tk_dialog_close(r, d);
        tk_widget_free(d);
        tk_root_free(r);
        tk_widget_free(top);
    }
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *top = tk_label_new("");
        tk_root *r = tk_root_new(top);
        tk_root_layout(r, 800, 480);
        tk_widget *d = tk_dialog_new("題");
        tk_widget *lab = tk_label_new("短い本文");
        tk_add_child(d, lab);
        tk_dialog_show_modeless(r, d);
        tk_rect q = tk_widget_rect(d);
        CHECK(q.w == 458); /* ContentDialog min width 320epx */
        CHECK(q.h >= 263); /* min height 184epx */
        CHECK(tk_widget_rect(lab).x - q.x == 34);
        tk_dialog_close(r, d);
        tk_widget_free(d);
        tk_root_free(r);
        tk_widget_free(top);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- AppBar: spec height + scrolled colour ---------- */
static void test_appbar_spec(void)
{
    tk_theme_set_default(&tk_theme_v2);
    {
        tk_widget *bar = tk_appbar_new("題名");
        tk_root *r = single_root(bar, NULL);
        tk_rect q = tk_widget_rect(bar);
        CHECK(q.h == 75); /* M3 small 64dp */
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 10, q.y + q.h / 2),
                     tk_theme_v2.surface));
        tk_canvas_free(c);
        tk_appbar_set_scrolled(bar, true);
        tk_root_layout(r, 800, 480);
        c = paint_full(r);
        /* scrolled = surfaceContainer (the ladder's middle
         * step), NOT surfaceContainerLow */
        CHECK(col_eq(px(c, q.x + q.w - 10, q.y + q.h / 2),
                     tk_theme_v2.container_mid));
        CHECK(!col_eq(px(c, q.x + q.w - 10, q.y + q.h / 2),
                      tk_theme_v2.container_low));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(bar);
    }
    tk_theme_set_default(&tk_theme_v2_dark);
    {
        tk_widget *bar = tk_appbar_new("題名");
        tk_root *r = single_root(bar, NULL);
        tk_rect q = tk_widget_rect(bar);
        CHECK(q.h == 75);
        tk_appbar_set_scrolled(bar, true);
        tk_root_layout(r, 800, 480);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w - 10, q.y + q.h / 2),
                     tk_theme_v2_dark.container_mid));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(bar);
    }
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *bar = tk_appbar_new("題名");
        tk_root *r = single_root(bar, NULL);
        CHECK(tk_widget_rect(bar).h == 65); /* 48epx compact (M22: x1.344) */
        tk_root_free(r);
        tk_widget_free(bar);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Slider: WinUI thumb is 18epx = 26px ---------- */
/* width of the (border|surface) run on the thumb's centre row */
static int thumb_span(tk_canvas *c, tk_rect q, const tk_theme *t)
{
    int y = q.y + q.h / 2;
    int lo = -1, hi = -1;
    for (int x = q.x; x < q.x + q.w; x++) {
        tk_color p = px(c, x, y);
        if (col_eq(p, t->border) || col_eq(p, t->surface)) {
            if (lo < 0) lo = x;
            hi = x;
        }
    }
    return lo < 0 ? 0 : hi - lo + 1;
}

static void test_slider_thumb(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        /* the WinUI border token is translucent, so the disc
         * outline blends to a mid gray on the white fill:
         * measure the span between the two outline pixels
         * (gray = equal channels, 150..245) on the centre row */
        tk_widget *s = tk_slider_new(0.5f);
        tk_root *r = single_root(s, NULL);
        tk_rect q = tk_widget_rect(s);
        tk_canvas *c = paint_full(r);
        int y = q.y + q.h / 2, lo = -1, hi = -1;
        for (int x = q.x; x < q.x + q.w; x++) {
            tk_color p = px(c, x, y);
            if (p.r == p.g && p.g == p.b && p.r >= 150 &&
                p.r <= 245) {
                if (lo < 0) lo = x;
                hi = x;
            }
        }
        CHECK(hi - lo + 1 == 24); /* M22: thumb 18epx at x1.344 */
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
    }
    tk_theme_set_default(&tk_theme_v2);
    {
        tk_widget *s = tk_slider_new(0.5f);
        tk_root *r = single_root(s, NULL);
        tk_rect q = tk_widget_rect(s);
        tk_canvas *c = paint_full(r);
        CHECK(thumb_span(c, q, &tk_theme_v2) == 28);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(s);
    }
}

/* ---------- Progress + Ring forms ---------- */
static void test_progress_ring(void)
{
    tk_theme_set_default(&tk_theme_v2);
    {
        tk_widget *p = tk_progress_new(0.5f);
        tk_root *r = single_root(p, NULL);
        CHECK(tk_widget_rect(p).h == 5); /* M3 track 4dp */
        tk_root_free(r);
        tk_widget_free(p);
        /* ring: continuous stroke - at 100% every sampled
         * point on the circle is the accent (the M7 dotted
         * form missed most of them) */
        tk_widget *ring = tk_ring_new(1.0f);
        tk_ring_set_size(ring, 96);
        tk_root *r2 = single_root(ring, NULL);
        tk_rect q = tk_widget_rect(ring);
        tk_canvas *c = paint_full(r2);
        int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
        int rad = (q.w < q.h ? q.w : q.h) / 2 - 4;
        int hits = 0;
        for (int a = 0; a < 360; a += 10) {
            float ang = a * 0.0174533f;
            int sx = cx + (int)(rad * cosf(ang));
            int sy = cy + (int)(rad * sinf(ang));
            if (count_near(c, (tk_rect){ sx - 1, sy - 1, 3, 3 },
                           tk_theme_v2.accent.r,
                           tk_theme_v2.accent.g,
                           tk_theme_v2.accent.b, 0) > 0)
                hits++;
        }
        CHECK(hits == 36);
        tk_canvas_free(c);
        tk_root_free(r2);
        tk_widget_free(ring);
    }
    tk_theme_set_default(&tk_theme_winui);
    {
        /* WinUI ProgressRing: NO track circle (transparent
         * background) - at 0% the ring paints nothing */
        tk_widget *ring = tk_ring_new(0.0f);
        tk_ring_set_size(ring, 96);
        tk_root *r = single_root(ring, NULL);
        tk_rect q = tk_widget_rect(ring);
        tk_canvas *c = paint_full(r);
        CHECK(count_near(c, q, tk_theme_winui.container_highest.r,
                         tk_theme_winui.container_highest.g,
                         tk_theme_winui.container_highest.b, 0) == 0);
        CHECK(count_near(c, q, tk_theme_winui.surface_alt.r,
                         tk_theme_winui.surface_alt.g,
                         tk_theme_winui.surface_alt.b, 0) == 0);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(ring);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Switch: disabled fades ---------- */
static void test_switch_disabled(void)
{
    tk_theme_set_default(&tk_theme_v2);
    {
        tk_widget *sw = tk_switch_new("項目", true);
        tk_root *r = single_root(sw, NULL);
        tk_rect q = tk_widget_rect(sw);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, cy), tk_theme_v2.accent));
        tk_canvas_free(c);
        tk_set_enabled(sw, false);
        tk_root_layout(r, 800, 480);
        c = paint_full(r);
        /* disabled ON: the track drops to
         * surfaceContainerHighest, never the accent */
        CHECK(col_eq(px(c, q.x + 6, cy),
                     tk_theme_v2.container_highest));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sw);
        /* disabled OFF: same faded track (was surface_alt);
         * probed at the track's right end, clear of the knob */
        tk_widget *sw2 = tk_switch_new("項目", false);
        tk_root *r2 = single_root(sw2, NULL);
        tk_rect q2 = tk_widget_rect(sw2);
        tk_set_enabled(sw2, false);
        tk_root_layout(r2, 800, 480);
        tk_canvas *c2 = paint_full(r2);
        CHECK(col_eq(px(c2, q2.x + 46, q2.y + q2.h / 2),
                      tk_theme_v2.container_highest));
        tk_canvas_free(c2);
        tk_root_free(r2);
        tk_widget_free(sw2);
    }
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *sw = tk_switch_new("項目", true);
        tk_root *r = single_root(sw, NULL);
        tk_rect q = tk_widget_rect(sw);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 6, cy), tk_theme_winui.accent));
        tk_canvas_free(c);
        tk_set_enabled(sw, false);
        tk_root_layout(r, 800, 480);
        c = paint_full(r);
        /* disabled ON: the accent is gone from the track.
         * M10: the checked disabled fill is its own token,
         * ControlFillColorDisabled over the page (the M9
         * paint used the rest ControlFill for both). */
        tk_color on_px = px(c, q.x + 6, cy);
        CHECK(!col_eq(on_px, tk_theme_winui.accent));
        CHECK(col_near_c(on_px,
            tk8_blend(tk_theme_winui.control_fill_dis,
                      tk_theme_winui.bg), 2));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sw);
        tk_widget *sw2 = tk_switch_new("項目", false);
        tk_root *r2 = single_root(sw2, NULL);
        tk_rect q2 = tk_widget_rect(sw2);
        tk_set_enabled(sw2, false);
        tk_root_layout(r2, 800, 480);
        tk_canvas *c2 = paint_full(r2);
        /* disabled OFF keeps the rest ControlFill (M10: it
         * now differs from the checked disabled fill) */
        CHECK(col_near_c(px(c2, q2.x + 46, q2.y + q2.h / 2),
            tk8_blend(tk_theme_winui.control_fill,
                      tk_theme_winui.bg), 2));
        CHECK(!col_eq(px(c2, q2.x + 46, q2.y + q2.h / 2),
                      on_px));
        tk_canvas_free(c2);
        tk_root_free(r2);
        tk_widget_free(sw2);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Menu: content insets ---------- */
static void test_menu_insets(void)
{
    /* measured relative to a label of the same string in
     * the same theme, so the modes' different font metrics
     * cancel: what remains is exactly the two content
     * insets - 12dp = 14px (M3) vs 11epx = 16px (WinUI) */
    tk_theme_set_default(&tk_theme_v2);
    tk_widget *m1 = tk_menu_new();
    tk_menu_add_item(m1, "ああああ", 1);
    tk_size s_v2 = tk_measure(m1, (tk_constraints){0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED});
    tk_widget *l1 = tk_label_new("ああああ");
    tk_size t_v2 = tk_measure(l1, (tk_constraints){0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED});
    CHECK(s_v2.w - t_v2.w == 28);
    tk_theme_set_default(&tk_theme_winui);
    tk_widget *m2 = tk_menu_new();
    tk_menu_add_item(m2, "ああああ", 1);
    tk_size s_wu = tk_measure(m2, (tk_constraints){0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED});
    tk_widget *l2 = tk_label_new("ああああ");
    tk_size t_wu = tk_measure(l2, (tk_constraints){0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED});
    CHECK(s_wu.w - t_wu.w == 32);
    tk_widget_free(m1);
    tk_widget_free(m2);
    tk_widget_free(l1);
    tk_widget_free(l2);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Snackbar: the InfoBar severity form ---------- */
static void test_snackbar_severity(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *top = tk_label_new("");
        tk_root *r = tk_root_new(top);
        tk_root_layout(r, 800, 480);
        tk_root_tick(r, 100);
        tk_snackbar_show_sev(r, "保存しました", TK_SNACK_SUCCESS,
                             NULL, NULL, NULL, 0);
        CHECK(tk_snackbar_visible(r));
        tk_canvas *c = paint_full(r);
        /* bar: x16, h = InfoBarMinHeight 48epx = 69 */
        tk_rect probe = { 16, 480 - 69 - 16, 560, 69 };
        /* severity background (SystemFill Success bg role) */
        CHECK(col_eq(px(c, probe.x + 120, probe.y + probe.h - 6),
                     tk_theme_winui.sys_ok_bg));
        /* severity icon disc: solid fill at the 16epx inset */
        CHECK(col_eq(px(c, probe.x + 34, probe.y + probe.h / 2),
                     tk_theme_winui.sys_ok));
        tk_canvas_free(c);
        tk_snackbar_hide(r);
        /* informational: M10 - the Attention values are now
         * sourced (Microsoft Learn Fluent token table), so
         * the disc is the solid SystemFillColorAttention and
         * the background its translucent pair, no longer the
         * accent stand-in / card background of M9. Probed
         * off-centre: the glyph stem sits at the disc centre */
        tk_snackbar_show_sev(r, "情報", TK_SNACK_INFO,
                             NULL, NULL, NULL, 0);
        c = paint_full(r);
        CHECK(col_eq(px(c, probe.x + 34 - 6,
                        probe.y + probe.h / 2 - 6),
                     tk_theme_winui.sys_attn));
        CHECK(col_near_c(px(c, probe.x + 120,
                            probe.y + probe.h - 6),
            tk8_blend(tk_theme_winui.sys_attn_bg,
                      tk_theme_winui.bg), 2));
        CHECK(!col_eq(px(c, probe.x + 120, probe.y + probe.h - 6),
                      tk_theme_winui.sys_ok_bg));
        tk_canvas_free(c);
        tk_snackbar_hide(r);
        tk_root_free(r);
        tk_widget_free(top);
    }
    tk_theme_set_default(&tk_theme_v2);
    {
        /* the M3 form has no severity: height and the inverse
         * pairing are unchanged by the severity argument */
        tk_widget *top = tk_label_new("");
        tk_root *r = tk_root_new(top);
        tk_root_layout(r, 800, 480);
        tk_root_tick(r, 100);
        tk_snackbar_show_sev(r, "失敗", TK_SNACK_ERROR,
                             NULL, NULL, NULL, 0);
        /* M12: the M3 form now slides up on show (200 ms);
         * run the entrance out before probing the settled
         * face - the entrance itself is verified in tk11 */
        tk_root_tick(r, 300);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, 120, 480 - 16 - 28),
                     tk_theme_v2.inverse_surface));
        tk_canvas_free(c);
        tk_snackbar_hide(r);
        tk_root_free(r);
        tk_widget_free(top);
    }
}

/* ---------- #5 C: the anchor toggles its menu ---------- */
static int split_main_hits, split_item_hits;
static void on_split_main(tk_widget *w, void *ud)
{
    (void)w; (void)ud; split_main_hits++;
}
static void on_split_item(tk_widget *w, void *ud)
{
    (void)w; (void)ud; split_item_hits++;
}

static void test_anchor_toggle(void)
{
    tk_theme_set_default(&tk_theme_v2);
    /* split button: arrow opens, arrow again CLOSES (through
     * M8 the root's press dismissal plus the arrow's release
     * handler reopened it at once) */
    tk_widget *sp = tk_splitbutton_new("保存", on_split_main, NULL);
    tk_widget *menu = tk_menu_new();
    tk_menu_add_item(menu, "名前を付けて保存", 1);
    tk_menu_add_item(menu, "上書き保存", 2);
    tk_menu_set_on_select(menu, on_split_item, NULL);
    tk_splitbutton_set_menu(sp, menu);
    tk_root *r = single_root(sp, NULL);
    tk_widget *arrow = tk_splitbutton_arrow(sp);
    click_widget(r, arrow);
    CHECK(tk_menu_is_open(r, menu));
    click_widget(r, arrow);
    CHECK(!tk_menu_is_open(r, menu));
    click_widget(r, arrow);
    CHECK(tk_menu_is_open(r, menu));
    /* selecting still works after the toggle cycles */
    tk_rect aq = tk_widget_rect(arrow);
    send_click(r, aq.x + 20, aq.y + aq.h + 24);
    CHECK(split_item_hits == 1);
    CHECK(!tk_menu_is_open(r, menu));
    tk_root_free(r);
    tk_widget_free(sp);

    /* menubar: a title toggles its own menu */
    tk_widget *bar = tk_menubar_new();
    tk_widget *ma = tk_menu_new();
    tk_menu_add_item(ma, "開く", 1);
    tk_widget *mb = tk_menu_new();
    tk_menu_add_item(mb, "情報", 2);
    tk_menubar_add_menu(bar, "ファイル", ma);
    tk_menubar_add_menu(bar, "ヘルプ", mb);
    tk_root *r2 = single_root(bar, NULL);
    tk_rect bq = tk_widget_rect(bar);
    send_click(r2, bq.x + 20, bq.y + bq.h / 2);
    CHECK(tk_menu_is_open(r2, ma));
    send_click(r2, bq.x + 20, bq.y + bq.h / 2);
    CHECK(!tk_menu_is_open(r2, ma));
    send_click(r2, bq.x + 20, bq.y + bq.h / 2);
    CHECK(tk_menu_is_open(r2, ma));
    tk_root_free(r2);
    tk_widget_free(bar);

    /* combobox (non-editable): the same toggle through the
     * owner record - its dropdown is recreated per open, so
     * the anchor is recognised by owner, not by identity */
    const char *items[3] = { "赤", "緑", "青" };
    tk_widget *cb = tk_combobox_new();
    tk_combobox_set_items(cb, items, 3);
    tk_root *r3 = single_root(cb, NULL);
    click_widget(r3, cb);
    CHECK(tk_combobox_is_open(cb));
    click_widget(r3, cb);
    CHECK(!tk_combobox_is_open(cb));
    click_widget(r3, cb);
    CHECK(tk_combobox_is_open(cb));
    tk_root_free(r3);
    tk_widget_free(cb);
}

/* ---------- Carousel: continuous item sizing ---------- */
static void test_carousel_continuous(void)
{
    tk_theme_set_default(&tk_theme_v2);
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
    tk_root *r = single_root(car, NULL);
    tk_rect q = tk_widget_rect(car);
    int my = q.y + q.h / 2;
    /* drag half a step (36 of 73px): item 0 must be PART WAY
     * between large and small - its right edge sits near
     * x + 157, not at the settled 248-36=212 the discrete
     * form painted, nor at the small width */
    send_ptr(r, TK_EV_POINTER_DOWN, q.x + 100, my);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + 64, my);
    tk_canvas *c = paint_full(r);
    int edge = -1;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near(px(c, x, my), 30, 200, 30, 24)) {
            edge = x;
            break;
        }
    CHECK(edge > q.x + 120 && edge < q.x + 185);
    tk_canvas_free(c);
    send_ptr(r, TK_EV_POINTER_UP, q.x + 64, my);
    /* under half a step the snap returns to item 0 */
    CHECK(tk_carousel_index(car) == 0);
    tk_root_free(r);
    tk_widget_free(car);
}

/* ---------- SearchBar: composition at the caret ---------- */
static void test_searchbar_composition(void)
{
    tk_theme_set_default(&tk_theme_v2);
    tk_widget *sb = tk_searchbar_new("検索");
    tk_searchbar_set_text(sb, "あい");
    tc_buffer *b = tk_searchbar_buffer(sb);
    tc_move_to(b, 0, false); /* caret before the text */
    tk_root *r = single_root(sb, NULL);
    tk_rect q = tk_widget_rect(sb);
    /* zone where the preedit must render: the first ~90px of
     * the text area, rows below the text centre (underlines)
     * and the text rows themselves */
    tk_rect zone = { q.x + 48, q.y + q.h / 2, 90, q.h / 2 };
    tk_canvas *c0 = paint_full(r);
    int plain = count_near(c0, zone, tk_theme_v2.accent.r,
                           tk_theme_v2.accent.g,
                           tk_theme_v2.accent.b, 0);
    tk_canvas_free(c0);
    CHECK(plain == 0);
    size_t segs[2] = { 6, 3 }; /* かん | じ */
    tc_set_composition_segments(b, "かんじ", segs, 2, 0, false);
    tk_root_layout(r, 800, 480);
    tk_canvas *c = paint_full(r);
    /* the active segment's 2px accent underline now sits at
     * the caret (the text start), not after the committed
     * text where the M7 form appended it */
    int acc = count_near(c, zone, tk_theme_v2.accent.r,
                         tk_theme_v2.accent.g,
                         tk_theme_v2.accent.b, 0);
    CHECK(acc > 20);
    tk_canvas_free(c);
    tc_set_composition(b, NULL);
    tk_root_free(r);
    tk_widget_free(sb);
}

/* ---------- Drawer: the standard in-layout form ---------- */
static void test_drawer_standard(void)
{
    tk_theme_set_default(&tk_theme_v2);
    tk_widget *hbox = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
    tk_widget *dr = tk_drawer_new("メニュー");
    tk_drawer_add_item(dr, "ホーム", 10, NULL, 0, 0);
    tk_drawer_add_item(dr, "設定", 20, NULL, 0, 0);
    notify_count = 0;
    tk_drawer_set_on_select(dr, on_notify, NULL);
    tk_drawer_attach(hbox, dr);
    tk_widget *body = tk_label_new("本体");
    tk_add_child(hbox, body);
    tk_root *r = tk_root_new(hbox);
    tk_root_layout(r, 800, 480);
    CHECK(tk_drawer_is_standard(dr));
    tk_rect dq = tk_widget_rect(dr);
    /* in-layout: at the box's left edge, full height, sized
     * to content under the 360dp = 421px cap */
    CHECK(dq.x == 0 && dq.h == 480 && dq.w > 0 && dq.w <= 421);
    /* the content starts after the panel */
    tk_rect bq = tk_widget_rect(body);
    CHECK(bq.x >= dq.x + dq.w);
    /* square far corner in the standard form: the corner
     * pixel is the container fill, not the page behind */
    tk_canvas *c = paint_full(r);
    CHECK(col_eq(px(c, dq.x + dq.w - 2, dq.y + 2),
                 tk_theme_v2.container_low));
    tk_canvas_free(c);
    /* selecting fires and the panel STAYS (no dismissal) */
    send_click(r, dq.x + 60, dq.y + 16 + 56 + 32);
    CHECK(notify_count == 1);
    CHECK(tk_drawer_selected(dr) == 10);
    CHECK(tk_drawer_is_standard(dr));
    CHECK(tk_widget_rect(dr).w == dq.w);
    tk_root_free(r);
    tk_widget_free(hbox);
}

/* ---------- One tree, four themes ---------- */
static void test_four_themes(void)
{
    const tk_theme *themes[4] = { &tk_theme_v2, &tk_theme_v2_dark,
                                  &tk_theme_winui,
                                  &tk_theme_winui_dark };
    const int bars[4] = { 75, 75, 65, 65 }; /* M22: WinUI bar 69 -> 65 */
    const int progs[4] = { 5, 5, 4, 4 };
    for (int i = 0; i < 4; i++) {
        tk_theme_set_default(themes[i]);
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 8);
        tk_widget *bar = tk_appbar_new("題名");
        tk_add_child(box, bar);
        tk_widget *sw = tk_switch_new("切替", true);
        tk_add_child(box, sw);
        tk_widget *sl = tk_slider_new(0.4f);
        tk_add_child(box, sl);
        tk_widget *pg = tk_progress_new(0.6f);
        tk_add_child(box, pg);
        tk_widget *tf = tk_textfield_new("入力");
        tk_add_child(box, tf);
        tk_widget *bt = tk_button_new("実行");
        tk_add_child(box, bt);
        tk_root *r = tk_root_new(box);
        tk_root_layout(r, 800, 480);
        CHECK(tk_widget_rect(bar).h == bars[i]);
        CHECK(tk_widget_rect(pg).h == progs[i]);
        tk_canvas *c = paint_full(r);
        /* the tree actually painted: several distinct colours
         * on the canvas (bg, surface, accent, text...) */
        int distinct = 0;
        tk_color seen[16];
        for (int y = 0; y < 480 && distinct < 16; y += 7)
            for (int x = 0; x < 800 && distinct < 16; x += 7) {
                tk_color p = px(c, x, y);
                bool known = false;
                for (int k = 0; k < distinct; k++)
                    if (col_eq(p, seen[k])) { known = true; break; }
                if (!known) seen[distinct++] = p;
            }
        CHECK(distinct >= 5);
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(box);
    }
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    tk_font *f = tk_font_load_bundled();
    CHECK(f != NULL);
    if (f) tk_font_install(f);
    test_dialog_spec();
    test_appbar_spec();
    test_slider_thumb();
    test_progress_ring();
    test_switch_disabled();
    test_menu_insets();
    test_snackbar_severity();
    test_anchor_toggle();
    test_carousel_continuous();
    test_searchbar_composition();
    test_drawer_standard();
    test_four_themes();
    printf("tk8: %d passed, %d failed\n", checks - failures,
           failures);
    return failures ? 1 : 0;
}
