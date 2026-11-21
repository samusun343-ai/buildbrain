/*
 * Brainux "brain" frontend for NetSurf: shared frontend state.
 *
 * The browser chrome (tab strip, pill URL bar, menus, dialogs) is
 * drawn with the Brainux toolkit v2 (libbrainui2, UI-STYLE-v2.md);
 * the page itself is rendered by NetSurf through the libnsfb
 * "brain" surface into the very same pixel buffer:
 *
 *  - standalone mode: the surface owns a shadow buffer over the
 *    framebuffer device (or a regular file in tests), the toolkit
 *    paints into the shadow, nsfb_update() presents the union of
 *    page and chrome damage.
 *  - WM mode: the surface runs target=shm over the braindesk2 WM
 *    client's back buffer; the toolkit paints into that buffer and
 *    the client COMMITs the merged damage.  Buffers alternate
 *    strictly, so damage is journaled per buffer (see gui.c).
 *
 * Both buffers are XRGB8888, which is bit-identical to the
 * toolkit's pixel packing, so page and chrome share memory with
 * zero copies.
 */

#ifndef BRAIN_GUI_H
#define BRAIN_GUI_H

#include <stdbool.h>
#include <stdint.h>
#include <libnsfb.h>

#include "tk.h"

struct browser_window;
struct bd_wm_client;
struct gui_window;

/** Design range: at most two tabs on this device
 * (DESIGN-brain-frontend.md).  Enforced in window.c, mirrored by
 * the tab strip arrays in toolbar.c. */
#define BRAIN_MAX_TABS 2

/** One browser window == one tab.  The chrome is shared and is
 * retargeted at the active window; per-window state lives here. */
struct gui_window {
	struct browser_window *bw;
	struct gui_window *next;
	struct gui_window *prev;

	int scrollx;
	int scrolly;

	bool throbbing;
	unsigned throbber_phase;

	char *status; /**< last status text from the core (owned) */
	char *url_text; /**< URL bar text for this tab (owned) */

	/** caret reported by the core, in window coordinates;
	 * caret_h == 0 means no caret */
	int caret_x;
	int caret_y;
	int caret_h;

	bool destroying;
};

/** Display mode of the frontend. */
enum brain_mode {
	BRAIN_MODE_WM = 0, /**< braindesk2 WM client (target=shm) */
	BRAIN_MODE_FB = 1, /**< standalone framebuffer */
};

/** Damage journal slot: what a given back buffer still misses.
 * Rects are window coordinates, capped like the toolkit canvas. */
#define BRAIN_JOURNAL_RECTS 24
struct brain_journal {
	void *ptr; /**< buffer pointer this slot tracks (NULL = fb) */
	nsfb_bbox_t rects[BRAIN_JOURNAL_RECTS];
	int nrects;
	bool valid;
};

/** Whole-frontend state (single instance, gui.c). */
struct brain_frontend {
	enum brain_mode mode;
	struct bd_wm_client *wm; /**< WM mode only */
	nsfb_t *nsfb; /**< page surface, both modes */

	/** Display mode from brainreg ui/mode + ui/dark, read once
	 * at startup through bd_theme_from_reg() (thememode.h, the
	 * canonical contract shared with the shell and the apps).
	 * winui selects the WinUI/Fluent chrome form in toolbar.c;
	 * the theme itself (tk_theme_default()) carries the colours
	 * and shapes for both modes.  No live switching: a mode
	 * change takes effect at the next start, as in the shell. */
	bool winui;
	bool dark;

	tk_root *root;
	int width;
	int height;
	bool quit;
	/** set once the first tab exists; closing the last tab quits
	 * only after this (tab-cap eviction may transiently empty
	 * the list while a replacement is being created) */
	bool session_started;

	/* chrome widgets (toolbar.c) */
	tk_widget *top;
	tk_widget *tabstrip;
	tk_widget *tab_row; /**< rebuilt on tab changes */
	tk_widget *toolbar;
	tk_widget *back_btn;
	tk_widget *fwd_btn;
	tk_widget *reload_btn;
	tk_widget *url_field;
	tk_widget *newtab_btn; /**< Android form only (NULL in WinUI) */
	tk_widget *fav_btn; /**< WinUI form only (NULL in Android) */
	tk_widget *home_btn; /**< WinUI form only (NULL in Android) */
	tk_widget *menu_btn;
	tk_widget *progress;
	tk_widget *page_holder;
	tk_widget *page_widget;
	tk_widget *status_bubble;
	tk_widget *osk; /**< standalone mode only */

	/* tabs (window.c) */
	struct gui_window *windows;
	struct gui_window *active;
	int n_windows;

	/* damage: rects invalidated since the last present, in
	 * window coordinates; merged into the journal at present */
	nsfb_bbox_t pending[BRAIN_JOURNAL_RECTS];
	int npending;
	struct brain_journal journal[2];
	void *presented_ptr; /**< buffer presented last (WM mode) */

	/* standalone-mode OSK visibility follows URL field focus */
	bool osk_shown;

	/** URL field edit mode (chrome redesign, report section 9):
	 * while true the field holds the full URL for editing; while
	 * false it holds the shortened steady-state display form */
	bool url_editing;

	/* page pointer state (pageview.c) */
	struct {
		int state; /**< 0 idle, 1 pressed, 2 core drag, 3 pan */
		int button;
		int start_cx; /**< press point, content coordinates */
		int start_cy;
		int start_sx; /**< scroll offsets at press (pan) */
		int start_sy;
		uint64_t last_click_ms;
		int click_count;
	} drag;
};

extern struct brain_frontend brain_fe;

/* ---- cross-file interface ---- */

/* gui.c */
void brain_invalidate(nsfb_bbox_t rect); /**< window coordinates */
void brain_invalidate_all(void);
void brain_request_present(void);
uint64_t brain_now_ms(void);
void brain_quit(void);

/* window.c */
extern struct gui_window_table brain_window_table;
struct gui_window *brain_new_window(struct browser_window *bw);
void brain_destroy_window(struct gui_window *gw);
void brain_set_active_window(struct gui_window *gw);
void brain_close_active_window(void);
void brain_open_tab(const char *url, bool background);
void brain_navigate(const char *url_text); /**< active tab */
void brain_session_save(void);
void brain_session_restore(void);
int brain_window_index(const struct gui_window *gw);

/* toolbar.c */
void brain_chrome_build(void);
void brain_chrome_sync(void); /**< retarget chrome at active tab */
void brain_chrome_layout(void); /**< after a resize */
void brain_set_status(struct gui_window *gw, const char *text);
void brain_set_throbber(struct gui_window *gw, bool active);
bool brain_overlay_open(void); /**< menu or dialog currently up */
void brain_overlays_invalidate(void); /**< repaint open overlays on top of page damage */
/** URL field edit mode (report section 9.3-6): begin restores the
 * full URL and selects it; end returns to the display form. */
void brain_url_begin_edit(void);
void brain_url_end_edit(void);
/** true while the transient page-area overlays (progress line,
 * status bubble) are up and must be repainted over page damage */
bool brain_transient_overlay_visible(void);

/* pageview.c */
void brain_page_geometry(nsfb_bbox_t *out); /**< window coords */
void brain_page_redraw(struct gui_window *gw, const nsfb_bbox_t *win_clip);
void brain_page_invalidate(struct gui_window *gw, nsfb_bbox_t content_clip);
void brain_page_full_redraw(struct gui_window *gw);
void brain_page_set_scroll(struct gui_window *gw, int sx, int sy);
void brain_page_overlay_paint(tk_canvas *canvas);
bool brain_page_event(const tk_event *ev);

#endif /* BRAIN_GUI_H */
