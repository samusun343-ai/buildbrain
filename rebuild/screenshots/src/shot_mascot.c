/* shot_mascot - screenshots of the desktop mascot running in
 * the real braindesk2 session (shell/src/session.c): the pack
 * is enabled through brainreg ([mascot] pack/size), the session
 * steps real time, and the session canvas is dumped.
 *
 * Usage: shot_mascot <pack.bdm> <tag> [size] [num] [mode]
 *   writes screenshots/raw/v2_<num>_mascot_<tag>_idle.raw and
 *          screenshots/raw/v2_<num+1>_mascot_<tag>_action.raw
 * Defaults reproduce the original shots (size 208, num 29,
 * mode walk). Mode "walk" waits for the WALK state for the
 * action shot; mode "tap" (for packs without Move clips,
 * e.g. the Live2D bakes) taps the mascot and shoots mid
 * reaction clip.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "session.h"
#include "mascot.h"
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "shot_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

static long mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <pack.bdm> <tag> [size] "
                        "[num] [mode]\n", argv[0]);
        return 2;
    }
    const char *pack = argv[1];
    const char *tag = argv[2];
    const char *size = argc > 3 ? argv[3] : "208";
    int num = argc > 4 ? atoi(argv[4]) : 29;
    const char *mode = argc > 5 ? argv[5] : "walk";
    tk_theme_set_default(&tk_theme_v2);

    char tmpl[] = "/tmp/shot-mascot-XXXXXX";
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 2; }
    char sysdir[512], userdir[512], wmpath[512];
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    snprintf(wmpath, sizeof(wmpath), "%s/wm.sock", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);

    reg_set("shell", "wallpaper", "#102030");
    reg_set("shell", "volume", "40");
    reg_set("mascot", "enabled", "1");
    reg_set("mascot", "pack", pack);
    reg_set("mascot", "size", size);

    de_catalog *cat = de_catalog_new("ja");
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications");
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications2");
    de_catalog_load_dir(cat, "apps/desktop");
    de_catalog_load_dir(cat, "apps/files");
    de_catalog_load_dir(cat, "apps/notes");
    ln_store *store = ln_store_new();
    ln_pin(store, "bd-files.desktop");
    ln_pin(store, "bd-notes.desktop");
    ln_pin(store, "bd-calc.desktop");
    ln_pin(store, "bd-player.desktop");
    ln_pin(store, "netsurf.desktop");
    ln_pin(store, "bd-settings.desktop");

    bd_session_config cfg = { 0 };
    cfg.width = 800;
    cfg.height = 480;
    cfg.catalog = cat;
    cfg.store = store;
    cfg.exec_fn = mock_exec;
    cfg.wm_path = wmpath;
    bd_session *s = bd_session_new(&cfg);
    if (!s) { fprintf(stderr, "session failed\n"); return 1; }
    bd_mascot *m = bd_session_mascot(s);
    if (!m) {
        fprintf(stderr, "mascot not created (pack %s)\n", pack);
        return 1;
    }
    bd_shell_set_clock(bd_session_shell(s), "09:41");
    bd_shell_set_battery(bd_session_shell(s), 72, false);

    char path[256];
    /* fall + land + settle (~2.5 s of real stepping) */
    for (int i = 0; i < 62; i++)
        bd_session_step(s, 40);
    printf("idle shot: state %d clip %s\n", bd_mascot_state(m),
           bd_mascot_clip_name(m));
    snprintf(path, sizeof(path),
             "screenshots/raw/v2_%02d_mascot_%s_idle.raw", num, tag);
    shot_dump(path, bd_session_canvas(s));

    if (strcmp(mode, "tap") == 0) {
        /* tap the mascot and shoot mid reaction clip */
        tk_rect r = bd_mascot_rect(m);
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        bd_mascot_pointer(m, TK_EV_POINTER_DOWN, cx, cy,
                          mono_ms());
        for (int i = 0; i < 3; i++)
            bd_session_step(s, 33);
        bd_mascot_pointer(m, TK_EV_POINTER_UP, cx, cy,
                          mono_ms());
        for (int i = 0; i < 14; i++)
            bd_session_step(s, 33);
        printf("action shot: state %d clip %s (tapped)\n",
               bd_mascot_state(m), bd_mascot_clip_name(m));
    } else {
        /* keep stepping until a walk (or another move) shows */
        int walked = 0;
        for (int i = 0; i < 375 && !walked; i++) {
            bd_session_step(s, 40);
            if (bd_mascot_state(m) == BD_MASCOT_WALK) walked = 1;
        }
        printf("action shot: state %d clip %s (walked %d)\n",
               bd_mascot_state(m), bd_mascot_clip_name(m), walked);
    }
    snprintf(path, sizeof(path),
             "screenshots/raw/v2_%02d_mascot_%s_action.raw",
             num + 1, tag);
    shot_dump(path, bd_session_canvas(s));

    bd_session_free(s);
    ln_store_free(store);
    de_catalog_free(cat);
    return 0;
}
