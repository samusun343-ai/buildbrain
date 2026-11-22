/* bd_player - see bd_player.h.
 *
 * Dual-mode layout (REPORT-apps-dual-mode-player.md). The
 * mode is read once at process start by main() through
 * bd_app_theme() (brainreg ui/mode); the constructor only
 * looks at tk_theme_default()->style (2 = WinUI) and keeps
 * the result in app->winui, so tests that preset a theme
 * keep driving the app exactly as before.
 *
 * Android mode (DESIGN-apps-envfit.md SS7.3, Samsung Music
 * fitted to this panel): three screens drilled between,
 * never panels shown side by side.
 *   LIBRARY     - header (title + search / overflow icons),
 *                 tab row (Songs / Albums / Artists / Genres /
 *                 Playlists, indicator tabs), a drill row when
 *                 drilled into a value, the track/value list, a
 *                 bottom context bar for the selection, one
 *                 status caption line, and the mini player.
 *   NOW PLAYING - large artwork + title/artist/album, scrub
 *                 bar with elapsed/total (or -remaining), the
 *                 transport row (shuffle / prev / play / next /
 *                 repeat, state shown by fill), volume row.
 *   QUEUE       - the Up Next list with its own bottom bar
 *                 (up / down / remove / clear).
 *
 * WinUI mode (Windows Media Player form): ONE screen - a
 * left navigation pane (library categories + queue + now
 * playing), the list / queue / now-playing content in the
 * right pane (now playing is shown large in the pane, not
 * drilled to), and a transport bar resident at the bottom
 * (artwork, title, scrub bar + time, shuffle / prev / play /
 * next / repeat, volume). The state-bearing widgets (list,
 * queue, scrub, volume, titles) are the same widgets in
 * both modes, only placed differently, so every sync path
 * (play / repeat / volume / tick) stays single.
 *
 * Playlists are created and filled through ONE dialog (name
 * field + Create + Add), reached from the context bar; rename
 * uses the same dialog shape. Nothing else is exposed at rest:
 * the old always-visible view/search/command/playlist rows
 * (24 controls) are gone.
 *
 * Icons come from the established vector sets through
 * bdicon (Material Symbols in the Android form, Fluent in
 * the WinUI form; see assets/icons/MANIFEST.md). No font
 * glyphs and no code-drawn shapes are used for icons.
 *
 * The engine is pump-driven: "pause" simply stops pumping, so
 * position survives untouched in the decoder. The audio sink
 * is an injectable factory (tests use the null sink;
 * production uses ALSA). Playback state is restored from
 * playmodel's state.txt on startup (paused, at the saved
 * position).
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_player.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { V_SONGS, V_ALBUMS, V_ARTISTS, V_GENRES, V_PLAYLISTS };
enum { SCR_LIB, SCR_NP, SCR_QUEUE };
/* WinUI right-pane content + navigation ids (the drawer ids
 * for the library views reuse V_*; queue / now playing get
 * their own ids so tk_drawer_last_id never collides) */
enum { WIN_SHOW_LIST, WIN_SHOW_QUEUE, WIN_SHOW_NP };
enum { NAV_QUEUE = 10, NAV_NP = 11 };
enum { CTX_PLAY, CTX_NEXT, CTX_LATER, CTX_ADDLIST, CTX_OPEN,
       CTX_RENAME, CTX_DELETE };
#define SHUFFLE_SEED 0xBD5EEDu
#define PUMP_FRAMES 4096

struct player_app {
    tk_root *root;
    tk_widget *top;
    pl_lib *lib;
    pl_order *order;
    char root_dir[1024];
    char scan_dirs[8][1024];
    size_t n_scan_dirs;
    pl_out_factory of;
    void *ud;
    int win_w, win_h;
    int screen;
    bool winui;                /* tk_theme_default()->style == 2
                                * at construction (see header) */
    int win_show;              /* WIN_SHOW_*: WinUI right pane */

    /* library screen */
    tk_widget *scr_lib;
    tk_widget *tabbar;
    tk_widget *search_row, *search;
    tk_widget *vol_row, *volume, *vol_label;
    tk_widget *drill_row, *drill_label;
    tk_widget *list;
    tk_widget *empty_label;      /* empty-library guidance
                                  * (eval F-14) */
    tk_widget *ctx_bar;                    /* tk_bottombar */
    tk_widget *ctx_cells[7];
    tk_widget *status;
    tk_widget *lib_menu;                   /* rebuilt on each open */
    /* mini player (bottom of the library screen) */
    tk_widget *mini_art;
    tk_widget *mini_title;                 /* flat button: opens NP */
    tk_widget *btn_repeat_mini;
    tk_widget *btn_prev, *btn_play, *btn_next;
    /* now playing screen */
    tk_widget *scr_np;
    tk_widget *art;
    tk_widget *np_title, *np_artist, *np_album;
    tk_widget *progress, *time_label;
    tk_widget *np_shuffle, *np_prev, *np_play, *np_next;
    tk_widget *np_repeat, *repeat_label;
    tk_widget *np_volume, *np_vol_label;
    tk_widget *np_menu;                    /* rebuilt on each open */
    bool show_remaining;                   /* time shows -remaining */
    /* queue screen */
    tk_widget *scr_queue;
    tk_widget *queue_list;
    tk_widget *queue_bar;                  /* queue ops bar */
    /* WinUI screen (all NULL in the Android form): one
     * horizontal screen + the resident transport bar */
    tk_widget *scr_win;
    tk_widget *win_nav;                    /* tk_drawer (std) */
    tk_widget *win_title;                  /* right-pane heading */
    tk_widget *win_np_pane;                /* large NP in pane */
    tk_widget *win_transport;
    tk_widget *win_shuffle;                /* transport shuffle */
    /* playlist dialog (add/create) and rename dialog */
    tk_widget *list_dlg, *list_name;
    tk_widget *btn_list_create, *btn_list_add;
    tk_widget *rename_dlg, *rename_field;
    char dlg_path[1024];                   /* track the dialog acts on */
    char rename_target[128];               /* playlist being renamed */

    /* icon pixmaps, borrowed from the bdicon cache (never
     * freed here; NULL when the asset is unavailable, in
     * which case the controls fall back to label-only forms).
     * ic_repeat tracks the repeat state (repeat / repeat_one
     * pair); ic_shuffle_on is the filled/active shuffle. */
    const uint32_t *ic_play, *ic_pause, *ic_prev, *ic_next,
                   *ic_repeat, *ic_repeat_one, *ic_shuffle,
                   *ic_back, *ic_vol;

    int view;
    bool drilled;              /* in a value/playlist track list */
    char drill_value[128];     /* album/artist/genre/playlist name */
    /* current visible rows */
    char **row_labels;
    size_t *row_tracks;        /* lib indices when rows are tracks */
    bool rows_are_tracks;
    size_t n_rows, cap_rows;
    /* playback */
    pe_engine *engine;
    pe_out *out;
    char now_path[1024];
    uint32_t dur_ms;
    bool playing;
    bool prog_guard;           /* setting progress from tick */
    bool vol_guard;            /* syncing the two volume sliders */
    double volume_v;
    unsigned tick_count;
};

/* ---------------------------------------------------------------- */
/* small helpers                                                     */
/* ---------------------------------------------------------------- */

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

static tk_widget *btn(const char *label, tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    return b;
}

/* Theme ink as a bdicon tint (0xRRGGBB). */
static uint32_t tint_of(tk_color c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) |
           (uint32_t)c.b;
}

/* Mode-paired icon at 24px in the given tint: the Material
 * name in the Android form, the Fluent name in WinUI. */
static const uint32_t *mode_icon(player_app *app, const char *mat,
                                 const char *flu, uint32_t tint)
{
    return bd_icon_mode(app->winui, mat, flu, 24, tint);
}

/* Header icon button: flat, icon-only, 48x48 target (SS3.2).
 * The label is the accessible name; with a NULL icon the
 * button shows its label text instead of rendering blank. */
static tk_widget *header_icon_btn(const char *label,
                                  const uint32_t *icon,
                                  tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    tk_button_set_flat(b, true);
    if (icon) {
        tk_button_set_icon(b, icon, 24, 24);
        tk_button_set_icon_only(b, true);
    }
    tk_widget_set_min_size(b, 48, 48);
    return b;
}

/* Back button: the mode's back icon (arrow_back /
 * arrow_left); the label stays as the accessible name and
 * shows only when the icon asset is unavailable. The icon
 * is read from app->ic_back, so callers must create back
 * buttons after the icon set is loaded. */
static tk_widget *header_back_btn(player_app *app, tk_notify_cb cb,
                                  void *ud)
{
    tk_widget *b = btn("戻る", cb, ud);
    tk_button_set_flat(b, true);
    if (app->ic_back) {
        tk_button_set_icon(b, app->ic_back, 24, 24);
        tk_button_set_icon_only(b, true);
    }
    tk_widget_set_min_size(b, 48, 48);
    return b;
}

/* Re-run layout after a visibility change so freshly shown
 * widgets have valid rects immediately (the damage-driven
 * host loop does not re-layout on its own; DESIGN SS5.2's
 * click-after-select contract depends on this). */
static void relayout(player_app *app)
{
    tk_widget_invalidate(app->top);
    if (app->root) tk_root_layout(app->root, app->win_w, app->win_h);
}

static void show_screen(player_app *app, int scr);   /* fwd */
static void refresh_queue(player_app *app);          /* fwd */
static void sync_ctx_bar(player_app *app);           /* fwd */

/* ---------------------------------------------------------------- */
/* rows                                                              */
/* ---------------------------------------------------------------- */

static void rows_clear(player_app *app)
{
    for (size_t i = 0; i < app->n_rows; i++) free(app->row_labels[i]);
    app->n_rows = 0;
}
static void rows_push(player_app *app, const char *label, size_t track_idx)
{
    if (app->n_rows == app->cap_rows) {
        app->cap_rows = app->cap_rows ? app->cap_rows * 2 : 32;
        app->row_labels = realloc(app->row_labels,
                                  app->cap_rows * sizeof(char *));
        app->row_tracks = realloc(app->row_tracks,
                                  app->cap_rows * sizeof(size_t));
    }
    app->row_labels[app->n_rows] = strdup(label);
    app->row_tracks[app->n_rows] = track_idx;
    app->n_rows++;
}

static void sync_marked(player_app *app);

/* Track rows are two-line (title - artist + duration sub-line,
 * the SS3.2 72px form); value rows stay single-line. */
static void rebuild_rows(player_app *app)
{
    rows_clear(app);
    char q[128];
    field_text(app->search, q, sizeof(q));
    app->rows_are_tracks = false;
    if (app->view == V_SONGS || app->drilled) {
        app->rows_are_tracks = true;
        size_t idx[1024];
        size_t n;
        if (app->view == V_SONGS) {
            n = pl_lib_search(app->lib, q, idx, 1024);
            for (size_t i = 0; i < n; i++) {
                const pl_track *t = pl_lib_at(app->lib, idx[i]);
                char lab[320];
                snprintf(lab, sizeof(lab), "%s — %s", t->title,
                         t->artist[0] ? t->artist : "（不明）");
                rows_push(app, lab, idx[i]);
            }
        } else if (app->view == V_PLAYLISTS) {
            size_t items = pl_lists_items(app->lib, app->drill_value);
            for (size_t i = 0; i < items; i++) {
                const char *path = pl_lists_item(app->lib,
                                                 app->drill_value, i);
                const pl_track *t = pl_lib_find(app->lib, path);
                char lab[320];
                size_t ti = (size_t)-1;
                if (t) {
                    snprintf(lab, sizeof(lab), "%s — %s", t->title,
                             t->artist[0] ? t->artist : "（不明）");
                    for (size_t j = 0; j < pl_lib_count(app->lib); j++)
                        if (pl_lib_at(app->lib, j) == t) { ti = j; break; }
                } else {
                    snprintf(lab, sizeof(lab), "%s", path);
                }
                rows_push(app, lab, ti);
            }
        } else {
            int field = app->view == V_ALBUMS ? PL_F_ALBUM
                      : app->view == V_ARTISTS ? PL_F_ARTIST : PL_F_GENRE;
            n = pl_lib_filter(app->lib, field, app->drill_value, idx, 1024);
            for (size_t i = 0; i < n; i++) {
                const pl_track *t = pl_lib_at(app->lib, idx[i]);
                if (q[0] && !strstr(t->title, q)) continue;
                char lab[320];
                snprintf(lab, sizeof(lab), "%s — %s", t->title,
                         t->artist[0] ? t->artist : "（不明）");
                rows_push(app, lab, idx[i]);
            }
        }
    } else if (app->view == V_PLAYLISTS) {
        for (size_t i = 0; i < pl_lists_count(app->lib); i++)
            rows_push(app, pl_lists_name(app->lib, i), 0);
    } else {
        int field = app->view == V_ALBUMS ? PL_F_ALBUM
                  : app->view == V_ARTISTS ? PL_F_ARTIST : PL_F_GENRE;
        const char *vals[256];
        size_t n = pl_lib_values(app->lib, field, vals, 256);
        for (size_t i = 0; i < n; i++) {
            if (q[0] && !strstr(vals[i], q)) continue;
            rows_push(app, vals[i], 0);
        }
    }
    tk_listview_set_items(app->list,
                          (const char *const *)app->row_labels,
                          app->n_rows);
    if (app->empty_label)
        tk_set_visible(app->empty_label,
                       pl_lib_count(app->lib) == 0);
    if (app->rows_are_tracks) {
        const char **subs = malloc((app->n_rows ? app->n_rows : 1) *
                                   sizeof(char *));
        char **owned = calloc(app->n_rows ? app->n_rows : 1,
                              sizeof(char *));
        for (size_t i = 0; i < app->n_rows; i++) {
            subs[i] = "";
            if (app->row_tracks[i] != (size_t)-1) {
                const pl_track *t = pl_lib_at(app->lib,
                                              app->row_tracks[i]);
                if (t && t->duration_ms) {
                    char ft[32];
                    fmt_time(ft, sizeof(ft), t->duration_ms);
                    subs[i] = owned[i] = strdup(ft);
                }
            }
        }
        tk_listview_set_subitems(app->list, subs, app->n_rows);
        for (size_t i = 0; i < app->n_rows; i++) free(owned[i]);
        free(owned);
        free(subs);
    }
    sync_marked(app);
    tk_widget_invalidate(app->list);
    tk_set_visible(app->drill_row, app->drilled);
    if (app->drilled) set_text(app->drill_label, app->drill_value);
    sync_ctx_bar(app);
    relayout(app);
}

/* Mark the row of the track loaded in the engine: the mark
 * follows now_path across view changes because rebuild_rows
 * re-runs this. */
static void sync_marked(player_app *app)
{
    int marked = -1;
    if (app->now_path[0] && app->rows_are_tracks) {
        for (size_t i = 0; i < app->n_rows; i++) {
            const pl_track *t;
            if (app->row_tracks[i] == (size_t)-1) continue;
            t = pl_lib_at(app->lib, app->row_tracks[i]);
            if (t && strcmp(t->path, app->now_path) == 0) {
                marked = (int)i;
                break;
            }
        }
    }
    tk_listview_set_marked(app->list, marked);
}

/* ---------------------------------------------------------------- */
/* playback                                                          */
/* ---------------------------------------------------------------- */

static void save_state(player_app *app)
{
    if (!app->now_path[0]) return;
    pl_state st;
    memset(&st, 0, sizeof(st));
    snprintf(st.path, sizeof(st.path), "%s", app->now_path);
    st.position_ms = app->engine ? pe_position_ms(app->engine) : 0;
    st.volume = app->volume_v;
    st.shuffle = pl_order_shuffle(app->order);
    st.repeat = pl_order_repeat(app->order);
    pl_state_save(app->root_dir, &st);
}

static void close_engine(player_app *app)
{
    if (app->engine) pe_close(app->engine);
    if (app->out) pe_out_free(app->out);
    app->engine = NULL;
    app->out = NULL;
}

/* The play buttons' labels mirror the playing state (the
 * DESIGN SS5.2 label contract: exactly "⏸ 一時停止" /
 * "▶ 再生" on the mini player's / transport button; the NP
 * button's label is the short form). The buttons are
 * icon-only, so the label is the accessible name; the state
 * itself is carried by the real icon (play_arrow / pause,
 * Fluent play / pause filled variants). The setters
 * invalidate the buttons. */
static void sync_play_ui(player_app *app)
{
    tk_button_set_label(app->btn_play,
                        app->playing ? "⏸ 一時停止" : "▶ 再生");
    if (app->ic_play && app->ic_pause)
        tk_button_set_icon(app->btn_play,
                           app->playing ? app->ic_pause : app->ic_play,
                           24, 24);
    tk_button_set_label(app->np_play, app->playing ? "⏸" : "▶");
    if (app->ic_play && app->ic_pause)
        tk_button_set_icon(app->np_play,
                           app->playing ? app->ic_pause : app->ic_play,
                           24, 24);
}

/* Shuffle state on every shuffle control (the NP toggle and,
 * in WinUI, the transport toggle): the toggled state plus
 * the tonal face while on (state shown by fill). */
static void sync_shuffle_ui(player_app *app)
{
    bool on = pl_order_shuffle(app->order);
    tk_button_set_toggled(app->np_shuffle, on);
    if (app->win_shuffle) tk_button_set_toggled(app->win_shuffle, on);
    const tk_theme *t = tk_theme_default();
    tk_button_set_face(app->np_shuffle,
                       on ? &t->surface_alt : NULL);
    if (app->win_shuffle)
        tk_button_set_face(app->win_shuffle,
                           on ? &t->surface_alt : NULL);
}

static const char *repeat_text(pl_repeat r)
{
    return r == PL_REPEAT_ALL ? "リピート: 全て"
         : r == PL_REPEAT_ONE ? "リピート: 1曲" : "リピート: なし";
}

static void sync_repeat_ui(player_app *app)
{
    pl_repeat r = pl_order_repeat(app->order);
    set_text(app->repeat_label, repeat_text(r));
    /* the mini repeat cell shows the state as its label and
     * gains the tonal face while a repeat mode is active
     * (state shown by fill, SS7.3); the icon follows the
     * state too: repeat_one (Fluent repeat_1) for 1曲, the
     * plain repeat (Fluent repeat_all) otherwise */
    tk_button_set_label(app->btn_repeat_mini,
                        r == PL_REPEAT_ALL ? "全て"
                        : r == PL_REPEAT_ONE ? "1曲" : "リピート");
    const uint32_t *ic = r == PL_REPEAT_ONE && app->ic_repeat_one
                             ? app->ic_repeat_one
                             : app->ic_repeat;
    if (ic) {
        tk_button_set_icon(app->btn_repeat_mini, ic, 24, 24);
        tk_button_set_icon(app->np_repeat, ic, 24, 24);
    }
    const tk_theme *t = tk_theme_default();
    tk_button_set_face(app->btn_repeat_mini,
                       r != PL_REPEAT_OFF ? &t->surface_alt : NULL);
    tk_button_set_face(app->np_repeat,
                       r != PL_REPEAT_OFF ? &t->surface_alt : NULL);
}

static void load_artwork(player_app *app, const char *track_path)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", track_path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else dir[0] = 0;
    static const char *names[] = { "cover.jpg", "cover.png",
                                   "folder.jpg", "folder.png" };
    for (size_t i = 0; i < 4; i++) {
        char p[1100];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        struct stat sb;
        if (stat(p, &sb) == 0 && tk_imageview_load(app->art, p)) {
            /* the mini artwork decodes at display size through
             * the budgeted thumbnail path, not a second full
             * decode */
            tk_imageview_load_thumbnail(app->mini_art, p, 56, 56);
            tk_widget_invalidate(app->art);
            tk_widget_invalidate(app->mini_art);
            return;
        }
    }
    /* no cover file: the mode's music-note icon over the
     * flat gray square (alpha must be 0xFF - with a zero
     * alpha byte the blit composites it as fully transparent
     * and the artwork area renders blank). The note is the
     * bdicon asset (music_note / music_note_2), centred;
     * without the asset the plain square remains. */
    static uint32_t ph[64 * 64];
    for (size_t i = 0; i < 64 * 64; i++) ph[i] = 0xFFC9CDD2;
    const uint32_t *note = bd_icon_mode(app->winui, "music_note",
                                        "music_note_2", 48,
                                        tint_of(tk_theme_default()
                                                    ->text_dim));
    if (note) {
        for (int y = 0; y < 48; y++)
            for (int x = 0; x < 48; x++) {
                uint32_t gp = note[y * 48 + x];
                uint32_t ga = gp >> 24;
                if (!ga) continue;
                uint32_t *dp = &ph[(y + 8) * 64 + (x + 8)];
                uint32_t r = (((gp >> 16) & 0xFF) * ga +
                              ((dp[0] >> 16 & 0xFF)) * (255 - ga)) /
                             255;
                uint32_t g = (((gp >> 8) & 0xFF) * ga +
                              ((dp[0] >> 8 & 0xFF)) * (255 - ga)) /
                             255;
                uint32_t b = ((gp & 0xFF) * ga +
                              (dp[0] & 0xFF) * (255 - ga)) / 255;
                *dp = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
    }
    tk_imageview_set_pixels(app->art, ph, 64, 64);
    tk_imageview_set_pixels(app->mini_art, ph, 64, 64);
    tk_widget_invalidate(app->art);
    tk_widget_invalidate(app->mini_art);
}

static bool open_track(player_app *app, const char *path, bool autoplay)
{
    pe_meta meta;
    if (pe_probe(path, &meta) != 0) {
        set_text(app->status, "再生できない形式です");
        return false;
    }
    close_engine(app);
    app->out = app->of ? app->of(app->ud, meta.rate, meta.channels)
                       : pe_out_alsa(NULL, meta.rate, meta.channels);
    if (!app->out) {
        set_text(app->status, "音声出力が開けません");
        return false;
    }
    app->engine = pe_open(path, app->out);
    if (!app->engine) {
        pe_out_free(app->out);
        app->out = NULL;
        set_text(app->status, "再生できません");
        return false;
    }
    pe_set_volume(app->engine, app->volume_v);
    app->dur_ms = meta.duration_ms;
    snprintf(app->now_path, sizeof(app->now_path), "%s", path);
    const pl_track *t = pl_lib_find(app->lib, path);
    const char *title = t ? t->title : meta.title;
    const char *artist = t ? t->artist : meta.artist;
    set_text(app->np_title, title);
    set_text(app->np_artist, artist[0] ? artist : "（不明）");
    set_text(app->np_album, t ? t->album : meta.album);
    /* the mini player's drill target shows title - artist;
     * in WinUI the transport bar's title slot is narrower
     * (the bar shares its row with the whole transport
     * cluster), so it carries the title only - the artist
     * is on the now-playing pane and in the list rows */
    {
        char buf[320];
        if (app->winui) {
            snprintf(buf, sizeof(buf), "%s", title);
        } else {
            snprintf(buf, sizeof(buf), "%s — %s", title,
                     artist[0] ? artist : "（不明）");
        }
        tk_button_set_label(app->mini_title, buf);
        tk_widget_invalidate(app->mini_title);
    }
    load_artwork(app, path);
    app->playing = autoplay;
    sync_play_ui(app);
    sync_marked(app);
    tk_widget_invalidate(app->list);
    /* every advance path funnels through here, and an advance
     * may have consumed the queue head: keep the queue
     * screen's list honest */
    refresh_queue(app);
    save_state(app);
    return true;
}

static void play_context_row(player_app *app, size_t row)
{
    if (!app->rows_are_tracks || row >= app->n_rows) return;
    /* build the context from the visible track rows */
    size_t n = 0;
    const char **paths = malloc((app->n_rows ? app->n_rows : 1) *
                                sizeof(char *));
    size_t start = 0;
    for (size_t i = 0; i < app->n_rows; i++) {
        const pl_track *t;
        if (app->row_tracks[i] == (size_t)-1) continue;
        t = pl_lib_at(app->lib, app->row_tracks[i]);
        if (!t) continue;
        if (i == row) start = n;
        paths[n++] = t->path;
    }
    if (!n) { free(paths); return; }
    pl_order_set_context(app->order, paths, n, start);
    const char *cur = pl_order_current(app->order);
    if (cur) open_track(app, cur, true);
    free(paths);
}

static void toggle_play(player_app *app)
{
    if (!app->engine) {
        /* nothing loaded: the play button starts the selected
         * track (the harness contract), else the restored /
         * current one */
        int sel = tk_listview_selected(app->list);
        if (sel >= 0 && app->rows_are_tracks) {
            play_context_row(app, (size_t)sel);
            return;
        }
        if (app->now_path[0]) { open_track(app, app->now_path, true); return; }
        const char *cur = pl_order_current(app->order);
        if (cur) open_track(app, cur, true);
        return;
    }
    /* pump-driven engine: pause = stop pumping; the decoder
     * position is untouched */
    app->playing = !app->playing;
    sync_play_ui(app);
    save_state(app);
}

static void skip_next(player_app *app)
{
    const char *nx = pl_order_next(app->order, false);
    if (nx) open_track(app, nx, true);
    else if (app->engine) {
        app->playing = false;
        sync_play_ui(app);
        save_state(app);
    }
}

static void skip_prev(player_app *app)
{
    if (app->engine && pe_position_ms(app->engine) > 3000) {
        pe_seek_ms(app->engine, 0);
        return;
    }
    const char *pv = pl_order_prev(app->order);
    if (pv) open_track(app, pv, true);
}

/* ---------------------------------------------------------------- */
/* transport handlers                                                */
/* ---------------------------------------------------------------- */

static void on_play_btn(tk_widget *w, void *ud)
{
    toggle_play((player_app *)ud);
    (void)w;
}

static void on_next_btn(tk_widget *w, void *ud)
{
    skip_next((player_app *)ud);
    (void)w;
}

static void on_prev_btn(tk_widget *w, void *ud)
{
    skip_prev((player_app *)ud);
    (void)w;
}

static void on_repeat_btn(tk_widget *w, void *ud)
{
    player_app *app = ud;
    pl_repeat r = pl_order_repeat(app->order);
    pl_order_set_repeat(app->order,
                        r == PL_REPEAT_OFF ? PL_REPEAT_ALL
                        : r == PL_REPEAT_ALL ? PL_REPEAT_ONE
                                             : PL_REPEAT_OFF);
    sync_repeat_ui(app);
    save_state(app);
    (void)w;
}

static void on_shuffle_btn(tk_widget *w, void *ud)
{
    player_app *app = ud;
    pl_order_set_shuffle(app->order, tk_button_toggled(w),
                         SHUFFLE_SEED);
    sync_shuffle_ui(app);
    save_state(app);
}

static void on_progress(tk_widget *w, void *ud)
{
    player_app *app = ud;
    if (app->prog_guard || !app->engine) return;
    pe_seek_ms(app->engine,
               (uint64_t)(tk_slider_value(w) * app->dur_ms));
}

static void sync_volume_ui(player_app *app)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)(app->volume_v * 100 + 0.5));
    app->vol_guard = true;
    tk_slider_set_value(app->volume, (float)app->volume_v);
    tk_slider_set_value(app->np_volume, (float)app->volume_v);
    app->vol_guard = false;
    set_text(app->vol_label, buf);
    set_text(app->np_vol_label, buf);
}

static void on_volume(tk_widget *w, void *ud)
{
    player_app *app = ud;
    if (app->vol_guard) return;
    app->volume_v = tk_slider_value(w);
    if (app->engine) pe_set_volume(app->engine, app->volume_v);
    sync_volume_ui(app);
    save_state(app);
}

/* ---------------------------------------------------------------- */
/* library: tabs, search, selection, context bar                     */
/* ---------------------------------------------------------------- */

static int selected_track(player_app *app)
{
    int sel = tk_listview_selected(app->list);
    if (sel < 0 || sel >= (int)app->n_rows || !app->rows_are_tracks)
        return -1;
    if (app->row_tracks[sel] == (size_t)-1) return -1;
    return (int)app->row_tracks[sel];
}

/* The context bar shows the operations for the current
 * selection only (SS7.3: at most 4 + overflow). Runs after
 * every selection / view change; relayout makes the cells'
 * rects valid for immediate clicks (SS5.2). */
static void sync_ctx_bar(player_app *app)
{
    int sel = tk_listview_selected(app->list);
    bool track_mode = sel >= 0 && app->rows_are_tracks;
    bool playlist_mode = sel >= 0 && !app->rows_are_tracks &&
                         app->view == V_PLAYLISTS && !app->drilled;
    bool value_mode = sel >= 0 && !app->rows_are_tracks &&
                      !playlist_mode;
    tk_set_visible(app->ctx_cells[CTX_PLAY], track_mode);
    tk_set_visible(app->ctx_cells[CTX_NEXT], track_mode);
    tk_set_visible(app->ctx_cells[CTX_LATER], track_mode);
    tk_set_visible(app->ctx_cells[CTX_ADDLIST], track_mode);
    tk_set_visible(app->ctx_cells[CTX_OPEN],
                   value_mode || playlist_mode);
    tk_set_visible(app->ctx_cells[CTX_RENAME], playlist_mode);
    tk_set_visible(app->ctx_cells[CTX_DELETE], playlist_mode);
    tk_set_visible(app->ctx_bar,
                   track_mode || playlist_mode || value_mode);
    tk_bottombar_set_selected(app->ctx_bar, -1);
    relayout(app);
}

static void on_list_select(tk_widget *w, void *ud)
{
    sync_ctx_bar((player_app *)ud);
    (void)w;
}

static void on_list_activate(tk_widget *w, void *ud)
{
    player_app *app = ud;
    player_app_open_row(app, (size_t)tk_listview_selected(w));
}

static void sync_win_chrome(player_app *app);   /* fwd */

static void on_tab(tk_widget *w, void *ud)
{
    player_app *app = ud;
    app->view = tk_tabbar_selected(w);
    app->drilled = false;
    rebuild_rows(app);
    set_text(app->status, "");
    if (app->winui) {
        /* a library view always shows the list pane */
        app->win_show = WIN_SHOW_LIST;
        sync_win_chrome(app);
    }
}

static void on_search_change(tk_widget *w, void *ud)
{
    rebuild_rows((player_app *)ud);
    (void)w;
}

static void on_drill_back(tk_widget *w, void *ud)
{
    player_app *app = ud;
    app->drilled = false;
    rebuild_rows(app);
    if (app->winui) sync_win_chrome(app);
    (void)w;
}

static void on_ctx_play(tk_widget *w, void *ud)
{
    player_app_play_selected((player_app *)ud);
    (void)w;
}

static void on_q_next(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int ti = selected_track(app);
    if (ti < 0) { set_text(app->status, "曲を選んでください"); return; }
    const pl_track *t = pl_lib_at(app->lib, (size_t)ti);
    pl_order_queue_next(app->order, t->path);
    refresh_queue(app);
    set_text(app->status, "次に再生へ追加しました");
    (void)w;
}

static void on_q_later(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int ti = selected_track(app);
    if (ti < 0) { set_text(app->status, "曲を選んでください"); return; }
    const pl_track *t = pl_lib_at(app->lib, (size_t)ti);
    pl_order_queue_later(app->order, t->path);
    refresh_queue(app);
    set_text(app->status, "キューへ追加しました");
    (void)w;
}

static void on_ctx_open(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int sel = tk_listview_selected(app->list);
    if (sel >= 0) player_app_open_row(app, (size_t)sel);
    (void)w;
}

/* ---------------------------------------------------------------- */
/* playlist dialog (add/create) + rename dialog                      */
/* ---------------------------------------------------------------- */

static void on_list_create(tk_widget *w, void *ud)
{
    player_app *app = ud;
    char name[128];
    field_text(app->list_name, name, sizeof(name));
    if (!name[0]) {
        set_text(app->status, "プレイリスト名を入力してください");
        return;
    }
    if (pl_lists_create(app->lib, name) == 0) {
        char buf[192];
        snprintf(buf, sizeof(buf), "プレイリスト「%s」を作成しました",
                 name);
        set_text(app->status, buf);
        if (app->view == V_PLAYLISTS && !app->drilled) rebuild_rows(app);
    } else {
        set_text(app->status, "同名のプレイリストがあります");
    }
    (void)w;
}

static void on_list_add(tk_widget *w, void *ud)
{
    player_app *app = ud;
    char name[128];
    field_text(app->list_name, name, sizeof(name));
    if (!name[0]) {
        set_text(app->status, "プレイリスト名を入力してください");
        return;
    }
    /* create-then-add: the old flow created the playlist on
     * Add when it did not exist; the dialog keeps that promise
     * (SS7.3). pl_lists_add does not dedupe. */
    bool known = false;
    for (size_t i = 0; i < pl_lists_count(app->lib); i++)
        if (strcmp(pl_lists_name(app->lib, i), name) == 0) known = true;
    if (!known && pl_lists_create(app->lib, name) != 0) {
        set_text(app->status, "プレイリストを作成できません");
        return;
    }
    if (app->dlg_path[0] &&
        pl_lists_add(app->lib, name, app->dlg_path) == 0) {
        char buf[192];
        snprintf(buf, sizeof(buf), "「%s」へ追加しました", name);
        set_text(app->status, buf);
        if (app->view == V_PLAYLISTS && !app->drilled) rebuild_rows(app);
    } else {
        set_text(app->status, "追加できませんでした");
    }
    tk_dialog_close(app->root, app->list_dlg);
    (void)w;
}

static void on_list_cancel(tk_widget *w, void *ud)
{
    player_app *app = ud;
    tk_dialog_close(app->root, app->list_dlg);
    (void)w;
}

static void on_ctx_addlist(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int ti = selected_track(app);
    if (ti < 0) { set_text(app->status, "曲を選んでください"); return; }
    const pl_track *t = pl_lib_at(app->lib, (size_t)ti);
    snprintf(app->dlg_path, sizeof(app->dlg_path), "%s", t->path);
    /* prefill with the first playlist so a single tap on Add
     * does the common thing */
    if (pl_lists_count(app->lib))
        tk_textfield_set_text(app->list_name,
                              pl_lists_name(app->lib, 0));
    tk_dialog_show(app->root, app->list_dlg);
    relayout(app);              /* dialog cells get valid rects */
    tk_focus(app->root, app->list_name);
    (void)w;
}

static void on_ctx_rename(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int sel = tk_listview_selected(app->list);
    if (sel < 0 || sel >= (int)app->n_rows || app->rows_are_tracks)
        return;
    snprintf(app->rename_target, sizeof(app->rename_target), "%s",
             app->row_labels[sel]);
    tk_textfield_set_text(app->rename_field, app->rename_target);
    tk_dialog_show(app->root, app->rename_dlg);
    relayout(app);
    tk_focus(app->root, app->rename_field);
    (void)w;
}

static void on_rename_apply(tk_widget *w, void *ud)
{
    player_app *app = ud;
    char name[128];
    field_text(app->rename_field, name, sizeof(name));
    if (name[0] &&
        pl_lists_rename(app->lib, app->rename_target, name) == 0) {
        char buf[192];
        snprintf(buf, sizeof(buf), "「%s」へ名前を変更しました", name);
        set_text(app->status, buf);
        if (app->drilled &&
            strcmp(app->drill_value, app->rename_target) == 0)
            snprintf(app->drill_value, sizeof(app->drill_value),
                     "%s", name);
        rebuild_rows(app);
    } else {
        set_text(app->status, "名前を変更できませんでした");
    }
    tk_dialog_close(app->root, app->rename_dlg);
    (void)w;
}

static void on_rename_cancel(tk_widget *w, void *ud)
{
    player_app *app = ud;
    tk_dialog_close(app->root, app->rename_dlg);
    (void)w;
}

static void on_ctx_delete(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int sel = tk_listview_selected(app->list);
    if (sel < 0 || sel >= (int)app->n_rows || app->rows_are_tracks)
        return;
    char name[128];
    snprintf(name, sizeof(name), "%s", app->row_labels[sel]);
    if (pl_lists_delete(app->lib, name) == 0) {
        char buf[192];
        snprintf(buf, sizeof(buf), "プレイリスト「%s」を削除しました",
                 name);
        set_text(app->status, buf);
        rebuild_rows(app);
    } else {
        set_text(app->status, "削除できませんでした");
    }
    (void)w;
}

/* ---------------------------------------------------------------- */
/* overflow menus (rebuilt fresh on each open, files-style)          */
/* ---------------------------------------------------------------- */

static void on_scan(player_app *app)
{
    /* the real rescan (the previous UI only printed the words
     * without scanning - recorded in the report) */
    for (size_t i = 0; i < app->n_scan_dirs; i++)
        pl_lib_scan(app->lib, app->scan_dirs[i]);
    app->drilled = false;
    rebuild_rows(app);
    char buf[96];
    snprintf(buf, sizeof(buf), "再スキャンしました（%zu曲）",
             pl_lib_count(app->lib));
    set_text(app->status, buf);
}

static void on_lib_menu(tk_widget *w, void *ud);
static void on_lib_overflow(tk_widget *w, void *ud)
{
    player_app *app = ud;
    tk_widget_free(app->lib_menu);
    app->lib_menu = tk_menu_new();
    tk_menu_add_item(app->lib_menu, "再スキャン", 1);
    tk_menu_add_item(app->lib_menu, "キューを表示", 2);
    tk_menu_add_check_item(app->lib_menu, "音量", 3,
                           tk_is_visible(app->vol_row));
    tk_menu_set_on_select(app->lib_menu, on_lib_menu, app);
    tk_rect r = tk_widget_rect(w);
    tk_menu_popup(app->root, app->lib_menu, r.x + r.w, r.y + r.h);
}

static void on_lib_menu(tk_widget *w, void *ud)
{
    player_app *app = ud;
    switch (tk_menu_last_id(w)) {
    case 1: on_scan(app); break;
    case 2: player_app_show_queue(app); break;
    case 3:
        tk_set_visible(app->vol_row, !tk_is_visible(app->vol_row));
        relayout(app);
        break;
    }
}

static void on_search_toggle(tk_widget *w, void *ud)
{
    player_app *app = ud;
    bool vis = tk_is_visible(app->search_row);
    tk_set_visible(app->search_row, !vis);
    if (!vis) tk_focus(app->root, app->search);
    relayout(app);
    (void)w;
}

static void on_np_menu(tk_widget *w, void *ud);
static void on_np_overflow(tk_widget *w, void *ud)
{
    player_app *app = ud;
    tk_widget_free(app->np_menu);
    app->np_menu = tk_menu_new();
    tk_menu_add_check_item(app->np_menu, "残り時間を表示", 1,
                           app->show_remaining);
    tk_menu_add_item(app->np_menu, "キューを開く", 2);
    tk_menu_set_on_select(app->np_menu, on_np_menu, app);
    tk_rect r = tk_widget_rect(w);
    tk_menu_popup(app->root, app->np_menu, r.x + r.w, r.y + r.h);
}

static void on_np_menu(tk_widget *w, void *ud)
{
    player_app *app = ud;
    switch (tk_menu_last_id(w)) {
    case 1:
        app->show_remaining = tk_menu_item_checked(w, 1);
        player_app_tick(app, 0);        /* repaint the time line */
        break;
    case 2: player_app_show_queue(app); break;
    }
}

static void on_back_to_lib(tk_widget *w, void *ud)
{
    show_screen((player_app *)ud, SCR_LIB);
    (void)w;
}

static void on_mini_title(tk_widget *w, void *ud)
{
    player_app_show_now_playing((player_app *)ud);
    (void)w;
}

/* ---------------------------------------------------------------- */
/* queue                                                             */
/* ---------------------------------------------------------------- */

static void refresh_queue(player_app *app)
{
    size_t n = pl_order_queue_count(app->order);
    const char **labels = malloc((n ? n : 1) * sizeof(char *));
    char **owned = calloc(n ? n : 1, sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        const char *path = pl_order_queue_at(app->order, i);
        const pl_track *t = pl_lib_find(app->lib, path);
        if (t) {
            char buf[320];
            snprintf(buf, sizeof(buf), "%s — %s", t->title, t->artist);
            labels[i] = owned[i] = strdup(buf);
        } else {
            labels[i] = path;
        }
    }
    tk_listview_set_items(app->queue_list, labels, n);
    for (size_t i = 0; i < n; i++) free(owned[i]);
    free(owned);
    free(labels);
    tk_widget_invalidate(app->queue_list);
}

static void on_queue_up(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int sel = tk_listview_selected(app->queue_list);
    if (sel > 0 &&
        pl_order_queue_move(app->order, (size_t)sel,
                            (size_t)(sel - 1)) == 0) {
        refresh_queue(app);
        tk_listview_select(app->queue_list, sel - 1);
    }
    (void)w;
}

static void on_queue_down(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int sel = tk_listview_selected(app->queue_list);
    if (sel >= 0 &&
        pl_order_queue_move(app->order, (size_t)sel,
                            (size_t)(sel + 1)) == 0) {
        refresh_queue(app);
        tk_listview_select(app->queue_list, sel + 1);
    }
    (void)w;
}

static void on_queue_del(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int sel = tk_listview_selected(app->queue_list);
    if (sel >= 0 && pl_order_queue_remove(app->order, (size_t)sel) == 0) {
        refresh_queue(app);
        if (sel >= (int)pl_order_queue_count(app->order))
            sel = (int)pl_order_queue_count(app->order) - 1;
        if (sel >= 0) tk_listview_select(app->queue_list, sel);
    }
    (void)w;
}

static void on_queue_clear(tk_widget *w, void *ud)
{
    player_app *app = ud;
    pl_order_queue_clear(app->order);
    refresh_queue(app);
    set_text(app->status, "キューを空にしました");
    (void)w;
}

static const char *view_name(int view)
{
    switch (view) {
    case V_SONGS:     return "曲";
    case V_ALBUMS:    return "アルバム";
    case V_ARTISTS:   return "アーティスト";
    case V_GENRES:    return "ジャンル";
    case V_PLAYLISTS: return "プレイリスト";
    default:          return "";
    }
}

/* WinUI chrome: the right pane shows exactly one of list /
 * queue / now playing, the heading names it, and the nav
 * selection follows (library views select their own item;
 * queue / now playing select theirs). */
static void sync_win_chrome(player_app *app)
{
    if (!app->winui) return;
    tk_set_visible(app->list, app->win_show == WIN_SHOW_LIST);
    tk_set_visible(app->queue_list, app->win_show == WIN_SHOW_QUEUE);
    tk_set_visible(app->queue_bar, app->win_show == WIN_SHOW_QUEUE);
    tk_set_visible(app->win_np_pane, app->win_show == WIN_SHOW_NP);
    tk_set_visible(app->drill_row,
                   app->win_show == WIN_SHOW_LIST && app->drilled);
    if (app->win_show == WIN_SHOW_LIST) {
        sync_ctx_bar(app);   /* re-derive from the selection */
    } else {
        tk_set_visible(app->ctx_bar, false);
    }
    const char *title = app->win_show == WIN_SHOW_QUEUE
                            ? "キュー"
                        : app->win_show == WIN_SHOW_NP
                            ? "再生中"
                        : app->drilled ? app->drill_value
                                       : view_name(app->view);
    set_text(app->win_title, title);
    int sel = app->win_show == WIN_SHOW_QUEUE
                  ? NAV_QUEUE
              : app->win_show == WIN_SHOW_NP ? NAV_NP : app->view;
    tk_drawer_select(app->win_nav, sel);
    relayout(app);
}

static void on_win_nav(tk_widget *w, void *ud)
{
    player_app *app = ud;
    int id = tk_drawer_last_id(w);
    if (id == NAV_QUEUE) {
        player_app_show_queue(app);
    } else if (id == NAV_NP) {
        player_app_show_now_playing(app);
    } else if (id >= 0 && id <= V_PLAYLISTS) {
        player_app_set_view(app, id);
    }
}

static void show_screen(player_app *app, int scr)
{
    app->screen = scr;
    if (app->winui) {
        /* WinUI has no drilled screens: queue / now playing
         * swap the right pane's content, library is the list */
        app->win_show = scr == SCR_QUEUE   ? WIN_SHOW_QUEUE
                        : scr == SCR_NP    ? WIN_SHOW_NP
                                           : WIN_SHOW_LIST;
        if (scr == SCR_QUEUE) refresh_queue(app);
        sync_win_chrome(app);
        return;
    }
    tk_set_visible(app->scr_lib, scr == SCR_LIB);
    tk_set_visible(app->scr_np, scr == SCR_NP);
    tk_set_visible(app->scr_queue, scr == SCR_QUEUE);
    if (scr == SCR_QUEUE) refresh_queue(app);
    relayout(app);
}

/* ---------------------------------------------------------------- */
/* public actions                                                    */
/* ---------------------------------------------------------------- */

void player_app_show_queue(player_app *app) { show_screen(app, SCR_QUEUE); }

void player_app_show_now_playing(player_app *app)
{
    show_screen(app, SCR_NP);
}


/* Back key (TK_KEY_BACK via the WM): now playing / queue ->
 * library, drilled list -> top level, exactly the on-screen
 * back path (player_app_back). Unconsumed in the library top
 * level so the shell can have the key. */
static bool player_back_key(void *ud, tk_event *ev)
{
    player_app *app = ud;
    if (!ev || ev->type != TK_EV_KEY_DOWN || ev->key != TK_KEY_BACK)
        return false;
    if (app->winui) {
        if (app->win_show == WIN_SHOW_LIST && !app->drilled)
            return false;
    } else {
        if (app->screen == SCR_LIB && !app->drilled) return false;
    }
    player_app_back(app);
    return true;
}

void player_app_back(player_app *app)
{
    if (app->winui) {
        /* WinUI: queue / now playing return to the list pane;
         * in the list pane, back undrills as in Android */
        if (app->win_show != WIN_SHOW_LIST) {
            show_screen(app, SCR_LIB);
            return;
        }
        if (app->drilled) {
            app->drilled = false;
            rebuild_rows(app);
            sync_win_chrome(app);
        }
        return;
    }
    if (app->screen != SCR_LIB) { show_screen(app, SCR_LIB); return; }
    if (app->drilled) {
        app->drilled = false;
        rebuild_rows(app);
    }
}

void player_app_set_view(player_app *app, int view)
{
    if (view < 0 || view > V_PLAYLISTS) return;
    if (tk_tabbar_selected(app->tabbar) != view) {
        tk_tabbar_select(app->tabbar, view);   /* fires on_tab */
    } else {
        app->view = view;
        app->drilled = false;
        rebuild_rows(app);
        if (app->winui) {
            app->win_show = WIN_SHOW_LIST;
            sync_win_chrome(app);
        }
    }
}

void player_app_open_row(player_app *app, size_t row)
{
    if (row >= app->n_rows) return;
    if (app->rows_are_tracks) {
        play_context_row(app, row);
        return;
    }
    snprintf(app->drill_value, sizeof(app->drill_value), "%s",
             app->row_labels[row]);
    app->drilled = true;
    rebuild_rows(app);
    if (app->winui) sync_win_chrome(app);
}

void player_app_play_selected(player_app *app)
{
    int sel = tk_listview_selected(app->list);
    if (sel >= 0 && app->rows_are_tracks)
        play_context_row(app, (size_t)sel);
}

int player_app_open_files(player_app *app, const char *const *paths,
                          size_t n)
{
    if (!n) return -1;
    pl_order_set_context(app->order, paths, n, 0);
    const char *cur = pl_order_current(app->order);
    if (cur) open_track(app, cur, true);
    return 0;
}

void player_app_tick(player_app *app, uint64_t mono_ms)
{
    (void)mono_ms;
    if (!app->engine) return;
    app->tick_count++;
    if (app->playing) {
        pe_pump(app->engine, PUMP_FRAMES);
        if (pe_eof(app->engine)) {
            const char *nx = pl_order_next(app->order, true);
            if (nx) { open_track(app, nx, true); return; }
            app->playing = false;
            sync_play_ui(app);
            save_state(app);
        }
    }
    uint64_t pos = pe_position_ms(app->engine);
    uint64_t dur = app->dur_ms;
    char buf[96], tp[24], td[24];
    fmt_time(tp, sizeof(tp), pos);
    fmt_time(td, sizeof(td), dur);
    if (app->show_remaining && dur > pos) {
        char tr[24];
        fmt_time(tr, sizeof(tr), dur - pos);
        snprintf(buf, sizeof(buf), "%s / -%s", tp, tr);
    } else {
        snprintf(buf, sizeof(buf), "%s / %s", tp, td);
    }
    set_text(app->time_label, buf);
    if (dur) {
        app->prog_guard = true;
        tk_slider_set_value(app->progress, (float)((double)pos / (double)dur));
        app->prog_guard = false;
    }
    /* persist position periodically (state.txt is rewritten
     * atomically by playmodel/bdstore, so an abrupt reset
     * loses at most the last seconds) */
    if (app->tick_count % 50 == 0) save_state(app);
}

tk_root *player_app_root(player_app *app) { return app->root; }
bool player_app_playing(const player_app *app) { return app->playing; }
const char *player_app_now_path(const player_app *app)
{
    return app->now_path[0] ? app->now_path : NULL;
}
size_t player_app_rows(const player_app *app) { return app->n_rows; }
int player_app_view(const player_app *app) { return app->view; }
tk_widget *player_app_list_widget(player_app *app) { return app->list; }
tk_widget *player_app_queue_widget(player_app *app)
{
    return app->queue_list;
}
tk_widget *player_app_view_button(player_app *app, int view)
{
    (void)view;
    return app->tabbar;    /* segments, not child widgets (SS7.3) */
}
tk_widget *player_app_tabbar(player_app *app) { return app->tabbar; }
tk_widget *player_app_play_button(player_app *app) { return app->btn_play; }
tk_widget *player_app_next_button(player_app *app) { return app->btn_next; }
tk_widget *player_app_prev_button(player_app *app) { return app->btn_prev; }
tk_widget *player_app_repeat_button(player_app *app)
{
    return app->btn_repeat_mini;
}
tk_widget *player_app_repeat_label(player_app *app)
{
    return app->repeat_label;
}
tk_widget *player_app_np_title(player_app *app) { return app->np_title; }
tk_widget *player_app_list_name_field(player_app *app)
{
    return app->list_name;
}
tk_widget *player_app_list_create_button(player_app *app)
{
    return app->btn_list_create;
}
tk_widget *player_app_list_add_button(player_app *app)
{
    return app->btn_list_add;
}
tk_widget *player_app_add_to_playlist_button(player_app *app)
{
    return app->ctx_cells[CTX_ADDLIST];
}
tk_widget *player_app_queue_later_button(player_app *app)
{
    return app->ctx_cells[CTX_LATER];
}
tk_widget *player_app_queue_next_button(player_app *app)
{
    return app->ctx_cells[CTX_NEXT];
}
tk_widget *player_app_mini_title_button(player_app *app)
{
    return app->mini_title;
}

/* ---------------------------------------------------------------- */
/* construction                                                      */
/* ---------------------------------------------------------------- */

player_app *player_app_new(const char *data_root,
                           const char *const *scan_dirs, size_t n_dirs,
                           pl_out_factory of, void *ud, int w, int h)
{
    player_app *app = calloc(1, sizeof(*app));
    app->of = of;
    app->ud = ud;
    app->volume_v = 0.8;
    app->win_w = w;
    app->win_h = h;
    snprintf(app->root_dir, sizeof(app->root_dir), "%s",
             data_root ? data_root : ".");
    app->lib = pl_lib_open(app->root_dir);
    if (!app->lib) { free(app); return NULL; }
    app->order = pl_order_new();
    for (size_t i = 0; i < n_dirs && i < 8; i++) {
        snprintf(app->scan_dirs[app->n_scan_dirs],
                 sizeof(app->scan_dirs[0]), "%s", scan_dirs[i]);
        app->n_scan_dirs++;
        pl_lib_scan(app->lib, scan_dirs[i]);
    }

    /* mode from the theme main() installed (never from reg:
     * the constructor's contract is the theme, see header) */
    app->winui = tk_theme_default()->style == 2;
    app->win_show = WIN_SHOW_LIST;

    /* the icon set, borrowed from the bdicon cache. The
     * transport pair tints with on_accent (the play button
     * is FILLED); everything else uses the theme ink. In
     * WinUI the Fluent play / pause / next / previous are
     * the filled variants of the set. */
    {
        const tk_theme *th = tk_theme_default();
        uint32_t ink = tint_of(th->text);
        uint32_t on_acc = tint_of(th->on_accent);
        app->ic_play = mode_icon(app, "play_arrow", "play", on_acc);
        app->ic_pause = mode_icon(app, "pause", "pause", on_acc);
        app->ic_prev = mode_icon(app, "skip_previous", "previous",
                                 ink);
        app->ic_next = mode_icon(app, "skip_next", "next", ink);
        app->ic_repeat = mode_icon(app, "repeat", "repeat_all", ink);
        app->ic_repeat_one = mode_icon(app, "repeat_one",
                                       "repeat_1", ink);
        app->ic_shuffle = mode_icon(app, "shuffle", "shuffle", ink);
        app->ic_back = mode_icon(app, "arrow_back", "arrow_left",
                                 ink);
        app->ic_vol = mode_icon(app, "volume_up", "speaker_2", ink);
    }
    const uint32_t *ic_search = mode_icon(app, "search", "search",
                                          tint_of(tk_theme_default()
                                                      ->text));
    const uint32_t *ic_dots = mode_icon(app, "more_vert",
                                        "more_vertical",
                                        tint_of(tk_theme_default()
                                                    ->text));
    const uint32_t *ic_add = mode_icon(app, "add", "add",
                                       tint_of(tk_theme_default()
                                                   ->text));
    const uint32_t *ic_addlist = mode_icon(app, "playlist_add",
                                           "add",
                                           tint_of(tk_theme_default()
                                                       ->text));
    const uint32_t *ic_open = mode_icon(app, "arrow_forward",
                                        "arrow_right",
                                        tint_of(tk_theme_default()
                                                    ->text));
    const uint32_t *ic_rename = mode_icon(app, "edit", "rename",
                                          tint_of(tk_theme_default()
                                                      ->text));
    const uint32_t *ic_del = mode_icon(app, "delete", "delete",
                                       tint_of(tk_theme_default()
                                                   ->text));
    const uint32_t *ic_up = mode_icon(app, "arrow_upward",
                                      "arrow_up",
                                      tint_of(tk_theme_default()
                                                  ->text));
    const uint32_t *ic_down = mode_icon(app, "arrow_downward",
                                        "arrow_down",
                                        tint_of(tk_theme_default()
                                                    ->text));

    /* WinUI: the top is a horizontal split - the navigation
     * pane spans the full window height (so the drawer never
     * overflows into the transport bar), and the right
     * column stacks the content over the transport bar. */
    app->top = tk_box_new(app->winui ? TK_DIR_HORIZONTAL
                                     : TK_DIR_VERTICAL,
                          0, 0);
    /* int12: single-column form gets the page margin on the
     * root; the WinUI split keeps its navigation pane
     * edge-attached and margins the library column instead
     * (lib_parent, below). */
    if (!app->winui) bd_page_pad_x(app->top);

    /* ---------- library screen (Android) / right pane (WinUI)
     * The shared library widgets (tabs, drill row, search,
     * list, context bar, status) are parented to lib_parent:
     * the Android library screen, or the WinUI right pane. */
    tk_widget *lib_parent;
    tk_widget *nav_pane = NULL;   /* WinUI: holds the tab bar */
    tk_widget *win_main = NULL;   /* WinUI: the right column */
    if (app->winui) {
        win_main = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        app->scr_win = win_main;
        nav_pane = tk_box_new(TK_DIR_VERTICAL, 8, 12);
        tk_widget_set_min_size(nav_pane, 224, 0);
        tk_add_child(app->top, nav_pane);
        tk_widget *nav_title = tk_label_new("ミュージック");
        tk_widget_set_text_style(nav_title, TK_TEXT_HEADING);
        tk_add_child(nav_pane, nav_title);
        app->win_nav = tk_drawer_new("ライブラリ");
        {
            const tk_theme *th = tk_theme_default();
            uint32_t ink = tint_of(th->text);
            tk_drawer_add_item(
                app->win_nav, "曲", V_SONGS,
                mode_icon(app, "music_note", "music_note_2", ink),
                24, 24);
            tk_drawer_add_item(
                app->win_nav, "アルバム", V_ALBUMS,
                mode_icon(app, "album", "album", ink), 24, 24);
            tk_drawer_add_item(
                app->win_nav, "アーティスト", V_ARTISTS,
                mode_icon(app, "group", "people", ink), 24, 24);
            tk_drawer_add_item(
                app->win_nav, "プレイリスト", V_PLAYLISTS,
                mode_icon(app, "queue_music", "list", ink), 24, 24);
            tk_drawer_add_separator(app->win_nav);
            tk_drawer_add_item(
                app->win_nav, "キュー", NAV_QUEUE,
                mode_icon(app, "playlist_play", "task_list", ink),
                24, 24);
            tk_drawer_add_item(
                app->win_nav, "再生中", NAV_NP,
                mode_icon(app, "play_circle", "play_circle", ink),
                24, 24);
        }
        tk_drawer_set_on_select(app->win_nav, on_win_nav, app);
        /* int10 (independent review B): same drawer wrap as
         * settings - 473px of destinations in a 304px pane
         * clipped the last rows (C5/C10). */
        tk_widget *nav_scroll = tk_scrollview_new();
        tk_add_child(nav_pane, nav_scroll);
        tk_set_flex(nav_scroll, 1);
        tk_drawer_attach(nav_scroll, app->win_nav);
        tk_drawer_select(app->win_nav, V_SONGS);

        tk_set_flex(win_main, 1);
        tk_add_child(app->top, win_main);
        lib_parent = tk_box_new(TK_DIR_VERTICAL, 0, 12);
        /* int12: the page margin lives on the library column,
         * not on win_main: the resident transport bar under it
         * is a bar surface (full-bleed like the shell's bar),
         * and its title button is the flex remainder of a
         * fixed cluster - margining the whole column starved
         * it to 0px at 700px-wide windows (test_matrix
         * floor24). */
        tk_box_set_pad_x(lib_parent, BD_PAGE_PAD_X);
        tk_set_flex(lib_parent, 1);
        tk_add_child(win_main, lib_parent);
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->win_title = tk_label_new("曲");
        tk_widget_set_text_style(app->win_title, TK_TEXT_HEADING);
        tk_set_flex(app->win_title, 1);
        tk_add_child(hdr, app->win_title);
        tk_add_child(hdr, header_icon_btn("検索", ic_search,
                                          on_search_toggle, app));
        tk_add_child(hdr, header_icon_btn("その他", ic_dots,
                                          on_lib_overflow, app));
        tk_widget_set_min_size(hdr, 0, 56);
        tk_add_child(lib_parent, hdr);
    } else {
        app->scr_lib = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(app->scr_lib, 1);
        tk_add_child(app->top, app->scr_lib);
        lib_parent = app->scr_lib;
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_widget *title = tk_label_new("ミュージック");
        tk_widget_set_text_style(title, TK_TEXT_HEADING);
        tk_set_flex(title, 1);
        tk_add_child(hdr, title);
        tk_add_child(hdr, header_icon_btn("検索", ic_search,
                                          on_search_toggle, app));
        tk_add_child(hdr, header_icon_btn("その他", ic_dots,
                                          on_lib_overflow, app));
        tk_widget_set_min_size(hdr, 0, 56);
        tk_add_child(lib_parent, hdr);
    }
    {
        /* volume row: hidden until the overflow menu asks
         * (SS7.3); the NP screen carries its own row */
        app->vol_row = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
        tk_widget *vl = tk_label_new("音量");
        tk_add_child(app->vol_row, vl);
        app->volume = tk_slider_new(0.8f);
        tk_slider_set_on_change(app->volume, on_volume, app);
        tk_set_flex(app->volume, 1);
        tk_add_child(app->vol_row, app->volume);
        app->vol_label = tk_label_new("80%");
        tk_add_child(app->vol_row, app->vol_label);
        tk_set_visible(app->vol_row, false);
        tk_add_child(lib_parent, app->vol_row);

        /* the tab bar is the view truth in BOTH modes: in
         * WinUI it lives hidden at the nav pane's foot and
         * the drawer drives it (and it drives the drawer
         * back through on_tab -> sync_win_chrome) */
        app->tabbar = tk_tabbar_new();
        tk_tabbar_add(app->tabbar, "曲");
        tk_tabbar_add(app->tabbar, "アルバム");
        tk_tabbar_add(app->tabbar, "アーティスト");
        tk_tabbar_add(app->tabbar, "ジャンル");
        tk_tabbar_add(app->tabbar, "プレイリスト");
        {
            uint32_t ink = tint_of(tk_theme_default()->text);
            static const char *tab_mat[5] = {
                "music_note", "album", "group", "category",
                "queue_music"
            };
            static const char *tab_flu[5] = {
                "music_note_2", "album", "people", "tag", "list"
            };
            for (int i = 0; i < 5; i++) {
                const uint32_t *ic = bd_icon_mode(app->winui,
                                                  tab_mat[i],
                                                  tab_flu[i],
                                                  24, ink);
                if (ic) tk_tabbar_set_item_icon(app->tabbar, i,
                                                ic, 24, 24);
            }
        }
        tk_tabbar_set_on_select(app->tabbar, on_tab, app);
        if (app->winui) {
            tk_add_child(nav_pane, app->tabbar);
            tk_set_visible(app->tabbar, false);
        } else {
            tk_add_child(lib_parent, app->tabbar);
        }

        app->drill_row = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_add_child(app->drill_row,
                     header_back_btn(app, on_drill_back, app));
        app->drill_label = tk_label_new("");
        tk_widget_set_text_style(app->drill_label, TK_TEXT_HEADING);
        tk_set_flex(app->drill_label, 1);
        tk_add_child(app->drill_row, app->drill_label);
        tk_set_visible(app->drill_row, false);
        tk_add_child(lib_parent, app->drill_row);

        app->search_row = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->search = tk_textfield_new("");
        tk_textfield_set_placeholder(app->search, "曲を検索");
        tk_textfield_set_on_change(app->search, on_search_change, app);
        tk_set_flex(app->search, 1);
        tk_add_child(app->search_row, app->search);
        tk_set_visible(app->search_row, false);
        tk_add_child(lib_parent, app->search_row);

        app->list = tk_listview_new();
        tk_listview_set_on_select(app->list, on_list_select, app);
        tk_listview_set_on_activate(app->list, on_list_activate, app);
        tk_set_flex(app->list, 1);
        tk_add_child(lib_parent, app->list);
        /* int10 (independent review B): the full sentence
         * measures 544px and was clipped at the right edge
         * of the 522px WinUI pane (C10); the WinUI form
         * drops 「ここに」 and fits. */
        app->empty_label = tk_label_new(app->winui
            ? "音楽がありません。microSD に音楽ファイルを"
              "入れると表示されます"
            : "音楽がありません。microSD に音楽ファイルを"
              "入れるとここに表示されます");
        tk_widget_set_text_style(app->empty_label,
                                 TK_TEXT_CAPTION);
        tk_set_visible(app->empty_label, false);
        tk_add_child(lib_parent, app->empty_label);

        app->ctx_bar = tk_bottombar_new();
        tk_bottombar_add_cell(app->ctx_bar, app->ic_play, 24, 24,
                              "再生", on_ctx_play, app);
        tk_bottombar_add_cell(app->ctx_bar, app->ic_next, 24, 24,
                              "次に再生", on_q_next, app);
        tk_bottombar_add_cell(app->ctx_bar, ic_add, 24, 24,
                              "後で再生", on_q_later, app);
        tk_bottombar_add_cell(app->ctx_bar, ic_addlist, 24, 24,
                              "プレイリストに追加", on_ctx_addlist,
                              app);
        tk_bottombar_add_cell(app->ctx_bar, ic_open, 24, 24,
                              "開く", on_ctx_open, app);
        tk_bottombar_add_cell(app->ctx_bar, ic_rename, 24, 24,
                              "名前変更", on_ctx_rename, app);
        tk_bottombar_add_cell(app->ctx_bar, ic_del, 24, 24,
                              "削除", on_ctx_delete, app);
        for (int i = 0; i < 7; i++)
            app->ctx_cells[i] = tk_bottombar_cell(app->ctx_bar, i);
        tk_set_visible(app->ctx_bar, false);
        tk_add_child(lib_parent, app->ctx_bar);

        app->status = tk_label_new("");
        tk_widget_set_text_style(app->status, TK_TEXT_CAPTION);
        tk_add_child(lib_parent, app->status);

    }
    if (!app->winui) {
        /* mini player: artwork, drill-in title, repeat state,
         * transport. Visible whenever the library is. (In
         * WinUI the same widgets live in the resident
         * transport bar built below.) */
        tk_widget *mini = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
        tk_box_set_align(mini, TK_ALIGN_CENTER);
        app->mini_art = tk_imageview_new();
        tk_widget_set_min_size(app->mini_art, 56, 56);
        tk_add_child(mini, app->mini_art);
        app->mini_title = btn("（停止中）", on_mini_title, app);
        tk_button_set_flat(app->mini_title, true);
        tk_button_set_text_align(app->mini_title, 0);
        tk_set_flex(app->mini_title, 1);
        tk_add_child(mini, app->mini_title);
        app->btn_repeat_mini = btn("リピート", on_repeat_btn, app);
        tk_button_set_flat(app->btn_repeat_mini, true);
        if (app->ic_repeat)
            tk_button_set_icon(app->btn_repeat_mini, app->ic_repeat,
                               24, 24);
        tk_widget_set_min_size(app->btn_repeat_mini, 56, 56);
        tk_add_child(mini, app->btn_repeat_mini);
        app->btn_prev = btn("⏮ 前へ", on_prev_btn, app);
        tk_button_set_flat(app->btn_prev, true);
        if (app->ic_prev) {
            tk_button_set_icon(app->btn_prev, app->ic_prev, 24, 24);
            tk_button_set_icon_only(app->btn_prev, true);
        }
        tk_widget_set_min_size(app->btn_prev, 56, 56);
        tk_add_child(mini, app->btn_prev);
        app->btn_play = btn("▶ 再生", on_play_btn, app);
        tk_button_set_variant(app->btn_play, TK_BTN_FILLED);
        tk_button_set_corner_radius(app->btn_play, 28);
        if (app->ic_play) {
            tk_button_set_icon(app->btn_play, app->ic_play, 24, 24);
            tk_button_set_icon_only(app->btn_play, true);
        }
        tk_widget_set_min_size(app->btn_play, 56, 56);
        tk_add_child(mini, app->btn_play);
        app->btn_next = btn("⏭ 次へ", on_next_btn, app);
        tk_button_set_flat(app->btn_next, true);
        if (app->ic_next) {
            tk_button_set_icon(app->btn_next, app->ic_next, 24, 24);
            tk_button_set_icon_only(app->btn_next, true);
        }
        tk_widget_set_min_size(app->btn_next, 56, 56);
        tk_add_child(mini, app->btn_next);
        tk_widget_set_min_size(mini, 0, 72);
        tk_add_child(app->scr_lib, mini);
    }

    /* ---------- now playing: Android screen / WinUI pane ----
     * WinUI: the pane is a padded two-column body (cover left,
     * information right, every right-column element sharing
     * the same left edge). The cover is a fixed-size,
     * non-flex child inside the pane's 16px padding, so it
     * always sits fully inside the window - no negative
     * offsets, no overflow past the pane. Seek and volume
     * live in the resident transport bar (below), exactly
     * as in Windows Media Player; the pane carries its own
     * centred transport row. Shuffle / repeat state is the
     * buttons' own toggle/face state - no floating state
     * text row (the repeat_label widget exists, updated as
     * always, but is not shown in this form). */
    tk_widget *np_body;
    if (app->winui) {
        app->win_np_pane = tk_box_new(TK_DIR_HORIZONTAL, 16, 16);
        tk_box_set_align(app->win_np_pane, TK_ALIGN_CENTER);
        tk_set_flex(app->win_np_pane, 1);
        tk_set_visible(app->win_np_pane, false);
        tk_add_child(lib_parent, app->win_np_pane);
        np_body = app->win_np_pane;
    } else {
        app->scr_np = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(app->scr_np, 1);
        tk_set_visible(app->scr_np, false);
        tk_add_child(app->top, app->scr_np);
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_add_child(hdr, header_back_btn(app, on_back_to_lib,
                                          app));
        tk_widget *t = tk_label_new("再生中");
        tk_widget_set_text_style(t, TK_TEXT_HEADING);
        tk_set_flex(t, 1);
        tk_add_child(hdr, t);
        tk_add_child(hdr, header_icon_btn("その他", ic_dots,
                                          on_np_overflow, app));
        tk_widget_set_min_size(hdr, 0, 56);
        tk_add_child(app->scr_np, hdr);
        np_body = tk_box_new(TK_DIR_HORIZONTAL, 12, 12);
        tk_box_set_align(np_body, TK_ALIGN_CENTER);
        tk_set_flex(np_body, 1);
    }
    {
        tk_widget *body = np_body;
        app->art = tk_imageview_new();
        int art_px = app->winui ? (h >= 400 ? 176 : 128)
                                : (h >= 400 ? 232 : 160);
        tk_widget_set_min_size(app->art, art_px, art_px);
        tk_add_child(body, app->art);
        tk_widget *col = tk_box_new(TK_DIR_VERTICAL, 6, 0);
        tk_set_flex(col, 1);
        app->np_title = tk_label_new("");
        tk_widget_set_text_style(app->np_title, TK_TEXT_TITLE);
        tk_add_child(col, app->np_title);
        app->np_artist = tk_label_new("");
        tk_add_child(col, app->np_artist);
        app->np_album = tk_label_new("");
        tk_widget_set_text_style(app->np_album, TK_TEXT_CAPTION);
        tk_add_child(col, app->np_album);
        if (!app->winui) {
            app->progress = tk_slider_new(0.0f);
            tk_slider_set_on_change(app->progress, on_progress,
                                    app);
            tk_add_child(col, app->progress);
            app->time_label = tk_label_new("0:00 / 0:00");
            tk_add_child(col, app->time_label);
        }
        tk_widget *trow = tk_box_new(TK_DIR_HORIZONTAL, 9, 0);
        /* CENTER, not the default STRETCH: the 72px round play
         * button must stay a circle, not an oval stretched to
         * the row's tallest cell */
        tk_box_set_align(trow, TK_ALIGN_CENTER);
        if (app->winui) {
            /* centre the transport row in the column: equal
             * flexible spacers on both sides */
            tk_widget *sp = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
            tk_set_flex(sp, 1);
            tk_add_child(trow, sp);
        }
        app->np_shuffle = btn("シャッフル", on_shuffle_btn, app);
        tk_button_set_variant(app->np_shuffle, TK_BTN_TONAL);
        tk_button_set_toggle(app->np_shuffle, true);
        if (app->ic_shuffle)
            tk_button_set_icon(app->np_shuffle, app->ic_shuffle,
                               24, 24);
        if (app->winui) {
            tk_button_set_icon_only(app->np_shuffle, true);
            tk_widget_set_min_size(app->np_shuffle, 56, 56);
        }
        tk_add_child(trow, app->np_shuffle);
        app->np_prev = btn("⏮ 前へ", on_prev_btn, app);
        if (app->ic_prev) {
            tk_button_set_icon(app->np_prev, app->ic_prev, 24, 24);
            tk_button_set_icon_only(app->np_prev, true);
        }
        tk_widget_set_min_size(app->np_prev, 56, 56);
        tk_add_child(trow, app->np_prev);
        app->np_play = btn("▶", on_play_btn, app);
        tk_button_set_variant(app->np_play, TK_BTN_FILLED);
        tk_button_set_corner_radius(app->np_play, 36);
        if (app->ic_play) {
            tk_button_set_icon(app->np_play, app->ic_play, 24, 24);
            tk_button_set_icon_only(app->np_play, true);
        }
        tk_widget_set_min_size(app->np_play, 72, 72);
        tk_widget_set_text_style(app->np_play, TK_TEXT_TITLE);
        tk_add_child(trow, app->np_play);
        app->np_next = btn("⏭ 次へ", on_next_btn, app);
        if (app->ic_next) {
            tk_button_set_icon(app->np_next, app->ic_next, 24, 24);
            tk_button_set_icon_only(app->np_next, true);
        }
        tk_widget_set_min_size(app->np_next, 56, 56);
        tk_add_child(trow, app->np_next);
        tk_widget *rcell = tk_box_new(TK_DIR_VERTICAL, 2, 0);
        app->np_repeat = btn("リピート", on_repeat_btn, app);
        tk_button_set_flat(app->np_repeat, true);
        if (app->ic_repeat)
            tk_button_set_icon(app->np_repeat, app->ic_repeat,
                               24, 24);
        if (app->winui) {
            /* state is the button's face + icon (repeat_1 for
             * 1曲): icon-only, no state text row under it */
            tk_button_set_icon_only(app->np_repeat, true);
            tk_widget_set_min_size(app->np_repeat, 56, 56);
        }
        tk_add_child(rcell, app->np_repeat);
        app->repeat_label = tk_label_new("リピート: なし");
        tk_widget_set_text_style(app->repeat_label,
                                 TK_TEXT_CAPTION);
        tk_add_child(rcell, app->repeat_label);
        if (app->winui) tk_set_visible(app->repeat_label, false);
        tk_add_child(trow, rcell);
        if (app->winui) {
            /* compact transport cluster: at the pane's width
             * the 56/72px Android sizes would push the last
             * buttons past the right edge */
            tk_widget_set_min_size(app->np_shuffle, 48, 48);
            tk_widget_set_min_size(app->np_prev, 48, 48);
            tk_widget_set_min_size(app->np_play, 64, 64);
            tk_button_set_corner_radius(app->np_play, 32);
            tk_widget_set_min_size(app->np_next, 48, 48);
            tk_widget_set_min_size(app->np_repeat, 48, 48);
            tk_widget *sp = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
            tk_set_flex(sp, 1);
            tk_add_child(trow, sp);
        }
        tk_add_child(col, trow);
        if (!app->winui) {
            tk_widget *vrow = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
            tk_widget *vl = tk_label_new("音量");
            tk_add_child(vrow, vl);
            app->np_volume = tk_slider_new(0.8f);
            tk_slider_set_on_change(app->np_volume, on_volume,
                                    app);
            tk_set_flex(app->np_volume, 1);
            tk_add_child(vrow, app->np_volume);
            app->np_vol_label = tk_label_new("80%");
            tk_add_child(vrow, app->np_vol_label);
            tk_add_child(col, vrow);
            tk_add_child(body, col);
            tk_add_child(app->scr_np, body);
        } else {
            tk_add_child(body, col);
        }
    }

    /* ---------- queue: Android screen / WinUI pane content --
     * The queue list and its ops bar are shared; in WinUI
     * they sit directly in the right pane (hidden until the
     * queue is shown), in Android on their own screen. */
    if (!app->winui) {
        app->scr_queue = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(app->scr_queue, 1);
        tk_set_visible(app->scr_queue, false);
        tk_add_child(app->top, app->scr_queue);
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_add_child(hdr, header_back_btn(app, on_back_to_lib,
                                          app));
        tk_widget *t = tk_label_new("次に再生");
        tk_widget_set_text_style(t, TK_TEXT_HEADING);
        tk_set_flex(t, 1);
        tk_add_child(hdr, t);
        tk_widget_set_min_size(hdr, 0, 56);
        tk_add_child(app->scr_queue, hdr);
    }
    {
        tk_widget *qparent = app->winui ? lib_parent
                                        : app->scr_queue;
        app->queue_list = tk_listview_new();
        tk_set_flex(app->queue_list, 1);
        tk_add_child(qparent, app->queue_list);
        app->queue_bar = tk_bottombar_new();
        tk_bottombar_add_cell(app->queue_bar, ic_up, 24, 24,
                              "上へ", on_queue_up, app);
        tk_bottombar_add_cell(app->queue_bar, ic_down, 24, 24,
                              "下へ", on_queue_down, app);
        tk_bottombar_add_cell(app->queue_bar, ic_del, 24, 24,
                              "削除", on_queue_del, app);
        tk_bottombar_add_cell(app->queue_bar, NULL, 24, 24,
                              "クリア", on_queue_clear, app);
        tk_add_child(qparent, app->queue_bar);
        if (app->winui) {
            tk_set_visible(app->queue_list, false);
            tk_set_visible(app->queue_bar, false);
        }
    }

    /* ---------- WinUI resident transport bar ----------
     * Always at the window's bottom in WinUI mode: scrub
     * row (slider + elapsed/total), then the control row
     * (artwork, title, shuffle / prev / play / next /
     * repeat, volume). The scrub slider is app->progress
     * and the volume slider app->np_volume, so tick / seek
     * / volume sync stay the single shared paths. */
    if (app->winui) {
        app->win_transport = tk_box_new(TK_DIR_VERTICAL, 2, 8);
        tk_add_child(win_main, app->win_transport);
        /* seek row: scrub slider + time, then the volume
         * group (icon + slider + level) - one aligned row,
         * so the control row below keeps room for the title
         * and the centred transport cluster */
        tk_widget *srow = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
        tk_box_set_align(srow, TK_ALIGN_CENTER);
        app->progress = tk_slider_new(0.0f);
        tk_slider_set_on_change(app->progress, on_progress,
                                app);
        tk_set_flex(app->progress, 1);
        tk_add_child(srow, app->progress);
        app->time_label = tk_label_new("0:00 / 0:00");
        tk_add_child(srow, app->time_label);
        tk_widget *vicon = tk_imageview_new();
        if (app->ic_vol)
            tk_imageview_set_pixels(vicon, app->ic_vol, 24, 24);
        tk_widget_set_min_size(vicon, 24, 24);
        tk_add_child(srow, vicon);
        app->np_volume = tk_slider_new(0.8f);
        tk_slider_set_on_change(app->np_volume, on_volume,
                                app);
        tk_widget_set_min_size(app->np_volume, 110, 0);
        tk_add_child(srow, app->np_volume);
        app->np_vol_label = tk_label_new("80%");
        tk_add_child(srow, app->np_vol_label);
        tk_add_child(app->win_transport, srow);

        tk_widget *trow = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
        tk_box_set_align(trow, TK_ALIGN_CENTER);
        app->mini_art = tk_imageview_new();
        tk_widget_set_min_size(app->mini_art, 40, 40);
        tk_add_child(trow, app->mini_art);
        app->mini_title = btn("（停止中）", on_mini_title, app);
        tk_button_set_flat(app->mini_title, true);
        tk_button_set_text_align(app->mini_title, 0);
        tk_set_flex(app->mini_title, 1);
        tk_add_child(trow, app->mini_title);
        app->win_shuffle = btn("シャッフル", on_shuffle_btn,
                               app);
        tk_button_set_variant(app->win_shuffle, TK_BTN_TONAL);
        tk_button_set_toggle(app->win_shuffle, true);
        if (app->ic_shuffle) {
            tk_button_set_icon(app->win_shuffle,
                               app->ic_shuffle, 24, 24);
            tk_button_set_icon_only(app->win_shuffle, true);
        }
        tk_widget_set_min_size(app->win_shuffle, 48, 48);
        tk_add_child(trow, app->win_shuffle);
        app->btn_prev = btn("⏮ 前へ", on_prev_btn, app);
        tk_button_set_flat(app->btn_prev, true);
        if (app->ic_prev) {
            tk_button_set_icon(app->btn_prev, app->ic_prev,
                               24, 24);
            tk_button_set_icon_only(app->btn_prev, true);
        }
        tk_widget_set_min_size(app->btn_prev, 48, 48);
        tk_add_child(trow, app->btn_prev);
        app->btn_play = btn("▶ 再生", on_play_btn, app);
        tk_button_set_variant(app->btn_play, TK_BTN_FILLED);
        tk_button_set_corner_radius(app->btn_play, 28);
        if (app->ic_play) {
            tk_button_set_icon(app->btn_play, app->ic_play,
                               24, 24);
            tk_button_set_icon_only(app->btn_play, true);
        }
        tk_widget_set_min_size(app->btn_play, 56, 56);
        tk_add_child(trow, app->btn_play);
        app->btn_next = btn("⏭ 次へ", on_next_btn, app);
        tk_button_set_flat(app->btn_next, true);
        if (app->ic_next) {
            tk_button_set_icon(app->btn_next, app->ic_next,
                               24, 24);
            tk_button_set_icon_only(app->btn_next, true);
        }
        tk_widget_set_min_size(app->btn_next, 48, 48);
        tk_add_child(trow, app->btn_next);
        app->btn_repeat_mini = btn("リピート", on_repeat_btn,
                                   app);
        tk_button_set_flat(app->btn_repeat_mini, true);
        if (app->ic_repeat) {
            tk_button_set_icon(app->btn_repeat_mini,
                               app->ic_repeat, 24, 24);
            tk_button_set_icon_only(app->btn_repeat_mini, true);
        }
        tk_widget_set_min_size(app->btn_repeat_mini, 48, 48);
        tk_add_child(trow, app->btn_repeat_mini);
        tk_add_child(app->win_transport, trow);
    }

    /* ---------- playlist dialogs ---------- */
    app->list_dlg = tk_dialog_new("プレイリスト");
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 8, 0);
        tk_widget *cap = tk_label_new(
            "名前を入力して作成、または選択中の曲を追加します");
        tk_widget_set_text_style(cap, TK_TEXT_CAPTION);
        tk_add_child(box, cap);
        app->list_name = tk_textfield_new("");
        tk_textfield_set_placeholder(app->list_name, "プレイリスト名");
        tk_add_child(box, app->list_name);
        tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 9, 0);
        tk_widget *cancel = btn("キャンセル", on_list_cancel, app);
        tk_button_set_variant(cancel, TK_BTN_TEXT);
        tk_add_child(row, cancel);
        app->btn_list_create = btn("作成", on_list_create, app);
        tk_button_set_variant(app->btn_list_create, TK_BTN_TONAL);
        tk_add_child(row, app->btn_list_create);
        app->btn_list_add = btn("追加", on_list_add, app);
        tk_button_set_variant(app->btn_list_add, TK_BTN_FILLED);
        tk_add_child(row, app->btn_list_add);
        tk_add_child(box, row);
        tk_add_child(app->list_dlg, box);
    }
    app->rename_dlg = tk_dialog_new("名前を変更");
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 8, 0);
        app->rename_field = tk_textfield_new("");
        tk_textfield_set_placeholder(app->rename_field, "新しい名前");
        tk_add_child(box, app->rename_field);
        tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 9, 0);
        tk_widget *cancel = btn("キャンセル", on_rename_cancel, app);
        tk_button_set_variant(cancel, TK_BTN_TEXT);
        tk_add_child(row, cancel);
        tk_widget *apply = btn("変更", on_rename_apply, app);
        tk_button_set_variant(apply, TK_BTN_FILLED);
        tk_add_child(row, apply);
        tk_add_child(box, row);
        tk_add_child(app->rename_dlg, box);
    }

    app->root = tk_root_new(app->top);
    tk_widget_set_event_hook(app->top, player_back_key, app);
    rebuild_rows(app);
    refresh_queue(app);

    /* restore playback state (paused at the saved position) */
    pl_state st;
    if (pl_state_load(app->root_dir, &st) == 0 && st.path[0]) {
        app->volume_v = st.volume;
        pl_order_set_shuffle(app->order, st.shuffle, SHUFFLE_SEED);
        pl_order_set_repeat(app->order, st.repeat);
        if (open_track(app, st.path, false)) {
            if (app->engine && st.position_ms)
                pe_seek_ms(app->engine, st.position_ms);
        }
        tk_button_set_toggled(app->np_shuffle, st.shuffle);
    }
    sync_repeat_ui(app);
    sync_shuffle_ui(app);
    sync_volume_ui(app);
    sync_play_ui(app);
    if (app->winui) sync_win_chrome(app);
    tk_root_layout(app->root, w, h);
    return app;
}

void player_app_free(player_app *app)
{
    if (!app) return;
    save_state(app);
    close_engine(app);
    tk_root_free(app->root);
    rows_clear(app);
    free(app->row_labels);
    free(app->row_tracks);
    /* the ic_* pixmaps are borrowed from the bdicon cache:
     * not freed here */
    pl_order_free(app->order);
    pl_lib_close(app->lib);
    free(app);
}

#ifdef BD_PLAYER_MAIN
static pe_out *mk_alsa(void *ud, uint32_t rate, uint16_t ch)
{
    (void)ud;
    return pe_out_alsa(NULL, rate, ch);
}

static void player_tick_cb(void *ud, uint64_t mono)
{
    player_app_tick((player_app *)ud, mono);
}

int main(int argc, char **argv)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    const char *home = getenv("HOME");
    char data[1024], music[1024];
    snprintf(data, sizeof(data), "%s/.local/share/bd-player",
             home ? home : ".");
    snprintf(music, sizeof(music), "%s/Music", home ? home : ".");
    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    const char *dirs[9];
    size_t nd = 0;
    dirs[nd++] = music;
    const char *files[64];
    size_t nf = 0;
    for (int i = 1; i < argc && nf < 64; i++) files[nf++] = argv[i];
    /* Display first, real size (see bd_calc.c main note).
     * The minimum is per form (proportion pass): the WinUI
     * split keeps a fixed 304px art pane beside the content
     * column, and below ~700px wide the content column can
     * no longer hold the seek/volume row (the seek slider
     * measured 13px at 480 wide) - so the WinUI form
     * declares 700x360. The Android form declares 480x336:
     * its resident chrome (header + tab bar + seek row +
     * status + transport = ~253px) left the track list
     * 52px high in the library at 272 and 0px once a track
     * was selected and the seek row appeared (matrix floor
     * check) - 336 is the smallest height that keeps a full
     * row of the list visible in both states. */
    int vw, vh;
    bool winui = tk_theme_default() &&
                 tk_theme_default()->style == 2;
    bd_app_display *disp = bd_app_display_open_ex(
        "ミュージック", 0, 0,
        winui ? 700 : 480, winui ? 360 : 336, 480, 272, &vw, &vh);
    player_app *app = player_app_new(data, dirs, nd, mk_alsa, NULL,
                                     vw, vh);
    if (!app) { fprintf(stderr, "bd-player: init failed\n"); return 1; }
    if (nf) player_app_open_files(app, files, nf);

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, player_tick_cb, app, 20);
    } else {
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-player: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    player_app_free(app);
    if (font) tk_font_free(font);
    return 0;
}
#endif
