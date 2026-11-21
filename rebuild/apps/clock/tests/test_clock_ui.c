/* test_clock_ui - bd-clock UI drive tests (headless).
 *
 * Drives the real UI: tab structure, world-clock add/remove,
 * alarm creation through the editor widgets + toggle without
 * the editor, ringing banner + snooze/dismiss through ticks,
 * stopwatch start/lap via ticks, timer create/start/fire, and
 * the damage-presentation discipline.
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_clock.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_int(const char *what, long got, long want)
{
    if (got == want) { passes++; printf("PASS %s -> %ld\n", what, got); }
    else { fails++; printf("FAIL %s: got %ld, want %ld\n", what, got, want); }
}

#define OFF 540
static int64_t local_epoch(int y, int mo, int d, int h, int mi, int s)
{
    return ck_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s
           - (int64_t)OFF * 60;
}

int main(void)
{
    char tmpl[] = "/tmp/bdclock-ui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    ck_store *s = ck_open(tmpl);
    check_true("U open", s != NULL);

    clock_app *app = clock_app_new(s, OFF, 480, 800);
    check_true("U build", app != NULL);
    check_int("U default tab (world)", clock_app_tab(app), 0);
    check_int("U seed city rows", (long)clock_app_city_rows(app), 5);

    int64_t w0 = local_epoch(2026, 11, 20, 6, 59, 0);
    check_int("U tick quiet", clock_app_tick(app, w0, 0), 0);
    check_int("U city list count",
              (long)tk_listview_count(clock_app_city_list(app)), 5);

    /* add a city through the combobox + action (Paris is not
     * in the seed list) */
    int paris = -1;
    for (size_t i = 0; i < ck_builtin_count(); i++)
        if (strcmp(ck_builtin_at(i)->name, "パリ") == 0)
            paris = (int)i;
    check_true("U paris in builtin", paris >= 0);
    tk_combobox_select(clock_app_city_combo(app), paris);
    check_int("U add city", clock_app_add_chosen_city(app), 0);
    check_int("U city rows 6", (long)clock_app_city_rows(app), 6);
    check_int("U remove city", clock_app_remove_city_at(app, 5), 0);
    check_int("U city rows 5", (long)clock_app_city_rows(app), 5);

    /* create an alarm through the editor widgets: 07:00, label */
    clock_app_set_tab(app, 1);
    check_int("U tab alarms", clock_app_tab(app), 1);
    tk_textfield_set_text(clock_app_editor_hour(app), "07");
    tk_textfield_set_text(clock_app_editor_min(app), "00");
    tk_textfield_set_text(clock_app_editor_label(app), "UI朝");
    uint64_t aid = clock_app_editor_new(app);
    check_true("U alarm created", aid != 0);
    check_int("U alarm rows", (long)clock_app_alarm_rows(app), 1);
    check_int("U alarm id at row",
              (long)clock_app_alarm_id_at(app, 0), (long)aid);
    const ck_alarm *a = ck_alarm_find(s, aid);
    check_true("U alarm fields",
               a && a->hour == 7 && a->min == 0 &&
               strcmp(a->label, "UI朝") == 0 && a->enabled);

    /* toggle OFF without the editor, then ON again */
    check_int("U toggle off", clock_app_toggle_alarm_row(app, 0), 0);
    check_true("U model disabled", !ck_alarm_find(s, aid)->enabled);
    check_int("U toggle on", clock_app_toggle_alarm_row(app, 0), 0);
    check_true("U model enabled", ck_alarm_find(s, aid)->enabled);

    /* editor save round-trip: change the label */
    clock_app_select_alarm_row(app, 0);
    tk_textfield_set_text(clock_app_editor_label(app), "UI朝2");
    check_int("U editor save", clock_app_editor_save(app), 0);
    check_true("U label saved",
               strcmp(ck_alarm_find(s, aid)->label, "UI朝2") == 0);

    /* the alarm fires through ticks; banner shows; dismiss */
    check_int("U tick to fire",
              clock_app_tick(app, w0 + 61, 61000), 1);
    check_true("U ringing", clock_app_ringing(app));
    check_true("U banner visible text",
               tk_label_text(clock_app_status_label(app))[0] != 0);
    check_int("U dismiss", clock_app_dismiss(app), 0);
    check_true("U not ringing", !clock_app_ringing(app));

    /* stopwatch through ticks */
    clock_app_set_tab(app, 2);
    int64_t mono = 61000;
    clock_app_sw_toggle(app, mono);            /* start */
    check_true("U sw running", clock_app_sw_running(app));
    mono += 30000;
    clock_app_tick(app, w0 + 61 + 30, mono);
    clock_app_sw_lap_or_reset(app, mono);      /* lap */
    check_int("U sw laps", clock_app_sw_laps(app), 1);
    mono += 10000;
    clock_app_tick(app, w0 + 61 + 40, mono);
    clock_app_sw_toggle(app, mono);            /* stop */
    check_true("U sw stopped", !clock_app_sw_running(app));
    clock_app_sw_lap_or_reset(app, mono);      /* reset while stopped */
    check_int("U sw laps cleared", clock_app_sw_laps(app), 0);

    /* timer: create 0:01:00 through editor widgets, start, fire */
    clock_app_set_tab(app, 3);
    tk_textfield_set_text(clock_app_timer_h(app), "0");
    tk_textfield_set_text(clock_app_timer_m(app), "1");
    tk_textfield_set_text(clock_app_timer_s(app), "0");
    tk_textfield_set_text(clock_app_timer_label(app), "UIタイマー");
    uint64_t tid = clock_app_editor_new_timer(app);
    check_true("U timer created", tid != 0);
    check_int("U timer rows", (long)clock_app_timer_rows(app), 1);
    int64_t tw = w0 + 61 + 40;
    check_int("U timer start",
              clock_app_timer_start(app, 0, tw, mono), 0);
    check_int("U timer quiet",
              clock_app_tick(app, tw + 59, mono + 59000), 0);
    check_int("U timer fires",
              clock_app_tick(app, tw + 60, mono + 60000), 1);
    check_true("U timer ringing", clock_app_ringing(app));
    check_int("U timer dismiss", clock_app_dismiss(app), 0);
    check_true("U timer not ringing", !clock_app_ringing(app));

    /* paint + damage discipline */
    {
        tk_font *font = tk_font_load("../fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load("fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load_default();
        if (font) tk_font_install(font);
        tk_canvas *canvas = tk_canvas_new(480, 800);
        tk_fb *fb = tk_fb_open_mem(480, 800, 16);
        const tk_theme *theme = tk_theme_default();
        tk_root_paint_damage(clock_app_root(app), canvas, theme);
        tk_fb_flush_damage(fb, canvas);
        check_int("D drained", (long)tk_fb_flush_damage(fb, canvas), 0);
        size_t total = 480u * 800u * 4u;
        uint32_t *before = malloc(total);
        memcpy(before, tk_canvas_pixels(canvas), total);
        clock_app_tick(app, tw + 61, mono + 61000);  /* setter-only */
        int painted = tk_root_paint_damage(clock_app_root(app), canvas,
                                           theme);
        size_t shipped = tk_fb_flush_damage(fb, canvas);
        check_true("D painted after tick", painted > 0);
        check_true("D bytes shipped", shipped > 0);
        check_true("D pixels changed",
                   memcmp(before, tk_canvas_pixels(canvas), total) != 0);
        free(before);
        tk_fb_close(fb);
        tk_canvas_free(canvas);
        if (font) tk_font_free(font);
    }

    clock_app_free(app);
    ck_close(s);
    printf("test_clock_ui: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
