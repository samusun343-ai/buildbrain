/* test_player_ui - drives the REAL player UI with a null
 * sink factory: scan a media dir, play a track from the list,
 * tick through end-of-track auto-advance, use the Up Next
 * queue, drill into the Albums view, create a playlist and
 * add the selected track, cycle repeat, and verify the
 * playback state is restored by a fresh app instance.
 *
 * argv[1] = fixtures directory.
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_player.h"

#include <math.h>
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

static pe_out *mk_out(void *ud, uint32_t rate, uint16_t ch)
{
    (void)ud;
    return pe_out_null(rate, ch);
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

/* The five views are segments of one tab bar (SS7.3), not
 * buttons: click the centre of segment idx. Driving-only
 * change; the checks below are unchanged. */
static void click_tab(tk_root *root, player_app *app, int idx)
{
    tk_widget *tab = player_app_tabbar(app);
    tk_rect r = tk_widget_rect(tab);
    int seg = r.w / 5;
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + seg * idx + seg / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
    ev.type = TK_EV_POINTER_UP;
    tk_root_event(root, &ev);
}

static void copy_file(const char *src, const char *dst)
{
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
}

static void write_wav(const char *path)
{
    const int rate = 22050;
    FILE *f = fopen(path, "wb");
    uint32_t frames = rate / 2;    /* 0.5 s */
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

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s FIXTURES\n", argv[0]); return 2; }
    const char *fx = argv[1];
    char tmpl[] = "/tmp/bdplayerui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    char media[1100], data[1100], p[1200], q[1200];
    snprintf(media, sizeof(media), "%s/media", tmpl);
    snprintf(data, sizeof(data), "%s/appdata", tmpl);
    mkdir(media, 0777);
    snprintf(p, sizeof(p), "%s/fixture.mp3", fx);
    snprintf(q, sizeof(q), "%s/a_song.mp3", media);
    copy_file(p, q);
    snprintf(p, sizeof(p), "%s/fixture.ogg", fx);
    snprintf(q, sizeof(q), "%s/b_song.ogg", media);
    copy_file(p, q);
    snprintf(q, sizeof(q), "%s/c_tone.wav", media);
    write_wav(q);

    const char *dirs[] = { media };
    player_app *app = player_app_new(data, dirs, 1, mk_out, NULL, 800, 600);
    CHECK(app != NULL, "app created");
    tk_root *root = player_app_root(app);
    CHECK(player_app_rows(app) == 3, "3 tracks listed, got %zu",
          player_app_rows(app));
    CHECK(tk_listview_count(player_app_list_widget(app)) == 3,
          "listview count 3");

    /* play the first row (sorted by title: c_tone.wav's title
     * is "c_tone"; the two tagged tracks sort by their tags) */
    tk_listview_select(player_app_list_widget(app), 0);
    click(root, player_app_play_button(app));
    CHECK(player_app_playing(app), "playing after play click");
    CHECK(strcmp(tk_button_label(player_app_play_button(app)),
                 "⏸ 一時停止") == 0,
          "play button shows pause while playing: %s",
          tk_button_label(player_app_play_button(app)));
    const char *now1 = player_app_now_path(app);
    CHECK(now1 != NULL, "now path set");
    CHECK(strlen(tk_label_text(player_app_np_title(app))) > 0,
          "np title shown: %s", tk_label_text(player_app_np_title(app)));

    /* tick: first track ends -> auto-advance to a different one */
    char first[1024];
    snprintf(first, sizeof(first), "%s", now1);
    for (int i = 0; i < 400 && player_app_playing(app); i++) {
        player_app_tick(app, (uint64_t)i * 50);
        const char *np = player_app_now_path(app);
        if (np && strcmp(np, first) != 0) break;
    }
    CHECK(player_app_now_path(app) &&
          strcmp(player_app_now_path(app), first) != 0,
          "auto-advanced to %s",
          player_app_now_path(app) ? player_app_now_path(app) : "(null)");

    /* queue: queue the WAV for later, then skip to it.
     * Title sort is bytewise: "c_tone" (ASCII) precedes the
     * Japanese titles (UTF-8 lead byte 0xE3), so the WAV is
     * row 0, the MP3 row 1, the OGG row 2. */
    tk_listview_select(player_app_list_widget(app), 0);
    click(root, player_app_queue_later_button(app));
    CHECK(tk_listview_count(player_app_queue_widget(app)) == 1,
          "queue holds 1");
    click(root, player_app_next_button(app));
    CHECK(player_app_now_path(app) &&
          strstr(player_app_now_path(app), "c_tone.wav") != NULL,
          "next jumped to queued wav: %s",
          player_app_now_path(app) ? player_app_now_path(app) : "(null)");
    CHECK(tk_listview_count(player_app_queue_widget(app)) == 0,
          "queue drained");

    /* albums view: one album value, drill into 2 tracks */
    click_tab(root, app, 1);
    CHECK(player_app_view(app) == 1, "albums view");
    CHECK(player_app_rows(app) == 1, "one album row, got %zu",
          player_app_rows(app));
    player_app_open_row(app, 0);
    CHECK(player_app_rows(app) == 2, "album has 2 tracks, got %zu",
          player_app_rows(app));
    player_app_back(app);
    CHECK(player_app_rows(app) == 1, "back to album values");

    /* playlists: create + add selected track + drill.
     * New flow (SS7.3): the dialog opens from the context
     * bar's add cell; checks unchanged. */
    click_tab(root, app, 0);
    tk_listview_select(player_app_list_widget(app), 0);
    click(root, player_app_add_to_playlist_button(app));
    tk_textfield_set_text(player_app_list_name_field(app), "マイリスト");
    click(root, player_app_list_create_button(app));
    click(root, player_app_list_add_button(app));
    click_tab(root, app, 4);
    CHECK(player_app_rows(app) == 1, "one playlist row");
    player_app_open_row(app, 0);
    CHECK(player_app_rows(app) == 1, "playlist holds 1 track, got %zu",
          player_app_rows(app));

    /* repeat cycles off -> all -> one -> off */
    click(root, player_app_repeat_button(app));
    CHECK(strstr(tk_label_text(player_app_repeat_label(app)), "全て") != NULL,
          "repeat all label: %s",
          tk_label_text(player_app_repeat_label(app)));
    click(root, player_app_repeat_button(app));
    CHECK(strstr(tk_label_text(player_app_repeat_label(app)), "1曲") != NULL,
          "repeat one label");
    click(root, player_app_repeat_button(app));
    CHECK(strstr(tk_label_text(player_app_repeat_label(app)), "なし") != NULL,
          "repeat off label");

    /* let everything play out; playback stops at the end */
    click_tab(root, app, 0);
    tk_listview_select(player_app_list_widget(app), 0);
    click(root, player_app_play_button(app));
    if (!player_app_playing(app))
        click(root, player_app_play_button(app));   /* was paused toggle */
    /* the pause toggle flips the button label both ways */
    click(root, player_app_play_button(app));   /* pause */
    CHECK(!player_app_playing(app), "paused by toggle");
    CHECK(strcmp(tk_button_label(player_app_play_button(app)),
                 "▶ 再生") == 0,
          "play button shows play while paused");
    click(root, player_app_play_button(app));   /* resume */
    CHECK(player_app_playing(app), "resumed by toggle");
    CHECK(strcmp(tk_button_label(player_app_play_button(app)),
                 "⏸ 一時停止") == 0,
          "play button shows pause after resume");
    for (int i = 0; i < 2000 && player_app_playing(app); i++)
        player_app_tick(app, 100000 + (uint64_t)i * 50);
    CHECK(!player_app_playing(app), "playback stops at end of context");
    CHECK(strcmp(tk_button_label(player_app_play_button(app)),
                 "▶ 再生") == 0,
          "play button back to play at end of context");

    /* dimensions (DESIGN-apps-envfit SS3.2): the header is
     * 56px (the tab bar starts at y=56) and the primary play
     * target is at least 56x56 */
    {
        tk_rect tr = tk_widget_rect(player_app_tabbar(app));
        CHECK(tr.y == 56, "header height 56 (tabbar y=%d)", tr.y);
        CHECK(tr.h >= 40, "tabbar height %d >= 40", tr.h);
        tk_rect pr = tk_widget_rect(player_app_play_button(app));
        CHECK(pr.w >= 56 && pr.h >= 56,
              "play target %dx%d >= 56", pr.w, pr.h);
    }

    const char *last = player_app_now_path(app);
    char last_copy[1024];
    snprintf(last_copy, sizeof(last_copy), "%s", last ? last : "");
    player_app_free(app);

    /* WinUI mode: the same app code builds, lists and paints
     * under the WinUI theme (mode switch = theme switch) */
    {
        tk_theme_set_default(&tk_theme_winui);
        char data2[1100];
        snprintf(data2, sizeof(data2), "%s/appdata-winui", tmpl);
        player_app *app2 = player_app_new(data2, dirs, 1, mk_out,
                                          NULL, 800, 600);
        CHECK(app2 != NULL, "winui app created");
        if (app2) {
            CHECK(player_app_rows(app2) == 3, "winui rows 3");
            tk_canvas *canvas = tk_canvas_new(800, 600);
            tk_root_paint(player_app_root(app2), canvas,
                          tk_theme_default());
            CHECK(canvas != NULL, "winui paint ran");
            tk_canvas_free(canvas);
            player_app_free(app2);
        }
        tk_theme_set_default(&tk_theme_v2);
    }

    /* state restore: a fresh app instance restores the last
     * track (paused) from state.txt */
    app = player_app_new(data, dirs, 1, mk_out, NULL, 800, 600);
    CHECK(app != NULL, "app recreated");
    root = player_app_root(app);   /* the old root was freed */
    CHECK(player_app_now_path(app) &&
          strcmp(player_app_now_path(app), last_copy) == 0,
          "restored last track %s",
          player_app_now_path(app) ? player_app_now_path(app) : "(null)");
    CHECK(!player_app_playing(app), "restored paused");
    CHECK(strcmp(tk_button_label(player_app_play_button(app)),
                 "▶ 再生") == 0,
          "play button shows play for restored paused state");

    /* %F entry: open an explicit two-file list */
    {
        char f1[1200], f2[1200];
        snprintf(f1, sizeof(f1), "%s/a_song.mp3", media);
        snprintf(f2, sizeof(f2), "%s/b_song.ogg", media);
        const char *files[] = { f1, f2 };
        CHECK(player_app_open_files(app, files, 2) == 0,
              "open_files ok");
        CHECK(player_app_playing(app), "open_files autoplays");
        CHECK(player_app_now_path(app) &&
              strcmp(player_app_now_path(app), f1) == 0,
              "open_files starts at first file");
        click(root, player_app_next_button(app));
        CHECK(player_app_now_path(app) &&
              strcmp(player_app_now_path(app), f2) == 0,
              "open_files context advances to second file");
    }
    player_app_free(app);

    printf("test_player_ui: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
