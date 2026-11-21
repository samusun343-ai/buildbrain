/* shot_apps - screenshots of the rebuild apps at 854x480.
 *
 * Usage: shot_apps <files|notes|settings|calc|player>
 *
 * Each branch builds the app's real UI through its public app
 * API (the same constructors the app mains and the UI tests use),
 * seeds it with realistic Japanese dummy data through the app's
 * own model layer / filesystem, and paints with tk_root_paint().
 * No app drawing code is duplicated here.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "tk.h"
#include "shot_common.h"

#include "bd_files.h"
#include "bd_notes.h"
#include "nmodel.h"
#include "bd_settings.h"
#include "setmodel.h"
#include "bd_calc.h"
#include "bd_player.h"
#include "backend.h"
#include "sysmodel.h"
#include "bd_tasks.h"
#include "tmodel.h"
#include "bd_clock.h"
#include "clockmodel.h"
#include "bd_recorder.h"
#include "bd_pkg.h"
#include "bd_admin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkpath(const char *path)
{
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    if (system(cmd) != 0) fprintf(stderr, "mkdir failed: %s\n", path);
}

static void write_file(const char *path, const char *data, size_t repeat)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    for (size_t i = 0; i < repeat; i++) fputs(data, f);
    fclose(f);
}

static void setup_reg(const char *base)
{
    char sysdir[600], userdir[600];
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkpath(sysdir);
    mkpath(userdir);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);
}

/* ---------------- bd-files ---------------- */

static int shot_files(void)
{
    const char *home = "/tmp/shot-files/home";
    const char *data = "/tmp/shot-files/data";
    mkpath(home);
    mkpath(data);
    char p[1200];
    snprintf(p, sizeof(p), "%s/仕事", home);            mkpath(p);
    snprintf(p, sizeof(p), "%s/学校", home);            mkpath(p);
    snprintf(p, sizeof(p), "%s/ダウンロード", home);    mkpath(p);
    snprintf(p, sizeof(p), "%s/会議メモ.txt", home);
    write_file(p, "会議メモ\n来週の定例は月曜10時から。\n", 4);
    snprintf(p, sizeof(p), "%s/買い物リスト.txt", home);
    write_file(p, "牛乳、卵、食パン、りんご\n", 2);
    snprintf(p, sizeof(p), "%s/旅行のしおり.txt", home);
    write_file(p, "1日目: 京都駅に集合\n2日目: 嵐山を散策\n", 8);
    snprintf(p, sizeof(p), "%s/予算表.csv", home);
    write_file(p, "項目,金額\n食費,32000\n交通費,8500\n", 3);
    snprintf(p, sizeof(p), "%s/写真_2026-11-20.jpg", home);
    write_file(p, "dummy-jpeg-bytes", 400);
    snprintf(p, sizeof(p), "%s/音楽サンプル.mp3", home);
    write_file(p, "dummy-mp3-bytes", 900);
    snprintf(p, sizeof(p), "%s/仕事/企画書.txt", home);
    write_file(p, "企画書の下書き\n", 20);

    setup_reg("/tmp/shot-files/reg");
    setenv("BD_FILES_APP_DIRS", "apps/desktop", 1);

    files_app *app = files_app_new(home, data, 854, 480);
    if (!app) { fprintf(stderr, "files_app_new failed\n"); return 1; }
    int rc = shot_root("screenshots/raw/v2_09_files.raw",
                       files_app_root(app), 854, 480);
    files_app_free(app);
    return rc;
}

/* ---------------- bd-notes ---------------- */

static int shot_notes(void)
{
    const char *root = "/tmp/shot-notes";
    mkpath(root);
    nm_store *s = nm_open(root, 1 << 20);
    if (!s) { fprintf(stderr, "nm_open failed\n"); return 1; }
    nm_folder_create(s, "日記");
    nm_folder_create(s, "買い物");
    uint64_t id1 = nm_create(s, "日記",
        "2026年11月20日（金）\n"
        "今日は天気が良かったので、駅前の公園まで散歩した。\n"
        "帰りに本屋で新しい国語辞典を買った。\n"
        "夜は明日の会議の資料を読む。");
    nm_create(s, "買い物",
        "買い物リスト\n・牛乳\n・卵 10個\n・食パン\n・りんご 5個\n・味噌");
    nm_create(s, NULL,
        "会議のメモ\n来週の月曜10時から定例会議。\n"
        "資料は前日までに共有すること。");

    notes_app *app = notes_app_new(s, 854, 480, false);
    if (!app) { fprintf(stderr, "notes_app_new failed\n"); return 1; }
    notes_app_select(app, id1);
    int rc = shot_root("screenshots/raw/v2_10_notes.raw",
                       notes_app_root(app), 854, 480);
    notes_app_free(app);
    nm_close(s);
    return rc;
}

/* ---------------- bd-settings ---------------- */

static int shot_settings(void)
{
    const char *base = "/tmp/shot-settings";
    setup_reg(base);
    char proc[600], logs[600], p[1200];
    snprintf(proc, sizeof(proc), "%s/proc", base);
    snprintf(logs, sizeof(logs), "%s/logs", base);
    mkpath(proc);
    mkpath(logs);
    snprintf(p, sizeof(p), "%s/meminfo", proc);
    write_file(p, "MemTotal: 131072 kB\nMemAvailable: 65536 kB\n", 1);
    snprintf(p, sizeof(p), "%s/mounts", proc);
    write_file(p, "/dev/root / ext4 rw 0 0\n", 1);

    bd_backend *be = bd_backend_mock_new();
    bd_backend_mock_set(be, 60, 35, 78, true);
    sy_sys *sys = sy_open(proc, logs);
    settings_app *app = settings_app_new(be, NULL, sys, 854, 480);
    if (!app) { fprintf(stderr, "settings_app_new failed\n"); return 1; }

    /* every page of the rebuilt settings, plus mid-interaction
     * states driven through the same public paths a tap takes */
    int rc = 0;
    /* display: brightness row selected, its own inline slider
     * driven to 80% - the shot IS the editing state */
    settings_app_set_page(app, SET_PAGE_DISPLAY);
    settings_app_select_row(app, 0);
    tk_slider_set_value(settings_app_editor_slider(app), 0.8f);
    rc |= shot_root("screenshots/raw/v2_11_settings.raw",
                    settings_app_root(app), 854, 480);
    /* keyboard: preset chips in the row */
    settings_app_set_page(app, SET_PAGE_KEYBOARD);
    rc |= shot_root("screenshots/raw/v2_15_settings_keyboard.raw",
                    settings_app_root(app), 854, 480);
    /* sound: volume slider row */
    settings_app_set_page(app, SET_PAGE_SOUND);
    rc |= shot_root("screenshots/raw/v2_16_settings_sound.raw",
                    settings_app_root(app), 854, 480);
    /* Wi-Fi: the on/off switch row heads the page (no
     * supplicant in the harness, so the status rows show the
     * honest 利用不可 state a device without one shows) */
    settings_app_set_page(app, SET_PAGE_WIFI);
    rc |= shot_root("screenshots/raw/v2_17_settings_wifi.raw",
                    settings_app_root(app), 854, 480);
    /* power: battery + sleep choice chips */
    settings_app_set_page(app, SET_PAGE_POWER);
    rc |= shot_root("screenshots/raw/v2_18_settings_power.raw",
                    settings_app_root(app), 854, 480);
    /* datetime: offset slider row */
    settings_app_set_page(app, SET_PAGE_DATETIME);
    rc |= shot_root("screenshots/raw/v2_19_settings_datetime.raw",
                    settings_app_root(app), 854, 480);
    /* about: info rows from the fixture proc */
    settings_app_set_page(app, SET_PAGE_ABOUT);
    rc |= shot_root("screenshots/raw/v2_20_settings_about.raw",
                    settings_app_root(app), 854, 480);
    settings_app_free(app);
    return rc;
}

/* ---------------- bd-calc ---------------- */

static int shot_calc(void)
{
    const char *dir = "/tmp/shot-calc";
    mkpath(dir);
    calc_app *app = calc_app_new(dir, 854, 480);
    if (!app) { fprintf(stderr, "calc_app_new failed\n"); return 1; }
    calc_app_set_expr(app, "6*7");
    if (calc_app_press(app, "=") != 0)
        fprintf(stderr, "calc: '=' press failed\n");
    calc_app_set_expr(app, "1234*5.6");
    if (calc_app_press(app, "=") != 0)
        fprintf(stderr, "calc: '=' press failed\n");
    printf("calc history rows: %zu\n", calc_app_history_rows(app));
    int rc = shot_root("screenshots/raw/v2_12_calc.raw",
                       calc_app_root(app), 854, 480);
    calc_app_free(app);
    return rc;
}

/* ---------------- bd-player ---------------- */

static pe_out *mk_out(void *ud, uint32_t rate, uint16_t ch)
{
    (void)ud;
    return pe_out_null(rate, ch);
}

static void copy_file(const char *src, const char *dst)
{
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    if (!a || !b) { perror(src); return; }
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
}

/* same WAV writer shape as player/tests/test_player_ui.c */
static void write_wav(const char *path, double seconds)
{
    const int rate = 22050;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    uint32_t frames = (uint32_t)(rate * seconds);
    uint32_t data = frames * 2;
    uint8_t h[44] = {0};
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + data;
    h[4] = v; h[5] = v >> 8; h[6] = v >> 16; h[7] = v >> 24;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[20] = 1; h[22] = 1;
    h[24] = rate; h[25] = rate >> 8; h[26] = rate >> 16; h[27] = rate >> 24;
    h[28] = rate * 2; h[29] = (rate * 2) >> 8;
    h[30] = (rate * 2) >> 16; h[31] = (rate * 2) >> 24;
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4);
    h[40] = data; h[41] = data >> 8; h[42] = data >> 16; h[43] = data >> 24;
    fwrite(h, 1, 44, f);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t s = (int16_t)(sin(2 * 3.14159265 * 440 * i / rate) *
                              0.5 * 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static int shot_player(void)
{
    const char *media = "/tmp/shot-player/media";
    const char *data = "/tmp/shot-player/data";
    mkpath(media);
    mkpath(data);
    char q[1200];
    snprintf(q, sizeof(q), "%s/a_song.mp3", media);
    copy_file("apps/player/tests/fixtures/fixture.mp3", q);
    snprintf(q, sizeof(q), "%s/b_song.ogg", media);
    copy_file("apps/player/tests/fixtures/fixture.ogg", q);
    snprintf(q, sizeof(q), "%s/c_tone.wav", media);
    write_wav(q, 30.0);
    snprintf(q, sizeof(q), "%s/d_morning.wav", media);
    write_wav(q, 45.0);

    const char *dirs[] = { media };
    player_app *app = player_app_new(data, dirs, 1, mk_out, NULL, 854, 480);
    if (!app) { fprintf(stderr, "player_app_new failed\n"); return 1; }
    printf("player rows: %zu\n", player_app_rows(app));
    player_app_open_row(app, 0);
    player_app_tick(app, 12000); /* 12 s into the track */
    int rc = shot_root("screenshots/raw/v2_13_player.raw",
                       player_app_root(app), 854, 480);
    player_app_free(app);
    return rc;
}

/* ---------------- shared click driver ---------------- */

static void shot_click(tk_root *root, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_UP;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
}

/* ---------------- bd-tasks ---------------- */

static uint64_t seed_task(tm_store *s, uint64_t list,
                          const char *title, const char *due,
                          bool flagged, int prio)
{
    tm_task f;
    memset(&f, 0, sizeof(f));
    f.title = (char *)title;
    f.list_id = list;
    f.flagged = flagged;
    f.priority = prio;
    if (due) snprintf(f.due_date, sizeof(f.due_date), "%s", due);
    return tm_task_add(s, &f);
}

static int shot_tasks(void)
{
    const char *dir = "/tmp/shot-tasks";
    mkpath(dir);
    tm_store *s = tm_open(dir);
    if (!s) { fprintf(stderr, "tm_open failed\n"); return 1; }
    uint64_t def = tm_default_list(s);
    uint64_t shop = tm_list_create(s, "買い物", 4);
    uint64_t study = tm_list_create(s, "勉強", 1);
    (void)study;
    seed_task(s, def, "会議の資料を作る", "2026-11-20", true,
              TM_PRIO_HIGH);
    seed_task(s, def, "先生にメールを返す", "2026-11-20", false,
              TM_PRIO_MEDIUM);
    seed_task(s, def, "図書館に本を返す", "2026-11-22", false,
              TM_PRIO_LOW);
    seed_task(s, shop, "牛乳", NULL, false, TM_PRIO_NONE);
    seed_task(s, shop, "卵 10個", NULL, false, TM_PRIO_NONE);
    seed_task(s, shop, "食パン", NULL, true, TM_PRIO_NONE);
    seed_task(s, shop, "りんご 5個", NULL, false, TM_PRIO_NONE);

    tasks_app *app = tasks_app_new(s, "2026-11-20", 854, 480);
    if (!app) { fprintf(stderr, "tasks_app_new failed\n"); return 1; }
    tasks_app_set_view(app, TM_VIEW_LIST, shop);
    tasks_app_select_row(app, 0);
    int rc = shot_root("screenshots/raw/v2_21_tasks.raw",
                       tasks_app_root(app), 854, 480);
    tasks_app_free(app);
    tm_close(s);
    return rc;
}

/* ---------------- bd-clock ---------------- */

static int shot_clock(void)
{
    const char *dir = "/tmp/shot-clock";
    mkpath(dir);
    ck_store *s = ck_open(dir);
    if (!s) { fprintf(stderr, "ck_open failed\n"); return 1; }
    clock_app *app = clock_app_new(s, 540, 854, 480);
    if (!app) { fprintf(stderr, "clock_app_new failed\n"); return 1; }
    /* 2026-11-20 13:45 JST = 04:45 UTC */
    int64_t wall = ck_days_from_civil(2026, 11, 20) * 86400 +
                   4 * 3600 + 45 * 60;
    clock_app_tick(app, wall, 0);
    int rc = shot_root("screenshots/raw/v2_22_clock.raw",
                       clock_app_root(app), 854, 480);
    /* alarms tab with one alarm made through the real editor */
    clock_app_set_tab(app, 1);
    tk_textfield_set_text(clock_app_editor_hour(app), "07");
    tk_textfield_set_text(clock_app_editor_min(app), "00");
    tk_textfield_set_text(clock_app_editor_label(app), "起床");
    clock_app_editor_new(app);
    rc |= shot_root("screenshots/raw/v2_23_clock_alarms.raw",
                    clock_app_root(app), 854, 480);
    clock_app_free(app);
    ck_close(s);
    return rc;
}

/* ---------------- bd-recorder ---------------- */

static rc_source *rec_mk_src(void *ud)
{
    (void)ud;
    return rc_source_synth(440, RC_RATE, RC_CHANNELS);
}
static rc_sink *rec_mk_sink(void *ud)
{
    (void)ud;
    return rc_sink_null(RC_RATE, RC_CHANNELS);
}

static void record_take(rec_app *app, tk_root *root, int base_ms,
                        int ticks)
{
    shot_click(root, rec_app_record_button(app));
    for (int i = 0; i < ticks; i++)
        rec_app_tick(app, (uint64_t)(base_ms + i * 100));
    shot_click(root, rec_app_rec_stop_button(app));
}

static int shot_recorder(void)
{
    const char *dir = "/tmp/shot-recorder";
    mkpath(dir);
    rec_app *app = rec_app_new(dir, rec_mk_src, rec_mk_sink, NULL,
                               854, 480);
    if (!app) { fprintf(stderr, "rec_app_new failed\n"); return 1; }
    tk_root *root = rec_app_root(app);
    record_take(app, root, 0, 30);     /* ~3 s */
    record_take(app, root, 4000, 15);  /* ~1.5 s */
    int rc = shot_root("screenshots/raw/v2_24_recorder.raw",
                       root, 854, 480);
    rec_app_free(app);
    return rc;
}

/* ---------------- bd-pkg ---------------- */

static void sh(const char *cmd) { if (system(cmd) != 0) fprintf(stderr, "sh: %s\n", cmd); }

static void make_deb(const char *dir, const char *name,
                     const char *version, const char *desc)
{
    char cmd[2400], sub[1200], ctl[1200], out[1200];
    snprintf(sub, sizeof(sub), "%s/build-%s-%s", dir, name, version);
    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s/DEBIAN %s/usr/share/%s && chmod 755 %s/DEBIAN",
             sub, sub, name, sub);
    sh(cmd);
    snprintf(ctl, sizeof(ctl), "%s/DEBIAN/control", sub);
    FILE *f = fopen(ctl, "w");
    fprintf(f,
            "Package: %s\nVersion: %s\nArchitecture: all\n"
            "Maintainer: Brain Project <dev@example.jp>\n"
            "Section: utils\nInstalled-Size: 4\n"
            "Description: %s\n", name, version, desc);
    fclose(f);
    snprintf(out, sizeof(out), "%s/%s_%s_all.deb", dir, name, version);
    snprintf(cmd, sizeof(cmd), "dpkg-deb --build %s %s >/dev/null",
             sub, out);
    sh(cmd);
}

static int shot_pkg(void)
{
    const char *base = "/tmp/shot-pkg";
    char root[1100], admin[1100], debs[1100], cmd[2400];
    snprintf(root, sizeof(root), "%s/root", base);
    snprintf(admin, sizeof(admin), "%s/root/var/lib/dpkg", base);
    snprintf(debs, sizeof(debs), "%s/debs", base);
    snprintf(cmd, sizeof(cmd),
             "rm -rf %s && mkdir -p %s %s && touch %s/status %s/available",
             base, admin, debs, admin, admin);
    sh(cmd);
    make_deb(debs, "bd-hello", "1.0", "あいさつを表示する小さな道具");
    make_deb(debs, "bd-memo", "2.0", "コマンドラインのメモ帳");

    pkg_app *app = pkg_app_new(debs, root, admin, NULL, NULL,
                               854, 480);
    if (!app) { fprintf(stderr, "pkg_app_new failed\n"); return 1; }
    tk_root *rt = pkg_app_root(app);
    /* install bd-hello 1.0 from the catalog (the only bd-hello
     * in it at this point), THEN publish 1.1 and reload, so the
     * updates view shows the pending update with the Update
     * All button - the reference flow */
    shot_click(rt, pkg_app_view_button(app, 1));
    for (size_t i = 0; i < pkg_app_rows(app); i++) {
        tk_listview_select(pkg_app_list_widget(app), (int)i);
        const char *sel = pkg_app_selected_name(app);
        if (sel && strcmp(sel, "bd-hello") == 0) break;
    }
    shot_click(rt, pkg_app_install_button(app));
    make_deb(debs, "bd-hello", "1.1", "あいさつを表示する小さな道具");
    pkg_app_refresh(app);
    shot_click(rt, pkg_app_view_button(app, 2));
    tk_listview_select(pkg_app_list_widget(app), 0);
    int rc = shot_root("screenshots/raw/v2_25_pkg.raw", rt, 854, 480);
    pkg_app_free(app);
    return rc;
}

/* ---------------- bd-admin ---------------- */

static int shot_admin(void)
{
    const char *logs = "/tmp/shot-admin/logs";
    mkpath(logs);
    char p[1200];
    snprintf(p, sizeof(p), "%s/system.log", logs);
    write_file(p, "braindesk2: セッション開始\n"
                  "wpa: 接続しました\n"
                  "kernel: SD カードを認識\n", 6);
    /* the monitor reads the host's real /proc: process, mount
     * and memory data are the machine's own, not fixtures */
    sy_sys *sys = sy_open("/proc", logs);
    admin_app *app = admin_app_new(sys, 854, 480);
    if (!app) { fprintf(stderr, "admin_app_new failed\n"); return 1; }
    admin_app_refresh(app);
    int rc = shot_root("screenshots/raw/v2_26_admin.raw",
                       admin_app_root(app), 854, 480);
    admin_app_free(app);
    sy_close(sys);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <files|notes|settings|calc|player|tasks|clock|recorder|pkg|admin>\n",
                argv[0]);
        return 2;
    }
    /* UI v2 is the product default theme; like the product
     * entry points, the harness selects it unconditionally */
    tk_theme_set_default(&tk_theme_v2);
    shot_install_font();
    if (!strcmp(argv[1], "files"))    return shot_files();
    if (!strcmp(argv[1], "notes"))    return shot_notes();
    if (!strcmp(argv[1], "settings")) return shot_settings();
    if (!strcmp(argv[1], "calc"))     return shot_calc();
    if (!strcmp(argv[1], "player"))   return shot_player();
    if (!strcmp(argv[1], "tasks"))    return shot_tasks();
    if (!strcmp(argv[1], "clock"))    return shot_clock();
    if (!strcmp(argv[1], "recorder")) return shot_recorder();
    if (!strcmp(argv[1], "pkg"))      return shot_pkg();
    if (!strcmp(argv[1], "admin"))    return shot_admin();
    fprintf(stderr, "unknown target: %s\n", argv[1]);
    return 2;
}
