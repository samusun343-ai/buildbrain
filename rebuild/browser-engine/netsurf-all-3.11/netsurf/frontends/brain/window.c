/*
 * Brainux "brain" frontend: gui_window table and the tab model.
 *
 * One gui_window is one tab.  The chrome (toolbar.c) is shared and
 * retargeted at the active tab; switching tabs always repaints the
 * whole page area, so invalidations for background tabs can be
 * dropped safely.  The tab count is capped at BRAIN_MAX_TABS: the
 * design range for this device is 1-2 tabs (DESIGN-brain-frontend
 * .md); when the core asks for another window beyond the cap, the
 * least recently used background tab is closed to make room.
 *
 * Session state (open tabs + active index) is persisted to
 * $HOME/.netsurf/brain-session.txt with an atomic rewrite, so a
 * sudden reset can at worst lose the last change, never corrupt
 * the file.
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "netsurf/browser_window.h"
#include "netsurf/window.h"
#include "desktop/browser_history.h"
#include "desktop/search.h"
#include "desktop/searchweb.h"

#include "brain/gui.h"
#include "brain/schedule.h"

/* ------------------------------------------------------------------ */
/* tab list management                                                */
/* ------------------------------------------------------------------ */

static void window_list_add(struct gui_window *gw)
{
	gw->next = brain_fe.windows;
	gw->prev = NULL;
	if (brain_fe.windows != NULL) {
		brain_fe.windows->prev = gw;
	}
	brain_fe.windows = gw;
	brain_fe.n_windows++;
}

static void window_list_remove(struct gui_window *gw)
{
	if (gw->prev != NULL) {
		gw->prev->next = gw->next;
	} else {
		brain_fe.windows = gw->next;
	}
	if (gw->next != NULL) {
		gw->next->prev = gw->prev;
	}
	gw->next = gw->prev = NULL;
	brain_fe.n_windows--;
}

int brain_window_index(const struct gui_window *gw)
{
	const struct gui_window *w;
	int i = 0;
	/* list is newest-first; the tab strip shows oldest first */
	for (w = brain_fe.windows; w != NULL; w = w->next) {
		i++;
		if (w == gw) {
			return brain_fe.n_windows - i;
		}
	}
	return -1;
}

/** Move gw to the head of the list (most recently used). */
static void window_list_touch(struct gui_window *gw)
{
	if (brain_fe.windows == gw) {
		return;
	}
	window_list_remove(gw); /* decrements the count */
	window_list_add(gw); /* and this restores it */
}

/* ------------------------------------------------------------------ */
/* throbber (drives the toolbar progress indicator)                   */
/* ------------------------------------------------------------------ */

static void throbber_advance(void *pw)
{
	struct gui_window *gw = pw;

	if (gw->destroying || !gw->throbbing) {
		return;
	}
	gw->throbber_phase++;
	if (gw == brain_fe.active) {
		/* indeterminate progress: the core has no percentage
		 * API for frontends, so the bar animates while the
		 * throbber runs (see REPORT-brain-frontend.md) */
		brain_chrome_sync();
	}
	brain_schedule(120, throbber_advance, gw);
}

static void window_start_throbber(struct gui_window *gw)
{
	if (gw->throbbing) {
		return;
	}
	gw->throbbing = true;
	gw->throbber_phase = 0;
	brain_schedule(120, throbber_advance, gw);
	if (gw == brain_fe.active) {
		brain_chrome_sync();
	}
}

static void window_stop_throbber(struct gui_window *gw)
{
	gw->throbbing = false;
	if (gw == brain_fe.active) {
		brain_chrome_sync();
		brain_session_save();
	}
}

/* ------------------------------------------------------------------ */
/* gui_window table                                                   */
/* ------------------------------------------------------------------ */

static struct gui_window *
window_create(struct browser_window *bw,
	      struct gui_window *existing,
	      gui_window_create_flags flags)
{
	struct gui_window *gw;

	/* enforce the tab cap: close the least recently used
	 * background tab (tail of the list) to make room */
	while (brain_fe.n_windows >= BRAIN_MAX_TABS) {
		struct gui_window *tail = brain_fe.windows;
		struct gui_window *victim = NULL;
		while (tail != NULL) {
			if (tail != brain_fe.active) {
				victim = tail;
			}
			tail = tail->next;
		}
		if (victim == NULL || victim->bw == NULL) {
			break;
		}
		NSLOG(netsurf, INFO,
		      "tab cap reached: closing background tab");
		browser_window_destroy(victim->bw);
	}

	gw = calloc(1, sizeof(struct gui_window));
	if (gw == NULL) {
		return NULL;
	}
	gw->bw = bw;
	gw->url_text = strdup("");
	window_list_add(gw);

	if (brain_fe.active == NULL ||
	    (flags & GW_CREATE_FOREGROUND) ||
	    !(flags & GW_CREATE_TAB)) {
		brain_set_active_window(gw);
	} else {
		brain_chrome_sync();
	}
	brain_session_save();
	return gw;
}

static void window_destroy(struct gui_window *gw)
{
	bool was_active = (gw == brain_fe.active);

	gw->destroying = true;
	window_list_remove(gw);
	if (was_active) {
		brain_fe.active = NULL;
		if (brain_fe.windows != NULL) {
			brain_set_active_window(brain_fe.windows);
		} else {
			brain_chrome_sync();
		}
	} else {
		brain_chrome_sync();
	}
	free(gw->status);
	free(gw->url_text);
	free(gw);
	brain_session_save();

	if (brain_fe.n_windows == 0 && brain_fe.session_started) {
		brain_quit();
	}
}

static nserror
window_invalidate(struct gui_window *gw, const struct rect *rect)
{
	nsfb_bbox_t clip;

	if (gw != brain_fe.active || gw->destroying) {
		/* background tabs repaint in full on activation */
		return NSERROR_OK;
	}
	if (rect != NULL) {
		clip.x0 = rect->x0;
		clip.y0 = rect->y0;
		clip.x1 = rect->x1;
		clip.y1 = rect->y1;
	} else {
		clip.x0 = 0;
		clip.y0 = 0;
		clip.x1 = 0x7fff;
		clip.y1 = 0x7fff;
	}
	brain_page_invalidate(gw, clip);
	return NSERROR_OK;
}

static bool
window_get_scroll(struct gui_window *gw, int *sx, int *sy)
{
	*sx = gw->scrollx;
	*sy = gw->scrolly;
	return true;
}

static nserror
window_set_scroll(struct gui_window *gw, const struct rect *rect)
{
	brain_page_set_scroll(gw, rect->x0, rect->y0);
	return NSERROR_OK;
}

static nserror
window_get_dimensions(struct gui_window *gw, int *width, int *height)
{
	nsfb_bbox_t page;
	brain_page_geometry(&page);
	*width = page.x1 - page.x0;
	*height = page.y1 - page.y0;
	return NSERROR_OK;
}

static void window_set_title(struct gui_window *gw, const char *title)
{
	(void)title;
	if (gw == brain_fe.active) {
		brain_chrome_sync(); /* tab strip shows titles */
	} else {
		brain_chrome_sync();
	}
}

static nserror window_set_url(struct gui_window *gw, struct nsurl *url)
{
	free(gw->url_text);
	gw->url_text = strdup(nsurl_access(url));
	if (gw == brain_fe.active) {
		brain_chrome_sync();
	}
	return NSERROR_OK;
}

static void window_set_status(struct gui_window *gw, const char *text)
{
	free(gw->status);
	gw->status = strdup(text != NULL ? text : "");
	if (gw == brain_fe.active) {
		brain_set_status(gw, gw->status);
	}
}

static void window_set_pointer(struct gui_window *gw,
		enum gui_pointer_shape shape)
{
	/* touch device: no pointer glyph to change */
	(void)gw;
	(void)shape;
}

static void
window_place_caret(struct gui_window *gw, int x, int y, int height,
		const struct rect *clip)
{
	nsfb_bbox_t page;
	nsfb_bbox_t r;

	(void)clip;
	brain_page_geometry(&page);
	/* core passes content coordinates; store window coords */
	gw->caret_x = page.x0 + x - gw->scrollx;
	gw->caret_y = page.y0 + y - gw->scrolly;
	gw->caret_h = height;
	if (gw == brain_fe.active) {
		r.x0 = gw->caret_x - 1;
		r.y0 = gw->caret_y - 1;
		r.x1 = gw->caret_x + 2;
		r.y1 = gw->caret_y + height + 1;
		brain_invalidate(r);
	}
}

static void window_remove_caret(struct gui_window *gw)
{
	nsfb_bbox_t r;

	if (gw->caret_h == 0) {
		return;
	}
	if (gw == brain_fe.active) {
		r.x0 = gw->caret_x - 1;
		r.y0 = gw->caret_y - 1;
		r.x1 = gw->caret_x + 2;
		r.y1 = gw->caret_y + gw->caret_h + 1;
		brain_invalidate(r);
	}
	gw->caret_h = 0;
}

static nserror
window_event(struct gui_window *gw, enum gui_window_event event)
{
	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		if (gw == brain_fe.active) {
			nsfb_bbox_t page;
			brain_page_geometry(&page);
			brain_invalidate(page); /* scrollbar thumb moves */
		}
		break;

	case GW_EVENT_REMOVE_CARET:
		window_remove_caret(gw);
		break;

	case GW_EVENT_START_THROBBER:
		window_start_throbber(gw);
		break;

	case GW_EVENT_STOP_THROBBER:
		window_stop_throbber(gw);
		break;

	default:
		break;
	}
	return NSERROR_OK;
}

struct gui_window_table brain_window_table = {
	.create = window_create,
	.destroy = window_destroy,
	.invalidate = window_invalidate,
	.get_scroll = window_get_scroll,
	.set_scroll = window_set_scroll,
	.get_dimensions = window_get_dimensions,
	.event = window_event,

	.set_title = window_set_title,
	.set_url = window_set_url,
	.set_status = window_set_status,
	.set_pointer = window_set_pointer,
	.place_caret = window_place_caret,
};

/* ------------------------------------------------------------------ */
/* tab operations used by the chrome                                  */
/* ------------------------------------------------------------------ */

void brain_set_active_window(struct gui_window *gw)
{
	brain_fe.active = gw;
	if (gw != NULL) {
		window_list_touch(gw);
	}
	brain_chrome_sync();
	if (gw != NULL) {
		brain_page_full_redraw(gw);
	}
	brain_session_save();
}

void brain_close_active_window(void)
{
	if (brain_fe.active != NULL && brain_fe.active->bw != NULL) {
		browser_window_destroy(brain_fe.active->bw);
	}
}

void brain_open_tab(const char *url, bool background)
{
	nsurl *nsurl_obj = NULL;
	struct browser_window *bw = NULL;
	enum browser_window_create_flags flags =
		BW_CREATE_HISTORY | BW_CREATE_TAB;
	nserror err;

	err = nsurl_create(url, &nsurl_obj);
	if (err != NSERROR_OK) {
		return;
	}
	if (!background) {
		flags |= BW_CREATE_FOREGROUND;
	}
	browser_window_create(flags, nsurl_obj, NULL, NULL, &bw);
	nsurl_unref(nsurl_obj);
}

void brain_navigate(const char *url_text)
{
	nsurl *url = NULL;

	if (brain_fe.active == NULL || brain_fe.active->bw == NULL ||
	    url_text == NULL || url_text[0] == '\0') {
		return;
	}
	/* resolve the typed text the Chrome way: a URL if it parses
	 * as one, otherwise a search through the configured
	 * provider (search_url_bar is enabled in brainreg.c) */
	if (search_web_omni(url_text, SEARCH_WEB_OMNI_NONE,
			&url) != NSERROR_OK || url == NULL) {
		if (nsurl_create(url_text, &url) != NSERROR_OK) {
			return;
		}
	}
	browser_window_navigate(brain_fe.active->bw, url, NULL,
			BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
	nsurl_unref(url);
}

/* ------------------------------------------------------------------ */
/* session persistence                                                */
/* ------------------------------------------------------------------ */

static char *session_path(void)
{
	const char *home = getenv("HOME");
	char *path;
	size_t len;

	if (home == NULL) {
		home = "/tmp";
	}
	len = strlen(home) + strlen("/.netsurf/brain-session.txt") + 1;
	path = malloc(len);
	if (path != NULL) {
		snprintf(path, len, "%s/.netsurf/brain-session.txt", home);
	}
	return path;
}

void brain_session_save(void)
{
	char *path = session_path();
	char *tmp;
	FILE *f;
	struct gui_window *gw;
	size_t len;

	if (path == NULL) {
		return;
	}
	len = strlen(path) + 5;
	tmp = malloc(len);
	if (tmp == NULL) {
		free(path);
		return;
	}
	snprintf(tmp, len, "%s.tmp", path);

	f = fopen(tmp, "w");
	if (f == NULL) {
		free(tmp);
		free(path);
		return;
	}
	/* oldest tab first */
	{
		int n = brain_fe.n_windows;
		int i;
		struct gui_window **order = calloc(n > 0 ? n : 1,
				sizeof(*order));
		if (order != NULL) {
			i = 0;
			for (gw = brain_fe.windows; gw != NULL; gw = gw->next) {
				order[n - 1 - i++] = gw;
			}
			for (i = 0; i < n; i++) {
				nsurl *u = browser_window_access_url(
						order[i]->bw);
				fprintf(f, "tab %s\n",
					u != NULL ? nsurl_access(u) : "about:blank");
			}
			free(order);
		}
	}
	fprintf(f, "active %d\n", brain_window_index(brain_fe.active));
	fclose(f);
	/* atomic replace: a sudden reset leaves old or new, never torn */
	rename(tmp, path);
	free(tmp);
	free(path);
}

void brain_session_restore(void)
{
	char *path = session_path();
	FILE *f;
	char line[2048];
	int restored = 0;
	int active_idx = 0;

	if (path == NULL) {
		return;
	}
	f = fopen(path, "r");
	free(path);
	if (f == NULL) {
		return;
	}
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strncmp(line, "tab ", 4) == 0) {
			char *url = line + 4;
			char *nl = strchr(url, '\n');
			if (nl != NULL) {
				*nl = '\0';
			}
			if (url[0] != '\0' && restored < BRAIN_MAX_TABS) {
				brain_open_tab(url, restored > 0);
				restored++;
			}
		} else if (strncmp(line, "active ", 7) == 0) {
			active_idx = atoi(line + 7);
		}
	}
	fclose(f);

	if (restored > 0) {
		/* activate the recorded tab (list is newest-first) */
		struct gui_window *gw = brain_fe.windows;
		int target = restored - 1 - active_idx;
		int i = 0;
		while (gw != NULL && i < target) {
			gw = gw->next;
			i++;
		}
		if (gw != NULL) {
			brain_set_active_window(gw);
		}
	}
}
