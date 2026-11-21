/* test_recorder_ui - drives the REAL recorder UI with a synth
 * source + null sink injected: record through the record
 * button, tick to pump, stop, verify the library list shows
 * the take; play it back through the play button to EOF;
 * delete -> recently-deleted tab -> restore -> purge.
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_recorder.h"
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static rc_source *mk_src(void *ud)
{
    (void)ud;
    return rc_source_synth(440, RC_RATE, RC_CHANNELS);
}
static rc_sink *mk_sink(void *ud)
{
    (void)ud;
    return rc_sink_null(RC_RATE, RC_CHANNELS);
}

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

/* click the first row of a list widget */
static void click_row0(tk_root *root, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + 10;
    ev.y = r.y + 4;
    tk_root_event(root, &ev);
    ev.type = TK_EV_POINTER_UP;
    tk_root_event(root, &ev);
}

int main(void)
{
    char tmpl[] = "/tmp/bdrecui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    rec_app *app = rec_app_new(tmpl, mk_src, mk_sink, NULL, 800, 600);
    CHECK(app != NULL, "app created");
    tk_root *root = rec_app_root(app);

    /* record through the real button */
    click(root, rec_app_record_button(app));
    CHECK(rec_app_recording(app), "recording after record click");
    for (int i = 0; i < 4; i++) rec_app_tick(app, (uint64_t)i * 100);
    click(root, rec_app_rec_pause_button(app));
    for (int i = 0; i < 2; i++) rec_app_tick(app, 500 + (uint64_t)i * 100);
    click(root, rec_app_rec_pause_button(app));      /* resume */
    for (int i = 0; i < 2; i++) rec_app_tick(app, 800 + (uint64_t)i * 100);
    click(root, rec_app_rec_stop_button(app));
    CHECK(!rec_app_recording(app), "stopped");
    CHECK(rec_app_selected(app) &&
          strcmp(rec_app_selected(app), "新規録音 1") == 0,
          "new take selected, got %s",
          rec_app_selected(app) ? rec_app_selected(app) : "(null)");
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 1, "list shows 1 take");
    CHECK(strstr(tk_label_text(rec_app_status_label(app)), "保存") != NULL,
          "status reports saved: %s", tk_label_text(rec_app_status_label(app)));

    /* play to EOF */
    click(root, rec_app_play_button(app));
    CHECK(rec_app_playing(app), "playing after play click");
    for (int i = 0; i < 200 && rec_app_playing(app); i++)
        rec_app_tick(app, 2000 + (uint64_t)i * 100);
    CHECK(!rec_app_playing(app), "playback reached EOF and stopped");

    /* search filters (no match -> empty list) */
    tk_textfield_set_text(rec_app_search_field(app), "存在しない");
    tk_widget_invalidate(rec_app_search_field(app));
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 0,
          "search filters list to 0");
    tk_textfield_set_text(rec_app_search_field(app), "");
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 1, "search cleared");

    /* delete -> recently deleted tab -> restore */
    click(root, rec_app_delete_button(app));
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 0,
          "list empty after delete");
    click(root, rec_app_tab_widget(app, 1));
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 1,
          "deleted tab shows the take");
    click_row0(root, rec_app_list_widget(app));
    click(root, rec_app_restore_button(app));
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 0,
          "deleted tab empty after restore");
    click(root, rec_app_tab_widget(app, 0));
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 1,
          "take restored to library");

    /* delete again and purge for good */
    click_row0(root, rec_app_list_widget(app));
    click(root, rec_app_delete_button(app));
    click(root, rec_app_tab_widget(app, 1));
    click_row0(root, rec_app_list_widget(app));
    click(root, rec_app_purge_button(app));
    CHECK(tk_listview_count(rec_app_list_widget(app)) == 0,
          "purged from deleted tab");

    rec_app_free(app);
    printf("test_recorder_ui: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
