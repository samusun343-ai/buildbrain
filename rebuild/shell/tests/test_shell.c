/* test_shell.c - headless tests for the M1 shell UI: build the
 * shell on a memory canvas, drive it with synthetic pointer events,
 * and verify views, launcher filtering, launching through the mock
 * exec seam, quick settings, tasks and notifications. */
#include "reg.h"
#include "desktop.h"
#include "launcher.h"
#include "shellui.h"
#include "kbdsettings.h"
#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static char sysdir[256], userdir[256];

static void setup_reg(void)
{
    char tmpl[] = "/tmp/bdshellXXXXXX";
    char *base = mkdtemp(tmpl);
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);
}

static int mock_calls;
static char mock_last[128];
static bool mock_exec(void *ud, const de_entry *e, char *const argv[], bool terminal)
{
    (void)ud; (void)terminal;
    mock_calls++;
    snprintf(mock_last, sizeof(mock_last), "%s", e->id);
    (void)argv;
    return true;
}

static void click(bd_shell *sh, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event dn = {0}, up = {0};
    dn.type = TK_EV_POINTER_DOWN; dn.x = r.x + r.w / 2; dn.y = r.y + r.h / 2;
    up.type = TK_EV_POINTER_UP; up.x = dn.x; up.y = dn.y;
    tk_root_event(bd_shell_root(sh), &dn);
    tk_root_event(bd_shell_root(sh), &up);
}

/* find a widget by kind + index in tree order (test navigation) */
static void collect(tk_widget *w, tk_widget_kind kind, tk_widget **out, int *n, int max)
{
    if (tk_widget_kind_of(w) == kind && *n < max) out[(*n)++] = w;
    for (size_t i = 0; i < tk_child_count(w); i++)
        collect(tk_child_at(w, i), kind, out, n, max);
}

static tk_widget *find_button(bd_shell *sh, const char *label)
{
    tk_widget *btns[64];
    int n = 0;
    collect(bd_shell_top(sh), TK_W_BUTTON, btns, &n, 64);
    for (int i = 0; i < n; i++)
        if (tk_button_label(btns[i]) &&
            strcmp(tk_button_label(btns[i]), label) == 0)
            return btns[i];
    return NULL;
}

/* --- fakes for the navigation / app-switcher tests --- */
static int nav_sink_count, nav_sink_last;
static void test_nav_sink(void *ud, bd_nav_action act)
{
    (void)ud;
    nav_sink_count++;
    nav_sink_last = act;
}
static int win_list_calls;
static uint32_t win_activated, win_closed;
static size_t test_win_list(void *ud, bd_shell_win_entry *out, size_t max)
{
    (void)ud;
    win_list_calls++;
    if (max < 2) return 0;
    memset(out, 0, sizeof(*out) * 2);
    out[0].id = 100;
    snprintf(out[0].title, sizeof(out[0].title), "AppB");
    out[1].id = 101;
    snprintf(out[1].title, sizeof(out[1].title), "AppA");
    out[1].focused = true;
    return 2;
}
static bool test_win_activate(void *ud, uint32_t id)
{
    (void)ud;
    win_activated = id;
    return true;
}
static bool test_win_close(void *ud, uint32_t id)
{
    (void)ud;
    win_closed = id;
    return true;
}
static bool test_win_thumb(void *ud, uint32_t id, uint32_t *px,
                           int w, int h)
{
    (void)ud; (void)id;
    for (int i = 0; i < w * h; i++) px[i] = 0xFF204060u;
    return true;
}
static const bd_shell_win_ops test_win_ops = {
    test_win_list, test_win_activate, test_win_close, test_win_thumb
};

int main(void)
{
    setup_reg();
    /* shell settings via the reg store, as the settings app would */
    reg_set("shell", "wallpaper", "#102030");
    reg_set("shell", "volume", "40");

    de_catalog *cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "tests/fixtures/applications");
    de_catalog_load_dir(cat, "tests/fixtures/applications2");
    ln_store *store = ln_store_new();
    ln_pin(store, "writer.desktop");

    mock_calls = 0;
    bd_shell *sh = bd_shell_new(800, 480, cat, store, mock_exec, NULL);
    CHECK(sh != NULL);
    bd_shell_layout(sh, 800, 480);

    /* settings were applied */
    CHECK(!bd_shell_panel_on_top(sh)); /* default bottom */
    tk_color wp = bd_shell_wallpaper(sh);
    CHECK(wp.r == 0x10 && wp.g == 0x20 && wp.b == 0x30);
    CHECK(bd_shell_volume(sh) == 40);

    /* paint the desktop: wallpaper color at a free point */
    tk_canvas *cv = tk_canvas_new(800, 480);
    tk_root_paint(bd_shell_root(sh), cv, tk_theme_default());
    tk_color at = tk_canvas_pixel(cv, 790, 10);
    CHECK(at.r == 0x10 && at.g == 0x20 && at.b == 0x30);

    /* initial state */
    CHECK(bd_shell_view(sh) == BD_VIEW_DESKTOP);
    CHECK(bd_shell_result_count(sh) == 9); /* visible catalog entries */
    CHECK(bd_shell_task_count(sh) == 0);
    CHECK(bd_shell_quick_open(sh) == false);

    /* desktop icon click launches through the mock (writer pinned) */
    tk_widget *wbtn = find_button(sh, "Writer");
    CHECK(wbtn != NULL);
    if (wbtn) click(sh, wbtn);
    CHECK(mock_calls == 1);
    CHECK(strcmp(mock_last, "writer.desktop") == 0);
    CHECK(bd_shell_task_count(sh) == 1);
    CHECK(bd_shell_active_task(sh) &&
          strcmp(bd_shell_active_task(sh), "Writer") == 0);
    CHECK(ln_recent_count(store) >= 1 &&
          strcmp(ln_recent_at(store, 0), "writer.desktop") == 0);

    /* open the launcher via the panel button */
    tk_widget *apps_btn = find_button(sh, "アプリ");
    CHECK(apps_btn != NULL);
    if (apps_btn) click(sh, apps_btn);
    CHECK(bd_shell_view(sh) == BD_VIEW_LAUNCHER);
    bd_shell_layout(sh, 800, 480);
    tk_root_paint(bd_shell_root(sh), cv, tk_theme_default());

    /* category filter (model level) */
    bd_shell_set_category(sh, DE_CAT_NETWORK);
    CHECK(bd_shell_result_count(sh) == 1);
    CHECK(bd_shell_result_at(sh, 0) &&
          strcmp(bd_shell_result_at(sh, 0)->id, "netsurf.desktop") == 0);
    bd_shell_set_category(sh, DE_CAT_ALL);
    CHECK(bd_shell_result_count(sh) == 9);

    /* search filter (model level) */
    bd_shell_set_query(sh, "terminal");
    CHECK(bd_shell_result_count(sh) == 1);
    CHECK(bd_shell_result_at(sh, 0) &&
          strcmp(bd_shell_result_at(sh, 0)->id, "lxterminal.desktop") == 0);
    bd_shell_set_query(sh, "");
    CHECK(bd_shell_result_count(sh) == 9);

    /* activate a result through the model API */
    bd_shell_set_query(sh, "netsurf");
    CHECK(bd_shell_result_count(sh) == 1);
    int before = mock_calls;
    CHECK(bd_shell_activate_result(sh, 0));
    CHECK(mock_calls == before + 1);
    CHECK(strcmp(mock_last, "netsurf.desktop") == 0);
    CHECK(bd_shell_task_count(sh) == 2);
    bd_shell_set_query(sh, "");

    /* task cycling + closing */
    CHECK(bd_shell_cycle_task(sh));
    CHECK(bd_shell_active_task(sh) &&
          strcmp(bd_shell_active_task(sh), "Writer") == 0);
    CHECK(bd_shell_close_task(sh, 0));
    CHECK(bd_shell_task_count(sh) == 1);
    CHECK(!bd_shell_close_task(sh, 9));

    /* quick settings via the tray (UI-REDESIGN 2.3: the tray as
     * a whole is the quick-settings button; the gear now opens
     * the settings app) */
    tk_widget *quick_btn = find_button(sh, "音量");
    CHECK(quick_btn != NULL);
    if (quick_btn) click(sh, quick_btn);
    CHECK(bd_shell_quick_open(sh));
    bd_shell_set_volume(sh, 75);
    CHECK(reg_get_int("shell", "volume", -1) == 75); /* persisted */
    bd_shell_set_wifi(sh, false);
    CHECK(reg_get_bool("shell", "wifi", true) == false);
    CHECK(!bd_shell_wifi(sh));

    /* keyboard settings (M2): defaults from an empty store.
     * The keymap is fixed (TK_KBD_LAYOUT_BRAIN): the built-in
     * keyboard has exactly one layout, so there is no layout
     * toggle in the panel and no layout choice in the store. */
    tk_kbd_config kc;
    bd_shell_kbd_config(sh, &kc);
    CHECK(kc.layout == TK_KBD_LAYOUT_BRAIN);
    CHECK(kc.repeat_delay_ms == 400 && kc.repeat_interval_ms == 50);
    CHECK(kc.repeat_mode == TK_KBD_REPEAT_SYNTHETIC);
    CHECK(kc.longpress_ms == 600);
    /* (Re-layout now that the panel is visible, as the session
     * loop does every frame.) */
    bd_shell_layout(sh, 800, 480);
    CHECK(find_button(sh, "配列: JIS") == NULL);  /* no such toggle */
    /* UI-REDESIGN P2-4: the repeat control left quick settings
     * (it belongs to the settings app); the config is driven
     * through the shell API instead. */
    CHECK(find_button(sh, "リピート: 標準") == NULL);
    {
        const bd_kbd_repeat_preset *pr = bd_kbd_repeat_preset_at(2);
        CHECK(pr && strcmp(pr->name, "速い") == 0);
        kc.repeat_delay_ms = pr->delay_ms;
        kc.repeat_interval_ms = pr->interval_ms;
        bd_shell_set_kbd_config(sh, &kc);
    }
    bd_shell_kbd_config(sh, &kc);
    CHECK(kc.repeat_delay_ms == 250 && kc.repeat_interval_ms == 30);
    CHECK(reg_get_int("keyboard", "repeat_delay_ms", -1) == 250);
    /* the save path records the fixed keymap in the store */
    char *lv = reg_get("keyboard", "layout");
    CHECK(lv && strcmp(lv, "brain") == 0);
    free(lv);
    /* kbdsettings round trip + sanitizing of absurd store values */
    tk_kbd_config kc2;
    bd_kbd_settings_load(&kc2);
    CHECK(kc2.layout == TK_KBD_LAYOUT_BRAIN);
    CHECK(kc2.repeat_delay_ms == 250);
    reg_set("keyboard", "repeat_delay_ms", "99999");
    bd_kbd_settings_load(&kc2);
    CHECK(kc2.repeat_delay_ms == 2000);  /* clamped to the max */
    /* a legacy stored layout value is ignored, never revived */
    reg_set("keyboard", "layout", "us");
    bd_kbd_settings_load(&kc2);
    CHECK(kc2.layout == TK_KBD_LAYOUT_BRAIN);
    reg_set("keyboard", "layout", "brain");

    if (quick_btn) click(sh, quick_btn);
    CHECK(!bd_shell_quick_open(sh));

    /* status setters */
    bd_shell_set_clock(sh, "21:45");
    bd_shell_set_battery(sh, 85, true);
    CHECK(bd_shell_battery(sh) == 85);
    /* the percentage is the battery button's own label: one
     * composite element (icon + %), no separate label widget,
     * and no "+" charging suffix (charging is the glyph bolt) */
    CHECK(find_button(sh, "85%") != NULL);
    CHECK(find_button(sh, "85%+") == NULL);
    bd_shell_layout(sh, 800, 480);
    tk_root_paint(bd_shell_root(sh), cv, tk_theme_default());

    /* notifications */
    CHECK(bd_shell_notify(sh, "ダウンロード完了"));
    CHECK(bd_shell_notify_count(sh) == 1);
    CHECK(bd_shell_notify_at(sh, 0) &&
          strcmp(bd_shell_notify_at(sh, 0), "ダウンロード完了") == 0);
    bd_shell_notify_dismiss(sh);
    CHECK(bd_shell_notify_count(sh) == 0);

    /* pin toggle through the store is reflected in the model */
    CHECK(ln_is_pinned(store, "writer.desktop"));
    CHECK(ln_unpin(store, "writer.desktop"));
    bd_shell_refresh_apps(sh);
    CHECK(!ln_is_pinned(store, "writer.desktop"));

    tk_canvas_free(cv);
    bd_shell_free(sh);
    ln_store_free(store);
    de_catalog_free(cat);

    /* second shell: panel on top via reg setting */
    reg_set("shell", "panel_position", "top");
    cat = de_catalog_new(NULL);
    de_catalog_load_dir(cat, "tests/fixtures/applications");
    store = ln_store_new();
    sh = bd_shell_new(800, 480, cat, store, mock_exec, NULL);
    CHECK(bd_shell_panel_on_top(sh));
    bd_shell_layout(sh, 800, 480);
    tk_widget *apps2 = find_button(sh, "アプリ");
    CHECK(apps2 != NULL);
    if (apps2) {
        tk_rect r = tk_widget_rect(apps2);
        CHECK(r.y < 60); /* panel sits at the top */
    }
    bd_shell_free(sh);
    ln_store_free(store);
    de_catalog_free(cat);

    /* third shell: image wallpaper through the icon cache.
     * Write a 2x2 solid-color uncompressed TGA (stb_image reads
     * TGA): color B=90,G=40,R=200. */
    {
        char tga[512];
        snprintf(tga, sizeof(tga), "%s/wp.tga", userdir);
        FILE *f = fopen(tga, "wb");
        unsigned char hdr[18] = {0};
        hdr[2] = 2;              /* uncompressed true-color */
        hdr[12] = 2; hdr[14] = 2; /* 2x2 */
        hdr[16] = 24;            /* 24bpp */
        hdr[17] = 0x20;          /* top-left origin */
        fwrite(hdr, 1, 18, f);
        for (int i = 0; i < 4; i++) {
            unsigned char bgr[3] = {90, 40, 200};
            fwrite(bgr, 1, 3, f);
        }
        fclose(f);
        reg_set("shell", "wallpaper", tga);
        cat = de_catalog_new(NULL);
        de_catalog_load_dir(cat, "tests/fixtures/applications");
        store = ln_store_new();
        sh = bd_shell_new(800, 480, cat, store, mock_exec, NULL);
        bd_shell_layout(sh, 800, 480);
        tk_canvas *cv2 = tk_canvas_new(800, 480);
        tk_root_paint(bd_shell_root(sh), cv2, tk_theme_default());
        /* panel_position=top persists from the previous scenario,
         * so sample the bottom-right corner of the main area,
         * outside the icon grid */
        tk_color at2 = tk_canvas_pixel(cv2, 795, 475);
        CHECK(at2.r == 200 && at2.g == 40 && at2.b == 90);
        /* the decode went through the shell's cache: one miss,
         * and a second get of the same key is a hit */
        size_t hits, misses, evict;
        ic_cache_stats(bd_shell_icon_cache(sh), &hits, &misses, &evict);
        CHECK(misses >= 1);
        int iw = 0, ih = 0;
        CHECK(ic_get(bd_shell_icon_cache(sh), tga, 800, 480, &iw, &ih) != NULL);
        ic_cache_stats(bd_shell_icon_cache(sh), &hits, &misses, &evict);
        CHECK(hits >= 1);
        tk_canvas_free(cv2);
        bd_shell_free(sh);
        ln_store_free(store);
        de_catalog_free(cat);
    }

    /* fourth shell: empty catalog (SD absent at boot) still
     * builds, lays out and paints */
    {
        de_catalog *ec = de_catalog_new(NULL);
        ln_store *es = ln_store_new();
        bd_shell *esh = bd_shell_new(800, 480, ec, es, mock_exec, NULL);
        CHECK(esh != NULL);
        bd_shell_layout(esh, 800, 480);
        tk_canvas *cv3 = tk_canvas_new(800, 480);
        tk_root_paint(bd_shell_root(esh), cv3, tk_theme_default());
        CHECK(bd_shell_result_count(esh) == 0);
        CHECK(bd_shell_view(esh) == BD_VIEW_DESKTOP);
        tk_canvas_free(cv3);
        bd_shell_free(esh);
        ln_store_free(es);
        de_catalog_free(ec);
    }

    /* fifth shell: mute semantics (REPORT-mute.md). The set
     * level is preserved across mute, the backend is driven to
     * the effective level, and an explicit level change to a
     * different value unmutes. */
    {
        reg_set("shell", "volume", "60");
        reg_set("shell", "muted", "false");
        de_catalog *mc = de_catalog_new(NULL);
        ln_store *ms = ln_store_new();
        bd_shell *msh = bd_shell_new(800, 480, mc, ms, mock_exec, NULL);
        CHECK(msh != NULL);
        bd_shell_layout(msh, 800, 480);
        bd_backend *mbe = bd_backend_mock_new();
        bd_shell_attach_backend(msh, mbe);

        CHECK(!bd_shell_muted(msh));
        CHECK(bd_shell_volume(msh) == 60);
        CHECK(bd_shell_effective_volume(msh) == 60);

        /* mute: level preserved, effective 0, backend at 0,
         * both reg keys reflect exactly that split */
        bd_shell_set_muted(msh, true);
        CHECK(bd_shell_muted(msh));
        CHECK(bd_shell_volume(msh) == 60);
        CHECK(bd_shell_effective_volume(msh) == 0);
        CHECK(bd_backend_volume_get(mbe) == 0);
        CHECK(reg_get_bool("shell", "muted", false) == true);
        CHECK(reg_get_int("shell", "volume", -1) == 60);

        /* backend poll while muted must not read the hardware
         * 0 back into the set level */
        (void)bd_shell_poll_backend(msh);
        CHECK(bd_shell_volume(msh) == 60);
        CHECK(bd_shell_muted(msh));

        /* setting the SAME level while muted keeps mute */
        bd_shell_set_volume(msh, 60);
        CHECK(bd_shell_muted(msh));
        CHECK(bd_shell_effective_volume(msh) == 0);
        CHECK(bd_backend_volume_get(mbe) == 0);

        /* a level change while muted unmutes and applies */
        bd_shell_set_volume(msh, 70);
        CHECK(!bd_shell_muted(msh));
        CHECK(bd_shell_volume(msh) == 70);
        CHECK(bd_shell_effective_volume(msh) == 70);
        CHECK(bd_backend_volume_get(mbe) == 70);
        CHECK(reg_get_bool("shell", "muted", true) == false);

        /* unmute restores the preserved level at the backend */
        bd_shell_set_muted(msh, true);
        CHECK(bd_backend_volume_get(mbe) == 0);
        bd_shell_set_muted(msh, false);
        CHECK(!bd_shell_muted(msh));
        CHECK(bd_backend_volume_get(mbe) == 70);

        /* a redundant set_muted is a no-op (no backend write) */
        int w0 = bd_backend_mock_writes(mbe);
        bd_shell_set_muted(msh, false);
        CHECK(bd_backend_mock_writes(mbe) == w0);

        /* mute persists across shell recreation (reg-backed);
         * the creation-time same-level set_volume must not
         * clear it */
        bd_shell_set_muted(msh, true);
        bd_shell_free(msh);
        bd_backend_free(mbe);
        bd_shell *msh2 = bd_shell_new(800, 480, mc, ms, mock_exec, NULL);
        CHECK(msh2 != NULL);
        CHECK(bd_shell_muted(msh2));
        CHECK(bd_shell_volume(msh2) == 70);
        CHECK(bd_shell_effective_volume(msh2) == 0);
        bd_shell_free(msh2);
        ln_store_free(ms);
        de_catalog_free(mc);
    }

    /* --- Wi-Fi state reaches the shell model (fake supplicant):
     * connection + the live SIGNAL_POLL reading that drives the
     * tray fan's fill. (cat/store above were freed by an earlier
     * block, so these two blocks use their own catalog/store.) --- */
    de_catalog *wcat = de_catalog_new(NULL);
    de_catalog_load_dir(wcat, "tests/fixtures/applications");
    ln_store *wstore = ln_store_new();
    {
        char dir[] = "/tmp/bdwifishellXXXXXX";
        CHECK(mkdtemp(dir) != NULL);
        char sock[256], logp[256];
        snprintf(sock, sizeof(sock), "%s/wpa.sock", dir);
        snprintf(logp, sizeof(logp), "%s/cmds.log", dir);
        setenv("BD_WPA_LOCAL_DIR", dir, 1);
        struct stat st;
        pid_t pid = fork();
        if (pid == 0) {
            char *args[] = { "./fake_wpa", sock, logp, NULL };
            execv(args[0], args);
            _exit(127);
        }
        int tries = 0;
        while (stat(sock, &st) != 0 && tries++ < 300) usleep(10000);
        CHECK(stat(sock, &st) == 0);
        bd_wifi *wctl = bd_wifi_new(sock);
        bd_shell *wsh = bd_shell_new(800, 480, wcat, wstore, mock_exec, NULL);
        CHECK(wsh != NULL);
        bd_shell_attach_wifi(wsh, wctl); /* applies pref + polls */
        CHECK(bd_shell_wifi_connected(wsh));
        CHECK(strcmp(bd_shell_wifi_ssid(wsh), "TestNet") == 0);
        CHECK(bd_shell_wifi_signal_dbm(wsh) == -42);
        bd_shell_free(wsh);
        bd_wifi_free(wctl);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);

        /* an unconnected supplicant: no connection, no reading */
        setenv("FAKE_WPA_STATE", "DISCONNECTED", 1);
        char sock2[256], logp2[256];
        snprintf(sock2, sizeof(sock2), "%s/wpa2.sock", dir);
        snprintf(logp2, sizeof(logp2), "%s/cmds2.log", dir);
        pid = fork();
        if (pid == 0) {
            char *args[] = { "./fake_wpa", sock2, logp2, NULL };
            execv(args[0], args);
            _exit(127);
        }
        tries = 0;
        while (stat(sock2, &st) != 0 && tries++ < 300) usleep(10000);
        bd_wifi *wctl2 = bd_wifi_new(sock2);
        bd_shell *wsh2 = bd_shell_new(800, 480, wcat, wstore, mock_exec, NULL);
        CHECK(wsh2 != NULL);
        bd_shell_attach_wifi(wsh2, wctl2);
        CHECK(!bd_shell_wifi_connected(wsh2));
        CHECK(bd_shell_wifi_signal_dbm(wsh2) == 0);
        bd_shell_free(wsh2);
        bd_wifi_free(wctl2);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        unsetenv("FAKE_WPA_STATE");
    }

    /* --- Android-mode 3-button navigation + app switcher ---
     * (the buttons exist only under the style-1 theme) */
    {
        tk_theme_set_default(&tk_theme_v2);
        bd_shell *nsh = bd_shell_new(800, 480, wcat, wstore, mock_exec, NULL);
        CHECK(nsh != NULL);
        bd_shell_layout(nsh, 800, 480);
        CHECK(find_button(nsh, "戻る") != NULL);
        CHECK(find_button(nsh, "ホーム") != NULL);
        CHECK(find_button(nsh, "最近使ったアプリ") != NULL);

        nav_sink_count = 0; nav_sink_last = -1;
        win_list_calls = 0; win_activated = 0; win_closed = 0;
        bd_shell_set_nav_sink(nsh, test_nav_sink, NULL);
        bd_shell_set_win_ops(nsh, &test_win_ops, NULL);

        /* Back with nothing open reaches the sink (the session
         * would deliver TK_KEY_BACK to the focused window) */
        bd_shell_nav(nsh, BD_NAV_BACK);
        CHECK(nav_sink_count == 1 && nav_sink_last == BD_NAV_BACK);
        /* Back closes an open shell panel first */
        bd_shell_set_quick_open(nsh, true);
        bd_shell_nav(nsh, BD_NAV_BACK);
        CHECK(!bd_shell_quick_open(nsh));
        CHECK(nav_sink_count == 1);
        /* Home: desktop view + sink (session minimizes windows) */
        bd_shell_show_launcher(nsh, true);
        bd_shell_nav(nsh, BD_NAV_HOME);
        CHECK(nav_sink_count == 2 && nav_sink_last == BD_NAV_HOME);
        CHECK(bd_shell_view(nsh) == BD_VIEW_DESKTOP);

        /* Recents: lists the bridge's windows, newest first;
         * a card tap activates, the x asks the window to exit */
        bd_shell_nav(nsh, BD_NAV_RECENTS);
        CHECK(bd_shell_recents_open(nsh));
        CHECK(win_list_calls >= 1);
        bd_shell_layout(nsh, 800, 480);
        tk_widget *card = find_button(nsh, "AppA");
        CHECK(card != NULL);
        if (card) click(nsh, card);
        CHECK(win_activated == 101);
        CHECK(!bd_shell_recents_open(nsh));
        bd_shell_set_recents_open(nsh, true);
        bd_shell_layout(nsh, 800, 480);
        tk_widget *xb = find_button(nsh, "×");
        CHECK(xb != NULL);
        if (xb) click(nsh, xb);
        CHECK(win_closed == 101);
        bd_shell_free(nsh);
        tk_theme_set_default(NULL);
    }
    ln_store_free(wstore);
    de_catalog_free(wcat);

    printf("test_shell: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
