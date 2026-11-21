/* wm - window manager protocol, server and client for shell v2 (M2).
 *
 * Transport (inherited from the old bui_wm, whose transport was the
 * one part the audit kept): a Unix stream socket for messages plus
 * memfd shared memory for pixel buffers. What M2 changes versus the
 * old protocol, answering the audited defects (max 8 windows,
 * fixed-size buffers, maximize = scale-blit):
 *
 *   - No fixed window limit; the practical limit is shm memory.
 *   - Resize is real: the server allocates a new buffer pair and
 *     sends RESIZE; the client redraws at the new size and commits.
 *     Buffers are never scaled into a different-sized frame.
 *   - Double buffering with explicit ownership: the client may only
 *     draw into a buffer the server has RELEASEd (or never
 *     committed); the server composites the last committed buffer
 *     by reading the shm directly - no staging copy anywhere.
 *   - COMMIT must carry a damage rectangle list (content
 *     coordinates). A COMMIT with zero rectangles is a protocol
 *     error: it is dropped and counted (bd_wm_server_proto_errors).
 *   - Chrome is drawn by the server: 32px title bar, close and
 *     minimize boxes, 24px resize handle at the frame's
 *     bottom-right corner (single-finger sizing, see
 *     DESIGN-shell-v2.md section 5).
 *
 * Wire format: every message is bd_wm_hdr followed by `len` payload
 * bytes. GEOM and RESIZE additionally carry two memfds via
 * SCM_RIGHTS. All integers are native-endian (both ends are the
 * same machine by construction).
 *
 * Buffer generations: GEOM establishes generation 0. Each RESIZE
 * bumps the generation; the server keeps compositing the old
 * generation until the first COMMIT of the new one arrives, then
 * releases the old buffers. COMMIT/RELEASE name (gen, buf).
 */
#ifndef BD_WM_H
#define BD_WM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tk.h"

#define BD_WM_MAGIC 0x4244574du /* "BDWM" */

enum bd_wm_msg {
    BD_WM_HELLO   = 1, /* c->s bd_wm_hello                       */
    BD_WM_GEOM    = 2, /* s->c bd_wm_geom + 2 memfds             */
    BD_WM_COMMIT  = 3, /* c->s bd_wm_commit + tk_rect payload    */
    BD_WM_RELEASE = 4, /* s->c bd_wm_bufref                      */
    BD_WM_EVENT   = 5, /* s->c bd_wm_event                       */
    BD_WM_RESIZE  = 6, /* s->c bd_wm_geom + 2 memfds (new gen)   */
    BD_WM_CLOSE   = 7, /* s->c (empty) please exit               */
    BD_WM_BYE     = 8, /* c->s (empty) client is exiting         */
    BD_WM_TITLE   = 9, /* c->s bd_wm_title                       */
    BD_WM_FOCUS   = 10, /* s->c bd_wm_focus                      */
    BD_WM_IM      = 11, /* c->s bd_wm_im: text-input state       */
    BD_WM_COMP    = 12 /* s->c bd_wm_comp: IME composition       */
};

typedef struct { uint32_t magic, type, len; } bd_wm_hdr;
typedef struct { char title[64]; int32_t req_w, req_h; } bd_wm_hello;
typedef struct { uint32_t win_id, gen; int32_t w, h; } bd_wm_geom;
typedef struct { uint32_t gen; int32_t buf, nrects; } bd_wm_commit;
typedef struct { uint32_t gen; int32_t buf; } bd_wm_bufref;
typedef struct { int32_t focused; } bd_wm_focus;

/* Pointer/key event delivered to a client. Coordinates are in
 * content space. `type` is one of BD_WM_EV_*. For key events x/y
 * are unused, key is a tk_key, text carries committed UTF-8 for
 * TK_KEY_NONE printable keys (may be empty). */
enum bd_wm_ev {
    BD_WM_EV_POINTER_DOWN = 1, BD_WM_EV_POINTER_UP, BD_WM_EV_POINTER_MOVE,
    BD_WM_EV_KEY_DOWN, BD_WM_EV_KEY_UP, BD_WM_EV_FOCUS_IN, BD_WM_EV_FOCUS_OUT
};
typedef struct {
    int32_t type, x, y, key;
    uint32_t mods;
    char text[32];
} bd_wm_event;
typedef struct { char title[64]; } bd_wm_title;

/* Text-input (input method) state reported by a client: active
 * while one of its text editors has focus. x/y/w/h is the focused
 * editor's rect in content coordinates so the server can tell
 * whether an on-screen keyboard would cover it; w <= 0 means the
 * client did not supply a rect. Sent on change only (the client
 * library suppresses duplicates). */
typedef struct { int32_t active; int32_t x, y, w, h; } bd_wm_im;

/* IME composition (preedit) for the focused window, produced by
 * the shell's on-screen keyboard. text is the whole composition
 * as a single segment; an empty string clears it. Committed text
 * keeps travelling as a BD_WM_EVENT key event with text set. */
typedef struct { char text[192]; } bd_wm_comp;

/* Default socket path: $BD_WM_SOCK if set, else
 * $XDG_RUNTIME_DIR/bdwm.sock, else /tmp/bdwm-<uid>.sock. */
const char *bd_wm_default_path(void);

/* ---------------- server ---------------- */

typedef struct bd_wm_server bd_wm_server;

typedef enum {
    BD_WIN_NORMAL = 0, BD_WIN_MAXIMIZED, BD_WIN_MINIMIZED, BD_WIN_FULLSCREEN
} bd_win_state;

typedef struct {
    uint32_t id;
    char title[64];
    tk_rect frame;    /* including chrome */
    tk_rect content;  /* content area inside the frame */
    bd_win_state state;
    bool focused;
} bd_wm_win_info;

bd_wm_server *bd_wm_server_start(const char *sock_path /* NULL = default */);
void bd_wm_server_free(bd_wm_server *srv);
const char *bd_wm_server_path(const bd_wm_server *srv);

/* Area new/maximized windows use, and the screen rect fullscreen
 * uses. Defaults: workarea 0,0,854,448; screen 0,0,854,480. */
void bd_wm_server_set_workarea(bd_wm_server *srv, tk_rect r);
void bd_wm_server_set_screen(bd_wm_server *srv, tk_rect r);

/* Accept connections and read messages (non-blocking when
 * timeout_ms == 0; otherwise waits up to timeout_ms for socket
 * activity). Returns the number of state changes observed. */
int bd_wm_server_poll(bd_wm_server *srv, int timeout_ms);

size_t bd_wm_server_count(const bd_wm_server *srv);
bool bd_wm_server_info(const bd_wm_server *srv, size_t idx, bd_wm_win_info *out);
bool bd_wm_server_info_id(const bd_wm_server *srv, uint32_t id, bd_wm_win_info *out);
uint32_t bd_wm_server_focused(const bd_wm_server *srv); /* win id or 0 */
unsigned bd_wm_server_proto_errors(const bd_wm_server *srv);

/* Scale the window's last committed content into px (w x h,
 * nearest neighbour). This is the one deliberate copy out of the
 * shared buffers: it exists for the app switcher's cards and is
 * taken only when the switcher opens, never per frame. False
 * when the window is unknown or has committed nothing yet. */
bool bd_wm_server_thumbnail(bd_wm_server *srv, uint32_t id,
                            uint32_t *px, int w, int h);

bool bd_wm_server_focus(bd_wm_server *srv, uint32_t id);
bool bd_wm_server_move(bd_wm_server *srv, uint32_t id, int x, int y);

/* --- on-screen keyboard support --- */
/* Reserve a strip at the bottom of the screen (the OSK's height,
 * 0 = none). Maximized windows use the workarea clipped above the
 * strip and fullscreen windows use the screen above the strip;
 * windows already in those states are re-laid out (RESIZE) when
 * the inset changes, and restored exactly when it returns to 0,
 * because their geometry is a pure function of state + areas.
 * Normal windows are not moved by the inset; the embedder moves
 * the focused one itself when its field would be covered. */
void bd_wm_server_set_bottom_inset(bd_wm_server *srv, int px);
int bd_wm_server_bottom_inset(const bd_wm_server *srv);
/* True when the focused window reports an active text editor
 * (BD_WM_IM). The embedder shows the OSK exactly then. */
bool bd_wm_server_im_active(const bd_wm_server *srv);
/* Counts IM state changes (activation edges and field-rect
 * changes, any window). A sampling embedder uses it as the
 * remote input target's identity: an inactive->active cycle
 * between two samples is otherwise invisible, and a latch keyed
 * on "active" alone could never tell a re-entered field from
 * the same field throughout. */
uint32_t bd_wm_server_im_serial(const bd_wm_server *srv);
/* The focused window's editor rect in screen coordinates.
 * False when IM is inactive or the client supplied no rect. */
bool bd_wm_server_im_field(const bd_wm_server *srv, tk_rect *out);
/* Send an IME composition update to the focused window
 * (BD_WM_COMP). NULL or "" clears the composition. False when no
 * window is focused. */
bool bd_wm_server_send_composition(bd_wm_server *srv, const char *text);
/* Request a new content size: allocates a new buffer generation
 * and sends RESIZE. The frame follows once the client commits at
 * the new size. */
bool bd_wm_server_resize(bd_wm_server *srv, uint32_t id, int content_w, int content_h);
bool bd_wm_server_set_state(bd_wm_server *srv, uint32_t id, bd_win_state st);
bool bd_wm_server_close(bd_wm_server *srv, uint32_t id); /* sends CLOSE */

/* Route a pointer event in screen coordinates: chrome (drag,
 * resize handle, close/minimize boxes) is handled by the server;
 * content events are forwarded to the window's client and raise
 * focus. Returns true when a window consumed the event. */
bool bd_wm_server_pointer(bd_wm_server *srv, int ev_type /* BD_WM_EV_* */,
                          int x, int y);
/* Route a key event to the focused window. text may be NULL. */
bool bd_wm_server_key(bd_wm_server *srv, int ev_type /* BD_WM_EV_* */,
                      int key, uint32_t mods, const char *text);

/* Screen-space damage accumulated since the last composite (peek;
 * does not clear). Rects are frame rects of what needs repaint. */
size_t bd_wm_server_damage(const bd_wm_server *srv, tk_rect *out, size_t max);
/* Add a screen rect to the damage accumulator (M3): the embedder
 * asks for windows to be redrawn over an area it repainted itself
 * (e.g. shell UI changes under/near windows). Merges like any
 * other damage; consumed by the next bd_wm_composite(). */
void bd_wm_server_invalidate(bd_wm_server *srv, tk_rect r);
/* Composite all visible windows (chrome + client content read
 * directly from shm) onto dst, clipped to the accumulated damage,
 * then clear the damage accumulator. With no damage this draws
 * nothing. Callers repaint their own background for damaged areas
 * first (or use bd_wm_composite_full once at start). */
void bd_wm_composite(bd_wm_server *srv, tk_canvas *dst, const tk_theme *t);
void bd_wm_composite_full(bd_wm_server *srv, tk_canvas *dst, const tk_theme *t);

/* ---------------- client ---------------- */

typedef struct bd_wm_client bd_wm_client;

/* Event surfaced to client applications. RESIZED means the buffer
 * generation changed: bd_wm_client_canvas() now wraps the new
 * back buffer at the new size and the app must redraw fully.
 * CLOSE means the server asked the app to exit (or the connection
 * died; see bd_wm_client_dead). */
enum bd_wm_cev {
    BD_WM_CEV_POINTER_DOWN = 1, BD_WM_CEV_POINTER_UP, BD_WM_CEV_POINTER_MOVE,
    BD_WM_CEV_KEY_DOWN, BD_WM_CEV_KEY_UP,
    BD_WM_CEV_FOCUS_IN, BD_WM_CEV_FOCUS_OUT,
    BD_WM_CEV_RESIZED, BD_WM_CEV_CLOSE,
    BD_WM_CEV_COMPOSITION /* comp: preedit text, "" = cleared */
};
typedef struct {
    int type;
    int x, y;          /* pointer: content coordinates */
    int key;           /* tk_key for key events */
    uint32_t mods;
    char text[32];     /* committed UTF-8 for key events */
    int w, h;          /* RESIZED: new content size */
    char comp[192];    /* COMPOSITION: preedit text */
} bd_wm_client_event;

/* Connect and perform HELLO/GEOM. sock_path NULL = default.
 * Returns NULL when no server is listening (caller may fall back
 * to running standalone). */
bd_wm_client *bd_wm_client_open(const char *sock_path, const char *title,
                                int req_w, int req_h);
void bd_wm_client_close(bd_wm_client *cl); /* sends BYE */
bool bd_wm_client_dead(const bd_wm_client *cl);
uint32_t bd_wm_client_id(const bd_wm_client *cl);
int bd_wm_client_width(const bd_wm_client *cl);
int bd_wm_client_height(const bd_wm_client *cl);

/* The back buffer to draw into, as a toolkit canvas wrapping the
 * shm pixels directly (zero copy). NULL when both buffers are with
 * the server (wait for events; a RELEASE is delivered internally
 * and surfaces as poll() returning 0 with a buffer available).
 * The canvas is owned by the client object and invalidated by
 * RESIZE/COMMIT of the other buffer - fetch it fresh each frame. */
tk_canvas *bd_wm_client_canvas(bd_wm_client *cl);
/* Raw pixel access to the same back buffer (for clients written
 * against this header only). stride is in pixels (= width). */
uint32_t *bd_wm_client_pixels(bd_wm_client *cl, int *stride);

/* Commit the back buffer with its damage list (content coords).
 * nrects must be >= 1 - committing without damage is a protocol
 * error and returns -1 without sending anything. On success the
 * buffer changes ownership until RELEASE. */
int bd_wm_client_commit(bd_wm_client *cl, const tk_rect *damage, int nrects);

/* Wait for the next application event. Internal messages (RELEASE,
 * FOCUS bookkeeping) are consumed transparently; RESIZED and CLOSE
 * are surfaced. Returns 1 = event, 0 = timeout (internal traffic
 * may have arrived), -1 = connection dead. */
int bd_wm_client_poll(bd_wm_client *cl, bd_wm_client_event *ev, int timeout_ms);

void bd_wm_client_set_title(bd_wm_client *cl, const char *title);
bool bd_wm_client_focused(const bd_wm_client *cl);

/* Report this client's text-input state (BD_WM_IM). field is the
 * focused editor's rect in content coordinates, or NULL when
 * inactive/unknown. Returns 0 on success. */
int bd_wm_client_im_notify(bd_wm_client *cl, bool active,
                           const tk_rect *field);
/* Derive the IM state from a toolkit root: active while the
 * focused widget is a TextField or TextView, with the widget's
 * arranged rect as the field rect. Sends BD_WM_IM only when the
 * state changed since the last call. Call it after event
 * dispatch and after layout. Returns true when a report was
 * sent. Toolkit clients call this instead of im_notify so the
 * focus -> keyboard behaviour is identical across apps. */
bool bd_wm_client_im_sync(bd_wm_client *cl, tk_root *root);

#endif /* BD_WM_H */
