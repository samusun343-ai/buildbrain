/* test_tk16.c - M17 verification: the D cells closed by the
 * M3 ripple, the WinUI brush transition, the chip /
 * segmented check appearance, the clickable card, and the
 * menu fade + exit; plus the invariance evidence for the
 * WinUI ColorPicker and TreeView.
 *
 * Reference facts relied on here, fetched 2026-11-20:
 * - M3 ripple (androidx material-ripple
 *   RippleAnimation.kt + material3 Ripple.kt): on press a
 *   disc grows from the touch point - radius from 0.3x the
 *   largest dimension to half the diagonal + 10dp (12px at
 *   this panel's scale) over 225 ms with the FastOutSlowIn
 *   cubic-bezier(0.4, 0, 0.2, 1); the centre drifts from the
 *   touch point to the component centre over the same
 *   225 ms (linear); the alpha reaches the pressed
 *   state-layer opacity (10%) over 75 ms (linear). On
 *   release the disc fades out over 150 ms (linear); a
 *   release during the expansion pins the alpha to full,
 *   finishes the expansion, then fades. The disc is
 *   clipped to the component's shape (bounded ripple).
 *   At full expansion the disc covers the whole shape, so
 *   the settled pressed form is the uniform state layer.
 * - WinUI button family (microsoft-ui-xaml main branch,
 *   Button / ToggleButton / AppBarButton
 *   _themeresources.xaml): state changes are discrete
 *   keyframes EXCEPT the ContentPresenter's
 *   BackgroundTransition - a BrushTransition of 83 ms on
 *   the background brush only. Foreground and border
 *   switch instantly.
 * - WinUI ColorPicker (ColorPicker.xaml, full template):
 *   no Storyboard / VisualTransition / BrushTransition /
 *   ThemeTransition anywhere - the D evidence is pixel
 *   stability across a live clock (the M15 #26 precedent).
 * - WinUI TreeView (TreeViewItem.xaml): no state
 *   transitions; the only animation in the template is
 *   the ReorderedPlaceholder's FadeOutThemeAnimation, on
 *   the multi-select drag-reorder path this
 *   implementation does not have. The normal path's D
 *   evidence is the same pixel stability.
 * - M3 menu (material3 Menu.kt): appearance and exit run
 *   scale 0.8<->1.0 + alpha 0<->1. This painter has no
 *   offscreen surface to scale, so only the alpha
 *   component is implemented (AUDIT SS15.2); the fade
 *   duration is the toolkit's small-surface pair, not a
 *   sourced M3 menu value.
 * - WinUI ListView ItemContainerTransitions: the default
 *   collection exists (Microsoft Learn, confirmed in
 *   M16), but its concrete values could not be sourced;
 *   no test here claims that cell.
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
static bool col_eq(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
}
/* source-over with the canvas's own arithmetic (as tk9/10) */
static tk_color blend(tk_color s, tk_color d)
{
    int a = s.a, ia = 255 - a;
    tk_color o;
    o.r = (uint8_t)((s.r * a + d.r * ia + 127) / 255);
    o.g = (uint8_t)((s.g * a + d.g * ia + 127) / 255);
    o.b = (uint8_t)((s.b * a + d.b * ia + 127) / 255);
    o.a = 255;
    return o;
}
static tk_color with_a(tk_color col, int a)
{
    col.a = (uint8_t)a;
    return col;
}
static int count_near(tk_canvas *c, tk_rect q, int rr, int gg,
                      int bb, int tol)
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
static void key(tk_root *r, tk_key k)
{
    tk_event e = { 0 };
    e.type = TK_EV_KEY_DOWN; e.key = k;
    tk_root_event(r, &e);
}
/* ---------- D: the M3 ripple (Android #1/#2/#3) ---------- */
static void test_ripple(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    tk_widget *b = tk_button_new("押下");
    tk_button_set_variant(b, TK_BTN_FILLED);
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    int cx = q.x + 10, cy = q.y + q.h / 2;
    tk_color settled = blend(with_a(t->on_accent, 26),
                             t->accent);
    tk_canvas *c0 = paint_full(r);
    tk_root_tick(r, 1000);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    tk_root_tick(r, 1060); /* mid expansion */
    tk_canvas *c1 = paint_full(r);
    /* the disc has reached the press point... */
    CHECK(!col_eq(px(c1, cx, cy), px(c0, cx, cy)));
    /* ...but not the far edge: the uniform pre-M17 layer
     * would have covered the whole face instantly */
    CHECK(col_eq(px(c1, q.x + q.w - 6, cy),
                 px(c0, q.x + q.w - 6, cy)));
    tk_root_tick(r, 1300); /* expansion complete (225 ms) */
    tk_canvas *c2 = paint_full(r);
    /* the settled pressed form is the uniform state layer,
     * at the far edge and at the press point alike */
    CHECK(col_near_c(px(c2, q.x + q.w - 6, cy), settled, 2));
    CHECK(col_near_c(px(c2, cx, cy), settled, 2));
    /* release: the fade-out runs - the face does not snap
     * back at the release instant */
    send_ptr(r, TK_EV_POINTER_UP, cx, cy);
    tk_canvas *c3 = paint_full(r);
    CHECK(diff_count(c2, c3, q) == 0);
    tk_root_tick(r, 1375); /* mid fade: strictly between */
    tk_canvas *c4 = paint_full(r);
    tk_color mid = blend(with_a(t->on_accent, 13), t->accent);
    CHECK(col_near_c(px(c4, cx, cy), mid, 3));
    tk_root_tick(r, 1500); /* fade complete */
    tk_canvas *c5 = paint_full(r);
    CHECK(col_eq(px(c5, cx, cy), t->accent));
    /* early release: the alpha pins to full and the
     * expansion finishes before the fade */
    tk_root_tick(r, 2000);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    tk_root_tick(r, 2050);
    send_ptr(r, TK_EV_POINTER_UP, cx, cy);
    tk_root_tick(r, 2225); /* expansion completes */
    tk_canvas *c6 = paint_full(r);
    CHECK(col_near_c(px(c6, q.x + q.w - 6, cy), settled, 2));
    tk_root_tick(r, 2400); /* then the fade completes */
    tk_canvas *c7 = paint_full(r);
    CHECK(col_eq(px(c7, cx, cy), t->accent));
    tk_canvas_free(c0); tk_canvas_free(c1); tk_canvas_free(c2);
    tk_canvas_free(c3); tk_canvas_free(c4); tk_canvas_free(c5);
    tk_canvas_free(c6); tk_canvas_free(c7);
    tk_root_free(r);
    tk_widget_free(b);
    /* no live clock: the press lands in the settled form
     * synchronously - pixel-identical to the pre-M17
     * uniform pressed layer */
    tk_widget *b2 = tk_button_new("押下");
    tk_button_set_variant(b2, TK_BTN_FILLED);
    tk_root *r2 = root_of(b2);
    tk_rect q2 = tk_widget_rect(b2);
    send_ptr(r2, TK_EV_POINTER_DOWN, q2.x + q2.w / 2,
             q2.y + q2.h / 2);
    tk_canvas *c8 = paint_full(r2);
    CHECK(col_near_c(px(c8, q2.x + q2.w - 6, q2.y + q2.h / 2),
                     settled, 2));
    CHECK(col_near_c(px(c8, q2.x + 6, q2.y + q2.h / 2),
                     settled, 2));
    tk_canvas_free(c8);
    tk_root_free(r2);
    tk_widget_free(b2);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D: the WinUI brush transition (#1/#2/#3) ----- */
static void test_brush(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    tk_widget *b = tk_button_new("押下");
    tk_button_set_variant(b, TK_BTN_FILLED);
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_canvas *c0 = paint_full(r);
    CHECK(col_near_c(px(c0, q.x + q.w - 6, cy),
                     blend(t->accent, t->bg), 2));
    tk_root_tick(r, 1000);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    /* the brush starts from the rest colour: no jump at the
     * press instant */
    tk_canvas *c1 = paint_full(r);
    CHECK(col_near_c(px(c1, q.x + q.w - 6, cy),
                     blend(t->accent, t->bg), 2));
    tk_root_tick(r, 1041); /* ~half of the 83 ms transition */
    tk_canvas *c2 = paint_full(r);
    tk_color from = blend(t->accent, t->bg);
    tk_color to = blend(with_a(t->accent, 204), t->bg);
    tk_color got = px(c2, q.x + q.w - 6, cy);
    CHECK(!col_near_c(got, from, 4));
    CHECK(!col_near_c(got, to, 4));
    tk_root_tick(r, 1100); /* transition complete */
    tk_canvas *c3 = paint_full(r);
    CHECK(col_near_c(px(c3, q.x + q.w - 6, cy), to, 2));
    /* release: the brush returns over another 83 ms. The
     * transition starts at the first paint after the state
     * change (the face is a paint-time blend), so paint
     * once to start it, then advance past it. */
    send_ptr(r, TK_EV_POINTER_UP, cx, cy);
    tk_canvas *c3b = paint_full(r);
    tk_canvas_free(c3b);
    tk_root_tick(r, 1200);
    tk_canvas *c4 = paint_full(r);
    CHECK(col_near_c(px(c4, q.x + q.w - 6, cy), from, 2));
    tk_canvas_free(c0); tk_canvas_free(c1); tk_canvas_free(c2);
    tk_canvas_free(c3); tk_canvas_free(c4);
    tk_root_free(r);
    tk_widget_free(b);
    tk_theme_set_default(&tk_theme_v2);
}

static void solid_icon(uint32_t *buf, int n, int rr, int gg,
                       int bb)
{
    for (int i = 0; i < n; i++)
        buf[i] = 0xFF000000u | ((uint32_t)rr << 16) |
                 ((uint32_t)gg << 8) | (uint32_t)bb;
}

/* ---------- D: the brush on the toggle + icon faces ------ */
/* WinUI #2 (the toggle's checked face) and #3 (the
 * AppBarButton form) run the same 83 ms BrushTransition;
 * the FILLED flow above covers the shared accent face. */
static void test_brush_faces(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    /* the checked toggle: accent -> Accent Tertiary */
    tk_widget *b = tk_button_new("切替");
    tk_button_set_toggle(b, true);
    tk_button_set_toggled(b, true);
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_color from = blend(t->accent, t->bg);
    tk_color to = blend(with_a(t->accent, 204), t->bg);
    tk_canvas *c0 = paint_full(r); /* the rest paint sets
                                    * the face's initial
                                    * brush state */
    CHECK(col_near_c(px(c0, q.x + q.w - 6, cy), from, 2));
    tk_canvas_free(c0);
    tk_root_tick(r, 1000);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    tk_canvas *c1 = paint_full(r);
    CHECK(col_near_c(px(c1, q.x + q.w - 6, cy), from, 2));
    tk_root_tick(r, 1041);
    tk_canvas *c2 = paint_full(r);
    tk_color got = px(c2, q.x + q.w - 6, cy);
    CHECK(!col_near_c(got, from, 4));
    CHECK(!col_near_c(got, to, 4));
    tk_root_tick(r, 1100);
    tk_canvas *c3 = paint_full(r);
    CHECK(col_near_c(px(c3, q.x + q.w - 6, cy), to, 2));
    tk_canvas_free(c1); tk_canvas_free(c2); tk_canvas_free(c3);
    tk_root_free(r);
    tk_widget_free(b);
    /* the legacy icon-only button (AppBarButton form):
     * transparent -> the subtle fill. The subtle token is
     * alpha 9, too faint for midpoint tolerances, so this
     * leg pins the endpoints and the no-jump start. */
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *ib = tk_button_new("");
    tk_button_set_icon(ib, ic, 24, 24);
    tk_button_set_icon_only(ib, true);
    tk_root *r2 = root_of(ib);
    tk_rect q2 = tk_widget_rect(ib);
    /* the button is 32x32 with the 24x24 icon centred:
     * sample the face above the icon (the mid-left point
     * would land on the icon itself) */
    int ix = q2.x + q2.w / 2, iy = q2.y + 2;
    tk_canvas *d0 = paint_full(r2);
    tk_color rest = px(d0, ix, iy);
    tk_root_tick(r2, 1000);
    send_ptr(r2, TK_EV_POINTER_DOWN, q2.x + q2.w / 2, iy);
    tk_canvas *d1 = paint_full(r2);
    CHECK(col_eq(px(d1, ix, iy), rest)); /* no jump */
    tk_root_tick(r2, 1100);
    tk_canvas *d2 = paint_full(r2);
    CHECK(col_near_c(px(d2, ix, iy),
                     blend(t->subtle, t->bg), 2));
    send_ptr(r2, TK_EV_POINTER_UP, q2.x + q2.w / 2, iy);
    tk_canvas *d2b = paint_full(r2); /* starts the return */
    tk_canvas_free(d2b);
    tk_root_tick(r2, 1200);
    tk_canvas *d3 = paint_full(r2);
    CHECK(col_eq(px(d3, ix, iy), rest));
    tk_canvas_free(d0); tk_canvas_free(d1); tk_canvas_free(d2);
    tk_canvas_free(d3);
    tk_root_free(r2);
    tk_widget_free(ib);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D: the chip check appearance (Android #8) ---- */
static int check_px(tk_canvas *c, tk_rect q, const tk_theme *t)
{
    /* the check strokes sit in the leading slot */
    tk_rect zone = { q.x, q.y, 34, q.h };
    return count_near(c, zone, t->accent.r, t->accent.g,
                      t->accent.b, 30);
}
static void test_chip_check(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
    tk_root *r = root_of(ch);
    tk_rect q = tk_widget_rect(ch);
    tk_canvas *c_un = paint_full(r);
    int n_un = check_px(c_un, q, t);
    tk_chip_set_selected(ch, true); /* no clock: snaps */
    tk_canvas *c_sel = paint_full(r);
    int n_sel = check_px(c_sel, q, t);
    CHECK(n_sel > n_un + 10);
    /* live: deselect fades the check out */
    tk_root_tick(r, 1000);
    tk_chip_set_selected(ch, false);
    tk_root_tick(r, 1100); /* mid fade (200 ms pair) */
    tk_canvas *c_mid = paint_full(r);
    int n_mid = check_px(c_mid, q, t);
    CHECK(n_mid < n_sel);
    tk_root_tick(r, 1300);
    tk_canvas *c_end = paint_full(r);
    CHECK(diff_count(c_un, c_end, q) == 0);
    /* live: select fades it back in to the settled form */
    tk_root_tick(r, 2000);
    tk_chip_set_selected(ch, true);
    tk_root_tick(r, 2300);
    tk_canvas *c_end2 = paint_full(r);
    CHECK(diff_count(c_sel, c_end2, q) == 0);
    tk_canvas_free(c_un); tk_canvas_free(c_sel);
    tk_canvas_free(c_mid); tk_canvas_free(c_end);
    tk_canvas_free(c_end2);
    tk_root_free(r);
    tk_widget_free(ch);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D: the segmented check (Android #33) --------- */
static void test_segmented_check(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "月");
    tk_segmented_add(sg, "火");
    tk_segmented_add(sg, "水");
    tk_segmented_select(sg, 2);
    tk_root *r = root_of(sg);
    tk_rect q = tk_widget_rect(sg);
    int sw = q.w / 3;
    tk_rect seg2 = { q.x + 2 * sw, q.y, sw, q.h };
    tk_canvas *c_set = paint_full(r);
    int n_set = count_near(c_set, seg2, t->text.r, t->text.g,
                           t->text.b, 40);
    tk_segmented_select(sg, 0); /* settled at 0, no clock */
    tk_canvas *c_0 = paint_full(r);
    int n_0 = count_near(c_0, seg2, t->text.r, t->text.g,
                         t->text.b, 40);
    CHECK(n_set > n_0);
    /* live: selecting 2 fades its check in - mid-flight
     * the segment differs from BOTH settled states (the
     * strokes are at partial alpha, neither absent nor
     * settled) */
    tk_root_tick(r, 1000);
    tk_segmented_select(sg, 2);
    tk_root_tick(r, 1100); /* mid fade */
    tk_canvas *c_mid = paint_full(r);
    CHECK(diff_count(c_0, c_mid, seg2) > 10);
    CHECK(diff_count(c_set, c_mid, seg2) > 10);
    tk_root_tick(r, 1400);
    tk_canvas *c_end = paint_full(r);
    CHECK(diff_count(c_set, c_end, q) == 0);
    tk_canvas_free(c_set); tk_canvas_free(c_0);
    tk_canvas_free(c_mid); tk_canvas_free(c_end);
    tk_root_free(r);
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D: the clickable card (Android #16) ---------- */
static int card16_fires;
static void on_card16(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    card16_fires++;
}
static void card_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    card16_fires = 0;
    tk_widget *cd = tk_card_new(TK_CARD_FILLED);
    /* the generic click setter (sets the widget's click
     * callback, as for buttons) makes the card clickable */
    tk_button_set_on_click(cd, on_card16, NULL);
    tk_add_child(cd, tk_label_new("カード"));
    tk_root *r = root_of(cd);
    tk_rect q = tk_widget_rect(cd);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_canvas *c0 = paint_full(r);
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    CHECK(card16_fires == 0); /* fires on release, like a button */
    tk_canvas *c1 = paint_full(r);
    if (t->style == 1) {
        /* the M3 pressed visual: the ripple's settled form
         * (this root has no live clock) - the content
         * colour's state layer over the card face */
        tk_color face = t->container_highest.a
                            ? t->container_highest
                            : t->surface_alt;
        CHECK(col_near_c(px(c1, q.x + q.w - 6, cy),
                         blend(with_a(t->text, 26), face), 2));
    } else {
        /* WinUI cards define no pressed visual: the face
         * is unchanged by the press */
        CHECK(diff_count(c0, c1, q) == 0);
    }
    send_ptr(r, TK_EV_POINTER_UP, cx, cy);
    CHECK(card16_fires == 1);
    /* a press released outside does not fire */
    send_ptr(r, TK_EV_POINTER_DOWN, cx, cy);
    send_ptr(r, TK_EV_POINTER_UP, 4, 4);
    CHECK(card16_fires == 1);
    tk_canvas_free(c0); tk_canvas_free(c1);
    tk_root_free(r);
    tk_widget_free(cd);
    /* a card without a click callback stays inert: no
     * press tracking, no ripple, no visual change */
    tk_widget *cd2 = tk_card_new(TK_CARD_FILLED);
    tk_add_child(cd2, tk_label_new("カード"));
    tk_root *r2 = root_of(cd2);
    tk_rect q2 = tk_widget_rect(cd2);
    tk_canvas *d0 = paint_full(r2);
    send_ptr(r2, TK_EV_POINTER_DOWN, q2.x + q2.w / 2,
             q2.y + q2.h / 2);
    tk_canvas *d1 = paint_full(r2);
    CHECK(diff_count(d0, d1, q2) == 0);
    tk_canvas_free(d0); tk_canvas_free(d1);
    tk_root_free(r2);
    tk_widget_free(cd2);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_card(void)
{
    card_flow(&tk_theme_v2);
    card_flow(&tk_theme_winui);
}

/* ---------- D: the menu fade + exit (#21) ---------------- */
static void menu_exit_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_button_new("主");
    tk_root *r = root_of(host);
    tk_focus(r, host);
    CHECK(tk_focused(r) == host);
    tk_widget *m = tk_menu_new();
    tk_menu_add_item(m, "開く", 10);
    tk_menu_add_item(m, "保存", 20);
    tk_root_tick(r, 1000);
    tk_menu_popup(r, m, 100, 100);
    CHECK(tk_menu_is_open(r, m));
    CHECK(tk_focused(r) == m);
    tk_rect q = tk_widget_rect(m);
    /* the entrance fade starts from transparent: the menu
     * surface is not yet at its settled form */
    tk_canvas *c_in = paint_full(r);
    tk_root_tick(r, 1300); /* entrance complete (<=167 ms) */
    tk_canvas *c_set = paint_full(r);
    CHECK(diff_count(c_in, c_set, q) > 100);
    /* close: the exit run keeps the menu open (and holds
     * the focus) until the fade completes */
    tk_menu_close(r, m);
    CHECK(tk_menu_is_open(r, m));
    CHECK(tk_focused(r) == m);
    tk_root_tick(r, 1340); /* mid exit (M20: the exit pair
                            * is 75 ms M3 / 167 ms WinUI): the
                            * fading surface
                            * differs from BOTH settled
                            * states (the theme's surface and
                            * background are too close for a
                            * single-pixel midpoint check) */
    tk_canvas *c_mid = paint_full(r);
    CHECK(diff_count(c_mid, c_set, q) > 20);
    /* input during the exit is swallowed: Enter does not
     * activate an item */
    key(r, TK_KEY_ENTER);
    CHECK(tk_menu_last_id(m) == -1);
    CHECK(tk_menu_is_open(r, m));
    tk_root_tick(r, 1600); /* exit complete */
    CHECK(!tk_menu_is_open(r, m));
    /* the focus returns to the pre-menu holder, at the end
     * of the exit (the dialog exit's order) */
    CHECK(tk_focused(r) == host);
    tk_canvas *c_gone = paint_full(r);
    CHECK(diff_count(c_mid, c_gone, q) > 20);
    tk_canvas_free(c_in); tk_canvas_free(c_set);
    tk_canvas_free(c_mid); tk_canvas_free(c_gone);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(m);
    /* no live clock: the close is synchronous, as before */
    tk_widget *host2 = tk_button_new("主");
    tk_root *r2 = root_of(host2);
    tk_widget *m2 = tk_menu_new();
    tk_menu_add_item(m2, "開く", 10);
    tk_menu_popup(r2, m2, 100, 100);
    CHECK(tk_menu_is_open(r2, m2));
    tk_menu_close(r2, m2);
    CHECK(!tk_menu_is_open(r2, m2));
    tk_root_free(r2);
    tk_widget_free(host2);
    tk_widget_free(m2);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_menu_exit(void)
{
    menu_exit_flow(&tk_theme_v2);
    menu_exit_flow(&tk_theme_winui);
}

/* ---------- D invariance: ColorPicker / TreeView --------- */
/* With a live clock running, frames at +0 / +500 / +2500 ms
 * must be pixel-identical over the widget's rect: the
 * reference default forms define no time-driven motion on
 * the normal path (see the header note). */
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
static void test_still(void)
{
    /* WinUI #35 ColorPicker */
    tk_theme_set_default(&tk_theme_winui);
    {
        tk_widget *host = tk_label_new("主");
        tk_root *r = root_of(host);
        tk_widget *cp = tk_colorpicker_new("色", 0xFF3366CCu);
        tk_colorpicker_show(r, cp);
        tk_canvas *warm = paint_full(r); /* settle the rect */
        tk_canvas_free(warm);
        still_check(cp, r);
        tk_root_free(r);
        tk_widget_free(host);
        tk_widget_free(cp);
    }
    /* WinUI #20 TreeView (normal path) */
    {
        tk_widget *tv = tk_treeview_new();
        int a = tk_treeview_add(tv, -1, "親1");
        tk_treeview_add(tv, a, "子1");
        tk_treeview_add(tv, a, "子2");
        tk_treeview_add(tv, -1, "親2");
        tk_treeview_set_expanded(tv, a, true);
        tk_root *r = root_of(tv);
        still_check(tv, r);
        tk_root_free(r);
        tk_widget_free(tv);
    }
    tk_theme_set_default(&tk_theme_v2);
    /* Android #19 ListView: the M3 default list defines no
     * item animation (LazyColumn's animateItem is an opt-in
     * API, not the default form - AUDIT SS14.3), so the D
     * evidence is the same pixel stability */
    {
        const char *items[] = { "甲", "乙", "丙", "丁" };
        tk_widget *lv = tk_listview_new();
        tk_listview_set_items(lv, items, 4);
        tk_root *r = root_of(lv);
        still_check(lv, r);
        tk_root_free(r);
        tk_widget_free(lv);
    }
}

int main(void)
{
    test_ripple();
    test_brush();
    test_brush_faces();
    test_chip_check();
    test_segmented_check();
    test_card();
    test_menu_exit();
    test_still();
    printf("tk16: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
