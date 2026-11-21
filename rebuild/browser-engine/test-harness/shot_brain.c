/* shot_brain - screenshot harness for the brain frontend.
 *
 * Runs the REAL WM server (shell/src/wm.c) in-process, launches
 * the REAL nsbrain binary as a WM client, pumps the server until
 * the client's frames settle, composites the server onto a memory
 * canvas (the same composite path the shell uses) and dumps raw
 * pixels in the shot_common format for convert.py.
 *
 * Scenarios:
 *   1 <url>            single tab showing <url>
 *   2 <url>            same, after clicking the toolbar "+" (new
 *                      tab) button and letting the second tab load
 *   3 <url>            same as 1, then clicks the menu button and
 *                      captures the open menu
 *   4 <url>            same as 1, then clicks the big link block
 *
 * Chrome coordinates below follow the redesigned chrome (report
 * section 9): with one tab there is no tab strip; the toolbar is
 * the top 56px of the client area, "+" is the second button from
 * the right (centre x = w-84), the menu button is the rightmost
 * (centre x = w-28), both centred at y = 28.
 *
 * Also prints the child PSS (from /proc/<pid>/smaps_rollup) while
 * the page is up, for the budget check.
 *
 * usage: shot_brain <scenario> <nsbrain> <url> <out.raw> <sock>
 *        <home-dir>
 * env:   SHOT_WAIT_MS (settle time, default 4000)
 */
#include "wm.h"
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int dump_raw(const char *path, tk_canvas *cv)
{
    int w = tk_canvas_width(cv), h = tk_canvas_height(cv);
    uint32_t *px = tk_canvas_pixels(cv);
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) { perror(path); return -1; }
    {
        uint32_t hdr[2] = { (uint32_t)w, (uint32_t)h };
        fwrite(hdr, 4, 2, f);
    }
    for (i = 0; i < w * h; i++) {
        uint8_t rgb[3] = { (uint8_t)(px[i] >> 16),
                           (uint8_t)(px[i] >> 8),
                           (uint8_t)px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("shot: %s (%dx%d)\n", path, w, h);
    return 0;
}

static long child_pss_kb(pid_t pid)
{
    char path[64];
    char line[256];
    FILE *f;
    long pss = -1;
    snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", (int)pid);
    f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "Pss: %ld kB", &pss) == 1) break;
    }
    fclose(f);
    return pss;
}

/* pump the server for ms milliseconds */
static void pump(bd_wm_server *srv, int ms)
{
    long end = now_ms() + ms;
    while (now_ms() < end) {
        bd_wm_server_poll(srv, 20);
    }
}

/* find the (single) client window's content rect */
static bool win_content(bd_wm_server *srv, tk_rect *out)
{
    bd_wm_win_info info;
    if (bd_wm_server_count(srv) == 0) return false;
    if (!bd_wm_server_info(srv, 0, &info)) return false;
    *out = info.content;
    return true;
}

static void click(bd_wm_server *srv, int x, int y)
{
    bd_wm_server_pointer(srv, BD_WM_EV_POINTER_DOWN, x, y);
    pump(srv, 120);
    bd_wm_server_pointer(srv, BD_WM_EV_POINTER_UP, x, y);
    pump(srv, 120);
}

int main(int argc, char **argv)
{
    int scenario;
    const char *nsbrain, *url, *outpath, *sock, *home;
    bd_wm_server *srv;
    pid_t pid;
    tk_canvas *cv;
    int wait_ms = 4000;
    const char *w;

    if (argc < 7) {
        fprintf(stderr, "usage: shot_brain <scenario> <nsbrain> "
                "<url> <out.raw> <sock> <home>\n");
        return 2;
    }
    scenario = atoi(argv[1]);
    nsbrain = argv[2];
    url = argv[3];
    outpath = argv[4];
    sock = argv[5];
    home = argv[6];
    w = getenv("SHOT_WAIT_MS");
    if (w) wait_ms = atoi(w);

    tk_theme_set_default(&tk_theme_v2);
    {
        tk_font *font = tk_font_load_default();
        if (font) tk_font_install(font);
    }

    srv = bd_wm_server_start(sock);
    if (!srv) { fprintf(stderr, "server start failed\n"); return 1; }
    bd_wm_server_set_screen(srv, (tk_rect){ 0, 0, 800, 480 });
    bd_wm_server_set_workarea(srv, (tk_rect){ 0, 0, 800, 480 });

    pid = fork();
    if (pid == 0) {
        setenv("HOME", home, 1);
        setenv("BD_WM_SOCK", sock, 1);
        setenv("BRAIN_MODE", "wm", 1);
        execl(nsbrain, nsbrain, url, (char *)NULL);
        perror("execl nsbrain");
        _exit(127);
    }
    if (pid < 0) { perror("fork"); return 1; }

    /* let the client connect, load and paint */
    pump(srv, wait_ms);
    printf("windows: %zu, proto errors: %u, child PSS: %ld kB\n",
           bd_wm_server_count(srv), bd_wm_server_proto_errors(srv),
           child_pss_kb(pid));
    {
        tk_rect content;
        if (win_content(srv, &content)) {
            printf("client content rect: (%d,%d) %dx%d\n",
                   content.x, content.y, content.w, content.h);
        }
    }

    if (scenario == 4) {
        tk_rect content;
        if (win_content(srv, &content)) {
            /* the big link block on page1: the page starts
             * at content y+56 (toolbar only, one tab) and the
             * block sits at page y ~130..240 at the default
             * 125% scale */
            click(srv, content.x + 400, content.y + 240);
            pump(srv, wait_ms);
            printf("after link click: PSS: %ld kB\n",
                   child_pss_kb(pid));
        } else {
            fprintf(stderr, "no client window found\n");
        }
    }
    if (scenario == 2 || scenario == 3) {
        tk_rect content;
        if (win_content(srv, &content)) {
            if (scenario == 2) {
                /* "+" button: toolbar, second from the right
                 * (content coords ~ (w-84, 28)) */
                click(srv, content.x + content.w - 84,
                      content.y + 28);
                pump(srv, wait_ms);
                printf("after new tab: windows: %zu, PSS: %ld kB\n",
                       bd_wm_server_count(srv), child_pss_kb(pid));
            } else {
                /* menu button: right end of the toolbar
                 * (content coords ~ (w-28, 28)) */
                click(srv, content.x + content.w - 28,
                      content.y + 28);
                pump(srv, 900);
            }
        } else {
            fprintf(stderr, "no client window found\n");
        }
    }

    cv = tk_canvas_new(800, 480);
    tk_canvas_clear(cv, (tk_color){ 32, 32, 40, 255 });
    bd_wm_composite_full(srv, cv, tk_theme_default());
    dump_raw(outpath, cv);
    tk_canvas_free(cv);

    kill(pid, SIGTERM);
    pump(srv, 200);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    bd_wm_server_free(srv);
    return 0;
}
