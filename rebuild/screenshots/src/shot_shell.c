/* shot_shell - screenshots of the braindesk2 shell UI (shell v2).
 *
 * Builds the real shell (shell/src/shellui.c) exactly the way
 * shell/tests/test_shell.c does - temp brainreg dirs, the fixture
 * .desktop catalog plus the rebuild apps' own .desktop files, a
 * mock exec seam - then paints four states of it at 800x480:
 *   01 desktop, 02 launcher open, 03 quick settings open,
 *   04 single-finger assist overlay open.
 * All painting is tk_root_paint() over bd_shell_root(): product
 * code only.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "shellui.h"
#include "backend.h"
#include "shot_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

static char pathbuf[160];
static const char *shot_path(const char *pfx, const char *name)
{
    snprintf(pathbuf, sizeof(pathbuf), "screenshots/raw/%s%s", pfx, name);
    return pathbuf;
}

int main(void)
{
    /* UI v2 is the product default theme (product entry points
     * select it via tk_theme_set_default); the harness does the
     * same, unconditionally, and prefixes outputs with v2_. */
    tk_theme_set_default(&tk_theme_v2);
    const char *pfx = "v2_";

    /* brainreg into a temp dir (same pattern as the shell tests) */
    char tmpl[] = "/tmp/shot-shell-XXXXXX";
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 2; }
    char sysdir[512], userdir[512];
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);

    reg_set("shell", "wallpaper", "#102030");
    reg_set("shell", "volume", "40");

    shot_install_font();

    de_catalog *cat = de_catalog_new("ja");
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications");
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications2");
    de_catalog_load_dir(cat, "apps/desktop");
    de_catalog_load_dir(cat, "apps/files");
    de_catalog_load_dir(cat, "apps/notes");
    ln_store *store = ln_store_new();
    /* UI v2 refinement: seed the store the way a real device
     * looks - six pinned apps with distinct icons (the pilot's
     * two pins both resolved to the same notes document icon,
     * which made the desktop and launcher read as empty) and a
     * few recents recorded through the store's own launch log. */
    ln_pin(store, "bd-files.desktop");
    ln_pin(store, "bd-notes.desktop");
    ln_pin(store, "bd-calc.desktop");
    ln_pin(store, "bd-player.desktop");
    ln_pin(store, "netsurf.desktop");
    ln_pin(store, "bd-settings.desktop");
    ln_record_launch(store, "bd-files.desktop");
    ln_record_launch(store, "bd-calc.desktop");
    ln_record_launch(store, "bd-notes.desktop");

    bd_shell *sh = bd_shell_new(800, 480, cat, store, mock_exec, NULL);
    if (!sh) { fprintf(stderr, "bd_shell_new failed\n"); return 1; }

    /* panel status: fixed clock, battery 72%, not charging */
    bd_shell_set_clock(sh, "09:41");
    bd_shell_set_battery(sh, 72, false);

    bd_shell_layout(sh, 800, 480);
    shot_root(shot_path(pfx, "01_desktop.raw"), bd_shell_root(sh), 800, 480);

    bd_shell_show_launcher(sh, true);
    printf("launcher results: %zu\n", bd_shell_result_count(sh));
    bd_shell_layout(sh, 800, 480);
    shot_root(shot_path(pfx, "02_launcher.raw"), bd_shell_root(sh), 800, 480);

    bd_shell_show_launcher(sh, false);
    bd_shell_set_quick_open(sh, true);
    bd_shell_layout(sh, 800, 480);
    shot_root(shot_path(pfx, "03_quicksettings.raw"), bd_shell_root(sh),
              800, 480);

    bd_shell_set_quick_open(sh, false);
    bd_shell_set_assist_open(sh, true);
    bd_shell_layout(sh, 800, 480);
    shot_root(shot_path(pfx, "04_assist.raw"), bd_shell_root(sh), 800, 480);

    /* 14: notification sheet (from the clock) + toast card */
    bd_shell_set_assist_open(sh, false);
    bd_shell_notify(sh, "ダウンロードが完了しました");
    bd_shell_notify(sh, "Wi-Fi に接続しました: home-net");
    bd_shell_layout(sh, 800, 480);
    {
        /* click the panel clock button to open the sheet */
        tk_widget *clock = NULL;
        tk_widget *stack[256];
        int sp = 0;
        stack[sp++] = tk_root_top(bd_shell_root(sh));
        while (sp > 0 && !clock) {
            tk_widget *w = stack[--sp];
            if (tk_widget_kind_of(w) == TK_W_BUTTON &&
                strcmp(tk_button_label(w), "09:41") == 0)
                clock = w;
            else
                for (size_t i = 0; i < tk_child_count(w) && sp < 256; i++)
                    stack[sp++] = tk_child_at(w, i);
        }
        if (clock) {
            tk_rect r = tk_widget_rect(clock);
            tk_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.x = r.x + r.w / 2;
            ev.y = r.y + r.h / 2;
            ev.type = TK_EV_POINTER_DOWN;
            tk_root_event(bd_shell_root(sh), &ev);
            ev.type = TK_EV_POINTER_UP;
            tk_root_event(bd_shell_root(sh), &ev);
        }
    }
    bd_shell_layout(sh, 800, 480);
    shot_root(shot_path(pfx, "14_notifications.raw"), bd_shell_root(sh),
              800, 480);

    bd_shell_free(sh);
    ln_store_free(store);
    de_catalog_free(cat);
    return 0;
}
