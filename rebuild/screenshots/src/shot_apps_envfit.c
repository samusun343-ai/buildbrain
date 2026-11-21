/* shot_apps_envfit - screenshots for the DESIGN-apps-envfit
 * rebuild of files / notes / player (2026-11-21 line).
 *
 * Usage: shot_apps_envfit <files|notes|player>
 * Run from rebuild/ (font + fixture relative paths).
 *
 * Each branch builds the app's real UI through its public API,
 * drives it into a mid-interaction state (selection + context
 * bar, editor screen, now-playing / queue screens) through the
 * same paths a tap takes, and paints at 854x480 in BOTH modes
 * (Android = tk_theme_v2, WinUI = tk_theme_winui; the mode
 * switch is a theme switch, app code unchanged).
 *
 * Outputs (raw, converted by convert.py):
 *   v2_72 files  Android, row selected, context bar shown
 *   v2_73 files  WinUI, same state
 *   v2_74 notes  Android, list screen
 *   v2_75 notes  Android, editor screen
 *   v2_76 notes  WinUI, editor screen
 *   v2_77 player Android, library (playing + selection)
 *   v2_78 player Android, now playing
 *   v2_79 player Android, queue
 *   v2_80 player WinUI, library
 *   v2_81 player WinUI, now playing
 *   v2_82 settings Android, home (category icons)
 *   v2_83 settings WinUI, home (left nav + cards)
 *   v2_84 settings WinUI, display page (nav + content)
 *
 * 2026-11-21 (dual-mode line): canvas corrected to the real
 * panel, 854x480 (was 800x480); settings branch added.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "tk.h"
#include "shot_common.h"

#include "bd_files.h"
#include "bd_notes.h"
#include "nmodel.h"
#include "bd_player.h"
#include "bd_settings.h"
#include "setmodel.h"
#include "backend.h"
#include "sysmodel.h"

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

static void copy_file(const char *src, const char *dst)
{
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    if (!a || !b) { fprintf(stderr, "copy %s failed\n", src); return; }
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
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

/* ---------------- bd-files ---------------- */

static void files_fixture(const char *home)
{
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
}

static int shot_files(void)
{
    const char *home = "/tmp/shot-envfit-files/home";
    mkpath(home);
    files_fixture(home);
    setup_reg("/tmp/shot-envfit-files/reg");
    setenv("BD_FILES_APP_DIRS", "apps/desktop", 1);
    int rc = 0;

    tk_theme_set_default(&tk_theme_v2);
    files_app *app = files_app_new(home, "/tmp/shot-envfit-files/data",
                                   854, 480);
    if (!app) { fprintf(stderr, "files_app_new failed\n"); return 1; }
    files_app_select_row(app, 0);   /* context bar appears */
    rc |= shot_root("screenshots/raw/v2_72_files_android.raw",
                    files_app_root(app), 854, 480);
    files_app_free(app);

    tk_theme_set_default(&tk_theme_winui);
    app = files_app_new(home, "/tmp/shot-envfit-files/data-w", 854, 480);
    if (!app) { fprintf(stderr, "files_app_new (winui) failed\n"); return 1; }
    files_app_select_row(app, 0);
    rc |= shot_root("screenshots/raw/v2_73_files_winui.raw",
                    files_app_root(app), 854, 480);
    files_app_free(app);
    tk_theme_set_default(&tk_theme_v2);
    return rc;
}

/* ---------------- bd-notes ---------------- */

static int shot_notes(void)
{
    const char *root = "/tmp/shot-envfit-notes";
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
    int rc = 0;

    tk_theme_set_default(&tk_theme_v2);
    notes_app *app = notes_app_new(s, 854, 480, false);
    if (!app) { fprintf(stderr, "notes_app_new failed\n"); return 1; }
    rc |= shot_root("screenshots/raw/v2_74_notes_list_android.raw",
                    notes_app_root(app), 854, 480);
    notes_app_select(app, id1);
    rc |= shot_root("screenshots/raw/v2_75_notes_editor_android.raw",
                    notes_app_root(app), 854, 480);
    notes_app_free(app);

    tk_theme_set_default(&tk_theme_winui);
    app = notes_app_new(s, 854, 480, false);
    if (!app) { fprintf(stderr, "notes_app_new (winui) failed\n"); return 1; }
    notes_app_select(app, id1);
    rc |= shot_root("screenshots/raw/v2_76_notes_editor_winui.raw",
                    notes_app_root(app), 854, 480);
    notes_app_free(app);
    tk_theme_set_default(&tk_theme_v2);
    nm_close(s);
    return rc;
}

/* ---------------- bd-player ---------------- */

static pe_out *mk_out(void *ud, uint32_t rate, uint16_t ch)
{
    (void)ud;
    return pe_out_null(rate, ch);
}

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
    h[24] = rate & 0xFF; h[25] = (rate >> 8) & 0xFF;
    h[26] = (rate >> 16) & 0xFF; h[27] = (rate >> 24) & 0xFF;
    uint32_t br = rate * 2;
    h[28] = br & 0xFF; h[29] = (br >> 8) & 0xFF;
    h[30] = (br >> 16) & 0xFF; h[31] = (br >> 24) & 0xFF;
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4);
    h[40] = data & 0xFF; h[41] = (data >> 8) & 0xFF;
    h[42] = (data >> 16) & 0xFF; h[43] = (data >> 24) & 0xFF;
    fwrite(h, 1, 44, f);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t smp = (int16_t)(sin(2 * 3.14159265 * 440 * i / rate) *
                                0.5 * 32767);
        fwrite(&smp, 2, 1, f);
    }
    fclose(f);
}

static void player_fixture(const char *media)
{
    char q[1200];
    snprintf(q, sizeof(q), "%s/a_song.mp3", media);
    copy_file("apps/player/tests/fixtures/fixture.mp3", q);
    snprintf(q, sizeof(q), "%s/b_song.ogg", media);
    copy_file("apps/player/tests/fixtures/fixture.ogg", q);
    snprintf(q, sizeof(q), "%s/c_tone.wav", media);
    write_wav(q, 30.0);
    snprintf(q, sizeof(q), "%s/d_morning.wav", media);
    write_wav(q, 45.0);
}

static int shot_player(void)
{
    const char *media = "/tmp/shot-envfit-player/media";
    mkpath(media);
    player_fixture(media);
    const char *dirs[] = { media };
    int rc = 0;

    tk_theme_set_default(&tk_theme_v2);
    player_app *app = player_app_new("/tmp/shot-envfit-player/data",
                                     dirs, 1, mk_out, NULL, 854, 480);
    if (!app) { fprintf(stderr, "player_app_new failed\n"); return 1; }
    player_app_open_row(app, 0);            /* starts playback */
    player_app_tick(app, 0);
    tk_listview_select(player_app_list_widget(app), 1);
    rc |= shot_root("screenshots/raw/v2_77_player_library_android.raw",
                    player_app_root(app), 854, 480);
    /* queue two tracks through the real context-bar cells */
    shot_click(player_app_root(app),
               player_app_queue_later_button(app));
    tk_listview_select(player_app_list_widget(app), 2);
    shot_click(player_app_root(app),
               player_app_queue_later_button(app));
    player_app_show_now_playing(app);
    rc |= shot_root("screenshots/raw/v2_78_player_np_android.raw",
                    player_app_root(app), 854, 480);
    player_app_show_queue(app);
    rc |= shot_root("screenshots/raw/v2_79_player_queue_android.raw",
                    player_app_root(app), 854, 480);
    player_app_free(app);

    tk_theme_set_default(&tk_theme_winui);
    app = player_app_new("/tmp/shot-envfit-player/data-w",
                         dirs, 1, mk_out, NULL, 854, 480);
    if (!app) { fprintf(stderr, "player_app_new (winui) failed\n"); return 1; }
    player_app_open_row(app, 0);
    player_app_tick(app, 0);
    tk_listview_select(player_app_list_widget(app), 1);
    rc |= shot_root("screenshots/raw/v2_80_player_library_winui.raw",
                    player_app_root(app), 854, 480);
    player_app_show_now_playing(app);
    rc |= shot_root("screenshots/raw/v2_81_player_np_winui.raw",
                    player_app_root(app), 854, 480);
    player_app_free(app);
    tk_theme_set_default(&tk_theme_v2);
    return rc;
}

/* ---------------- bd-settings ---------------- */

static int shot_settings(void)
{
    const char *base = "/tmp/shot-envfit-settings";
    mkpath(base);
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
    int rc = 0;

    bd_backend *be = bd_backend_mock_new();
    bd_backend_mock_set(be, 60, 35, 78, true);
    sy_sys *sys = sy_open(proc, logs);

    tk_theme_set_default(&tk_theme_v2);
    settings_app *app = settings_app_new(be, NULL, sys, 854, 480);
    if (!app) { fprintf(stderr, "settings_app_new failed\n"); return 1; }
    rc |= shot_root("screenshots/raw/v2_82_settings_home_android.raw",
                    settings_app_root(app), 854, 480);
    settings_app_free(app);

    tk_theme_set_default(&tk_theme_winui);
    app = settings_app_new(be, NULL, sys, 854, 480);
    if (!app) { fprintf(stderr, "settings_app_new (winui) failed\n"); return 1; }
    rc |= shot_root("screenshots/raw/v2_83_settings_home_winui.raw",
                    settings_app_root(app), 854, 480);
    settings_app_set_page(app, SET_PAGE_DISPLAY);
    settings_app_select_row(app, 0);
    tk_slider_set_value(settings_app_editor_slider(app), 0.8f);
    rc |= shot_root("screenshots/raw/v2_84_settings_display_winui.raw",
                    settings_app_root(app), 854, 480);
    settings_app_free(app);
    tk_theme_set_default(&tk_theme_v2);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <files|notes|player|settings>\n", argv[0]);
        return 2;
    }
    tk_theme_set_default(&tk_theme_v2);
    shot_install_font();
    if (!strcmp(argv[1], "files"))  return shot_files();
    if (!strcmp(argv[1], "notes"))  return shot_notes();
    if (!strcmp(argv[1], "player")) return shot_player();
    if (!strcmp(argv[1], "settings")) return shot_settings();
    fprintf(stderr, "unknown target: %s\n", argv[1]);
    return 2;
}
