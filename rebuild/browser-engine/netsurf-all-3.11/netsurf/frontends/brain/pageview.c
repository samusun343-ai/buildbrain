/*
 * Brainux "brain" frontend: the page area.
 *
 * The page is NOT a toolkit-painted widget: the placeholder widget
 * in the chrome tree only reserves the geometry.  Painting goes
 * through the NetSurf plotters into the shared buffer
 * (brain_page_redraw, called from the present cycle in gui.c), and
 * a thin overlay pass afterwards draws the two things the core
 * does not plot: the scrollbar thumb and the caret.
 *
 * Input reaches this file from the router in gui.c, which hands
 * over every pointer event inside the page rect (and, while a
 * page drag is in flight, the matching moves/ups wherever they
 * land) plus key/text events while the page placeholder holds
 * focus.  Pointer semantics mirror the framebuffer frontend
 * (press / click / drag with 5px threshold and 500ms multi-click
 * window): a drag is a core drag (selection, link dragging),
 * exactly as in fb.  Touch scrolling is provided by the directly
 * draggable scrollbar strip on the right edge, the wheel, and
 * PgUp/PgDn.
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libnsfb.h>

#include "utils/log.h"
#include "utils/utf8.h"
#include "netsurf/browser_window.h"
#include "netsurf/window.h"
#include "netsurf/keypress.h"
#include "netsurf/plotters.h"
#include "desktop/browser_history.h"

#include "brain/gui.h"
#include "brain/surface.h"

/** width of the scrollbar strip on the right edge of the page */
#define SCROLLBAR_STRIP 14
/** minimum scrollbar thumb height */
#define THUMB_MIN 28

void brain_page_geometry(nsfb_bbox_t *out)
{
	if (brain_fe.page_holder != NULL) {
		tk_rect r = tk_widget_rect(brain_fe.page_holder);
		out->x0 = r.x;
		out->y0 = r.y;
		out->x1 = r.x + r.w;
		out->y1 = r.y + r.h;
	} else {
		out->x0 = 0;
		out->y0 = 0;
		out->x1 = brain_fe.width;
		out->y1 = brain_fe.height;
	}
}

static bool page_size(int *w, int *h)
{
	nsfb_bbox_t page;
	brain_page_geometry(&page);
	*w = page.x1 - page.x0;
	*h = page.y1 - page.y0;
	return *w > 0 && *h > 0;
}

/* ------------------------------------------------------------------ */
/* redraw                                                             */
/* ------------------------------------------------------------------ */

void brain_page_redraw(struct gui_window *gw, const nsfb_bbox_t *win_clip)
{
	nsfb_bbox_t page;
	struct rect clip;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &brain_plotters,
	};

	if (gw == NULL || gw->bw == NULL || brain_fe.nsfb == NULL) {
		return;
	}
	brain_page_geometry(&page);

	clip.x0 = win_clip->x0;
	clip.y0 = win_clip->y0;
	clip.x1 = win_clip->x1;
	clip.y1 = win_clip->y1;
	if (clip.x0 < page.x0) clip.x0 = page.x0;
	if (clip.y0 < page.y0) clip.y0 = page.y0;
	if (clip.x1 > page.x1) clip.x1 = page.x1;
	if (clip.y1 > page.y1) clip.y1 = page.y1;
	if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1) {
		return;
	}

	browser_window_redraw(gw->bw,
			page.x0 - gw->scrollx,
			page.y0 - gw->scrolly,
			&clip, &ctx);
}

void brain_page_invalidate(struct gui_window *gw, nsfb_bbox_t content_clip)
{
	nsfb_bbox_t page;
	nsfb_bbox_t r;

	if (gw != brain_fe.active) {
		return;
	}
	brain_page_geometry(&page);
	r.x0 = content_clip.x0 - gw->scrollx + page.x0;
	r.y0 = content_clip.y0 - gw->scrolly + page.y0;
	r.x1 = content_clip.x1 - gw->scrollx + page.x0;
	r.y1 = content_clip.y1 - gw->scrolly + page.y0;
	if (r.x0 < page.x0) r.x0 = page.x0;
	if (r.y0 < page.y0) r.y0 = page.y0;
	if (r.x1 > page.x1) r.x1 = page.x1;
	if (r.y1 > page.y1) r.y1 = page.y1;
	if (r.x0 < r.x1 && r.y0 < r.y1) {
		brain_invalidate(r);
	}
}

void brain_page_full_redraw(struct gui_window *gw)
{
	nsfb_bbox_t page;

	if (gw != brain_fe.active) {
		return;
	}
	brain_page_geometry(&page);
	brain_invalidate(page);
}

/* ------------------------------------------------------------------ */
/* scrolling                                                          */
/* ------------------------------------------------------------------ */

void brain_page_set_scroll(struct gui_window *gw, int sx, int sy)
{
	int pw, ph;
	int ew = 0, eh = 0;

	if (gw == NULL || gw->bw == NULL) {
		return;
	}
	if (!page_size(&pw, &ph)) {
		return;
	}
	browser_window_get_extents(gw->bw, true, &ew, &eh);
	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (sx > ew - pw) sx = ew - pw > 0 ? ew - pw : 0;
	if (sy > eh - ph) sy = eh - ph > 0 ? eh - ph : 0;
	if (sx == gw->scrollx && sy == gw->scrolly) {
		return;
	}
	gw->scrollx = sx;
	gw->scrolly = sy;
	if (gw == brain_fe.active) {
		brain_page_full_redraw(gw);
	}
}

static void scroll_by(struct gui_window *gw, int dx, int dy)
{
	brain_page_set_scroll(gw, gw->scrollx + dx, gw->scrolly + dy);
}

/* ------------------------------------------------------------------ */
/* overlay: scrollbar thumb + caret                                   */
/* ------------------------------------------------------------------ */

void brain_page_overlay_paint(tk_canvas *canvas)
{
	struct gui_window *gw = brain_fe.active;
	nsfb_bbox_t page;
	const tk_theme *th;
	int ew = 0, eh = 0;
	int pw, ph;

	if (gw == NULL || gw->bw == NULL || canvas == NULL) {
		return;
	}
	th = tk_theme_default();
	brain_page_geometry(&page);
	pw = page.x1 - page.x0;
	ph = page.y1 - page.y0;
	if (pw <= 0 || ph <= 0) {
		return;
	}
	browser_window_get_extents(gw->bw, true, &ew, &eh);

	if (eh > ph) {
		int thumb_h = ph * ph / eh;
		int thumb_y;
		tk_color col = th->border;
		if (thumb_h < THUMB_MIN) thumb_h = THUMB_MIN;
		if (thumb_h > ph) thumb_h = ph;
		thumb_y = page.y0 + (eh - ph > 0 ?
			(ph - thumb_h) * gw->scrolly / (eh - ph) : 0);
		col.a = 170;
		tk_canvas_fill_round_rect(canvas,
				(tk_rect){ page.x1 - SCROLLBAR_STRIP + 3,
						thumb_y, 8, thumb_h },
				4, col);
	}

	if (gw->caret_h > 0) {
		tk_canvas_fill_rect(canvas,
				(tk_rect){ gw->caret_x, gw->caret_y,
						2, gw->caret_h },
				th->accent);
	}
}

/* ------------------------------------------------------------------ */
/* input (fed by the router in gui.c)                                 */
/* ------------------------------------------------------------------ */

static void content_point(const tk_event *ev, int *cx, int *cy)
{
	nsfb_bbox_t page;
	struct gui_window *gw = brain_fe.active;

	brain_page_geometry(&page);
	*cx = ev->x - page.x0 + (gw != NULL ? gw->scrollx : 0);
	*cy = ev->y - page.y0 + (gw != NULL ? gw->scrolly : 0);
}

static bool in_scrollbar_strip(const tk_event *ev)
{
	nsfb_bbox_t page;
	brain_page_geometry(&page);
	return ev->x >= page.x1 - SCROLLBAR_STRIP && ev->x < page.x1 &&
		ev->y >= page.y0 && ev->y < page.y1;
}

static void scrollbar_drag_to(const tk_event *ev)
{
	struct gui_window *gw = brain_fe.active;
	nsfb_bbox_t page;
	int ew = 0, eh = 0;
	int ph, thumb_h, rel;

	if (gw == NULL || gw->bw == NULL) {
		return;
	}
	brain_page_geometry(&page);
	ph = page.y1 - page.y0;
	browser_window_get_extents(gw->bw, true, &ew, &eh);
	if (eh <= ph || ph <= 0) {
		return;
	}
	thumb_h = ph * ph / eh;
	if (thumb_h < THUMB_MIN) thumb_h = THUMB_MIN;
	rel = ev->y - page.y0 - thumb_h / 2;
	if (rel < 0) rel = 0;
	if (rel > ph - thumb_h) rel = ph - thumb_h;
	brain_page_set_scroll(gw, gw->scrollx,
			(ph - thumb_h > 0) ?
				rel * (eh - ph) / (ph - thumb_h) : 0);
}

static bool page_pointer(const tk_event *ev)
{
	struct gui_window *gw = brain_fe.active;
	int cx, cy;

	if (gw == NULL || gw->bw == NULL) {
		return false;
	}
	content_point(ev, &cx, &cy);

	switch (ev->type) {
	case TK_EV_POINTER_DOWN:
		/* single-finger touch device: every press is button 1 */
		if (in_scrollbar_strip(ev)) {
			brain_fe.drag.state = 3;
			scrollbar_drag_to(ev);
			return true;
		}
		browser_window_mouse_click(gw->bw,
				BROWSER_MOUSE_PRESS_1, cx, cy);
		brain_fe.drag.state = 1;
		brain_fe.drag.button = 1;
		brain_fe.drag.start_cx = cx;
		brain_fe.drag.start_cy = cy;
		brain_fe.drag.start_sx = gw->scrollx;
		brain_fe.drag.start_sy = gw->scrolly;
		return true;

	case TK_EV_POINTER_MOVE:
		if (brain_fe.drag.state == 3) {
			scrollbar_drag_to(ev);
			return true;
		}
		if (brain_fe.drag.state == 1) {
			int dx = cx - brain_fe.drag.start_cx;
			int dy = cy - brain_fe.drag.start_cy;
			if (abs(dx) > 5 || abs(dy) > 5) {
				browser_window_mouse_click(gw->bw,
						BROWSER_MOUSE_DRAG_1,
						brain_fe.drag.start_cx,
						brain_fe.drag.start_cy);
				brain_fe.drag.state = 2;
			}
			return true;
		}
		if (brain_fe.drag.state == 2) {
			browser_window_mouse_track(gw->bw,
					BROWSER_MOUSE_DRAG_ON |
					BROWSER_MOUSE_HOLDING_1,
					cx, cy);
			return true;
		}
		browser_window_mouse_track(gw->bw, 0, cx, cy);
		return true;

	case TK_EV_POINTER_UP:
		if (brain_fe.drag.state == 3) {
			brain_fe.drag.state = 0;
			return true;
		}
		if (brain_fe.drag.state == 2) {
			browser_window_mouse_track(gw->bw, 0, cx, cy);
			brain_fe.drag.state = 0;
			return true;
		}
		if (brain_fe.drag.state == 1) {
			browser_mouse_state mouse = BROWSER_MOUSE_CLICK_1;
			uint64_t now = brain_now_ms();
			brain_fe.drag.state = 0;
			if (now < brain_fe.drag.last_click_ms + 500) {
				brain_fe.drag.click_count++;
				if (brain_fe.drag.click_count == 2) {
					mouse |= BROWSER_MOUSE_DOUBLE_CLICK;
				} else if (brain_fe.drag.click_count >= 3) {
					mouse |= BROWSER_MOUSE_TRIPLE_CLICK;
					brain_fe.drag.click_count = 0;
				}
			} else {
				brain_fe.drag.click_count = 1;
			}
			brain_fe.drag.last_click_ms = now;
			browser_window_mouse_click(gw->bw, mouse, cx, cy);
			return true;
		}
		return false;

	case TK_EV_WHEEL:
		if (ev->wheel_dy != 0) {
			if (!browser_window_scroll_at_point(gw->bw,
					cx, cy, 0, ev->wheel_dy * 100)) {
				scroll_by(gw, 0, ev->wheel_dy * 100);
			}
			return true;
		}
		return false;

	default:
		return false;
	}
}

static bool page_key(const tk_event *ev)
{
	struct gui_window *gw = brain_fe.active;
	int pw, ph;

	if (gw == NULL || gw->bw == NULL) {
		return false;
	}
	if (ev->type == TK_EV_TEXT_INPUT) {
		/* committed text (physical keyboard or, in WM mode,
		 * the shell OSK): feed the core one codepoint at a
		 * time, like a keyboard would */
		bool handled = false;
		const char *s = ev->text;
		size_t len, off = 0;
		if (s == NULL) {
			return false;
		}
		len = strlen(s);
		while (off < len) {
			uint32_t cp = utf8_to_ucs4(s + off, len - off);
			if (browser_window_key_press(gw->bw, cp)) {
				handled = true;
			}
			off = utf8_next(s, len, off);
		}
		return handled;
	}
	if (ev->type != TK_EV_KEY_DOWN) {
		return false;
	}
	page_size(&pw, &ph);
	switch (ev->key) {
	case TK_KEY_UP:
		return browser_window_key_press(gw->bw, NS_KEY_UP);
	case TK_KEY_DOWN:
		return browser_window_key_press(gw->bw, NS_KEY_DOWN);
	case TK_KEY_LEFT:
		return browser_window_key_press(gw->bw, NS_KEY_LEFT);
	case TK_KEY_RIGHT:
		return browser_window_key_press(gw->bw, NS_KEY_RIGHT);
	case TK_KEY_ENTER:
		return browser_window_key_press(gw->bw, NS_KEY_CR);
	case TK_KEY_BACKSPACE:
		return browser_window_key_press(gw->bw, NS_KEY_DELETE_LEFT);
	case TK_KEY_DELETE:
		return browser_window_key_press(gw->bw, NS_KEY_DELETE_RIGHT);
	case TK_KEY_TAB:
		return browser_window_key_press(gw->bw, NS_KEY_TAB);
	case TK_KEY_ESCAPE:
		return browser_window_key_press(gw->bw, NS_KEY_ESCAPE);
	case TK_KEY_SPACE:
		return browser_window_key_press(gw->bw, ' ');
	case TK_KEY_PAGE_UP:
		if (!browser_window_key_press(gw->bw, NS_KEY_PAGE_UP)) {
			scroll_by(gw, 0, -ph);
		}
		return true;
	case TK_KEY_PAGE_DOWN:
		if (!browser_window_key_press(gw->bw, NS_KEY_PAGE_DOWN)) {
			scroll_by(gw, 0, ph);
		}
		return true;
	case TK_KEY_HOME:
		scroll_by(gw, 0, -gw->scrolly);
		return true;
	case TK_KEY_END:
		scroll_by(gw, 0, 0x3fffffff);
		return true;
	case TK_KEY_NONE:
		/* printable keys arrive with text on the key event */
		if (ev->text != NULL && ev->text[0] != '\0') {
			tk_event tev = *ev;
			tev.type = TK_EV_TEXT_INPUT;
			return page_key(&tev);
		}
		return false;
	default:
		return false;
	}
}

bool brain_page_event(const tk_event *ev)
{
	switch (ev->type) {
	case TK_EV_POINTER_DOWN:
	case TK_EV_POINTER_UP:
	case TK_EV_POINTER_MOVE:
	case TK_EV_WHEEL:
		return page_pointer(ev);
	case TK_EV_KEY_DOWN:
	case TK_EV_TEXT_INPUT:
		return page_key(ev);
	default:
		return false;
	}
}
