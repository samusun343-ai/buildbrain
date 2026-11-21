/* test_tk12.c - M13: the B-state pixels and D wiring the
 * earlier lines left unverified.
 *
 * B (all-state appearance), against the mode specs:
 *   FAB face roles (M3 primaryContainer / onPrimaryContainer,
 *   WinUI accent mapping), the standard icon button's pressed
 *   state layer, the split button's asymmetric inner corners
 *   (4dp), the navigation rail / drawer indicator role
 *   (secondaryContainer) with pressed and disabled forms, the
 *   bottom sheet drag handle (onSurfaceVariant 40%), the
 *   bottom bar's WinUI indicator form, badge colours in the
 *   standalone and anchored forms, dialog modeless /
 *   full-screen faces (shared with the basic form).
 * D (animation), start / middle / end values against the
 *   engine formula with an INDEPENDENT bezier reference:
 *   toast fade (timeline), tab indicator slide, carousel
 *   settle, checkbox / radio glyph transitions, slider
 *   programmatic value animation, the M3 progress keyframes
 *   (independent keyframe evaluation from the spec tables)
 *   and determinate transition, the spinner's rotation, the
 *   FAB menu entrance / exit, dialog modeless / full-screen
 *   entrances.
 *
 * Spec sources (fetched 2026-11-20, see
 * REPORT-toolkit-m13.md): M3 component specs and token
 * tables (m3.material.io, developer.android.com compose
 * defaults), material-web _linear-progress.scss (the MDC
 * keyframes verbatim), Microsoft Learn WinUI timing /
 * themeresources values carried from M10-M12.
 *
 * The bezier reference below is a dense parametric scan -
 * it shares no code with the library's Newton solver, so
 * the comparisons are real cross-checks. */
#include "tk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; \
        printf("FAIL %d: %s\n", __LINE__, #cond); } \
} while (0)

static bool col_eq(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
}
static tk_color px(tk_canvas *c, int x, int y)
{
    return tk_canvas_pixel(c, x, y);
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
/* count pixels near col in a rect */
static int count_near(tk_canvas *c, tk_rect q, tk_color col, int tol)
{
    int n = 0;
    for (int y = q.y; y < q.y + q.h; y++)
        for (int x = q.x; x < q.x + q.w; x++)
            if (col_near_c(px(c, x, y), col, tol)) n++;
    return n;
}

/* --- independent cubic-bezier reference (dense scan) --- */
static float ref_bezier(float x1, float y1, float x2, float y2,
                        float x)
{
    float best_t = 0, best_d = 1e9f;
    for (int i = 0; i <= 2000; i++) {
        float t = i / 2000.0f, u = 1 - t;
        float bx = 3 * u * u * t * x1 + 3 * u * t * t * x2 +
                   t * t * t;
        float d = fabsf(bx - x);
        if (d < best_d) { best_d = d; best_t = t; }
    }
    float t = best_t, u = 1 - t;
    return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
}
static float ref_standard(float x) { return ref_bezier(0.2f, 0, 0, 1, x); }
static float ref_std_dec(float x)  { return ref_bezier(0, 0, 0, 1, x); }
static float ref_emph_dec(float x) { return ref_bezier(0.05f, 0.7f, 0.1f, 1, x); }

static int anim_val(int from, int to, float e)
{
    return from + (int)((to - from) * e + 0.5f);
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
static tk_canvas *paint_full(tk_root *r)
{
    tk_canvas *c = tk_canvas_new(800, 480);
    tk_root_paint(r, c, tk_theme_default());
    return c;
}
static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = ty; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
static void send_down(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_DOWN, x, y);
}
static void send_up(tk_root *r, int x, int y)
{
    send_ptr(r, TK_EV_POINTER_UP, x, y);
}
static void solid_icon(uint32_t *buf, int n, int r, int g, int b)
{
    for (int i = 0; i < n; i++)
        buf[i] = 0xFF000000u | ((uint32_t)r << 16) |
                 ((uint32_t)g << 8) | (uint32_t)b;
}

/* ---------- FAB + IconButton states ---------- */
static void fab_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 200, 30, 30);
    tk_widget *fab = tk_fab_new(ic, 24, 24, 64);
    tk_root *r = root_of(fab);
    tk_rect q = tk_widget_rect(fab);
    CHECK(q.w == 64 && q.h == 64);
    tk_canvas *c = paint_full(r);
    /* rest face away from the icon and the corner curve */
    tk_color face = px(c, q.x + 6, q.y + 6);
    if (winui) CHECK(col_eq(face, t->accent));
    else CHECK(col_eq(face, t->container)); /* primaryContainer */
    /* the corner is NOT filled (rounded-square shape, not a
     * square); with the pre-M13 circular radius the (6,6)
     * point would be outside too, so also probe the top edge
     * midpoint band: radius 19 fills x+19 at row 2, the old
     * radius 32 did not */
    CHECK(!col_eq(px(c, q.x + 1, q.y + 1), face));
    if (!winui)
        /* radius 19 fills x+13 at row 2; the pre-M13
         * circular radius (32) left it empty */
        CHECK(col_eq(px(c, q.x + 13, q.y + 2), t->container));
    tk_canvas_free(c);
    /* pressed: the content colour's 10% state layer over the
     * face (WinUI: the accent at 80% - Accent Tertiary) */
    send_down(r, q.x + 32, q.y + 32);
    c = paint_full(r);
    tk_color want;
    if (winui) {
        tk_color f = t->accent; f.a = 204;
        tk_color bgc = px(c, q.x + q.w + 8, q.y + 6);
        want = blend(f, bgc);
    } else {
        tk_color o = t->on_container; o.a = 26;
        want = blend(o, t->container);
    }
    CHECK(col_near_c(px(c, q.x + 6, q.y + 6), want, 2));
    tk_canvas_free(c);
    send_up(r, q.x + 32, q.y + 32);
    tk_root_free(r);
    tk_widget_free(fab);
    /* disabled (M3 has no FAB disabled form in spec; the
     * toolkit maps the filled-button framework: on-surface
     * 12% container. WinUI keeps the accent-disabled pair.) */
    tk_widget *fab2 = tk_fab_new(ic, 24, 24, 64);
    tk_root *r2 = root_of(fab2);
    tk_rect q2 = tk_widget_rect(fab2);
    tk_set_enabled(fab2, false);
    c = paint_full(r2);
    tk_color got = px(c, q2.x + 6, q2.y + 6);
    tk_color under = px(c, q2.x + q2.w + 8, q2.y + 6);
    if (winui) {
        CHECK(col_near_c(got, blend(t->accent_dis, under), 2));
    } else {
        tk_color f = t->text; f.a = 31;
        CHECK(col_near_c(got, blend(f, under), 2));
    }
    tk_canvas_free(c);
    tk_root_free(r2);
    tk_widget_free(fab2);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_fab(void)
{
    fab_flow(&tk_theme_v2, false);
    fab_flow(&tk_theme_winui, true);
}

static void iconbtn_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 90, 90, 90);
    tk_widget *b = tk_button_new("");
    tk_button_set_icon(b, ic, 24, 24);
    tk_button_set_icon_only(b, true);
    tk_widget_set_min_size(b, 56, 56);
    tk_root *r = root_of(b);
    tk_rect q = tk_widget_rect(b);
    /* rest: no face - the pixel beside the icon is the
     * surface beneath */
    tk_canvas *c = paint_full(r);
    tk_color under = px(c, q.x + 4, q.y + 28);
    tk_canvas_free(c);
    send_down(r, q.x + 28, q.y + 28);
    c = paint_full(r);
    tk_color got = px(c, q.x + 4, q.y + 28);
    if (winui) {
        tk_color f = t->subtle.a ? t->subtle : t->surface_alt;
        CHECK(col_near_c(got, blend(f, under), 3));
    } else {
        /* M3: onSurfaceVariant at 10%, full-round shape */
        tk_color o = t->text_dim; o.a = 26;
        CHECK(col_near_c(got, blend(o, under), 2));
        /* the layer is round: the corner stays unfilled */
        CHECK(col_eq(px(c, q.x + 1, q.y + 1), under));
    }
    tk_canvas_free(c);
    send_up(r, q.x + 28, q.y + 28);
    tk_root_free(r);
    tk_widget_free(b);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_iconbutton(void)
{
    iconbtn_flow(&tk_theme_v2, false);
    iconbtn_flow(&tk_theme_winui, true);
}

/* ---------- Split button corners + states ---------- */
static int split_fires;
static void on_split_main(tk_widget *w, void *ud)
{
    (void)w; (void)ud; split_fires++;
}
static void split_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    split_fires = 0;
    tk_widget *sb = tk_splitbutton_new("保存", on_split_main, NULL);
    tk_widget *menu = tk_menu_new();
    tk_menu_add_item(menu, "別名で保存", 1);
    tk_splitbutton_set_menu(sb, menu);
    tk_root *r = root_of(sb);
    tk_widget *main_btn = tk_child_at(sb, 0);
    tk_widget *arrow = tk_child_at(sb, 1);
    tk_rect mq = tk_widget_rect(main_btn);
    tk_rect aq = tk_widget_rect(arrow);
    CHECK(aq.x - (mq.x + mq.w) == 2); /* the 2dp gap */
    tk_canvas *c = paint_full(r);
    /* face sample left of the centred label */
    tk_color face = px(c, mq.x + 6, mq.y + mq.h / 2);
    CHECK(col_eq(face, t->accent)); /* filled face, both modes */
    if (!winui) {
        /* inner corners take the 4dp (5px) radius: at 2px
         * from the inner top corner the face is filled -
         * with the pre-M13 full radius it was not. The outer
         * corners keep the full radius: not filled at 2px. */
        CHECK(col_eq(px(c, mq.x + mq.w - 2, mq.y + 2), t->accent));
        CHECK(!col_eq(px(c, mq.x + 2, mq.y + 2), t->accent));
        CHECK(col_eq(px(c, aq.x + 2, aq.y + 2), t->accent));
        CHECK(!col_eq(px(c, aq.x + aq.w - 3, aq.y + 2),
                      t->accent));
    }
    tk_canvas_free(c);
    /* pressed main half: the filled-button pressed form */
    send_down(r, mq.x + mq.w / 2, mq.y + mq.h / 2);
    c = paint_full(r);
    tk_color pw = px(c, mq.x + 6, mq.y + mq.h / 2);
    if (winui) {
        tk_color f = t->accent; f.a = 204;
        tk_color under = px(c, aq.x + aq.w + 8, mq.y + mq.h / 2);
        CHECK(col_near_c(pw, blend(f, under), 2));
    } else {
        tk_color o = t->on_accent; o.a = 26;
        CHECK(col_near_c(pw, blend(o, t->accent), 2));
    }
    tk_canvas_free(c);
    send_up(r, mq.x + mq.w / 2, mq.y + mq.h / 2);
    CHECK(split_fires == 1);
    tk_root_free(r);
    tk_widget_free(sb);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_split(void)
{
    split_flow(&tk_theme_v2, false);
    split_flow(&tk_theme_winui, true);
}

/* ---------- Navigation rail states ---------- */
static void rail_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 40, 40, 40);
    tk_widget *rail = tk_navrail_new();
    tk_navrail_add(rail, "AB", ic, 24, 24);
    tk_navrail_add(rail, "CD", ic, 24, 24);
    tk_navrail_select(rail, 0);
    tk_root *r = root_of(rail);
    tk_rect q = tk_widget_rect(rail);
    int cx = q.x + q.w / 2;
    /* cell 0 spans y+8 .. y+72 (RAIL_CELL 64) */
    tk_canvas *c = paint_full(r);
    if (winui) {
        /* selected: subtle cell + the 3px accent bar */
        CHECK(col_eq(px(c, q.x + 1, q.y + 8 + 32), t->accent));
        tk_color sub = px(c, q.x + 6, q.y + 8 + 32);
        /* the surface beneath = an unselected cell's interior */
        tk_color under = px(c, q.x + 6, q.y + 8 + 64 + 32);
        CHECK(col_near_c(sub,
                         blend(t->subtle.a ? t->subtle
                                           : t->surface_alt,
                               under), 3));
    } else {
        /* selected indicator: secondaryContainer (selection),
         * NOT primaryContainer (container) */
        tk_color ind = px(c, cx - 24, q.y + 8 + 10);
        CHECK(col_eq(ind, t->selection));
        CHECK(!col_eq(ind, t->container));
    }
    /* the unselected indicator area at rest is the rail bg */
    tk_color rest1 = px(c, cx - 24, q.y + 8 + 64 + 10);
    tk_canvas_free(c);
    /* pressed on the unselected item: the state layer */
    send_down(r, cx, q.y + 8 + 64 + 32);
    c = paint_full(r);
    tk_color pr = px(c, cx - 24, q.y + 8 + 64 + 10);
    if (winui) {
        tk_color f = t->subtle.a ? t->subtle : t->surface_alt;
        f.a = 204;
        CHECK(col_near_c(pr, blend(f, rest1), 3));
    } else {
        tk_color o = t->text; o.a = 26; /* onSurface 10% */
        CHECK(col_near_c(pr, blend(o, rest1), 2));
    }
    tk_canvas_free(c);
    send_up(r, cx, q.y + 8 + 64 + 32);
    /* pressed on the selected item (M3): the layer takes the
     * onSecondaryContainer role over the indicator (the
     * earlier press SELECTED item 1 on its UP, so re-select
     * item 0 first) */
    if (!winui) {
        tk_navrail_select(rail, 0);
        send_down(r, cx, q.y + 8 + 32);
        c = paint_full(r);
        tk_color o = t->on_selection; o.a = 26;
        CHECK(col_near_c(px(c, cx - 24, q.y + 8 + 10),
                         blend(o, t->selection), 2));
        tk_canvas_free(c);
        send_up(r, cx, q.y + 8 + 32);
    }
    /* disabled rail (M3): labels take onSurface 38%; the
     * indicator colour has no disabled variant and stays */
    if (!winui) {
        tk_set_enabled(rail, false);
        c = paint_full(r);
        CHECK(col_eq(px(c, cx - 24, q.y + 8 + 10), t->selection));
        tk_color dis = t->text; dis.a = 97;
        tk_color want = blend(dis, t->surface);
        /* the "AB" label band: full-coverage glyph pixels of
         * the blended colour exist, full-strength text none */
        tk_rect band = { q.x, q.y + 8 + 38, q.w, 20 };
        CHECK(count_near(c, band, want, 3) > 0);
        CHECK(count_near(c, band, t->text, 0) == 0);
        tk_canvas_free(c);
        tk_set_enabled(rail, true);
    }
    tk_root_free(r);
    tk_widget_free(rail);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_rail(void)
{
    rail_flow(&tk_theme_v2, false);
    rail_flow(&tk_theme_winui, true);
}

/* ---------- Navigation drawer states ---------- */
static void drawer_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
    tk_widget *dr = tk_drawer_new(NULL);
    tk_drawer_add_item(dr, "Alpha", 1, NULL, 0, 0);
    tk_drawer_add_item(dr, "Beta", 2, NULL, 0, 0);
    tk_drawer_add_item(dr, "Gamma", 3, NULL, 0, 0);
    tk_drawer_select(dr, 1);
    /* disable "Gamma" through the menu item state */
    tk_drawer_attach(host, dr);
    tk_root *r = tk_root_new(host);
    tk_root_layout(r, 800, 480);
    tk_rect q = tk_widget_rect(dr);
    /* rows start at y+16, each 64 high; the selected pill
     * spans x+12 .. , y+4 .. within its row */
    tk_canvas *c = paint_full(r);
    if (winui) {
        CHECK(col_eq(px(c, q.x + 1, q.y + 16 + 32), t->accent));
    } else {
        tk_color ind = px(c, q.x + 16, q.y + 16 + 32);
        CHECK(col_eq(ind, t->selection)); /* secondaryContainer */
        CHECK(!col_eq(ind, t->container));
        /* active label: onSecondaryContainer */
        tk_rect band = { q.x + 33, q.y + 16, 200, 64 };
        CHECK(count_near(c, band, t->on_selection, 2) > 0);
    }
    tk_canvas_free(c);
    /* disabled item label: M3 = onSurface 38% over the panel
     * fill; WinUI = TextFillColorDisabled. (The item state
     * rides the shared menu-item storage, so the public
     * menu API drives it.) */
    tk_menu_set_item_enabled(dr, 3, false);
    c = paint_full(r);
    tk_rect band3 = { q.x + 33, q.y + 16 + 128, 200, 64 };
    if (winui) {
        /* TextFillColorDisabled is translucent: the glyph
         * pixels are its blend over the panel fill (sampled
         * inside the panel, clear of the label) */
        tk_color panel = px(c, q.x + 250, band3.y + 32);
        tk_color want = blend(t->text_dis.a ? t->text_dis
                                            : t->text_dim,
                              panel);
        CHECK(count_near(c, band3, want, 3) > 0);
    } else {
        tk_color dis = t->text; dis.a = 97;
        tk_color want = blend(dis, t->container_low);
        CHECK(count_near(c, band3, want, 3) > 0);
        /* and it is not the unselected colour it used to be */
        CHECK(count_near(c, band3, t->text_dim, 0) == 0 ||
              count_near(c, band3, want, 3) >
                  count_near(c, band3, t->text_dim, 0));
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(host);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_drawer(void)
{
    drawer_flow(&tk_theme_v2, false);
    drawer_flow(&tk_theme_winui, true);
}

/* ---------- Bottom sheet handle ---------- */
static void sheet_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *sh = tk_sheet_new();
    tk_widget_set_min_size(sh, 400, 200);
    tk_root *r = root_of(sh);
    tk_rect q = tk_widget_rect(sh);
    tk_canvas *c = paint_full(r);
    /* container + handle (centred, y+8 .. y+13) */
    tk_color cont = px(c, q.x + 20, q.y + 100);
    tk_color hd = px(c, q.x + q.w / 2, q.y + 10);
    if (winui) {
        /* the border token is translucent: the handle is its
         * blend over the sheet's own fill beside it */
        tk_color beside = px(c, q.x + q.w / 2 + 40, q.y + 10);
        CHECK(col_near_c(hd, blend(t->border, beside), 2));
        CHECK(col_near_c(cont, blend(t->layer.a ? t->layer
                                                : t->surface,
                                     px(c, q.x + q.w + 8,
                                        q.y + 100)), 3));
    } else {
        CHECK(col_eq(cont, t->container_low));
        tk_color want = t->text_dim; want.a = 102; /* 40% */
        CHECK(col_near_c(hd, blend(want, t->container_low), 2));
        CHECK(!col_eq(hd, t->border)); /* the pre-M13 colour */
    }
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sh);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_sheet(void)
{
    sheet_flow(&tk_theme_v2, false);
    sheet_flow(&tk_theme_winui, true);
}

/* ---------- BottomBar WinUI indicator form ---------- */
static void test_bottombar_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    const tk_theme *t = tk_theme_default();
    uint32_t ic[24 * 24];
    solid_icon(ic, 24 * 24, 70, 70, 70);
    tk_widget *bar = tk_bottombar_new();
    tk_bottombar_add_cell(bar, ic, 24, 24, "ホーム", NULL, NULL);
    tk_bottombar_add_cell(bar, ic, 24, 24, "設定", NULL, NULL);
    tk_bottombar_set_indicator_mode(bar, 1);
    tk_bottombar_set_selected(bar, 0);
    tk_root *r = root_of(bar);
    tk_widget *cell0 = tk_bottombar_cell(bar, 0);
    tk_rect cq = tk_widget_rect(cell0);
    tk_canvas *c = paint_full(r);
    /* NavigationView form: the 3px accent bar at the cell's
     * left edge, subtle cell fill */
    CHECK(col_eq(px(c, cq.x + 1, cq.y + cq.h / 2), t->accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(bar);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Badge colours (standalone + anchored) ---------- */
static void test_badge(void)
{
    const tk_color err = { 186, 26, 26, 255 }; /* M3 error */
    for (int mode = 0; mode < 2; mode++) {
        tk_theme_set_default(mode ? &tk_theme_winui
                                  : &tk_theme_v2);
        /* standalone dot */
        tk_widget *b = tk_badge_new(-1);
        tk_root *r = root_of(b);
        tk_rect q = tk_widget_rect(b);
        tk_canvas *c = paint_full(r);
        CHECK(col_eq(px(c, q.x + q.w / 2, q.y + q.h / 2), err));
        tk_canvas_free(c);
        tk_root_free(r);
        tk_widget_free(b);
        /* standalone count: error face, white count */
        tk_widget *b2 = tk_badge_new(7);
        tk_root *r2 = root_of(b2);
        tk_rect q2 = tk_widget_rect(b2);
        c = paint_full(r2);
        CHECK(col_eq(px(c, q2.x + 2, q2.y + q2.h / 2), err));
        /* M22: at the 13px caption the count glyph's peak
         * coverage is ~247/255, so no pixel is exactly
         * white; near-white (>= ~94% coverage) is the
         * size-independent presence test */
        CHECK(count_near(c, q2, (tk_color){255, 255, 255, 255},
                         16) > 0);
        tk_canvas_free(c);
        tk_root_free(r2);
        tk_widget_free(b2);
        /* anchored: the tab bar's icon badge dot */
        uint32_t ic[24 * 24];
        solid_icon(ic, 24 * 24, 40, 40, 40);
        tk_widget *tb = tk_tabbar_new();
        tk_tabbar_add(tb, "One");
        tk_tabbar_add(tb, "Two");
        tk_tabbar_set_item_icon(tb, 0, ic, 24, 24);
        tk_tabbar_set_item_badge(tb, 0, -1);
        tk_root *r3 = root_of(tb);
        tk_rect q3 = tk_widget_rect(tb);
        c = paint_full(r3);
        /* the dot sits at the icon's top-right corner: the
         * icon is centred in segment 0 */
        int seg = q3.w / 2;
        int ix = q3.x + (seg - 24) / 2;
        CHECK(col_eq(px(c, ix + 24 - 4 + 4, q3.y + 4), err) ||
              count_near(c, (tk_rect){ ix + 14, q3.y, 16, 14 },
                         err, 0) > 0);
        tk_canvas_free(c);
        tk_root_free(r3);
        tk_widget_free(tb);
    }
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Dialog modeless / full-screen faces ---------- */
static void dialog_face_check(const tk_theme *theme, bool winui,
                              bool fullscreen)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *host = tk_label_new("host");
    tk_root *r = root_of(host);
    tk_widget *d = tk_dialog_new("Title");
    tk_widget *ok = tk_button_new("OK");
    tk_add_child(d, ok);
    if (fullscreen) {
        tk_dialog_set_fullscreen(d, true);
        tk_dialog_show(r, d);
        CHECK(tk_dialog_is_modal(r, d));
    } else {
        tk_dialog_show_modeless(r, d);
        CHECK(!tk_dialog_is_modal(r, d));
    }
    CHECK(tk_dialog_is_shown(r, d));
    tk_rect q = tk_widget_rect(d);
    tk_canvas *c = paint_full(r);
    /* the face is the dialog surface in both modes (the same
     * paint as the basic form): sample a corner inside the
     * face, clear of the title text */
    tk_color face = px(c, q.x + q.w - 12, q.y + q.h - 12);
    /* the shared dialog face: the basic / modeless form is
     * surfaceContainerHigh in the M3 mode; the full-screen
     * form and the WinUI mode take surface */
    tk_color want_face = (winui || fullscreen)
        ? t->surface : t->container_high;
    CHECK(col_near_c(face, want_face, 3));
    tk_canvas_free(c);
    tk_dialog_close(r, d);
    CHECK(!tk_dialog_is_shown(r, d));
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(d);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_dialog_faces(void)
{
    dialog_face_check(&tk_theme_v2, false, false);
    dialog_face_check(&tk_theme_v2, false, true);
    dialog_face_check(&tk_theme_winui, true, false);
    dialog_face_check(&tk_theme_winui, true, true);
}

/* ---------- Toast fade (D) ---------- */
static void toast_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    /* M18: the M3-mode fade is the AOSP framework pair -
     * 500 ms (config_longAnimTime) with decelerate_quad in /
     * accelerate_quad out (the exit is evaluated as
     * decelerate_quad on the remaining fraction, which is
     * the same curve); the WinUI fade stays 83 ms Linear.
     * The M3-mode anchor moved with the AOSP geometry
     * (bottom offset 48dp = 56px, caption text + 12dp
     * vertical padding: centre y = 401); the WinUI form
     * keeps the toolkit geometry (centre y = 436). */
    int fade = winui ? 83 : 500;
    int ay = winui ? 436 : 401;
    tk_widget *host = tk_label_new("host");
    tk_root *r = root_of(host);
    /* the toast anchors bottom-centre: (400, ay) */
    tk_canvas *c = paint_full(r);
    tk_color under = px(c, 400, ay);
    tk_canvas_free(c);
    tk_root_tick(r, 1000);
    tk_toast_show(r, "保存しました", 4000);
    CHECK(tk_toast_visible(r));
    c = paint_full(r);
    /* fade-in starts at alpha 0 on a live-clock root */
    CHECK(col_eq(px(c, 400, ay), under));
    tk_color face = px(c, 400, ay); /* becomes the face later */
    tk_canvas_free(c);
    tk_root_tick(r, 1000 + fade);
    c = paint_full(r);
    face = px(c, 400, ay);
    CHECK(!col_eq(face, under)); /* fully faded in */
    tk_canvas_free(c);
    /* mid fade-out (the tail of the display duration) */
    tk_root_tick(r, 1000 + 4000 - fade / 2);
    c = paint_full(r);
    float e = winui ? (float)(fade / 2) / (float)fade
                    : 0.75f; /* decelerate_quad(0.5) */
    tk_color f2 = face;
    f2.a = (uint8_t)(255.0f * (winui ? 0.5f : e) + 0.5f);
    if (winui) f2.a = 128;
    CHECK(col_near_c(px(c, 400, ay), blend(f2, under), 5));
    tk_canvas_free(c);
    tk_root_tick(r, 1000 + 4000);
    CHECK(!tk_toast_visible(r));
    /* explicit hide fades out from full alpha */
    tk_root_tick(r, 2000);
    tk_toast_show(r, "二つ目", 4000);
    tk_root_tick(r, 2000 + fade); /* fully in */
    tk_root_tick(r, 3000);
    tk_toast_hide(r);
    CHECK(tk_toast_visible(r)); /* fading, not gone */
    tk_root_tick(r, 3000 + fade);
    CHECK(!tk_toast_visible(r));
    tk_root_free(r);
    tk_widget_free(host);
    /* headless root: no clock - synchronous, as before */
    tk_widget *host2 = tk_label_new("host");
    tk_root *r2 = root_of(host2);
    tk_toast_show(r2, "即時", 4000);
    c = paint_full(r2);
    CHECK(!col_eq(px(c, 400, ay), under));
    tk_canvas_free(c);
    tk_toast_hide(r2);
    CHECK(!tk_toast_visible(r2));
    tk_root_free(r2);
    tk_widget_free(host2);
    tk_theme_set_default(&tk_theme_v2);
    (void)t;
}
static void test_toast(void)
{
    toast_flow(&tk_theme_v2, false);
    toast_flow(&tk_theme_winui, true);
}

/* ---------- TabBar indicator slide (D) ---------- */
static int ind_left(tk_root *r, tk_rect q, tk_color accent)
{
    tk_canvas *c = paint_full(r);
    int y = q.y + q.h - 2, found = -1;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, y), accent, 2)) { found = x; break; }
    tk_canvas_free(c);
    return found;
}
static void tabbar_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 167 : 200;
    tk_widget *tb = tk_tabbar_new();
    tk_tabbar_add(tb, "One");
    tk_tabbar_add(tb, "Two");
    tk_tabbar_add(tb, "Three");
    tk_widget_set_min_size(tb, 600, 56);
    tk_root *r = root_of(tb);
    tk_rect q = tk_widget_rect(tb);
    CHECK(q.w >= 600);
    int seg = q.w / 3;
    int iw2 = seg - 16;
    int x0 = q.x + (seg - iw2) / 2;
    int x2 = q.x + 2 * seg + (seg - iw2) / 2;
    tk_root_tick(r, 1000);
    CHECK(ind_left(r, q, t->accent) == x0);
    tk_tabbar_select(tb, 2);
    CHECK(tk_tabbar_selected(tb) == 2); /* selection is immediate */
    CHECK(ind_left(r, q, t->accent) == x0); /* indicator starts here */
    tk_root_tick(r, 1000 + dur / 2);
    int want = anim_val(x0, x2, ref_std_dec(0.5f));
    CHECK(abs(ind_left(r, q, t->accent) - want) <= 2);
    tk_root_tick(r, 1000 + dur);
    CHECK(ind_left(r, q, t->accent) == x2);
    tk_root_free(r);
    tk_widget_free(tb);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_tabbar(void)
{
    tabbar_flow(&tk_theme_v2, false);
    tabbar_flow(&tk_theme_winui, true);
}

/* ---------- Carousel settle (D) ---------- */
/* the item layout simulation, from the widget's form
 * constants (selected 248, rest 64, gap 9) */
static int car_count_sim(int scroll, int win_w)
{
    int total = 0;
    int x = -scroll;
    for (int i = 0; i < 4; i++) {
        int d = i * 73 - scroll;
        if (d < 0) d = -d;
        float f = d >= 73 ? 0.0f : 1.0f - (float)d / 73.0f;
        int w = 64 + (int)(184.0f * f + 0.5f);
        int a = x > 0 ? x : 0;
        int b = x + w < win_w ? x + w : win_w;
        if (b > a) total += b - a;
        x += w + 9;
    }
    return total;
}
static void carousel_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 250 : 300;
    tk_widget *car = tk_carousel_new();
    tk_carousel_add(car, "A", NULL, 0, 0);
    tk_carousel_add(car, "B", NULL, 0, 0);
    tk_carousel_add(car, "C", NULL, 0, 0);
    tk_carousel_add(car, "D", NULL, 0, 0);
    tk_widget_set_min_size(car, 700, 120);
    tk_root *r = root_of(car);
    tk_rect q = tk_widget_rect(car);
    tk_color item = t->container_high.a ? t->container_high
                                        : t->surface_alt;
    int mid_y = q.y + q.h / 2;
    tk_root_tick(r, 1000);
    tk_canvas *c = paint_full(r);
    int n0 = 0;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, mid_y), item, 2)) n0++;
    tk_canvas_free(c);
    CHECK(abs(n0 - car_count_sim(0, q.w)) <= 3);
    tk_carousel_set_index(car, 2);
    CHECK(tk_carousel_index(car) == 2);
    tk_root_tick(r, 1000 + dur / 2);
    float e = winui ? ref_std_dec(0.5f) : ref_emph_dec(0.5f);
    int s_mid = anim_val(0, 146, e);
    c = paint_full(r);
    int nm = 0;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, mid_y), item, 2)) nm++;
    tk_canvas_free(c);
    CHECK(abs(nm - car_count_sim(s_mid, q.w)) <= 4);
    tk_root_tick(r, 1000 + dur);
    c = paint_full(r);
    int n1 = 0;
    for (int x = q.x; x < q.x + q.w; x++)
        if (col_near_c(px(c, x, mid_y), item, 2)) n1++;
    tk_canvas_free(c);
    CHECK(abs(n1 - car_count_sim(146, q.w)) <= 3);
    tk_root_free(r);
    tk_widget_free(car);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_carousel(void)
{
    carousel_flow(&tk_theme_v2, false);
    carousel_flow(&tk_theme_winui, true);
}

/* ---------- Checkbox / Radio glyph transitions (D) ---------- */
static void glyph_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 167 : 200;
    tk_widget *cb = tk_checkbox_new("有効", false);
    tk_root *r = root_of(cb);
    tk_rect q = tk_widget_rect(cb);
    int bsz = winui ? 28 : 21; /* M22: the real M3 box is 21px */
    int bx = q.x, by = q.y + (q.h - bsz) / 2;
    int cy = by + bsz / 2;
    tk_root_tick(r, 1000);
    tk_checkbox_set_checked(cb, true);
    CHECK(tk_checkbox_checked(cb)); /* logical state immediate */
    tk_canvas *c = paint_full(r);
    if (winui) {
        /* gp = 0: the edge is the unchecked base over bg */
        tk_color base = blend(t->control_fill, t->bg);
        CHECK(col_near_c(px(c, bx + 1, cy), base, 3));
    } else {
        CHECK(col_eq(px(c, bx + 2, cy), t->surface));
    }
    tk_canvas_free(c);
    tk_root_tick(r, 1000 + dur / 2);
    float e = winui ? ref_std_dec(0.5f) : ref_standard(0.5f);
    int alpha = (int)(255.0f * e + 0.5f);
    c = paint_full(r);
    if (winui) {
        tk_color f = t->accent; f.a = (uint8_t)alpha;
        tk_color base = blend(t->control_fill, t->bg);
        CHECK(col_near_c(px(c, bx + 1, cy), blend(f, base), 3));
    } else {
        tk_color f = t->accent; f.a = (uint8_t)alpha;
        CHECK(col_near_c(px(c, bx + 2, cy),
                         blend(f, t->surface), 2));
    }
    tk_canvas_free(c);
    tk_root_tick(r, 1000 + dur);
    c = paint_full(r);
    CHECK(col_eq(px(c, bx + 1, cy), t->accent));
    if (!winui) /* the M3 mark at full size: the probe sits
                 * on the long arm of the material3-ratio
                 * check path (M22) */
        CHECK(col_eq(px(c, bx + 12, cy), t->on_accent));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(cb);
    /* radios: the newly selected dot grows, the old shrinks */
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *r1 = tk_radio_new("One", 1, 1);
    tk_widget *r2 = tk_radio_new("Two", 1, 2);
    tk_add_child(box, r1);
    tk_add_child(box, r2);
    tk_root *rr = tk_root_new(box);
    tk_root_layout(rr, 800, 480);
    tk_radio_select(r1); /* no clock yet: snaps */
    tk_root_tick(rr, 1000);
    tk_radio_select(r2);
    tk_rect q1 = tk_widget_rect(r1);
    tk_rect q2 = tk_widget_rect(r2);
    int rb = winui ? 28 : 18;
    tk_rect b1 = { q1.x, q1.y + (q1.h - rb) / 2, rb, rb };
    tk_rect b2 = { q2.x, q2.y + (q2.h - rb) / 2, rb, rb };
    tk_root_tick(rr, 1000 + dur / 2);
    c = paint_full(rr);
    int n2 = count_near(c, b2, t->accent, 2);
    int n1 = count_near(c, b1, t->accent, 2);
    CHECK(n2 > 8);        /* dot well grown */
    CHECK(n1 < n2);       /* old dot shrinking behind */
    tk_canvas_free(c);
    tk_root_tick(rr, 1000 + dur);
    c = paint_full(rr);
    CHECK(count_near(c, b2, t->accent, 2) > 20);
    /* the deselected dot is gone (the WinUI ring keeps its
     * accent only while selected) */
    if (winui) CHECK(count_near(c, b1, t->accent, 2) <= 4);
    else CHECK(count_near(c, b1, t->accent, 2) == 0);
    tk_canvas_free(c);
    tk_root_free(rr);
    tk_widget_free(box);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_glyph(void)
{
    glyph_flow(&tk_theme_v2, false);
    glyph_flow(&tk_theme_winui, true);
}

/* ---------- Slider programmatic animation (D) ---------- */
/* probe the active track at a fraction of the width, on the
 * track's middle row, at least 20px clear of wherever the
 * thumb sits in the state under test (the thumb overpaints
 * the track around the fill's end in both modes, so a raw
 * accent count is not a stable measure of the fill) */
static bool slider_at(tk_root *r, tk_rect q, tk_color accent,
                      float frac)
{
    tk_canvas *c = paint_full(r);
    int y = q.y + q.h / 2;
    bool on = col_near_c(px(c, q.x + (int)(frac * q.w), y),
                         accent, 2);
    tk_canvas_free(c);
    return on;
}
static void slider_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    uint32_t dur = winui ? 167 : 200;
    tk_widget *s = tk_slider_new(0.2f);
    tk_widget_set_min_size(s, 400, 56);
    tk_slider_set_animate_values(s, true); /* opt-in under test */
    tk_root *r = root_of(s);
    tk_rect q = tk_widget_rect(s);
    CHECK(q.w >= 400);
    tk_root_tick(r, 1000);
    tk_slider_set_value(s, 0.8f);
    CHECK(tk_slider_value(s) == 0.8f); /* logical immediate */
    /* displayed still 0.2: 0.10 in, 0.35 out */
    CHECK(slider_at(r, q, t->accent, 0.10f));
    CHECK(!slider_at(r, q, t->accent, 0.35f));
    tk_root_tick(r, 1000 + dur / 2);
    /* displayed = 0.2 + 0.6 * decelerate(0.5) ~= 0.65:
     * 0.50 in, 0.78 out */
    CHECK(slider_at(r, q, t->accent, 0.50f));
    CHECK(!slider_at(r, q, t->accent, 0.78f));
    tk_root_tick(r, 1000 + dur);
    /* displayed 0.8: 0.72 in */
    CHECK(slider_at(r, q, t->accent, 0.72f));
    /* pointer set: direct, no animation lag (0.25) */
    send_down(r, q.x + 10, q.y + q.h / 2);
    send_ptr(r, TK_EV_POINTER_MOVE, q.x + q.w / 4, q.y + q.h / 2);
    CHECK(slider_at(r, q, t->accent, 0.15f));
    CHECK(!slider_at(r, q, t->accent, 0.35f));
    send_up(r, q.x + q.w / 4, q.y + q.h / 2);
    tk_root_free(r);
    tk_widget_free(s);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_slider(void)
{
    slider_flow(&tk_theme_v2, false);
    slider_flow(&tk_theme_winui, true);
}

/* ---------- Progress: M3 keyframes + determinate (D) ---------- */
/* independent keyframe evaluation: spec tables (material-web
 * _linear-progress.scss, MDC values) + the dense-scan bezier */
typedef struct { float p, v, e[4]; } ref_kf;
static float ref_kf_eval(const ref_kf *kfs, int n, float p)
{
    if (p <= kfs[0].p) return kfs[0].v;
    for (int i = 0; i + 1 < n; i++) {
        if (p <= kfs[i + 1].p) {
            float x = (p - kfs[i].p) / (kfs[i + 1].p - kfs[i].p);
            float y = ref_bezier(kfs[i].e[0], kfs[i].e[1],
                                 kfs[i].e[2], kfs[i].e[3], x);
            return kfs[i].v + (kfs[i + 1].v - kfs[i].v) * y;
        }
    }
    return kfs[n - 1].v;
}
static const ref_kf R_PRI_TR[] = {
    { 0.0f, 0.0f, { 0, 0, 1, 1 } },
    { 0.20f, 0.0f, { 0.5f, 0, 0.701732f, 0.495819f } },
    { 0.5915f, 83.6714f, { 0.302435f, 0.381352f, 0.55f, 0.956352f } },
    { 1.0f, 200.611f, { 0, 0, 1, 1 } },
};
static const ref_kf R_PRI_SC[] = {
    { 0.0f, 0.08f, { 0, 0, 1, 1 } },
    { 0.3665f, 0.08f, { 0.334731f, 0.12482f, 0.785844f, 1 } },
    { 0.6915f, 0.661479f, { 0.06f, 0.11f, 0.6f, 1 } },
    { 1.0f, 0.08f, { 0, 0, 1, 1 } },
};
static const ref_kf R_SEC_TR[] = {
    { 0.0f, 0.0f, { 0.15f, 0, 0.515058f, 0.409685f } },
    { 0.25f, 37.6519f, { 0.31033f, 0.284058f, 0.8f, 0.733712f } },
    { 0.4835f, 84.3862f, { 0.4f, 0.627035f, 0.6f, 0.902026f } },
    { 1.0f, 160.278f, { 0, 0, 1, 1 } },
};
static const ref_kf R_SEC_SC[] = {
    { 0.0f, 0.08f, { 0.205028f, 0.057051f, 0.57661f, 0.453971f } },
    { 0.1915f, 0.457104f, { 0.152313f, 0.196432f, 0.648374f, 1.00432f } },
    { 0.4415f, 0.72796f, { 0.257759f, -0.003163f, 0.211762f, 1.38179f } },
    { 1.0f, 0.08f, { 0, 0, 1, 1 } },
};
static void test_progress_kf(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    tk_widget *p = tk_progress_new(0.0f);
    tk_widget_set_min_size(p, 400, 24);
    tk_root *r = root_of(p);
    tk_rect q = tk_widget_rect(p);
    CHECK(q.w >= 400);
    tk_root_tick(r, 1000);
    tk_progress_set_indeterminate(p, true);
    static const float phases[] = {
        0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f
    };
    for (size_t pi = 0; pi < sizeof(phases) / sizeof(phases[0]);
         pi++) {
        float ph = phases[pi];
        tk_root_tick(r, 1000 + (uint32_t)(ph * 2000.0f));
        tk_canvas *c = paint_full(r);
        int cy = q.y + q.h / 2;
        const ref_kf *trs[2] = { R_PRI_TR, R_SEC_TR };
        const ref_kf *scs[2] = { R_PRI_SC, R_SEC_SC };
        const float base[2] = { -145.167f, -54.8889f };
        int any_visible = 0;
        for (int b = 0; b < 2; b++) {
            float lp = base[b] + ref_kf_eval(trs[b], 4, ph);
            float wp = ref_kf_eval(scs[b], 4, ph) * 100.0f;
            int sx = q.x + (int)(lp / 100.0f * (float)q.w +
                                 (lp >= 0 ? 0.5f : -0.5f));
            int sw = (int)(wp / 100.0f * (float)q.w + 0.5f);
            int a = sx > q.x ? sx : q.x;
            int e2 = sx + sw < q.x + q.w ? sx + sw : q.x + q.w;
            if (e2 - a >= 10) {
                any_visible = 1;
                CHECK(col_near_c(px(c, (a + e2) / 2, cy),
                                 t->accent, 2));
                if (a > q.x + 10) /* the gap before the bar */
                    CHECK(!col_near_c(px(c, a - 5, cy),
                                      t->accent, 2));
                if (e2 < q.x + q.w - 10)
                    CHECK(!col_near_c(px(c, e2 + 5, cy),
                                      t->accent, 2));
            }
        }
        if (ph == 0.125f) {
            /* both bars are off-track at this phase */
            CHECK(!any_visible);
            int n = 0;
            for (int x = q.x; x < q.x + q.w; x++)
                if (col_near_c(px(c, x, cy), t->accent, 2)) n++;
            CHECK(n == 0);
        } else {
            CHECK(any_visible);
        }
        tk_canvas_free(c);
    }
    tk_root_free(r);
    tk_widget_free(p);
    /* determinate transition: 250ms cubic-bezier(0.4,0,0.6,1),
     * opt-in per widget */
    tk_widget *p2 = tk_progress_new(0.2f);
    tk_widget_set_min_size(p2, 400, 24);
    tk_progress_set_animate_values(p2, true);
    tk_root *r2 = root_of(p2);
    tk_rect q2 = tk_widget_rect(p2);
    tk_root_tick(r2, 1000);
    tk_progress_set_value(p2, 0.8f);
    CHECK(tk_progress_value(p2) == 0.8f);
    tk_root_tick(r2, 1125);
    tk_canvas *c = paint_full(r2);
    int cy = q2.y + q2.h / 2, right = -1;
    for (int x = q2.x; x < q2.x + q2.w; x++)
        if (col_near_c(px(c, x, cy), t->accent, 2)) right = x;
    tk_canvas_free(c);
    float e = ref_bezier(0.4f, 0, 0.6f, 1, 0.5f);
    int want = (int)((0.2f + 0.6f * e) * (float)q2.w);
    CHECK(abs((right - q2.x + 1) - want) <= 3);
    tk_root_tick(r2, 1250);
    c = paint_full(r2);
    right = -1;
    for (int x = q2.x; x < q2.x + q2.w; x++)
        if (col_near_c(px(c, x, cy), t->accent, 2)) right = x;
    tk_canvas_free(c);
    CHECK(abs((right - q2.x + 1) - (int)(0.8f * q2.w)) <= 3);
    tk_root_free(r2);
    tk_widget_free(p2);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- Spinner rotation (D, #12) ---------- */
static void test_spinner(void)
{
    tk_theme_set_default(&tk_theme_v2);
    const tk_theme *t = tk_theme_default();
    tk_widget *sp = tk_spinner_new(40);
    tk_root *r = root_of(sp);
    tk_rect q = tk_widget_rect(sp);
    int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
    tk_root_tick(r, 1000);
    tk_canvas *c = paint_full(r); /* starts the sweep */
    /* phase 0: dots span 0..270deg - the 45deg point has a
     * dot, the 315deg point is inside the 90deg gap */
    CHECK(col_near_c(px(c, cx + 12, cy + 12), t->accent, 2));
    CHECK(!col_near_c(px(c, cx + 12, cy - 12), t->accent, 2));
    tk_canvas_free(c);
    tk_root_tick(r, 1300); /* phase 250: the arc turned 90deg */
    c = paint_full(r);
    CHECK(!col_near_c(px(c, cx + 12, cy + 12), t->accent, 2));
    CHECK(col_near_c(px(c, cx + 12, cy - 12), t->accent, 2));
    tk_canvas_free(c);
    tk_root_free(r);
    tk_widget_free(sp);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- FAB menu entrance / exit (D) ---------- */
static void fabmenu_flow(const tk_theme *theme, bool winui)
{
    tk_theme_set_default(theme);
    uint32_t dur = winui ? 167 : 200;
    uint32_t ic[8 * 8];
    solid_icon(ic, 8 * 8, 10, 10, 10);
    tk_widget *host = tk_label_new("host");
    tk_root *r = root_of(host);
    tk_widget *fm = tk_fabmenu_new();
    tk_fabmenu_add_item(fm, ic, 8, 8, "追加", NULL, NULL);
    tk_fabmenu_add_item(fm, ic, 8, 8, "編集", NULL, NULL);
    tk_root_tick(r, 1000);
    tk_fabmenu_show(r, fm);
    CHECK(tk_fabmenu_is_shown(r, fm));
    tk_widget *item0 = tk_child_at(fm, 0);
    int mh = tk_widget_rect(fm).h;
    CHECK(mh > 20);
    int y_start = tk_widget_rect(item0).y;
    tk_root_tick(r, 1000 + dur / 2);
    int y_mid = tk_widget_rect(item0).y;
    tk_root_tick(r, 1000 + dur);
    int y_end = tk_widget_rect(item0).y;
    /* the menu rose its own height over the small pair */
    CHECK(abs((y_start - y_end) - mh) <= 2);
    CHECK(abs(y_mid - (y_end + anim_val(mh, 0,
                                        ref_std_dec(0.5f)))) <= 2);
    /* exit: slides back down, removed when it lands */
    tk_root_tick(r, 2000);
    tk_fabmenu_hide(r, fm);
    CHECK(tk_fabmenu_is_shown(r, fm));
    tk_root_tick(r, 2000 + dur / 2);
    CHECK(tk_widget_rect(item0).y > y_end);
    tk_root_tick(r, 2000 + dur);
    CHECK(!tk_fabmenu_is_shown(r, fm));
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(fm);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_fabmenu(void)
{
    fabmenu_flow(&tk_theme_v2, false);
    fabmenu_flow(&tk_theme_winui, true);
}

/* ---------- Dialog modeless / full-screen entrances (D) --- */
static void dialog_anim_check(const tk_theme *theme, bool winui,
                              bool fullscreen)
{
    tk_theme_set_default(theme);
    uint32_t dur = winui ? 167 : 200;
    /* canonical rect without a clock */
    tk_rect q0;
    {
        tk_widget *host = tk_label_new("host");
        tk_root *ra = root_of(host);
        tk_widget *d = tk_dialog_new("確認");
        if (fullscreen) {
            tk_dialog_set_fullscreen(d, true);
            tk_dialog_show(ra, d);
        } else {
            tk_dialog_show_modeless(ra, d);
        }
        q0 = tk_widget_rect(d);
        tk_root_free(ra);
        tk_widget_free(host);
        tk_widget_free(d);
    }
    tk_widget *host = tk_label_new("host");
    tk_root *r = root_of(host);
    tk_widget *d = tk_dialog_new("確認");
    tk_root_tick(r, 1000);
    if (fullscreen) {
        tk_dialog_set_fullscreen(d, true);
        tk_dialog_show(r, d);
    } else {
        tk_dialog_show_modeless(r, d);
    }
    tk_rect q = tk_widget_rect(d);
    CHECK(q.y == q0.y + 12); /* the shared entrance starts +12 */
    CHECK(q.x == q0.x && q.w == q0.w && q.h == q0.h);
    tk_root_tick(r, 1000 + dur / 2);
    q = tk_widget_rect(d);
    CHECK(abs(q.y - (q0.y + anim_val(12, 0, ref_std_dec(0.5f))))
          <= 1);
    tk_root_tick(r, 1000 + dur);
    q = tk_widget_rect(d);
    CHECK(q.y == q0.y);
    tk_root_free(r);
    tk_widget_free(host);
    tk_widget_free(d);
    tk_theme_set_default(&tk_theme_v2);
}
static void test_dialog_anim(void)
{
    dialog_anim_check(&tk_theme_v2, false, false);
    dialog_anim_check(&tk_theme_v2, false, true);
    dialog_anim_check(&tk_theme_winui, true, false);
    dialog_anim_check(&tk_theme_winui, true, true);
}

int main(void)
{
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    test_fab();
    test_iconbutton();
    test_split();
    test_rail();
    test_drawer();
    test_sheet();
    test_bottombar_winui();
    test_badge();
    test_dialog_faces();
    test_toast();
    test_tabbar();
    test_carousel();
    test_glyph();
    test_slider();
    test_progress_kf();
    test_spinner();
    test_fabmenu();
    test_dialog_anim();
    printf("tk12: %d passed, %d failed\n",
           checks - failures, failures);
    return failures ? 1 : 0;
}
