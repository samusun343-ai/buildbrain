/* Interaction test: feeds synthetic pointer events into the scene
 * state machine and renders the result of each step.
 *   1. tap start button        -> launcher opens
 *   2. tap 設定 tile           -> settings opens
 *   3. drag brightness slider  -> value follows the pointer
 *   4. tap lid-sleep toggle    -> flips off
 *   5. tap close (X)           -> settings closes
 */
#include "bui.h"
#include "bui_input.h"
#include "scenes.h"
#include <stdio.h>

static bu_theme *T;
static bd_state s;
static bu_canvas *c;

static void render(const char *name)
{
	bd_draw_shell(c, T, &s);
	bu_canvas_write_ppm(c, name);
}

static void tap(int x, int y)
{
	bool ch;
	bd_handle_event(&s, BU_EV_POINTER_DOWN, 0, 1, x, y, &ch);
	bd_handle_event(&s, BU_EV_POINTER_UP, 0, 0, x, y, &ch);
}

int main(int argc, char **argv)
{
	const char *font_path = argc > 1 ? argv[1] : "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
	T = &bu_theme_dark;
	T->font = bu_font_load(font_path);
	if (!T->font) return 1;
	c = bu_canvas_new(854, 480);
	bd_state_init(&s);
	bool ch;

	render("t0_desktop.ppm");
	tap(25, 457);				/* start button */
	render("t1_launcher.ppm");
	printf("after start tap: view=%d (want 1)\n", s.view);
	tap(250, 130);				/* 設定 tile (2nd in row 1) */
	render("t2_settings.ppm");
	printf("after tile tap: settings_open=%d (want 1)\n", (int)s.settings_open);
	/* drag brightness slider from its knob to the left */
	bd_handle_event(&s, BU_EV_POINTER_DOWN, 0, 1, 620, 146, &ch);
	bd_handle_event(&s, BU_EV_POINTER_MOVE, 0, 0, 420, 146, &ch);
	bd_handle_event(&s, BU_EV_POINTER_UP, 0, 0, 420, 146, &ch);
	render("t3_dimmer.ppm");
	printf("brightness after drag: %.2f (want < 0.70)\n", (double)s.brightness);
	tap(795, 236);				/* lid-sleep toggle */
	render("t4_toggle.ppm");
	printf("lid_sleep after tap: %d (want 0)\n", (int)s.lid_sleep);
	tap(789, 45);				/* close X */
	render("t5_closed.ppm");
	printf("settings_open after X: %d (want 0)\n", (int)s.settings_open);
	return 0;
}
