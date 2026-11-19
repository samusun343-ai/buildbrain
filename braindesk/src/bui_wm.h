/* bui_wm: braindesk window manager.
 *
 * Apps used to paint a picture of a window full-screen and own the
 * display until they exited. This is the real thing instead: the
 * shell runs a tiny compositor. A client app connects over a Unix
 * socket, gets a shared-memory pixel buffer for its content area,
 * draws into it with libbrainui as usual, and commits frames. The
 * shell composites every window over the wallpaper, draws the real
 * chrome (title bar, close/minimize buttons) in the active skin's
 * style, and routes input: drag by the title bar, click to focus,
 * keys go to the focused window.
 *
 * Wire protocol (all messages: header + optional payload):
 *   client -> server  WM_HELLO   {title, req_w, req_h}  (once)
 *   server -> client  WM_GEOM    {w, h} + memfd (SCM_RIGHTS)
 *   client -> server  WM_COMMIT  {}         frame ready
 *   server -> client  WM_EVENT   {bu_event} input for this window
 *   server -> client  WM_CLOSE   {}         please exit
 */
#ifndef BUI_WM_H
#define BUI_WM_H

#include "bui.h"
#include "bui_input.h"
#include <stdint.h>

enum { WM_HELLO = 1, WM_GEOM = 2, WM_COMMIT = 3, WM_EVENT = 4, WM_CLOSE = 5 };

struct wm_hdr { uint32_t type; uint32_t len; };
struct wm_hello { char title[64]; int32_t w, h; };
struct wm_geom { int32_t w, h; };
struct wm_event { int32_t type, code, value, x, y; };

/* ---------- client side (apps) ---------- */

typedef struct bd_wm_client bd_wm_client;

/* Connect to the shell's WM socket. Returns NULL when not running
 * under the shell (caller falls back to the direct display). */
bd_wm_client *bd_wm_client_open(const char *sock_path, const char *title,
				int req_w, int req_h);
/* The shared pixel buffer as a canvas (do not free the canvas). */
bu_canvas *bd_wm_client_canvas(bd_wm_client *cl);
void bd_wm_client_commit(bd_wm_client *cl);
/* Next input event; returns 1 on event, 0 on timeout, -1 when the
 * shell asked the app to close (or the connection died). */
int bd_wm_client_next(bd_wm_client *cl, bu_event *ev, int timeout_ms);
void bd_wm_client_close(bd_wm_client *cl);

/* ---------- server side (shell) ---------- */

/* Start listening. sock_path may be NULL for the default
 * (/run/braindesk/wm.sock, falling back to /tmp). Also publishes
 * the path in the BRAINDESK_WM environment variable so launched
 * children inherit it. */
int bd_wm_server_start(const char *sock_path);
/* Work area new windows are placed in and drags are clamped to
 * (the desktop minus the panel / top bar). The shell pushes the
 * current area every frame, so skin changes apply at once.
 * Default is the full 854x480 panel until the shell pushes one. */
void bd_wm_set_workarea(int x, int y, int w, int h);
const char *bd_wm_server_path(void);
/* Service clients (accept, frames, disconnects). Returns 1 when
 * anything changed and the desktop needs a redraw. */
int bd_wm_poll(void);
int bd_wm_window_count(void);
/* Composite all windows (with chrome) onto the desktop canvas. */
void bd_wm_draw(bu_canvas *c, bu_theme *t);
/* Task buttons in the panel: draw + shared hit geometry. */
void bd_wm_draw_tasks(bu_canvas *c, bu_theme *t, int x0, int x1, int py, int ph);
int bd_wm_task_hit(int x, int y, int x0, int x1, int py, int ph); /* idx or -1 */
/* Route a pointer event; returns 1 when a window consumed it. */
int bd_wm_pointer(int type, int x, int y);
/* Route a key event to the focused window; 1 when consumed. */
int bd_wm_key(int code, int value);
/* True while any window exists and none of the shell's own
 * overlays should take the keys. */
int bd_wm_focused(void);
void bd_wm_raise_task(int idx);

#endif
