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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BDAPP_FONT "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"

typedef struct {
	bu_display *disp;
	bu_canvas *canvas;
	bu_theme *theme;
	bu_input *input;
	int w, h;
	const char *title;
} bdapp;

static inline bu_rect bdapp_content(bdapp *a)
{
	return (bu_rect){ 0, 44, a->w, a->h - 44 };
}

static inline void bdapp_frame(bdapp *a)
{
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	const bd_skin *sk = bd_skin_get();
	bu_gradient_v(c, (bu_rect){ 0, 0, a->w, a->h }, sk->wall_top, sk->wall_bottom);
	bu_color pc = t->panel; pc.a = 235;
	bu_fill_rect(c, (bu_rect){ 0, 0, a->w, 44 }, pc);
	bu_fill_rect(c, (bu_rect){ 0, 43, a->w, 1 }, BU_RGBA(255, 255, 255, 26));
	bu_draw_text(c, t->font, 16, 8, a->title, 17, t->text);
	/* close button */
	bu_fill_round_rect(c, (bu_rect){ a->w - 40, 7, 32, 30 }, 8, t->surface);
	bu_line(c, a->w - 31, 15, a->w - 17, 29, 2, t->text);
	bu_line(c, a->w - 17, 15, a->w - 31, 29, 2, t->text);
}

static inline bool bdapp_close_hit(bdapp *a, int x, int y)
{
	return x >= a->w - 44 && x < a->w - 4 && y >= 4 && y < 40;
}

/* draw + present one frame (app calls its own draw first) */
static inline void bdapp_present(bdapp *a)
{
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
	a.disp = bu_display_open(864, 480);
	a.w = bu_display_width(a.disp);
	a.h = bu_display_height(a.disp);
	a.canvas = bu_canvas_new(a.w, a.h);
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
	a.input = bu_input_open(a.w, a.h);
	bool quit = false;
	while (!quit) {
		bu_event ev;
		bool changed = false;
		if (bu_input_next(a.input, &ev, 250)) {
			if (ev.type == BU_EV_KEY && ev.code == 116 && ev.value == 1)
				break; /* power key: back to shell/greeter */
			if (ev.type == BU_EV_POINTER_DOWN && bdapp_close_hit(&a, ev.x, ev.y))
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
	bu_input_close(a.input);
	bu_display_close(a.disp);
	return 0;
}

#endif
