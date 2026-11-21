/* session - the braindesk2 session runtime (M3).
 *
 * Until M2 the pieces existed separately: the shell UI (shellui),
 * the WM server (wm, driven by tests only), the assist overlay,
 * the hardware backends. This module is the main loop that runs
 * them as one process - the actual braindesk2:
 *
 *   input (evdev) -> route: shell actions, then WM windows
 *   (they float above the desktop), then the shell UI
 *   WM socket -> accept clients, composite their shm buffers
 *   notification socket -> app posts/cancels applied to the
 *   shell's notification model (notify.h)
 *   timers -> clock (on minute change), backend poll (1 s:
 *   battery/volume/brightness + Wi-Fi status), catalog refresh
 *   (5 s: mtime-invalidated, changed files only)
 *   frame -> damage-driven: repaint only damaged areas, draw
 *   windows over exactly those areas, flush only the damage to
 *   the framebuffer (tk_fb_flush_damage). A quiet desktop
 *   transfers ZERO bytes per frame.
 *
 * Ownership: the session owns the shell, the WM server and the
 * bd-alarmd helper process (spawned at creation, terminated at
 * free; see bd_session_config.alarmd_path). The
 * catalog, pin/recent store, framebuffer, evdev devices, backend
 * and Wi-Fi handle are borrowed (the embedder creates and frees
 * them) - tests wire a memory framebuffer, a virtual evdev and
 * mock backends; braindesk2 wires the real ones.
 *
 * The keyboard configuration from the shell's quick settings is
 * applied to the input backend at creation (the embedder's
 * tk_evdev); the assist overlay's page keys are routed to the
 * focused WM window.
 */
#ifndef BD_SESSION_H
#define BD_SESSION_H

#include <stddef.h>
#include <sys/types.h>
#include "shellui.h"
#include "wm.h"
#include "backend.h"
#include "wifi.h"
#include "mascot.h"
#include "notify.h"
#include "tk.h"

typedef struct bd_session bd_session;

typedef struct {
    int width, height;        /* screen size, e.g. 800x480 */
    de_catalog *catalog;      /* borrowed */
    ln_store *store;          /* borrowed */
    ln_exec_fn exec_fn;       /* app launching; may be NULL */
    void *exec_userdata;
    const char *wm_path;      /* WM socket path; NULL = default */
    const char *notify_path;  /* notification socket path; NULL =
                                 default (bd_nt_default_path()).
                                 When the server cannot be started
                                 the session runs without it -
                                 notifications are not critical to
                                 the session, unlike the WM. */
    const char *alarmd_path;  /* bd-alarmd helper daemon (the
                                 resident alarm/timer evaluator,
                                 apps/alarmd). The session spawns
                                 it at creation and terminates it
                                 at bd_session_free(); it inherits
                                 the session's environment (HOME,
                                 XDG_RUNTIME_DIR, BRAINUX_CLOCK_DIR),
                                 which is everything the daemon
                                 resolves its state from. NULL =
                                 default /usr/local/bin/bd-alarmd;
                                 "" disables. A missing or
                                 non-executable helper is NOT fatal:
                                 the session runs without it (alarms
                                 then only evaluate while bd-clock
                                 itself is open). A helper that exits
                                 on its own is reaped and NOT
                                 restarted (bd_session_alarmd_pid()
                                 reports 0 from then on). */
    tk_fb *fb;                /* borrowed; NULL = canvas only */
    tk_evdev *input;          /* borrowed real evdev; may be NULL */
    tk_evdev *input_virtual;  /* borrowed virtual evdev (tests) */
    bd_backend *backend;      /* borrowed; may be NULL */
    bd_wifi *wifi;            /* borrowed; may be NULL */
} bd_session_config;

bd_session *bd_session_new(const bd_session_config *cfg);
void bd_session_free(bd_session *s);

bd_shell *bd_session_shell(bd_session *s);
bd_wm_server *bd_session_wm(bd_session *s);
/* The notification IPC server (NULL when it could not be
 * started). Polled from bd_session_step() like the WM server;
 * messages land in the shell's notification model. */
bd_nt_server *bd_session_notify_server(bd_session *s);
/* PID of the session's bd-alarmd helper, or 0 when none is
 * running (disabled, missing, or already exited and reaped). */
pid_t bd_session_alarmd_pid(const bd_session *s);
tk_canvas *bd_session_canvas(bd_session *s);
/* The desktop mascot (NULL when disabled or no pack found).
 * Configured through brainreg section "mascot": enabled
 * (bool, default true), pack (path to a .bdm; default search:
 * $BRAINUX_MASCOT_PACK, ~/.local/share/brainux/mascots/
 * mascot.bdm, /usr/share/brainux/mascots/mascot.bdm), size
 * (display height px, default 208), fullscreen (bool, default
 * false: stay visible over a fullscreen window). Visible only
 * on the desktop view; painted above the WM windows, below
 * the OSK. See shell/mascot/FORMAT.md. */
bd_mascot *bd_session_mascot(bd_session *s);

/* Route one toolkit event (screen coordinates) through the
 * session. Shell action keys (volume/brightness/home/...) are
 * handled first; while the on-screen keyboard is up, pointer
 * events that start inside its strip go to the OSK (it floats
 * above the windows); other pointer events go to the WM (windows
 * above the desktop) and fall through to the shell UI when no
 * window takes them; key/text events go to the focused WM window
 * when one exists, else to the shell UI. Physical typing
 * dismisses the OSK (see bd_shell_osk_suppress). Returns true
 * when consumed. The evdev drain uses this too - tests may call
 * it directly. */
bool bd_session_event(bd_session *s, tk_event *ev);

/* One main-loop iteration: service the WM socket (waiting up to
 * timeout_ms), drain input, run due timers, render one frame.
 * Returns the frame's framebuffer transfer in bytes - 0 when
 * nothing changed anywhere. With a virtual input the session
 * clock advances by timeout_ms per step, which makes the timers
 * deterministic in tests. */
size_t bd_session_step(bd_session *s, int timeout_ms);

/* Forced syncs (the step timers call these when due). */
void bd_session_sync_backend(bd_session *s);
int bd_session_refresh_catalog(bd_session *s);

#endif /* BD_SESSION_H */
