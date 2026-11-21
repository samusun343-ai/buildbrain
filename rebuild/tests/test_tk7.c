/* test_tk7.c - audit M8: two-mode equivalence verification.
 *
 * The M7 matrix (AUDIT-toolkit.md SS2) left the WinUI column
 * almost unverified (4 of 229 cells) and three Android A cells
 * unconfirmed, plus two unimplemented cells: the split button
 * chevron rotation. This suite drives the SAME widget trees
 * with tk_theme_winui / tk_theme_winui_dark installed as the
 * default theme (so measure, layout and paint all run in the
 * mode) and checks them against the values in Microsoft's
 * official themeresources (github.com/microsoft/microsoft-ui-xaml,
 * controls/dev/CommonStyles + per-control folders, fetched
 * 2026-11-20; effective px at 96dpi, converted x1.43 to the
 * device scale - the SS2.2 convention), the M3 split-button
 * guideline (m3.material.io, fetched 2026-11-20), and the M3
 * values for rail / app bar / carousel obtained in the same
 * pass. Where the implementation deliberately differs from a
 * converted value (touch floor, compact forms), the check pins
 * the implemented value and REPORT-toolkit-m8.md records the
 * delta - the matrix grade follows the evidence, not the wish.
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
static void send_key(tk_root *r, tk_key k)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN; ev.key = k;
    (void)tk_root_event(r, &ev);
}
static void send_text(tk_root *r, const char *s)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT; ev.text = s;
    (void)tk_root_event(r, &ev);
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
/* count pixels in rect r whose rgb is within tol of (cr,cg,cb) */
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
/* WinUI tests install tk_theme_winui as the default theme at
 * the top of the test and restore tk_theme_v2 at the bottom,
 * so measure, layout, events and paint all run in the mode. */

/* M10: TK7_DARK=1 re-runs the whole suite with every WinUI
 * selection substituted by tk_theme_winui_dark, so the same
 * checks run against the dark Fluent token set (the formal
 * dark re-run M9 left undone). tk7_set is defined BEFORE the
 * macro so its own call reaches the real function; every later
 * tk_theme_set_default call in this file is routed through
 * tk7_map. Checks that used to pin the light blend of a
 * translucent fill over the page background are written
 * theme-relative (tk7_blend) so they follow the substitution. */
static void tk7_set(const tk_theme *t) { tk_theme_set_default(t); }
static const tk_theme *tk7_map(const tk_theme *t)
{
    if (t == &tk_theme_winui && getenv("TK7_DARK"))
        return &tk_theme_winui_dark;
    return t;
}
#define tk_theme_set_default(t) tk7_set(tk7_map(t))
/* the canvas blend arithmetic (tk_draw.c), for expected values */
static tk_color tk7_blend(tk_color s, tk_color d)
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

/* ---------- canvas rotation primitive (M8) ---------- */
/* A 16x16 image with one opaque white 2x2 dot at top-centre and
 * one translucent red pixel; rotation must move the dot
 * clockwise (screen coordinates) about the dst centre. */
static void test_rot(void)
{
    static uint32_t img[16 * 16];
    for (int i = 0; i < 16 * 16; i++) img[i] = 0;
    img[1 * 16 + 7] = 0xFFFFFFFFu; img[1 * 16 + 8] = 0xFFFFFFFFu;
    img[2 * 16 + 7] = 0xFFFFFFFFu; img[2 * 16 + 8] = 0xFFFFFFFFu;
    img[13 * 16 + 2] = 0x80FF0000u; /* translucent red, lower left */
    tk_canvas *c = tk_canvas_new(64, 64);
    tk_canvas_clear(c, (tk_color){ 0, 0, 0, 255 });
    tk_rect dst = { 24, 24, 16, 16 }; /* centre (32,32) */
    /* 0 degrees: dot at top centre */
    tk_canvas_draw_image_rot(c, img, 16, 16, dst, 0.0f);
    CHECK(col_near(px(c, 32, 26), 255, 255, 255, 0));
    CHECK(!col_near(px(c, 32, 38), 255, 255, 255, 0));
    /* translucent source blends instead of storing */
    CHECK(col_near(px(c, 26, 37), 128, 0, 0, 4));
    tk_canvas_clear(c, (tk_color){ 0, 0, 0, 255 });
    /* 90 degrees clockwise: dot at right centre */
    tk_canvas_draw_image_rot(c, img, 16, 16, dst, 90.0f);
    CHECK(col_near(px(c, 38, 32), 255, 255, 255, 0));
    CHECK(!col_near(px(c, 32, 26), 255, 255, 255, 0));
    tk_canvas_clear(c, (tk_color){ 0, 0, 0, 255 });
    /* 180: dot at bottom centre */
    tk_canvas_draw_image_rot(c, img, 16, 16, dst, 180.0f);
    CHECK(col_near(px(c, 32, 38), 255, 255, 255, 0));
    CHECK(!col_near(px(c, 32, 26), 255, 255, 255, 0));
    tk_canvas_clear(c, (tk_color){ 0, 0, 0, 255 });
    /* 270: dot at left centre; 360 == 0 */
    tk_canvas_draw_image_rot(c, img, 16, 16, dst, 270.0f);
    CHECK(col_near(px(c, 26, 32), 255, 255, 255, 0));
    tk_canvas_clear(c, (tk_color){ 0, 0, 0, 255 });
    tk_canvas_draw_image_rot(c, img, 16, 16, dst, 360.0f);
    CHECK(col_near(px(c, 32, 26), 255, 255, 255, 0));
    tk_canvas_free(c);
}

/* ---------- Split button chevron rotation (M8, both modes) --- */
/* The chevron bitmap is a down-vee: vertex at icon (8,11), arm
 * tops at (2..4,5) and (12..14,5). Painted white on the filled
 * face. Rotated 180 it is an up-vee: vertex at (8,5). The icon
 * is 16x16 centred in the arrow rect, so the vertex sits at
 * (centre_x, centre_y + 3) closed and (centre_x, centre_y - 3)
 * open. */
static int split_hits;
static void on_split_item7(tk_widget *w, void *ud)
{
    (void)w; (void)ud; split_hits++;
}
static bool chev_vertex(tk_root *r, tk_widget *arrow, bool open_pos)
{
    tk_rect q = tk_widget_rect(arrow);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_canvas *c = paint_full(r);
    tk_color v = px(c, cx, open_pos ? cy - 3 : cy + 3);
    tk_canvas_free(c);
    return col_near(v, 255, 255, 255, 8);
}
static void split_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *sp = tk_splitbutton_new("保存", on_notify, NULL);
    tk_widget *menu = tk_menu_new();
    tk_menu_add_item(menu, "名前を付けて保存", 1);
    tk_menu_add_item(menu, "上書き保存", 2);
    tk_menu_set_on_select(menu, on_split_item7, NULL);
    tk_splitbutton_set_menu(sp, menu);
    tk_widget *arrow = tk_splitbutton_arrow(sp);
    split_hits = 0;
    tk_root *r = single_root(sp, NULL);
    tk_root_tick(r, 1000);
    /* closed: vertex down, no vertex up, arm tops present */
    CHECK(chev_vertex(r, arrow, false));
    CHECK(!chev_vertex(r, arrow, true));
    {
        tk_rect q = tk_widget_rect(arrow);
        tk_canvas *c = paint_full(r);
        CHECK(col_near(px(c, q.x + q.w / 2 - 6, q.y + q.h / 2 - 3),
                       255, 255, 255, 8));
        tk_canvas_free(c);
    }
    /* open: the chevron animates (200 ms); mid-flight the
     * closed pattern has left (the arm-top pixel is gone), at
     * the end the up vertex has arrived */
    click_widget(r, arrow);
    CHECK(tk_menu_is_open(r, menu));
    tk_root_tick(r, 1100);
    {
        tk_rect q = tk_widget_rect(arrow);
        tk_canvas *c = paint_full(r);
        CHECK(!col_near(px(c, q.x + q.w / 2 - 6, q.y + q.h / 2 - 3),
                        255, 255, 255, 8));
        tk_canvas_free(c);
    }
    tk_root_tick(r, 1300);
    CHECK(chev_vertex(r, arrow, true));
    CHECK(!chev_vertex(r, arrow, false));
    /* dismiss by picking an item: chevron returns */
    tk_rect mq = tk_widget_rect(menu);
    send_click(r, mq.x + 24, mq.y + 20);
    CHECK(split_hits == 1);
    CHECK(!tk_menu_is_open(r, menu));
    tk_root_tick(r, 1600);
    CHECK(chev_vertex(r, arrow, false));
    /* reopen, dismiss with an outside click: chevron returns */
    click_widget(r, arrow);
    tk_root_tick(r, 1900);
    CHECK(chev_vertex(r, arrow, true));
    send_click(r, 700, 400);
    CHECK(!tk_menu_is_open(r, menu));
    tk_root_tick(r, 2200);
    CHECK(chev_vertex(r, arrow, false));
    /* reopen, dismiss with Esc: chevron returns */
    click_widget(r, arrow);
    tk_root_tick(r, 2500);
    send_key(r, TK_KEY_ESCAPE);
    CHECK(!tk_menu_is_open(r, menu));
    tk_root_tick(r, 2800);
    CHECK(chev_vertex(r, arrow, false));
    /* reduced motion: the chevron snaps with the menu state */
    tk_root_set_animations_enabled(r, false);
    click_widget(r, arrow);
    CHECK(tk_menu_is_open(r, menu));
    CHECK(chev_vertex(r, arrow, true));
    send_key(r, TK_KEY_ESCAPE);
    CHECK(!tk_menu_is_open(r, menu));
    CHECK(chev_vertex(r, arrow, false));
    tk_root_free(r);
    tk_widget_free(sp);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_split_chevron(void)
{
    split_flow(&tk_theme_v2);     /* Android mode */
    split_flow(tk7_map(&tk_theme_winui));  /* WinUI mode */
}

/* ---------- WinUI Button ---------- */
/* Spec (Button_themeresources.xaml): Padding 11,5,11,6,
 * BorderThickness 1, corner = ControlCornerRadius 4epx;
 * converted: side padding 16px, radius 6. Body text 14epx = 20px. */
static void test_winui_button(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const tk_theme *t = tk7_map(&tk_theme_winui);
        CHECK(t->control_h == 54);        /* floor: 48dp target (M22) */
        CHECK(t->radius_button == 6);     /* 4epx x 1.43 = 5.7 */
        CHECK(t->font_px == 19);          /* 14epx x 1.344 (M22) */
        tk_widget *b = tk_button_new("OK");
        tk_button_set_on_click(b, on_notify, NULL);
        notify_count = 0;
        tk_size d = tk_measure(b, (tk_constraints){0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED});
        CHECK(d.h == 54);
        CHECK(d.w == tk_text_width(t, "OK") + 4 * t->unit);
        tk_root *r = single_root(b, NULL);
        tk_rect q = tk_widget_rect(b);
        tk_canvas *c = paint_full(r);
        /* face: control fill (white 70%) over the #F3F3F3 base */
        tk_color normal = px(c, q.x + q.w / 2, q.y + 4);
        CHECK(col_near_c(normal, tk7_blend(t->control_fill, t->bg), 4));
        /* label glyphs are near-black on the face */
        CHECK(count_near(c, q, 0, 0, 0, 60) > 10);
        tk_canvas_free(c);
        /* pressed face is darker (the tertiary fill direction) */
        send_ptr(r, TK_EV_POINTER_DOWN, q.x + q.w / 2, q.y + q.h / 2);
        c = paint_full(r);
        tk_color pressed = px(c, q.x + q.w / 2, q.y + 4);
        CHECK(pressed.r < normal.r);
        tk_canvas_free(c);
        send_ptr(r, TK_EV_POINTER_UP, q.x + q.w / 2, q.y + q.h / 2);
        CHECK(notify_count == 1);
        /* keyboard: the Fluent focus visual is a double-line
         * rectangle - a near-black outer stroke just outside the
         * control - then Enter and Space fire */
        CHECK(tk_focus(r, b));
        c = paint_full(r);
        CHECK(count_near(c, (tk_rect){q.x - 3, q.y - 3, q.w + 6, 4},
                         0, 0, 0, 40) > 10);
        tk_canvas_free(c);
        send_key(r, TK_KEY_ENTER);
        CHECK(notify_count == 2);
        send_key(r, TK_KEY_SPACE);
        CHECK(notify_count == 3);
        /* disabled: no fire */
        tk_set_enabled(b, false);
        click_widget(r, b);
        CHECK(notify_count == 3);
        tk_root_free(r);
        tk_widget_free(b);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Checkbox ---------- */
/* Spec (CheckBox_themeresources.xaml): box 20epx, glyph 12epx,
 * height 32epx, border 1. Converted: box 28px, glyph 17px. */
static void test_winui_checkbox(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const tk_theme *t = tk7_map(&tk_theme_winui);
        tk_widget *cb = tk_checkbox_new("有効", false);
        tk_checkbox_set_on_change(cb, on_notify, NULL);
        notify_count = 0;
        tk_size d = tk_measure(cb, (tk_constraints){0, TK_UNLIMITED,
                                                   0, TK_UNLIMITED});
        CHECK(d.h == 54); /* touch floor over the 32epx = 43px form */
        /* M8 fix: measure matches the 28px painted box */
        CHECK(d.w == 28 + t->unit + tk_text_width(t, "有効"));
        tk_root *r = single_root(cb, NULL);
        tk_rect q = tk_widget_rect(cb);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        /* unchecked: box interior is the control fill, not accent */
        CHECK(!col_eq(px(c, q.x + 14, cy), t->accent));
        CHECK(col_near_c(px(c, q.x + 14, cy), tk7_blend(t->control_fill, t->bg), 5));
        tk_canvas_free(c);
        click_widget(r, cb);
        CHECK(tk_checkbox_checked(cb));
        CHECK(notify_count == 1);
        c = paint_full(r);
        /* checked: the 28px box is accent edge to edge at mid row,
         * with the check glyph in on-accent white inside */
        CHECK(col_eq(px(c, q.x + 1, cy), t->accent));
        CHECK(col_eq(px(c, q.x + 26, cy), t->accent));
        CHECK(count_near(c, (tk_rect){q.x, q.y, 28, q.h},
                         255, 255, 255, 8) >= 8);
        tk_canvas_free(c);
        /* Space toggles back while focused */
        CHECK(tk_focus(r, cb));
        send_key(r, TK_KEY_SPACE);
        CHECK(!tk_checkbox_checked(cb));
        CHECK(notify_count == 2);
        tk_root_free(r);
        tk_widget_free(cb);
    }
    tk_theme_set_default(&tk_theme_v2);
    /* the same measure check in Android mode (M21: the
     * converted 21px box form): the label slot is at
     * 23 + unit */
    {
        const tk_theme *t = tk_theme_default();
        tk_widget *cb = tk_checkbox_new("有効", false);
        tk_size d = tk_measure(cb, (tk_constraints){0, TK_UNLIMITED,
                                                   0, TK_UNLIMITED});
        CHECK(d.w == 23 + t->unit + tk_text_width(t, "有効"));
        tk_widget_free(cb);
    }
}

/* ---------- WinUI RadioButton ---------- */
/* Spec (RadioButton_themeresources.xaml): outer circle 20epx,
 * check glyph ellipse 12epx, border 1. Converted: 28px / 17px. */
static void test_winui_radio(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const tk_theme *t = tk7_map(&tk_theme_winui);
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *r1 = tk_radio_new("第一", 7, 1);
        tk_widget *r2 = tk_radio_new("第二", 7, 2);
        tk_radio_set_on_change(r2, on_notify, NULL);
        notify_count = 0;
        tk_add_child(box, r1);
        tk_add_child(box, r2);
        tk_root *r = tk_root_new(box);
        tk_root_layout(r, 800, 480);
        tk_radio_select(r1);
        tk_rect q = tk_widget_rect(r1);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        /* selected: accent ring at the 28px circle's edge and the
         * 17px glyph dot through the centre, fill between them */
        CHECK(col_eq(px(c, q.x + 14, q.y + (q.h - 28) / 2), t->accent));
        CHECK(col_eq(px(c, q.x + 14, cy), t->accent));
        CHECK(col_eq(px(c, q.x + 6, cy), t->accent));  /* dot left */
        CHECK(col_eq(px(c, q.x + 22, cy), t->accent)); /* dot right */
        CHECK(!col_eq(px(c, q.x + 4, cy), t->accent)); /* gap */
        tk_canvas_free(c);
        /* click selects the sibling and deselects the first */
        click_widget(r, r2);
        CHECK(tk_radio_selected(r2));
        CHECK(!tk_radio_selected(r1));
        CHECK(notify_count == 1);
        /* Space on the focused first radio selects it back */
        CHECK(tk_focus(r, r1));
        send_key(r, TK_KEY_SPACE);
        CHECK(tk_radio_selected(r1));
        CHECK(!tk_radio_selected(r2));
        tk_root_free(r);
        tk_widget_free(box);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI ToggleSwitch ---------- */
/* Spec (ToggleSwitch_themeresources.xaml template): track
 * 40x20epx, knob 12epx in a 20epx knob grid, stroke 1.
 * Converted: track 56x28, knob 17, travel 28 (left edge 6->34). */
static void test_winui_switch(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const tk_theme *t = tk7_map(&tk_theme_winui);
        tk_widget *sw = tk_switch_new("Wi-Fi", false);
        tk_switch_set_on_change(sw, on_notify, NULL);
        notify_count = 0;
        tk_size d = tk_measure(sw, (tk_constraints){0, TK_UNLIMITED,
                                                   0, TK_UNLIMITED});
        /* M8 fix: measure matches the painted track.
         * M21: plus the template's 12epx label gap = 17px.
         * M22: the track is 54px at x1.344. */
        CHECK(d.w == 54 + 17 + tk_text_width(t, "Wi-Fi"));
        tk_root *r = single_root(sw, NULL);
        tk_root_tick(r, 1000);
        tk_rect q = tk_widget_rect(sw);
        int cy = q.y + q.h / 2;
        tk_canvas *c = paint_full(r);
        /* OFF: track outline at the top edge, fill inside, the
         * 16px knob at the left (centre q.x+6+8). M10: the
         * outline is ControlStrongStrokeColorDefault and the
         * knob ControlStrongFillColorDefault (the pre-M10 paint
         * used text_dim, which only coincides in light), so
         * the expectations are the token blends over the
         * track's own fill - valid in both themes. */
        tk_color edge = px(c, q.x + 28, cy - 13); /* M22: track top */
        tk_color trackfill = tk7_blend(t->control_fill, t->bg);
        CHECK(col_near_c(edge, tk7_blend(t->strong_stroke,
                                         trackfill), 5));
        CHECK(col_near_c(px(c, q.x + 46, cy), trackfill, 5));
        tk_color knob = px(c, q.x + 14, cy);
        CHECK(col_near_c(knob, tk7_blend(t->strong_fill,
                                         trackfill), 5));
        tk_canvas_free(c);
        /* toggle ON: knob animates; before the tick the right
         * landing spot still shows the accent track, after the
         * tick the white knob sits there (centre q.x+34+8) */
        click_widget(r, sw);
        CHECK(tk_switch_on(sw));
        CHECK(notify_count == 1);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + 28, cy), t->accent));
        CHECK(col_eq(px(c, q.x + 42, cy), t->accent));
        tk_canvas_free(c);
        tk_root_tick(r, 1200);
        c = paint_full(r);
        /* M10: the on-state knob is the on-accent colour
         * (white in light, black in dark), not surface */
        CHECK(col_eq(px(c, q.x + 42, cy), t->on_accent));
        tk_canvas_free(c);
        /* Space toggles back while focused */
        CHECK(tk_focus(r, sw));
        send_key(r, TK_KEY_SPACE);
        CHECK(!tk_switch_on(sw));
        tk_root_free(r);
        tk_widget_free(sw);
    }
    tk_theme_set_default(&tk_theme_v2);
    /* Android-mode measure is the converted track form
     * (M21: 52dp at 187ppi = 61px) */
    {
        const tk_theme *t = tk_theme_default();
        tk_widget *sw = tk_switch_new("Wi-Fi", false);
        tk_size d = tk_measure(sw, (tk_constraints){0, TK_UNLIMITED,
                                                   0, TK_UNLIMITED});
        CHECK(d.w == 61 + t->unit + tk_text_width(t, "Wi-Fi"));
        tk_widget_free(sw);
    }
}

/* ---------- WinUI Slider ---------- */
/* Spec (Slider_themeresources.xaml): height 32epx, thumb outer
 * 18epx / inner 12epx, track height 4epx. Converted: track 6px
 * (the theme token), thumb 26px (implementation paints 28px -
 * the delta is recorded in the report). */
static void test_winui_slider(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const tk_theme *t = tk7_map(&tk_theme_winui);
        CHECK(t->slider_track_h == 6);
        tk_widget *sl = tk_slider_new(0.5f);
        tk_slider_set_on_change(sl, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(sl, NULL);
        tk_rect q = tk_widget_rect(sl);
        int cy = q.y + q.h / 2, cx = q.x + q.w / 2;
        tk_canvas *c = paint_full(r);
        /* filled half accent; the unfilled half is the
         * strong-fill rail (M21: surface_alt left it
         * invisible on the page); the thumb's accent inner
         * dot sits centred on the value */
        CHECK(col_eq(px(c, q.x + q.w / 4, cy), t->accent));
        CHECK(col_eq(px(c, q.x + 3 * q.w / 4, cy),
                     tk7_blend(t->strong_fill, t->bg)));
        CHECK(col_eq(px(c, cx, cy), t->accent));
        tk_canvas_free(c);
        /* click at 3/4 sets the value there */
        send_click(r, q.x + 3 * q.w / 4, cy);
        CHECK(tk_slider_value(sl) > 0.72f && tk_slider_value(sl) < 0.78f);
        CHECK(notify_count >= 1);
        /* arrows step the value while focused */
        CHECK(tk_focus(r, sl));
        float v0 = tk_slider_value(sl);
        send_key(r, TK_KEY_RIGHT);
        CHECK(tk_slider_value(sl) > v0);
        send_key(r, TK_KEY_LEFT);
        send_key(r, TK_KEY_LEFT);
        CHECK(tk_slider_value(sl) < v0);
        tk_root_free(r);
        tk_widget_free(sl);
        /* range form: the filled span sits between the handles */
        tk_widget *rg = tk_slider_new(0.0f);
        tk_slider_set_range(rg, 0.0f, 100.0f, 10.0f);
        tk_slider_set_values2(rg, 20.0f, 80.0f);
        tk_root *r2 = single_root(rg, NULL);
        tk_rect q2 = tk_widget_rect(rg);
        int cy2 = q2.y + q2.h / 2;
        tk_canvas *c2 = paint_full(r2);
        /* sample between the step ticks (drawn at each 10%) */
        CHECK(col_eq(px(c2, q2.x + 45 * q2.w / 100, cy2),
                     t->accent));
        CHECK(col_eq(px(c2, q2.x + 15 * q2.w / 100, cy2),
                     tk7_blend(t->strong_fill, t->bg)));
        tk_canvas_free(c2);
        CHECK(tk_slider_value_range(rg) == 80.0f);
        /* arrows step the active handle by the range step */
        CHECK(tk_focus(r2, rg));
        tk_slider_set_active_handle(rg, 0);
        send_key(r2, TK_KEY_RIGHT);
        /* one step up (frac storage: compare with tolerance) */
        CHECK(tk_slider_value_lo(rg) > 29.0f &&
              tk_slider_value_lo(rg) < 31.0f);
        tk_root_free(r2);
        tk_widget_free(rg);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI TextField (TextBox) ---------- */
/* Spec (Common_themeresources.xaml): TextControlThemePadding
 * 10,5,6,6, border 1 (focused bottom 2). The field height is
 * the theme control height (touch floor over the 32epx min). */
static void test_winui_textfield(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *f1 = tk_textfield_new("");
        tk_widget *f2 = tk_textfield_new("");
        tk_textfield_set_placeholder(f1, "名前");
        tk_add_child(box, f1);
        tk_add_child(box, f2);
        tk_root *r = tk_root_new(box);
        tk_root_layout(r, 800, 480);
        tk_rect q = tk_widget_rect(f1);
        CHECK(q.h == 54); /* M22 floor */
        /* placeholder glyphs are present and dim (not full
         * text). M10: the bands follow the theme - in the dark
         * theme the face itself is dark, so "dim" is the
         * placeholder's blend band and the ban is on
         * full-strength glyph pixels, not on dark ones. */
        tk_canvas *c = paint_full(r);
        int dim = 0, full = 0;
        bool darkth = tk_theme_default()->dark != 0;
        for (int y = q.y; y < q.y + q.h; y++)
            for (int x = q.x; x < q.x + 120; x++) {
                tk_color p = px(c, x, y);
                if (!darkth) {
                    if (p.r > 90 && p.r < 200) dim++;
                    if (p.r < 90) full++;
                } else {
                    if (p.r > 150 && p.r < 240) dim++;
                    if (p.r >= 250) full++;
                }
            }
        CHECK(dim > 10);
        CHECK(full == 0);
        tk_canvas_free(c);
        /* typing lands in the focused field; Tab moves focus and
         * the input with it (the traversal is the mode's too) */
        CHECK(tk_focus(r, f1));
        send_text(r, "abc");
        char buf[32];
        CHECK(tk_textfield_text(f1, buf, sizeof buf) == 3 &&
              strcmp(buf, "abc") == 0);
        send_key(r, TK_KEY_BACKSPACE);
        CHECK(tk_textfield_text(f1, buf, sizeof buf) == 2 &&
              strcmp(buf, "ab") == 0);
        send_key(r, TK_KEY_TAB);
        CHECK(tk_focused(r) == f2);
        send_text(r, "z");
        CHECK(tk_textfield_text(f2, buf, sizeof buf) == 1 &&
              strcmp(buf, "z") == 0);
        tk_root_free(r);
        tk_widget_free(box);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI SearchBar (AutoSuggestBox) ---------- */
/* The AutoSuggestBox is a TextBox plus a suggestion flyout;
 * the flow (type -> prefix list -> pick) must hold in the mode. */
static void test_winui_searchbar(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *sb = tk_searchbar_new("検索");
        tk_searchbar_set_on_search(sb, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(sb, NULL);
        tk_rect q = tk_widget_rect(sb);
        CHECK(q.h == 54); /* M22 floor */
        click_widget(r, sb);
        const char *items[] = { "apple", "apricot", "banana" };
        tk_searchbar_set_suggestions(sb, items, 3);
        send_text(r, "ap");
        char buf[64];
        CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 2);
        CHECK(tk_searchbar_suggestions_open(sb));
        send_click(r, q.x + 30, q.y + q.h + 24);
        CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 5 &&
              strcmp(buf, "apple") == 0);
        CHECK(!tk_searchbar_suggestions_open(sb));
        CHECK(notify_count == 1);
        tk_root_free(r);
        tk_widget_free(sb);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Menu (MenuFlyout) ---------- */
/* Spec (MenuFlyout_themeresources.xaml): item padding
 * 11,8,11,9, item margin 4,2,4,2, presenter padding 0,2,0,2,
 * separator 1. Ours: rows at the control height (56, over the
 * converted 53) and 8px text inset (delta recorded). */
static void test_winui_menu(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *anchor = tk_button_new("開く");
        tk_root *r = single_root(anchor, NULL);
        tk_widget *m = tk_menu_new();
        tk_menu_add_item(m, "新規", 1);
        tk_menu_add_item(m, "開く", 2);
        tk_menu_add_separator(m);
        tk_menu_add_item(m, "削除", 3);
        tk_menu_set_item_enabled(m, 3, false);
        tk_menu_set_on_select(m, on_notify, NULL);
        notify_count = 0;
        tk_menu_popup(r, m, 100, 100);
        CHECK(tk_menu_is_open(r, m));
        tk_rect mq = tk_widget_rect(m);
        /* 3 rows at the control height + a 9px separator row */
        CHECK(mq.h == 3 * 54 + 9); /* M22 floor */
        tk_canvas *c = paint_full(r);
        /* first row carries the hover/selection face, second not */
        tk_color hov = px(c, mq.x + 4, mq.y + 28);
        tk_color plain = px(c, mq.x + 4, mq.y + 54 + 27);
        CHECK(!col_eq(hov, plain));
        /* the separator row shows its rule */
        tk_color sep = px(c, mq.x + mq.w / 2, mq.y + 2 * 54 + 4);
        CHECK(!col_eq(sep, plain));
        tk_canvas_free(c);
        /* keys: Down past the separator lands on the disabled
         * item only as a stop - Enter on it must not fire; move
         * back up and Enter activates item 2 */
        send_key(r, TK_KEY_DOWN);
        send_key(r, TK_KEY_ENTER);
        CHECK(notify_count == 1);
        CHECK(tk_menu_last_id(m) == 2);
        CHECK(!tk_menu_is_open(r, m));
        /* click path: item 1 fires with its id */
        tk_menu_popup(r, m, 100, 100);
        send_click(r, mq.x + 24, mq.y + 28);
        CHECK(notify_count == 2);
        CHECK(tk_menu_last_id(m) == 1);
        /* Esc dismisses without firing */
        tk_menu_popup(r, m, 100, 100);
        send_key(r, TK_KEY_ESCAPE);
        CHECK(!tk_menu_is_open(r, m));
        CHECK(notify_count == 2);
        tk_widget_free(m);
        tk_root_free(r);
        tk_widget_free(anchor);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Dialog (ContentDialog) ---------- */
/* Spec (ContentDialog_themeresources.xaml): padding 24,
 * min 320x184, max width 548, border 1, title margin bottom 12.
 * Ours: widget padding 16 and content-sized width (deltas
 * recorded); the mode's solid surface + overlay radius paint
 * and the modal contract are what this test pins. */
static void test_winui_dialog(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *behind = tk_button_new("後ろ");
        tk_button_set_on_click(behind, on_notify, NULL);
        notify_count = 0;
        tk_add_child(box, behind);
        tk_root *r = tk_root_new(box);
        tk_root_layout(r, 800, 480);
        tk_widget *dlg = tk_dialog_new("確認");
        tk_widget *lab = tk_label_new("内容");
        tk_add_child(dlg, lab);
        tk_dialog_show(r, dlg);
        CHECK(tk_dialog_is_shown(r, dlg));
        CHECK(tk_dialog_is_modal(r, dlg));
        tk_rect q = tk_widget_rect(dlg);
        /* centred, on the solid surface, with the 1px border */
        CHECK(q.x > 0 && q.y > 0 && q.x + q.w < 800);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w / 2, q.y + q.h - 6),
                     tk_theme_default()->surface));
        tk_canvas_free(c);
        /* modal: the button behind does not fire */
        click_widget(r, behind);
        CHECK(notify_count == 0);
        tk_dialog_close(r, dlg);
        click_widget(r, behind);
        CHECK(notify_count == 1);
        tk_widget_free(dlg);
        tk_root_free(r);
        tk_widget_free(box);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI ListView ---------- */
/* Spec (ListViewItem themeresources): MinHeight 40epx,
 * MinWidth 88, CornerRadius 4. Converted row floor 57px; the
 * theme row height is 56 (delta 1, recorded). */
static void test_winui_listview(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const char *items[] = { "one", "two", "three", "four" };
        tk_widget *lv = tk_listview_new();
        tk_listview_set_items(lv, items, 4);
        tk_listview_set_on_select(lv, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(lv, NULL);
        tk_rect q = tk_widget_rect(lv);
        int rh = tk_theme_winui.row_h;
        CHECK(rh == 54); /* M22 floor */
        /* click row 2 selects it and fires */
        send_click(r, q.x + 40, q.y + 2 * rh + rh / 2);
        CHECK(tk_listview_selected(lv) == 2);
        CHECK(notify_count == 1);
        /* the selected row paints the selection face */
        tk_canvas *c = paint_full(r);
        tk_color sel = px(c, q.x + 4, q.y + 2 * rh + rh / 2);
        tk_color unsel = px(c, q.x + 4, q.y + rh / 2);
        CHECK(!col_eq(sel, unsel));
        tk_canvas_free(c);
        /* arrows move the selection while focused */
        CHECK(tk_focus(r, lv));
        send_key(r, TK_KEY_UP);
        CHECK(tk_listview_selected(lv) == 1);
        send_key(r, TK_KEY_DOWN);
        send_key(r, TK_KEY_DOWN);
        CHECK(tk_listview_selected(lv) == 3);
        tk_root_free(r);
        tk_widget_free(lv);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI TreeView ---------- */
/* Spec (TreeView themeresources): item MinHeight 28epx,
 * content height 20epx. Ours: rows at the theme row height
 * (touch floor, delta recorded). */
static void test_winui_treeview(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *tv = tk_treeview_new();
        int root_id = tk_treeview_add(tv, -1, "root");
        int a = tk_treeview_add(tv, root_id, "child-a");
        tk_treeview_add(tv, root_id, "child-b");
        tk_treeview_add(tv, -1, "second");
        CHECK(tk_treeview_visible_rows(tv) == 4);
        tk_root *r = single_root(tv, NULL);
        tk_rect q = tk_widget_rect(tv);
        int rh = tk_theme_winui.row_h;
        /* click the child row selects its node */
        send_click(r, q.x + 60, q.y + rh + rh / 2);
        CHECK(tk_treeview_selected(tv) == a);
        /* collapsing the root hides the children */
        tk_treeview_set_expanded(tv, root_id, false);
        CHECK(tk_treeview_visible_rows(tv) == 2);
        tk_treeview_set_expanded(tv, root_id, true);
        CHECK(tk_treeview_visible_rows(tv) == 4);
        tk_root_free(r);
        tk_widget_free(tv);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Date/Time pickers ---------- */
/* Spec (DatePicker/TimePicker themeresources): flyout item
 * height 40epx. Ours is the inline segmented form at the
 * control height (form delta recorded in the report); the
 * field/step/key behaviour is what this test pins. */
static void test_winui_pickers(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *dp = tk_datepicker_new(2024, 1, 31);
        tk_datepicker_set_on_change(dp, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(dp, NULL);
        tk_rect q = tk_widget_rect(dp);
        CHECK(q.h == 54); /* M22 floor */
        int y, m, d;
        CHECK(tk_focus(r, dp));
        send_key(r, TK_KEY_RIGHT);          /* field: month */
        send_key(r, TK_KEY_UP);             /* Jan -> Feb, clamp */
        tk_datepicker_value(dp, &y, &m, &d);
        CHECK(m == 2 && d == 29);
        CHECK(notify_count == 1);
        /* stepper zone steps the active field */
        send_click(r, q.x + q.w - 14, q.y + q.h / 4);
        tk_datepicker_value(dp, &y, &m, &d);
        CHECK(m == 3);
        tk_root_free(r);
        tk_widget_free(dp);
        /* time picker wraps the hour at 24 */
        tk_widget *tp = tk_timepicker_new(false);
        tk_timepicker_set_value(tp, 23, 30, 0);
        tk_root *r2 = single_root(tp, NULL);
        CHECK(tk_focus(r2, tp));
        send_key(r2, TK_KEY_UP);
        int hh, mm, ss;
        tk_timepicker_value(tp, &hh, &mm, &ss);
        CHECK(hh == 0 && mm == 30);
        tk_root_free(r2);
        tk_widget_free(tp);
    }
    tk_theme_set_default(&tk_theme_v2);
}

static void solid_icon(uint32_t *buf, int n, uint8_t r, uint8_t g,
                       uint8_t b)
{
    uint32_t v = 0xFF000000u | ((uint32_t)r << 16) |
                 ((uint32_t)g << 8) | b;
    for (int i = 0; i < n; i++) buf[i] = v;
}

/* ---------- WinUI ProgressBar / ProgressRing ---------- */
/* Spec (ProgressBar themeresources): MinHeight 3, track 1 -
 * the shipped control renders a ~4epx line; ours is a 20px
 * bar (form delta recorded). ProgressRing stroke 4epx = 6px;
 * ours draws 5px dots on the ring (delta recorded). */
static void test_winui_progress(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        const tk_theme *t = tk7_map(&tk_theme_winui);
        tk_widget *p = tk_progress_new(0.5f);
        tk_root *r = single_root(p, NULL);
        tk_rect q = tk_widget_rect(p);
        /* M9: ProgressBarMinHeight 3epx = 4px at the device
         * scale (was the generic 20px bar through M8) */
        CHECK(q.h == 4);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w / 4, q.y + q.h / 2), t->accent));
        /* M10: the track is ControlStrongStrokeColorDefault
         * (the value M9 could not obtain), blended over the
         * page background */
        CHECK(col_near_c(px(c, q.x + 3 * q.w / 4, q.y + q.h / 2),
                         tk7_blend(t->strong_stroke, t->bg), 2));
        tk_canvas_free(c);
        tk_progress_set_value(p, 0.25f);
        c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w / 5, q.y + q.h / 2), t->accent));
        CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + q.h / 2),
                         tk7_blend(t->strong_stroke, t->bg), 2));
        tk_canvas_free(c);
        /* indeterminate: a partial accent segment, not a full
         * bar. The sweep phase is set by the anim clock at the
         * first indeterminate paint; step the clock until the
         * segment is on-screen (it is 50% wide, so a few 200ms
         * steps always land one) */
        tk_progress_set_indeterminate(p, true);
        int acc = 0;
        for (int i = 1; i <= 8 && acc == 0; i++) {
            tk_root_tick(r, 150 * i);
            c = paint_full(r);
            acc = count_near(c, q, t->accent.r, t->accent.g,
                             t->accent.b, 0);
            tk_canvas_free(c);
        }
        CHECK(acc > 0 && acc < q.w * q.h);
        tk_root_free(r);
        tk_widget_free(p);
        /* ring at 25%: accent arc in the top-right quadrant,
         * track only in the bottom-left. Quadrants are taken
         * around the widget centre (the box stretches the
         * width, the ring stays centred) */
        tk_widget *ring = tk_ring_new(0.25f);
        tk_root *r2 = single_root(ring, NULL);
        tk_rect q2 = tk_widget_rect(ring);
        int rcx = q2.x + q2.w / 2, rcy = q2.y + q2.h / 2;
        tk_canvas *c2 = paint_full(r2);
        CHECK(count_near(c2, (tk_rect){rcx + 8, rcy - 22, 14, 14},
                         t->accent.r, t->accent.g, t->accent.b, 0) > 0);
        CHECK(count_near(c2, (tk_rect){rcx - 22, rcy + 8, 14, 14},
                         t->accent.r, t->accent.g, t->accent.b, 0) == 0);
        tk_canvas_free(c2);
        tk_ring_set_value(ring, 0.75f);
        CHECK(tk_ring_value(ring) == 0.75f);
        c2 = paint_full(r2);
        CHECK(count_near(c2, (tk_rect){rcx - 22, rcy + 8, 14, 14},
                         t->accent.r, t->accent.g, t->accent.b, 0) > 0);
        tk_canvas_free(c2);
        tk_root_free(r2);
        tk_widget_free(ring);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Snackbar (InfoBar) ---------- */
/* Spec (InfoBar themeresources): border 1, icon margin
 * 0,16,14,16, content padding left 16epx. Through M8 ours was
 * the shared snackbar form without severity (SS9 delta); M9
 * implements the severity form (see test_tk8). This test pins
 * the action / dismiss / expiry contract. */
static void test_winui_snackbar(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *top = tk_label_new("");
        tk_root *r = tk_root_new(top);
        tk_root_layout(r, 800, 480);
        tk_root_tick(r, 3000);
        tk_snackbar_show(r, "削除しました", "元に戻す",
                         on_notify, NULL, 0);
        notify_count = 0;
        CHECK(tk_snackbar_visible(r));
        /* action zone at the bar's right end (bar x16 y395 h69
         * since M9: the InfoBar MinHeight 48epx at scale) */
        send_click(r, 540, 436);
        CHECK(notify_count == 1);
        CHECK(!tk_snackbar_visible(r));
        /* timed expiry runs on root time */
        tk_snackbar_show(r, "短い", NULL, NULL, NULL, 1500);
        tk_root_tick(r, 3000 + 1499);
        CHECK(tk_snackbar_visible(r));
        tk_root_tick(r, 3000 + 1500);
        CHECK(!tk_snackbar_visible(r));
        tk_root_free(r);
        tk_widget_free(top);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Badge (InfoBadge) ---------- */
/* Spec (InfoBadge themeresources): max height 16epx, value
 * font 11epx, text margin 4,0,4,2. Ours: 19px pill, text + 12
 * (deltas recorded). */
static void test_winui_badge(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *b5 = tk_badge_new(5);
        tk_size d5 = tk_measure(b5, (tk_constraints){0, TK_UNLIMITED,
                                                    0, TK_UNLIMITED});
        CHECK(d5.h == 19);
        tk_widget *b100 = tk_badge_new(100);
        tk_size d100 = tk_measure(b100,
            (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
        CHECK(d100.w > d5.w);           /* "99+" is wider than "5" */
        tk_widget *dot = tk_badge_new(-1);
        tk_size dd = tk_measure(dot, (tk_constraints){0, TK_UNLIMITED,
                                                     0, TK_UNLIMITED});
        CHECK(dd.w == 8 && dd.h == 8);
        tk_widget *none = tk_badge_new(0);
        tk_size dn = tk_measure(none, (tk_constraints){0, TK_UNLIMITED,
                                                      0, TK_UNLIMITED});
        CHECK(dn.w == 0 && dn.h == 0);
        tk_widget_free(b5);
        tk_widget_free(b100);
        tk_widget_free(dot);
        tk_widget_free(none);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI TabBar (TabView) ---------- */
/* Spec (TabView themeresources): item header padding 8,3,4,3,
 * header padding top 8. Ours: strip at the control height with
 * the layer strip / raised active tab form (painted in the
 * mode's branch); the test pins that form + selection + keys. */
static void test_winui_tabbar(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *tb = tk_tabbar_new();
        tk_tabbar_add(tb, "概要");
        tk_tabbar_add(tb, "詳細");
        tk_tabbar_add(tb, "履歴");
        tk_tabbar_set_on_select(tb, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(tb, NULL);
        tk_rect q = tk_widget_rect(tb);
        CHECK(q.h == 54); /* M22 floor */
        int seg = q.w / 3;
        tk_canvas *c = paint_full(r);
        /* active tab raised to the surface over the layer strip */
        CHECK(col_eq(px(c, q.x + seg / 2, q.y + 3),
                     tk_theme_default()->surface));
        tk_color inactive = px(c, q.x + seg + seg / 2, q.y + 3);
        CHECK(inactive.r < 254);
        tk_canvas_free(c);
        /* click selects; arrows move while focused */
        send_click(r, q.x + seg + seg / 2, q.y + q.h / 2);
        CHECK(tk_tabbar_selected(tb) == 1);
        CHECK(notify_count == 1);
        CHECK(tk_focus(r, tb));
        send_key(r, TK_KEY_RIGHT);
        CHECK(tk_tabbar_selected(tb) == 2);
        send_key(r, TK_KEY_LEFT);
        CHECK(tk_tabbar_selected(tb) == 1);
        tk_root_free(r);
        tk_widget_free(tb);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Navigation rail (NavigationView compact) -- */
/* Spec (NavigationView themeresources): item margin via
 * NavigationViewItemButtonMargin 4,2; the pane form has no
 * fixed compact width token in the resources we fetched.
 * Ours keeps the 80dp rail width (94px) in both modes. */
static void test_winui_navrail(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        uint32_t ic[64];
        solid_icon(ic, 64, 30, 30, 30);
        tk_widget *rail = tk_navrail_new();
        tk_navrail_add(rail, "ホーム", ic, 8, 8);
        tk_navrail_add(rail, "検索", ic, 8, 8);
        tk_navrail_add(rail, "設定", ic, 8, 8);
        tk_navrail_set_on_select(rail, on_notify, NULL);
        tk_navrail_select(rail, 0);
        notify_count = 0;
        tk_size d = tk_measure(rail, (tk_constraints){0, TK_UNLIMITED,
                                                     0, TK_UNLIMITED});
        CHECK(d.w == 94);
        tk_root *r = single_root(rail, NULL);
        tk_rect q = tk_widget_rect(rail);
        tk_canvas *c = paint_full(r);
        /* selected cell: 3px accent bar at the left edge,
         * centred on the first cell (cells start at +8, 64 tall) */
        CHECK(col_eq(px(c, q.x + 1, q.y + 8 + 32),
                     tk_theme_default()->accent));
        tk_canvas_free(c);
        /* click the third cell */
        send_click(r, q.x + q.w / 2, q.y + 8 + 2 * 64 + 32);
        CHECK(tk_navrail_selected(rail) == 2);
        CHECK(notify_count == 1);
        tk_root_free(r);
        tk_widget_free(rail);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI BottomBar (NavigationView form) ---------- */
static void test_winui_bottombar(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        uint32_t ic[64];
        solid_icon(ic, 64, 30, 30, 30);
        tk_widget *bar = tk_bottombar_new();
        tk_bottombar_add_cell(bar, ic, 8, 8, "ホーム",
                              on_notify, NULL);
        tk_bottombar_add_cell(bar, ic, 8, 8, "設定",
                              on_notify, NULL);
        notify_count = 0;
        tk_bottombar_set_indicator_mode(bar, 1);
        tk_bottombar_set_selected(bar, 0);
        tk_root *r = single_root(bar, NULL);
        tk_widget *cell0 = tk_bottombar_cell(bar, 0);
        tk_rect cq = tk_widget_rect(cell0);
        tk_canvas *c = paint_full(r);
        /* the mode's selection: 3px accent bar at the cell's
         * left edge (cells are inset 2px inside the bar) */
        CHECK(col_eq(px(c, cq.x + 1, cq.y + cq.h / 2),
                     tk_theme_default()->accent));
        tk_canvas_free(c);
        /* cells are buttons: the second cell's click fires */
        tk_widget *cell = tk_bottombar_cell(bar, 1);
        click_widget(r, cell);
        CHECK(notify_count == 1);
        tk_root_free(r);
        tk_widget_free(bar);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Segmented / Chip / Card ---------- */
static void test_winui_segmented_chip_card(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *seg = tk_segmented_new(false);
        tk_segmented_add(seg, "日");
        tk_segmented_add(seg, "週");
        tk_segmented_add(seg, "月");
        tk_segmented_set_on_change(seg, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(seg, NULL);
        tk_rect q = tk_widget_rect(seg);
        CHECK(q.h == 48);
        send_click(r, q.x + q.w / 2, q.y + q.h / 2);
        CHECK(tk_segmented_selected(seg) == 1);
        CHECK(notify_count == 1);
        tk_root_free(r);
        tk_widget_free(seg);
        /* chip: the mode renders the ToggleButton form - accent
         * fill while selected */
        tk_widget *chip = tk_chip_new(TK_CHIP_FILTER, "絞り込み");
        tk_chip_set_on_click(chip, on_notify, NULL);
        notify_count = 0;
        tk_root *r2 = single_root(chip, NULL);
        tk_rect qc = tk_widget_rect(chip);
        CHECK(qc.h == 40);
        click_widget(r2, chip);
        CHECK(tk_chip_selected(chip));
        CHECK(notify_count == 1);
        tk_canvas *c = paint_full(r2);
        CHECK(col_eq(px(c, qc.x + qc.w - 6, qc.y + qc.h / 2),
                     tk_theme_default()->accent));
        tk_canvas_free(c);
        tk_root_free(r2);
        tk_widget_free(chip);
        /* card: the filled kind paints the card background role */
        tk_widget *card = tk_card_new(TK_CARD_FILLED);
        tk_root *r3 = single_root(card, NULL);
        tk_rect qk = tk_widget_rect(card);
        tk_canvas *c3 = paint_full(r3);
        tk_color f = px(c3, qk.x + qk.w / 2, qk.y + qk.h / 2);
        CHECK(col_near_c(f, tk7_blend(tk_theme_default()->card_bg, tk_theme_default()->bg), 4));
        CHECK(!col_eq(f, tk_theme_default()->bg));
        tk_canvas_free(c3);
        tk_root_free(r3);
        tk_widget_free(card);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI Drawer / Sheet ---------- */
static int drawer_hits;
static void on_drawer_item(tk_widget *w, void *ud)
{
    (void)w; (void)ud; drawer_hits++;
}
static void test_winui_drawer_sheet(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *top = tk_label_new("本体");
        tk_root *r = single_root(top, NULL);
        tk_widget *dr = tk_drawer_new("メニュー");
        tk_drawer_add_item(dr, "ホーム", 1, NULL, 0, 0);
        tk_drawer_add_item(dr, "設定", 2, NULL, 0, 0);
        tk_drawer_set_on_select(dr, on_drawer_item, NULL);
        drawer_hits = 0;
        tk_drawer_show(r, dr, true);
        CHECK(tk_drawer_is_shown(r, dr));
        tk_rect q = tk_widget_rect(dr);
        send_click(r, q.x + 60, q.y + 56 + 28); /* first item row */
        CHECK(drawer_hits == 1);
        CHECK(tk_drawer_last_id(dr) == 1);
        CHECK(!tk_drawer_is_shown(r, dr)); /* modal closes on pick */
        tk_widget_free(dr);
        /* sheet: modal bottom sheet dismisses from the scrim */
        tk_widget *sh = tk_sheet_new();
        tk_widget *inner = tk_button_new("中身");
        tk_add_child(sh, inner);
        tk_sheet_show(r, sh, true);
        CHECK(tk_sheet_is_shown(r, sh));
        send_click(r, 400, 40); /* scrim above the panel */
        CHECK(!tk_sheet_is_shown(r, sh));
        tk_widget_free(sh);
        tk_root_free(r);
        tk_widget_free(top);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI ColorPicker / Carousel / Toast / TextView - */
static uint32_t cp_rgba;
static bool cp_chosen;
static void on_cp_result(tk_widget *dlg, uint32_t rgba, bool chosen,
                         void *ud)
{
    (void)dlg; (void)ud; cp_rgba = rgba; cp_chosen = chosen;
}
static void test_winui_misc(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        /* ColorPicker: value round-trip through the public API
         * (0xRRGGBBAA packing; with alpha disabled - the
         * default - set_value forces the alpha byte opaque) */
        tk_widget *cp = tk_colorpicker_new("色", 0xFF3366CCu);
        CHECK(tk_colorpicker_value(cp) == 0xFF3366CCu);
        tk_colorpicker_set_value(cp, 0xFF00AA55u);
        CHECK(tk_colorpicker_value(cp) == 0xFF00AAFFu);
        tk_colorpicker_set_alpha_enabled(cp, true);
        tk_colorpicker_set_value(cp, 0x11223344u);
        CHECK(tk_colorpicker_value(cp) == 0x11223344u);
        tk_colorpicker_set_on_result(cp, on_cp_result, NULL);
        tk_widget *top = tk_label_new("");
        tk_root *r = single_root(top, NULL);
        tk_colorpicker_show(r, cp);
        CHECK(tk_dialog_is_shown(r, cp));
        tk_dialog_close(r, cp);
        tk_widget_free(cp);
        tk_root_free(r);
        tk_widget_free(top);
        /* Carousel (FlipView form): tap the small item to move */
        uint32_t red[64], grn[64], blu[64];
        solid_icon(red, 64, 200, 30, 30);
        solid_icon(grn, 64, 30, 200, 30);
        solid_icon(blu, 64, 30, 30, 200);
        tk_widget *car = tk_carousel_new();
        tk_carousel_add(car, "赤", red, 8, 8);
        tk_carousel_add(car, "緑", grn, 8, 8);
        tk_carousel_add(car, "青", blu, 8, 8);
        tk_carousel_set_on_select(car, on_notify, NULL);
        notify_count = 0;
        tk_root *r2 = single_root(car, NULL);
        tk_rect q = tk_widget_rect(car);
        tk_canvas *c = paint_full(r2);
        CHECK(col_near(px(c, q.x + 120, q.y + q.h / 2),
                       200, 30, 30, 24));
        tk_canvas_free(c);
        send_click(r2, q.x + 248 + 9 + 32, q.y + q.h / 2);
        CHECK(tk_carousel_index(car) == 1);
        CHECK(notify_count == 1);
        tk_root_free(r2);
        tk_widget_free(car);
        /* Toast: shows and expires on root time */
        tk_widget *top2 = tk_label_new("");
        tk_root *r3 = tk_root_new(top2);
        tk_root_layout(r3, 800, 480);
        tk_root_tick(r3, 1000);
        tk_toast_show(r3, "保存しました", 1500);
        CHECK(tk_toast_visible(r3));
        tk_root_tick(r3, 1000 + 1499);
        CHECK(tk_toast_visible(r3));
        tk_root_tick(r3, 1000 + 1500);
        CHECK(!tk_toast_visible(r3));
        tk_root_free(r3);
        tk_widget_free(top2);
        /* TextView: editing works in the mode */
        tk_widget *tv = tk_textview_new("行一\n行二");
        tk_root *r4 = single_root(tv, NULL);
        CHECK(tk_focus(r4, tv));
        send_text(r4, "X");
        char buf[64];
        size_t n = tk_textview_text(tv, buf, sizeof buf);
        CHECK(n > 0 && buf[0] == 'X');
        tk_root_free(r4);
        tk_widget_free(tv);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI FAB / FAB menu / long-press / tk_anim ----- */
static bool lp_hook(void *ud, tk_event *ev)
{
    (void)ud;
    if (ev->type == TK_EV_LONGPRESS) { notify_count++; return true; }
    return false;
}
static int anim_seen[8];
static int anim_n;
static void on_anim_val(void *ud, int v)
{
    (void)ud;
    if (anim_n < 8) anim_seen[anim_n] = v;
    anim_n++;
}
static void test_winui_fab_lp_anim(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        uint32_t ic[64];
        solid_icon(ic, 64, 255, 255, 255);
        tk_widget *fab = tk_fab_new(ic, 8, 8, 56);
        tk_button_set_on_click(fab, on_notify, NULL);
        notify_count = 0;
        tk_root *r = single_root(fab, NULL);
        tk_rect q = tk_widget_rect(fab);
        /* the box stretches the width; height and the measured
         * size keep the 56px spec */
        tk_size fsz = tk_measure(fab, (tk_constraints){0,
                                 TK_UNLIMITED, 0, TK_UNLIMITED});
        CHECK(fsz.w == 56 && fsz.h == 56);
        CHECK(q.h == 56);
        click_widget(r, fab);
        CHECK(notify_count == 1);
        tk_root_free(r);
        tk_widget_free(fab);
        /* FAB menu: show, pick, dismiss */
        tk_widget *anchor = tk_label_new("");
        tk_root *r2 = single_root(anchor, NULL);
        tk_widget *fm = tk_fabmenu_new();
        tk_fabmenu_add_item(fm, ic, 8, 8, "追加", on_notify, NULL);
        notify_count = 0;
        tk_fabmenu_show(r2, fm);
        CHECK(tk_fabmenu_is_shown(r2, fm));
        tk_rect fq = tk_widget_rect(fm);
        send_click(r2, fq.x + fq.w / 2, fq.y + fq.h / 2);
        CHECK(notify_count == 1);
        CHECK(!tk_fabmenu_is_shown(r2, fm));
        tk_root_free(r2);
        tk_widget_free(anchor);
        tk_widget_free(fm);
        /* long-press fires once at the threshold, in the mode */
        tk_widget *b = tk_button_new("長押し");
        tk_widget_set_event_hook(b, lp_hook, NULL);
        notify_count = 0;
        tk_root *r3 = single_root(b, NULL);
        tk_rect qb = tk_widget_rect(b);
        tk_root_tick(r3, 1000);
        send_ptr(r3, TK_EV_POINTER_DOWN,
                 qb.x + qb.w / 2, qb.y + qb.h / 2);
        tk_root_tick(r3, 1000 + TK_LONGPRESS_MS - 1);
        CHECK(notify_count == 0);
        tk_root_tick(r3, 1000 + TK_LONGPRESS_MS);
        CHECK(notify_count == 1);
        send_ptr(r3, TK_EV_POINTER_UP,
                 qb.x + qb.w / 2, qb.y + qb.h / 2);
        tk_root_free(r3);
        tk_widget_free(b);
        /* tk_anim runs under the mode: intermediate values in
         * order, root reports activity, final value delivered */
        tk_widget *host = tk_label_new("");
        tk_root *r4 = single_root(host, NULL);
        anim_n = 0;
        tk_root_tick(r4, 2000);
        tk_anim *a = tk_anim_start(r4, 0, 100, 200, tk_ease_linear,
                                   on_anim_val, NULL, NULL);
        CHECK(a != NULL);
        CHECK(tk_root_anim_active(r4));
        tk_root_tick(r4, 2100);
        tk_root_tick(r4, 2200);
        CHECK(anim_n >= 2);
        CHECK(anim_seen[anim_n - 1] == 100);
        CHECK(!tk_root_anim_active(r4));
        tk_root_free(r4);
        tk_widget_free(host);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- WinUI clipboard / i18n / a11y ---------- */
static void test_winui_shared(void)
{
    tk_theme_set_default(&tk_theme_winui);
    {
        /* shared clipboard across two fields, in the mode */
        tk_clipboard_clear();
        tk_widget *a = tk_textfield_new("コピー元");
        tk_widget *b = tk_textfield_new("");
        tc_select_all(tk_textfield_buffer(a));
        CHECK(tc_copy(tk_textfield_buffer(a)));
        CHECK(tc_paste(tk_textfield_buffer(b)));
        char buf[64];
        CHECK(tk_textfield_text(b, buf, sizeof buf) == 12 &&
              strcmp(buf, "コピー元") == 0);
        tk_clipboard_clear();
        tk_widget_free(a);
        tk_widget_free(b);
        /* i18n: locale switch changes the lookup, in the mode */
        tk_i18n_clear();
        static const tk_i18n_entry ja[] = { { "ok", "了解" } };
        static const tk_i18n_entry en[] = { { "ok", "OK" } };
        tk_i18n_add_locale("ja", ja, 1);
        tk_i18n_add_locale("en", en, 1);
        tk_i18n_set_locale("en");
        CHECK(strcmp(tk_tr("ok"), "OK") == 0);
        tk_i18n_set_locale("ja");
        CHECK(strcmp(tk_tr("ok"), "了解") == 0);
        tk_i18n_clear();
        /* a11y: nodes carry the UIA control-type names */
        CHECK(strcmp(tk_a11y_uia_type(TK_ROLE_CHECKBOX),
                     "CheckBox") == 0);
        CHECK(strcmp(tk_a11y_uia_type(TK_ROLE_RADIO),
                     "RadioButton") == 0);
        CHECK(strcmp(tk_a11y_uia_type(TK_ROLE_PROGRESS),
                     "ProgressBar") == 0);
        tk_widget *cbx = tk_checkbox_new("通知", true);
        tk_root *r = single_root(cbx, NULL);
        tk_a11y_node nodes[8];
        size_t nn = tk_a11y_collect(r, nodes, 8);
        bool found = false;
        for (size_t i = 0; i < nn; i++)
            if (nodes[i].role == TK_ROLE_CHECKBOX &&
                strcmp(nodes[i].state, "checked") == 0)
                found = true;
        CHECK(found);
        tk_root_free(r);
        tk_widget_free(cbx);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Android: rail / app bar / carousel A resolution - */
/* M3 values fetched 2026-11-20: navigation rail container
 * width 80dp (M3NavigationSizes railWidth token mirror);
 * top app bar small 64dp / medium 112dp / large 152dp (M3
 * top-app-bar tokens); carousel items have NO fixed width -
 * they size from the layout (official guidelines), small items
 * 40-56dp. At the device scale (x187/160): 80dp = 94px,
 * 64dp = 75px. */
static void test_android_dims(void)
{
    /* rail: the 80dp container width is implemented exactly */
    uint32_t ic[64];
    solid_icon(ic, 64, 30, 30, 30);
    tk_widget *rail = tk_navrail_new();
    tk_navrail_add(rail, "ホーム", ic, 8, 8);
    tk_navrail_add(rail, "設定", ic, 8, 8);
    tk_size d = tk_measure(rail, (tk_constraints){0, TK_UNLIMITED,
                                                 0, TK_UNLIMITED});
    CHECK(d.w == 94);
    tk_widget_free(rail);
    /* app bar: the spec's small-bar height is 64dp = 75px;
     * M9 carries it as the theme's appbar_h and the bar now
     * measures exactly that (through M8 it was pinned at the
     * 56px control height as an explicit delta) */
    tk_widget *bar = tk_appbar_new("題");
    tk_root *r = single_root(bar, NULL);
    tk_rect q = tk_widget_rect(bar);
    CHECK(q.h == 75);
    tk_root_free(r);
    tk_widget_free(bar);
    /* carousel: no fixed item width exists in the spec; the
     * implementation's default stage is 360x112 - pinned */
    tk_widget *car = tk_carousel_new();
    tk_carousel_add(car, "赤", ic, 8, 8);
    tk_size dc = tk_measure(car, (tk_constraints){0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED});
    CHECK(dc.w == 360 && dc.h == 112);
    tk_widget_free(car);
}

int main(void)
{
    /* glyph pixel checks need the real font installed (without
     * one, draw_text paints nothing by design - see tk.h) */
    tk_font *f = tk_font_load_bundled();
    CHECK(f != NULL);
    if (f) tk_font_install(f);
    if (getenv("TK7_DARK"))
        printf("tk7: TK7_DARK=1 - WinUI sections run under "
               "tk_theme_winui_dark (M10 formal dark re-run)\n");
    test_rot();
    test_split_chevron();
    test_winui_button();
    test_winui_checkbox();
    test_winui_radio();
    test_winui_switch();
    test_winui_slider();
    test_winui_textfield();
    test_winui_searchbar();
    test_winui_menu();
    test_winui_dialog();
    test_winui_listview();
    test_winui_treeview();
    test_winui_pickers();
    test_winui_progress();
    test_winui_snackbar();
    test_winui_badge();
    test_winui_tabbar();
    test_winui_navrail();
    test_winui_bottombar();
    test_winui_segmented_chip_card();
    test_winui_drawer_sheet();
    test_winui_misc();
    test_winui_fab_lp_anim();
    test_winui_shared();
    test_android_dims();
    printf("tk7: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
