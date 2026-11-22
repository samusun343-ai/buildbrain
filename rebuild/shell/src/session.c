/* session.c - braindesk2 main loop. See session.h. */
#include "session.h"
#include "reg.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct bd_session {
    bd_shell *shell;
    tk_root *root;
    bd_wm_server *wm;
    bd_nt_server *notify;  /* NULL when start failed (non-fatal) */
    tk_canvas *canvas;
    tk_fb *fb;          /* borrowed */
    tk_evdev *input;    /* borrowed */
    tk_evdev *vinput;   /* borrowed */
    int w, h;
    long vnow_ms;          /* virtual clock (vinput sessions) */
    long last_backend_ms;
    long last_catalog_ms;
    int last_clock_min;    /* minute last shown, -1 = never */
    /* on-screen keyboard state */
    bool osk_applied;      /* inset/geometry currently applied */
    int osk_inset;         /* inset currently applied (px) */
    bool osk_capture;      /* current pointer gesture belongs to the OSK */
    /* desktop mascot state */
    bd_mascot *mascot;     /* NULL when disabled / no pack */
    bool mascot_over_fs;   /* reg mascot/fullscreen */
    bool mascot_capture;   /* current gesture belongs to the mascot */
    tk_rect mascot_dmg[8]; /* damage reported by the last tick
                              (v2 sparse bands need up to 8) */
    int mascot_ndmg;
    /* bd-alarmd helper process (0 = not running). Spawned at
     * creation, reaped by the step when it exits on its own,
     * terminated at free. See session.h alarmd_path. */
    pid_t alarmd_pid;
};

static long monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long session_now(const bd_session *s)
{
    return s->vinput ? s->vnow_ms : monotonic_ms();
}

/* Assist page keys -> the focused WM window. */
static void session_page(void *ud, int key)
{
    bd_session *s = ud;
    (void)bd_wm_server_key(s->wm, BD_WM_EV_KEY_DOWN, key, 0, NULL);
    (void)bd_wm_server_key(s->wm, BD_WM_EV_KEY_UP, key, 0, NULL);
}

/* Android-mode navigation sink (REPORT-status-icons section
 * 7): Back reaches the focused WM window as TK_KEY_BACK - the
 * same bd_wm_server_key route the page keys above use; whether
 * the app acts on it is the app's side of the contract. Home
 * minimizes every window so the shell's desktop (which the
 * shell side has already switched to) is what the user lands
 * on. Recents never arrives here: it is shell-internal UI fed
 * by the window bridge below. */
static void session_nav(void *ud, bd_nav_action act)
{
    bd_session *s = ud;
    if (act == BD_NAV_BACK) {
        (void)bd_wm_server_key(s->wm, BD_WM_EV_KEY_DOWN,
                               TK_KEY_BACK, 0, NULL);
        (void)bd_wm_server_key(s->wm, BD_WM_EV_KEY_UP,
                               TK_KEY_BACK, 0, NULL);
    } else if (act == BD_NAV_HOME) {
        size_t n = bd_wm_server_count(s->wm);
        for (size_t i = 0; i < n; i++) {
            bd_wm_win_info info;
            if (bd_wm_server_info(s->wm, i, &info) &&
                info.state != BD_WIN_MINIMIZED)
                (void)bd_wm_server_set_state(s->wm, info.id,
                                             BD_WIN_MINIMIZED);
        }
    }
}

/* Window bridge for the shell's app switcher: the WM server's
 * window list is the only source (titles, states, thumbnails). */
static size_t session_win_list(void *ud, bd_shell_win_entry *out,
                               size_t max)
{
    bd_session *s = ud;
    size_t n = bd_wm_server_count(s->wm);
    if (n > max) n = max;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        bd_wm_win_info info;
        if (!bd_wm_server_info(s->wm, i, &info)) continue;
        out[k].id = info.id;
        snprintf(out[k].title, sizeof(out[k].title), "%s", info.title);
        out[k].state = (int)info.state;
        out[k].focused = info.focused;
        k++;
    }
    return k;
}

static bool session_win_activate(void *ud, uint32_t id)
{
    bd_session *s = ud;
    bd_wm_win_info info;
    if (!bd_wm_server_info_id(s->wm, id, &info)) return false;
    if (info.state == BD_WIN_MINIMIZED)
        /* NORMAL restores the pre-minimize state + focus */
        (void)bd_wm_server_set_state(s->wm, id, BD_WIN_NORMAL);
    return bd_wm_server_focus(s->wm, id);
}

static bool session_win_close(void *ud, uint32_t id)
{
    bd_session *s = ud;
    return bd_wm_server_close(s->wm, id);
}

static bool session_win_thumbnail(void *ud, uint32_t id,
                                  uint32_t *px, int w, int h)
{
    bd_session *s = ud;
    return bd_wm_server_thumbnail(s->wm, id, px, w, h);
}

static const bd_shell_win_ops session_win_ops = {
    session_win_list, session_win_activate,
    session_win_close, session_win_thumbnail
};

/* OSK output sink: while the OSK serves a WM window, everything
 * it would deliver to a local editor is forwarded to the focused
 * window instead - commits as key events carrying text (the
 * established TEXT_INPUT route), compositions as BD_WM_COMP,
 * editing keys as plain key events. */
static bool session_osk_sink(void *ud, tk_event *ev)
{
    bd_session *s = ud;
    switch (ev->type) {
    case TK_EV_TEXT_INPUT:
        return bd_wm_server_key(s->wm, BD_WM_EV_KEY_DOWN, TK_KEY_NONE,
                                0, ev->text);
    case TK_EV_COMPOSITION:
        return bd_wm_server_send_composition(s->wm, ev->text);
    case TK_EV_KEY_DOWN:
        return bd_wm_server_key(s->wm, BD_WM_EV_KEY_DOWN, ev->key,
                                ev->modifiers, NULL);
    case TK_EV_KEY_UP:
        return bd_wm_server_key(s->wm, BD_WM_EV_KEY_UP, ev->key,
                                ev->modifiers, NULL);
    default:
        return false;
    }
}

/* ---------------- bd-alarmd helper ---------------- */

/* Spawn the resident alarm evaluator. Returns the child PID, or
 * 0 when disabled / missing / not executable / fork failed - all
 * non-fatal by design (see session.h). The daemon takes no
 * arguments: it resolves its data root from the environment and
 * $HOME, and posts through the default notification path, so it
 * must simply inherit the session's environment. It is
 * single-instance by flock, so an accidental double spawn exits
 * immediately and is reaped like any other early exit. */
static pid_t session_spawn_alarmd(const char *path)
{
    if (!path) path = "/usr/local/bin/bd-alarmd";
    if (!*path) return 0;
    if (access(path, X_OK) != 0) return 0;
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    return pid;
}

/* Reap the helper when it exited on its own (no restart: a
 * daemon that dies stays down for this session, and the PID
 * reporting 0 makes that visible instead of silent). */
static void session_reap_alarmd(bd_session *s)
{
    if (s->alarmd_pid <= 0) return;
    int st;
    if (waitpid(s->alarmd_pid, &st, WNOHANG) == s->alarmd_pid)
        s->alarmd_pid = 0;
}

/* Terminate the helper at session teardown: SIGTERM first (the
 * daemon exits 0 on it), a bounded grace period, then SIGKILL.
 * Always reaps, so the session never leaves a zombie or an
 * orphaned evaluator behind. */
static void session_stop_alarmd(bd_session *s)
{
    pid_t pid = s->alarmd_pid;
    if (pid <= 0) return;
    s->alarmd_pid = 0;
    int st;
    if (waitpid(pid, &st, WNOHANG) == pid) return;  /* already out */
    kill(pid, SIGTERM);
    for (int i = 0; i < 100; i++) {
        if (waitpid(pid, &st, WNOHANG) == pid) return;
        struct timespec ts = { 0, 10 * 1000 * 1000 };  /* 10 ms */
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
}

/* The mascot's floor: the simulation clamps the character's
 * anchor to this area, so the floor IS the region the sprite
 * may occupy. On the desktop view the icon grid owns the left
 * of the screen - start the floor to the right of the grid,
 * with room for half the sprite plus a margin, so the
 * character can never cover an icon or its label whatever
 * pack is loaded. (Integration phase 4: the shipped pack has
 * no Walk clip and the mascot stays at its spawn point, which
 * used to be the middle of the grid, hiding labels.) Falls
 * back to the full main rect when the grid leaves no usable
 * strip, and in every non-desktop view. */
static tk_rect session_mascot_floor(bd_session *s)
{
    tk_rect mr = bd_shell_main_rect(s->shell);
    if (bd_shell_view(s->shell) == BD_VIEW_DESKTOP) {
        tk_rect gr;
        if (bd_shell_desktop_grid_rect(s->shell, &gr)) {
            tk_rect sr = bd_mascot_rect(s->mascot);
            int half = sr.w > 0 ? sr.w / 2 : 104;
            /* The floor is the anchor's walk range: its left
             * edge puts the sprite's left edge 12px right of
             * the icon grid, its right edge is the main rect's
             * right edge.  The previous form additionally
             * demanded 110px of range beyond a further 24px
             * margin and fell back to the FULL main rect when
             * that failed - on the 854px screen with the WinUI
             * grid (616px and up) the test passed by 3px on the
             * host catalog and failed in the guest, so the
             * mascot spawned mid-grid and stayed there
             * (integration phase 10, second finding).  Now any
             * non-degenerate range beside the grid is used;
             * only a sprite wider than the whole strip pins
             * the anchor to the strip's left edge (the sprite
             * may then reach past the screen's right edge,
             * but it never covers the grid). */
            int lo = gr.x + gr.w + 12 + half;
            int hi = mr.x + mr.w;
            if (hi - lo >= 24)
                return (tk_rect){ lo, mr.y, hi - lo, mr.h };
            return (tk_rect){ lo, mr.y, 24, mr.h };
        }
    }
    return mr;
}

bd_session *bd_session_new(const bd_session_config *cfg)
{
    bd_session *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->w = cfg->width;
    s->h = cfg->height;
    s->fb = cfg->fb;
    s->input = cfg->input;
    s->vinput = cfg->input_virtual;
    s->last_clock_min = -1;

    s->shell = bd_shell_new(cfg->width, cfg->height, cfg->catalog,
                            cfg->store, cfg->exec_fn, cfg->exec_userdata);
    if (!s->shell) { free(s); return NULL; }
    s->root = bd_shell_root(s->shell);
    /* Toolkit M5: an fb-backed session presents its canvas 1:1
     * through tk_fb_flush_damage, so its ScrollViews (the
     * launcher's results list) may use the shift fast path. WM
     * clients present through the compositor and keep the
     * toolkit default (off); headless sessions keep the legacy
     * byte-accounting semantics. */
    if (s->fb) tk_root_set_scroll_blit(s->root, true);
    bd_shell_layout(s->shell, cfg->width, cfg->height);

    s->canvas = tk_canvas_new(cfg->width, cfg->height);
    s->wm = bd_wm_server_start(cfg->wm_path);
    if (!s->canvas || !s->wm) {
        if (s->wm) bd_wm_server_free(s->wm);
        if (s->canvas) tk_canvas_free(s->canvas);
        bd_shell_free(s->shell);
        free(s);
        return NULL;
    }
    /* Notification IPC (notify.h): apps post into the shell's
     * notification model. Non-fatal when unavailable. */
    s->notify = bd_nt_server_start(cfg->notify_path);

    /* Resident alarm/timer evaluation (bd-alarmd): spawned
     * after the notification server so the daemon's first posts
     * can already reach it. Non-fatal when unavailable. */
    s->alarmd_pid = session_spawn_alarmd(cfg->alarmd_path);

    bd_wm_server_set_screen(s->wm, (tk_rect){ 0, 0, cfg->width, cfg->height });
    tk_rect mr = bd_shell_main_rect(s->shell);
    if (mr.w > 0 && mr.h > 0) bd_wm_server_set_workarea(s->wm, mr);

    /* desktop mascot (reg section "mascot"; see session.h) */
    s->mascot_over_fs = reg_get_bool("mascot", "fullscreen", false);
    if (reg_get_bool("mascot", "enabled", true)) {
        char *pack = reg_get("mascot", "pack");
        char home_pack[512];
        if (!pack) {
            const char *env = getenv("BRAINUX_MASCOT_PACK");
            if (env && *env) pack = strdup(env);
        }
        if (!pack) {
            const char *home = getenv("HOME");
            if (home && *home) {
                snprintf(home_pack, sizeof(home_pack),
                         "%s/.local/share/brainux/mascots/mascot.bdm",
                         home);
                if (access(home_pack, R_OK) == 0) pack = strdup(home_pack);
            }
        }
        if (!pack && access("/usr/share/brainux/mascots/mascot.bdm",
                            R_OK) == 0)
            pack = strdup("/usr/share/brainux/mascots/mascot.bdm");
        if (pack) {
            bd_mascot_config mc = { 0 };
            mc.pack_path = pack;
            mc.size_px = reg_get_int("mascot", "size", 208);
            /* Same floor the per-frame tick applies, so the
             * spawn point is already clear of the icon grid. */
            mc.floor_area = session_mascot_floor(s);
            mc.seed = (uint32_t)(uintptr_t)s;
            s->mascot = bd_mascot_new(&mc);
            /* The floor above was computed with s->mascot still
             * NULL, so session_mascot_floor used the default
             * half-width and may have fallen back to the full
             * main rect (wide icon grid, WinUI).  Re-apply it now
             * that the real sprite size is known; set_floor also
             * pulls the spawn anchor inside the corrected floor
             * (integration phase 10). */
            if (s->mascot)
                bd_mascot_set_floor(s->mascot,
                                    session_mascot_floor(s));
            free(pack);
        }
    }

    /* keyboard settings from quick settings -> the input backend */
    tk_kbd_config kbd;
    bd_shell_kbd_config(s->shell, &kbd);
    if (s->input) tk_evdev_kbd_set_config(s->input, &kbd);
    if (s->vinput) tk_evdev_kbd_set_config(s->vinput, &kbd);

    if (cfg->backend) {
        bd_shell_attach_backend(s->shell, cfg->backend);
        bd_session_sync_backend(s);
    }
    if (cfg->wifi) bd_shell_attach_wifi(s->shell, cfg->wifi);
    bd_shell_set_page_sink(s->shell, session_page, s);
    bd_shell_set_nav_sink(s->shell, session_nav, s);
    bd_shell_set_win_ops(s->shell, &session_win_ops, s);
    bd_shell_osk_set_sink(s->shell, session_osk_sink, s);
    return s;
}

void bd_session_free(bd_session *s)
{
    if (!s) return;
    session_stop_alarmd(s);
    bd_mascot_free(s->mascot);
    bd_nt_server_free(s->notify);
    bd_wm_server_free(s->wm);
    tk_canvas_free(s->canvas);
    bd_shell_free(s->shell);
    free(s);
}

bd_shell *bd_session_shell(bd_session *s) { return s->shell; }
bd_wm_server *bd_session_wm(bd_session *s) { return s->wm; }
bd_nt_server *bd_session_notify_server(bd_session *s) { return s->notify; }
pid_t bd_session_alarmd_pid(const bd_session *s)
{
    return s ? s->alarmd_pid : 0;
}
tk_canvas *bd_session_canvas(bd_session *s) { return s->canvas; }
bd_mascot *bd_session_mascot(bd_session *s) { return s->mascot; }

void bd_session_sync_backend(bd_session *s)
{
    (void)bd_shell_poll_backend(s->shell);
    (void)bd_shell_poll_wifi(s->shell);
    (void)bd_shell_poll_windows(s->shell);
}

int bd_session_refresh_catalog(bd_session *s)
{
    return bd_shell_reload_catalog(s->shell);
}

/* ---------------- on-screen keyboard ---------------- */

static tk_rect rect_clip(tk_rect a, tk_rect b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x1 <= x0 || y1 <= y0) return (tk_rect){ 0, 0, 0, 0 };
    return (tk_rect){ x0, y0, x1 - x0, y1 - y0 };
}

/* Apply (or remove) the window-geometry consequences of the OSK
 * state:
 *   - open: the WM bottom inset becomes the OSK height. The
 *     server re-derives maximized/fullscreen geometry from it,
 *     so those windows shrink above the strip and restore
 *     exactly on close. A NORMAL focused window whose reported
 *     editor rect (or, without one, whose frame) would be
 *     covered by the strip is moved up by the overlap, clamped
 *     to the screen top. Its position is NOT restored on close:
 *     a floating window's position belongs to the user, and
 *     moving it back would fight later drags.
 *   - close: the inset returns to 0 and the strip is invalidated
 *     in both the shell root and the WM, so the frame repaints
 *     whatever the OSK covered. */
static void session_osk_apply(bd_session *s, bool open)
{
    bd_shell *sh = s->shell;
    if (open) {
        int h = bd_shell_osk_height(sh);
        bool fresh = !s->osk_applied || h != s->osk_inset;
        if (fresh) {
            bd_wm_server_set_bottom_inset(s->wm, h);
            s->osk_inset = h;
            uint32_t fid = bd_wm_server_focused(s->wm);
            bd_wm_win_info info;
            if (fid && bd_wm_server_info_id(s->wm, fid, &info) &&
                info.state == BD_WIN_NORMAL) {
                tk_rect osk = bd_shell_osk_rect(sh);
                tk_rect field;
                int bottom = bd_wm_server_im_field(s->wm, &field)
                                 ? field.y + field.h
                                 : info.frame.y + info.frame.h;
                int overlap = bottom - osk.y;
                if (overlap > 0) {
                    int ny = info.frame.y - overlap;
                    if (ny < 0) ny = 0;
                    if (ny != info.frame.y)
                        bd_wm_server_move(s->wm, fid, info.frame.x, ny);
                }
            }
            tk_root_invalidate_rect(bd_shell_osk_root(sh),
                                    bd_shell_osk_rect(sh));
        }
        s->osk_applied = true;
    } else if (s->osk_applied) {
        tk_rect osk = { 0, s->h - s->osk_inset, s->w, s->osk_inset };
        bd_wm_server_set_bottom_inset(s->wm, 0);
        s->osk_inset = 0;
        s->osk_applied = false;
        s->osk_capture = false;
        tk_root_invalidate_rect(s->root, osk);
        bd_wm_server_invalidate(s->wm, osk);
    }
}

/* One policy pass: the shell decides open/close from the focus
 * state (its own focused editor, or the focused WM window's IM
 * report); the session applies the geometry consequences. */
static void session_osk_sync(bd_session *s)
{
    bool remote = bd_wm_server_im_active(s->wm);
    bool open = bd_shell_osk_update(s->shell, remote,
                                    bd_wm_server_im_serial(s->wm));
    session_osk_apply(s, open);
}

/* ---------------- event routing ---------------- */

static bool session_action(bd_session *s, int action)
{
    bd_shell *sh = s->shell;
    switch (action) {
    case TK_ACTION_VOLUME_UP:
        bd_shell_set_volume(sh, bd_shell_volume(sh) + 10);
        return true;
    case TK_ACTION_VOLUME_DOWN:
        bd_shell_set_volume(sh, bd_shell_volume(sh) - 10);
        return true;
    case TK_ACTION_MUTE:
        bd_shell_set_muted(sh, !bd_shell_muted(sh));
        return true;
    case TK_ACTION_BRIGHTNESS_UP:
        bd_shell_set_brightness(sh, bd_shell_brightness(sh) + 10);
        return true;
    case TK_ACTION_BRIGHTNESS_DOWN:
        bd_shell_set_brightness(sh, bd_shell_brightness(sh) - 10);
        return true;
    case TK_ACTION_HOME:
        bd_shell_show_launcher(sh, false);
        return true;
    case TK_ACTION_SEARCH:
        bd_shell_show_launcher(sh, true);
        return true;
    default:
        return false;
    }
}

bool bd_session_event(bd_session *s, tk_event *ev)
{
    if (ev->type == TK_EV_ACTION) return session_action(s, ev->action);
    switch (ev->type) {
    case TK_EV_POINTER_DOWN:
    case TK_EV_POINTER_UP:
    case TK_EV_POINTER_MOVE: {
        /* The OSK floats above the windows: it gets first claim
         * on a touch that starts inside its strip and keeps the
         * gesture until the finger lifts (a flick leaves the
         * strip by design). */
        if (s->osk_applied) {
            if (ev->type == TK_EV_POINTER_DOWN) {
                tk_rect osk = bd_shell_osk_rect(s->shell);
                s->osk_capture = osk.w > 0 &&
                    ev->x >= osk.x && ev->x < osk.x + osk.w &&
                    ev->y >= osk.y && ev->y < osk.y + osk.h;
            }
            if (s->osk_capture) {
                bool done = tk_root_event(bd_shell_osk_root(s->shell), ev);
                if (ev->type == TK_EV_POINTER_UP) s->osk_capture = false;
                return done;
            }
        }
        /* The mascot floats above the windows on the desktop:
         * a touch that starts on it is its gesture (drag/tap)
         * until the finger lifts. A hidden mascot (a window is
         * in front, contract 4.2) never starts a gesture. */
        if (s->mascot && ((ev->type == TK_EV_POINTER_DOWN &&
                           bd_mascot_visible(s->mascot)) ||
                          s->mascot_capture)) {
            bool done = bd_mascot_pointer(s->mascot, ev->type, ev->x,
                                          ev->y, session_now(s));
            if (done) {
                s->mascot_capture = ev->type != TK_EV_POINTER_UP;
                return true;
            }
            if (ev->type == TK_EV_POINTER_UP) s->mascot_capture = false;
        }
        /* Shell overlays (launcher, recents switcher, quick
         * settings, notification sheet) are composited ABOVE
         * the windows, so at points they cover the shell gets
         * first claim: the WM-first order below would hand a
         * tap on a switcher card to the maximized window
         * underneath it, and the card could never be tapped
         * (integration phase 6). Points the overlay does not
         * consume still fall through to the windows. */
        if (bd_shell_overlay_hit(s->shell, ev->x, ev->y) &&
            tk_root_event(s->root, ev))
            return true;
        int wt = ev->type == TK_EV_POINTER_DOWN ? BD_WM_EV_POINTER_DOWN :
                 ev->type == TK_EV_POINTER_UP ? BD_WM_EV_POINTER_UP :
                                                BD_WM_EV_POINTER_MOVE;
        if (bd_wm_server_pointer(s->wm, wt, ev->x, ev->y)) return true;
        return tk_root_event(s->root, ev);
    }
    case TK_EV_KEY_DOWN:
    case TK_EV_KEY_UP: {
        int wt = ev->type == TK_EV_KEY_DOWN ? BD_WM_EV_KEY_DOWN
                                            : BD_WM_EV_KEY_UP;
        bool handled;
        if (bd_wm_server_focused(s->wm) != 0 &&
            bd_wm_server_key(s->wm, wt, ev->key, ev->modifiers, ev->text))
            handled = true;
        else
            handled = tk_root_event(s->root, ev);
        /* Physical typing means the built-in keyboard is in use:
         * the OSK steps aside (the key itself was delivered
         * above) and stays down until the input target changes. */
        if (ev->type == TK_EV_KEY_DOWN && s->osk_applied &&
            (ev->text || ev->key == TK_KEY_BACKSPACE ||
             ev->key == TK_KEY_DELETE || ev->key == TK_KEY_ENTER ||
             ev->key == TK_KEY_SPACE)) {
            bd_shell_osk_suppress(s->shell);
            session_osk_apply(s, false);
        }
        return handled;
    }
    case TK_EV_TEXT_INPUT: {
        /* committed text (IME) belongs to the focused window when
         * there is one, as a key event carrying the text */
        bool handled;
        if (bd_wm_server_focused(s->wm) != 0 &&
            bd_wm_server_key(s->wm, BD_WM_EV_KEY_DOWN, TK_KEY_NONE, 0,
                             ev->text))
            handled = true;
        else
            handled = tk_root_event(s->root, ev);
        /* text committed from the physical path dismisses the OSK
         * like any other physical typing */
        if (s->osk_applied) {
            bd_shell_osk_suppress(s->shell);
            session_osk_apply(s, false);
        }
        return handled;
    }
    default:
        return tk_root_event(s->root, ev);
    }
}

/* ---------------- frame ---------------- */

static size_t session_frame(bd_session *s)
{
    /* Windows float above the shell UI. Areas the WM reports
     * damaged need the shell background repainted first, so fold
     * them into the root's damage; then repaint; then draw the
     * windows over exactly the areas this frame repainted (their
     * own damage is already in the WM accumulator). The OSK
     * floats above even the windows: it is painted last, and any
     * damage the lower layers took inside its strip is folded
     * into the OSK root so the keyboard is redrawn over it. */
    tk_rect dmg[64];
    size_t nd = bd_wm_server_damage(s->wm, dmg, 64);
    tk_rect osk = s->osk_applied ? bd_shell_osk_rect(s->shell)
                                 : (tk_rect){ 0, 0, 0, 0 };
    tk_root *oroot = bd_shell_osk_root(s->shell);
    for (size_t i = 0; i < nd; i++) {
        tk_root_invalidate_rect(s->root, dmg[i]);
        if (osk.w > 0) {
            tk_rect in = rect_clip(dmg[i], osk);
            if (in.w > 0) tk_root_invalidate_rect(oroot, in);
        }
    }
    /* Mascot movement vacates/claims desktop area: the lower
     * layers repaint it like WM damage, the mascot itself is
     * blended over the result below. */
    for (int i = 0; i < s->mascot_ndmg; i++) {
        tk_root_invalidate_rect(s->root, s->mascot_dmg[i]);
        bd_wm_server_invalidate(s->wm, s->mascot_dmg[i]);
        if (osk.w > 0) {
            tk_rect in = rect_clip(s->mascot_dmg[i], osk);
            if (in.w > 0) tk_root_invalidate_rect(oroot, in);
        }
    }
    s->mascot_ndmg = 0;
    tk_root_paint_damage(s->root, s->canvas, tk_theme_default());
    size_t nc = tk_canvas_damage_count(s->canvas);
    for (size_t i = 0; i < nc; i++) {
        bd_wm_server_invalidate(s->wm, tk_canvas_damage_rect(s->canvas, i));
        if (osk.w > 0) {
            tk_rect in = rect_clip(tk_canvas_damage_rect(s->canvas, i), osk);
            if (in.w > 0) tk_root_invalidate_rect(oroot, in);
        }
    }
    bd_wm_composite(s->wm, s->canvas, tk_theme_default());
    /* The mascot floats above the windows (below the OSK): it
     * is blended over exactly this frame's damaged areas - the
     * tick already folded its own movement into that damage. */
    if (s->mascot && bd_mascot_visible(s->mascot)) {
        tk_rect cd[64];
        size_t ncd = tk_canvas_damage_count(s->canvas);
        if (ncd > 64) ncd = 64;
        for (size_t i = 0; i < ncd; i++)
            cd[i] = tk_canvas_damage_rect(s->canvas, i);
        bd_mascot_paint(s->mascot, s->canvas, cd, ncd);
    }
    /* Shell overlays (launcher, recents switcher, quick
     * settings, notification sheet) belong ABOVE the windows:
     * they were painted as part of the shell root UNDER the
     * composited windows, so while one is open its surface is
     * repainted over the windows (phase 6). Only the panels'
     * own rects are repainted, and only while one is open -
     * a quiet desktop frame still transfers zero bytes. */
    {
        tk_rect ov[4];
        size_t nov = bd_shell_overlay_rects(s->shell, ov, 4);
        for (size_t i = 0; i < nov; i++)
            tk_root_invalidate_rect(s->root, ov[i]);
        if (nov)
            tk_root_paint_damage(s->root, s->canvas,
                                 tk_theme_default());
    }
    if (osk.w > 0)
        tk_root_paint_damage(oroot, s->canvas, tk_theme_default());
    if (s->fb) return tk_fb_flush_damage(s->fb, s->canvas);
    /* No framebuffer (headless embedder): report the damage as
     * 16bpp bytes so callers/tests can observe transfer volume,
     * then clear it like a flush would. */
    size_t bytes = 0;
    for (size_t i = 0; i < tk_canvas_damage_count(s->canvas); i++) {
        tk_rect r = tk_canvas_damage_rect(s->canvas, i);
        if (r.w > 0 && r.h > 0) bytes += (size_t)r.w * (size_t)r.h * 2u;
    }
    tk_canvas_damage_clear(s->canvas);
    return bytes;
}

/* ---------------- step ---------------- */

static void session_clock(bd_session *s)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    if (tmv.tm_min == s->last_clock_min) return;
    s->last_clock_min = tmv.tm_min;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    bd_shell_set_clock(s->shell, buf);
}

size_t bd_session_step(bd_session *s, int timeout_ms)
{
    if (timeout_ms < 0) timeout_ms = 0;
    /* 0. Helper supervision: reap bd-alarmd if it exited on its
     * own, so it never lingers as a zombie. One waitpid per step
     * is negligible at the session's cadence. */
    session_reap_alarmd(s);
    /* 1. WM socket (may wait): connects, commits, resizes. */
    bd_wm_server_poll(s->wm, timeout_ms);
    /* 1a. Notification socket (never waits): posts/cancels land
     * in the shell's notification model before input routing,
     * so a notification raised by this step's input is visible
     * in the same frame. */
    if (s->notify) bd_nt_server_poll(s->notify, s->shell);
    /* 1b. OSK policy: IM reports from the poll above (and the
     * shell's own focus) decide whether the keyboard is up; the
     * geometry consequences land before input is routed so a
     * touch in the strip reaches the OSK in the same step. */
    session_osk_sync(s);
    /* 2. Input drain through the production evdev path. */
    if (s->vinput) {
        s->vnow_ms += timeout_ms > 0 ? timeout_ms : 1;
        tk_event ev;
        while (tk_evdev_poll_at(s->vinput, s->vnow_ms, &ev))
            bd_session_event(s, &ev);
    }
    if (s->input) {
        tk_event ev;
        while (tk_evdev_poll(s->input, &ev))
            bd_session_event(s, &ev);
    }
    tk_root_tick(s->root, (uint32_t)session_now(s));
    /* 3. Timers (session clock: virtual when testing). */
    long now = session_now(s);
    if (now - s->last_backend_ms >= 1000) {
        s->last_backend_ms = now;
        bd_session_sync_backend(s);
    }
    if (now - s->last_catalog_ms >= 5000) {
        s->last_catalog_ms = now;
        bd_session_refresh_catalog(s);
    }
    session_clock(s);
    /* 3b. Desktop mascot: visibility policy and one simulation
     * tick. Its damage is folded into the frame below.
     * Layer rule (DESIGN-layout-contract.md 4.2): the mascot
     * lives on the DESKTOP layer, so it is present only while
     * the desktop itself is the frontmost surface - no window
     * in front of it. The rule this replaces hid it only over
     * a focused FULLSCREEN window, which let it paint over a
     * maximized app's content (the phase-6 follow-up finding:
     * the mascot covered the settings scrollbar and the
     * calculator's right column in the guest shots). HOME
     * minimizes every window, so "desktop frontmost" is
     * exactly "no non-minimized window exists"; the old
     * mascot/fullscreen opt-in is subsumed and gone. */
    if (s->mascot) {
        bool vis = bd_shell_view(s->shell) == BD_VIEW_DESKTOP;
        if (vis) {
            size_t n = bd_wm_server_count(s->wm);
            for (size_t i = 0; i < n; i++) {
                bd_wm_win_info info;
                if (bd_wm_server_info(s->wm, i, &info) &&
                    info.state != BD_WIN_MINIMIZED) {
                    vis = false;
                    break;
                }
            }
        }
        bd_mascot_set_floor(s->mascot, session_mascot_floor(s));
        bd_mascot_set_visible(s->mascot, vis);
        s->mascot_ndmg = bd_mascot_tick(s->mascot, now,
                                        s->mascot_dmg, 8);
    }
    /* 4. One damage-driven frame. */
    return session_frame(s);
}
