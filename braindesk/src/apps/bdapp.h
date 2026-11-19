/* bdapp: shared framework for braindesk apps.
 *
 * Every app is a small program on libbrainui: it opens the
 * display, draws a window (title bar with a close button, content
 * area), and runs an event loop. Power key or the close button
 * exits back to the shell. Apps stay separate processes so the
 * shell's memory footprint never grows with app count.
 */
#ifndef BDAPP_H
#define BDAPP_H

#include "../bui.h"
#include "../bui_fb.h"
#include "../bui_input.h"
#include "../bui_skin.h"
#include "../bui_wm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BDAPP_FONT "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"

/* App frame metrics, codified from the measured values the
 * frame has always used: the standalone title bar is 36 px
 * high and the content area starts below it. */
#define BDAPP_TITLEBAR_H 36

typedef struct {
	bu_display *disp;
	bd_wm_client *wm;	/* non-NULL when running windowed under the shell */
	bu_canvas *canvas;
	bu_theme *theme;
	bu_input *input;
	int w, h;
	const char *title;
} bdapp;

static inline bu_rect bdapp_content(bdapp *a)
{
	if (a->wm)
		return (bu_rect){ 0, 0, a->w, a->h };
	return (bu_rect){ 0, BDAPP_TITLEBAR_H, a->w, a->h - BDAPP_TITLEBAR_H };
}

static inline void bdapp_frame(bdapp *a)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	if (a->wm) {
		/* windowed: the shell draws the real window chrome;
		 * the app paints only its content background */
		bu_fill_rect(c, (bu_rect){ 0, 0, a->w, a->h }, t->surface);
		return;
	}
	const bd_skin *sk = bd_skin_get();
	bu_gradient_v(c, (bu_rect){ 0, 0, a->w, a->h }, sk->wall_top, sk->wall_bottom);
	bu_color pc = t->panel; pc.a = 235;
	bu_fill_rect(c, (bu_rect){ 0, 0, a->w, BDAPP_TITLEBAR_H }, pc);
	bu_fill_rect(c, (bu_rect){ 0, BDAPP_TITLEBAR_H - 1, a->w, 1 }, t->line);
	int l = sk->layout;
	if (l == BD_LAYOUT_MAC) {
		/* traffic lights left, title centred */
		bu_fill_circle(c, 16, BDAPP_TITLEBAR_H / 2, 6, BU_RGB(255, 95, 87));
		bu_fill_circle(c, 36, BDAPP_TITLEBAR_H / 2, 6, BU_RGB(254, 188, 46));
		bu_fill_circle(c, 56, BDAPP_TITLEBAR_H / 2, 6, BU_RGB(40, 200, 64));
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 8, a->w, 20 }, a->title, 13.5, t->text);
	} else if (l == BD_LAYOUT_CLASSIC) {
		bu_stroke_round_rect(c, (bu_rect){ 12, 13, 10, 10 }, 0, 1, t->text_dim);
		bu_draw_text_centered(c, t->font, (bu_rect){ 0, 8, a->w, 20 }, a->title, 13.5, t->text);
	} else if (l >= BD_LAYOUT_WIN11) {
		bu_color ink = (bu_chrome == BU_CHROME_BEVEL || bu_chrome == BU_CHROME_LUNA)
			? BU_RGB(255, 255, 255) : t->text;
		bu_draw_text(c, t->font, 14, 9, a->title, 13.5, ink);
		bu_line(c, a->w - 32, 13, a->w - 22, 23, 1, ink);
		bu_line(c, a->w - 22, 13, a->w - 32, 23, 1, ink);
	} else {
		bu_draw_text(c, t->font, 14, 8, a->title, 15, t->text);
		bu_fill_round_rect(c, (bu_rect){ a->w - 36, 5, 28, 26 }, 7, t->surface);
		bu_line(c, a->w - 29, 12, a->w - 15, 26, 2, t->text);
		bu_line(c, a->w - 15, 12, a->w - 29, 26, 2, t->text);
	}
}

static inline bool bdapp_close_hit(bdapp *a, int x, int y)
{
	int l = bd_skin_get()->layout;
	if (l == BD_LAYOUT_MAC)
		return x >= 4 && x < 30 && y >= 4 && y < 32;
	if (l == BD_LAYOUT_CLASSIC)
		return x >= 6 && x < 30 && y >= 6 && y < 30;
	if (l >= BD_LAYOUT_WIN11)
		return x >= a->w - 46 && x < a->w && y >= 0 && y < BDAPP_TITLEBAR_H;
	return x >= a->w - 40 && x < a->w - 4 && y >= 3 && y < 34;
}

/* draw + present one frame (app calls its own draw first) */
static inline void bdapp_present(bdapp *a)
{
	if (a->wm) {
		bd_wm_client_commit(a->wm);
		return;
	}
	bu_display_present(a->disp, a->canvas, (bu_rect){ 0, 0, a->w, a->h });
}

/* Open everything; shot_path != NULL renders one frame via the
 * draw callback into a PPM (host testing) and returns NULL. */
typedef void (*bdapp_draw_fn)(bdapp *a, void *ctx);
typedef void (*bdapp_event_fn)(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit);
typedef void (*bdapp_tick_fn)(bdapp *a, void *ctx, bool *changed);

static inline int bdapp_run(const char *title, const char *shot_path,
			    bdapp_draw_fn draw, bdapp_event_fn onevent, bdapp_tick_fn tick, void *ctx)
{
	bdapp a;
	memset(&a, 0, sizeof(a));
	a.title = title;
	const char *wm_path = getenv("BRAINDESK_WM");
	if (wm_path && !shot_path)
		a.wm = bd_wm_client_open(wm_path, title, 560, 380);
	if (a.wm) {
		a.canvas = bd_wm_client_canvas(a.wm);
		a.w = a.canvas->w;
		a.h = a.canvas->h;
	} else {
		a.disp = bu_display_open(864, 480);
		a.w = bu_display_width(a.disp);
		a.h = bu_display_height(a.disp);
		a.canvas = bu_canvas_new(a.w, a.h);
	}
	a.theme = &bu_theme_dark;
	bd_skin_load();
	bd_skin_apply(a.theme);
	a.theme->font = bu_font_load(BDAPP_FONT);
	if (!a.theme->font) {
		fprintf(stderr, "font load failed\n");
		return 1;
	}
	draw(&a, ctx);
	bdapp_present(&a);
	if (shot_path) {
		if (!bu_display_is_real(a.disp))
			bu_display_dump_ppm(a.disp, shot_path);
		return 0;
	}
	if (!a.wm)
		a.input = bu_input_open(a.w, a.h);
	bool quit = false;
	while (!quit) {
		bu_event ev;
		bool changed = false;
		int got;
		if (a.wm) {
			got = bd_wm_client_next(a.wm, &ev, 250);
			if (got < 0)
				break; /* the shell closed this window */
		} else {
			got = bu_input_next(a.input, &ev, 250) ? 1 : 0;
		}
		if (got == 1) {
			if (!a.wm && ev.type == BU_EV_KEY && ev.code == 116 && ev.value == 1)
				break; /* power key: back to shell/greeter */
			if (!a.wm && ev.type == BU_EV_POINTER_DOWN && bdapp_close_hit(&a, ev.x, ev.y))
				break;
			onevent(&a, ctx, &ev, &changed, &quit);
		}
		if (tick)
			tick(&a, ctx, &changed);
		if (changed) {
			draw(&a, ctx);
			bdapp_present(&a);
		}
	}
	if (a.wm)
		bd_wm_client_close(a.wm);
	else {
		bu_input_close(a.input);
		bu_display_close(a.disp);
		bu_canvas_free(a.canvas);
	}
	return 0;
}

#endif
