/* shellui - the M1 shell UI on top of libbrainui2 (toolkit v2).
 *
 * What M1 is (see DESIGN-shell-v2.md):
 *   - A widget-tree shell that renders into a memory canvas (and,
 *     via tk_fb, an fbdev canvas on device) and can be driven by
 *     synthetic events in tests: desktop, panel, launcher.
 *   - The launcher is driven by the real .desktop catalog and the
 *     pin/recent store - nothing is hardcoded.
 *   - App launching goes through the ln_exec_fn seam.
 *
 * What M1 is NOT:
 *   - Not a window manager for real clients yet: the window/task
 *     model below is the shell-side model (task list, active task,
 *     cycling) without client connections. Client protocol, resize
 *     of real windows, and the X coexistence gate are later
 *     milestones, specified in DESIGN-shell-v2.md sections 3-4.
 *   - Not session-integrated: no greeter, not the default session.
 */
#ifndef BD_SHELLUI_H
#define BD_SHELLUI_H

#include <stdbool.h>
#include <stddef.h>
#include "tk.h"
#include "tk_kbd.h"
#include "osk.h"
#include "desktop.h"
#include "launcher.h"
#include "iconcache.h"
#include "backend.h"
#include "assist.h"
#include "wifi.h"

typedef struct bd_shell bd_shell;

typedef enum { BD_VIEW_DESKTOP = 0, BD_VIEW_LAUNCHER = 1 } bd_view;

#define BD_TASK_MAX 16
#define BD_NOTIFY_MAX 4

/* Create the shell for a w x h screen. Settings are read through
 * the reg store at creation: shell/wallpaper (#RRGGBB color, or an
 * absolute image path - loaded once through the icon cache, scaled
 * to the screen), shell/panel_position ("top"|"bottom"),
 * shell/volume (0..100), shell/wifi (bool). exec_fn may be NULL
 * (launches then fail). */
bd_shell *bd_shell_new(int w, int h, de_catalog *catalog, ln_store *store,
                       ln_exec_fn exec_fn, void *exec_userdata);
void bd_shell_free(bd_shell *sh);

tk_root *bd_shell_root(bd_shell *sh);
tk_widget *bd_shell_top(bd_shell *sh);
/* Layout for a w x h screen: toolkit layout plus fitting the
 * Fixed views (desktop/launcher) to the main area. Embedders call
 * this on start and on resize instead of tk_root_layout directly. */
void bd_shell_layout(bd_shell *sh, int w, int h);
/* The main area's rect in screen coordinates as of the last
 * bd_shell_layout() (the region below/above the panel). The
 * session uses it as the WM workarea. Zero rect before layout. */
tk_rect bd_shell_main_rect(const bd_shell *sh);
/* The desktop icon grid's rect in screen coordinates as of the
 * last bd_shell_layout(). Returns false before the first layout
 * or when the grid has no area. The session uses it to keep the
 * desktop mascot's floor clear of the icons and their labels. */
bool bd_shell_desktop_grid_rect(const bd_shell *sh, tk_rect *out);

/* --- views --- */
bd_view bd_shell_view(const bd_shell *sh);
void bd_shell_show_launcher(bd_shell *sh, bool show);

/* --- launcher state --- */
/* Re-run the current category+search query and refill the list.
 * Works purely from the catalog's in-memory parse results. */
void bd_shell_refresh_apps(bd_shell *sh);
/* Rescan the catalog's directories (mtime-invalidated re-parse of
 * changed .desktop files only) and refill the list. Returns the
 * number of catalog changes. Desktop icons are built at creation
 * and are not rebuilt by this call (documented M1 limit). */
int bd_shell_reload_catalog(bd_shell *sh);
void bd_shell_set_category(bd_shell *sh, de_category cat);
de_category bd_shell_category(const bd_shell *sh);
void bd_shell_set_query(bd_shell *sh, const char *query);
size_t bd_shell_result_count(const bd_shell *sh);
const de_entry *bd_shell_result_at(const bd_shell *sh, size_t i);
/* Activate (launch) result i through the exec seam. */
bool bd_shell_activate_result(bd_shell *sh, size_t i);

/* --- panel status --- */
void bd_shell_set_clock(bd_shell *sh, const char *hhmm);
void bd_shell_set_battery(bd_shell *sh, int pct, bool charging); /* pct<0 = unknown */
int bd_shell_battery(const bd_shell *sh);
void bd_shell_set_volume(bd_shell *sh, int pct); /* persists to reg */
int bd_shell_volume(const bd_shell *sh);
/* Mute is a state SEPARATE from the volume level: the level
 * (bd_shell_volume) is preserved while muted and is what
 * unmuting restores. Mute persists to reg (shell/muted). The
 * EFFECTIVE volume - what the backend (ALSA) is driven to and
 * what the tray icon shows - is 0 while muted. An explicit
 * volume change to a different level through
 * bd_shell_set_volume() clears mute (the hardware-key
 * convention: Android/Windows/GNOME all unmute on adjust);
 * setting the same level does not. */
void bd_shell_set_muted(bd_shell *sh, bool muted); /* persists + backend */
bool bd_shell_muted(const bd_shell *sh);
int bd_shell_effective_volume(const bd_shell *sh); /* 0 when muted */

/* --- quick settings --- */
bool bd_shell_quick_open(const bd_shell *sh);
void bd_shell_set_quick_open(bd_shell *sh, bool open);
void bd_shell_set_wifi(bd_shell *sh, bool on); /* persists to reg */
bool bd_shell_wifi(const bd_shell *sh);
/* Wi-Fi real control (M3): attach a bd_wifi (not owned). Once
 * attached, bd_shell_set_wifi also drives the supplicant
 * (on = RECONNECT, off = DISCONNECT), attaching applies the saved
 * reg preference once, and bd_shell_poll_wifi() syncs the quick
 * settings status line from STATUS. The network-settings screen
 * uses the bd_wifi API directly (scan/connect); the shell only
 * owns the toggle + status surface. Without an attached bd_wifi
 * the M1/M2 reg-only behaviour is unchanged. */
void bd_shell_attach_wifi(bd_shell *sh, bd_wifi *w);
int bd_shell_poll_wifi(bd_shell *sh); /* 1 = status synced, 0 = none/dead */
bool bd_shell_wifi_connected(const bd_shell *sh);
const char *bd_shell_wifi_ssid(const bd_shell *sh); /* "" when none */
int bd_shell_wifi_signal_dbm(const bd_shell *sh); /* live RSSI, 0 = unknown */

/* Icon assets (REPORT-status-icons section 8): load a
 * vector-set render (<set>/<name>.png under the shell's asset
 * roots) tinted to a theme colour at an exact box size.
 * Returns a malloc'd box*box canvas-packed buffer or false. */
bool bd_icon_pixels(const char *set, const char *name, int box,
                    tk_color tint, uint32_t **out_px);
/* Keyboard settings (M2): the shell owns the "keyboard" reg
 * section (see kbdsettings.h). The quick settings panel has a
 * repeat preset cycle, which persists immediately. (No layout
 * toggle exists: the built-in keyboard has one fixed keymap.)
 * The session embedder reads the config with
 * bd_shell_kbd_config() and applies it to its evdev backend
 * (tk_evdev_kbd_set_config) - at startup, and again whenever it
 * wants to pick up changes made in the panel. */
void bd_shell_kbd_config(const bd_shell *sh, tk_kbd_config *out);
void bd_shell_set_kbd_config(bd_shell *sh, const tk_kbd_config *cfg);

/* --- task model (shell side; no client windows in M1) --- */
size_t bd_shell_task_count(const bd_shell *sh);
const char *bd_shell_active_task(const bd_shell *sh); /* name or NULL */
bool bd_shell_cycle_task(bd_shell *sh);  /* activate next task */
bool bd_shell_close_task(bd_shell *sh, size_t idx);

/* --- notifications (model + panel toast label) --- */
bool bd_shell_notify(bd_shell *sh, const char *text);
size_t bd_shell_notify_count(const bd_shell *sh);
const char *bd_shell_notify_at(const bd_shell *sh, size_t i);
void bd_shell_notify_dismiss(bd_shell *sh);
/* Structured post, used by the notification IPC (notify.h):
 * carries the sender app name and an urgency (bd_nt_urgency)
 * and returns the notification id (> 0), or 0 on failure, so
 * the sender can cancel it later via bd_shell_notify_cancel().
 * Unlike bd_shell_notify() - which fails when the list is
 * full - a structured post EVICTS the oldest entry when full:
 * the list is a bounded history (BD_NOTIFY_MAX entries), not a
 * queue that can wedge a poster (REPORT-os-internals-1.md
 * section 3.3). Display text is the title, or "title - body"
 * when a body is present. The app name is metadata (queryable
 * below); it is not part of the display text. */
uint32_t bd_shell_notify_post(bd_shell *sh, const char *app,
                              const char *title, const char *body,
                              int urgency);
bool bd_shell_notify_cancel(bd_shell *sh, uint32_t id);
uint32_t bd_shell_notify_id_at(const bd_shell *sh, size_t i);
const char *bd_shell_notify_app_at(const bd_shell *sh, size_t i);
int bd_shell_notify_urgency_at(const bd_shell *sh, size_t i);

/* --- hardware backend (M2) --- */
/* Attach a backend (not owned). Once attached, volume changes
 * are applied to it in addition to the reg persist, the assist
 * overlay's brightness actions drive it, and bd_shell_poll_backend
 * syncs panel state from it. Without a backend the shell keeps
 * its M1 behavior (state + reg only). */
void bd_shell_attach_backend(bd_shell *sh, bd_backend *be);
/* Pull battery/volume/brightness from the attached backend into
 * the panel state. Returns the number of fields updated, 0 when
 * no backend is attached. Poll results update the display only;
 * they are not written back to reg (hardware is the truth here,
 * reg is the user's saved preference at set time). While muted
 * the volume field is skipped: the shell itself drives the
 * hardware to 0 then, so the read-back is not the set level. */
int bd_shell_poll_backend(bd_shell *sh);
void bd_shell_set_brightness(bd_shell *sh, int pct); /* persists + backend */
int bd_shell_brightness(const bd_shell *sh);

/* --- single-finger assist overlay (M2) --- */
bool bd_shell_assist_open(const bd_shell *sh);
void bd_shell_set_assist_open(bd_shell *sh, bool open);
/* Page up/down from the assist overlay leave through this sink
 * (the session wires it to the focused WM window). key is a
 * tk_key (TK_KEY_PAGE_UP / TK_KEY_PAGE_DOWN). */
typedef void (*bd_shell_page_cb)(void *userdata, int key);
void bd_shell_set_page_sink(bd_shell *sh, bd_shell_page_cb cb, void *userdata);
typedef void (*bd_shell_lock_cb)(void *userdata);
void bd_shell_set_lock_cb(bd_shell *sh, bd_shell_lock_cb cb, void *userdata);

/* --- Android-mode 3-button navigation + app switcher ---
 * (REPORT-status-icons section 7). The buttons exist only in
 * the Android mode (theme style 1): Back / Home / Recents in
 * the tablet form - the trio grouped in the shelf, order
 * Back - Home - Recents. WinUI mode keeps the taskbar form and
 * has no nav cluster.
 *
 * BACK: shell panels consume it first (recents, quick settings,
 * notification sheet, launcher - Android's order of dismissal);
 * when no panel is open it leaves through the nav sink, and the
 * session delivers TK_KEY_BACK to the focused WM window (the
 * route exists: bd_wm_server_key, the same one the assist page
 * keys use). Whether an app acts on the key is the app's side
 * of the contract.
 * HOME: closes every shell panel, shows the desktop view, and
 * leaves through the nav sink so the session can minimize the
 * WM windows (the desktop is under them otherwise).
 * RECENTS: opens the shell's app switcher, fed by the window
 * bridge below - the WM's window list is the truth, not the
 * shell's launch model. */
typedef enum { BD_NAV_BACK = 0, BD_NAV_HOME, BD_NAV_RECENTS } bd_nav_action;
typedef void (*bd_shell_nav_cb)(void *userdata, bd_nav_action act);
void bd_shell_set_nav_sink(bd_shell *sh, bd_shell_nav_cb cb, void *userdata);
void bd_shell_nav(bd_shell *sh, bd_nav_action act); /* as if tapped */
bool bd_shell_recents_open(const bd_shell *sh);
void bd_shell_set_recents_open(bd_shell *sh, bool open);
/* Shell overlay surfaces (launcher, recents switcher, quick
 * settings, notification sheet) float above the WM windows:
 * the session repaints them over the composited windows while
 * one is open, and routes pointer input to the shell first at
 * points they cover, so a window underneath can neither hide
 * them nor steal their taps (integration phase 6). */
bool bd_shell_overlay_open(const bd_shell *sh);
size_t bd_shell_overlay_rects(const bd_shell *sh, tk_rect *out,
                              size_t max);
bool bd_shell_overlay_hit(const bd_shell *sh, int x, int y);

/* Window bridge for the app switcher, implemented by the
 * session over the WM server. list() fills up to `max` entries
 * in z-order (bottom first) and returns the count. activate()
 * raises + focuses a window, restoring it first when minimized.
 * close() asks the window to exit (BD_WM_CLOSE). thumbnail()
 * scales the window's last committed content into px (w x h);
 * false when it has committed nothing yet. */
typedef struct {
    uint32_t id;
    char title[64];
    int state;      /* bd_win_state values (wm.h) */
    bool focused;
} bd_shell_win_entry;
typedef struct {
    size_t (*list)(void *ud, bd_shell_win_entry *out, size_t max);
    bool (*activate)(void *ud, uint32_t id);
    bool (*close)(void *ud, uint32_t id);
    bool (*thumbnail)(void *ud, uint32_t id, uint32_t *px, int w, int h);
} bd_shell_win_ops;
void bd_shell_set_win_ops(bd_shell *sh, const bd_shell_win_ops *ops,
                          void *userdata);
/* Band cover (proportion pass): while a window is up, the
 * strip around the floating shelf is painted as shell surface
 * instead of showing the (near-black) wallpaper under a bright
 * window. bd_shell_poll_windows() syncs the cover's visibility
 * from the win_ops window list; the session polls it with the
 * backend sync. */
int bd_shell_poll_windows(bd_shell *sh);
bool bd_shell_band_cover_visible(const bd_shell *sh);

/* --- on-screen keyboard (system-wide, OSK phase) ---
 * The OSK lives in its OWN root, separate from the shell UI
 * root: the session paints it AFTER compositing the WM windows
 * (so it floats above even fullscreen clients) and routes
 * pointer events to it before the WM. Show/hide is focus-driven
 * through bd_shell_osk_update(), called by the session once per
 * step:
 *   - when the shell's own focused widget is a text editor, the
 *     OSK attaches to that editor directly;
 *   - else, when remote_active (the focused WM window reported
 *     an active text editor via BD_WM_IM), the OSK attaches to a
 *     sink whose events leave through the sink callback - the
 *     session forwards them to the focused window;
 *   - otherwise the OSK closes.
 * bd_shell_osk_suppress() is the physical-keyboard latch: once
 * the user starts typing on the built-in keyboard the OSK closes
 * and stays closed until the input target changes. */
tk_root *bd_shell_osk_root(bd_shell *sh);
tk_widget *bd_shell_osk_widget(bd_shell *sh);
bool bd_shell_osk_open(const bd_shell *sh);
/* Natural height (px) of the OSK at the shell's width in its
 * current mode; the session reserves this as the WM bottom
 * inset while the OSK is open. */
int bd_shell_osk_height(const bd_shell *sh);
/* Screen rect of the OSK strip; empty when closed. */
tk_rect bd_shell_osk_rect(const bd_shell *sh);
/* remote_serial is the WM's IM change counter
 * (bd_wm_server_im_serial): it identifies the remote input
 * target, so a focus cycle the sampler never saw (inactive and
 * active again between two steps) still counts as a new target
 * and clears the physical-key latch. Pass 0 when there is no WM. */
bool bd_shell_osk_update(bd_shell *sh, bool remote_active,
                         uint32_t remote_serial);
void bd_shell_osk_suppress(bd_shell *sh);
typedef bool (*bd_shell_osk_sink_cb)(void *userdata, tk_event *ev);
void bd_shell_osk_set_sink(bd_shell *sh, bd_shell_osk_sink_cb cb,
                           void *userdata);
/* Inject the IME backend for the shell OSK (NULL restores the
 * toolkit's built-in kana fallback). The session embedder wires
 * the libbrainime backend; the shell itself stays engine-free. */
void bd_shell_osk_set_backend(bd_shell *sh, const osk_ime_backend *be,
                              void *engine);

/* --- settings applied at creation (for tests/diagnostics) --- */
bool bd_shell_panel_on_top(const bd_shell *sh);
tk_color bd_shell_wallpaper(const bd_shell *sh);
/* The shell's icon cache (decode+scale happens once per icon+size;
 * see iconcache.h). Owned by the shell. */
ic_cache *bd_shell_icon_cache(bd_shell *sh);

#endif /* BD_SHELLUI_H */
