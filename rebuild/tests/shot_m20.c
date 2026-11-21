/* shot_m20.c - M20 judgment-material screenshots (not a test).
 *
 * Renders, for both themes (Material 3 v2 / WinUI):
 *  1. "slider": the current Slider form (real widgets) next to
 *     the M3 Expressive pill form drawn as a canvas mock from
 *     the recorded proposal tokens (AUDIT SS16.2: track 16dp
 *     -> 19px, handle 4x44dp -> 5x51px), by state (value 0 /
 *     50 / 100, pressed, disabled). The pill form is NOT
 *     implemented in the widget; the mock exists only so the
 *     user can judge the proposal. The pressed row's mock
 *     uses the same geometry as the static form (no pressed
 *     geometry is specified for the mock).
 *  2. "fit": the (a)-class real-device adaptation widgets
 *     (AUDIT SS15.4 / SS16.2): FAB, Checkbox, RadioButton,
 *     Switch, Chip, Badge, ListView rows, BottomBar - the
 *     items whose classification waits on the user's call.
 *     M20 fix: each toggle appears in BOTH states, the
 *     reference labels are per-mode (the pre-fix sheet
 *     printed the Material references on the WinUI sheet
 *     too), and the plus glyph takes the theme's text
 *     colour instead of pure white.
 *
 * Output: .raw files (u32 w, u32 h, RGB triplets - the
 * screenshots/convert.py format) at the path arguments.
 * Usage: shot_m20 <slider|fit> <v2|winui> <out.raw>
 *
 * M22: the canvas is the real panel size 854x480 (the
 * M20/M21 sheets were shot at the superseded 800 premise,
 * AUDIT SS20); the file keeps its name for continuity.
 */
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t plus_icon[24 * 24];

/* M20 fix: the glyph takes the theme's text colour. The
 * pre-fix icon was pure white, which is invisible on the
 * light selection faces of BOTH modes (the WinUI bottom
 * bar's subtle fill made it stand out as a defect there);
 * real icon bitmaps carry their own colours, and a glyph
 * icon follows the content colour. */
static void make_plus_icon(tk_color fg)
{
    uint32_t px = 0xff000000u | ((uint32_t)fg.r << 16) |
                  ((uint32_t)fg.g << 8) | (uint32_t)fg.b;
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++) {
            bool bar = (x >= 10 && x < 14) || (y >= 10 && y < 14);
            plus_icon[y * 24 + x] = bar ? px : 0x00000000u;
        }
}

static void dump_raw(tk_canvas *c, const char *path)
{
    int w = tk_canvas_width(c), h = tk_canvas_height(c);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint32_t hdr[2] = { (uint32_t)w, (uint32_t)h };
    fwrite(hdr, 4, 2, f);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            tk_color p = tk_canvas_pixel(c, x, y);
            fputc(p.r, f); fputc(p.g, f); fputc(p.b, f);
        }
    fclose(f);
}

/* Draw the proposed M3 Expressive pill slider into the row
 * whose real slider occupies q (mock column starts after q).
 * Tokens (AUDIT SS16.2): track 16dp -> 19px, handle
 * 4x44dp -> 5x51px, handle gap 6px each side. */
static void draw_pill_mock(tk_canvas *c, const tk_theme *t, tk_rect q,
                           float v, bool disabled)
{
    int mx = q.x + q.w + 24;
    int mw = tk_canvas_width(c) - mx - 16;
    int cy = q.y + q.h / 2;
    int hx = mx + (int)(v * (float)mw);
    /* M21: keep the handle inside the mock column at the
     * ends, as the real thumb clamps into its own rect */
    if (hx < mx + 2) hx = mx + 2;
    if (hx > mx + mw - 3) hx = mx + mw - 3;
    tk_color act = disabled ? t->border : t->accent;
    tk_color inact = disabled ? t->surface_alt : t->container_highest;
    tk_color handle = disabled ? t->text_dim : t->accent;
    /* inactive track (right of the handle gap) */
    if (hx + 6 < mx + mw)
        tk_canvas_fill_round_rect(c, (tk_rect){ hx + 6, cy - 9,
            mx + mw - (hx + 6), 19 }, 9, inact);
    /* active track (left of the handle gap) */
    if (hx - 6 > mx)
        tk_canvas_fill_round_rect(c, (tk_rect){ mx, cy - 9,
            hx - 6 - mx, 19 }, 9, act);
    /* pill handle 5x51 */
    tk_canvas_fill_round_rect(c, (tk_rect){ hx - 2, cy - 25, 5, 51 },
                              2, handle);
}

static void shot_slider(const tk_theme *theme, const char *out)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 8, 16);
    tk_add_child(box, tk_label_new(
        theme == &tk_theme_winui
            ? "Slider forms - WinUI mode (pill = M3 proposal, reference)"
            : "Slider forms - Material 3 mode"));
    tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 16, 0);
    tk_widget *h1 = tk_label_new("CURRENT FORM");
    tk_widget *h2 = tk_label_new("PILL FORM (mock, not implemented)");
    tk_set_flex(h1, 1); tk_set_flex(h2, 1);
    tk_add_child(hdr, tk_label_new("            "));
    tk_add_child(hdr, h1);
    tk_add_child(hdr, h2);
    tk_add_child(box, hdr);

    static const char *names[5] = {
        "value 0", "value 50", "value 100", "pressed 50",
        "disabled 50"
    };
    static const float vals[5] = { 0.0f, 0.5f, 1.0f, 0.5f, 0.5f };
    tk_widget *sliders[5];
    for (int i = 0; i < 5; i++) {
        /* M21: the pressed row is taller and bottom-aligned:
         * the value bubble the toolkit draws while pressed
         * floats 55px above the slider's top edge, and the
         * extra band keeps it inside its own row instead of
         * overlapping the value-100 row (the pre-M21 sheet
         * padded the row by only 20px, and the bubble landed
         * in the row above) */
        tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 16, 0);
        if (i == 3) {
            tk_widget_set_min_size(row, 0, 116);
            tk_box_set_align(row, TK_ALIGN_END);
        }
        tk_add_child(row, tk_label_new(names[i]));
        sliders[i] = tk_slider_new(vals[i]);
        tk_set_flex(sliders[i], 1);
        tk_add_child(row, sliders[i]);
        tk_widget *sp = tk_label_new("");
        tk_set_flex(sp, 1);
        tk_add_child(row, sp);
        tk_add_child(box, row);
    }
    tk_set_enabled(sliders[4], false);

    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 854, 480);
    /* pressed state: pointer down on the thumb (value 0.5 =
     * centre of the track), no release */
    tk_rect pq = tk_widget_rect(sliders[3]);
    tk_event ev = { 0 };
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = pq.x + pq.w / 2;
    ev.y = pq.y + pq.h / 2;
    tk_root_event(r, &ev);

    tk_canvas *c = tk_canvas_new(854, 480);
    tk_root_paint(r, c, t);
    for (int i = 0; i < 5; i++)
        draw_pill_mock(c, t, tk_widget_rect(sliders[i]), vals[i],
                       i == 4);
    dump_raw(c, out);
    tk_canvas_free(c);
    tk_root_free(r);
}

static void shot_fit(const tk_theme *theme, const char *out)
{
    tk_theme_set_default(theme);
    const tk_theme *t = tk_theme_default();
    bool winui = theme == &tk_theme_winui;
    make_plus_icon(t->text);
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 8, 16);
    tk_add_child(box, tk_label_new(
        winui ? "(a) adaptation items - WinUI mode"
              : "(a) adaptation items - Material 3 mode"));

    tk_widget *cols = tk_box_new(TK_DIR_HORIZONTAL, 24, 0);
    tk_widget *left = tk_box_new(TK_DIR_VERTICAL, 10, 0);
    tk_widget *right = tk_box_new(TK_DIR_VERTICAL, 10, 0);
    tk_set_flex(left, 1);
    tk_set_flex(right, 1);
    tk_add_child(cols, left);
    tk_add_child(cols, right);
    tk_add_child(box, cols);

    tk_widget *r1 = tk_box_new(TK_DIR_HORIZONTAL, 16, 0);
    tk_add_child(r1, tk_fab_new(plus_icon, 24, 24, 0));
    tk_add_child(r1, tk_label_new(winui
        ? "#3 FAB 65px (no WinUI FAB)"
        : "#3 FAB 65px (ref 56dp)"));
    tk_add_child(left, r1);

    /* M20 fix: both states of each toggle, identical in the
     * two modes - the pre-fix sheet ran the same states too,
     * but the M3 checked mark (a filled square) made the
     * Android sheet read as unchecked, so the pair did not
     * read as aligned material. */
    tk_widget *r2 = tk_box_new(TK_DIR_HORIZONTAL, 24, 0);
    tk_add_child(r2, tk_checkbox_new("Checkbox", true));
    tk_widget *rd = tk_radio_new("Radio", 1, 1);
    tk_add_child(r2, rd);
    tk_radio_select(rd);
    tk_add_child(left, r2);
    tk_widget *r2b = tk_box_new(TK_DIR_HORIZONTAL, 24, 0);
    tk_add_child(r2b, tk_checkbox_new("Checkbox", false));
    tk_add_child(r2b, tk_radio_new("Radio", 1, 2));
    tk_add_child(left, r2b);
    tk_add_child(left, tk_label_new(winui
        ? "#6/#7 box 28px (WinUI size)"
        : "#6/#7 glyph 21px, dot 9px (ref 18dp / 8dp)"));

    tk_widget *r3 = tk_box_new(TK_DIR_HORIZONTAL, 24, 0);
    tk_add_child(r3, tk_switch_new("Switch", true));
    tk_add_child(r3, tk_label_new(winui
        ? "#8 ToggleSwitch (WinUI)"
        : "#8 track 61x37 (ref 52x32dp)"));
    tk_add_child(left, r3);

    tk_widget *r4 = tk_box_new(TK_DIR_HORIZONTAL, 24, 0);
    tk_box_set_align(r4, TK_ALIGN_START);
    tk_widget *chip = tk_chip_new(TK_CHIP_FILTER, "Chip");
    tk_chip_set_selected(chip, true);
    tk_add_child(r4, chip);
    tk_add_child(r4, tk_label_new(winui
        ? "#16 chip h40 (no WinUI Chip)"
        : "#16 chip h37 (ref 32dp)"));
    tk_add_child(left, r4);

    tk_widget *r5 = tk_box_new(TK_DIR_HORIZONTAL, 24, 0);
    tk_box_set_align(r5, TK_ALIGN_START);
    tk_add_child(r5, tk_badge_new(3));
    tk_add_child(r5, tk_label_new(winui
        ? "#17 badge h19 (InfoBadge 6-23px)"
        : "#17 badge h19 (ref 18.7px)"));
    tk_add_child(left, r5);

    static const char *items[2] = { "ListView row 1", "ListView row 2" };
    tk_widget *lv = tk_listview_new();
    tk_listview_set_items(lv, items, 2);
    tk_add_child(right, tk_label_new(winui
        ? "#19 list rows 54px (ref 40epx = 54px)"
        : "#19 list rows 54px (ref 56dp = 63px)"));
    tk_add_child(right, lv);

    tk_widget *bar = tk_bottombar_new();
    tk_bottombar_add_cell(bar, plus_icon, 24, 24, "Home", NULL, NULL);
    tk_bottombar_add_cell(bar, plus_icon, 24, 24, "Apps", NULL, NULL);
    tk_bottombar_add_cell(bar, plus_icon, 24, 24, "Set", NULL, NULL);
    tk_bottombar_set_selected(bar, 0);
    tk_add_child(right, tk_label_new(winui
        ? "#29 bottom bar (no std WinUI form)"
        : "#29 bottom bar 73px (ref 80dp = 90px)"));
    tk_add_child(right, bar);

    tk_root *r = tk_root_new(box);
    tk_root_layout(r, 854, 480);
    tk_canvas *c = tk_canvas_new(854, 480);
    tk_root_paint(r, c, t);
    dump_raw(c, out);
    tk_canvas_free(c);
    tk_root_free(r);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: shot_m20 <slider|fit> <v2|winui> "
                        "<out.raw>\n");
        return 2;
    }
    tk_font *f = tk_font_load_bundled();
    if (f) tk_font_install(f);
    const tk_theme *theme =
        strcmp(argv[2], "winui") == 0 ? &tk_theme_winui
                                      : &tk_theme_v2;
    if (strcmp(argv[1], "slider") == 0)
        shot_slider(theme, argv[3]);
    else
        shot_fit(theme, argv[3]);
    return 0;
}
