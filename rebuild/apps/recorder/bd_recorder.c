/* bd_recorder - see bd_recorder.h. Toolkit v2 public API only.
 *
 * Dual-mode (apps/common/bdappmode.h + bdicon.h): main() reads
 * the display mode from brainreg ui/mode via bd_app_theme();
 * the constructor only looks at tk_theme_default()->style
 * (2 = WinUI) and keeps it in app->winui, so tests that preset
 * a theme keep working. Android form = Google Recorder style
 * (single column, list over detail, big centred record button
 * at the bottom); WinUI form = Windows Voice Recorder style
 * (recordings pane on the left, playback/waveform on the
 * right, record controls along the bottom). All icons are
 * real vector-set icons (Material / Fluent) via bd_icon_mode;
 * no font glyphs are used as icons or status marks. */
#define _POSIX_C_SOURCE 200809L
#include "bd_recorder.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WAVE_W = 456, WAVE_H = 56 };

struct rec_app {
    tk_root *root;
    tk_widget *top;
    rc_lib *lib;
    rec_source_factory sf;
    rec_sink_factory kf;
    void *ud;

    tk_widget *w_tabs[2];
    tk_widget *search;
    tk_widget *list;
    tk_widget *detail_name, *detail_info;
    tk_widget *wave;
    uint32_t wave_px[WAVE_W * WAVE_H];
    tk_widget *btn_play, *btn_back, *btn_fwd, *pos_label;
    tk_widget *speed_slider, *speed_label;
    tk_widget *rename_field, *btn_rename, *btn_fav;
    tk_widget *btn_delete, *btn_restore, *btn_purge;
    tk_widget *trim_start, *trim_end, *btn_trim_keep, *btn_trim_del;
    tk_widget *btn_record, *btn_rec_pause, *btn_rec_stop;
    tk_widget *level_bar, *rec_label;
    tk_widget *status;
    /* Android page swap (proportion pass): list page and
     * detail page replace each other - see rec_set_detail().
     * NULL in WinUI mode (split pane form). */
    tk_widget *list_page, *detail_page;
    bool detail_open;

    bool winui;
    bool deleted_view;
    char sel[160];
    rc_player *player;
    rc_sink *sink;
    char playing_path[1100];
    bool playing;
    rc_recorder *rec;
    rc_source *src;
    char rec_name[160];
};

static void set_text(tk_widget *w, const char *s)
{
    tk_label_set_text(w, s);
    tk_widget_invalidate(w);
}

static void field_text(tk_widget *w, char *out, size_t cap)
{
    if (cap) out[0] = 0;
    tk_textfield_text(w, out, cap);
}

static void fmt_time(char *out, size_t cap, uint64_t ms)
{
    uint64_t s = ms / 1000;
    snprintf(out, cap, "%llu:%02llu", (unsigned long long)(s / 60),
             (unsigned long long)(s % 60));
}

/* ---------- waveform ---------- */

static void draw_waveform(rec_app *app)
{
    for (int i = 0; i < WAVE_W * WAVE_H; i++) app->wave_px[i] = 0xEDEFF2;
    const rc_entry *e = NULL;
    if (app->deleted_view) {
        for (size_t i = 0; i < rc_lib_deleted_count(app->lib); i++) {
            const rc_entry *d = rc_lib_deleted_at(app->lib, i);
            if (d && strcmp(d->name, app->sel) == 0) e = d;
        }
    } else if (app->sel[0]) {
        e = rc_lib_find(app->lib, app->sel);
    }
    if (e) {
        uint8_t peaks[2048];
        size_t n = rc_peaks(e->path, peaks, sizeof(peaks));
        for (int x = 0; x < WAVE_W && n; x++) {
            size_t a = (size_t)x * n / WAVE_W;
            size_t b = (size_t)(x + 1) * n / WAVE_W;
            if (b <= a) b = a + 1;
            uint8_t mx = 0;
            for (size_t i = a; i < b && i < n; i++)
                if (peaks[i] > mx) mx = peaks[i];
            int h = mx * (WAVE_H / 2 - 2) / 255;
            if (h < 1) h = 1;
            for (int y = WAVE_H / 2 - h; y <= WAVE_H / 2 + h; y++)
                app->wave_px[y * WAVE_W + x] = 0x2456A6;
        }
    }
    tk_imageview_set_pixels(app->wave, app->wave_px, WAVE_W, WAVE_H);
    tk_widget_invalidate(app->wave);
}

/* ---------- list / detail ---------- */

static void stop_playback(rec_app *app)
{
    if (app->player) rc_player_close(app->player);
    if (app->sink) rc_sink_free(app->sink);
    app->player = NULL;
    app->sink = NULL;
    app->playing = false;
    app->playing_path[0] = 0;
}

static void refresh_detail(rec_app *app)
{
    const rc_entry *e = NULL;
    if (app->deleted_view) {
        for (size_t i = 0; i < rc_lib_deleted_count(app->lib); i++) {
            const rc_entry *d = rc_lib_deleted_at(app->lib, i);
            if (d && strcmp(d->name, app->sel) == 0) e = d;
        }
    } else if (app->sel[0]) {
        e = rc_lib_find(app->lib, app->sel);
    }
    if (!e) {
        set_text(app->detail_name, "（未選択）");
        set_text(app->detail_info, "");
        set_text(app->pos_label, "0:00 / 0:00");
    } else {
        char buf[256], t1[32];
        fmt_time(t1, sizeof(t1), e->duration_ms);
        set_text(app->detail_name, e->name);
        snprintf(buf, sizeof(buf), "長さ %s / %llu KB%s%s", t1,
                 (unsigned long long)(e->size_bytes / 1024),
                 e->favorite ? " / お気に入り" : "",
                 e->needs_repair ? " / 要修復" : "");
        set_text(app->detail_info, buf);
        snprintf(buf, sizeof(buf), "0:00 / %s", t1);
        set_text(app->pos_label, buf);
        tk_textfield_set_text(app->rename_field, e->name);
        tk_slider_set_value(app->speed_slider,
                            (float)((e->speed - 0.5) / 1.5));
        char sl[32];
        snprintf(sl, sizeof(sl), "%.2gx", e->speed);
        set_text(app->speed_label, sl);
    }
    draw_waveform(app);
}

/* Row sub-line: recording date + duration (the reference
 * list form). mtime is display-only data in the model. */
static void fmt_subline(const rc_entry *e, char *buf, size_t cap)
{
    char t1[32];
    fmt_time(t1, sizeof(t1), e->duration_ms);
    time_t mt = (time_t)e->mtime;
    struct tm tmv;
    if (mt > 0 && localtime_r(&mt, &tmv)) {
        snprintf(buf, cap, "%d/%d %02d:%02d · %s",
                 tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                 tmv.tm_min, t1);
    } else {
        snprintf(buf, cap, "%s", t1);
    }
}

static void rebuild_list(rec_app *app)
{
    char q[128];
    field_text(app->search, q, sizeof(q));
    int sel = -1;
    if (app->deleted_view) {
        size_t n = rc_lib_deleted_count(app->lib);
        const char **names = calloc(n ? n : 1, sizeof(char *));
        size_t w = 0;
        for (size_t i = 0; i < n; i++) {
            const rc_entry *e = rc_lib_deleted_at(app->lib, i);
            if (q[0] && !strstr(e->name, q)) continue;
            names[w++] = e->name;
            if (strcmp(e->name, app->sel) == 0) sel = (int)w - 1;
        }
        tk_listview_set_items(app->list, names, w);
        {
            const char **subs = calloc(w ? w : 1, sizeof(char *));
            char **owned = calloc(w ? w : 1, sizeof(char *));
            size_t k = 0;
            for (size_t i = 0; i < n; i++) {
                const rc_entry *e = rc_lib_deleted_at(app->lib, i);
                if (q[0] && !strstr(e->name, q)) continue;
                char buf[96];
                fmt_subline(e, buf, sizeof(buf));
                subs[k] = owned[k] = strdup(buf);
                k++;
            }
            tk_listview_set_subitems(app->list, subs, w);
            for (size_t i = 0; i < w; i++) free(owned[i]);
            free(owned);
            free(subs);
        }
        free(names);
    } else {
        size_t idx[512];
        size_t n = rc_lib_search(app->lib, q, idx, 512);
        const char **names = calloc(n ? n : 1, sizeof(char *));
        for (size_t i = 0; i < n; i++) {
            names[i] = rc_lib_at(app->lib, idx[i])->name;
            if (strcmp(names[i], app->sel) == 0) sel = (int)i;
        }
        tk_listview_set_items(app->list, names, n);
        {
            const char **subs = calloc(n ? n : 1, sizeof(char *));
            char **owned = calloc(n ? n : 1, sizeof(char *));
            for (size_t i = 0; i < n; i++) {
                char buf[96];
                fmt_subline(rc_lib_at(app->lib, idx[i]), buf,
                            sizeof(buf));
                subs[i] = owned[i] = strdup(buf);
            }
            tk_listview_set_subitems(app->list, subs, n);
            for (size_t i = 0; i < n; i++) free(owned[i]);
            free(owned);
            free(subs);
        }
        free(names);
    }
    if (sel >= 0) tk_listview_select(app->list, sel);
    tk_widget_invalidate(app->list);
}

/* Android page swap (proportion pass): the takes list and
 * the detail block used to stack in one column - the detail's
 * ~390px of fixed rows plus the record area summed past every
 * window height and the box crushed the LIST to 0px at every
 * size, so takes could not even be seen, let alone picked.
 * The detail is now its own page (the Google Recorder form:
 * list first, tap a take for its player page), and its rows
 * live in a scrollview so they keep their full height. */
static void rec_set_detail(rec_app *app, bool detail)
{
    if (!app->list_page || !app->detail_page) return;
    if (app->detail_open == detail) return;
    app->detail_open = detail;
    tk_set_visible(app->list_page, !detail);
    tk_set_visible(app->detail_page, detail);
    tk_widget_invalidate(app->list_page);
    tk_widget_invalidate(app->detail_page);
    tk_rect tr = tk_widget_rect(app->top);
    if (tr.w > 0 && tr.h > 0) tk_root_layout(app->root, tr.w, tr.h);
}

static void on_detail_back(tk_widget *w, void *ud)
{
    (void)w;
    rec_set_detail(ud, false);
}

static void select_name(rec_app *app, const char *name)
{
    if (strcmp(app->sel, name) != 0) stop_playback(app);
    snprintf(app->sel, sizeof(app->sel), "%s", name);
    refresh_detail(app);
    if (!app->winui) rec_set_detail(app, true);
}

/* ---------- callbacks ---------- */

static void on_list(tk_widget *w, void *ud)
{
    rec_app *app = ud;
    int idx = tk_listview_selected(w);
    if (idx < 0) return;
    char q[128];
    field_text(app->search, q, sizeof(q));
    if (app->deleted_view) {
        int seen = -1;
        for (size_t i = 0; i < rc_lib_deleted_count(app->lib); i++) {
            const rc_entry *e = rc_lib_deleted_at(app->lib, i);
            if (q[0] && !strstr(e->name, q)) continue;
            if (++seen == idx) { select_name(app, e->name); return; }
        }
    } else {
        size_t map[512];
        size_t n = rc_lib_search(app->lib, q, map, 512);
        if ((size_t)idx < n)
            select_name(app, rc_lib_at(app->lib, map[idx])->name);
    }
}

static void on_tab(tk_widget *w, void *ud)
{
    rec_app *app = ud;
    app->deleted_view = (w == app->w_tabs[1]);
    app->sel[0] = 0;
    stop_playback(app);
    rec_set_detail(app, false);
    rebuild_list(app);
    refresh_detail(app);
}

static void on_search(tk_widget *w, void *ud)
{
    (void)w;
    rebuild_list(ud);
}

static void on_play(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (app->deleted_view || !app->sel[0]) return;
    const rc_entry *e = rc_lib_find(app->lib, app->sel);
    if (!e) return;
    if (app->playing && app->player &&
        strcmp(app->playing_path, e->path) == 0) {
        app->playing = false;
        return;
    }
    if (!app->player || strcmp(app->playing_path, e->path) != 0) {
        stop_playback(app);
        app->sink = app->kf ? app->kf(app->ud)
                            : rc_sink_alsa(NULL, RC_RATE, RC_CHANNELS);
        if (!app->sink) {
            set_text(app->status, "音声出力が開けません");
            return;
        }
        app->player = rc_player_open(e->path, app->sink);
        if (!app->player) {
            rc_sink_free(app->sink);
            app->sink = NULL;
            set_text(app->status, "再生できません");
            return;
        }
        snprintf(app->playing_path, sizeof(app->playing_path), "%s", e->path);
        rc_player_set_speed(app->player, e->speed);
    }
    app->playing = true;
}

static void on_seek(tk_widget *w, void *ud)
{
    rec_app *app = ud;
    if (!app->player) return;
    int64_t np = (int64_t)rc_player_position_ms(app->player) +
                 (w == app->btn_fwd ? 15000 : -15000);
    if (np < 0) np = 0;
    rc_player_seek_ms(app->player, (uint64_t)np);
}

static void on_speed(tk_widget *w, void *ud)
{
    rec_app *app = ud;
    double speed = 0.5 + tk_slider_value(w) * 1.5;
    if (app->sel[0] && !app->deleted_view) {
        rc_lib_set_speed(app->lib, app->sel, speed);
        if (app->player) rc_player_set_speed(app->player, speed);
        char sl[32];
        snprintf(sl, sizeof(sl), "%.2gx", speed);
        set_text(app->speed_label, sl);
    }
}

static void on_rename(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    char nn[160];
    field_text(app->rename_field, nn, sizeof(nn));
    if (!app->sel[0] || !nn[0]) return;
    if (rc_lib_rename(app->lib, app->sel, nn) == 0) {
        snprintf(app->sel, sizeof(app->sel), "%s", nn);
        rebuild_list(app);
        refresh_detail(app);
        set_text(app->status, "名前を変更しました");
    } else {
        set_text(app->status, "変更できません（同名あり）");
    }
}

static void on_fav(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    const rc_entry *e = rc_lib_find(app->lib, app->sel);
    if (!e) return;
    rc_lib_set_favorite(app->lib, app->sel, !e->favorite);
    refresh_detail(app);
}

static void on_delete(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (!app->sel[0]) return;
    stop_playback(app);
    if (rc_lib_delete(app->lib, app->sel) == 0) {
        app->sel[0] = 0;
        rec_set_detail(app, false);
        rebuild_list(app);
        refresh_detail(app);
        set_text(app->status, "最近削除へ移動しました");
    }
}

static void on_restore(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (!app->sel[0]) return;
    if (rc_lib_restore(app->lib, app->sel) == 0) {
        app->sel[0] = 0;
        rec_set_detail(app, false);
        rebuild_list(app);
        refresh_detail(app);
        set_text(app->status, "復元しました");
    }
}

static void on_purge(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (!app->sel[0]) return;
    if (rc_lib_purge_one(app->lib, app->sel) == 0) {
        app->sel[0] = 0;
        rec_set_detail(app, false);
        rebuild_list(app);
        refresh_detail(app);
        set_text(app->status, "完全に削除しました");
    }
}

static void do_trim(rec_app *app, int mode)
{
    const rc_entry *e = rc_lib_find(app->lib, app->sel);
    if (!e) return;
    char sb[32], eb[32];
    field_text(app->trim_start, sb, sizeof(sb));
    field_text(app->trim_end, eb, sizeof(eb));
    double s = strtod(sb, NULL), en = strtod(eb, NULL);
    if (en <= s) {
        set_text(app->status, "トリム範囲が不正です");
        return;
    }
    char out_name[192];
    snprintf(out_name, sizeof(out_name), "%s (トリム)", e->name);
    char out_path[1100];
    snprintf(out_path, sizeof(out_path), "%s/%s.wav",
             rc_lib_recordings_dir(app->lib), out_name);
    if (rc_trim(e->path, out_path, (uint64_t)(s * 1000),
                (uint64_t)(en * 1000), mode) == 0) {
        rc_lib_rescan(app->lib);
        snprintf(app->sel, sizeof(app->sel), "%s", out_name);
        rebuild_list(app);
        refresh_detail(app);
        set_text(app->status, "トリム結果を保存しました");
    } else {
        set_text(app->status, "トリムに失敗しました");
    }
}
static void on_trim_keep(tk_widget *w, void *ud) { (void)w; do_trim(ud, RC_TRIM_KEEP); }
static void on_trim_del(tk_widget *w, void *ud) { (void)w; do_trim(ud, RC_TRIM_DELETE); }

static void on_record(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (app->rec) return;
    rc_lib_next_name(app->lib, app->rec_name, sizeof(app->rec_name));
    app->src = app->sf ? app->sf(app->ud)
                       : rc_source_alsa(NULL, RC_RATE, RC_CHANNELS);
    if (!app->src) {
        set_text(app->status, "マイクが開けません");
        return;
    }
    app->rec = rc_recorder_start(rc_lib_recordings_dir(app->lib),
                                 app->rec_name, app->src);
    if (!app->rec) {
        rc_source_free(app->src);
        app->src = NULL;
        set_text(app->status, "録音を開始できません");
        return;
    }
    char buf[220];
    snprintf(buf, sizeof(buf), "録音中: %s", app->rec_name);
    set_text(app->rec_label, buf);
}

static void on_rec_pause(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (!app->rec) return;
    bool p = !rc_recorder_paused(app->rec);
    rc_recorder_pause(app->rec, p);
    set_text(app->rec_label, p ? "一時停止中" : "録音中");
}

static void on_rec_stop(tk_widget *w, void *ud)
{
    (void)w;
    rec_app *app = ud;
    if (!app->rec) return;
    rc_recorder_stop(app->rec);
    app->rec = NULL;
    rc_source_free(app->src);
    app->src = NULL;
    set_text(app->rec_label, "録音していません");
    tk_progress_set_value(app->level_bar, 0);
    tk_widget_invalidate(app->level_bar);
    rc_lib_rescan(app->lib);
    snprintf(app->sel, sizeof(app->sel), "%s", app->rec_name);
    rebuild_list(app);
    refresh_detail(app);
    if (!app->winui) rec_set_detail(app, true); /* open the new take */
    set_text(app->status, "録音を保存しました");
}

/* ---------- construction ---------- */

static tk_widget *btn(tk_widget *parent, const char *label,
                      tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    tk_add_child(parent, b);
    return b;
}
static tk_widget *lbl(tk_widget *parent, const char *s)
{
    tk_widget *l = tk_label_new(s);
    tk_add_child(parent, l);
    return l;
}
static tk_widget *field(tk_widget *parent, const char *ph)
{
    tk_widget *f = tk_textfield_new("");
    tk_textfield_set_placeholder(f, ph);
    tk_add_child(parent, f);
    return f;
}
static tk_widget *hbox(tk_widget *parent)
{
    tk_widget *b = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_add_child(parent, b);
    return b;
}

/* ---------- icons (real vector-set icons, no glyphs) ---------- */

static uint32_t theme_rgb(tk_color c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) |
           (uint32_t)c.b;
}

/* Attach the mode's real icon to a button (icon + label row,
 * tonal face so the button keeps a face with an icon). */
static void btn_icon(rec_app *app, tk_widget *b, const char *mat,
                     const char *flu, int box, uint32_t tint)
{
    const uint32_t *px = bd_icon_mode(app->winui, mat, flu, box,
                                      tint);
    if (!px) return;
    tk_button_set_icon(b, px, box, box);
    tk_button_set_icon_layout(b, TK_BTN_ICON_ROW);
    tk_button_set_variant(b, TK_BTN_TONAL);
}

static tk_widget *ibtn(rec_app *app, tk_widget *parent,
                       const char *label, const char *mat,
                       const char *flu, tk_notify_cb cb)
{
    tk_widget *b = btn(parent, label, cb, app);
    const tk_theme *t = tk_theme_default();
    btn_icon(app, b, mat, flu, 24,
             t ? theme_rgb(t->text) : 0x202020u);
    return b;
}

rec_app *rec_app_new(const char *data_root,
                     rec_source_factory sf, rec_sink_factory kf, void *ud,
                     int w, int h)
{
    rec_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->sf = sf;
    app->kf = kf;
    app->ud = ud;
    app->lib = rc_lib_open(data_root);
    if (!app->lib) { free(app); return NULL; }

    app->winui = bd_app_winui(tk_theme_default());

    tk_widget *top = tk_box_new(TK_DIR_VERTICAL, 4, 4);
    app->top = top;

    /* Structure branch:
     *  WinUI  - full-width tab/search row on top, then left
     *           pane (list) + detail right.
     *  Android - single column: tabs/search, list, detail.
     * The WinUI tab row used to live inside the left pane,
     * where the two labeled tabs (~140px each with icons)
     * cannot fit a pane that is 156px wide at a 480px
     * window: the second tab measured 12px (proportion
     * pass). Spanning the window like the Android form
     * also returns the tabs' height to the list. */
    tk_widget *mid = NULL, *left = NULL;
    tk_widget *tabs = hbox(top);
    app->w_tabs[0] = ibtn(app, tabs, "録音一覧", "mic", "mic",
                          on_tab);
    app->w_tabs[1] = ibtn(app, tabs, "最近削除", "delete", "delete",
                          on_tab);
    app->search = field(tabs, "検索");
    tk_textfield_set_on_change(app->search, on_search, app);
    tk_set_flex(app->search, 1);
    if (app->winui) {
        mid = hbox(top);
        /* mid must flex: at its desired height the detail column
         * pushes the record row and the status line off the
         * bottom of a 480px window */
        tk_set_flex(mid, 1);
        left = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_add_child(mid, left);
        tk_set_flex(left, 1);
    }

    app->list = tk_listview_new();
    tk_listview_set_on_select(app->list, on_list, app);
    tk_set_flex(app->list, 1);
    if (app->winui) {
        tk_add_child(left, app->list);
    } else {
        /* Android pages: the list (with the record area under
         * it) and the detail swap in the same slot of the
         * column; the header row and the status line are
         * common to both pages */
        app->list_page = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_set_flex(app->list_page, 1);
        tk_add_child(top, app->list_page);
        tk_add_child(app->list_page, app->list);
        app->detail_page = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_set_flex(app->detail_page, 1);
        tk_set_visible(app->detail_page, false);
        tk_add_child(top, app->detail_page);
        tk_widget *brow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        ibtn(app, brow, "戻る", "arrow_back", "arrow_left",
             on_detail_back);
        tk_add_child(app->detail_page, brow);
    }

    tk_widget *det = tk_box_new(TK_DIR_VERTICAL, 3, 0);
    /* the detail rows keep their full height inside a
     * scrollview instead of being crushed by the column
     * (both modes; in WinUI the pane was short of its own
     * rows at 368px too) */
    tk_widget *det_scroll = tk_scrollview_new();
    tk_scrollview_set_child(det_scroll, det);
    if (app->winui) {
        tk_add_child(mid, det_scroll);
        tk_set_flex(det_scroll, 2);
    } else {
        tk_add_child(app->detail_page, det_scroll);
        tk_set_flex(det_scroll, 1);
    }
    app->detail_name = lbl(det, "（未選択）");
    app->detail_info = lbl(det, "");
    app->wave = tk_imageview_new();
    tk_add_child(det, app->wave);

    tk_widget *prow = hbox(det);
    app->btn_play = ibtn(app, prow, "再生/一時停止", "play_arrow",
                         "play", on_play);
    app->btn_back = btn(prow, "-15秒", on_seek, app);
    app->btn_fwd = btn(prow, "+15秒", on_seek, app);

    tk_widget *srow = hbox(det);
    app->speed_slider = tk_slider_new(1.0f / 3.0f);
    tk_slider_set_on_change(app->speed_slider, on_speed, app);
    tk_add_child(srow, app->speed_slider);
    tk_set_flex(app->speed_slider, 1);
    /* int10 (review round 2): the label drops the 速度
     * prefix - in the guest the row measured a few px
     * wider than on the host (font metrics) and the
     * position label clipped at the window's right edge
     * (C10). The slider makes the meaning plain. */
    app->speed_label = lbl(srow, "1.00x");
    /* the position label rides the speed row: on the play
     * row it was the element squeezed to 0px in a narrow
     * detail pane (proportion pass) */
    app->pos_label = lbl(srow, "0:00 / 0:00");
    /* int11: a fixed 16px trailing spacer after the position
     * label. Without it the label's rect ended exactly at
     * the pane's right edge (host probe: rect 751..850 of an
     * 850px content edge), and the guest renders this text
     * ~9px wider than the host measures it, so the last
     * glyphs ran off the window (sd-v9 guest shot, C10).
     * The spacer makes the flex slider absorb the slack
     * instead: whatever the local font metrics, the label
     * always stops 16px short of the edge. (Padding the
     * whole detail column instead broke the 480x272 touch
     * floor in test_matrix, so the margin lives on this
     * row only.) */
    {
        tk_widget *sp = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_widget_set_min_size(sp, 16, 1);
        tk_add_child(srow, sp);
    }

    /* detail rows are sized for the narrowest detail pane
     * (312px in the WinUI split at a 480px window): the
     * rename row carried field + rename + favourite
     * (~460px with icons), which left the field 5px wide,
     * and the trim row packed label + 2 fields + 2 buttons
     * (~690px), which zeroed the fields (proportion pass).
     * Rows now hold at most one field + one action, or two
     * actions. */
    tk_widget *rrow = hbox(det);
    app->rename_field = field(rrow, "名前");
    tk_set_flex(app->rename_field, 1);
    app->btn_rename = ibtn(app, rrow, "名前変更",
                           "drive_file_rename_outline", "rename",
                           on_rename);

    tk_widget *drow = hbox(det);
    app->btn_fav = btn(drow, "お気に入り", on_fav, app);
    {
        const uint32_t *px = bd_icon_mode(app->winui, "star",
                                          "star", 24, 0xF0A020u);
        if (px) {
            tk_button_set_icon(app->btn_fav, px, 24, 24);
            tk_button_set_icon_layout(app->btn_fav,
                                      TK_BTN_ICON_ROW);
            tk_button_set_variant(app->btn_fav, TK_BTN_TONAL);
        }
    }
    app->btn_delete = ibtn(app, drow, "削除", "delete", "delete",
                           on_delete);
    tk_widget *drow2 = hbox(det);
    app->btn_restore = ibtn(app, drow2, "復元", "restore_from_trash",
                            "arrow_undo", on_restore);
    app->btn_purge = ibtn(app, drow2, "完全削除", "delete_forever",
                          "delete", on_purge);

    tk_widget *trow = hbox(det);
    lbl(trow, "トリム(秒)");
    app->trim_start = field(trow, "開始");
    tk_set_flex(app->trim_start, 1);
    app->trim_end = field(trow, "終了");
    tk_set_flex(app->trim_end, 1);
    tk_widget *trow2 = hbox(det);
    app->btn_trim_keep = ibtn(app, trow2, "範囲を保持", "content_cut",
                              "cut", on_trim_keep);
    app->btn_trim_del = ibtn(app, trow2, "範囲を削除", "content_cut",
                             "cut", on_trim_del);

    /* Record area. Android: level/status line, then the big
     * record button centred at the bottom (Google Recorder
     * form), flanked by pause / stop. WinUI: one bottom
     * control bar (Windows Voice Recorder form). */
    if (app->winui) {
        tk_widget *recrow = hbox(top);
        app->btn_record = btn(recrow, "録音", on_record, app);
        app->btn_rec_pause = ibtn(app, recrow, "一時停止", "pause",
                                  "pause", on_rec_pause);
        app->btn_rec_stop = ibtn(app, recrow, "停止", "stop",
                                 "stop", on_rec_stop);
        /* int10 (review B follow-up): the level bar and the
         * state label get their own row under the buttons.
         * In one 480px row the squeeze shrank the label
         * under its text width (C10, matrix labelclip) -
         * the same two-row split the Android form uses. */
        tk_widget *lvlrow2 = hbox(top);
        app->level_bar = tk_progress_new(0);
        tk_add_child(lvlrow2, app->level_bar);
        tk_set_flex(app->level_bar, 1);
        app->rec_label = lbl(lvlrow2, "録音していません");
        const tk_theme *t = tk_theme_default();
        const uint32_t *px = bd_icon_mode(
            app->winui, "fiber_manual_record", "record", 24,
            t ? theme_rgb(t->danger) : 0xC42B1Cu);
        if (px) {
            tk_button_set_icon(app->btn_record, px, 24, 24);
            tk_button_set_icon_layout(app->btn_record,
                                      TK_BTN_ICON_ROW);
        }
        tk_button_set_variant(app->btn_record, TK_BTN_FILLED);
    } else {
        tk_widget *lvlrow = hbox(app->list_page);
        app->rec_label = lbl(lvlrow, "録音していません");
        app->level_bar = tk_progress_new(0);
        tk_add_child(lvlrow, app->level_bar);
        tk_set_flex(app->level_bar, 1);
        tk_widget *recrow = hbox(app->list_page);
        tk_widget *sp1 = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_add_child(recrow, sp1);
        tk_set_flex(sp1, 1);
        app->btn_rec_pause = ibtn(app, recrow, "一時停止", "pause",
                                  "pause", on_rec_pause);
        app->btn_record = btn(recrow, "録音", on_record, app);
        app->btn_rec_stop = ibtn(app, recrow, "停止", "stop",
                                 "stop", on_rec_stop);
        tk_widget *sp2 = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_add_child(recrow, sp2);
        tk_set_flex(sp2, 1);
        const tk_theme *t = tk_theme_default();
        const uint32_t *px = bd_icon_mode(
            app->winui, "fiber_manual_record", "record", 40,
            t ? theme_rgb(t->danger) : 0xC42B1Cu);
        if (px) {
            tk_button_set_icon(app->btn_record, px, 40, 40);
            tk_button_set_icon_layout(app->btn_record,
                                      TK_BTN_ICON_CELL);
        }
        tk_button_set_variant(app->btn_record, TK_BTN_FILLED);
        tk_widget_set_min_size(app->btn_record, 84, 84);
    }
    app->status = lbl(top, "");

    app->root = tk_root_new(top);
    tk_root_layout(app->root, w, h);
    rebuild_list(app);
    refresh_detail(app);
    return app;
}

void rec_app_free(rec_app *app)
{
    if (!app) return;
    if (app->rec) rc_recorder_abandon(app->rec);
    if (app->src) rc_source_free(app->src);
    stop_playback(app);
    rc_lib_close(app->lib);
    tk_root_free(app->root);
    tk_widget_free(app->top);
    free(app);
}

void rec_app_tick(rec_app *app, uint64_t mono_ms)
{
    (void)mono_ms;
    if (app->rec) {
        rc_recorder_pump(app->rec);
        tk_progress_set_value(app->level_bar, rc_recorder_level(app->rec));
        tk_widget_invalidate(app->level_bar);
        char buf[128], t[32];
        fmt_time(t, sizeof(t), rc_recorder_elapsed_ms(app->rec));
        snprintf(buf, sizeof(buf), "%s %s",
                 rc_recorder_paused(app->rec) ? "一時停止中" : "録音中",
                 t);
        set_text(app->rec_label, buf);
    }
    if (app->playing && app->player) {
        rc_player_pump(app->player, 4096);
        const rc_entry *e = rc_lib_find(app->lib, app->sel);
        char buf[96], a[32], b[32];
        fmt_time(a, sizeof(a), rc_player_position_ms(app->player));
        fmt_time(b, sizeof(b),
                 e ? e->duration_ms : rc_player_duration_ms(app->player));
        snprintf(buf, sizeof(buf), "%s / %s", a, b);
        set_text(app->pos_label, buf);
        if (rc_player_eof(app->player)) app->playing = false;
    }
}

bool rec_app_recording(const rec_app *app) { return app && app->rec; }
bool rec_app_playing(const rec_app *app) { return app && app->playing; }
const char *rec_app_selected(const rec_app *app)
{
    return (app && app->sel[0]) ? app->sel : NULL;
}
void rec_app_reload(rec_app *app)
{
    rc_lib_rescan(app->lib);
    rebuild_list(app);
    refresh_detail(app);
}

tk_root *rec_app_root(rec_app *app) { return app->root; }
tk_widget *rec_app_list_widget(rec_app *app) { return app->list; }
tk_widget *rec_app_search_field(rec_app *app) { return app->search; }
tk_widget *rec_app_tab_widget(rec_app *app, int dv)
{
    return app->w_tabs[dv ? 1 : 0];
}
tk_widget *rec_app_record_button(rec_app *app) { return app->btn_record; }
tk_widget *rec_app_rec_pause_button(rec_app *app) { return app->btn_rec_pause; }
tk_widget *rec_app_rec_stop_button(rec_app *app) { return app->btn_rec_stop; }
tk_widget *rec_app_play_button(rec_app *app) { return app->btn_play; }
tk_widget *rec_app_delete_button(rec_app *app) { return app->btn_delete; }
tk_widget *rec_app_restore_button(rec_app *app) { return app->btn_restore; }
tk_widget *rec_app_purge_button(rec_app *app) { return app->btn_purge; }
tk_widget *rec_app_status_label(rec_app *app) { return app->status; }
tk_widget *rec_app_detail_label(rec_app *app) { return app->detail_name; }

#ifdef BD_RECORDER_MAIN
static void rec_tick_cb(void *ud, uint64_t mono)
{
    rec_app_tick((rec_app *)ud, mono);
}

int main(void)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    char dir[512];
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.local/share/bd-recorder",
             home ? home : ".");
    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    /* Display first, real size (see bd_calc.c main note). The
     * factories stay NULL: the model falls back to ALSA "default"
     * for capture and playback. */
    int vw, vh;
    bd_app_display *disp = bd_app_display_open_ex("ボイスレコーダー", 0, 0, 480, 272, 800, 600, &vw, &vh);
    rec_app *app = rec_app_new(dir, NULL, NULL, NULL, vw, vh);
    if (!app) { fprintf(stderr, "bd-recorder: cannot build UI\n"); return 1; }

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, rec_tick_cb, app, 20);
    } else {
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-recorder: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    rec_app_free(app);
    if (font) tk_font_free(font);
    return 0;
}
#endif
