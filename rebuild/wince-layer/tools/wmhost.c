/* wmhost -- host-side acceptance harness for BrainCE M2 (GWE).
 *
 * Runs a real braindesk WM server in this process, launches the
 * ce-loader (under qemu-arm) as a child with BD_WM_SOCK pointing at
 * the server, and waits for the CE GUI test app to appear as a
 * normal Brainux window. The app runs in GHOLD mode: it writes its
 * message log at timer 8 and keeps the window open, so the harness
 * can composite the server surface and verify the painted scene
 * pixel by pixel against the deterministic ceguitest scene:
 *
 *   blue band (200,20)   = (0,0,255)      white bg (200,270)
 *   red block (60,100)   = (255,0,0)      yellow   (325,75)
 *   green rect (100,190) = (0,200,0)      magenta  (390,290)
 *   cyan pixel (395,295) = (0,255,255)    black text pixels > 30
 *                                         in (10..200, 228..262)
 *
 * Usage: wmhost SOCKPATH GUILOG_PATH COMMAND [ARGS...]
 * The child is killed after the checks. Exit 0 = all checks pass.
 */
#define _GNU_SOURCE
#include "wm.h"
#include "tk.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("ok: "); printf(__VA_ARGS__); printf("\n"); } \
    else { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static bd_wm_server *srv;

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static int find_window(const char *title, bd_wm_win_info *out)
{
    size_t n = bd_wm_server_count(srv);
    for (size_t i = 0; i < n; i++) {
        if (bd_wm_server_info(srv, i, out)
            && strcmp(out->title, title) == 0)
            return 1;
    }
    return 0;
}

static int px_eq(tk_canvas *cv, int x, int y, int r, int g, int b)
{
    tk_color c = tk_canvas_pixel(cv, x, y);
    return c.r == r && c.g == g && c.b == b;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: wmhost SOCKPATH GUILOG COMMAND [ARGS...]\n");
        return 2;
    }
    const char *sockpath = argv[1];
    const char *guilog = argv[2];

    tk_font *f = tk_font_load_bundled();
    if (f)
        tk_font_install(f);

    unlink(sockpath);
    srv = bd_wm_server_start(sockpath);
    if (!srv) {
        fprintf(stderr, "wmhost: server start failed\n");
        return 2;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setenv("BD_WM_SOCK", sockpath, 1);
        execvp(argv[3], &argv[3]);
        _exit(127);
    }

    /* Wait for the window, then for the app's own log file (written
     * at timer 8 in GHOLD mode, window stays open). */
    bd_wm_win_info info;
    int have_win = 0, have_log = 0;
    for (int t = 0; t < 600 && !(have_win && have_log); t++) {
        bd_wm_server_poll(srv, 50);
        if (!have_win && find_window("BrainCE M2 Test", &info))
            have_win = 1;
        if (!have_log && file_exists(guilog))
            have_log = 1;
        int st;
        if (waitpid(pid, &st, WNOHANG) == pid) {
            printf("FAIL: child exited early (status %d)\n", st);
            bd_wm_server_free(srv);
            return 1;
        }
    }
    CHECK(have_win, "CE window appeared as a Brainux window");
    CHECK(have_log, "guilog.txt written by the app (message loop ran)");
    if (!have_win) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        bd_wm_server_free(srv);
        printf("WMHOST FAIL (%d passed, %d failed)\n", passes, fails);
        return 1;
    }
    find_window("BrainCE M2 Test", &info);
    CHECK(info.content.w == 400 && info.content.h == 300,
          "content rect is 400x300 (got %dx%d)",
          info.content.w, info.content.h);

    /* let the final paints land, then composite */
    for (int t = 0; t < 20; t++)
        bd_wm_server_poll(srv, 50);
    tk_canvas *cv = tk_canvas_new(854, 480);
    tk_canvas_clear(cv, (tk_color){ 10, 20, 30, 255 });
    bd_wm_composite_full(srv, cv, tk_theme_default());

    int ox = info.content.x, oy = info.content.y;
    CHECK(px_eq(cv, ox + 200, oy + 20, 0, 0, 255), "blue band pixel");
    CHECK(px_eq(cv, ox + 60, oy + 100, 255, 0, 0), "red block pixel");
    CHECK(px_eq(cv, ox + 100, oy + 190, 0, 200, 0), "green rect pixel");
    CHECK(px_eq(cv, ox + 325, oy + 75, 255, 255, 0),
          "yellow BitBlt pixel");
    CHECK(px_eq(cv, ox + 390, oy + 290, 255, 0, 255),
          "magenta SetPixel pixel");
    CHECK(px_eq(cv, ox + 395, oy + 295, 0, 255, 255),
          "cyan GetDC/SetPixel pixel");
    CHECK(px_eq(cv, ox + 200, oy + 270, 255, 255, 255),
          "white background pixel");
    int black = 0;
    for (int y = 228; y < 262; y++)
        for (int x = 10; x < 200; x++) {
            tk_color c = tk_canvas_pixel(cv, ox + x, oy + y);
            if (c.r < 60 && c.g < 60 && c.b < 60)
                black++;
        }
    CHECK(black > 30, "text 'BrainCE M2' painted (%d black pixels)",
          black);

    kill(pid, SIGTERM);
    int st = 0;
    for (int t = 0; t < 40; t++) {
        bd_wm_server_poll(srv, 50);
        if (waitpid(pid, &st, WNOHANG) == pid)
            break;
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);

    tk_canvas_free(cv);
    bd_wm_server_free(srv);
    printf("WMHOST %s (%d passed, %d failed)\n",
           fails ? "FAIL" : "PASS", passes, fails);
    return fails ? 1 : 0;
}
