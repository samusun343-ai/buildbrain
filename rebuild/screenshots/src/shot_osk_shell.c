/* shot_osk_shell - the system-wide OSK over real WM windows.
 *
 * Runs a full bd_session (shell + WM + compositor on a memory
 * framebuffer, the product's own session code) with the real
 * libbrainime engine wired into the shell OSK, and a real WM
 * client (shell/tests/im_client.c: a toolkit window with a text
 * field) as a child process. Tapping the client's field raises
 * the OSK above the window; the shots are the session canvas
 * itself, dumped after the compositor ran:
 *   v2_27_osk_window.raw      normal window, field focused,
 *                             composition in flight
 *   v2_28_osk_fullscreen.raw  the same window fullscreen: the
 *                             client area ends above the strip
 * No drawing code here: every pixel comes from product code
 * (shell, WM client toolkit, OSK, IME candidate bar).
 *
 * Run from rebuild/ (relative paths: catalog fixture, dict,
 * shell/im_client):
 *   make -C screenshots shot_osk_shell && ./screenshots/shot_osk_shell
 *   python3 screenshots/convert.py
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "session.h"
#include "shellui.h"
#include "wm.h"
#include "osk.h"
#include "brainime.h"
#include "osk_brainime.h"
#include "shot_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

static char child_out[16384];
static size_t child_out_len;
static int child_fd = -1;

static void drain_child(void)
{
    char buf[1024];
    ssize_t n;
    while ((n = read(child_fd, buf, sizeof(buf))) > 0) {
        size_t room = sizeof(child_out) - 1 - child_out_len;
        if ((size_t)n > room) n = (ssize_t)room;
        if (n <= 0) break;
        memcpy(child_out + child_out_len, buf, (size_t)n);
        child_out_len += (size_t)n;
        child_out[child_out_len] = 0;
    }
}

/* Step the session until `needle` appears in the child output
 * or the step budget runs out. Returns success. */
static bool pump_until(bd_session *s, const char *needle, int max_steps)
{
    for (int i = 0; i < max_steps; i++) {
        (void)bd_session_step(s, 5);
        drain_child();
        if (strstr(child_out, needle)) return true;
    }
    return strstr(child_out, needle) != NULL;
}

static bool last_quad(const char *tag, int *a, int *b, int *c, int *d)
{
    const char *p = child_out, *hit = NULL;
    size_t tl = strlen(tag);
    while ((p = strstr(p, tag)) != NULL) { hit = p; p += tl; }
    if (!hit) return false;
    return sscanf(hit + tl, "%d %d %d %d", a, b, c, d) == 4;
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
}

int main(void)
{
    tk_theme_set_default(&tk_theme_v2);

    /* brainreg into a temp dir (same pattern as the shell tests) */
    char tmpl[] = "/tmp/shot-osk-XXXXXX";
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 2; }
    char sysdir[512], userdir[512];
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);

    de_catalog *cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications");
    ln_store *store = ln_store_new();

    char wmdir[] = "/tmp/shot-osk-wmXXXXXX";
    if (!mkdtemp(wmdir)) return 2;
    char wmpath[256];
    snprintf(wmpath, sizeof(wmpath), "%s/wm.sock", wmdir);

    tk_fb *fb = tk_fb_open_mem(800, 480, 16);
    tk_evdev *vinput = tk_evdev_new_virtual();
    bd_backend *be = bd_backend_mock_new();
    bd_backend_mock_set(be, 50, 40, 80, false);
    bd_wifi *wifi = bd_wifi_new("/nonexistent/wpa/wlan0");

    bd_session_config cfg = {0};
    cfg.width = 800;
    cfg.height = 480;
    cfg.catalog = cat;
    cfg.store = store;
    cfg.exec_fn = mock_exec;
    cfg.wm_path = wmpath;
    cfg.fb = fb;
    cfg.input_virtual = vinput;
    cfg.backend = be;
    cfg.wifi = wifi;
    bd_session *s = bd_session_new(&cfg);
    if (!s) { fprintf(stderr, "session failed\n"); return 3; }
    bd_shell *sh = bd_session_shell(s);
    bd_wm_server *wm = bd_session_wm(s);

    /* the real IME engine behind the shell OSK */
    bi_engine *eng = bi_engine_open("libbrainime/data/brainux3.bid3");
    if (eng) bd_shell_osk_set_backend(sh, osk_brainime_backend(), eng);
    else fprintf(stderr, "shot: dict not found, kana fallback\n");

    for (int i = 0; i < 40; i++) (void)bd_session_step(s, 5);

    /* spawn the IM client */
    int pfd[2];
    if (pipe(pfd) != 0) return 3;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[0]);
        close(pfd[1]);
        execl("./shell/im_client", "im_client", wmpath, "400", "300",
              (char *)NULL);
        _exit(127);
    }
    close(pfd[1]);
    child_fd = pfd[0];
    fcntl(child_fd, F_SETFL, O_NONBLOCK);

    if (!pump_until(s, "BTNRECT", 400)) {
        fprintf(stderr, "shot: client did not come up\n");
        return 4;
    }
    uint32_t imid = bd_wm_server_focused(wm);
    bd_wm_win_info im;
    if (!bd_wm_server_info_id(wm, imid, &im)) return 4;
    int fx = 0, fy = 0, fw = 0, fh = 0;
    if (!last_quad("FIELD ", &fx, &fy, &fw, &fh)) return 4;

    /* focus the field: the OSK rises above the window */
    tap_at(s, im.content.x + fx + fw / 2, im.content.y + fy + fh / 2);
    bool up = false;
    for (int i = 0; i < 900 && !up; i++) {
        (void)bd_session_step(s, 5);
        drain_child();
        up = bd_shell_osk_open(sh);
    }
    if (!up) { fprintf(stderr, "shot: OSK did not open\n"); return 5; }

    /* type k-a-n-j-i on the OSK: the candidate bar and the
     * field's preedit show the conversion in flight */
    tk_widget *oskw = bd_shell_osk_widget(sh);
    osk_cell cells[128];
    int nc = tk_osk_layout(oskw, cells, 128);
    const char *seq = "kanji";
    for (const char *p = seq; *p; p++) {
        for (int i = 0; i < nc; i++) {
            if (cells[i].kind == OSK_CELL_LETTER &&
                cells[i].text[0] == *p && cells[i].text[1] == 0) {
                tap_at(s, cells[i].r.x + cells[i].r.w / 2,
                       cells[i].r.y + cells[i].r.h / 2);
                break;
            }
        }
        for (int i = 0; i < 30; i++) (void)bd_session_step(s, 5);
        drain_child();
    }
    for (int i = 0; i < 20; i++) (void)bd_session_step(s, 5);
    shot_dump("screenshots/raw/v2_27_osk_window.raw",
              bd_session_canvas(s));

    /* fullscreen: the client area must end above the strip */
    if (!bd_wm_server_info_id(wm, imid, &im)) return 4;
    (void)bd_wm_server_set_state(wm, imid, BD_WIN_FULLSCREEN);
    (void)pump_until(s, "RESIZED 800 208", 400);
    for (int i = 0; i < 400; i++) {
        (void)bd_session_step(s, 5);
        if (bd_wm_server_info_id(wm, imid, &im) &&
            im.content.w == 800 && im.content.h == 480 - 272)
            break;
    }
    for (int i = 0; i < 10; i++) (void)bd_session_step(s, 5);
    shot_dump("screenshots/raw/v2_28_osk_fullscreen.raw",
              bd_session_canvas(s));

    (void)bd_wm_server_close(wm, imid);
    (void)pump_until(s, "CLOSED", 200);
    int st = 0;
    (void)waitpid(pid, &st, 0);
    bd_session_free(s);
    if (eng) bi_engine_close(eng);
    return 0;
}
