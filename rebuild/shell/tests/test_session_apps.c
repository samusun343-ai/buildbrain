/* test_session_apps - the DE's own apps as WM clients of a real
 * session (integration phase 6: the shell bar must stay visible
 * AND operable while apps are in front).
 *
 * The fb-era apps drew over the whole panel, bar included; as
 * WM clients (apps/common/bdapp_run.c) they must instead:
 *   - open MAXIMIZED inside the work area (the bar's strip is
 *     never covered),
 *   - coexist: a second app does not kill or minimize the first,
 *   - leave the bar pixel-identical (it is the shell's own
 *     layer, composited above the windows),
 *   - be switchable FROM THE BAR: a tap on the real recents
 *     button (virtual input through bd_session_event) opens the
 *     switcher, and a tap on the other app's card focuses it.
 *
 * Usage: test_session_apps <path-to-bd-calc> <path-to-bd-notes>
 * With no arguments the test SKIPs (exit 0): the apps tree
 * builds it via its own check target with real paths.
 */
#include "session.h"
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "shellui.h"
#include "thememode.h"
#include "wm.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static char reg_userdir[256];

static void setup_reg(void)
{
    static char sysdir[256];
    char tmpl[] = "/tmp/bdsessappsXXXXXX";
    char *base = mkdtemp(tmpl);
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(reg_userdir, sizeof(reg_userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(reg_userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", reg_userdir, 1);
}

/* ---- widget tree search (buttons by label, labels by text) --- */

static tk_widget *find_btn(tk_widget *w, const char *label)
{
    if (!w) return NULL;
    if (tk_widget_kind_of(w) == TK_W_BUTTON &&
        tk_is_visible(w)) {
        const char *l = tk_button_label(w);
        if (l && strcmp(l, label) == 0) return w;
    }
    for (size_t i = 0; i < tk_child_count(w); i++) {
        tk_widget *hit = find_btn(tk_child_at(w, i), label);
        if (hit) return hit;
    }
    return NULL;
}

static tk_widget *find_lbl(tk_widget *w, const char *text)
{
    if (!w) return NULL;
    if (tk_widget_kind_of(w) == TK_W_LABEL && tk_is_visible(w)) {
        const char *l = tk_label_text(w);
        if (l && strcmp(l, text) == 0) return w;
    }
    for (size_t i = 0; i < tk_child_count(w); i++) {
        tk_widget *hit = find_lbl(tk_child_at(w, i), text);
        if (hit) return hit;
    }
    return NULL;
}

/* ---- session driving ---- */

static void settle(bd_session *s, int max_steps)
{
    int quiet = 0;
    for (int i = 0; i < max_steps && quiet < 3; i++) {
        size_t moved = bd_session_step(s, 5);
        quiet = moved == 0 ? quiet + 1 : 0;
        usleep(1000);
    }
}

static bool wait_windows(bd_session *s, size_t n, int max_steps)
{
    bd_wm_server *wm = bd_session_wm(s);
    for (int i = 0; i < max_steps; i++) {
        if (bd_wm_server_count(wm) == n) return true;
        bd_session_step(s, 5);
        usleep(1000);
    }
    return bd_wm_server_count(wm) == n;
}

/* Step until the window has a committed frame (thumbnail
 * available) - process startup and first paint take real time
 * that a fixed settle cannot budget for. */
static bool wait_committed(bd_session *s, uint32_t id, int max_steps)
{
    static uint32_t probe[8 * 8];
    bd_wm_server *wm = bd_session_wm(s);
    for (int i = 0; i < max_steps; i++) {
        if (bd_wm_server_thumbnail(wm, id, probe, 8, 8)) return true;
        bd_session_step(s, 5);
        usleep(1000);
    }
    return bd_wm_server_thumbnail(wm, id, probe, 8, 8);
}

static bool info_by_title(bd_session *s, const char *title,
                          bd_wm_win_info *out)
{
    bd_wm_server *wm = bd_session_wm(s);
    size_t n = bd_wm_server_count(wm);
    for (size_t i = 0; i < n; i++) {
        if (bd_wm_server_info(wm, i, out) &&
            strcmp(out->title, title) == 0)
            return true;
    }
    return false;
}

static void tap_at(bd_session *s, int x, int y)
{
    tk_event d = {0};
    d.type = TK_EV_POINTER_DOWN;
    d.x = x;
    d.y = y;
    (void)bd_session_event(s, &d);
    tk_event u = {0};
    u.type = TK_EV_POINTER_UP;
    u.x = x;
    u.y = y;
    (void)bd_session_event(s, &u);
    settle(s, 200);
}

static tk_color px(bd_session *s, int x, int y)
{
    return tk_canvas_pixel(bd_session_canvas(s), x, y);
}

static bool same_px(tk_color a, tk_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static pid_t spawn_app(const char *path, const char *wm_path,
                       const char *home)
{
    pid_t pid = fork();
    if (pid == 0) {
        setenv("BD_WM_SOCK", wm_path, 1);
        setenv("HOME", home, 1);
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

static bd_session *make_session(de_catalog *cat, ln_store *store,
                                  const char *wm_path)
{
    bd_session_config cfg = {0};
    cfg.width = 854;
    cfg.height = 480;
    cfg.catalog = cat;
    cfg.store = store;
    cfg.exec_fn = mock_exec;
    cfg.wm_path = wm_path;
    cfg.fb = tk_fb_open_mem(854, 480, 16);
    cfg.input_virtual = tk_evdev_new_virtual();
    cfg.backend = bd_backend_mock_new();
    bd_backend_mock_set(cfg.backend, 50, 40, 80, false);
    cfg.wifi = bd_wifi_new("/nonexistent/wpa/wlan0");
    return bd_session_new(&cfg);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("test_session_apps: SKIP (app paths not given)\n");
        return 0;
    }
    const char *calc_path = argv[1];
    const char *notes_path = argv[2];

    setup_reg();
    char home[] = "/tmp/bdsessapps-homeXXXXXX";
    CHECK(mkdtemp(home) != NULL);

    de_catalog *cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "tests/fixtures/applications");
    ln_store *store = ln_store_new();

    /* ================= Android mode ================= */
    char wmdir[] = "/tmp/bdsessapps-wmXXXXXX";
    CHECK(mkdtemp(wmdir) != NULL);
    char wmpath[256];
    snprintf(wmpath, sizeof(wmpath), "%s/wm.sock", wmdir);

    /* the product entry installs the reg-driven theme before
     * building the shell (braindesk2.c); the session deliberately
     * does not, so the test does it the same way */
    tk_theme_set_default(bd_theme_from_reg());
    bd_session *s = make_session(cat, store, wmpath);
    CHECK(s != NULL);
    if (!s) return 1;
    bd_shell *sh = bd_session_shell(s);
    bd_wm_server *wm = bd_session_wm(s);
    settle(s, 600);

    /* bar reference pixels on the bare desktop */
    tk_widget *top = bd_shell_top(sh);
    tk_widget *recents_btn = find_btn(top, "最近使ったアプリ");
    tk_widget *home_btn = find_btn(top, "ホーム");
    CHECK(recents_btn != NULL && home_btn != NULL);
    if (!recents_btn || !home_btn) {
        printf("test_session_apps: %d passed, %d failed\n",
               passes, fails);
        return 1;
    }
    tk_rect rr = tk_widget_rect(recents_btn);
    tk_rect hr = tk_widget_rect(home_btn);
    /* int10: the nav glyphs are outlines (hollow centres),
     * so a centre sample reads whatever is BEHIND the bar:
     * the wallpaper on the desktop, but the shell's band
     * cover (theme surface) while a window is open - the
     * cover is the user-confirmed design (no near-black
     * band under a bright window). The bar invariant is
     * therefore sampled app-front vs app-front below;
     * ref_recents/ref_home are captured once calc is up. */
    tk_color ref_recents = { 0, 0, 0, 0 };
    tk_color ref_home = { 0, 0, 0, 0 };
    tk_color ref_edge = px(s, 10, rr.y + rr.h / 2);
    tk_rect mr = bd_shell_main_rect(sh);
    CHECK(mr.w == 854 && mr.y + mr.h < 480); /* work area < panel */

    /* --- app 1: calc --- */
    pid_t calc_pid = spawn_app(calc_path, wmpath, home);
    CHECK(wait_windows(s, 1, 4000));
    settle(s, 600);
    bd_wm_win_info calc, notes;
    CHECK(info_by_title(s, "電卓", &calc));
    CHECK(calc.state == BD_WIN_MAXIMIZED);
    CHECK(calc.frame.x == 0 && calc.frame.y == 0 &&
          calc.frame.w == mr.w && calc.frame.h == mr.h);
    CHECK(bd_wm_server_focused(wm) == calc.id);
    /* the app really painted (a committed, non-flat frame) */
    CHECK(wait_committed(s, calc.id, 4000));
    {
        static uint32_t thumb[60 * 40];
        CHECK(bd_wm_server_thumbnail(wm, calc.id, thumb, 60, 40));
        int distinct = 0;
        for (int i = 0; i < 60 * 40 && distinct < 5; i++) {
            bool seen = false;
            for (int j = 0; j < i; j++)
                if (thumb[j] == thumb[i]) { seen = true; break; }
            if (!seen) distinct++;
        }
        CHECK(distinct >= 4);
    }
    /* bar unchanged with an app in front: capture the
     * app-front reference here, compare the next app
     * against it */
    ref_recents = px(s, rr.x + rr.w / 2, rr.y + rr.h / 2);
    ref_home = px(s, hr.x + hr.w / 2, hr.y + hr.h / 2);
    CHECK(same_px(px(s, 10, rr.y + rr.h / 2), ref_edge));

    /* --- app 2: notes; calc must survive, unfocused, not
     * minimized --- */
    pid_t notes_pid = spawn_app(notes_path, wmpath, home);
    CHECK(wait_windows(s, 2, 4000));
    settle(s, 600);
    CHECK(info_by_title(s, "メモ", &notes));
    CHECK(wait_committed(s, notes.id, 4000));
    CHECK(notes.state == BD_WIN_MAXIMIZED);
    CHECK(notes.frame.x == 0 && notes.frame.y == 0 &&
          notes.frame.w == mr.w && notes.frame.h == mr.h);
    CHECK(bd_wm_server_focused(wm) == notes.id);
    CHECK(info_by_title(s, "電卓", &calc));
    CHECK(calc.state == BD_WIN_MAXIMIZED);
    CHECK(same_px(px(s, rr.x + rr.w / 2, rr.y + rr.h / 2), ref_recents));
    CHECK(same_px(px(s, 10, rr.y + rr.h / 2), ref_edge));

    /* --- switch from the bar: tap recents, tap calc's card --- */
    tap_at(s, rr.x + rr.w / 2, rr.y + rr.h / 2);
    CHECK(bd_shell_recents_open(sh));
    tk_widget *card_lbl = find_lbl(bd_shell_top(sh), "電卓");
    CHECK(card_lbl != NULL);
    if (card_lbl) {
        tk_widget *card = tk_parent(card_lbl);
        tk_widget *thumb_btn = card ? tk_child_at(card, 0) : NULL;
        CHECK(thumb_btn != NULL);
        if (thumb_btn) {
            tk_rect tr = tk_widget_rect(thumb_btn);
            tap_at(s, tr.x + tr.w / 2, tr.y + tr.h / 2);
        }
    }
    CHECK(!bd_shell_recents_open(sh));
    CHECK(bd_wm_server_focused(wm) == calc.id);

    /* --- home from the bar: desktop view, windows survive --- */
    tap_at(s, hr.x + hr.w / 2, hr.y + hr.h / 2);
    CHECK(bd_shell_view(sh) == BD_VIEW_DESKTOP);
    CHECK(bd_wm_server_count(wm) == 2);

    kill(calc_pid, SIGTERM);
    kill(notes_pid, SIGTERM);
    waitpid(calc_pid, NULL, 0);
    waitpid(notes_pid, NULL, 0);
    bd_session_free(s);

    /* ================= WinUI mode ================= */
    {
        char regf[320];
        snprintf(regf, sizeof(regf), "%s/10-ui.conf", reg_userdir);
        FILE *f = fopen(regf, "w");
        CHECK(f != NULL);
        if (f) { fputs("[ui]\nmode = winui\n", f); fclose(f); }
    }
    char wmdir2[] = "/tmp/bdsessapps-wm2XXXXXX";
    CHECK(mkdtemp(wmdir2) != NULL);
    char wmpath2[256];
    snprintf(wmpath2, sizeof(wmpath2), "%s/wm.sock", wmdir2);
    tk_theme_set_default(bd_theme_from_reg()); /* now winui */
    bd_session *s2 = make_session(cat, store, wmpath2);
    CHECK(s2 != NULL);
    if (s2) {
        bd_shell *sh2 = bd_session_shell(s2);
        settle(s2, 600);
        tk_widget *start_btn = find_btn(bd_shell_top(sh2), "アプリ");
        CHECK(start_btn != NULL);
        if (!start_btn) {
            printf("test_session_apps: %d passed, %d failed\n",
                   passes, fails);
            return 1;
        }
        tk_rect sr = tk_widget_rect(start_btn);
        tk_color ref_start = px(s2, sr.x + sr.w / 2, sr.y + sr.h / 2);
        tk_rect mr2 = bd_shell_main_rect(sh2);
        CHECK(mr2.y + mr2.h < 480);

        pid_t c2 = spawn_app(calc_path, wmpath2, home);
        CHECK(wait_windows(s2, 1, 4000));
        settle(s2, 600);
        bd_wm_win_info ci;
        CHECK(info_by_title(s2, "電卓", &ci));
        CHECK(ci.state == BD_WIN_MAXIMIZED);
        CHECK(ci.frame.y + ci.frame.h <= mr2.y + mr2.h);
        /* taskbar unchanged with the app in front */
        CHECK(same_px(px(s2, sr.x + sr.w / 2, sr.y + sr.h / 2),
                      ref_start));
        kill(c2, SIGTERM);
        waitpid(c2, NULL, 0);
        bd_session_free(s2);
    }

    ln_store_free(store);
    de_catalog_free(cat);
    printf("test_session_apps: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
