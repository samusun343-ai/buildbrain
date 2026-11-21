/* test_settings - settings model tests.
 *
 * The model must read/write through the SAME stores the
 * shell uses: brainreg (isolated here via BRAINREG_*_DIR env
 * into a scratch tree), the kbdsettings bridge, and a mock
 * bd_backend whose writes are counted. Wi-Fi without a
 * supplicant degrades to info rows + failed actions.
 */
#define _POSIX_C_SOURCE 200809L
#include "setmodel.h"
#include "kbdsettings.h"
#include "reg.h"
#include "tk_kbd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static const set_row *find_row(const set_row *rows, size_t n,
                               const char *id)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(rows[i].id, id) == 0) return &rows[i];
    return NULL;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

int main(void)
{
    char tmpl[] = "/tmp/bdsettings-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    char user_dir[1100], sys_dir[1100], proc[1100], logs[1100];
    snprintf(user_dir, sizeof(user_dir), "%s/reg-user", tmpl);
    snprintf(sys_dir, sizeof(sys_dir), "%s/reg-sys", tmpl);
    snprintf(proc, sizeof(proc), "%s/proc", tmpl);
    snprintf(logs, sizeof(logs), "%s/logs", tmpl);
    char cmd[2400];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s %s %s %s",
             user_dir, sys_dir, proc, logs);
    if (system(cmd) != 0) return 2;
    setenv("BRAINREG_USER_DIR", user_dir, 1);
    setenv("BRAINREG_SYS_DIR", sys_dir, 1);

    /* fixture proc for the About page */
    char p[1200];
    snprintf(p, sizeof(p), "%s/meminfo", proc);
    write_file(p, "MemTotal:       131072 kB\n"
                  "MemAvailable:    65536 kB\n"
                  "SwapTotal:       32768 kB\n"
                  "SwapFree:        16384 kB\n");
    snprintf(p, sizeof(p), "%s/mounts", proc);
    char mp[1300];
    snprintf(mp, sizeof(mp), "/dev/root %s ext4 rw 0 0\n", tmpl);
    write_file(p, mp);

    bd_backend *be = bd_backend_mock_new();
    CHECK(be != NULL, "mock backend");
    bd_backend_mock_set(be, 40, 30, 55, true);
    sy_sys *sys = sy_open(proc, logs);
    set_model *m = set_model_new(be, NULL, sys);
    CHECK(m != NULL, "model created");
    set_row rows[40];
    size_t n;

    /* display: slider reflects backend, write goes through */
    n = set_model_rows(m, SET_PAGE_DISPLAY, rows, 40);
    const set_row *r = find_row(rows, n, "brightness");
    CHECK(r && r->kind == SR_SLIDER && r->slider_cur == 40,
          "brightness row 40");
    int w0 = bd_backend_mock_writes(be);
    CHECK(set_model_set_slider(m, SET_PAGE_DISPLAY, "brightness",
                               70) == 0, "brightness set");
    CHECK(bd_backend_brightness_get(be) == 70, "backend now 70");
    CHECK(bd_backend_mock_writes(be) > w0, "write counted");

    /* sound */
    n = set_model_rows(m, SET_PAGE_SOUND, rows, 40);
    r = find_row(rows, n, "volume");
    CHECK(r && r->slider_cur == 30, "volume row 30");
    CHECK(set_model_set_slider(m, SET_PAGE_SOUND, "volume", 12) == 0,
          "volume set");
    CHECK(bd_backend_volume_get(be) == 12, "backend volume 12");

    /* keyboard: defaults from kbdsettings, writes persist */
    n = set_model_rows(m, SET_PAGE_KEYBOARD, rows, 40);
    r = find_row(rows, n, "delay");
    CHECK(r && r->slider_cur == 400, "delay default 400");
    r = find_row(rows, n, "preset");
    CHECK(r && strcmp(r->value, "標準") == 0, "preset 標準");
    CHECK(set_model_set_slider(m, SET_PAGE_KEYBOARD, "delay",
                               500) == 0, "delay set");
    tk_kbd_config cfg;
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_delay_ms == 500, "delay persisted 500");
    /* preset cycle from custom (500/50) -> preset index -1 ->
     * next is preset 0 （遅い 600/80) */
    CHECK(set_model_cycle(m, SET_PAGE_KEYBOARD, "preset") == 0,
          "preset cycle");
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_delay_ms == 600 && cfg.repeat_interval_ms == 80,
          "preset applied 600/80, got %d/%d",
          cfg.repeat_delay_ms, cfg.repeat_interval_ms);
    /* sanitize: absurd slider values are clamped by the bridge */
    CHECK(set_model_set_slider(m, SET_PAGE_KEYBOARD, "interval",
                               100000) == 0, "interval huge set");
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_interval_ms <= 200,
          "interval clamped to %d", cfg.repeat_interval_ms);

    /* power: battery from backend, sleep cycle via reg */
    n = set_model_rows(m, SET_PAGE_POWER, rows, 40);
    r = find_row(rows, n, "battery");
    CHECK(r && strstr(r->value, "55") != NULL, "battery 55: %s",
          r ? r->value : "?");
    r = find_row(rows, n, "sleep");
    CHECK(r && strstr(r->value, "5") != NULL, "sleep default 5 min");
    CHECK(set_model_cycle(m, SET_PAGE_POWER, "sleep") == 0,
          "sleep cycle");
    CHECK(reg_get_int("power", "sleep_timeout_min", -1) == 10,
          "sleep now 10");

    /* datetime: offset slider persists to the key bd-clock reads */
    n = set_model_rows(m, SET_PAGE_DATETIME, rows, 40);
    r = find_row(rows, n, "offset");
    CHECK(r && r->slider_cur == 540, "offset default 540 (JST)");
    CHECK(set_model_set_slider(m, SET_PAGE_DATETIME, "offset",
                               600) == 0, "offset set");
    CHECK(reg_get_int("datetime", "local_offset_min", -1) == 600,
          "offset persisted 600");
    CHECK(set_model_set_slider(m, SET_PAGE_DATETIME, "offset",
                               615) == 0, "offset set 615");
    CHECK(reg_get_int("datetime", "local_offset_min", -1) == 600,
          "offset snapped to 30-min step (600)");

    /* wifi without a supplicant: graceful */
    n = set_model_rows(m, SET_PAGE_WIFI, rows, 40);
    r = find_row(rows, n, "state");
    CHECK(r && strcmp(r->value, "利用不可") == 0, "wifi unavailable row");
    CHECK(set_model_action(m, SET_PAGE_WIFI, "scan", NULL) != 0,
          "wifi action fails cleanly");

    /* mascot: rows over the brainreg "mascot" section the
     * session reads at startup; packs enumerated from a
     * fixture dir (BRAINUX_MASCOT_DIR) */
    {
        char mdir[1100], mp[1200];
        snprintf(mdir, sizeof(mdir), "%s/mascots", tmpl);
        snprintf(mp, sizeof(mp), "mkdir -p %s", mdir);
        CHECK(system(mp) == 0, "mascot fixture dir");
        snprintf(mp, sizeof(mp), "%s/alpha.bdm", mdir);
        write_file(mp, "dummy");
        snprintf(mp, sizeof(mp), "%s/beta.bdm", mdir);
        write_file(mp, "dummy");
        snprintf(mp, sizeof(mp), "%s/readme.txt", mdir);
        write_file(mp, "not a pack");
        setenv("BRAINUX_MASCOT_DIR", mdir, 1);

        n = set_model_rows(m, SET_PAGE_MASCOT, rows, 40);
        r = find_row(rows, n, "enabled");
        CHECK(r && r->kind == SR_TOGGLE && r->toggle_on,
              "mascot enabled defaults on");
        r = find_row(rows, n, "pack");
        CHECK(r && r->kind == SR_CYCLE && r->n_choices == 2,
              "mascot pack lists 2 packs (txt excluded)");
        CHECK(r && strcmp(r->choices[0], "alpha") == 0 &&
              strcmp(r->choices[1], "beta") == 0,
              "mascot pack choices sorted by name");
        r = find_row(rows, n, "size");
        CHECK(r && r->kind == SR_CYCLE && r->choice_cur == 1,
              "mascot size default 208 selected");

        /* character choice writes the full path the session
         * will load */
        CHECK(set_model_set_choice(m, SET_PAGE_MASCOT, "pack",
                                   1) == 0, "mascot pack choose");
        char *pv = reg_get("mascot", "pack");
        snprintf(mp, sizeof(mp), "%s/beta.bdm", mdir);
        CHECK(pv && strcmp(pv, mp) == 0,
              "mascot pack persisted: %s", pv ? pv : "?");
        free(pv);
        n = set_model_rows(m, SET_PAGE_MASCOT, rows, 40);
        r = find_row(rows, n, "pack");
        CHECK(r && r->choice_cur == 1 &&
              strcmp(r->value, "beta") == 0,
              "mascot pack row shows beta");
        /* cycle wraps to the first pack */
        CHECK(set_model_cycle(m, SET_PAGE_MASCOT, "pack") == 0,
              "mascot pack cycle");
        pv = reg_get("mascot", "pack");
        snprintf(mp, sizeof(mp), "%s/alpha.bdm", mdir);
        CHECK(pv && strcmp(pv, mp) == 0, "mascot pack cycled");
        free(pv);

        /* size choice + cycle wrap */
        CHECK(set_model_set_choice(m, SET_PAGE_MASCOT, "size",
                                   3) == 0, "mascot size choose");
        CHECK(reg_get_int("mascot", "size", -1) == 320,
              "mascot size persisted 320");
        CHECK(set_model_cycle(m, SET_PAGE_MASCOT, "size") == 0,
              "mascot size cycle");
        CHECK(reg_get_int("mascot", "size", -1) == 168,
              "mascot size wrapped to 168");

        /* toggles write through reg (the atomic store) */
        CHECK(set_model_set_toggle(m, SET_PAGE_MASCOT, "enabled",
                                   false) == 0, "mascot disable");
        CHECK(!reg_get_bool("mascot", "enabled", true),
              "mascot disabled persisted");
        CHECK(set_model_set_toggle(m, SET_PAGE_MASCOT,
                                   "fullscreen", true) == 0,
              "mascot fullscreen on");
        CHECK(reg_get_bool("mascot", "fullscreen", false),
              "mascot fullscreen persisted");
        n = set_model_rows(m, SET_PAGE_MASCOT, rows, 40);
        r = find_row(rows, n, "enabled");
        CHECK(r && !r->toggle_on, "mascot row shows disabled");

        /* the write is the reg layer's atomic replace: the
         * section file exists and no .tmp is left behind */
        snprintf(mp, sizeof(mp), "%s/mascot.conf", user_dir);
        CHECK(access(mp, F_OK) == 0, "mascot.conf written");
        snprintf(mp, sizeof(mp), "%s/mascot.conf.tmp", user_dir);
        CHECK(access(mp, F_OK) != 0, "no leftover mascot.conf.tmp");
        unsetenv("BRAINUX_MASCOT_DIR");
    }

    /* mute (rebuild): the toggle mirrors bd_shell_set_muted -
     * reg shell/muted plus the backend driven to the effective
     * level; the volume row then shows the preserved level */
    bd_backend_mock_set(be, 40, 30, 55, true);
    /* establish a known preserved level through the real
     * write path (the earlier volume set persisted 12) */
    CHECK(set_model_set_slider(m, SET_PAGE_SOUND, "volume",
                               30) == 0, "volume reset to 30");
    CHECK(set_model_set_toggle(m, SET_PAGE_SOUND, "mute",
                               true) == 0, "mute on");
    CHECK(reg_get_bool("shell", "muted", false), "mute persisted");
    CHECK(bd_backend_volume_get(be) == 0,
          "backend driven to 0 while muted");
    n = set_model_rows(m, SET_PAGE_SOUND, rows, 40);
    r = find_row(rows, n, "mute");
    CHECK(r && r->kind == SR_TOGGLE && r->toggle_on,
          "mute row shows on");
    r = find_row(rows, n, "volume");
    CHECK(r && r->slider_cur == 30 &&
          strstr(r->value, "ミュート中") != NULL,
          "volume row shows preserved 30 while muted: %s",
          r ? r->value : "?");
    /* unmute-on-adjust: a different level clears the mute and
     * persists as the shell's set_volume does */
    CHECK(set_model_set_slider(m, SET_PAGE_SOUND, "volume",
                               45) == 0, "volume adjust while muted");
    CHECK(!reg_get_bool("shell", "muted", false),
          "adjust cleared the mute");
    CHECK(bd_backend_volume_get(be) == 45, "backend volume 45");
    CHECK(reg_get_int("shell", "volume", -1) == 45,
          "volume persisted to shell/volume");
    /* muting again and unmuting restores the preserved level */
    CHECK(set_model_set_toggle(m, SET_PAGE_SOUND, "mute",
                               true) == 0, "mute on again");
    CHECK(set_model_set_toggle(m, SET_PAGE_SOUND, "mute",
                               false) == 0, "mute off");
    CHECK(bd_backend_volume_get(be) == 45,
          "unmute restored preserved 45");

    /* keyboard repeat_source (rebuild): a real store key the
     * old UI never exposed */
    n = set_model_rows(m, SET_PAGE_KEYBOARD, rows, 40);
    r = find_row(rows, n, "repeat_source");
    CHECK(r && r->kind == SR_CYCLE && r->choice_cur == 0,
          "repeat_source defaults to software");
    CHECK(set_model_set_choice(m, SET_PAGE_KEYBOARD,
                               "repeat_source", 1) == 0,
          "repeat_source choose kernel");
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_mode == TK_KBD_REPEAT_KERNEL,
          "repeat_source persisted as kernel");
    CHECK(set_model_cycle(m, SET_PAGE_KEYBOARD,
                          "repeat_source") == 0,
          "repeat_source cycle");
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_mode == TK_KBD_REPEAT_SYNTHETIC,
          "repeat_source cycled back to synthetic");

    /* personal page (rebuild): display mode, wallpaper,
     * panel position - all brainreg shell keys */
    n = set_model_rows(m, SET_PAGE_PERSONAL, rows, 40);
    r = find_row(rows, n, "ui_mode");
    CHECK(r && r->kind == SR_CYCLE && r->choice_cur == 0 &&
          strcmp(r->value, "Android") == 0,
          "ui_mode defaults to Android");
    CHECK(set_model_set_choice(m, SET_PAGE_PERSONAL, "ui_mode",
                               1) == 0, "ui_mode choose Windows");
    {
        char *mv = reg_get("ui", "mode");
        CHECK(mv && strcmp(mv, "winui") == 0,
              "ui_mode persisted as winui");
        free(mv);
    }
    n = set_model_rows(m, SET_PAGE_PERSONAL, rows, 40);
    r = find_row(rows, n, "ui_mode");
    CHECK(r && r->choice_cur == 1, "ui_mode row shows Windows");
    CHECK(set_model_cycle(m, SET_PAGE_PERSONAL, "ui_mode") == 0,
          "ui_mode cycle wraps");
    {
        char *mv = reg_get("ui", "mode");
        CHECK(mv && strcmp(mv, "android") == 0,
              "ui_mode cycled back to android");
        free(mv);
    }
    r = find_row(rows, n, "wallpaper");
    CHECK(r && r->kind == SR_CYCLE && r->choice_cur == 0,
          "wallpaper defaults to theme default");
    CHECK(set_model_set_choice(m, SET_PAGE_PERSONAL, "wallpaper",
                               1) == 0, "wallpaper choose 紺");
    {
        char *wv = reg_get("shell", "wallpaper");
        CHECK(wv && strcmp(wv, "#2E4A6B") == 0,
              "wallpaper persisted as hex");
        free(wv);
    }
    n = set_model_rows(m, SET_PAGE_PERSONAL, rows, 40);
    r = find_row(rows, n, "wallpaper");
    CHECK(r && r->choice_cur == 1 &&
          strcmp(r->value, "紺") == 0, "wallpaper row shows 紺");
    CHECK(set_model_set_choice(m, SET_PAGE_PERSONAL, "wallpaper",
                               0) == 0, "wallpaper back to default");
    {
        char *wv = reg_get("shell", "wallpaper");
        CHECK(!wv || !wv[0], "wallpaper key unset for default");
        free(wv);
    }
    /* an image-path value is a custom value, never silently
     * treated as a preset */
    reg_set("shell", "wallpaper", "/usr/share/wallpapers/a.png");
    n = set_model_rows(m, SET_PAGE_PERSONAL, rows, 40);
    r = find_row(rows, n, "wallpaper");
    CHECK(r && r->choice_cur == -1 &&
          strstr(r->value, "画像") != NULL,
          "image wallpaper reported honestly: %s",
          r ? r->value : "?");
    reg_unset("shell", "wallpaper");
    r = find_row(rows, n, "panel");
    CHECK(r && r->kind == SR_CYCLE && r->choice_cur == 0,
          "panel defaults to bottom");
    CHECK(set_model_set_choice(m, SET_PAGE_PERSONAL, "panel",
                               1) == 0, "panel choose top");
    {
        char *pv2 = reg_get("shell", "panel_position");
        CHECK(pv2 && strcmp(pv2, "top") == 0,
              "panel persisted as top");
        free(pv2);
    }

    /* storage page (rebuild): the fixture mount appears with
     * a used percentage */
    n = set_model_rows(m, SET_PAGE_STORAGE, rows, 40);
    CHECK(n >= 1, "storage rows %zu", n);
    r = find_row(rows, n, "fs0");
    CHECK(r && r->kind == SR_INFO &&
          strstr(r->value, "使用") != NULL,
          "storage row shows usage: %s", r ? r->value : "?");

    /* about: meminfo + mounts through sysmodel */
    n = set_model_rows(m, SET_PAGE_ABOUT, rows, 40);
    r = find_row(rows, n, "mem");
    CHECK(r && strstr(r->value, "128") != NULL,
          "about mem 128 MB: %s", r ? r->value : "?");
    bool has_fs = false;
    for (size_t i = 0; i < n; i++)
        if (strncmp(rows[i].id, "fs", 2) == 0) has_fs = true;
    CHECK(has_fs, "about lists the fixture mount");

    set_model_free(m);
    sy_close(sys);
    bd_backend_free(be);
    printf("test_settings: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
