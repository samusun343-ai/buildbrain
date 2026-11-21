/*
 * Brainux "brain" frontend: main, display backends, event routing,
 * and the damage-journaled present cycle.
 *
 * Two display modes share everything above the present cycle:
 *
 *  - WM mode (default when a braindesk2 server answers): the page
 *    surface runs libnsfb target=shm over the WM client's back
 *    buffer.  The WM swaps buffers strictly, so damage is
 *    journaled per buffer: a buffer is repainted with everything
 *    it missed (its journal) plus the current frame's damage, and
 *    every present is added to the other buffer's journal.  A
 *    buffer seen for the first time (start, resize generation)
 *    has a full-screen journal entry.
 *  - Standalone mode: the surface renders into its shadow over
 *    the framebuffer device (XRGB8888 requested, so the toolkit
 *    paints into the same shadow), and nsfb_update() presents the
 *    union box of the frame damage.  Input comes from the
 *    surface's evdev reader, translated below.
 *
 * The toolkit side accumulates its own damage in the root; the
 * present cycle feeds the journal rects into the root damage so
 * chrome repaints exactly what the target buffer missed, then
 * reads the frame's real painted set back from the canvas damage
 * (plus the page rects the plotters drew, which bypass the
 * canvas).  Nothing is ever painted twice and nothing is copied:
 * page, chrome and overlay all write the destination buffer
 * directly.
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <libnsfb.h>
#include <libnsfb_event.h>

#include "utils/config.h"
#include "utils/errors.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "utils/utils.h"
#include "netsurf/browser.h"
#include "netsurf/browser_window.h"
#include "netsurf/misc.h"
#include "netsurf/window.h"
#include "netsurf/clipboard.h"
#include "netsurf/fetch.h"
#include "netsurf/bitmap.h"
#include "netsurf/layout.h"
#include "netsurf/url_db.h"
#include "content/backing_store.h"
#include "desktop/hotlist.h"
#include "netsurf/netsurf.h"

#include "wm.h"
#include "thememode.h"
#include "tk.h"

#include "brain/gui.h"
#include "brain/surface.h"
#include "brain/schedule.h"
#include "brain/fetch.h"
#include "brain/findfile.h"
#include "brain/clipboard.h"
#include "brain/bitmap.h"
#include "brain/font.h"
#include "brain/brainreg.h"

struct brain_frontend brain_fe;

static void die(const char *error)
{
	fprintf(stderr, "%s", error);
	exit(1);
}

uint64_t brain_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void brain_quit(void)
{
	brain_fe.quit = true;
}

/* ------------------------------------------------------------------ */
/* damage bookkeeping                                                 */
/* ------------------------------------------------------------------ */

static bool bbox_empty(const nsfb_bbox_t *b)
{
	return b->x0 >= b->x1 || b->y0 >= b->y1;
}

static void rect_list_add(nsfb_bbox_t *list, int *n, nsfb_bbox_t r)
{
	int i;

	if (bbox_empty(&r)) {
		return;
	}
	/* merge with an overlapping/abutting rect when possible */
	for (i = 0; i < *n; i++) {
		nsfb_bbox_t *o = &list[i];
		if (r.x0 <= o->x1 && r.x1 >= o->x0 &&
		    r.y0 <= o->y1 && r.y1 >= o->y0) {
			if (r.x0 < o->x0) o->x0 = r.x0;
			if (r.y0 < o->y0) o->y0 = r.y0;
			if (r.x1 > o->x1) o->x1 = r.x1;
			if (r.y1 > o->y1) o->y1 = r.y1;
			return;
		}
	}
	if (*n >= BRAIN_JOURNAL_RECTS) {
		/* collapse to a single bounding box */
		for (i = 0; i < *n; i++) {
			if (list[i].x0 < r.x0) r.x0 = list[i].x0;
			if (list[i].y0 < r.y0) r.y0 = list[i].y0;
			if (list[i].x1 > r.x1) r.x1 = list[i].x1;
			if (list[i].y1 > r.y1) r.y1 = list[i].y1;
		}
		list[0] = r;
		*n = 1;
		return;
	}
	list[(*n)++] = r;
}

void brain_invalidate(nsfb_bbox_t rect)
{
	/* clip to the window */
	if (rect.x0 < 0) rect.x0 = 0;
	if (rect.y0 < 0) rect.y0 = 0;
	if (rect.x1 > brain_fe.width) rect.x1 = brain_fe.width;
	if (rect.y1 > brain_fe.height) rect.y1 = brain_fe.height;
	rect_list_add(brain_fe.pending, &brain_fe.npending, rect);
}

void brain_invalidate_all(void)
{
	nsfb_bbox_t full = { 0, 0, brain_fe.width, brain_fe.height };
	brain_fe.npending = 0;
	rect_list_add(brain_fe.pending, &brain_fe.npending, full);
}

void brain_request_present(void)
{
	/* presents happen once per main-loop iteration; nothing to
	 * do here beyond what brain_invalidate already recorded */
}

/* ------------------------------------------------------------------ */
/* present cycle                                                      */
/* ------------------------------------------------------------------ */

static struct brain_journal *journal_for(void *ptr, bool create)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (brain_fe.journal[i].valid &&
		    brain_fe.journal[i].ptr == ptr) {
			return &brain_fe.journal[i];
		}
	}
	if (!create) {
		return NULL;
	}
	for (i = 0; i < 2; i++) {
		if (!brain_fe.journal[i].valid) {
			brain_fe.journal[i].valid = true;
			brain_fe.journal[i].ptr = ptr;
			brain_fe.journal[i].nrects = 0;
			/* a buffer we have never presented starts out
			 * missing everything */
			rect_list_add(brain_fe.journal[i].rects,
					&brain_fe.journal[i].nrects,
					(nsfb_bbox_t){ 0, 0, brain_fe.width,
							brain_fe.height });
			return &brain_fe.journal[i];
		}
	}
	return NULL;
}

static void present(void)
{
	void *pixels;
	int stride_px = 0;
	struct brain_journal *slot;
	nsfb_bbox_t need[BRAIN_JOURNAL_RECTS * 2];
	int nneed = 0;
	nsfb_bbox_t painted[BRAIN_JOURNAL_RECTS * 2];
	int npainted = 0;
	nsfb_bbox_t page;
	tk_canvas *canvas;
	int i;
	bool page_touched = false;

	if (brain_fe.root == NULL) {
		return;
	}

	/* acquire the target buffer */
	if (brain_fe.mode == BRAIN_MODE_WM) {
		uint32_t *px = bd_wm_client_pixels(brain_fe.wm, &stride_px);
		if (px == NULL) {
			return; /* both buffers are with the server */
		}
		pixels = px;
		if (pixels != brain_fe.presented_ptr) {
			brain_surface_repoint(brain_fe.nsfb, pixels,
					stride_px * 4);
		}
	} else {
		unsigned char *buf;
		int stride_bytes = 0;
		nsfb_get_buffer(brain_fe.nsfb, &buf, &stride_bytes);
		pixels = buf;
		stride_px = stride_bytes / 4;
		(void)stride_px;
	}
	if (pixels == NULL) {
		return;
	}

	slot = journal_for(pixels, true);
	if (slot == NULL) {
		return;
	}

	/* what this buffer needs: its journal plus the frame damage */
	for (i = 0; i < slot->nrects; i++) {
		rect_list_add(need, &nneed, slot->rects[i]);
	}
	for (i = 0; i < brain_fe.npending; i++) {
		rect_list_add(need, &nneed, brain_fe.pending[i]);
	}
	if (nneed == 0 && tk_root_damage_count(brain_fe.root) == 0) {
		return;
	}

	brain_page_geometry(&page);

	/* Split the needed rects: page parts are redrawn through
	 * the plotters below; chrome parts go to the root damage so
	 * the toolkit repaints exactly what this buffer missed.
	 * Page rects must NOT be fed to the root: its damage paint
	 * starts with a background fill, which would erase page
	 * pixels the plotters are responsible for.  For the same
	 * reason the root paints FIRST and the page second: any
	 * root-internal damage that reaches into the page area
	 * (focus-outline damage on the page holder, an overlay that
	 * just closed) is repaired by the page redraw, and the
	 * frontend forces a full page redraw on the transitions
	 * that can cause such damage (focus change to/from the
	 * page, overlay close). */
	for (i = 0; i < nneed; i++) {
		nsfb_bbox_t r = need[i];
		nsfb_bbox_t pr = r;
		if (pr.x0 < page.x0) pr.x0 = page.x0;
		if (pr.y0 < page.y0) pr.y0 = page.y0;
		if (pr.x1 > page.x1) pr.x1 = page.x1;
		if (pr.y1 > page.y1) pr.y1 = page.y1;
		if (!bbox_empty(&pr)) {
			rect_list_add(painted, &npainted, pr);
			page_touched = true;
		}
		/* chrome strips of r around the page rect */
		if (r.y0 < page.y0) {
			tk_root_invalidate_rect(brain_fe.root,
				(tk_rect){ r.x0, r.y0, r.x1 - r.x0,
					page.y0 - r.y0 });
		}
		if (r.y1 > page.y1) {
			tk_root_invalidate_rect(brain_fe.root,
				(tk_rect){ r.x0, page.y1, r.x1 - r.x0,
					r.y1 - page.y1 });
		}
		if (r.x0 < page.x0 && r.y1 > page.y0 && r.y0 < page.y1) {
			int y0 = r.y0 > page.y0 ? r.y0 : page.y0;
			int y1 = r.y1 < page.y1 ? r.y1 : page.y1;
			tk_root_invalidate_rect(brain_fe.root,
				(tk_rect){ r.x0, y0, page.x0 - r.x0,
					y1 - y0 });
		}
		if (r.x1 > page.x1 && r.y1 > page.y0 && r.y0 < page.y1) {
			int y0 = r.y0 > page.y0 ? r.y0 : page.y0;
			int y1 = r.y1 < page.y1 ? r.y1 : page.y1;
			tk_root_invalidate_rect(brain_fe.root,
				(tk_rect){ page.x1, y0, r.x1 - page.x1,
					y1 - y0 });
		}
	}

	canvas = tk_canvas_wrap(brain_fe.width, brain_fe.height,
			(uint32_t *)pixels);
	if (canvas == NULL) {
		return;
	}
	tk_canvas_damage_clear(canvas);
	tk_root_paint_damage(brain_fe.root, canvas, tk_theme_default());

	/* page parts, drawn after the chrome so nothing the root
	 * filled over the page area survives */
	for (i = 0; i < npainted; i++) {
		brain_page_redraw(brain_fe.active, &painted[i]);
	}
	if (page_touched) {
		brain_page_overlay_paint(canvas);
	}
	/* toolkit overlays (menu/dialogs) sit over the page: the
	 * page redraw above just painted over their lower half.
	 * Repaint them on top (their rects only).  The transient
	 * page-area overlays (progress line, status bubble) need
	 * the same rescue while they are up. */
	if (page_touched && (brain_overlay_open() ||
			brain_transient_overlay_visible())) {
		brain_overlays_invalidate();
		tk_root_paint_damage(brain_fe.root, canvas,
				tk_theme_default());
	}
	/* the status bubble lives inside the page area but is a
	 * toolkit widget: paint it manually over the page (it is
	 * kept out of the root damage path on purpose) */
	if (brain_fe.status_bubble != NULL &&
	    tk_is_visible(brain_fe.status_bubble)) {
		tk_paint(brain_fe.status_bubble, canvas,
				tk_theme_default());
	}
	/* the frame's real painted set: everything the canvas
	 * recorded (chrome, fills, overlay, bubble) plus the page
	 * rects the plotters drew (recorded in `painted` above) */
	{
		size_t n = tk_canvas_damage_count(canvas);
		size_t k;
		for (k = 0; k < n; k++) {
			tk_rect tr = tk_canvas_damage_rect(canvas, k);
			rect_list_add(painted, &npainted,
					(nsfb_bbox_t){ tr.x, tr.y,
						tr.x + tr.w, tr.y + tr.h });
		}
	}
	tk_canvas_free(canvas);

	if (npainted == 0) {
		slot->nrects = 0;
		brain_fe.npending = 0;
		return;
	}

	/* present */
	if (brain_fe.mode == BRAIN_MODE_WM) {
		tk_rect dmg[BRAIN_JOURNAL_RECTS * 2];
		int n = 0;
		for (i = 0; i < npainted && n < BRAIN_JOURNAL_RECTS * 2; i++) {
			dmg[n++] = (tk_rect){ painted[i].x0, painted[i].y0,
				painted[i].x1 - painted[i].x0,
				painted[i].y1 - painted[i].y0 };
		}
		if (bd_wm_client_commit(brain_fe.wm, dmg, n) != 0) {
			return; /* buffer not accepted; retry next frame */
		}
	} else {
		nsfb_bbox_t box = painted[0];
		for (i = 1; i < npainted; i++) {
			if (painted[i].x0 < box.x0) box.x0 = painted[i].x0;
			if (painted[i].y0 < box.y0) box.y0 = painted[i].y0;
			if (painted[i].x1 > box.x1) box.x1 = painted[i].x1;
			if (painted[i].y1 > box.y1) box.y1 = painted[i].y1;
		}
		nsfb_update(brain_fe.nsfb, &box);
	}
	brain_fe.presented_ptr = pixels;

	/* journal: this buffer is current; the other buffer missed
	 * everything we just painted */
	slot->nrects = 0;
	for (i = 0; i < 2; i++) {
		struct brain_journal *o = &brain_fe.journal[i];
		int k;
		if (o == slot || !o->valid) {
			continue;
		}
		for (k = 0; k < npainted; k++) {
			rect_list_add(o->rects, &o->nrects, painted[k]);
		}
	}
	brain_fe.npending = 0;
}

/* ------------------------------------------------------------------ */
/* event routing                                                      */
/* ------------------------------------------------------------------ */

static bool point_in_page(int x, int y)
{
	nsfb_bbox_t page;
	brain_page_geometry(&page);
	return x >= page.x0 && x < page.x1 && y >= page.y0 && y < page.y1;
}

static void update_osk_visibility(void)
{
	bool want;

	if (brain_fe.mode != BRAIN_MODE_FB || brain_fe.osk == NULL) {
		return;
	}
	want = (tk_focused(brain_fe.root) == brain_fe.url_field);
	if (want == brain_fe.osk_shown) {
		return;
	}
	brain_fe.osk_shown = want;
	tk_set_visible(brain_fe.osk, want);
	tk_root_layout(brain_fe.root, brain_fe.width, brain_fe.height);
	brain_chrome_layout();
	brain_invalidate_all();
	if (brain_fe.active != NULL) {
		brain_page_full_redraw(brain_fe.active);
	}
}

static void dispatch_event(tk_event *ev)
{
	bool handled = false;
	bool page_focus_before =
		(tk_focused(brain_fe.root) == brain_fe.page_holder);
	bool url_focus_before =
		(tk_focused(brain_fe.root) == brain_fe.url_field);

	/* URL field, tapped while already focused but showing the
	 * steady-state display form (e.g. right after Enter): the
	 * tap re-enters edit mode with the full URL before the
	 * event is routed, so the caret lands in the real text. */
	if (ev->type == TK_EV_POINTER_DOWN && url_focus_before &&
	    !brain_fe.url_editing && brain_fe.url_field != NULL) {
		tk_rect ur = tk_widget_rect(brain_fe.url_field);
		if (ev->x >= ur.x && ev->x < ur.x + ur.w &&
		    ev->y >= ur.y && ev->y < ur.y + ur.h) {
			brain_url_begin_edit();
		}
	}

	if (ev->type == TK_EV_POINTER_DOWN ||
	    ev->type == TK_EV_POINTER_UP ||
	    ev->type == TK_EV_POINTER_MOVE ||
	    ev->type == TK_EV_WHEEL) {
		if (brain_fe.drag.state != 0) {
			/* a page drag is in flight: it owns the pointer
			 * until the up, wherever it lands */
			handled = brain_page_event(ev);
		} else if (brain_overlay_open()) {
			handled = tk_root_event(brain_fe.root, ev);
		} else if (point_in_page(ev->x, ev->y)) {
			if (ev->type == TK_EV_POINTER_DOWN) {
				tk_focus(brain_fe.root, brain_fe.page_holder);
			}
			handled = brain_page_event(ev);
		} else {
			handled = tk_root_event(brain_fe.root, ev);
		}
	} else {
		handled = tk_root_event(brain_fe.root, ev);
		if (!handled &&
		    ev->type == TK_EV_KEY_DOWN &&
		    ev->key == TK_KEY_ENTER &&
		    tk_focused(brain_fe.root) == brain_fe.url_field) {
			char buf[2048];
			tk_textfield_text(brain_fe.url_field, buf,
					sizeof(buf));
			brain_navigate(buf);
			/* back to the display form for the new URL */
			brain_url_end_edit();
			handled = true;
		} else if (!handled &&
			   tk_focused(brain_fe.root) ==
					brain_fe.page_holder) {
			handled = brain_page_event(ev);
		}
	}

	/* URL field focus transitions drive edit mode: gaining
	 * focus restores the full URL (selected); losing it
	 * returns the field to the steady-state display form. */
	if (!url_focus_before &&
	    tk_focused(brain_fe.root) == brain_fe.url_field) {
		brain_url_begin_edit();
	} else if (url_focus_before &&
		   tk_focused(brain_fe.root) != brain_fe.url_field) {
		brain_url_end_edit();
	}

	/* A focus change to or from the page holder makes the root
	 * add the holder's whole rect to its damage, and its paint
	 * fills that with the theme background before painting
	 * widgets: pair every such transition with a full page
	 * redraw so the present cycle repairs it (see present()). */
	if ((tk_focused(brain_fe.root) == brain_fe.page_holder) !=
			page_focus_before &&
	    brain_fe.active != NULL) {
		brain_page_full_redraw(brain_fe.active);
	}

	if (brain_fe.mode == BRAIN_MODE_WM) {
		/* keep the shell's OSK state in step with our focus,
		 * and add the page caret (web form focus) on top: the
		 * toolkit-derived state knows nothing of it */
		bool synced = bd_wm_client_im_sync(brain_fe.wm,
				brain_fe.root);
		(void)synced;
		if (tk_focused(brain_fe.root) == brain_fe.page_holder &&
		    brain_fe.active != NULL &&
		    brain_fe.active->caret_h > 0) {
			tk_rect field = {
				brain_fe.active->caret_x,
				brain_fe.active->caret_y,
				2, brain_fe.active->caret_h
			};
			bd_wm_client_im_notify(brain_fe.wm, true, &field);
		}
	} else {
		update_osk_visibility();
	}
}

/* ------------------------------------------------------------------ */
/* WM mode events                                                     */
/* ------------------------------------------------------------------ */

static void handle_resize(int w, int h)
{
	if (w < 300) w = 300;
	if (h < 200) h = 200;
	brain_fe.width = w;
	brain_fe.height = h;
	nsfb_set_geometry(brain_fe.nsfb, w, h, NSFB_FMT_XRGB8888);
	tk_root_layout(brain_fe.root, w, h);
	brain_chrome_layout();
	/* buffers are a new generation: forget the journals */
	brain_fe.journal[0].valid = false;
	brain_fe.journal[1].valid = false;
	brain_fe.presented_ptr = NULL;
	brain_invalidate_all();
	if (brain_fe.active != NULL) {
		brain_page_full_redraw(brain_fe.active);
	}
}

static void wm_poll(int timeout_ms)
{
	bd_wm_client_event ev;
	int rc;

	while ((rc = bd_wm_client_poll(brain_fe.wm, &ev, timeout_ms)) > 0) {
		tk_event tev = { 0 };
		timeout_ms = 0;
		switch (ev.type) {
		case BD_WM_CEV_CLOSE:
			brain_quit();
			return;
		case BD_WM_CEV_RESIZED:
			handle_resize(ev.w, ev.h);
			break;
		case BD_WM_CEV_COMPOSITION:
			tev.type = TK_EV_COMPOSITION;
			tev.text = ev.comp[0] ? ev.comp : NULL;
			tk_root_event(brain_fe.root, &tev);
			break;
		case BD_WM_CEV_POINTER_DOWN:
		case BD_WM_CEV_POINTER_UP:
		case BD_WM_CEV_POINTER_MOVE:
			tev.type = ev.type == BD_WM_CEV_POINTER_DOWN ?
					TK_EV_POINTER_DOWN :
					ev.type == BD_WM_CEV_POINTER_UP ?
					TK_EV_POINTER_UP :
					TK_EV_POINTER_MOVE;
			tev.x = ev.x;
			tev.y = ev.y;
			dispatch_event(&tev);
			break;
		case BD_WM_CEV_KEY_DOWN:
		case BD_WM_CEV_KEY_UP:
			tev.type = ev.type == BD_WM_CEV_KEY_DOWN ?
					TK_EV_KEY_DOWN : TK_EV_KEY_UP;
			tev.key = (tk_key)ev.key;
			tev.modifiers = ev.mods;
			tev.text = ev.text[0] ? ev.text : NULL;
			dispatch_event(&tev);
			break;
		default:
			break;
		}
		if (brain_fe.quit) {
			return;
		}
	}
	if (rc < 0) {
		brain_quit();
	}
}

/* ------------------------------------------------------------------ */
/* standalone mode events (libnsfb surface input)                     */
/* ------------------------------------------------------------------ */

static int mouse_x, mouse_y;

static void fb_translate_key(nsfb_event_t *ev, bool down)
{
	tk_event tev = { 0 };
	static char textbuf[8];
	int code = ev->value.keycode;

	tev.type = down ? TK_EV_KEY_DOWN : TK_EV_KEY_UP;
	switch (code) {
	case NSFB_KEY_MOUSE_1:
		tev.type = down ? TK_EV_POINTER_DOWN : TK_EV_POINTER_UP;
		tev.x = mouse_x;
		tev.y = mouse_y;
		break;
	case NSFB_KEY_MOUSE_4:
		if (down) {
			tev.type = TK_EV_WHEEL;
			tev.x = mouse_x;
			tev.y = mouse_y;
			tev.wheel_dy = -1;
		} else {
			return;
		}
		break;
	case NSFB_KEY_MOUSE_5:
		if (down) {
			tev.type = TK_EV_WHEEL;
			tev.x = mouse_x;
			tev.y = mouse_y;
			tev.wheel_dy = 1;
		} else {
			return;
		}
		break;
	case NSFB_KEY_BACKSPACE:
		tev.key = TK_KEY_BACKSPACE;
		break;
	case NSFB_KEY_TAB:
		tev.key = TK_KEY_TAB;
		break;
	case NSFB_KEY_RETURN:
	case NSFB_KEY_LF:
		tev.key = TK_KEY_ENTER;
		break;
	case NSFB_KEY_ESCAPE:
		tev.key = TK_KEY_ESCAPE;
		break;
	case NSFB_KEY_DELETE:
		tev.key = TK_KEY_DELETE;
		break;
	case NSFB_KEY_LEFT:
		tev.key = TK_KEY_LEFT;
		break;
	case NSFB_KEY_RIGHT:
		tev.key = TK_KEY_RIGHT;
		break;
	case NSFB_KEY_UP:
		tev.key = TK_KEY_UP;
		break;
	case NSFB_KEY_DOWN:
		tev.key = TK_KEY_DOWN;
		break;
	case NSFB_KEY_HOME:
		tev.key = TK_KEY_HOME;
		break;
	case NSFB_KEY_END:
		tev.key = TK_KEY_END;
		break;
	case NSFB_KEY_PAGEUP:
		tev.key = TK_KEY_PAGE_UP;
		break;
	case NSFB_KEY_PAGEDOWN:
		tev.key = TK_KEY_PAGE_DOWN;
		break;
	case NSFB_KEY_SPACE:
		tev.key = TK_KEY_SPACE;
		break;
	default:
		if (code >= 0x20 && code < 0x110000) {
			/* printable: carry the UTF-8 form */
			char *p = textbuf;
			uint32_t cp = (uint32_t)code;
			if (cp < 0x80) {
				*p++ = (char)cp;
			} else if (cp < 0x800) {
				*p++ = (char)(0xc0 | (cp >> 6));
				*p++ = (char)(0x80 | (cp & 0x3f));
			} else if (cp < 0x10000) {
				*p++ = (char)(0xe0 | (cp >> 12));
				*p++ = (char)(0x80 | ((cp >> 6) & 0x3f));
				*p++ = (char)(0x80 | (cp & 0x3f));
			} else {
				*p++ = (char)(0xf0 | (cp >> 18));
				*p++ = (char)(0x80 | ((cp >> 12) & 0x3f));
				*p++ = (char)(0x80 | ((cp >> 6) & 0x3f));
				*p++ = (char)(0x80 | (cp & 0x3f));
			}
			*p = '\0';
			tev.text = textbuf;
		} else {
			return;
		}
		break;
	}
	dispatch_event(&tev);
}

static void fb_poll(int timeout_ms)
{
	nsfb_event_t ev;

	for (;;) {
		if (!nsfb_event(brain_fe.nsfb, &ev, timeout_ms)) {
			return;
		}
		timeout_ms = 0;
		switch (ev.type) {
		case NSFB_EVENT_CONTROL:
			if (ev.value.controlcode == NSFB_CONTROL_QUIT) {
				brain_quit();
				return;
			}
			break;
		case NSFB_EVENT_MOVE_ABSOLUTE:
			mouse_x = ev.value.vector.x;
			mouse_y = ev.value.vector.y;
			{
				tk_event tev = { 0 };
				tev.type = TK_EV_POINTER_MOVE;
				tev.x = mouse_x;
				tev.y = mouse_y;
				dispatch_event(&tev);
			}
			break;
		case NSFB_EVENT_KEY_DOWN:
			fb_translate_key(&ev, true);
			break;
		case NSFB_EVENT_KEY_UP:
			fb_translate_key(&ev, false);
			break;
		case NSFB_EVENT_RESIZE:
			handle_resize(ev.value.resize.w, ev.value.resize.h);
			break;
		default:
			break;
		}
		if (brain_fe.quit) {
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* main loop                                                          */
/* ------------------------------------------------------------------ */

static void brain_run(void)
{
	bool overlay_was_open = false;

	while (!brain_fe.quit) {
		int timeout = schedule_run();
		bool overlay_now = brain_overlay_open();

		/* an overlay (menu/dialog) that just closed leaves its
		 * rect in the root damage; the root will bg-fill it.
		 * Repair with a full page redraw, as for focus
		 * transitions (see present()). */
		if (overlay_was_open && !overlay_now &&
		    brain_fe.active != NULL) {
			brain_page_full_redraw(brain_fe.active);
		}
		overlay_was_open = overlay_now;

		if (timeout < 0 || timeout > 100) {
			timeout = 100;
		}
		/* pending damage means: do not sleep, present first */
		if (brain_fe.npending > 0 ||
		    tk_root_damage_count(brain_fe.root) > 0) {
			timeout = 0;
		}
		tk_root_tick(brain_fe.root, (uint32_t)brain_now_ms());
		if (brain_fe.mode == BRAIN_MODE_WM) {
			wm_poll(timeout);
		} else {
			fb_poll(timeout);
		}
		present();
	}
}

/* ------------------------------------------------------------------ */
/* initialisation                                                     */
/* ------------------------------------------------------------------ */

static nserror set_defaults(struct nsoption_s *defaults)
{
	const char *home = getenv("HOME");
	static char cookies[PATH_MAX];
	static char hotlist[PATH_MAX];
	static char urlfile[PATH_MAX];

	if (home != NULL && home[0] != '\0') {
		snprintf(cookies, sizeof(cookies), "%s/.netsurf/Cookies",
				home);
		snprintf(hotlist, sizeof(hotlist), "%s/.netsurf/hotlist",
				home);
		snprintf(urlfile, sizeof(urlfile), "%s/.netsurf/urls",
				home);
		nsoption_setnull_charp(cookie_file, strdup(cookies));
		nsoption_setnull_charp(cookie_jar, strdup(cookies));
		nsoption_setnull_charp(hotlist_path, strdup(hotlist));
		nsoption_setnull_charp(url_file, strdup(urlfile));
	}

	/* system colours for page furniture (form controls etc.) */
	nsoption_set_colour(sys_colour_ActiveBorder, 0x00000000);
	nsoption_set_colour(sys_colour_ActiveCaption, 0x00ddddcc);
	nsoption_set_colour(sys_colour_AppWorkspace, 0x00eeeeee);
	nsoption_set_colour(sys_colour_Background, 0x00aa0000);
	nsoption_set_colour(sys_colour_ButtonFace, 0x00dddddd);
	nsoption_set_colour(sys_colour_ButtonHighlight, 0x00cccccc);
	nsoption_set_colour(sys_colour_ButtonShadow, 0x00bbbbbb);
	nsoption_set_colour(sys_colour_ButtonText, 0x00000000);
	nsoption_set_colour(sys_colour_CaptionText, 0x00000000);
	nsoption_set_colour(sys_colour_GrayText, 0x00777777);
	nsoption_set_colour(sys_colour_Highlight, 0x00ee0000);
	nsoption_set_colour(sys_colour_HighlightText, 0x00000000);
	nsoption_set_colour(sys_colour_InactiveBorder, 0x00000000);
	nsoption_set_colour(sys_colour_InactiveCaption, 0x00ffffff);
	nsoption_set_colour(sys_colour_InactiveCaptionText, 0x00cccccc);
	nsoption_set_colour(sys_colour_InfoBackground, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_InfoText, 0x00000000);
	nsoption_set_colour(sys_colour_Menu, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_MenuText, 0x00000000);
	nsoption_set_colour(sys_colour_Scrollbar, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_ThreeDDarkShadow, 0x00555555);
	nsoption_set_colour(sys_colour_ThreeDFace, 0x00dddddd);
	nsoption_set_colour(sys_colour_ThreeDHighlight, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_ThreeDLightShadow, 0x00999999);
	nsoption_set_colour(sys_colour_ThreeDShadow, 0x00777777);
	nsoption_set_colour(sys_colour_Window, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_WindowFrame, 0x00000000);
	nsoption_set_colour(sys_colour_WindowText, 0x00000000);

	return NSERROR_OK;
}

static bool nslog_stream_configure(FILE *fptr)
{
	setbuf(fptr, NULL);
	return true;
}

static void gui_quit(void)
{
	urldb_save_cookies(nsoption_charp(cookie_jar));
	if (nsoption_charp(url_file) != NULL) {
		urldb_save(nsoption_charp(url_file));
	}
	hotlist_fini();
	brain_brainreg_store();
	brain_session_save();
}

/** Resolve the page font: the same typeface as the toolkit.
 * Sets the fb_face_* options to the first candidate that exists,
 * so font_freetype finds it without searching. */
static void resolve_page_font(void)
{
	static const char *candidates[] = {
		NULL, /* $BRAINUI_FONT_PATH */
		"../../../fonts/brainui-jp-subset.ttf",
		"./fonts/brainui-jp-subset.ttf",
		/* the toolkit's installed location (same /usr/local
		 * prefix as libbrainui2 itself; tk_font.c's first
		 * absolute candidate) */
		"/usr/local/share/fonts/libbrainui2/brainui-jp-subset.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		NULL,
	};
	const char *env = getenv("BRAINUI_FONT_PATH");
	const char *chosen = NULL;
	int i;

	candidates[0] = env;
	for (i = 0; candidates[i] != NULL; i++) {
		if (access(candidates[i], R_OK) == 0) {
			chosen = candidates[i];
			break;
		}
	}
	if (chosen == NULL) {
		return; /* font_freetype falls back to its respath search */
	}
	nsoption_set_charp(fb_face_sans_serif, strdup(chosen));
	nsoption_set_charp(fb_face_sans_serif_bold, strdup(chosen));
	nsoption_set_charp(fb_face_sans_serif_italic, strdup(chosen));
	nsoption_set_charp(fb_face_sans_serif_italic_bold, strdup(chosen));
	nsoption_set_charp(fb_face_serif, strdup(chosen));
	nsoption_set_charp(fb_face_serif_bold, strdup(chosen));
	nsoption_set_charp(fb_face_monospace, strdup(chosen));
	nsoption_set_charp(fb_face_monospace_bold, strdup(chosen));
	nsoption_set_charp(fb_face_cursive, strdup(chosen));
	nsoption_set_charp(fb_face_fantasy, strdup(chosen));
}

static struct gui_misc_table brain_misc_table = {
	.schedule = brain_schedule,
	.quit = gui_quit,
};

int main(int argc, char **argv)
{
	char *options;
	char *messages;
	const char *initial_url = NULL;
	nserror ret;
	struct netsurf_table brain_table = {
		.misc = &brain_misc_table,
		.window = &brain_window_table,
		.clipboard = brain_clipboard_table,
		.fetch = brain_fetch_table,
		.utf8 = brain_utf8_table,
		.bitmap = brain_bitmap_table,
		.layout = brain_layout_table,
		.llcache = filesystem_llcache_table,
	};
	const char *home = getenv("HOME");
	char store_path[PATH_MAX];
	int i;

	memset(&brain_fe, 0, sizeof(brain_fe));
	brain_fe.width = 800;
	brain_fe.height = 480;

	ret = netsurf_register(&brain_table);
	if (ret != NSERROR_OK) {
		die("NetSurf operation table failed registration");
	}

	respaths = brain_init_resource_path(NETSURF_BRAIN_RESPATH);

	nslog_init(nslog_stream_configure, &argc, argv);

	ret = nsoption_init(set_defaults, &nsoptions, &nsoptions_default);
	if (ret != NSERROR_OK) {
		die("Options failed to initialise");
	}
	options = filepath_find(respaths, "Choices");
	nsoption_read(options, nsoptions);
	free(options);
	nsoption_commandline(&argc, argv, nsoptions);

	/* brainreg overlays the Choices file: it is the settings
	 * store of record for this frontend (DESIGN section 5) */
	brain_brainreg_apply();

	/* display mode: option, then environment, then auto */
	{
		const char *mode = getenv("BRAIN_MODE");
		if (mode == NULL) {
			mode = nsoption_charp(brain_mode);
		}
		if (mode != NULL && strcmp(mode, "fb") == 0) {
			brain_fe.mode = BRAIN_MODE_FB;
		} else if (mode != NULL && strcmp(mode, "wm") == 0) {
			brain_fe.mode = BRAIN_MODE_WM;
		} else {
			brain_fe.mode = BRAIN_MODE_WM; /* auto: try WM first */
		}
	}

	messages = filepath_find(respaths, "Messages");
	ret = messages_add_from_file(messages);
	free(messages);
	if (ret != NSERROR_OK) {
		fprintf(stderr, "Message translations failed to load\n");
	}

	/* the backing store lives below ~/.netsurf/cache: on the
	 * device that is the microSD Brainux area (eMMC untouched) */
	store_path[0] = '\0';
	if (home != NULL && home[0] != '\0') {
		char nsdir[PATH_MAX];
		snprintf(nsdir, sizeof(nsdir), "%s/.netsurf", home);
		mkdir(nsdir, 0755);
		snprintf(store_path, sizeof(store_path),
				"%s/.netsurf/cache", home);
	}
	ret = netsurf_init(store_path[0] != '\0' ? store_path : NULL);
	if (ret != NSERROR_OK) {
		die("NetSurf failed to initialise");
	}

	/* no frontend popup for form <select>: the core draws it */
	nsoption_set_bool(core_select_menu, true);

	/* persistent data: history + bookmarks + cookies */
	if (nsoption_charp(url_file) != NULL) {
		urldb_load(nsoption_charp(url_file));
	}
	hotlist_init(nsoption_charp(hotlist_path),
			nsoption_charp(hotlist_path));
	urldb_load_cookies(nsoption_charp(cookie_file));

	/* toolkit: display-mode theme + the bundled font, before
	 * any widget is built (widgets read both at measure/paint
	 * time).  The mode comes from brainreg ui/mode + ui/dark
	 * through bd_theme_from_reg() (shell thememode.c, compiled
	 * in like reg.c): android -> tk_theme_v2[_dark], winui ->
	 * tk_theme_winui[_dark].  This is the same contract the
	 * shell and the apps follow; before this the browser was
	 * the only process hardcoding the Android light theme. */
	{
		const tk_theme *theme = bd_theme_from_reg();
		tk_theme_set_default(theme);
		brain_fe.winui = (theme->style == 2);
		brain_fe.dark = (theme->dark != 0);
		NSLOG(netsurf, INFO, "ui mode: %s%s",
		      brain_fe.winui ? "winui" : "android",
		      brain_fe.dark ? " (dark)" : "");
	}
	{
		tk_font *font = tk_font_load_default();
		if (font != NULL) {
			tk_font_install(font);
		}
	}
	resolve_page_font();
	if (fb_font_init() == false) {
		die("Unable to initialise the font system");
	}

	/* display backend + page surface */
	if (brain_fe.mode == BRAIN_MODE_WM) {
		brain_fe.wm = bd_wm_client_open(NULL, "ブラウザ",
				brain_fe.width, brain_fe.height);
		if (brain_fe.wm == NULL) {
			NSLOG(netsurf, INFO,
			      "no WM server: falling back to standalone");
			brain_fe.mode = BRAIN_MODE_FB;
		} else {
			int stride = 0;
			uint32_t *px;
			brain_fe.width = bd_wm_client_width(brain_fe.wm);
			brain_fe.height = bd_wm_client_height(brain_fe.wm);
			px = bd_wm_client_pixels(brain_fe.wm, &stride);
			brain_fe.nsfb = brain_surface_create_shm(px,
					stride * 4, brain_fe.width,
					brain_fe.height);
			if (brain_fe.nsfb == NULL) {
				die("Unable to initialise shm surface");
			}
		}
	}
	if (brain_fe.mode == BRAIN_MODE_FB) {
		const char *fbdev = getenv("BRAIN_FBDEV");
		const char *input = nsoption_charp(brain_input);
		if (fbdev == NULL) {
			fbdev = nsoption_charp(brain_fbdev);
		}
		if (fbdev == NULL) {
			fbdev = "/dev/fb0";
		}
		brain_fe.nsfb = brain_surface_create_fb(fbdev, input,
				brain_fe.width, brain_fe.height);
		if (brain_fe.nsfb == NULL) {
			die("Unable to initialise framebuffer surface");
		}
		/* a real (or fake) device may impose its own size */
		{
			enum nsfb_format_e fmt;
			nsfb_get_geometry(brain_fe.nsfb, &brain_fe.width,
					&brain_fe.height, &fmt);
		}
	}

	/* chrome */
	brain_chrome_build();
	brain_invalidate_all();

	/* first tab: command line, session restore, or homepage */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-' && strchr(argv[i], ':') != NULL) {
			initial_url = argv[i];
			break;
		}
		if (argv[i][0] != '-' && strchr(argv[i], '.') != NULL) {
			initial_url = argv[i];
			break;
		}
	}
	if (initial_url != NULL) {
		brain_open_tab(initial_url, false);
	} else {
		brain_session_restore();
		if (brain_fe.n_windows == 0) {
			const char *hp = nsoption_charp(homepage_url);
			if (hp == NULL || hp[0] == '\0') {
				hp = "resource:welcome.html";
			}
			brain_open_tab(hp, false);
		}
	}
	brain_fe.session_started = true;
	brain_chrome_sync();

	brain_run();

	/* teardown */
	while (brain_fe.windows != NULL) {
		struct gui_window *gw = brain_fe.windows;
		brain_fe.session_started = false;
		browser_window_destroy(gw->bw);
	}
	if (brain_fe.mode == BRAIN_MODE_WM && brain_fe.wm != NULL) {
		bd_wm_client_close(brain_fe.wm);
	}
	brain_surface_destroy(brain_fe.nsfb);
	netsurf_exit();
	if (fb_font_finalise() == false) {
		NSLOG(netsurf, INFO, "Font finalisation failed.");
	}
	nsoption_finalise(nsoptions, nsoptions_default);
	nslog_finalise();
	return 0;
}
