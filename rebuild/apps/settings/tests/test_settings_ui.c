/* test_settings_ui - drives the REAL settings UI of the
 * 2026-11-21 rebuild (DESIGN-settings-rebuild.md):
 *
 *   Android mode: homepage (category list with live
 *   summaries) -> drill into a page -> in-row editors write
 *   through to the mock backend and brainreg; search covers
 *   every row of every page and jumps to the hit's row.
 *
 *   WinUI mode: the navigation drawer selects pages, the
 *   rows are cards, and the same model writes land in the
 *   same stores.
 *
 * Row selection goes through settings_app_select_row - the
 * same state a tap on the row's control produces - and the
 * slider driven here is the selected row's own inline
 * slider (settings_app_editor_slider).
 *
 * When BD_SETTINGS_SHOT_DIR is set, raw frames of the main
 * views are dumped there (the shot_common format) so the
 * rebuild's screens can be eyeballed without touching the
 * screenshots harness.
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_settings.h"
#include "kbdsettings.h"
#include "reg.h"
#include "tk_kbd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static void click(tk_root *root, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
    ev.type = TK_EV_POINTER_UP;
    tk_root_event(root, &ev);
}

/* focus the widget with a click, then type into it through
 * the real text-input event path (which is what fires the
 * search bar's change callback) */
static void type_text(tk_root *root, tk_widget *w, const char *s)
{
    click(root, w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT;
    ev.text = s;
    tk_root_event(root, &ev);
}

static void dump_shot(settings_app *app, const char *dir,
                      const char *name, int w, int h)
{
    if (!dir) return;
    tk_canvas *cv = tk_canvas_new(w, h);
    if (!cv) return;
    tk_root_paint(settings_app_root(app), cv,
                  tk_theme_default());
    uint32_t *px = tk_canvas_pixels(cv);
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (f) {
        uint32_t hdr[2] = { (uint32_t)w, (uint32_t)h };
        fwrite(hdr, 4, 2, f);
        for (int i = 0; i < w * h; i++) {
            uint8_t rgb[3] = { (uint8_t)(px[i] >> 16),
                               (uint8_t)(px[i] >> 8),
                               (uint8_t)px[i] };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("shot: %s\n", path);
    }
    tk_canvas_free(cv);
}

static const char *shot_dir(void)
{
    const char *d = getenv("BD_SETTINGS_SHOT_DIR");
    return (d && *d) ? d : NULL;
}

int main(void)
{
    char tmpl[] = "/tmp/bdsettingsui-XXXXXX";
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
    char p[1200];
    snprintf(p, sizeof(p), "%s/meminfo", proc);
    FILE *f = fopen(p, "w");
    fputs("MemTotal: 131072 kB\nMemAvailable: 65536 kB\n", f);
    fclose(f);
    snprintf(p, sizeof(p), "%s/mounts", proc);
    f = fopen(p, "w");
    fputs("/dev/root / ext4 rw 0 0\n", f);
    fclose(f);

    /* the bundled product font, when run from the tree root
     * (the screenshot path); harmless when absent */
    tk_font *font = tk_font_load_bundled();
    if (font) tk_font_install(font);

    bd_backend *be = bd_backend_mock_new();
    bd_backend_mock_set(be, 40, 30, 55, false);
    sy_sys *sys = sy_open(proc, logs);

    /* ============ Android mode (product theme) ============ */
    tk_theme_set_default(&tk_theme_v2); /* the product default
                                         * every app main sets */
    settings_app *app = settings_app_new(be, NULL, sys, 854, 480);
    CHECK(app != NULL, "app created (android mode)");
    tk_root *root = settings_app_root(app);
    CHECK(settings_app_page(app) == SETTINGS_PAGE_HOME,
          "starts on the homepage");
    CHECK(tk_listview_count(settings_app_page_list(app)) ==
          SET_PAGE_COUNT,
          "all %d categories on the homepage", SET_PAGE_COUNT);
    CHECK(settings_app_rows(app) == SET_PAGE_COUNT,
          "homepage rows = categories");

    /* drill into the keyboard page and edit in place */
    settings_app_set_page(app, SET_PAGE_KEYBOARD);
    CHECK(settings_app_page(app) == SET_PAGE_KEYBOARD,
          "keyboard page open");
    CHECK(settings_app_rows(app) == 6,
          "keyboard rows incl. repeat_source: %zu",
          settings_app_rows(app));
    /* rows: preset 0, repeat_source 1, delay 2, interval 3,
     * longpress 4, layout 5 */
    settings_app_select_row(app, 2);  /* delay */
    tk_slider_set_value(settings_app_editor_slider(app), 1.0f);
    tk_kbd_config cfg;
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_delay_ms == 1000,
          "delay via UI slider = %d", cfg.repeat_delay_ms);
    settings_app_select_row(app, 0);  /* preset */
    settings_app_activate_row(app);
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_delay_ms == 600 && cfg.repeat_interval_ms == 80,
          "preset cycled to 遅い (%d/%d)",
          cfg.repeat_delay_ms, cfg.repeat_interval_ms);
    /* repeat_source via activation (cycle) */
    settings_app_select_row(app, 1);
    settings_app_activate_row(app);
    bd_kbd_settings_load(&cfg);
    CHECK(cfg.repeat_mode == TK_KBD_REPEAT_KERNEL,
          "repeat_source cycled to kernel via UI");

    /* display page: brightness through the row's own slider */
    settings_app_set_page(app, SET_PAGE_DISPLAY);
    settings_app_select_row(app, 0);
    dump_shot(app, shot_dir(), "settings_new_android_display.raw",
              854, 480);
    tk_slider_set_value(settings_app_editor_slider(app), 0.8f);
    CHECK(bd_backend_brightness_get(be) == 80,
          "brightness via UI = %d", bd_backend_brightness_get(be));

    /* sound page: mute toggle drives the backend to 0 and
     * back, through the same store the shell uses */
    settings_app_set_page(app, SET_PAGE_SOUND);
    CHECK(settings_app_rows(app) == 2, "sound rows: volume+mute");
    settings_app_select_row(app, 1);  /* mute */
    settings_app_activate_row(app);
    CHECK(reg_get_bool("shell", "muted", false),
          "mute on via UI");
    CHECK(bd_backend_volume_get(be) == 0,
          "backend at 0 while muted");
    settings_app_activate_row(app);
    CHECK(!reg_get_bool("shell", "muted", false),
          "mute off via UI");
    CHECK(bd_backend_volume_get(be) == 30,
          "unmute restored 30, got %d",
          bd_backend_volume_get(be));

    /* personal page: the display-mode switch writes the
     * brainreg mode key (design section 6) */
    settings_app_set_page(app, SET_PAGE_PERSONAL);
    CHECK(settings_app_rows(app) == 3,
          "personal rows: mode/wallpaper/panel");
    settings_app_select_row(app, 0);  /* ui_mode */
    settings_app_activate_row(app);
    {
        char *mv = reg_get("ui", "mode");
        CHECK(mv && strcmp(mv, "winui") == 0,
              "ui_mode -> winui via UI");
        free(mv);
    }
    settings_app_activate_row(app);
    {
        char *mv = reg_get("ui", "mode");
        CHECK(mv && strcmp(mv, "android") == 0,
              "ui_mode -> android via UI");
        free(mv);
    }
    dump_shot(app, shot_dir(), "settings_new_android_personal.raw",
              854, 480);

    /* storage + about pages render their rows */
    settings_app_set_page(app, SET_PAGE_STORAGE);
    CHECK(settings_app_rows(app) >= 1, "storage rows %zu",
          settings_app_rows(app));
    settings_app_set_page(app, SET_PAGE_ABOUT);
    CHECK(settings_app_rows(app) >= 3, "about rows %zu",
          settings_app_rows(app));

    /* mascot page: toggle writes through to brainreg */
    settings_app_set_page(app, SET_PAGE_MASCOT);
    CHECK(settings_app_rows(app) == 5, "mascot rows %zu",
          settings_app_rows(app));
    settings_app_select_row(app, 0);   /* enabled toggle */
    settings_app_activate_row(app);
    CHECK(!reg_get_bool("mascot", "enabled", true),
          "mascot disabled via UI");

    /* search: from the homepage, covering every page's rows;
     * a hit jumps to its page with the row selected */
    settings_app_set_page(app, SETTINGS_PAGE_HOME);
    CHECK(settings_app_page(app) == SETTINGS_PAGE_HOME,
          "back on the homepage");
    dump_shot(app, shot_dir(), "settings_new_android_home.raw",
              854, 480);
    type_text(root, settings_app_search_field(app), "輝度");
    CHECK(settings_app_rows(app) >= 1,
          "search hits for 輝度: %zu", settings_app_rows(app));
    settings_app_select_row(app, 0);
    settings_app_activate_row(app);
    CHECK(settings_app_page(app) == SET_PAGE_DISPLAY,
          "search jump landed on display, got %d",
          settings_app_page(app));
    CHECK(settings_app_editor_slider(app) != NULL,
          "the brightness row is selected after the jump");
    settings_app_free(app);

    /* ============ WinUI mode ============ */
    tk_theme_set_default(&tk_theme_winui);
    app = settings_app_new(be, NULL, sys, 854, 480);
    CHECK(app != NULL, "app created (winui mode)");
    root = settings_app_root(app);
    CHECK(settings_app_page(app) == SET_PAGE_DISPLAY,
          "winui opens on display, got %d",
          settings_app_page(app));
    CHECK(tk_drawer_selected(settings_app_page_list(app)) ==
          SET_PAGE_DISPLAY, "nav selection follows the page");
    settings_app_select_row(app, 0);
    dump_shot(app, shot_dir(), "settings_new_winui_display.raw",
              854, 480);
    tk_slider_set_value(settings_app_editor_slider(app), 0.5f);
    CHECK(bd_backend_brightness_get(be) == 50,
          "winui brightness via card slider = %d",
          bd_backend_brightness_get(be));
    /* nav-driven page switch */
    settings_app_set_page(app, SET_PAGE_SOUND);
    CHECK(tk_drawer_selected(settings_app_page_list(app)) ==
          SET_PAGE_SOUND, "nav selection moved to sound");
    settings_app_select_row(app, 0);
    tk_slider_set_value(settings_app_editor_slider(app), 0.6f);
    CHECK(bd_backend_volume_get(be) == 60,
          "winui volume via card slider = %d",
          bd_backend_volume_get(be));
    /* personal page in winui: mode row present, cycle works */
    settings_app_set_page(app, SET_PAGE_PERSONAL);
    CHECK(settings_app_rows(app) == 3, "winui personal rows");
    dump_shot(app, shot_dir(), "settings_new_winui_personal.raw",
              854, 480);
    settings_app_select_row(app, 0);
    settings_app_activate_row(app);
    {
        char *mv = reg_get("ui", "mode");
        CHECK(mv && strcmp(mv, "winui") == 0,
              "winui ui_mode -> winui via UI");
        free(mv);
    }
    /* search replaces the content pane; a hit jumps */
    type_text(root, settings_app_search_field(app), "音量");
    CHECK(settings_app_rows(app) >= 1,
          "winui search hits for 音量: %zu",
          settings_app_rows(app));
    dump_shot(app, shot_dir(), "settings_new_winui_search.raw",
              854, 480);
    settings_app_select_row(app, 0);
    settings_app_activate_row(app);
    CHECK(settings_app_page(app) == SET_PAGE_SOUND,
          "winui search jump landed on sound, got %d",
          settings_app_page(app));
    settings_app_free(app);
    tk_theme_set_default(NULL);

    sy_close(sys);
    bd_backend_free(be);
    printf("test_settings_ui: %d passed, %d failed\n",
           passes, fails);
    return fails ? 1 : 0;
}
