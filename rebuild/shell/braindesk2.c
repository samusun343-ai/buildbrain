/* braindesk2 - the shell v2 session executable (M3).
 *
 * Wires the real facilities into bd_session (src/session.c):
 * fbdev display, evdev input, sysfs/ALSA backend, wpa_supplicant
 * Wi-Fi, the .desktop catalog from the standard application
 * directories, and ln_exec_real for launching. The session script
 * (integration/brainux-session.sh) runs braingreeter2 first and
 * then this, as the logged-in user.
 *
 * Usage:
 *   braindesk2            GUI session on /dev/fb0 + /dev/input/event0
 *   braindesk2 --check    headless smoke: build the session on a
 *                         memory framebuffer, run a few frames,
 *                         print the first frame's transfer, exit 0
 *
 * Application directories: $BD_APPLICATIONS_DIRS (colon list)
 * overrides the defaults (/usr/share/applications, then
 * ~/.local/share/applications). All writes stay inside the
 * Brainux user area (brainreg user layer); the eMMC is never
 * touched (DESIGN section 12.3).
 */
#include "session.h"
#include "thememode.h"
#include "launcher.h"
#include "brainime.h"
#include "osk_brainime.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop_flag;
static void on_stop(int sig) { (void)sig; stop_flag = 1; }

/* IME engine for the shell OSK (owned here; the OSK's sessions
 * borrow it). NULL when no dictionary is installed - the OSK
 * then keeps the toolkit's built-in kana fallback and is still
 * fully usable for direct/kana input. */
static bi_engine *osk_engine;

static void close_osk_engine(void)
{
    if (osk_engine) bi_engine_close(osk_engine);
    osk_engine = NULL;
}

static void load_catalog(de_catalog *cat)
{
    const char *env = getenv("BD_APPLICATIONS_DIRS");
    if (env && *env) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", env);
        for (char *p = strtok(buf, ":"); p; p = strtok(NULL, ":"))
            de_catalog_load_dir(cat, p);
        return;
    }
    de_catalog_load_dir(cat, "/usr/share/applications");
    const char *home = getenv("HOME");
    if (home && *home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.local/share/applications", home);
        de_catalog_load_dir(cat, path);
    }
}

static bd_session *build_session(tk_fb *fb, tk_evdev *input,
                                 tk_evdev *vinput, bd_backend *be,
                                 bd_wifi *wifi, const char *wm_path,
                                 de_catalog **cat_out, ln_store **store_out)
{
    de_catalog *cat = de_catalog_new("ja");
    load_catalog(cat);
    ln_store *store = ln_store_new();
    bd_session_config cfg = {0};
    cfg.width = 854; /* M22: the panel is 854x480 (AUDIT SS20) */
    cfg.height = 480;
    cfg.catalog = cat;
    cfg.store = store;
    cfg.exec_fn = ln_exec_real;
    cfg.wm_path = wm_path;
    /* bd-alarmd helper: $BD_ALARMD_PATH overrides the session's
     * default (/usr/local/bin/bd-alarmd); set it empty to run the
     * session without the resident alarm evaluator. */
    cfg.alarmd_path = getenv("BD_ALARMD_PATH");
    cfg.fb = fb;
    cfg.input = input;
    cfg.input_virtual = vinput;
    cfg.backend = be;
    cfg.wifi = wifi;
    bd_session *s = bd_session_new(&cfg);
    if (!s) {
        de_catalog_free(cat);
        ln_store_free(store);
        return NULL;
    }
    /* Wire the real conversion engine into the shell OSK.
     * Dictionary: $BRAINUX_IME_DICT, else the installed path. */
    const char *dict = getenv("BRAINUX_IME_DICT");
    if (!dict) dict = "/usr/share/brainux/ime/brainux3.bid3";
    osk_engine = bi_engine_open(dict);
    if (osk_engine)
        bd_shell_osk_set_backend(bd_session_shell(s),
                                 osk_brainime_backend(), osk_engine);
    *cat_out = cat;
    *store_out = store;
    return s;
}

static int check_main(void)
{
    de_catalog *cat = NULL;
    ln_store *store = NULL;
    tk_fb *fb = tk_fb_open_mem(854, 480, 16);
    bd_session *s = build_session(fb, NULL, NULL, NULL, NULL, NULL,
                                  &cat, &store);
    if (!s) {
        fprintf(stderr, "braindesk2 --check: session build failed\n");
        return 1;
    }
    size_t first = bd_session_step(s, 10);
    size_t quiet = bd_session_step(s, 10);
    printf("braindesk2 --check: ok (first frame %zu bytes, "
           "quiet frame %zu bytes)\n", first, quiet);
    bd_session_free(s);
    close_osk_engine();
    de_catalog_free(cat);
    ln_store_free(store);
    tk_fb_close(fb);
    return 0;
}

int main(int argc, char **argv)
{
    /* display mode from brainreg (ui/mode; thememode.h) */
    tk_theme_set_default(bd_theme_from_reg());
    if (argc >= 2 && strcmp(argv[1], "--check") == 0) return check_main();

    tk_fb *fb = tk_fb_open("/dev/fb0");
    tk_evdev *input = tk_evdev_open(NULL); /* /dev/input/event0 */
    if (!fb || !input) {
        fprintf(stderr,
                "braindesk2: no framebuffer or input device; "
                "refusing to idle headless (use --check)\n");
        if (input) tk_evdev_close(input);
        if (fb) tk_fb_close(fb);
        return 3;
    }
    bd_backend *be = bd_backend_sysfs_new();
    bd_wifi *wifi = bd_wifi_new(NULL);
    de_catalog *cat = NULL;
    ln_store *store = NULL;
    bd_session *s = build_session(fb, input, NULL, be, wifi, NULL,
                                  &cat, &store);
    if (!s) {
        fprintf(stderr, "braindesk2: session build failed\n");
        return 1;
    }
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    while (!stop_flag) {
        /* Toolkit M5: while an animation is running (switch
         * knobs, QS/launcher surface slides), step at ~60 Hz so
         * the 150-200 ms tokens read as motion; idle steps keep
         * the power-friendly 50 ms cadence. */
        tk_root *root = bd_shell_root(bd_session_shell(s));
        int step = tk_root_anim_active(root) ? 16 : 50;
        /* A playing mascot clip (24-30 fps bakes) needs a
         * finer cadence than the idle 50 ms step, or poses
         * advance in clumps and the motion reads as jerky. */
        if (step > 33 && bd_mascot_animating(bd_session_mascot(s)))
            step = 33;
        (void)bd_session_step(s, step);
    }
    bd_session_free(s);
    close_osk_engine();
    de_catalog_free(cat);
    ln_store_free(store);
    bd_wifi_free(wifi);
    bd_backend_free(be);
    tk_evdev_close(input);
    tk_fb_close(fb);
    return 0;
}
