/* test_tk18.c - M19 verification: the WinUI-mode D cells
 * whose transitions are implemented by following a real
 * control's template but were never verified on the
 * component's own path.
 *
 * - WinUI #16 Chip: WinUI has no first-party Chip control;
 *   the toolkit renders the ToggleButton form, whose
 *   background runs the 83 ms BrushTransition (M17,
 *   microsoft-ui-xaml ToggleButton_themeresources.xaml:
 *   state changes are discrete except the background
 *   brush). tk16 verified that transition on the FILLED
 *   button, the toggle button's checked face and the
 *   icon-only button - but not on the chip's own paint
 *   path, which reaches tk_face_brush through the chip's
 *   state pair (unselected = ControlFill, selected =
 *   accent). This file pins that path: no jump at the
 *   change instant, a mid-transition value strictly
 *   between the two settled faces, the settled selected
 *   face identical to a no-clock reference, and the
 *   return leg.
 * - WinUI #33 Segmented: WinUI has no first-party
 *   Segmented control; the toolkit's connected form shows
 *   the selection check through the same glyph transition
 *   as the M3 form, driven by the mode's selection pair
 *   (WinUI: 167 ms, decelerate - tk_anim_selection_pair).
 *   tk16 verified the fade in Android mode only. This
 *   file pins the WinUI pair on the segmented's own path:
 *   mid-flight the segment differs from BOTH settled
 *   states, and the completed frame is pixel-identical
 *   to the settled reference.
 *
 * Grading note (AUDIT SS17): verifying the borrowed
 * template's behaviour caps these cells at PARTIAL -
 * there is no first-party WinUI Chip / Segmented whose
 * own transition definition could be matched in full.
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
static bool col_near_c(tk_color a, tk_color b, int tol)
{
    return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol &&
           abs(a.b - b.b) <= tol;
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

/* ---------- D: the chip's ToggleButton brush (WinUI #16) - */
static void test_chip_brush_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    /* settled selected reference, no clock: the first
     * paint sets the brush directly (the template's
     * initial state) */
    tk_widget *ref = tk_chip_new(TK_CHIP_FILTER, "絞込");
    tk_chip_set_selected(ref, true);
    tk_root *rr = root_of(ref);
    tk_rect qr = tk_widget_rect(ref);
    tk_canvas *cr = paint_full(rr);
    tk_color settled = px(cr, qr.x + qr.w / 2, qr.y + qr.h - 4);
    tk_canvas_free(cr);
    tk_root_free(rr);
    tk_widget_free(ref);

    tk_widget *ch = tk_chip_new(TK_CHIP_FILTER, "絞込");
    tk_root *r = root_of(ch);
    tk_rect q = tk_widget_rect(ch);
    /* sample the face below the label, clear of the
     * leading check slot in both width forms */
    int sx = q.x + q.w / 2, sy = q.y + q.h - 4;
    tk_canvas *c0 = paint_full(r); /* rest: sets the
                                    * brush's initial state */
    tk_color rest = px(c0, sx, sy);
    CHECK(!col_near_c(rest, settled, 4)); /* the two
                                           * settled faces
                                           * really differ */
    tk_root_tick(r, 1000);
    tk_chip_set_selected(ch, true);
    /* the transition starts from the displayed colour:
     * no jump at the change instant */
    tk_canvas *c1 = paint_full(r);
    CHECK(col_near_c(px(c1, sx, sy), rest, 2));
    tk_root_tick(r, 1041); /* ~half of the 83 ms
                            * BrushTransition */
    tk_canvas *c2 = paint_full(r);
    tk_color mid = px(c2, sx, sy);
    CHECK(!col_near_c(mid, rest, 4));
    CHECK(!col_near_c(mid, settled, 4));
    tk_root_tick(r, 1100); /* transition complete */
    tk_canvas *c3 = paint_full(r);
    CHECK(col_near_c(px(c3, sx, sy), settled, 2));
    /* deselect: the brush returns over another 83 ms,
     * again with no jump at the change instant */
    tk_chip_set_selected(ch, false);
    tk_canvas *c4 = paint_full(r); /* starts the return */
    CHECK(col_near_c(px(c4, sx, sy), settled, 2));
    tk_root_tick(r, 1200);
    tk_canvas *c5 = paint_full(r);
    CHECK(col_near_c(px(c5, sx, sy), rest, 2));
    tk_canvas_free(c0); tk_canvas_free(c1); tk_canvas_free(c2);
    tk_canvas_free(c3); tk_canvas_free(c4); tk_canvas_free(c5);
    tk_root_free(r);
    tk_widget_free(ch);
    tk_theme_set_default(&tk_theme_v2);
}

/* ---------- D: the segmented check fade (WinUI #33) ------ */
static void test_segmented_check_winui(void)
{
    tk_theme_set_default(&tk_theme_winui);
    tk_widget *sg = tk_segmented_new(false);
    tk_segmented_add(sg, "月");
    tk_segmented_add(sg, "火");
    tk_segmented_add(sg, "水");
    tk_segmented_select(sg, 2);
    tk_root *r = root_of(sg);
    tk_rect q = tk_widget_rect(sg);
    int sw = q.w / 3;
    tk_rect seg2 = { q.x + 2 * sw, q.y, sw, q.h };
    tk_canvas *c_set = paint_full(r); /* settled, check
                                       * on segment 2 */
    tk_segmented_select(sg, 0); /* no clock yet: snaps */
    tk_canvas *c_0 = paint_full(r);
    /* live: selecting 2 fades its check in on the WinUI
     * selection pair (167 ms) - mid-flight the segment
     * differs from BOTH settled states */
    tk_root_tick(r, 1000);
    tk_segmented_select(sg, 2);
    tk_root_tick(r, 1083); /* mid fade */
    tk_canvas *c_mid = paint_full(r);
    CHECK(diff_count(c_0, c_mid, seg2) > 10);
    CHECK(diff_count(c_set, c_mid, seg2) > 10);
    tk_root_tick(r, 1200); /* past the 167 ms pair */
    tk_canvas *c_end = paint_full(r);
    CHECK(diff_count(c_set, c_end, q) == 0);
    tk_canvas_free(c_set); tk_canvas_free(c_0);
    tk_canvas_free(c_mid); tk_canvas_free(c_end);
    tk_root_free(r);
    tk_widget_free(sg);
    tk_theme_set_default(&tk_theme_v2);
}

int main(void)
{
    test_chip_brush_winui();
    test_segmented_check_winui();
    printf("tk18: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
