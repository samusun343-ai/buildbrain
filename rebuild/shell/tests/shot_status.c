/* shot_status - screenshot harness for the status-icon /
 * WinUI-home / 3-button-nav work (REPORT-status-icons).
 *
 * Builds the real shell the way test_shell.c does (temp
 * brainreg dirs, fixture catalog, mock exec seam), drives the
 * public state setters (battery, volume/mute, Wi-Fi through a
 * fake wpa_supplicant), and paints states at 800x480 into raw
 * dumps for shell/shots/. Product code only; run with
 * cwd = rebuild/ (font + fixture + asset paths are relative).
 *
 * Build (from rebuild/):
 *   gcc -I shell/include -I shell/greeter -I include -I osk \
 *       -I libbrainime/include -I screenshots/src \
 *       -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -std=c11 -g -O2 \
 *       shell/tests/shot_status.c -o /tmp/shot_status \
 *       -L shell -lbdshell libbrainui2.a -lm
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "shellui.h"
#include "wifi.h"
#include "shot_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static bool mock_exec(void *ud, const de_entry *e, char *const argv[],
                      bool terminal)
{
    (void)ud; (void)e; (void)argv; (void)terminal;
    return true;
}

/* fake window bridge for the recents shot (same fakes as
 * test_shell.c) */
static size_t fake_list(void *ud, bd_shell_win_entry *out, size_t max)
{
    (void)ud;
    if (max < 2) return 0;
    memset(out, 0, sizeof(*out) * 2);
    out[0].id = 100;
    snprintf(out[0].title, sizeof(out[0].title), "メモ");
    out[1].id = 101;
    snprintf(out[1].title, sizeof(out[1].title), "ファイル");
    out[1].focused = true;
    return 2;
}
static bool fake_activate(void *ud, uint32_t id)
{
    (void)ud; (void)id;
    return true;
}
static bool fake_close(void *ud, uint32_t id)
{
    (void)ud; (void)id;
    return true;
}
static bool fake_thumb(void *ud, uint32_t id, uint32_t *px, int w, int h)
{
    (void)ud;
    uint32_t base = id == 101 ? 0xFF2A4A7Fu : 0xFF7A2A3Au;
    for (int i = 0; i < w * h; i++) px[i] = base;
    return true;
}
static const bd_shell_win_ops fake_ops = {
    fake_list, fake_activate, fake_close, fake_thumb
};

static de_catalog *cat;
static ln_store *store;
static bd_wifi *wifi_strong, *wifi_weak;

/* wmode: 0 = radio off, 1 = radio on / no supplicant
 * (disconnected), 2 = strong supplicant (-42 dBm),
 * 3 = weak supplicant (-85 dBm). panel: 0 none, 1 launcher,
 * 2 quick settings, 3 recents. */
static void shoot(const char *name, const tk_theme *theme, int wmode,
                  int batt, bool charging, int vol, bool muted,
                  int panel)
{
    tk_theme_set_default(theme);
    bd_shell *sh = bd_shell_new(800, 480, cat, store, mock_exec, NULL);
    if (!sh) { fprintf(stderr, "no shell for %s\n", name); return; }
    /* the reg store is shared by every shot in this process,
     * so the radio preference is set explicitly each time
     * (an earlier radio-off shot must not leak into later ones) */
    if (wmode == 0) bd_shell_set_wifi(sh, false);
    else {
        bd_shell_set_wifi(sh, true);
        if (wmode == 2) bd_shell_attach_wifi(sh, wifi_strong);
        else if (wmode == 3) bd_shell_attach_wifi(sh, wifi_weak);
    }
    bd_shell_set_battery(sh, batt, charging);
    bd_shell_set_clock(sh, "21:45");
    bd_shell_set_volume(sh, vol);
    if (muted) bd_shell_set_muted(sh, true);
    bd_shell_set_win_ops(sh, &fake_ops, NULL);
    if (panel == 1) bd_shell_show_launcher(sh, true);
    if (panel == 2) bd_shell_set_quick_open(sh, true);
    if (panel == 3) bd_shell_set_recents_open(sh, true);
    bd_shell_layout(sh, 800, 480);
    char path[512];
    snprintf(path, sizeof(path), "shell/shots/raw/%s.raw", name);
    shot_root(path, bd_shell_root(sh), 800, 480);
    bd_shell_free(sh);
}

static bd_wifi *spawn_fake_wpa(const char *dir, const char *tag,
                               const char *rssi, pid_t *out_pid)
{
    char sock[256], logp[256];
    snprintf(sock, sizeof(sock), "%s/%s.sock", dir, tag);
    snprintf(logp, sizeof(logp), "%s/%s.log", dir, tag);
    if (rssi) setenv("FAKE_WPA_RSSI", rssi, 1);
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = { "shell/fake_wpa", sock, logp, NULL };
        execv(args[0], args);
        _exit(127);
    }
    unsetenv("FAKE_WPA_RSSI");
    struct stat st;
    int tries = 0;
    while (stat(sock, &st) != 0 && tries++ < 300) usleep(10000);
    *out_pid = pid;
    return bd_wifi_new(sock);
}

int main(void)
{
    char tmpl[] = "/tmp/shot-status-XXXXXX";
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

    cat = de_catalog_new("ja");
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications");
    de_catalog_load_dir(cat, "shell/tests/fixtures/applications2");
    store = ln_store_new();
    ln_pin(store, "writer.desktop");

    if (system("mkdir -p shell/shots/raw") != 0) return 2;

    pid_t p1 = 0, p2 = 0;
    wifi_strong = spawn_fake_wpa(base, "strong", NULL, &p1);
    wifi_weak = spawn_fake_wpa(base, "weak", "-85", &p2);

    /* Android mode: the status states */
    shoot("st_a_home_low", &tk_theme_v2, 2, 12, false, 40, true, 0);
    shoot("st_a_home_charge", &tk_theme_v2, 3, 72, true, 30, false, 0);
    shoot("st_a_home_full", &tk_theme_v2, 1, 100, false, 100, false, 0);
    shoot("st_a_home_off", &tk_theme_v2, 0, 45, false, 65, false, 0);
    shoot("st_a_qs", &tk_theme_v2, 2, 72, false, 30, false, 2);
    shoot("st_a_recents", &tk_theme_v2, 2, 72, false, 40, false, 3);
    shoot("st_a_launcher", &tk_theme_v2, 2, 72, false, 40, false, 1);

    /* WinUI mode: home, Start, quick settings */
    shoot("st_w_home", &tk_theme_winui, 2, 55, false, 40, false, 0);
    shoot("st_w_home_charge_mute", &tk_theme_winui, 3, 18, true, 40, true, 0);
    shoot("st_w_launcher", &tk_theme_winui, 2, 55, false, 40, false, 1);
    shoot("st_w_qs", &tk_theme_winui, 2, 55, false, 30, false, 2);

    bd_wifi_free(wifi_strong);
    bd_wifi_free(wifi_weak);
    kill(p1, SIGTERM);
    kill(p2, SIGTERM);
    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
    ln_store_free(store);
    de_catalog_free(cat);
    return 0;
}
