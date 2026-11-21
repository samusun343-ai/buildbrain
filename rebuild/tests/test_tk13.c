/* test_tk13.c - M14 verification: the A/E/F sweep.
 *
 * F (keyboard operation / focus circulation) for the parts
 * whose key paths were never exercised per mode, E (text /
 * icon handling) for the cells that lacked per-mode pixel
 * evidence, and the two M13 leftovers: the Range slider's
 * opt-in programmatic animation (D) and the Spinner's
 * rotation under the WinUI themes (D).
 *
 * Keyboard contracts verified here come from the primary
 * sources, fetched 2026-11-20:
 * - Radio: Microsoft Learn, "Guidelines for radio buttons"
 *   (Selection follows focus; Ctrl+arrow moves focus only;
 *   the group does not wrap) and the APG radio pattern.
 * - Slider: M3 sliders accessibility (arrows step, Home/End
 *   set the first/last value); the APG slider pattern adds
 *   Up/Down as step keys and PageUp/PageDown as a large step
 *   (the WinUI LargeChange convention, 10x the arrow step).
 * - Segmented (multi): the APG toolbar pattern - arrows move
 *   the cursor, Space/Enter toggles - mapped onto this
 *   toolkit's single tab stop.
 * - Carousel: Left/Right move between items, clamped.
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
    /* spacers keep the widget off the root's corner, so
     * probes can sample the surface beside it */
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
static tk_color blend(tk_color fg, tk_color bg)
{
    int a = fg.a;
    tk_color o;
    o.r = (uint8_t)((fg.r * a + bg.r * (255 - a)) / 255);
    o.g = (uint8_t)((fg.g * a + bg.g * (255 - a)) / 255);
    o.b = (uint8_t)((fg.b * a + bg.b * (255 - a)) / 255);
    o.a = 255;
    return o;
}
/* pixels inside q near the rgb triple (any alpha source) */
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
static void text_in(tk_root *r, const char *s)
{
    tk_event e = { 0 };
    e.type = TK_EV_TEXT_INPUT;
    e.text = s;
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
static bool feq(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

/* ---------- Radio: arrow keys, selection follows focus ---------- */
static void radio_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *r0 = tk_radio_new("甲", 5, 11);
    tk_widget *r1 = tk_radio_new("乙", 5, 22);
    tk_widget *r2 = tk_radio_new("丙", 5, 33);
    tk_widget *rx = tk_radio_new("別", 9, 99);
    notify_count = 0;
    tk_radio_set_on_change(r0, on_notify, NULL);
    tk_radio_set_on_change(r2, on_notify, NULL);
    tk_add_child(box, r0);
    tk_add_child(box, r1);
    tk_add_child(box, r2);
    tk_add_child(box, rx);
    tk_set_enabled(r1, false); /* skipped by the arrow walk */
    tk_root *r = root_of(box);
    CHECK(tk_focus(r, r0));
    tk_radio_select(r0);
    notify_count = 0;
    /* Down: focus moves to the next ENABLED member (r1 is
     * disabled) and selection follows focus */
    key(r, TK_KEY_DOWN);
    CHECK(tk_focused(r) == r2);
    CHECK(tk_radio_selected(r2));
    CHECK(!tk_radio_selected(r0));
    CHECK(notify_count == 1);
    /* the edge does not wrap */
    key(r, TK_KEY_DOWN);
    CHECK(tk_focused(r) == r2);
    CHECK(tk_radio_selected(r2));
    /* Up returns, Right/Left are the horizontal pair */
    key(r, TK_KEY_UP);
    CHECK(tk_focused(r) == r0);
    CHECK(tk_radio_selected(r0));
    key(r, TK_KEY_RIGHT);
    CHECK(tk_focused(r) == r2);
    CHECK(tk_radio_selected(r2));
    key(r, TK_KEY_LEFT);
    CHECK(tk_focused(r) == r0);
    CHECK(tk_radio_selected(r0));
    /* Ctrl+arrow moves focus only; Space then selects */
    key_mod(r, TK_KEY_DOWN, TK_MOD_CTRL);
    CHECK(tk_focused(r) == r2);
    CHECK(tk_radio_selected(r0));
    CHECK(!tk_radio_selected(r2));
    key(r, TK_KEY_SPACE);
    CHECK(tk_radio_selected(r2));
    /* the other group was never touched */
    CHECK(!tk_radio_selected(rx));
    tk_root_free(r);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_radio_keys(void)
{
    radio_flow(&tk_theme_v2);
    radio_flow(&tk_theme_v2_dark);
    radio_flow(&tk_theme_winui);
    radio_flow(&tk_theme_winui_dark);
}

/* ---------- Slider: the full key contract + dual bubble (E) --- */
static bool dual_fill_at(tk_root *r, tk_rect q, tk_color accent,
                         float frac)
{
    tk_canvas *c = paint_full(r);
    int y = q.y + q.h / 2;
    bool on = col_near_c(px(c, q.x + (int)(frac * (float)q.w), y),
                         accent, 2);
    tk_canvas_free(c);
    return on;
}
static void slider_keys_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    /* single, fraction form */
    tk_widget *s = tk_slider_new(0.5f);
    tk_widget_set_min_size(s, 400, 56);
    tk_root *r = root_of(s);
    CHECK(tk_focus(r, s));
    key(r, TK_KEY_RIGHT);
    CHECK(feq(tk_slider_value(s), 0.55f));
    key(r, TK_KEY_UP);
    CHECK(feq(tk_slider_value(s), 0.60f));
    key(r, TK_KEY_PAGE_UP); /* +10 steps, clamps at the end */
    CHECK(feq(tk_slider_value(s), 1.0f));
    key(r, TK_KEY_LEFT);
    CHECK(feq(tk_slider_value(s), 0.95f));
    key(r, TK_KEY_PAGE_DOWN);
    CHECK(feq(tk_slider_value(s), 0.45f));
    key(r, TK_KEY_END);
    CHECK(feq(tk_slider_value(s), 1.0f));
    key(r, TK_KEY_HOME);
    CHECK(feq(tk_slider_value(s), 0.0f));
    key(r, TK_KEY_DOWN); /* clamps at the start */
    CHECK(feq(tk_slider_value(s), 0.0f));
    tk_root_free(r);
    tk_widget_free(s);
    /* single, discrete range form: the step is the stop */
    s = tk_slider_new(0.4f);
    tk_slider_set_range(s, 0, 100, 5);
    tk_widget_set_min_size(s, 400, 56);
    r = root_of(s);
    CHECK(feq(tk_slider_value_range(s), 40));
    CHECK(tk_focus(r, s));
    key(r, TK_KEY_RIGHT);
    CHECK(feq(tk_slider_value_range(s), 45));
    key(r, TK_KEY_PAGE_UP); /* 10 stops */
    CHECK(feq(tk_slider_value_range(s), 95));
    key(r, TK_KEY_PAGE_UP);
    CHECK(feq(tk_slider_value_range(s), 100));
    key(r, TK_KEY_HOME);
    CHECK(feq(tk_slider_value_range(s), 0));
    key(r, TK_KEY_END);
    CHECK(feq(tk_slider_value_range(s), 100));
    key(r, TK_KEY_DOWN);
    CHECK(feq(tk_slider_value_range(s), 95));
    tk_root_free(r);
    tk_widget_free(s);
    /* dual (range form): the active handle steps; its ends
     * of travel are the other handle and its own end */
    s = tk_slider_new(0.0f);
    tk_slider_set_range(s, 0, 100, 0);
    tk_slider_set_values2(s, 20, 80);
    tk_widget_set_min_size(s, 400, 56);
    r = root_of(s);
    tk_rect q = tk_widget_rect(s);
    /* E (differential): the value-indicator bubble is absent
     * before any step, present above the active handle while
     * stepped - its interior is the inverse-surface fill
     * plus glyph pixels pulled strongly toward
     * inverse-on-surface (counted as deviation from the
     * fill, which stays valid in the themes whose
     * background sits near the inverse-on colour) */
    int acx = q.x + (int)(0.80f * (float)q.w);
    tk_rect bb = { acx - 28, q.y - 55 < 0 ? 0 : q.y - 55, 56, 51 };
    /* the text band of the bubble, kept clear of the thumb
     * zone below it */
    tk_rect inner = { bb.x + 6, bb.y + 6, 44, 26 };
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, inner, t->inverse_surface.r,
                     t->inverse_surface.g, t->inverse_surface.b,
                     10) < 50);
    tk_canvas_free(c);
    CHECK(tk_focus(r, s));
    CHECK(tk_slider_active_handle(s) == 1);
    key(r, TK_KEY_LEFT); /* hi: 80 - 5 (5% of span) */
    CHECK(feq(tk_slider_value_hi(s), 75));
    CHECK(feq(tk_slider_value_lo(s), 20));
    c = paint_full(r);
    CHECK(count_near(c, inner, t->inverse_surface.r,
                     t->inverse_surface.g, t->inverse_surface.b,
                     10) > 400);
    {
        int dev = 0;
        for (int y = inner.y; y < inner.y + inner.h; y++)
            for (int x = inner.x; x < inner.x + inner.w; x++) {
                tk_color p = px(c, x, y);
                int d = abs(p.r - t->inverse_surface.r);
                if (abs(p.g - t->inverse_surface.g) > d)
                    d = abs(p.g - t->inverse_surface.g);
                if (abs(p.b - t->inverse_surface.b) > d)
                    d = abs(p.b - t->inverse_surface.b);
                if (d > 50) dev++;
            }
        CHECK(dev >= 15); /* the "%g" value glyphs */
    }
    tk_canvas_free(c);
    tk_slider_set_active_handle(s, 0);
    key(r, TK_KEY_RIGHT); /* lo: 20 + 5 */
    CHECK(feq(tk_slider_value_lo(s), 25));
    key(r, TK_KEY_END); /* lo's far end is the hi handle */
    CHECK(feq(tk_slider_value_lo(s), 75));
    tk_slider_set_active_handle(s, 1);
    key(r, TK_KEY_HOME); /* hi's far end is the lo handle */
    CHECK(feq(tk_slider_value_hi(s), 75));
    tk_slider_set_active_handle(s, 0);
    key(r, TK_KEY_HOME); /* lo back to its own end */
    CHECK(feq(tk_slider_value_lo(s), 0));
    tk_slider_set_active_handle(s, 1);
    key(r, TK_KEY_PAGE_DOWN); /* hi: 75 - 50 */
    CHECK(feq(tk_slider_value_hi(s), 25));
    tk_root_free(r);
    tk_widget_free(s);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_slider_keys(void)
{
    slider_keys_flow(&tk_theme_v2);
    slider_keys_flow(&tk_theme_v2_dark);
    slider_keys_flow(&tk_theme_winui);
    slider_keys_flow(&tk_theme_winui_dark);
}

/* ---------- Range D: the opt-in programmatic animation -------- */
static void range_anim_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 167 : 200;
    tk_widget *s = tk_slider_new(0.0f);
    tk_slider_set_range(s, 0, 1, 0);
    tk_slider_set_values2(s, 0.2f, 0.5f);
    tk_widget_set_min_size(s, 400, 56);
    tk_slider_set_animate_values(s, true); /* opt-in under test */
    tk_root *r = root_of(s);
    tk_rect q = tk_widget_rect(s);
    CHECK(q.w >= 400);
    tk_root_tick(r, 1000);
    tk_slider_set_values2(s, 0.6f, 0.9f);
    /* the logical values are immediate */
    CHECK(feq(tk_slider_value_lo(s), 0.6f));
    CHECK(feq(tk_slider_value_hi(s), 0.9f));
    /* displayed still (0.2, 0.5): the fill spans [.2, .5] */
    CHECK(dual_fill_at(r, q, t->accent, 0.35f));
    CHECK(!dual_fill_at(r, q, t->accent, 0.75f));
    tk_root_tick(r, 1000 + dur / 2);
    /* displayed ~= (0.55, 0.85): the pair's eased progress
     * at half duration measures 0.885 (M21 probe), not the
     * 0.75 a quadratic decelerate would give. The "not yet
     * there" probe point must clear the hi thumb itself
     * (disc + inner dot reach ~0.887), hence 0.95 - at
     * 0.87 the WinUI thumb's accent dot (M21) is the
     * correct paint and the old point sampled it. */
    CHECK(dual_fill_at(r, q, t->accent, 0.65f));
    CHECK(!dual_fill_at(r, q, t->accent, 0.40f));
    CHECK(!dual_fill_at(r, q, t->accent, 0.95f));
    tk_root_tick(r, 1000 + dur);
    /* displayed (0.6, 0.9) */
    CHECK(dual_fill_at(r, q, t->accent, 0.75f));
    CHECK(!dual_fill_at(r, q, t->accent, 0.35f));
    /* opt-in off (the default): the display snaps */
    tk_slider_set_animate_values(s, false);
    tk_slider_set_values2(s, 0.1f, 0.3f);
    CHECK(dual_fill_at(r, q, t->accent, 0.20f));
    CHECK(!dual_fill_at(r, q, t->accent, 0.75f));
    tk_root_free(r);
    tk_widget_free(s);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_range_anim(void)
{
    range_anim_flow(&tk_theme_v2, false);
    range_anim_flow(&tk_theme_winui, true);
}

/* ---------- Spinner D under the WinUI themes ------------------ */
static bool spin_dot(tk_canvas *c, int cx, int cy, int ang_deg,
                     tk_color accent)
{
    float a = (float)ang_deg * 0.0174533f;
    int x = cx + (int)(45 * cosf(a));
    int y = cy + (int)(45 * sinf(a));
    return col_near_c(px(c, x, y), accent, 40);
}
static void spinner_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sp = tk_spinner_new(96);
    tk_widget_set_min_size(sp, 96, 96);
    tk_root *r = root_of(sp);
    tk_rect q = tk_widget_rect(sp);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_root_tick(r, 1000);
    tk_canvas *c = paint_full(r);
    /* phase 0: the 270-degree arc starts at angle 0 */
    CHECK(spin_dot(c, cx, cy, 45, t->accent));
    CHECK(!spin_dot(c, cx, cy, 315, t->accent));
    tk_canvas_free(c);
    tk_root_tick(r, 1250);
    c = paint_full(r);
    /* 250 ms later the phase advanced 90 degrees */
    CHECK(!spin_dot(c, cx, cy, 45, t->accent));
    CHECK(spin_dot(c, cx, cy, 315, t->accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sp);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_spinner_winui(void)
{
    spinner_flow(&tk_theme_winui);
    spinner_flow(&tk_theme_winui_dark);
}

/* ---------- Focus traversal + the focus ring ------------------ */
static void traversal_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *bA = tk_button_new("A");
    tk_widget *cbB = tk_checkbox_new("B", false);
    tk_widget *bDis = tk_button_new("D");
    tk_widget *hid = tk_slider_new(0.5f);
    tk_widget *tfC = tk_textfield_new("");
    tk_widget *swD = tk_switch_new(NULL, false);
    tk_set_enabled(bDis, false);
    tk_set_visible(hid, false);
    tk_add_child(box, bA);
    tk_add_child(box, cbB);
    tk_add_child(box, bDis);
    tk_add_child(box, hid);
    tk_add_child(box, tfC);
    tk_add_child(box, swD);
    tk_root *r = root_of(box);
    /* the walk: tree order, disabled and invisible skipped,
     * wrapping at both ends */
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == bA);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == cbB);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == tfC);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == swD);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == bA);
    /* Tab / Shift+Tab through the root's key path */
    key(r, TK_KEY_TAB);
    CHECK(tk_focused(r) == cbB);
    key_mod(r, TK_KEY_TAB, TK_MOD_SHIFT);
    CHECK(tk_focused(r) == bA);
    /* the focus ring paints around the focused widget:
     * v2 modes use the focus role; the WinUI modes use the
     * double-stroke visual (dark outer line in light,
     * light outer line in dark) */
    tk_rect q = tk_widget_rect(bA);
    tk_canvas *c = paint_full(r);
    tk_rect band = { q.x - 3, q.y - 3, q.w + 6, 4 };
    if (t->style == 2) {
        if (t->dark)
            CHECK(count_near(c, band, 255, 255, 255, 60) > 8);
        else
            CHECK(count_near(c, band, 0, 0, 0, 60) > 8);
    } else {
        CHECK(count_near(c, band, t->focus.r, t->focus.g,
                         t->focus.b, 24) > 8);
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_traversal(void)
{
    traversal_flow(&tk_theme_v2);
    traversal_flow(&tk_theme_v2_dark);
    traversal_flow(&tk_theme_winui);
    traversal_flow(&tk_theme_winui_dark);
}

/* ---------- Button family: keyboard activation ---------------- */
static void button_keys_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    /* plain + elevated share the button key contract */
    for (int form = 0; form < 2; form++) {
        tk_widget *b = tk_button_new("実行");
        if (form == 1) tk_button_set_variant(b, TK_BTN_ELEVATED);
        tk_button_set_on_click(b, on_notify, NULL);
        notify_count = 0;
        tk_root *r = root_of(b);
        CHECK(tk_focus(r, b));
        key(r, TK_KEY_ENTER);
        CHECK(notify_count == 1);
        key(r, TK_KEY_SPACE);
        CHECK(notify_count == 2);
        key(r, TK_KEY_UP); /* not an activation key */
        CHECK(notify_count == 2);
        tk_root_free(r);
        tk_widget_free(b);
    }
    /* toggle form: Space flips the checked state */
    tk_widget *tb = tk_button_new("切替");
    tk_button_set_variant(tb, TK_BTN_TONAL);
    tk_button_set_toggle(tb, true);
    tk_button_set_on_click(tb, on_notify, NULL);
    notify_count = 0;
    tk_root *r = root_of(tb);
    CHECK(tk_focus(r, tb));
    key(r, TK_KEY_SPACE);
    CHECK(tk_button_toggled(tb));
    CHECK(notify_count == 1);
    key(r, TK_KEY_SPACE);
    CHECK(!tk_button_toggled(tb));
    tk_root_free(r);
    tk_widget_free(tb);
    /* FAB (a button form): focus + Space fires */
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *fab = tk_fab_new(ic, 24, 24, 0);
    tk_button_set_on_click(fab, on_notify, NULL);
    notify_count = 0;
    r = root_of(fab);
    CHECK(tk_focus(r, fab));
    key(r, TK_KEY_SPACE);
    CHECK(notify_count == 1);
    tk_root_free(r);
    tk_widget_free(fab);
    /* icon button (icon-only button): same contract */
    tk_widget *ib = tk_button_new("");
    tk_button_set_icon(ib, ic, 24, 24);
    tk_button_set_icon_only(ib, true);
    tk_button_set_on_click(ib, on_notify, NULL);
    notify_count = 0;
    r = root_of(ib);
    CHECK(tk_focus(r, ib));
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    tk_root_free(r);
    tk_widget_free(ib);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_button_keys(void)
{
    button_keys_flow(&tk_theme_v2);
    button_keys_flow(&tk_theme_winui);
}

/* ---------- Checkbox / Switch: Space while focused ------------ */
static void toggle_keys_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *cb = tk_checkbox_new("有効", false);
    tk_checkbox_set_on_change(cb, on_notify, NULL);
    notify_count = 0;
    tk_root *r = root_of(cb);
    CHECK(tk_focus(r, cb));
    key(r, TK_KEY_SPACE);
    CHECK(tk_checkbox_checked(cb));
    CHECK(notify_count == 1);
    key(r, TK_KEY_SPACE);
    CHECK(!tk_checkbox_checked(cb));
    CHECK(notify_count == 2);
    tk_root_free(r);
    tk_widget_free(cb);
    tk_widget *sw = tk_switch_new(NULL, false);
    tk_switch_set_on_change(sw, on_notify, NULL);
    notify_count = 0;
    r = root_of(sw);
    CHECK(tk_focus(r, sw));
    key(r, TK_KEY_SPACE);
    CHECK(tk_switch_on(sw));
    CHECK(notify_count == 1);
    key(r, TK_KEY_SPACE);
    CHECK(!tk_switch_on(sw));
    tk_root_free(r);
    tk_widget_free(sw);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_toggle_keys(void)
{
    toggle_keys_flow(&tk_theme_v2);
    toggle_keys_flow(&tk_theme_v2_dark);
    toggle_keys_flow(&tk_theme_winui);
    toggle_keys_flow(&tk_theme_winui_dark);
}

/* ---------- Chip: keys (F) + icon/label (E) -------------------- */
static void chip_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
    tk_chip_set_icon(ch, ic, 24, 24);
    tk_chip_set_on_click(ch, on_notify, NULL);
    notify_count = 0;
    tk_root *r = root_of(ch);
    tk_rect q = tk_widget_rect(ch);
    /* E: the leading icon's pixels and the label's glyphs */
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(count_near(c, q, t->text.r, t->text.g, t->text.b,
                     40) > 10);
    tk_canvas_free(c);
    /* F: Space and Enter both toggle the filter chip */
    CHECK(tk_focus(r, ch));
    key(r, TK_KEY_SPACE);
    CHECK(tk_chip_selected(ch));
    CHECK(notify_count == 1);
    key(r, TK_KEY_ENTER);
    CHECK(!tk_chip_selected(ch));
    CHECK(notify_count == 2);
    tk_root_free(r);
    tk_widget_free(ch);
    /* assist chip: activation fires, no selection state */
    tk_widget *as = tk_chip_new(TK_CHIP_ASSIST, "候補");
    tk_chip_set_on_click(as, on_notify, NULL);
    notify_count = 0;
    r = root_of(as);
    CHECK(tk_focus(r, as));
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    CHECK(!tk_chip_selected(as));
    tk_root_free(r);
    tk_widget_free(as);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_chip(void)
{
    chip_flow(&tk_theme_v2);
    chip_flow(&tk_theme_winui);
    chip_flow(&tk_theme_winui_dark);
}

/* ---------- TabBar: arrow keys --------------------------------- */
static void tabbar_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *tb = tk_tabbar_new();
    tk_tabbar_add(tb, "一");
    tk_tabbar_add(tb, "二");
    tk_tabbar_add(tb, "三");
    tk_tabbar_set_on_select(tb, on_notify, NULL);
    notify_count = 0;
    tk_root *r = root_of(tb);
    CHECK(tk_focus(r, tb));
    CHECK(tk_tabbar_selected(tb) == 0);
    key(r, TK_KEY_RIGHT);
    CHECK(tk_tabbar_selected(tb) == 1);
    CHECK(notify_count == 1);
    key(r, TK_KEY_RIGHT);
    CHECK(tk_tabbar_selected(tb) == 2);
    key(r, TK_KEY_RIGHT); /* clamps */
    CHECK(tk_tabbar_selected(tb) == 2);
    key(r, TK_KEY_LEFT);
    CHECK(tk_tabbar_selected(tb) == 1);
    tk_root_free(r);
    tk_widget_free(tb);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_tabbar_keys(void)
{
    tabbar_flow(&tk_theme_v2);
    tabbar_flow(&tk_theme_winui);
}

/* ---------- Segmented: single + multi keyboard models --------- */
static void segmented_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    /* single-select: arrows move the selection */
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "日");
    tk_segmented_add(sg, "週");
    tk_segmented_add(sg, "月");
    tk_segmented_set_on_change(sg, on_notify, NULL);
    tk_segmented_select(sg, 0); /* apps set an initial value */
    notify_count = 0;
    tk_root *r = root_of(sg);
    CHECK(tk_focus(r, sg));
    key(r, TK_KEY_RIGHT);
    CHECK(tk_segmented_selected(sg) == 1);
    CHECK(notify_count == 1);
    key(r, TK_KEY_LEFT);
    CHECK(tk_segmented_selected(sg) == 0);
    tk_root_free(r);
    tk_widget_free(sg);
    /* multi-select: arrows move the cursor (painted as a
     * state layer while focused), Space/Enter toggle it */
    sg = tk_segmented_new(true);
    tk_segmented_add(sg, "朝");
    tk_segmented_add(sg, "昼");
    tk_segmented_add(sg, "夜");
    tk_segmented_set_on_change(sg, on_notify, NULL);
    notify_count = 0;
    r = root_of(sg);
    tk_rect q = tk_widget_rect(sg);
    int sw3 = q.w / 3;
    /* a spot inside segment 1, clear of its label glyphs'
     * vertical centre band edges */
    int probe_x = q.x + sw3 + sw3 / 2;
    int probe_y = q.y + q.h - 6;
    tk_canvas *c = paint_full(r);
    tk_color plain = px(c, probe_x, probe_y);
    tk_canvas_free(c);
    CHECK(tk_focus(r, sg));
    key(r, TK_KEY_RIGHT); /* cursor -> segment 1 */
    c = paint_full(r);
    tk_color layered = px(c, probe_x, probe_y);
    tk_color want = blend((tk_color){ t->text.r, t->text.g,
                                      t->text.b, 26 }, plain);
    CHECK(col_near_c(layered, want, 3));
    tk_canvas_free(c);
    key(r, TK_KEY_SPACE);
    CHECK(tk_segmented_mask(sg) == 0x2u);
    CHECK(notify_count == 1);
    key(r, TK_KEY_ENTER);
    CHECK(tk_segmented_mask(sg) == 0x0u);
    key(r, TK_KEY_LEFT); /* cursor back to segment 0 */
    key(r, TK_KEY_SPACE);
    CHECK(tk_segmented_mask(sg) == 0x1u);
    tk_root_free(r);
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_segmented_keys(void)
{
    segmented_flow(&tk_theme_v2);
    segmented_flow(&tk_theme_v2_dark);
    segmented_flow(&tk_theme_winui);
    segmented_flow(&tk_theme_winui_dark);
}

/* ---------- NavRail / BottomBar / Carousel keys ---------------- */
static void navrail_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *rail = tk_navrail_new();
    tk_navrail_add(rail, "ホーム", ic, 24, 24);
    tk_navrail_add(rail, "検索", ic, 24, 24);
    tk_navrail_add(rail, "設定", ic, 24, 24);
    tk_navrail_set_on_select(rail, on_notify, NULL);
    tk_navrail_select(rail, 0); /* apps set an initial value */
    notify_count = 0;
    tk_root *r = root_of(rail);
    CHECK(tk_focus(r, rail));
    key(r, TK_KEY_DOWN);
    CHECK(tk_navrail_selected(rail) == 1);
    CHECK(notify_count >= 1);
    key(r, TK_KEY_DOWN);
    CHECK(tk_navrail_selected(rail) == 2);
    key(r, TK_KEY_UP);
    CHECK(tk_navrail_selected(rail) == 1);
    tk_root_free(r);
    tk_widget_free(rail);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_navrail_keys(void)
{
    navrail_flow(&tk_theme_v2);
    navrail_flow(&tk_theme_winui);
}

static void bottombar_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *bar = tk_bottombar_new();
    tk_bottombar_add_cell(bar, ic, 24, 24, "ホーム", on_notify,
                          NULL);
    tk_bottombar_add_cell(bar, ic, 24, 24, "設定", on_notify,
                          NULL);
    tk_root *r = root_of(bar);
    tk_widget *cell = tk_bottombar_cell(bar, 1);
    tk_rect q = tk_widget_rect(cell);
    /* E: the cell's icon pixels and label glyphs */
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(count_near(c, q, t->text.r, t->text.g, t->text.b,
                     40) > 6);
    tk_canvas_free(c);
    /* F: the cells are buttons - focus + Space activates */
    notify_count = 0;
    CHECK(tk_focus(r, cell));
    key(r, TK_KEY_SPACE);
    CHECK(notify_count == 1);
    tk_root_free(r);
    tk_widget_free(bar);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_bottombar(void)
{
    bottombar_flow(&tk_theme_v2);
    bottombar_flow(&tk_theme_winui);
    bottombar_flow(&tk_theme_winui_dark);
}

static void carousel_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    uint32_t img[48 * 48];
    solid_icon(img, 48 * 48, 255, 0, 255);
    tk_widget *car = tk_carousel_new();
    tk_carousel_add(car, "一", img, 48, 48);
    tk_carousel_add(car, "二", img, 48, 48);
    tk_carousel_add(car, "三", img, 48, 48);
    tk_widget_set_min_size(car, 600, 200);
    tk_root *r = root_of(car);
    CHECK(tk_focus(r, car));
    CHECK(tk_carousel_index(car) == 0);
    key(r, TK_KEY_RIGHT);
    CHECK(tk_carousel_index(car) == 1);
    key(r, TK_KEY_RIGHT);
    CHECK(tk_carousel_index(car) == 2);
    key(r, TK_KEY_RIGHT); /* clamps at the last item */
    CHECK(tk_carousel_index(car) == 2);
    key(r, TK_KEY_LEFT);
    CHECK(tk_carousel_index(car) == 1);
    tk_root_free(r);
    tk_widget_free(car);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_carousel_keys(void)
{
    carousel_flow(&tk_theme_v2);
    carousel_flow(&tk_theme_winui);
}

/* ---------- Dialogs: focus trap (F) + texts (E) + forms (C) --- */
typedef struct { tk_root *r; tk_widget *dlg; int fired; } dlg_ctx;
static void dlg_ok_cb(tk_widget *w, void *ud)
{
    (void)w;
    dlg_ctx *ctx = ud;
    ctx->fired++;
    tk_dialog_close(ctx->r, ctx->dlg);
}
static int glyphs_near(tk_canvas *c, tk_rect q, tk_color col)
{
    return count_near(c, q, col.r, col.g, col.b, 40);
}
static void dialog_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *ob = tk_button_new("外側");
    tk_add_child(host, ob);
    tk_root *r = root_of(host);
    tk_widget *d = tk_dialog_new("題名です");
    tk_widget *body = tk_label_new("本文テキストです");
    tk_add_child(d, body);
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
    tk_widget *cancel = tk_button_new("中止");
    tk_widget *ok = tk_button_new("決定");
    dlg_ctx ctx = { r, d, 0 };
    tk_button_set_on_click(ok, dlg_ok_cb, &ctx);
    tk_add_child(row, cancel);
    tk_add_child(row, ok);
    tk_add_child(d, row);
    tk_dialog_show(r, d);
    CHECK(tk_dialog_is_shown(r, d));
    CHECK(tk_dialog_is_modal(r, d));
    /* F: initial focus is the first focusable inside; the
     * trap refuses focus outside and Tab cycles inside */
    CHECK(tk_focused(r) == cancel);
    CHECK(!tk_focus(r, ob));
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == ok);
    CHECK(tk_focus_next(r));
    CHECK(tk_focused(r) == cancel);
    /* E: title, body and action-label glyphs all render */
    tk_rect q = tk_widget_rect(d);
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, (tk_rect){ q.x, q.y, q.w, 56 },
                      t->text) > 10);
    CHECK(glyphs_near(c, q, t->text) > 60);
    tk_canvas_free(c);
    /* activation closes through the app callback */
    CHECK(tk_focus(r, ok));
    key(r, TK_KEY_ENTER);
    CHECK(ctx.fired == 1);
    CHECK(!tk_dialog_is_shown(r, d));
    /* modeless form: no trap - outside focus stays live */
    tk_dialog_show_modeless(r, d);
    CHECK(tk_dialog_is_shown(r, d));
    CHECK(!tk_dialog_is_modal(r, d));
    CHECK(tk_focus(r, ob));
    tk_dialog_close(r, d);
    /* full-screen form: the container covers the root */
    tk_dialog_set_fullscreen(d, true);
    tk_dialog_show(r, d);
    q = tk_widget_rect(d);
    CHECK(q.x == 0 && q.y == 0 && q.w == 800 && q.h == 480);
    c = paint_full(r);
    CHECK(glyphs_near(c, (tk_rect){ 0, 0, 800, 120 },
                      t->text) > 10);
    tk_canvas_free(c);
    tk_dialog_close(r, d);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(d);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_dialogs(void)
{
    dialog_flow(&tk_theme_v2);
    dialog_flow(&tk_theme_winui);
    dialog_flow(&tk_theme_winui_dark);
}

/* ---------- Menu: keyboard pick (F) + icon/label (E) ---------- */
static void menu_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_of(host);
    tk_widget *m = tk_menu_new();
    tk_menu_add_item(m, "開く", 10);
    tk_menu_add_item(m, "保存", 20);
    tk_menu_add_item(m, "終了", 30);
    tk_menu_set_item_icon(m, 10, ic, 24, 24);
    tk_menu_popup(r, m, 100, 100);
    CHECK(tk_menu_is_open(r, m));
    /* E: the first item's icon pixels and label glyphs */
    tk_rect q = tk_widget_rect(m);
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(glyphs_near(c, q, t->text) > 30);
    tk_canvas_free(c);
    /* F: hover starts on the first item; Enter picks it */
    key(r, TK_KEY_ENTER);
    CHECK(tk_menu_last_id(m) == 10);
    CHECK(!tk_menu_is_open(r, m));
    /* Down moves the hover; Enter picks the second item */
    tk_menu_popup(r, m, 100, 100);
    key(r, TK_KEY_DOWN);
    key(r, TK_KEY_ENTER);
    CHECK(tk_menu_last_id(m) == 20);
    /* Escape dismisses without a pick */
    tk_menu_popup(r, m, 100, 100);
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_menu_is_open(r, m));
    CHECK(tk_menu_last_id(m) == 20);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(m);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_menu(void)
{
    menu_flow(&tk_theme_v2);
    menu_flow(&tk_theme_winui);
}

/* ---------- SearchBar: submit / suggest / dismiss (F) --------- */
static void searchbar_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    /* plain submit: Enter fires on_search with the typed text */
    tk_widget *sb = tk_searchbar_new("検索");
    tk_searchbar_set_on_search(sb, on_notify, NULL);
    notify_count = 0;
    tk_root *r = root_of(sb);
    tk_rect q = tk_widget_rect(sb);
    /* E: the leading search glyph and the placeholder text */
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, (tk_rect){ q.x, q.y, 56, q.h },
                     t->text_dim.r, t->text_dim.g, t->text_dim.b,
                     40) > 4);
    CHECK(glyphs_near(c, q, t->text_dim) > 10);
    tk_canvas_free(c);
    CHECK(tk_focus(r, sb));
    text_in(r, "abc");
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    char buf[64];
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 3 &&
          strcmp(buf, "abc") == 0);
    /* editing keys work while focused */
    key(r, TK_KEY_BACKSPACE);
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 2 &&
          strcmp(buf, "ab") == 0);
    tk_root_free(r);
    tk_widget_free(sb);
    /* suggestion form: Down + Enter picks from the list */
    sb = tk_searchbar_new("検索");
    tk_searchbar_set_on_search(sb, on_notify, NULL);
    const char *items[] = { "apple", "apricot", "banana" };
    tk_searchbar_set_suggestions(sb, items, 3);
    notify_count = 0;
    r = root_of(sb);
    CHECK(tk_focus(r, sb));
    text_in(r, "ap");
    CHECK(tk_searchbar_suggestions_open(sb));
    key(r, TK_KEY_DOWN);
    key(r, TK_KEY_ENTER);
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 7 &&
          strcmp(buf, "apricot") == 0);
    CHECK(notify_count == 1);
    CHECK(!tk_searchbar_suggestions_open(sb));
    tk_root_free(r);
    tk_widget_free(sb);
    /* Escape dismisses the list; the typed text submits */
    sb = tk_searchbar_new("検索");
    tk_searchbar_set_on_search(sb, on_notify, NULL);
    tk_searchbar_set_suggestions(sb, items, 3);
    notify_count = 0;
    r = root_of(sb);
    CHECK(tk_focus(r, sb));
    text_in(r, "ap");
    CHECK(tk_searchbar_suggestions_open(sb));
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_searchbar_suggestions_open(sb));
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    CHECK(tk_searchbar_text(sb, buf, sizeof buf) == 2 &&
          strcmp(buf, "ap") == 0);
    tk_root_free(r);
    tk_widget_free(sb);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_searchbar(void)
{
    searchbar_flow(&tk_theme_v2);
    searchbar_flow(&tk_theme_winui);
}

/* ---------- Pickers: field/step keys (F) + active cell (B) ---- */
/* the date picker's cells: year takes 2 of the 4 field units
 * of the area left of the 28px stepper (tk_pk_cell_rect) */
static int pk_cell0_x(tk_rect q) { return q.x + (q.w - 28) / 4; }
static int pk_cell1_x(tk_rect q) { return q.x + 5 * (q.w - 28) / 8; }
static void picker_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *dp = tk_datepicker_new(2026, 11, 20);
    tk_datepicker_set_on_change(dp, on_notify, NULL);
    notify_count = 0;
    tk_root *r = root_of(dp);
    tk_rect q = tk_widget_rect(dp);
    /* B: the active field's cell carries the selection fill
     * (an alpha role in the WinUI mode - compare the blend
     * over the surface beneath), the inactive cells the
     * alternate surface; advancing the field moves the fill */
    tk_canvas *c = paint_full(r);
    tk_color under = px(c, pk_cell0_x(q), q.y - 6);
    tk_color active = blend(t->selection, under);
    CHECK(col_near_c(px(c, pk_cell0_x(q), q.y + 6), active, 3));
    CHECK(col_near_c(px(c, pk_cell1_x(q), q.y + 6),
                     t->surface_alt, 3));
    tk_canvas_free(c);
    int y, mo, da;
    CHECK(tk_focus(r, dp));
    key(r, TK_KEY_UP); /* year + 1 */
    tk_datepicker_value(dp, &y, &mo, &da);
    CHECK(y == 2027 && mo == 11 && da == 20);
    key(r, TK_KEY_RIGHT); /* -> month */
    c = paint_full(r);
    CHECK(col_near_c(px(c, pk_cell0_x(q), q.y + 6),
                     t->surface_alt, 3));
    CHECK(col_near_c(px(c, pk_cell1_x(q), q.y + 6), active, 3));
    tk_canvas_free(c);
    key(r, TK_KEY_UP);
    tk_datepicker_value(dp, &y, &mo, &da);
    CHECK(mo == 12);
    key(r, TK_KEY_DOWN);
    tk_datepicker_value(dp, &y, &mo, &da);
    CHECK(mo == 11);
    key(r, TK_KEY_RIGHT); /* -> day */
    key(r, TK_KEY_DOWN);
    tk_datepicker_value(dp, &y, &mo, &da);
    CHECK(da == 19);
    key(r, TK_KEY_RIGHT); /* wraps -> year */
    key(r, TK_KEY_DOWN);
    tk_datepicker_value(dp, &y, &mo, &da);
    CHECK(y == 2026);
    CHECK(notify_count >= 4);
    tk_root_free(r);
    tk_widget_free(dp);
    /* time picker: hour/minute fields */
    tk_widget *tp = tk_timepicker_new(false);
    tk_timepicker_set_value(tp, 10, 30, 0);
    r = root_of(tp);
    CHECK(tk_focus(r, tp));
    key(r, TK_KEY_UP);
    int h, mi, s2;
    tk_timepicker_value(tp, &h, &mi, &s2);
    CHECK(h == 11 && mi == 30);
    key(r, TK_KEY_RIGHT);
    key(r, TK_KEY_UP);
    tk_timepicker_value(tp, &h, &mi, &s2);
    CHECK(mi == 31);
    tk_root_free(r);
    tk_widget_free(tp);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_pickers(void)
{
    picker_flow(&tk_theme_v2);
    picker_flow(&tk_theme_winui);
}

/* ---------- TreeView: keys (F) + label/glyph (E) + row (B) ---- */
static void treeview_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *tv = tk_treeview_new();
    int root_a = tk_treeview_add(tv, -1, "親A");
    int child1 = tk_treeview_add(tv, root_a, "子1");
    tk_treeview_add(tv, root_a, "子2");
    tk_treeview_add(tv, -1, "親B");
    tk_treeview_set_on_activate(tv, on_notify, NULL);
    tk_treeview_set_expanded(tv, root_a, false);
    tk_widget_set_min_size(tv, 400, 300);
    notify_count = 0;
    tk_root *r = root_of(tv);
    tk_rect q = tk_widget_rect(tv);
    CHECK(tk_focus(r, tv));
    /* collapsed: 2 visible rows; Down selects the first row */
    CHECK(tk_treeview_visible_rows(tv) == 2);
    key(r, TK_KEY_DOWN);
    CHECK(tk_treeview_selected(tv) == root_a);
    /* E: the expand glyph and the node label render */
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) > 30);
    tk_canvas_free(c);
    /* B: the selected row carries the selection fill (an
     * alpha role in the WinUI mode - the blend over the
     * surface the tree paints first) with the accent bar */
    c = paint_full(r);
    int rowy = q.y + t->row_h / 2;
    CHECK(col_near_c(px(c, q.x + q.w / 2, rowy),
                     blend(t->selection, t->surface), 2));
    CHECK(col_near_c(px(c, q.x + 1, rowy), t->accent, 2));
    tk_canvas_free(c);
    /* Right expands: children appear; Down moves to child 1 */
    key(r, TK_KEY_RIGHT);
    CHECK(tk_treeview_expanded(tv, root_a));
    CHECK(tk_treeview_visible_rows(tv) == 4);
    /* E: the child's label starts deeper than the parent's */
    c = paint_full(r);
    int parent_l = -1, child_l = -1;
    for (int x = q.x; x < q.x + q.w && parent_l < 0; x++)
        if (col_near_c(px(c, x, q.y + t->row_h / 2), t->text, 40))
            parent_l = x;
    for (int x = q.x; x < q.x + q.w && child_l < 0; x++)
        if (col_near_c(px(c, x, q.y + t->row_h + t->row_h / 2),
                       t->text, 40))
            child_l = x;
    CHECK(parent_l > 0 && child_l > parent_l);
    tk_canvas_free(c);
    key(r, TK_KEY_DOWN);
    CHECK(tk_treeview_selected(tv) == child1);
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    /* Left collapses the selected node: step back to the
     * parent, then collapse it */
    key(r, TK_KEY_UP);
    CHECK(tk_treeview_selected(tv) == root_a);
    key(r, TK_KEY_LEFT);
    CHECK(!tk_treeview_expanded(tv, root_a));
    CHECK(tk_treeview_visible_rows(tv) == 2);
    tk_root_free(r);
    tk_widget_free(tv);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_treeview(void)
{
    treeview_flow(&tk_theme_v2);
    treeview_flow(&tk_theme_winui);
}

/* ---------- SplitButton: two tab stops (F) + label/chev (E) --- */
static void split_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sp = tk_splitbutton_new("保存", on_notify, NULL);
    tk_widget *m = tk_menu_new();
    tk_menu_add_item(m, "名前を付けて保存", 1);
    tk_menu_add_item(m, "上書き保存", 2);
    tk_splitbutton_set_menu(sp, m);
    notify_count = 0;
    tk_root *r = root_of(sp);
    tk_widget *main_btn = tk_splitbutton_main(sp);
    tk_widget *arrow = tk_splitbutton_arrow(sp);
    /* E: the main label glyphs and the chevron in the arrow -
     * both segments are filled buttons, so the content takes
     * the on-accent role */
    tk_rect qa = tk_widget_rect(arrow);
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, tk_widget_rect(main_btn),
                      t->on_accent) > 10);
    CHECK(glyphs_near(c, qa, t->on_accent) > 2);
    tk_canvas_free(c);
    /* F: the main segment activates from the keyboard */
    CHECK(tk_focus(r, main_btn));
    key(r, TK_KEY_SPACE);
    CHECK(notify_count == 1);
    /* F: the chevron segment opens the menu from the keyboard */
    CHECK(tk_focus(r, arrow));
    key(r, TK_KEY_SPACE);
    CHECK(tk_menu_is_open(r, m));
    key(r, TK_KEY_ESCAPE);
    CHECK(!tk_menu_is_open(r, m));
    tk_root_free(r);
    tk_widget_free(sp); /* the split owns its menu */
    tk_theme_set_default(&tk_theme_v2);
}
static void test_split(void)
{
    split_flow(&tk_theme_v2);
    split_flow(&tk_theme_winui);
}

/* ---------- AppBar: title/actions (E), activation (C/F) ------- */
static void appbar_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *ab = tk_appbar_new("題名です");
    tk_widget *lead = tk_button_new("");
    tk_button_set_icon(lead, ic, 24, 24);
    tk_button_set_icon_only(lead, true);
    tk_appbar_set_leading(ab, lead);
    tk_widget *act = tk_button_new("");
    tk_button_set_icon(act, ic, 24, 24);
    tk_button_set_icon_only(act, true);
    tk_button_set_on_click(act, on_notify, NULL);
    tk_add_child(ab, act);
    notify_count = 0;
    tk_root *r = root_of(ab);
    tk_rect q = tk_widget_rect(ab);
    /* E: the title glyphs and the action icon render */
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) > 10);
    tk_rect qa = tk_widget_rect(act);
    CHECK(count_near(c, qa, 255, 0, 255, 8) > 40);
    tk_canvas_free(c);
    /* C/F: the action is a tab stop and activates; the
     * scrolled state toggles without disturbing the actions */
    CHECK(tk_focus(r, act));
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    tk_appbar_set_scrolled(ab, true);
    c = paint_full(r);
    CHECK(count_near(c, qa, 255, 0, 255, 8) > 40);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(ab);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_appbar(void)
{
    appbar_flow(&tk_theme_v2);
    appbar_flow(&tk_theme_winui);
    appbar_flow(&tk_theme_winui_dark);
}

/* ---------- Snackbar / Toast texts under WinUI (E) ------------ */
static bool find_face(tk_canvas *c, tk_color face, tk_rect *out)
{
    int x0 = 900, y0 = 900, x1 = -1, y1 = -1;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 800; x++) {
            if (col_near_c(px(c, x, y), face, 2)) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
        }
    if (x1 < 0) return false;
    *out = (tk_rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    return true;
}
static void transient_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_of(host);
    /* InfoBar form: the severity disc (the solid system
     * colour) anchors the layout - the message sits right of
     * it in the text role, the action at the far end in the
     * accent. (The bar's own background is an alpha token in
     * the dark theme, so the opaque disc is the locator.) */
    tk_snackbar_show_sev(r, "保存しました", TK_SNACK_INFO,
                         "元に戻す", on_notify, NULL, 60000);
    CHECK(tk_snackbar_visible(r));
    tk_canvas *c = paint_full(r);
    tk_rect disc;
    CHECK(find_face(c, t->sys_attn, &disc));
    if (disc.w > 0) {
        tk_rect msg = { disc.x + disc.w + 20, disc.y - 16,
                        260, 44 };
        CHECK(glyphs_near(c, msg, t->text) > 10);
        tk_rect act = { disc.x + disc.w + 250, disc.y - 16,
                        330, 44 };
        CHECK(glyphs_near(c, act, t->accent) > 3);
    }
    tk_canvas_free(c);
    tk_snackbar_hide(r);
    /* toast: the inverse container with its text */
    tk_toast_show(r, "完了しました", 60000);
    c = paint_full(r);
    tk_rect q;
    CHECK(find_face(c, t->inverse_surface, &q));
    if (q.w > 0)
        CHECK(glyphs_near(c, q, t->inverse_on_surface) > 8);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_transients(void)
{
    transient_flow(&tk_theme_winui);
    transient_flow(&tk_theme_winui_dark);
}

/* ---------- Drawer / Sheet: item texts (E) + geometry (A) ----- */
static tk_rect drawer_rect_seen;
static void drawer_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *hbox = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
    tk_widget *dr = tk_drawer_new("見出しです");
    tk_drawer_add_item(dr, "ホーム", 1, ic, 24, 24);
    tk_drawer_add_item(dr, "設定", 2, ic, 24, 24);
    tk_drawer_attach(hbox, dr);
    tk_add_child(hbox, tk_label_new("本文"));
    tk_root *r = tk_root_new(hbox);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(dr);
    /* A (mode mapping): the standard panel measures from
     * its content (280 floor, capped at the 360dp = 421px
     * token) and fills the parent's height; the arranged
     * rect is identical across modes (checked in
     * test_drawer) */
    CHECK(q.w >= 280 && q.w <= 421);
    CHECK(q.h == 480);
    drawer_rect_seen = q;
    /* E: the headline and the unselected item labels take
     * the dim (onSurfaceVariant) role; the icons render;
     * selecting an item switches its label to the
     * on-selection role */
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, (tk_rect){ q.x, q.y, q.w, 72 },
                      t->text_dim) > 10);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(glyphs_near(c, q, t->text_dim) > 40);
    tk_canvas_free(c);
    tk_drawer_select(dr, 1);
    c = paint_full(r);
    tk_color onsel = t->on_selection.a ? t->on_selection
                                       : t->text;
    CHECK(glyphs_near(c, (tk_rect){ q.x, q.y + 72, q.w, 64 },
                      onsel) > 5);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(hbox);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_drawer(void)
{
    drawer_flow(&tk_theme_v2);
    tk_rect a = drawer_rect_seen;
    drawer_flow(&tk_theme_winui);
    CHECK(drawer_rect_seen.w == a.w && drawer_rect_seen.h == a.h);
}

static void sheet_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_of(host);
    tk_widget *sh = tk_sheet_new();
    tk_add_child(sh, tk_label_new("シートの内容です"));
    tk_sheet_show(r, sh, true);
    CHECK(tk_sheet_is_shown(r, sh));
    tk_rect q = tk_widget_rect(sh);
    /* A (mode mapping): the bottom sheet is anchored to the
     * bottom edge with equal side margins in both modes */
    CHECK(q.y + q.h == 480);
    CHECK(q.w >= 700 && q.x == 800 - (q.x + q.w));
    /* E: the content label renders inside the sheet */
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) > 10);
    tk_canvas_free(c);
    tk_sheet_close(r, sh);
    CHECK(!tk_sheet_is_shown(r, sh));
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(sh);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_sheet(void)
{
    sheet_flow(&tk_theme_v2);
    sheet_flow(&tk_theme_winui);
}

/* ---------- Card: content (E), traversal (F), variant (C) ----- */
static void card_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *card = tk_card_new(TK_CARD_OUTLINED);
    tk_add_child(card, tk_label_new("カードの本文です"));
    tk_widget *btn = tk_button_new("開く");
    tk_button_set_on_click(btn, on_notify, NULL);
    tk_add_child(card, btn);
    notify_count = 0;
    tk_root *r = root_of(card);
    tk_rect q = tk_widget_rect(card);
    /* E: the child's text renders inside the card's padding */
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) > 20);
    tk_canvas_free(c);
    /* F: traversal reaches the card's interactive child */
    CHECK(tk_focus(r, btn));
    key(r, TK_KEY_ENTER);
    CHECK(notify_count == 1);
    /* C: the variant cycles and the children stay laid out
     * inside the card */
    tk_card_set_variant(card, TK_CARD_FILLED);
    CHECK(tk_card_variant_of(card) == TK_CARD_FILLED);
    tk_rect qb = tk_widget_rect(btn);
    CHECK(qb.x >= q.x && qb.y >= q.y &&
          qb.x + qb.w <= q.x + q.w &&
          qb.y + qb.h <= q.y + q.h);
    tk_root_free(r);
    tk_widget_free(card);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_card(void)
{
    card_flow(&tk_theme_v2);
    card_flow(&tk_theme_winui);
}

/* ---------- Badge behaviour under WinUI (C) -------------------- */
static void badge_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *b = tk_badge_new(3);
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    /* the count pill: error-red face with the count glyph */
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 186, 26, 26, 4) > 20);
    CHECK(count_near(c, q, 255, 255, 255, 8) > 3);
    tk_canvas_free(c);
    /* zero hides: no measure, no paint */
    tk_badge_set_count(b, 0);
    tk_size d = tk_measure(b, (tk_constraints){ 0, TK_UNLIMITED,
                                               0, TK_UNLIMITED });
    CHECK(d.w == 0 && d.h == 0);
    /* past two digits the label caps at 99+ (and widens) */
    tk_badge_set_count(b, 150);
    tk_size d2 = tk_measure(b, (tk_constraints){ 0, TK_UNLIMITED,
                                                 0, TK_UNLIMITED });
    CHECK(d2.w > q.w);
    /* the dot form is a plain disc */
    tk_badge_set_count(b, -1);
    tk_size d3 = tk_measure(b, (tk_constraints){ 0, TK_UNLIMITED,
                                                 0, TK_UNLIMITED });
    CHECK(d3.w == 8 && d3.h == 8);
    tk_root_free(r);
    tk_widget_free(b);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_badge(void)
{
    badge_flow(&tk_theme_winui);
    badge_flow(&tk_theme_v2);
}

/* ---------- Drag & drop under WinUI (C) ------------------------ */
static int drop_fires;
static char drop_data[32];
static void on_drop13(tk_widget *target, void *ud, const char *mime,
                      const void *data, size_t len, int x, int y)
{
    (void)target; (void)ud; (void)mime; (void)x; (void)y;
    drop_fires++;
    size_t n = len < sizeof drop_data - 1 ? len
                                          : sizeof drop_data - 1;
    memcpy(drop_data, data, n);
    drop_data[n] = 0;
}
static void dnd_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
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
    tk_widget_set_on_drop(dst, on_drop13, NULL);
    drop_fires = 0;
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    send_ptr(r, TK_EV_POINTER_DOWN, 400, 60);
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 100);
    CHECK(tk_drag_active(r));
    send_ptr(r, TK_EV_POINTER_MOVE, 400, 220);
    send_ptr(r, TK_EV_POINTER_UP, 400, 220);
    CHECK(!tk_drag_active(r));
    CHECK(drop_fires == 1);
    CHECK(strcmp(drop_data, "DATA") == 0);
    tk_root_free(r);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_dnd(void)
{
    dnd_flow(&tk_theme_winui);
}

/* ---------- ListView states (B, v2) + icon/text (E, WinUI) ---- */
static void listview_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    const char *items[] = { "見出し", "項目1", "項目2", "項目3" };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 4);
    tk_listview_set_header(lv, 0, true);
    tk_listview_set_checkable(lv, true);
    tk_listview_set_row_checked(lv, 2, true);
    tk_listview_select(lv, 1);
    tk_listview_set_marked(lv, 3);
    tk_widget_set_min_size(lv, 500, 300);
    tk_root *r = root_of(lv);
    tk_rect q = tk_widget_rect(lv);
    int rh = tk_listview_row_h(lv, t);
    tk_canvas *c = paint_full(r);
    /* B (v2): the selected row is the inset selection pill */
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + rh + rh / 2),
                     t->selection, 2));
    /* the marked row takes the alternate-surface pill */
    CHECK(col_near_c(px(c, q.x + q.w / 2, q.y + 3 * rh + rh / 2),
                     t->surface_alt, 2));
    /* the checked row's box: accent with the on-accent core.
     * M22: the frame is the real painted box (21px in the
     * M3 form, 18px otherwise, as in the ListView paint)
     * and the glyph probe sits on the long arm of the
     * material3-ratio check path in each size. */
    int lbs = t->style == 1 ? 21 : 18;
    int bx = q.x + 2 * t->unit, by = q.y + 2 * rh + (rh - lbs) / 2;
    CHECK(col_near_c(px(c, bx + (t->style == 1 ? 12 : 11),
                        by + (t->style == 1 ? 10 : 9)),
                     t->on_accent, 2));
    /* M20 fix: the fill sample sits inside the r3 corner arc;
     * (bx+1, by+1) is on the arc itself since the box outline
     * follows the rounded fill (it was accent only under the
     * pre-fix square stroke) */
    CHECK(col_near_c(px(c, bx + 2, by + 2), t->accent, 2));
    /* the header row: caption text in the dim role, no pill */
    CHECK(glyphs_near(c, (tk_rect){ q.x, q.y, q.w, rh },
                      t->text_dim) > 5);
    CHECK(!col_near_c(px(c, q.x + q.w / 2, q.y + rh / 2),
                      t->selection, 2));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(lv);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_listview_states(void)
{
    listview_flow(&tk_theme_v2);
    listview_flow(&tk_theme_v2_dark);
}

static void listview_e_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    const char *items[] = { "項目1", "項目2" };
    const char *subs[] = { "補足1", "補足2" };
    const uint32_t *icons[] = { ic, ic };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 2);
    tk_listview_set_subitems(lv, subs, 2);
    tk_listview_set_item_icons(lv, icons, 24, 24, 2);
    tk_widget_set_min_size(lv, 500, 300);
    tk_root *r = root_of(lv);
    tk_rect q = tk_widget_rect(lv);
    /* E: item icons, main labels and sub-labels all render */
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, q, 255, 0, 255, 8) > 40);
    CHECK(glyphs_near(c, q, t->text) > 20);
    CHECK(glyphs_near(c, q, t->text_dim) > 5);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(lv);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_listview_e(void)
{
    listview_e_flow(&tk_theme_winui);
    listview_e_flow(&tk_theme_winui_dark);
}

/* ---------- Segmented faces under v2 (B) ----------------------- */
static void segmented_b_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "日");
    tk_segmented_add(sg, "週");
    tk_segmented_add(sg, "月");
    tk_root *r = root_of(sg);
    tk_rect q = tk_widget_rect(sg);
    int sw3 = q.w / 3;
    /* unselected: a divider (the border-line role) stands
     * between two unselected segments */
    tk_canvas *c = paint_full(r);
    CHECK(col_near_c(px(c, q.x + sw3, q.y + q.h / 2),
                     t->border_line, 24));
    tk_canvas_free(c);
    /* selected: the tonal container fill plus the check
     * glyph in the segment's leading slot (the check takes
     * the content colour; the strip before the label holds
     * only the check) */
    tk_segmented_select(sg, 1);
    c = paint_full(r);
    CHECK(col_near_c(px(c, q.x + sw3 + sw3 / 2, q.y + q.h / 2),
                     t->container, 2));
    {
        int tw = tk_text_width(t, "週");
        int cx = q.x + sw3 + (sw3 - (tw + 24)) / 2;
        CHECK(count_near(c, (tk_rect){ cx - 2, q.y, 20, q.h },
                         t->text.r, t->text.g, t->text.b,
                         24) > 6);
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_segmented_b(void)
{
    segmented_b_flow(&tk_theme_v2);
    segmented_b_flow(&tk_theme_v2_dark);
}

/* ---------- ColorPicker: swatches (B), hex (E), focus (F) ----- */
static int cp_fires;
static uint32_t cp_value;
static bool cp_chosen;
static void on_cp13(tk_widget *dlg, uint32_t rgba, bool chosen,
                    void *ud)
{
    (void)dlg; (void)ud;
    cp_fires++; cp_value = rgba; cp_chosen = chosen;
}
static void colorpicker_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_of(host);
    tk_widget *dlg = tk_colorpicker_new("色の選択", 0x2196F3FFu);
    tk_colorpicker_set_on_result(dlg, on_cp13, NULL);
    tk_colorpicker_show(r, dlg);
    CHECK(tk_dialog_is_shown(r, dlg));
    tk_widget *grid = tk_child_at(dlg, 0);
    CHECK(grid != NULL && tk_child_count(grid) == 24);
    /* B: the first swatch paints its palette colour (the
     * Material red 500 the palette starts with) */
    tk_widget *sw0 = tk_child_at(grid, 0);
    tk_rect q0 = tk_widget_rect(sw0);
    tk_canvas *c = paint_full(r);
    CHECK(col_near_c(px(c, q0.x + q0.w / 2, q0.y + q0.h / 2),
                     (tk_color){ 244, 67, 54, 255 }, 4));
    tk_canvas_free(c);
    /* F: focus is trapped inside; the hex field (child 3)
     * takes focus. The field opens with the current value's
     * hex in it - clear it the way a user does (End, then
     * Backspace over the text) and type the new value */
    tk_widget *hex = tk_child_at(dlg, 3);
    CHECK(hex != NULL);
    CHECK(tk_focus(r, hex));
    key(r, TK_KEY_END);
    for (int i = 0; i < 8; i++) key(r, TK_KEY_BACKSPACE);
    text_in(r, "00FF00");
    CHECK(tk_colorpicker_value(dlg) == 0x00FF00FFu);
    /* E: the typed text itself is in the field (checked via
     * the field's text API) */
    char buf[32];
    CHECK(tk_textfield_text(hex, buf, sizeof buf) == 6 &&
          strcmp(buf, "00FF00") == 0);
    /* OK (button row child 2, its child 1) delivers + closes */
    tk_widget *row = tk_child_at(dlg, 2);
    tk_widget *ok = tk_child_at(row, 1);
    cp_fires = 0;
    CHECK(tk_focus(r, ok));
    key(r, TK_KEY_ENTER);
    CHECK(cp_fires == 1 && cp_chosen);
    CHECK(cp_value == 0x00FF00FFu);
    CHECK(!tk_dialog_is_shown(r, dlg));
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(dlg);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_colorpicker(void)
{
    colorpicker_flow(&tk_theme_v2);
    colorpicker_flow(&tk_theme_winui);
}

/* ---------- TextView under WinUI: keys (F) + selection (E) ---- */
static void textview_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *tv = tk_textview_new("hello world");
    tk_widget_set_min_size(tv, 500, 200);
    tk_root *r = root_of(tv);
    tk_rect q = tk_widget_rect(tv);
    tc_buffer *b = tk_textview_buffer(tv);
    CHECK(tk_focus(r, tv));
    /* F: caret keys move through the buffer */
    key(r, TK_KEY_END);
    CHECK(tc_cursor(b) == 11);
    key(r, TK_KEY_LEFT);
    CHECK(tc_cursor(b) == 10);
    key(r, TK_KEY_HOME);
    CHECK(tc_cursor(b) == 0);
    key_mod(r, TK_KEY_RIGHT, TK_MOD_SHIFT);
    key_mod(r, TK_KEY_RIGHT, TK_MOD_SHIFT);
    CHECK(tc_has_selection(b));
    size_t lo, hi;
    tc_selection_range(b, &lo, &hi);
    CHECK(lo == 0 && hi == 2);
    /* E: the selection paints the selection fill behind the
     * selected glyphs (an alpha role in the WinUI mode -
     * compare the blend over the surface), and the text
     * itself renders */
    tk_canvas *c = paint_full(r);
    CHECK(glyphs_near(c, q, t->text) > 20);
    tk_color selb = blend(t->selection, t->surface);
    CHECK(count_near(c, q, selb.r, selb.g, selb.b, 6) > 30);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(tv);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_textview(void)
{
    textview_flow(&tk_theme_winui);
    textview_flow(&tk_theme_winui_dark);
}

/* ---------- FAB menu items: icon + label (E) ------------------- */
static void fabmenu_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 255, 0, 255);
    tk_widget *host = tk_label_new("主");
    tk_root *r = root_of(host);
    tk_widget *fm = tk_fabmenu_new();
    tk_fabmenu_add_item(fm, ic, 24, 24, "追加する", on_notify,
                        NULL);
    tk_fabmenu_add_item(fm, ic, 24, 24, "共有する", on_notify,
                        NULL);
    tk_fabmenu_show(r, fm);
    /* E: the items' icons and labels render */
    tk_canvas *c = paint_full(r);
    CHECK(count_near(c, (tk_rect){ 0, 0, 800, 480 },
                     255, 0, 255, 8) > 40);
    /* the item labels take the tonal content role (the
     * text role in the WinUI button form) */
    int lbl_px = count_near(c, (tk_rect){ 0, 0, 800, 480 },
                            t->text.r, t->text.g, t->text.b, 40);
    if (t->on_container.a)
        lbl_px += count_near(c, (tk_rect){ 0, 0, 800, 480 },
                             t->on_container.r, t->on_container.g,
                             t->on_container.b, 40);
    CHECK(lbl_px > 20);
    tk_canvas_free(c);
    tk_fabmenu_hide(r, fm);
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

int main(void)
{
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    test_radio_keys();
    test_slider_keys();
    test_range_anim();
    test_spinner_winui();
    test_traversal();
    test_button_keys();
    test_toggle_keys();
    test_chip();
    test_tabbar_keys();
    test_segmented_keys();
    test_navrail_keys();
    test_bottombar();
    test_carousel_keys();
    test_dialogs();
    test_menu();
    test_searchbar();
    test_pickers();
    test_treeview();
    test_split();
    test_appbar();
    test_transients();
    test_drawer();
    test_sheet();
    test_card();
    test_badge();
    test_dnd();
    test_listview_states();
    test_listview_e();
    test_segmented_b();
    test_colorpicker();
    test_textview();
    test_fabmenu();
    printf("tk13: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
