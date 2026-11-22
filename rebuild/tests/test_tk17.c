/* test_tk17.c - M18 verification: the A (dimension) cells
 * left at ◐ by the M17 classification audit (AUDIT SS15.4
 * class (c): "no recorded deviation, dimension tests
 * incomplete"), the M18 spec-driven fixes, the AOSP toast
 * shape, and the suggestion-dropdown entrance (D #15).
 *
 * Reference facts relied on here, fetched 2026-11-20:
 * - M3 tokens (androidx compose/material3 tokens, GitHub
 *   androidx/androidx, compose/material3/material3/src/
 *   commonMain/kotlin/androidx/compose/material3/tokens/):
 *   BaselineButtonTokens (height 40dp, spaces 24dp, icon
 *   20dp), CheckboxTokens (container 18dp, state layer
 *   40dp), RadioButtonTokens (icon 20dp, state layer
 *   40dp), SearchBarTokens (ContainerHeight 56dp),
 *   OutlinedSegmentedButtonTokens (height 40dp),
 *   PrimaryNavigationTabTokens (icon+label container
 *   64dp), CircularProgressIndicatorTokens (stroke 4dp),
 *   SnackbarTokens (single-line 48dp), SheetBottomTokens
 *   (drag handle 32x4dp), TypeScaleTokens (BodyLarge
 *   16/24sp ...). dp -> px at this panel (187ppi):
 *   x1.16875, rounded.
 * - WinUI themeresources (microsoft/microsoft-ui-xaml
 *   main): ProgressRingStrokeThickness 4epx, TabViewItem
 *   MinHeight 32epx, TreeViewItemContentHeight 20epx
 *   (TreeView.xaml), InfoBadge MaxHeight 16epx / value
 *   font 11epx, ContentDialog clamps as pinned by tk7.
 *   epx -> px at this panel: x1.43.
 * - Windows type ramp (Microsoft Learn, Typography in
 *   Windows, fetched 2026-11-20): Caption 12/16, Body
 *   14/20, BodyLarge 18/24, Subtitle 20/28, Title 28/36
 *   epx (size/line height).
 * - AOSP framework toast (platform/frameworks/base
 *   core/res, fetched 2026-11-20): layout/
 *   transient_notification.xml (wrap_content, margins
 *   16dp, padding 16dp, text padding 12dp vertical,
 *   maxLines 2), values/dimens.xml (toast_width 300dp,
 *   toast_y_offset 48dp, toast_text_size 14sp),
 *   anim/toast_enter.xml + toast_exit.xml (alpha,
 *   decelerate_quad / accelerate_quad,
 *   config_longAnimTime = 500 ms).
 *
 * M20 additions (2026-11-21): the #12 ring default
 * diameter (CircularProgressIndicatorTokens.Size 40dp,
 * re-verified 2026-11-21 via ProgressIndicator.kt's
 * CircularIndicatorDiameter = Tokens.Size) and the #21
 * menu scale component (material3 Menu.kt: the entrance
 * transition drives alpha AND scale 0.8 <-> 1.0 about the
 * anchor-side transform origin; In 120 ms in the shipped
 * 1.x line, mirrored by fluentui-android's Menu.kt
 * constants fetched 2026-11-21). The toolkit drives the
 * scale from the same animation value as the fade (the
 * mode pair's duration/easing, as M17 set the fade).
 *
 * Where a cell's basis is the toolkit's own documented
 * rule (the 56px control floor, the shared cross-mode
 * form, a design token in UI-STYLE-v2), the test pins the
 * implemented value and AUDIT SS16 states the basis; no
 * reference value is claimed for those.
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

static uint32_t iconpx[24 * 24];

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
static int diff_count(tk_canvas *a, tk_canvas *b, tk_rect q)
{
    int n = 0;
    for (int y = q.y; y < q.y + q.h && y < 480; y++)
        for (int x = q.x; x < q.x + q.w && x < 800; x++)
            if (!col_eq(px(a, x, y), px(b, x, y))) n++;
    return n;
}
static tk_size meas(tk_widget *w)
{
    return tk_measure(w, (tk_constraints){0, TK_UNLIMITED,
                                          0, TK_UNLIMITED});
}
static void text_in(tk_root *r, const char *s)
{
    tk_event e = { 0 };
    e.type = TK_EV_TEXT_INPUT;
    e.text = s;
    tk_root_event(r, &e);
}

/* ---------- #2: button variants share the #1 geometry -- */
static void test_button_variants(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *b = tk_button_new("OK");
    tk_size ref = meas(b);
    tk_widget_free(b);
    CHECK(ref.h == 54); /* the 48dp-target control floor (M22) */
    static const tk_button_variant vs[] = {
        TK_BTN_ELEVATED, TK_BTN_TONAL, TK_BTN_OUTLINED,
        TK_BTN_TEXT
    };
    for (size_t i = 0; i < sizeof(vs) / sizeof(vs[0]); i++) {
        b = tk_button_new("OK");
        tk_button_set_variant(b, vs[i]);
        tk_size s = meas(b);
        CHECK(s.w == ref.w && s.h == ref.h);
        tk_widget_free(b);
    }
    b = tk_button_new("OK");
    tk_button_set_toggle(b, true);
    tk_size s = meas(b);
    CHECK(s.w == ref.w && s.h == ref.h);
    tk_widget_free(b);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #5: split button --------------------------- */
static void test_split(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *sp = tk_splitbutton_new("保存", NULL, NULL);
    tk_root *r = root_of(sp);
    tk_rect q = tk_widget_rect(sp);
    tk_rect m = tk_widget_rect(tk_splitbutton_main(sp));
    tk_rect a = tk_widget_rect(tk_splitbutton_arrow(sp));
    CHECK(q.h == 54);
    /* the M3 split gap is 2dp -> 2px; the parts tile the
     * whole: main + gap + chevron segment */
    CHECK(a.x - (m.x + m.w) == 2);
    CHECK(m.w + 2 + a.w == q.w);
    CHECK(m.h == 54 && a.h == 54);
    tk_root_free(r);
    tk_widget_free(sp);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #6/#7 (a) pins: glyph geometry ------------- */
/* M21: the M3-mode glyphs take the converted token sizes
 * (AUDIT SS19): checkbox box 21px (18dp at 187ppi), radio
 * ring 21px with a 9px dot at +6 (8dp). The WinUI glyphs
 * are the converted forms (28px box; the selected radio's
 * ring and dot are both accent, so the centre row is
 * accent across the box). AUDIT SS16.2. */
static void test_glyphs(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *cb = tk_checkbox_new("同意", false);
    tk_root *r = root_of(cb);
    tk_rect q = tk_widget_rect(cb);
    CHECK(q.h == 54);
    tk_canvas *c = paint_full(r);
    tk_color bg = px(c, 700, 460);
    int cy = q.y + q.h / 2;
    int f = -1, l = -1;
    for (int x = 0; x < 40; x++) {
        if (!col_eq(px(c, q.x + x, cy), bg)) {
            if (f < 0) f = x;
            l = x;
        }
    }
    CHECK(l - f + 1 == (winui ? 28 : 21));
    if (!winui) {
        int tf = -1, tl = -1;
        for (int y = 0; y < 40; y++) {
            if (!col_eq(px(c, q.x + 9, q.y + y), bg)) {
                if (tf < 0) tf = y;
                tl = y;
            }
        }
        CHECK(tl - tf + 1 == 21);
        /* the box is centred in the 54px row (M22 floor) */
        CHECK(tf == (54 - 21) / 2);
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cb);
    /* radio, selected: the dot */
    tk_widget *rd = tk_radio_new("選択", 1, 1);
    tk_radio_select(rd);
    r = root_of(rd);
    q = tk_widget_rect(rd);
    c = paint_full(r);
    cy = q.y + q.h / 2;
    f = -1; l = -1;
    for (int x = 0; x < 40; x++) {
        if (col_eq(px(c, q.x + x, cy), t->accent)) {
            if (f < 0) f = x;
            l = x;
        }
    }
    if (winui) {
        CHECK(f == 0 && l == 27);
    } else {
        CHECK(f == 6 && l - f + 1 == 9);
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(rd);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #6/#7 glyph SHAPES (M20 fix) ---------------- */
/* The M20 judgment-material screenshots exposed two M3
 * rendering defects that the dimension pins above and the
 * presence-only samples in tk12/tk13 could not see:
 *  - the checked Checkbox mark was a filled 10x10 square
 *    (a checked box read as an empty frame);
 *  - the RadioButton ring was stroked as a square over its
 *    circular fill (the glyph read as a rounded square).
 * The ListView checkable box shares the checkbox's box
 * language (21px in the M3 form since M21) and had the
 * same square mark. These checks pin
 * the SHAPES: a check has arms and empty corners, a ring is
 * round (corners clear, edge midpoints on the outline). */
static int count_col(tk_canvas *c, tk_rect b, tk_color col)
{
    int n = 0;
    for (int y = b.y; y < b.y + b.h; y++)
        for (int x = b.x; x < b.x + b.w; x++)
            if (col_eq(px(c, x, y), col)) n++;
    return n;
}

static void test_glyph_shapes(void)
{
    const tk_theme *t;
    /* --- M3 checkbox, checked: a check, not a square --- */
    tk_theme_set_default(&tk_theme_v2);
    t = tk_theme_default();
    tk_widget *cb = tk_checkbox_new("同意", true);
    tk_root *r = root_of(cb);
    tk_rect q = tk_widget_rect(cb);
    tk_rect box = { q.x, q.y + (q.h - 21) / 2, 21, 21 };
    tk_canvas *c = paint_full(r);
    int nmark = count_col(c, box, t->on_accent);
    /* the 21px check measures 38 stroke pixels (probed
     * 2026-11-21); a filled square of the glyph's extent
     * would be several times that */
    CHECK(nmark >= 20 && nmark <= 80);
    /* the old square's corners are accent fill now */
    CHECK(col_eq(px(c, box.x + 4, box.y + 4), t->accent));
    CHECK(col_eq(px(c, box.x + 13, box.y + 13), t->accent));
    /* both arms and the vertex are on-accent (21px glyph
     * geometry; M22: re-probed for the material3-ratio
     * path - the long arm's top end moved +1px right) */
    CHECK(col_eq(px(c, box.x + 5, box.y + 10), t->on_accent));
    CHECK(col_eq(px(c, box.x + 15, box.y + 6), t->on_accent));
    CHECK(col_eq(px(c, box.x + 7, box.y + 12), t->on_accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cb);

    /* --- M3 radio, selected: the ring is a circle --- */
    tk_widget *rd = tk_radio_new("選択", 1, 1);
    tk_radio_select(rd);
    r = root_of(rd);
    q = tk_widget_rect(rd);
    box = (tk_rect){ q.x, q.y + (q.h - 21) / 2, 21, 21 };
    c = paint_full(r);
    /* corners clear of the outline (a square stroke fails) */
    CHECK(!col_eq(px(c, box.x, box.y), t->border));
    CHECK(!col_eq(px(c, box.x + 20, box.y), t->border));
    CHECK(!col_eq(px(c, box.x, box.y + 20), t->border));
    CHECK(!col_eq(px(c, box.x + 20, box.y + 20), t->border));
    /* edge midpoints on the outline */
    CHECK(col_eq(px(c, box.x + 10, box.y), t->border));
    CHECK(col_eq(px(c, box.x, box.y + 10), t->border));
    CHECK(col_eq(px(c, box.x + 20, box.y + 10), t->border));
    CHECK(col_eq(px(c, box.x + 10, box.y + 20), t->border));
    /* the dot */
    CHECK(col_eq(px(c, box.x + 10, box.y + 10), t->accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(rd);

    /* --- M3 ListView checkable box: the same check glyph - */
    const char *items[] = { "項目1", "項目2" };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 2);
    tk_listview_set_checkable(lv, true);
    tk_listview_set_row_checked(lv, 1, true);
    r = root_of(lv);
    q = tk_widget_rect(lv);
    int rh = tk_listview_row_h(lv, t);
    tk_rect lbox = { q.x + 2 * t->unit + 2, /* int14: LV_PAD_X */
                     q.y + rh + (rh - 21) / 2, 21, 21 };
    c = paint_full(r);
    CHECK(col_eq(px(c, lbox.x + 4, lbox.y + 4), t->accent));
    CHECK(col_eq(px(c, lbox.x + 15, lbox.y + 6), t->on_accent));
    CHECK(col_eq(px(c, lbox.x + 7, lbox.y + 12), t->on_accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(lv);

    /* --- WinUI checkbox / radio: guard the correct forms - */
    tk_theme_set_default(&tk_theme_winui);
    t = tk_theme_default();
    cb = tk_checkbox_new("同意", true);
    r = root_of(cb);
    q = tk_widget_rect(cb);
    box = (tk_rect){ q.x, q.y + (q.h - 28) / 2, 28, 28 };
    c = paint_full(r);
    nmark = count_col(c, box, t->on_accent);
    CHECK(nmark >= 20 && nmark <= 120);
    CHECK(col_eq(px(c, box.x + 8, box.y + 16), t->on_accent));
    CHECK(col_eq(px(c, box.x + 20, box.y + 10), t->on_accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cb);

    rd = tk_radio_new("選択", 1, 1);
    tk_radio_select(rd);
    r = root_of(rd);
    q = tk_widget_rect(rd);
    box = (tk_rect){ q.x, q.y + (q.h - 28) / 2, 28, 28 };
    c = paint_full(r);
    CHECK(!col_eq(px(c, box.x, box.y), t->accent));
    CHECK(!col_eq(px(c, box.x + 27, box.y + 27), t->accent));
    CHECK(col_eq(px(c, box.x + 14, box.y), t->accent));
    CHECK(col_eq(px(c, box.x + 14, box.y + 14), t->accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(rd);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #9/#10 slider geometry --------------------- */
/* M3 mode (a): the track (16px) and thumb (28px) follow
 * the toolkit's documented design tokens (UI-STYLE-v2);
 * the value bubble is the converted 48x44dp form (code
 * comment in the painter). WinUI: track 4epx -> 6px,
 * thumb 24px (M22: 18epx at x1.344). */
static void test_slider(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sl = tk_slider_new(0.5f);
    tk_size s = meas(sl);
    CHECK(s.w == 120 && s.h == 54);
    tk_root *r = root_of(sl);
    tk_rect q = tk_widget_rect(sl);
    tk_canvas *c = paint_full(r);
    int cy = q.y + q.h / 2;
    /* track height at a column clear of the thumb */
    int rows = 0;
    for (int y = 0; y < q.h; y++)
        if (col_eq(px(c, q.x + 10, q.y + y), t->accent)) rows++;
    CHECK(rows == (winui ? 6 : 16));
    /* thumb diameter: the centre column's painted run -
     * the thumb is taller than the track, so that column
     * crosses the thumb alone */
    tk_color bg = px(c, 700, 460);
    int rows_c = 0;
    for (int y = 0; y < q.h; y++)
        if (!col_eq(px(c, q.x + q.w / 2, q.y + y), bg)) rows_c++;
    CHECK(rows_c == (winui ? 24 : 28));
    /* M21: the thumb centre is the accent dot in both modes
     * (the WinUI inner dot was missing entirely), and the
     * WinUI inactive span is the strong fill - distinct
     * from both the page background and the old surface_alt
     * wash, which was invisible on the page. The M3
     * inactive span stays surfaceContainerHighest
     * (== surface_alt). */
    CHECK(col_eq(px(c, q.x + q.w / 2, cy), t->accent));
    {
        tk_color ina = px(c, q.x + q.w - 5, cy);
        if (winui) {
            CHECK(!col_eq(ina, t->surface_alt));
            CHECK(!col_eq(ina, bg));
        } else {
            CHECK(col_eq(ina, t->surface_alt));
        }
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sl);
    /* M21: a disabled WinUI slider stays visible - the
     * spans and the thumb take the strong-fill disabled
     * gray, not the near-white disabled fill wash */
    if (winui) {
        sl = tk_slider_new(0.5f);
        tk_set_enabled(sl, false);
        r = root_of(sl);
        q = tk_widget_rect(sl);
        c = paint_full(r);
        cy = q.y + q.h / 2;
        CHECK(!col_eq(px(c, q.x + q.w - 5, cy), bg));
        CHECK(!col_eq(px(c, q.x + q.w / 2, cy), bg));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(sl);
    }
    /* dual (Range): two thumbs at the value positions,
     * the span between them on the active track. A
     * column's painted run is the track height off-thumb
     * and the thumb diameter on-thumb, so the thumb
     * zones are the column runs above 20px. */
    sl = tk_slider_new(0.5f);
    tk_slider_set_values2(sl, 0.25f, 0.75f);
    r = root_of(sl);
    q = tk_widget_rect(sl);
    c = paint_full(r);
    cy = q.y + q.h / 2;
    bg = px(c, 700, 460);
    int zone_c[2] = { -1, -1 }, nz = 0, run_start = -1;
    for (int x = 0; x <= q.w; x++) {
        int cnt = 0;
        if (x < q.w)
            for (int y = 0; y < q.h; y++)
                if (!col_eq(px(c, q.x + x, q.y + y), bg)) cnt++;
        bool in = x < q.w && cnt >= 20;
        if (in && run_start < 0) run_start = x;
        if (!in && run_start >= 0) {
            if (nz < 2) zone_c[nz++] = (run_start + x - 1) / 2;
            run_start = -1;
        }
    }
    CHECK(nz == 2);
    CHECK(abs(zone_c[0] - q.w / 4) <= 2);
    CHECK(abs(zone_c[1] - 3 * q.w / 4) <= 2);
    CHECK(col_eq(px(c, q.x + q.w / 2, cy), t->accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sl);
    tk_theme_set_default(&tk_theme_v2);
}

static void send_ptr17(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event e = { 0 };
    e.type = ty; e.x = x; e.y = y;
    tk_root_event(r, &e);
}

/* ---------- M22: WinUI slider dot state scales ---------- */
/* The template's inner dot (12epx) scales 0.86 at rest,
 * 1.167 on hover, 0.71 pressed (M21 SS5.1 extraction);
 * at x1.344 that is 14 / 19 / 11px. The dot covers the
 * track at the thumb's centre column, so the accent run
 * through the centre row measures the dot diameter. */
static int dot_run(tk_canvas *c, int cx, int cy, tk_color accent)
{
    int n = 0;
    for (int y = cy; col_eq(px(c, cx, y), accent); y++) n++;
    for (int y = cy - 1; col_eq(px(c, cx, y), accent); y--) n++;
    return n;
}
static void test_slider_dot_scales(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    tk_widget *sl = tk_slider_new(0.5f);
    /* a deep root: the drag value bubble clamps to the
     * canvas top and would cover the thumb's top in the
     * shallow root_of layout (pre-existing behaviour) */
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_add_child(box, tk_label_new(" "));
    tk_add_child(box, tk_label_new(" "));
    tk_add_child(box, tk_label_new(" "));
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
    tk_add_child(row, tk_label_new("    "));
    tk_add_child(row, sl);
    tk_add_child(box, row);
    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(sl);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_canvas *c = paint_full(r);
    CHECK(dot_run(c, cx, cy, t->accent) == 14);   /* rest */
    tk_canvas_free(c);
    send_ptr17(r, TK_EV_POINTER_MOVE, cx, cy);    /* hover */
    c = paint_full(r);
    CHECK(dot_run(c, cx, cy, t->accent) == 19);
    tk_canvas_free(c);
    send_ptr17(r, TK_EV_POINTER_DOWN, cx, cy);    /* pressed */
    c = paint_full(r);
    CHECK(dot_run(c, cx, cy, t->accent) == 11);
    tk_canvas_free(c);
    send_ptr17(r, TK_EV_POINTER_UP, cx, cy);
    tk_root_free(r);
    tk_widget_free(sl);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- M22: M3 switch knob state diameters --------- */
/* material3 Switch (M21 SS5.3): thumb 16dp unselected,
 * 24dp selected, 28dp pressed; at 1.12px/dp that is
 * 18 / 27 / 31px. The knob is the surface colour on the
 * centre row; its span there is the diameter. */
static int knob_span(tk_canvas *c, tk_rect q, tk_color knob)
{
    int cy = q.y + q.h / 2, f = -1, l = -1;
    for (int x = q.x; x < q.x + 70; x++)
        if (col_eq(px(c, x, cy), knob)) {
            if (f < 0) f = x;
            l = x;
        }
    return f < 0 ? 0 : l - f + 1;
}
static void test_switch_knob_sizes(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    tk_widget *sw = tk_switch_new("", false);
    tk_root *r = root_of(sw);
    tk_rect q = tk_widget_rect(sw);
    int cy = q.y + q.h / 2;
    tk_canvas *c = paint_full(r);
    CHECK(knob_span(c, q, t->surface) == 18);     /* off */
    tk_canvas_free(c);
    send_ptr17(r, TK_EV_POINTER_DOWN, q.x + 20, cy);
    c = paint_full(r);
    CHECK(knob_span(c, q, t->surface) == 31);     /* pressed */
    tk_canvas_free(c);
    send_ptr17(r, TK_EV_POINTER_UP, q.x + 20, cy);
    tk_root_tick(r, 5000);                        /* slide settles */
    c = paint_full(r);
    CHECK(knob_span(c, q, t->surface) == 27);     /* on */
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sw);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #12: ring / spinner ------------------------ */
static void test_ring(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *rg = tk_ring_new(0.25f);
    tk_size s = meas(rg);
    /* M20: the unset default is the theme's diameter - M3:
     * CircularProgressIndicatorTokens.Size 40dp = 47px
     * (re-verified 2026-11-21); WinUI: the toolkit's 48px
     * form (no size token on ProgressRing) */
    CHECK(s.w == (winui ? 48 : 47) && s.h == (winui ? 48 : 47));
    tk_root *r = root_of(rg);
    tk_rect q = tk_widget_rect(rg);
    tk_canvas *c = paint_full(r);
    /* stroke at the arc's top crossing (the 25% arc runs
     * from the top): the nominal stroke is 4dp -> 5px
     * (M3) / 4epx -> 6px (WinUI). M21: ring_arc rounds its
     * stamp centres instead of truncating them, which
     * centres the ink band on the nominal path - the
     * band at a crossing now measures the nominal stroke
     * exactly (the pre-M21 band was nominal + 1, recorded
     * as a rasterizer residual in AUDIT SS18.2 and
     * corrected in SS19) */
    int n = 0;
    for (int y = q.h / 2 - 26; y < q.h / 2 - 13; y++)
        if (col_eq(px(c, q.x + q.w / 2, q.y + y), t->accent)) n++;
    CHECK(n == (winui ? 6 : 5));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(rg);
    tk_widget *sp = tk_spinner_new(0);
    s = meas(sp);
    CHECK(s.w == 40 && s.h == 40);
    tk_widget_free(sp);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #21 D (M20): the menu scale component ----- */
/* The scaled blit itself: bilinear values, identity,
 * clipping. The source is a 4x4 ramp, r = x * 40, g = y * 40,
 * so every sample is predictable. */
static void test_blit_scaled(void)
{
    tk_canvas *src = tk_canvas_new(4, 4);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            tk_canvas_fill_rect(src, (tk_rect){x, y, 1, 1},
                (tk_color){ (uint8_t)(x * 40), (uint8_t)(y * 40),
                            7, 255 });
    /* identity: a same-size blit is an exact copy */
    tk_canvas *dst = tk_canvas_new(8, 8);
    tk_canvas_clear(dst, (tk_color){ 9, 9, 9, 255 });
    tk_canvas_blit_scaled(dst, src, (tk_rect){0, 0, 4, 4},
                          (tk_rect){2, 2, 4, 4});
    CHECK(col_eq(px(dst, 2, 2), (tk_color){ 0, 0, 7, 255 }));
    CHECK(col_eq(px(dst, 5, 5), (tk_color){ 120, 120, 7, 255 }));
    CHECK(col_eq(px(dst, 0, 0), (tk_color){ 9, 9, 9, 255 }));
    /* 2x upscale: dst pixel (3,3) of an 8x8 target samples
     * the source at (1.25, 1.25): r = 40 * 1.25 = 50 */
    tk_canvas *up = tk_canvas_new(8, 8);
    tk_canvas_blit_scaled(up, src, (tk_rect){0, 0, 4, 4},
                          (tk_rect){0, 0, 8, 8});
    CHECK(col_eq(px(up, 3, 3), (tk_color){ 50, 50, 7, 255 }));
    CHECK(col_eq(px(up, 0, 0), (tk_color){ 0, 0, 7, 255 }));
    CHECK(col_eq(px(up, 7, 7), (tk_color){ 120, 120, 7, 255 }));
    /* 2x downscale: dst pixel (1,1) of a 2x2 target samples
     * (2.5, 2.5) clamped to the edge pixel (3,3) centre-side:
     * the sample point is 2.5 -> between 2 and 3 at 0.5 ->
     * r = (80 + 120) / 2 = 100 */
    tk_canvas *dn = tk_canvas_new(2, 2);
    tk_canvas_blit_scaled(dn, src, (tk_rect){0, 0, 4, 4},
                          (tk_rect){0, 0, 2, 2});
    CHECK(col_eq(px(dn, 1, 1), (tk_color){ 100, 100, 7, 255 }));
    /* the clip stack bounds the write, and the recorded
     * damage stays inside it */
    tk_canvas *cl = tk_canvas_new(8, 8);
    tk_canvas_clear(cl, (tk_color){ 9, 9, 9, 255 });
    tk_canvas_damage_clear(cl);
    tk_canvas_push_clip(cl, (tk_rect){0, 0, 4, 8});
    tk_canvas_blit_scaled(cl, src, (tk_rect){0, 0, 4, 4},
                          (tk_rect){0, 0, 8, 8});
    tk_canvas_pop_clip(cl);
    CHECK(col_eq(px(cl, 7, 7), (tk_color){ 9, 9, 9, 255 }));
    CHECK(tk_canvas_damage_bounds(cl).w <= 4);
    tk_canvas_free(src); tk_canvas_free(dst); tk_canvas_free(up);
    tk_canvas_free(dn); tk_canvas_free(cl);
}

/* The menu entrance/exit geometry: the painted surface's
 * footprint (backdrop-diff bounding box, the focus ring's
 * 3px frame excluded) grows from the anchor corner, is
 * strictly smaller than the settled surface mid-transition,
 * and the settled frame is byte-identical to the same menu
 * rendered without an animation clock. Driven through
 * tk_root_paint_damage, the presentation path apps use. */
static tk_rect diff_bbox(tk_canvas *a, tk_canvas *b, tk_rect zone)
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    for (int y = zone.y; y < zone.y + zone.h && y < 480; y++)
        for (int x = zone.x; x < zone.x + zone.w && x < 800; x++)
            if (!col_eq(px(a, x, y), px(b, x, y))) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) return (tk_rect){0, 0, 0, 0};
    return (tk_rect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

static void menu_scale_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    /* reference: no live clock, the menu appears in its
     * settled form directly */
    tk_widget *host0 = tk_button_new("主");
    tk_root *r0 = root_of(host0);
    tk_widget *m0 = tk_menu_new();
    tk_menu_add_item(m0, "ファイルを開く", 10);
    tk_menu_add_item(m0, "名前を付けて保存する", 20);
    tk_menu_add_item(m0, "終了", 30);
    tk_menu_popup(r0, m0, 100, 100);
    tk_canvas *cref = paint_full(r0);
    tk_rect q0 = tk_widget_rect(m0);
    /* animated run, presented through the damage path */
    tk_widget *host = tk_button_new("主");
    tk_root *r = root_of(host);
    tk_canvas *c = tk_canvas_new(800, 480);
    tk_root_paint_damage(r, c, theme); /* full first frame */
    tk_canvas *cback = tk_canvas_new(800, 480);
    tk_root_paint(r, cback, theme);    /* backdrop, no menu */
    tk_widget *m = tk_menu_new();
    tk_menu_add_item(m, "ファイルを開く", 10);
    tk_menu_add_item(m, "名前を付けて保存する", 20);
    tk_menu_add_item(m, "終了", 30);
    tk_root_tick(r, 1000);
    tk_menu_popup(r, m, 100, 100);
    tk_rect q = tk_widget_rect(m);
    CHECK(q.w == q0.w && q.h == q0.h);
    if (!winui) {
        /* M3: mid entrance (+30 ms of the 120 ms pair): the
         * footprint is anchored at the rect's top-left (the
         * popup origin) and strictly smaller than the
         * settled surface. The scan zone is the rect's
         * middle band, re-read from the CURRENT rect at each
         * sample: the focus ring around the menu follows
         * the list radius, so its corner arcs intrude
         * several px into the rect's top/bottom rows - the
         * middle band is the ring-free region (its straight
         * segments sit on the rect's own edges, outside the
         * 3px x-inset). */
        tk_root_tick(r, 1030);
        tk_root_paint_damage(r, c, theme);
        q = tk_widget_rect(m);
        tk_rect zone = { q.x + 3, q.y + q.h / 3, q.w - 6, q.h / 3 };
        tk_rect bb = diff_bbox(c, cback, zone);
        CHECK(bb.w > 0);
        CHECK(bb.x <= zone.x + 1);        /* grows from origin */
        CHECK(bb.x + bb.w < zone.x + zone.w - 4); /* not settled */
        int mid_w = bb.w;
        /* later (+60 ms): still growing */
        tk_root_tick(r, 1060);
        tk_root_paint_damage(r, c, theme);
        q = tk_widget_rect(m);
        zone = (tk_rect){ q.x + 3, q.y + q.h / 3, q.w - 6, q.h / 3 };
        bb = diff_bbox(c, cback, zone);
        CHECK(bb.w > mid_w);
    } else {
        /* WinUI: the pop-in form is fade + translation (no
         * scale in the primary sources): mid entrance the
         * surface is mid-fade (differs from BOTH the
         * backdrop and the settled render) and the slide
         * has not landed yet */
        tk_root_tick(r, 1060);
        tk_root_paint_damage(r, c, theme);
        q = tk_widget_rect(m);
        CHECK(q.y < 100);                 /* slide in flight */
        CHECK(diff_count(c, cref, q) > 20);
        CHECK(diff_count(c, cback, q) > 20);
    }
    /* settled (+400 ms): byte-identical to the reference
     * render over the whole canvas - the animation left no
     * residue and the rest form is the direct paint */
    tk_root_tick(r, 1400);
    tk_root_paint_damage(r, c, theme);
    CHECK(diff_count(c, cref, (tk_rect){0, 0, 800, 480}) == 0);
    /* exit: mid-exit the surface is animating again (M3:
     * the footprint shrinks below the settled surface;
     * WinUI: the fade runs), then the menu is gone */
    tk_menu_close(r, m);
    CHECK(tk_menu_is_open(r, m));
    tk_root_tick(r, 1430);
    tk_root_paint_damage(r, c, theme);
    q = tk_widget_rect(m);
    if (!winui) {
        tk_rect zone = { q.x + 3, q.y + q.h / 3, q.w - 6, q.h / 3 };
        tk_rect bb = diff_bbox(c, cback, zone);
        CHECK(bb.w > 0 && bb.x + bb.w < zone.x + zone.w - 4);
    } else {
        CHECK(diff_count(c, cref, q) > 20);
        CHECK(diff_count(c, cback, q) > 20);
    }
    tk_root_tick(r, 1800);
    tk_root_paint_damage(r, c, theme);
    CHECK(!tk_menu_is_open(r, m));
    CHECK(diff_count(c, cback, (tk_rect){0, 0, 800, 480}) == 0);
    tk_canvas_free(cref); tk_canvas_free(c); tk_canvas_free(cback);
    tk_root_free(r0); tk_root_free(r);
    tk_widget_free(host0); tk_widget_free(host);
    tk_widget_free(m0); tk_widget_free(m);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #13/#14/#15 fields and text ---------------- */
static void test_fields(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sb = tk_searchbar_new("検索");
    tk_size s = meas(sb);
    /* M18 fix (F1): the M3 SearchBar container is 56dp =
     * 65px; the WinUI AutoSuggestBox keeps the TextBox
     * floor form (54px since the M22 floor change) */
    CHECK(s.h == (winui ? 54 : 65));
    CHECK(s.w == 280);
    tk_widget_free(sb);
    tk_widget *tf = tk_textfield_new("x");
    s = meas(tf);
    CHECK(s.h == 54);
    tk_widget_free(tf);
    /* TextView: height is lines x the body line height
     * (font metrics) + 8 - the formula both modes share;
     * the type-ramp collation itself is in AUDIT SS16.3 */
    int lh = tk_line_height_st(t, TK_TEXT_BODY);
    tk_widget *tv = tk_textview_new("あ");
    s = meas(tv);
    CHECK(s.h == lh + 8);
    tk_widget_free(tv);
    tv = tk_textview_new("あ\nい");
    s = meas(tv);
    CHECK(s.h == 2 * lh + 8);
    tk_widget_free(tv);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #15 D: the suggestion dropdown entrance --- */
/* The dropdown is the toolkit's menu overlay (tk_m7
 * suggest path -> tk_menu_popup), so its appearance is
 * the menu entrance: a fade from transparent over the
 * mode's small-surface pair. What is NOT implemented is
 * the M3 SearchView's expanding-surface form (AUDIT
 * SS16.5) - this test pins the part that exists. */
static void suggest_fade_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *sb = tk_searchbar_new("検索");
    const char *items[] = { "apple", "apricot", "banana" };
    tk_searchbar_set_suggestions(sb, items, 3);
    tk_root *r = root_of(sb);
    tk_rect q = tk_widget_rect(sb);
    tk_root_tick(r, 1000); /* live clock */
    CHECK(tk_focus(r, sb));
    text_in(r, "ap");
    CHECK(tk_searchbar_suggestions_open(sb));
    tk_rect zone = { q.x, q.y + q.h, q.w, 140 };
    tk_canvas *c_in = paint_full(r);
    tk_root_tick(r, 1400); /* entrance complete (<=167 ms) */
    tk_canvas *c_set = paint_full(r);
    CHECK(diff_count(c_in, c_set, zone) > 20);
    tk_canvas_free(c_in);
    tk_canvas_free(c_set);
    tk_root_free(r);
    tk_widget_free(sb);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #18: card ---------------------------------- */
static void test_card(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *cd = tk_card_new(TK_CARD_OUTLINED);
    tk_widget *cl = tk_label_new("見出し");
    tk_add_child(cd, cl);
    tk_root *r = root_of(cd);
    tk_rect q = tk_widget_rect(cd);
    tk_rect k = tk_widget_rect(cl);
    /* content padding 19px (16dp) on both axes, both
     * modes (the WinUI card follows the same toolkit
     * padding rule; there is no first-party WinUI Card
     * control - AUDIT SS16.4) */
    CHECK(k.x - q.x == 19 && k.y - q.y == 19);
    tk_canvas *c = paint_full(r);
    tk_color bg = px(c, 700, 460);
    bool ink22 = !col_eq(px(c, q.x + 2, q.y + 2), bg);
    bool ink55 = !col_eq(px(c, q.x + 5, q.y + 5), bg);
    if (winui) {
        /* corner radius 6 (ControlCornerRadius): the
         * (2,2) point is inside the fill; radius 11
         * (overlay) would leave it outside */
        CHECK(ink22);
    } else {
        /* corner radius 14 (12dp): (2,2) outside the
         * fill, (5,5) inside - radius 24 (the theme's
         * card token) would leave (5,5) outside too */
        CHECK(!ink22 && ink55);
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cd);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #20: treeview ------------------------------ */
static void test_treeview(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *tr = tk_treeview_new();
    int n0 = tk_treeview_add(tr, -1, "root");
    tk_treeview_add(tr, n0, "child");
    tk_treeview_add(tr, -1, "second");
    tk_size s = meas(tr);
    /* rows follow the toolkit row height (the ListView
     * row basis); the indent has no M3 counterpart and
     * the WinUI content minimum (20epx -> 29px) is under
     * the row - AUDIT SS16.4 */
    CHECK(s.h == 3 * t->row_h);
    CHECK(tk_treeview_visible_rows(tr) == 3);
    tk_widget_free(tr);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #23/#24: dialogs --------------------------- */
static void dialog_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("h");
    tk_root *r = root_of(host);
    tk_widget *d = tk_dialog_new("確認");
    tk_widget *dl = tk_label_new("本文テキスト");
    tk_add_child(d, dl);
    tk_dialog_show(r, d);
    tk_rect basic = tk_widget_rect(d);
    int basic_child_dy = tk_widget_rect(dl).y - basic.y;
    tk_dialog_close(r, d);
    tk_widget *d2 = tk_dialog_new("確認");
    tk_add_child(d2, tk_label_new("本文テキスト"));
    tk_dialog_show_modeless(r, d2);
    tk_rect modeless = tk_widget_rect(d2);
    /* the modeless form is the basic form without the
     * modal behaviour: identical geometry */
    CHECK(modeless.x == basic.x && modeless.y == basic.y &&
          modeless.w == basic.w && modeless.h == basic.h);
    tk_dialog_close(r, d2);
    if (winui) {
        /* ContentDialog clamps: min width 320epx ->
         * 458px, min height 184epx -> 263px */
        CHECK(basic.w == 458 && basic.h == 263);
    } else {
        /* M3 basic dialog min width 280dp -> 327px */
        CHECK(basic.w == 327);
    }
    CHECK(basic_child_dy == (winui ? 88 : 82)); /* M22: title
                                    * band follows control_h */
    /* the width cap with wide content */
    tk_widget *d3 = tk_dialog_new("確認");
    tk_add_child(d3, tk_label_new(
        "ああああああああああああああああああああ"
        "ああああああああああああああああああああ"
        "ああああああああああああああああああああ"));
    tk_dialog_show(r, d3);
    tk_rect wide = tk_widget_rect(d3);
    /* M3 max 560dp -> 655px; WinUI max 548epx -> 784px */
    CHECK(wide.w == (winui ? 784 : 655));
    tk_dialog_close(r, d3);
    /* full-screen: fills the root; the content starts
     * below header + padding. M18 fix (F4): the M3
     * full-screen header is 56dp = 65px. */
    tk_widget *d4 = tk_dialog_new("確認");
    tk_widget *d4l = tk_label_new("本文");
    tk_add_child(d4, d4l);
    tk_dialog_set_fullscreen(d4, true);
    tk_dialog_show(r, d4);
    tk_rect fs = tk_widget_rect(d4);
    CHECK(fs.x == 0 && fs.y == 0 && fs.w == 800 && fs.h == 480);
    CHECK(tk_widget_rect(d4l).y == (winui ? 88 : 93));
    tk_dialog_close(r, d4);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #25: date / time fields -------------------- */
static void test_pickers(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *dp = tk_datepicker_new(2026, 11, 20);
    tk_size s = meas(dp);
    CHECK(s.h == 54); /* the field floor, both modes */
    tk_widget_free(dp);
    tk_widget *tp = tk_timepicker_new(false);
    s = meas(tp);
    CHECK(s.h == 54);
    tk_widget_free(tp);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #26: snackbar ------------------------------ */
static void snack_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("h");
    tk_root *r = root_of(host);
    tk_snackbar_show(r, "保存しました", NULL, NULL, NULL, 4000);
    tk_canvas *c = paint_full(r);
    tk_color bg = px(c, 700, 60);
    int top = -1, bot = -1, lf = -1, rt = -1;
    for (int y = 300; y < 480; y++)
        for (int x = 0; x < 800; x++) {
            if (col_eq(px(c, x, y), bg)) continue;
            if (top < 0) top = y;
            bot = y;
            if (lf < 0 || x < lf) lf = x;
            if (x > rt) rt = x;
        }
    /* M3 single-line container 48dp -> 54px; the WinUI
     * InfoBar form is 65px (48epx MinHeight) - both at
     * the M22 premise. Margins 16px and the 560px width
     * cap are the toolkit's host rules (the code's own
     * constants). */
    CHECK(bot - top + 1 == (winui ? 65 : 54));
    CHECK(rt - lf + 1 == 560);
    CHECK(lf == 16);
    CHECK(480 - (bot + 1) == 16);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #27: toast (AOSP shape, M3 mode) ----------- */
static void test_toast_shape(void)
{
    tk_theme_set_default(&tk_theme_v2);
    tk_widget *host = tk_label_new("h");
    tk_root *r = root_of(host);
    tk_toast_show(r, "保存しました", 4000);
    tk_canvas *c = paint_full(r);
    tk_color bg = px(c, 700, 60);
    int top = -1, bot = -1, lf = -1, rt = -1;
    for (int y = 300; y < 480; y++)
        for (int x = 0; x < 800; x++) {
            if (col_eq(px(c, x, y), bg)) continue;
            if (top < 0) top = y;
            bot = y;
            if (lf < 0 || x < lf) lf = x;
            if (x > rt) rt = x;
        }
    /* caption text 6 glyphs x 13px = 78 + padding
     * 19px/side = 116; height = caption line 17 +
     * 14px/side = 45; bottom offset 48dp -> 56px;
     * centred horizontally (M22: caption 14 -> 13px) */
    CHECK(rt - lf + 1 == 116);
    CHECK(bot - top + 1 == 45);
    CHECK(480 - (bot + 1) == 56);
    CHECK(lf == (800 - 116) / 2);
    tk_canvas_free(c);
    tk_toast_hide(r);
    /* the max width: 300dp -> 351px */
    tk_toast_show(r,
        "ああああああああああああああああああああ"
        "ああああああああああああああああああああ", 4000);
    c = paint_full(r);
    lf = -1; rt = -1;
    for (int y = 300; y < 480; y++)
        for (int x = 0; x < 800; x++) {
            if (col_eq(px(c, x, y), bg)) continue;
            if (lf < 0 || x < lf) lf = x;
            if (x > rt) rt = x;
        }
    CHECK(rt - lf + 1 == 351);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    /* the WinUI form keeps the toolkit geometry (the
     * Windows toast is an OS notification surface; there
     * is no in-app reference shape - AUDIT SS16.6) */
    tk_theme_set_default(&tk_theme_winui);
    host = tk_label_new("h");
    r = root_of(host);
    tk_toast_show(r, "保存しました", 4000);
    c = paint_full(r);
    bg = px(c, 700, 60);
    top = -1; bot = -1; lf = -1; rt = -1;
    for (int y = 300; y < 480; y++)
        for (int x = 0; x < 800; x++) {
            if (col_eq(px(c, x, y), bg)) continue;
            if (top < 0) top = y;
            bot = y;
            if (lf < 0 || x < lf) lf = x;
            if (x > rt) rt = x;
        }
    CHECK(rt - lf + 1 == 146); /* 6 x 19px body + 32 (M22) */
    CHECK(bot - top + 1 == 41); /* line 25 + 16 */
    CHECK(480 - (bot + 1) == 24);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #28: tab bar ------------------------------- */
static void test_tabbar(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *tb = tk_tabbar_new();
    tk_tabbar_add(tb, "曲");
    tk_tabbar_add(tb, "本");
    tk_size s = meas(tb);
    CHECK(s.h == 54); /* text-only: the 48dp target */
    tk_tabbar_set_item_icon(tb, 0, iconpx, 24, 24);
    s = meas(tb);
    /* M18 fix (F2): the M3 icon+label tab container is
     * 64dp = 75px; the WinUI form keeps the toolkit's
     * two-line row */
    CHECK(s.h == (winui ? 73 : 75)); /* M22: WinUI row_h2 */
    tk_widget_free(tb);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #4: FAB menu ------------------------------- */
static void fabmenu_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("h");
    tk_root *r = root_of(host);
    tk_widget *fm = tk_fabmenu_new();
    tk_fabmenu_add_item(fm, iconpx, 24, 24, "新規", NULL, NULL);
    tk_fabmenu_add_item(fm, iconpx, 24, 24, "開く", NULL, NULL);
    tk_fabmenu_show(r, fm);
    tk_rect q = tk_widget_rect(fm);
    tk_rect i0 = tk_widget_rect(tk_child_at(fm, 0));
    tk_rect i1 = tk_widget_rect(tk_child_at(fm, 1));
    /* items are tonal buttons at the control height
     * (the #1/#2 basis); the item gap is 4dp -> 5px;
     * the box padding is 8px */
    CHECK(i0.h == 54 && i1.h == 54);
    CHECK(i1.y - (i0.y + i0.h) == 5);
    CHECK(q.h == 8 + 54 + 5 + 54 + 8);
    CHECK(q.w == (winui ? 118 : 112)); /* M22: WinUI text
                                    * at the 19px body */
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(fm);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #29/#30/#31: bars, rail, drawer ------------ */
static void test_bars(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    /* bottom bar: cell-content-driven height (icon 24 +
     * label line + cell padding, + the bar's 2px x2).
     * The WinUI figure follows its larger type ramp. */
    tk_widget *bb = tk_bottombar_new();
    tk_bottombar_add_cell(bb, iconpx, 24, 24, "ホーム", NULL, NULL);
    tk_bottombar_add_cell(bb, iconpx, 24, 24, "設定", NULL, NULL);
    tk_root *r = root_of(bb);
    CHECK(tk_widget_rect(bb).h == (winui ? 77 : 73));
    tk_root_free(r);
    tk_widget_free(bb);
    /* nav rail: the shared M3 rail form in both modes -
     * 80dp -> 94px wide, 64px cells (AUDIT SS16.4: the
     * WinUI NavigationView compact pane, 48epx -> 69px,
     * is not adopted) */
    tk_widget *nr = tk_navrail_new();
    tk_navrail_add(nr, "ホーム", NULL, 0, 0);
    tk_navrail_add(nr, "設定", NULL, 0, 0);
    tk_size s = meas(nr);
    CHECK(s.w == 94 && s.h == 2 * 64 + 16);
    tk_widget_free(nr);
    /* drawer: the panel cap is the shared 360dp ->
     * 421px form in both modes (the WinUI expanded
     * pane length is not adopted - AUDIT SS16.4) */
    tk_widget *host = tk_label_new("h");
    r = root_of(host);
    tk_widget *dw = tk_drawer_new("メニュー");
    tk_drawer_add_item(dw,
        "ああああああああああああああああ"
        "ああああああああああああああああ", 1, NULL, 0, 0);
    tk_drawer_show(r, dw, true);
    tk_canvas *c = paint_full(r);
    tk_color bg = px(c, 700, 460);
    int rt = -1;
    for (int x = 0; x < 800; x++)
        if (!col_eq(px(c, x, 200), bg)) rt = x;
    CHECK(rt + 1 == 421);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(dw);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #32: bottom sheet -------------------------- */
static void sheet_flow(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *host = tk_label_new("h");
    tk_root *r = root_of(host);
    tk_widget *sh = tk_sheet_new();
    tk_add_child(sh, tk_label_new("シート本文"));
    tk_sheet_show(r, sh, true);
    tk_rect q = tk_widget_rect(sh);
    /* width: full width up to the 640dp cap -> 748px,
     * centred (x = 26 on an 800px root) */
    CHECK(q.w == 748 && q.x == 26);
    tk_canvas *c = paint_full(r);
    /* the drag handle: 32dp x 4dp -> 37 x 5px (M18 fix
     * F6), centred, 8px below the top edge. Scan the
     * handle row's middle band for non-face pixels;
     * the face is sampled on the same row, clear of
     * the handle and the focus ring. */
    tk_color face = px(c, q.x + 100, q.y + 10);
    int f = -1, l = -1;
    for (int x = q.w / 2 - 60; x < q.w / 2 + 60; x++) {
        if (!col_eq(px(c, q.x + x, q.y + 10), face)) {
            if (f < 0) f = x;
            l = x;
        }
    }
    CHECK(l - f + 1 == 37);
    CHECK(abs((f + l) / 2 - q.w / 2) <= 1);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(sh);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #33: segmented ----------------------------- */
static void test_segmented(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "日");
    tk_segmented_add(sg, "週");
    tk_segmented_add(sg, "月");
    tk_size s = meas(sg);
    /* M18 fix (F3): the M3 outlined segmented height is
     * 40dp = 47px; the WinUI connected-toggle form keeps
     * the toolkit's 48px */
    CHECK(s.h == (winui ? 48 : 47));
    CHECK(s.w % 3 == 0); /* equal segments */
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #34: carousel ------------------------------ */
static void test_carousel(const tk_theme *theme)
{
    tk_theme_set_default(theme);
    tk_widget *ca = tk_carousel_new();
    tk_carousel_add(ca, "a", NULL, 0, 0);
    tk_carousel_add(ca, "b", NULL, 0, 0);
    tk_carousel_add(ca, "c", NULL, 0, 0);
    tk_size s = meas(ca);
    CHECK(s.w == 360 && s.h == 112); /* natural size */
    tk_root *r = root_of(ca);
    tk_rect q = tk_widget_rect(ca);
    tk_canvas *c = paint_full(r);
    /* at rest the hero item is 248px wide, followed by
     * the 8dp -> 9px gap (the multi-browse strategy's
     * geometry; the reference defines no fixed item
     * size - AUDIT SS16.4) */
    tk_color face = px(c, q.x + 10, q.y + q.h / 2);
    int brk = -1;
    for (int x = 0; x < q.w; x++) {
        if (!col_eq(px(c, q.x + x, q.y + q.h / 2), face)) {
            brk = x;
            break;
        }
    }
    CHECK(brk == 248);
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(ca);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #35: color picker (composed form) ---------- */
static void test_colorpicker(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *cp = tk_colorpicker_new("色", 0xff336699u);
    tk_size s = meas(cp);
    /* a composed dialog (swatch grid + sliders): its
     * size is the dialog content's, pinned as the
     * toolkit form - the M3 spec has no colour picker
     * and the WinUI control's spectrum form differs
     * (AUDIT SS16.6) */
    CHECK(s.w == (winui ? 404 : 392));
    CHECK(s.h == (winui ? 530 : 518)); /* M22: seven
                                    * control rows at -2px */
    tk_widget_free(cp);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- #3/#16/#17 small forms --------------------- */
static void test_small_forms(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    tk_widget *fb = tk_fab_new(iconpx, 24, 24, 0);
    tk_size s = meas(fb);
    CHECK(s.w == 65 && s.h == 65); /* the shared FAB form
                                    * (M21: the converted
                                    * 56dp = 65px; AUDIT
                                    * SS19) */
    tk_widget_free(fb);
    tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
    s = meas(ch);
    CHECK(s.h == (winui ? 40 : 37)); /* M21: the M3 chip
                                    * takes the converted
                                    * 32dp = 37px; the WinUI
                                    * form keeps the shared
                                    * 40px (no first-party
                                    * Chip there) */
    tk_widget_free(ch);
    tk_widget *bd = tk_badge_new(3);
    s = meas(bd);
    CHECK(s.h == 19);
    CHECK(s.w == (winui ? 20 : 18)); /* M22: caption 13px */
    tk_widget_free(bd);
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    for (int i = 0; i < 24 * 24; i++) iconpx[i] = 0xff888888u;
    test_button_variants(&tk_theme_v2);
    test_button_variants(&tk_theme_winui);
    test_split(&tk_theme_v2);
    test_split(&tk_theme_winui);
    test_glyphs(&tk_theme_v2, false);
    test_glyphs(&tk_theme_winui, true);
    test_glyph_shapes();
    test_slider(&tk_theme_v2, false);
    test_slider(&tk_theme_winui, true);
    test_slider_dot_scales();
    test_switch_knob_sizes();
    test_ring(&tk_theme_v2, false);
    test_ring(&tk_theme_winui, true);
    test_blit_scaled();
    menu_scale_flow(&tk_theme_v2, false);
    menu_scale_flow(&tk_theme_winui, true);
    test_fields(&tk_theme_v2, false);
    test_fields(&tk_theme_winui, true);
    suggest_fade_flow(&tk_theme_v2);
    suggest_fade_flow(&tk_theme_winui);
    test_card(&tk_theme_v2, false);
    test_card(&tk_theme_winui, true);
    test_treeview(&tk_theme_v2);
    test_treeview(&tk_theme_winui);
    dialog_flow(&tk_theme_v2, false);
    dialog_flow(&tk_theme_winui, true);
    test_pickers(&tk_theme_v2);
    test_pickers(&tk_theme_winui);
    snack_flow(&tk_theme_v2, false);
    snack_flow(&tk_theme_winui, true);
    test_toast_shape();
    test_tabbar(&tk_theme_v2, false);
    test_tabbar(&tk_theme_winui, true);
    fabmenu_flow(&tk_theme_v2, false);
    fabmenu_flow(&tk_theme_winui, true);
    test_bars(&tk_theme_v2, false);
    test_bars(&tk_theme_winui, true);
    sheet_flow(&tk_theme_v2);
    sheet_flow(&tk_theme_winui);
    test_segmented(&tk_theme_v2, false);
    test_segmented(&tk_theme_winui, true);
    test_carousel(&tk_theme_v2);
    test_carousel(&tk_theme_winui);
    test_colorpicker(&tk_theme_v2, false);
    test_colorpicker(&tk_theme_winui, true);
    test_small_forms(&tk_theme_v2, false);
    test_small_forms(&tk_theme_winui, true);
    printf("tk17: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
