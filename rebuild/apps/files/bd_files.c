#define _POSIX_C_SOURCE 200809L
#include "bd_files.h"
#include "bdstore.h"
#include "bdmime.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"
#include "desktop.h"
#include "reg.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* Layout (DESIGN-apps-envfit.md SS7.1): ONE main screen - the
 * folder view. Header = back icon + folder name / path caption
 * + search / places / overflow icons (nothing else is exposed
 * at rest). Places are a full-screen menu from the header, the
 * long tail of operations lives in the overflow menu, and the
 * selection's actions appear in a bottom context bar. The raw
 * path entry and the search field are toggle rows, not chrome.
 * All filesystem behaviour lives in fmodel; this file is
 * presentation + wiring, and every control calls one of the
 * files_app_* action functions below (which is also what the
 * UI tests drive). */

typedef struct { files_app *app; int idx; } crumb_cb;

struct files_app {
    tk_root *root;
    tk_widget *top;
    /* header (top's first child; its first child is Back -
     * the UI test contract, DESIGN-apps-envfit SS5.1) */
    tk_widget *header_title;
    tk_widget *header_path;
    tk_widget *search_row;       /* toggled from the header */
    tk_widget *search_field;
    tk_widget *path_row;         /* toggled from the menu */
    tk_widget *path_field;
    tk_widget *list;
    tk_widget *empty_label;      /* empty-folder guidance
                                  * (eval F-14) */
    tk_widget *content_box;
    tk_widget *grid;             /* current grid widget or NULL */
    tk_widget *context_bar;      /* tk_bottombar, selection actions */
    tk_widget *cell_open, *cell_copy, *cell_rename, *cell_delete;
    tk_widget *cell_restore, *cell_perm, *cell_paste, *cell_more;
    tk_widget *status;
    tk_widget *space_label;      /* status-row free-space caption */
    tk_widget *space_bar;        /* status-row free-space bar */
    /* menus are rebuilt fresh on every open so check items and
     * enabled states always reflect current state (the toolkit
     * has no check-state setter); the previous instance is
     * freed when the next one is built */
    tk_widget *overflow_menu;
    tk_widget *places_menu;
    /* row icon pixmaps (24x24 RGBA8888; Papirus colour icons
     * borrowed from the bdicon cache - NOT owned, never freed) */
    const uint32_t *icon_dir, *icon_file, *icon_image, *icon_audio,
             *icon_video, *icon_text;
    /* WinUI form (Explorer structure): nav pane + command bar +
     * breadcrumb segments. All NULL in the Android form. */
    bool winui;
    tk_widget *nav_list;
    tk_widget *crumb_box;
    char *crumb_paths[40];
    int n_crumb_paths;
    crumb_cb crumb_cbs[40];
    /* prompt dialog (new folder/file, rename) */
    tk_widget *prompt_dlg;
    tk_widget *prompt_field;
    tk_widget *prompt_title;
    int prompt_op;               /* 0 none, 1 folder, 2 file, 3 rename */
    /* properties dialog */
    tk_widget *props_dlg;
    tk_widget *props_labels[10];

    int win_w, win_h;
    char *data_root;
    fm_trash *trash;
    fm_thumbcache *thumbs;
    de_catalog *cat;
    fm_history hist;
    char cur[FM_PATH_MAX];
    fm_listing listing;
    bool listing_is_search;
    fm_trash_list trash_items;
    bool trash_view;
    bool show_hidden;
    bool grid_view;
    fm_sort_key sort_key;
    bool sort_desc;
    fm_group group;
    /* display rows: entry index into listing/trash_items, -1 header */
    int *row_map;
    size_t n_rows;
    int sel_entry;               /* index into listing (or trash list) */
    /* clipboard */
    char **clip;
    size_t n_clip;
    bool clip_cut;
    files_launch_hook hook;
    void *hook_ud;
};

/* ---------------------------------------------------------------- */
/* helpers                                                           */
/* ---------------------------------------------------------------- */

static tk_widget *btn(const char *label, tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    return b;
}

static void set_status(files_app *app, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    tk_label_set_text(app->status, buf);
    /* toolkit setters do NOT auto-invalidate (SDK-verified
     * pitfall): without this, damage-driven presentation would
     * ship 0 bytes and the change would never reach the screen */
    tk_widget_invalidate(app->status);
}

/* forward declarations for callbacks */
static void refresh(files_app *app);
static void navigate_to(files_app *app, const char *path, bool push);
static void sync_context_bar(files_app *app);

/* ---------------------------------------------------------------- */
/* icons                                                             */
/* ---------------------------------------------------------------- */

/* Icons come from the established vector sets via bdicon
 * (assets/icons/MANIFEST.md): Papirus full colour for file
 * types and places, Material Symbols (Android form) / Fluent
 * (WinUI form) for actions, tinted with the theme ink. No
 * font glyphs and no code-drawn shapes are used for icons
 * (user direction 2026-11-21). All pixmaps are borrowed from
 * the bdicon cache; NULL means "asset unavailable" and
 * callers fall back to label-only controls, as before. */

static uint32_t bd_tint_of(const tk_theme *t)
{
    return ((uint32_t)t->text.r << 16) |
           ((uint32_t)t->text.g << 8) | (uint32_t)t->text.b;
}

/* mode-paired action icon at 24px in the theme ink */
static const uint32_t *action_icon(files_app *app, const char *mat,
                                   const char *flu)
{
    return bd_icon_mode(app->winui, mat, flu, 24,
                        bd_tint_of(tk_theme_default()));
}

/* Header icon button: flat, icon-only, 48x48 target with a
 * 24px icon (DESIGN-apps-envfit SS3.2). The label remains as
 * the accessible name; when the icon is NULL (no font at
 * construction time) the button falls back to showing its
 * label text so the control never renders blank. */
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

/* ---------------------------------------------------------------- */
/* display construction                                              */
/* ---------------------------------------------------------------- */

static void build_rows(files_app *app)
{
    free(app->row_map);
    app->row_map = NULL;
    app->n_rows = 0;
    if (app->trash_view) {
        app->n_rows = app->trash_items.n;
        app->row_map = malloc((app->n_rows ? app->n_rows : 1) * sizeof(int));
        for (size_t i = 0; i < app->n_rows; i++) app->row_map[i] = (int)i;
        return;
    }
    fm_listing *l = &app->listing;
    if (app->group == FM_GROUP_NONE || app->grid_view) {
        app->n_rows = l->n;
        app->row_map = malloc((app->n_rows ? app->n_rows : 1) * sizeof(int));
        for (size_t i = 0; i < app->n_rows; i++) app->row_map[i] = (int)i;
        return;
    }
    /* grouped: entries are pre-sorted so groups are contiguous;
     * insert a header row whenever the group label changes */
    app->row_map = malloc((l->n * 2 + 1) * sizeof(int));
    char prev[64] = "", label[64];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
    int64_t today = (int64_t)mktime(&tmv);
    int64_t yesterday = today - 86400;
    for (size_t i = 0; i < l->n; i++) {
        fm_entry_group(&l->v[i], app->group, today, yesterday,
                       label, sizeof(label));
        if (strcmp(label, prev) != 0) {
            app->row_map[app->n_rows++] = -1;
            snprintf(prev, sizeof(prev), "%s", label);
        }
        app->row_map[app->n_rows++] = (int)i;
    }
}

static void group_label_for(files_app *app, size_t entry_idx,
                            char *label, size_t cap)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
    int64_t today = (int64_t)mktime(&tmv);
    fm_entry_group(&app->listing.v[entry_idx], app->group, today,
                   today - 86400, label, cap);
}

/* The six 24x24 row icons: Papirus full-colour file-type
 * icons, borrowed from the bdicon cache (not owned). */
static void draw_row_icons(files_app *app)
{
    app->icon_dir = bd_icon_color("folder", 24);
    app->icon_file = bd_icon_color("application-x-generic", 24);
    app->icon_image = bd_icon_color("image-x-generic", 24);
    app->icon_audio = bd_icon_color("audio-x-generic", 24);
    app->icon_video = bd_icon_color("video-x-generic", 24);
    app->icon_text = bd_icon_color("text-x-generic", 24);
}

/* Row icon for an entry, chosen from the extension class
 * (UI-REDESIGN 2.4: icon + name is the identification unit). The
 * pixmaps are drawn once at startup; the list borrows them. */
static const uint32_t *icon_for(files_app *app, const char *path,
                                bool is_dir)
{
    if (is_dir) return app->icon_dir;
    char ext[16];
    bd_mime_extension(path, ext, sizeof(ext));
    if (!strcmp(ext, "png") || !strcmp(ext, "jpg") ||
        !strcmp(ext, "jpeg") || !strcmp(ext, "gif") ||
        !strcmp(ext, "bmp") || !strcmp(ext, "webp"))
        return app->icon_image;
    if (!strcmp(ext, "mp3") || !strcmp(ext, "wav") ||
        !strcmp(ext, "flac") || !strcmp(ext, "ogg") ||
        !strcmp(ext, "m4a"))
        return app->icon_audio;
    if (!strcmp(ext, "mp4") || !strcmp(ext, "mkv") ||
        !strcmp(ext, "avi") || !strcmp(ext, "mov") ||
        !strcmp(ext, "webm"))
        return app->icon_video;
    if (!strcmp(ext, "txt") || !strcmp(ext, "md") ||
        !strcmp(ext, "log"))
        return app->icon_text;
    return app->icon_file;
}

static void refresh_list_widget(files_app *app)
{
    build_rows(app);
    if (app->empty_label)
        tk_set_visible(app->empty_label, app->n_rows == 0);
    const char **items = malloc((app->n_rows ? app->n_rows : 1) *
                                sizeof(char *));
    char **owned = malloc((app->n_rows ? app->n_rows : 1) * sizeof(char *));
    /* two-line rows: name on line 1, size/kind as the caption
     * sub-line, plus a type icon per row */
    const char **subs = calloc(app->n_rows ? app->n_rows : 1,
                               sizeof(char *));
    char **owned_sub = calloc(app->n_rows ? app->n_rows : 1,
                              sizeof(char *));
    const uint32_t **icons = calloc(app->n_rows ? app->n_rows : 1,
                                    sizeof(uint32_t *));
    for (size_t i = 0; i < app->n_rows; i++) {
        int ei = app->row_map[i];
        if (ei < 0) {
            /* find the group label from the next entry */
            char label[64] = "";
            if (i + 1 < app->n_rows && app->row_map[i + 1] >= 0)
                group_label_for(app, (size_t)app->row_map[i + 1],
                                label, sizeof(label));
            char tmp[80];
            snprintf(tmp, sizeof(tmp), "— %s —", label);
            owned[i] = strdup(tmp);
            items[i] = owned[i];
            continue;
        }
        owned[i] = NULL;
        if (app->trash_view) {
            const fm_trash_entry *e = &app->trash_items.v[ei];
            char sz[32];
            bd_format_size(e->size, sz, sizeof(sz));
            items[i] = owned[i] = strdup(e->name);
            subs[i] = owned_sub[i] = strdup(sz);
            icons[i] = icon_for(app, e->name, false);
        } else {
            const fm_entry *e = &app->listing.v[ei];
            char sz[32];
            bd_format_size(e->size, sz, sizeof(sz));
            char buf[FM_PATH_MAX + 128];
            if (e->is_dir)
                snprintf(buf, sizeof(buf), "%s/", e->name);
            else
                snprintf(buf, sizeof(buf), "%s", e->name);
            items[i] = owned[i] = strdup(buf);
            if (e->is_dir)
                subs[i] = owned_sub[i] = strdup(e->kind);
            else {
                char sb[96];
                snprintf(sb, sizeof(sb), "%s ・ %s", sz, e->kind);
                subs[i] = owned_sub[i] = strdup(sb);
            }
            icons[i] = icon_for(app, e->name, e->is_dir);
        }
    }
    tk_listview_set_items(app->list, items, app->n_rows);
    tk_listview_set_subitems(app->list, subs, app->n_rows);
    tk_listview_set_item_icons(app->list, icons, 24, 24, app->n_rows);
    for (size_t i = 0; i < app->n_rows; i++) {
        free(owned[i]);
        free(owned_sub[i]);
    }
    free(owned);
    free(owned_sub);
    free(items);
    free(subs);
    free(icons);
}

static void on_grid_click(tk_widget *w, void *ud);

static void refresh_grid_widget(files_app *app)
{
    if (app->grid) {
        tk_remove_child(app->content_box, app->grid);
        tk_widget_free(app->grid);
        app->grid = NULL;
    }
    if (!app->grid_view) return;
    build_rows(app);
    int cols = 4;
    app->grid = tk_grid_new(cols, 4, 4, 4);
    size_t count = app->trash_view ? app->trash_items.n : app->listing.n;
    for (size_t i = 0; i < count; i++) {
        const char *name = app->trash_view ? app->trash_items.v[i].name
                                           : app->listing.v[i].name;
        bool is_dir = !app->trash_view && app->listing.v[i].is_dir;
        tk_widget *b = tk_button_new(name);
        /* icon-over-label cell form, like the references' grid */
        tk_button_set_icon(b, icon_for(app, name, is_dir), 24, 24);
        tk_button_set_on_click(b, on_grid_click, app);
        tk_add_child(app->grid, b);
    }
    tk_set_flex(app->grid, 1); /* fill the content area like the list */
    tk_add_child(app->content_box, app->grid);
}

/* Header title/caption (DESIGN-apps-envfit SS7.1): the folder
 * name is the title, the full path a caption - the trail is
 * text now; jumping to ancestors lives in the places menu and
 * the overflow menu's 上へ, not in a segment-button strip. */
static void rebuild_crumbs(files_app *app);

static void update_header(files_app *app)
{
    if (app->winui) {
        /* the WinUI form has no title header: the location
         * shows as breadcrumb segments in the command bar */
        rebuild_crumbs(app);
        if (app->nav_list)
            tk_listview_select(app->nav_list,
                               app->trash_view ? 6 : -1);
        return;
    }
    if (app->trash_view) {
        tk_label_set_text(app->header_title, "ゴミ箱");
        tk_label_set_text(app->header_path, "");
    } else if (app->listing_is_search) {
        tk_label_set_text(app->header_title, "検索結果");
        tk_label_set_text(app->header_path, app->cur);
    } else {
        const char *base = strrchr(app->cur, '/');
        base = base ? base + 1 : app->cur;
        if (!*base) base = "ルート";
        tk_label_set_text(app->header_title, base);
        tk_label_set_text(app->header_path, app->cur);
    }
    tk_widget_invalidate(app->header_title);
    tk_widget_invalidate(app->header_path);
}

/* Status-row free-space readout for the current directory's
 * filesystem: caption + thin bar, refreshed on navigation.
 * Capacity-based, no clock involved. */
static void update_space(files_app *app)
{
    struct statvfs sv;
    if (statvfs(app->cur, &sv) != 0 || sv.f_blocks == 0) {
        tk_label_set_text(app->space_label, "");
        return;
    }
    uint64_t total = (uint64_t)sv.f_blocks * sv.f_frsize;
    uint64_t freeb = (uint64_t)sv.f_bavail * sv.f_frsize;
    char fs[32], ts[32];
    bd_format_size(freeb, fs, sizeof(fs));
    bd_format_size(total, ts, sizeof(ts));
    char buf[96];
    snprintf(buf, sizeof(buf), "空き %s / %s", fs, ts);
    tk_label_set_text(app->space_label, buf);
    tk_widget_invalidate(app->space_label);
    double frac = total ? 1.0 - (double)freeb / (double)total : 0.0;
    tk_progress_set_value(app->space_bar, frac);
    tk_widget_invalidate(app->space_bar);
}

/* The bottom context bar (DESIGN-apps-envfit SS7.1): the
 * selection's actions, or Paste when a clipboard exists with
 * no selection. Cells are fixed at construction; visibility
 * carries the state. A layout pass runs here so the bar's
 * cells have valid rects the moment the bar appears (the
 * damage-driven host loop does not re-layout on its own). */
static void sync_context_bar(files_app *app)
{
    bool sel = app->sel_entry >= 0;
    tk_set_visible(app->cell_open, sel && !app->trash_view);
    tk_set_visible(app->cell_copy, sel && !app->trash_view);
    tk_set_visible(app->cell_rename, sel && !app->trash_view);
    tk_set_visible(app->cell_delete, sel && !app->trash_view);
    tk_set_visible(app->cell_restore, sel && app->trash_view);
    tk_set_visible(app->cell_perm, sel && app->trash_view);
    tk_set_visible(app->cell_paste,
                   !sel && !app->trash_view && app->n_clip > 0);
    tk_set_visible(app->context_bar,
                   sel || (!app->trash_view && app->n_clip > 0));
    tk_widget_invalidate(app->top);
    if (app->root) tk_root_layout(app->root, app->win_w, app->win_h);
}

static void refresh(files_app *app)
{
    if (app->trash_view) {
        fm_trash_list_free(&app->trash_items);
        fm_trash_list_all(app->trash, &app->trash_items);
        char sz[32], bz[32];
        bd_format_size(fm_trash_total_bytes(app->trash), sz, sizeof(sz));
        bd_format_size(fm_trash_budget(app->trash), bz, sizeof(bz));
        set_status(app, "ゴミ箱: %zu 項目, %s（予算 %s）",
                   app->trash_items.n, sz, bz);
    } else if (!app->listing_is_search) {
        fm_listing_free(&app->listing);
        if (fm_listing_load(&app->listing, app->cur, app->show_hidden) != 0) {
            set_status(app, "開けません: %s", app->cur);
        }
        /* grouping needs its own key order */
        if (app->group == FM_GROUP_TYPE)
            fm_listing_sort(&app->listing, FM_SORT_TYPE, false, true);
        else if (app->group == FM_GROUP_DATE)
            fm_listing_sort(&app->listing, FM_SORT_MTIME, true, true);
        else
            fm_listing_sort(&app->listing, app->sort_key, app->sort_desc,
                            true);
        tk_textfield_set_text(app->path_field, app->cur);
        set_status(app, "%zu 項目", app->listing.n);
    } else {
        set_status(app, "検索結果: %zu 項目", app->listing.n);
    }
    update_header(app);
    update_space(app);
    app->sel_entry = -1;
    if (app->grid_view) {
        tk_set_visible(app->list, false);
        refresh_grid_widget(app);
    } else {
        tk_set_visible(app->list, true);
        if (app->grid) {
            tk_remove_child(app->content_box, app->grid);
            tk_widget_free(app->grid);
            app->grid = NULL;
        }
        refresh_list_widget(app);
    }
    sync_context_bar(app);
    /* a refresh replaces list items and header text via
     * setters, none of which auto-invalidate: mark the whole UI
     * damaged so the next paint_damage actually presents it */
    tk_root_invalidate(app->root, app->top);
    /* rebuilt widgets (grid cells) keep 0x0 rects until a
     * layout pass: lay out here so the app is geometrically
     * consistent after every refresh. */
    if (app->root) tk_root_layout(app->root, app->win_w, app->win_h);
}

/* ---------------------------------------------------------------- */
/* navigation / actions                                              */
/* ---------------------------------------------------------------- */

static void navigate_to(files_app *app, const char *path, bool push)
{
    app->trash_view = false;
    app->listing_is_search = false;
    snprintf(app->cur, sizeof(app->cur), "%s", path);
    if (push) fm_history_push(&app->hist, path);
    refresh(app);
}

void files_app_navigate(files_app *app, const char *path)
{
    navigate_to(app, path, true);
}

void files_app_back(files_app *app)
{
    const char *p = fm_history_back(&app->hist);
    if (p) navigate_to(app, p, false);
}

void files_app_forward(files_app *app)
{
    const char *p = fm_history_forward(&app->hist);
    if (p) navigate_to(app, p, false);
}

void files_app_up(files_app *app)
{
    if (app->trash_view) return;
    char parent[FM_PATH_MAX];
    if (bd_dirname(app->cur, parent, sizeof(parent)) == 0 &&
        strcmp(parent, app->cur) != 0)
        navigate_to(app, parent, true);
}

void files_app_refresh(files_app *app) { refresh(app); }
bool files_app_in_trash(files_app *app) { return app->trash_view; }
const char *files_app_dir(files_app *app)
{
    return app->trash_view ? NULL : app->cur;
}
size_t files_app_row_count(files_app *app) { return app->n_rows; }
int files_app_selected(files_app *app) { return app->sel_entry; }
uint64_t files_app_trash_bytes(files_app *app)
{
    return fm_trash_total_bytes(app->trash);
}

void files_app_select_row(files_app *app, size_t row)
{
    if (row >= app->n_rows) return;
    tk_listview_select(app->list, (int)row);
    int ei = app->row_map[row];
    app->sel_entry = ei;
    if (ei >= 0 && !app->trash_view) {
        const fm_entry *e = &app->listing.v[ei];
        char sz[32];
        bd_format_size(e->size, sz, sizeof(sz));
        set_status(app, "%s - %s - %s", e->name, e->kind, sz);
    }
    sync_context_bar(app);
}

static bool default_launch(void *ud, char *const argv[])
{
    (void)ud;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    return true;
}

void files_app_open_selected(files_app *app)
{
    if (app->sel_entry < 0) return;
    if (app->trash_view) return; /* restore is the trash "open" */
    const fm_entry *e = &app->listing.v[app->sel_entry];
    if (e->is_dir) {
        navigate_to(app, e->path, true);
        return;
    }
    if (!app->cat) {
        set_status(app, "アプリカタログがありません");
        return;
    }
    char **argv = bd_mime_argv(app->cat, e->path);
    if (!argv) {
        set_status(app, "開けるアプリがありません: %s", e->name);
        return;
    }
    bool ok = app->hook ? app->hook(app->hook_ud, argv)
                        : default_launch(NULL, argv);
    set_status(app, ok ? "起動しました: %s" : "起動に失敗: %s", e->name);
    bd_mime_argv_free(argv);
}

void files_app_open_trash(files_app *app)
{
    app->trash_view = true;
    app->listing_is_search = false;
    refresh(app);
}

int files_app_new_folder(files_app *app, const char *name)
{
    if (app->trash_view) return -1;
    char out[FM_PATH_MAX];
    int rc = fm_mkdir(app->cur, name, out, sizeof(out));
    if (rc == 0) refresh(app);
    return rc;
}

int files_app_new_file(files_app *app, const char *name)
{
    if (app->trash_view) return -1;
    char out[FM_PATH_MAX];
    int rc = fm_create_file(app->cur, name, out, sizeof(out));
    if (rc == 0) refresh(app);
    return rc;
}

int files_app_rename_selected(files_app *app, const char *new_name)
{
    if (app->trash_view || app->sel_entry < 0) return -1;
    char out[FM_PATH_MAX];
    int rc = fm_rename(app->listing.v[app->sel_entry].path, new_name,
                       out, sizeof(out));
    if (rc == 0) refresh(app);
    return rc;
}

int files_app_delete_selected(files_app *app)
{
    if (app->sel_entry < 0) return -1;
    int rc;
    if (app->trash_view) {
        rc = fm_trash_delete(app->trash,
                             app->trash_items.v[app->sel_entry].id);
    } else {
        int evicted = 0;
        rc = fm_trash_add(app->trash,
                          app->listing.v[app->sel_entry].path, &evicted);
        if (rc == 0 && evicted > 0)
            set_status(app, "ゴミ箱の容量上限で %d 件を完全削除しました",
                       evicted);
    }
    if (rc == 0) refresh(app);
    return rc;
}

int files_app_restore_selected(files_app *app)
{
    if (!app->trash_view || app->sel_entry < 0) return -1;
    char fallback[FM_PATH_MAX];
    snprintf(fallback, sizeof(fallback), "%s/restored", app->data_root);
    char out[FM_PATH_MAX];
    int rc = fm_trash_restore(app->trash,
                              app->trash_items.v[app->sel_entry].id,
                              fallback, out, sizeof(out));
    if (rc == 0) refresh(app);
    return rc;
}

int files_app_delete_permanent_selected(files_app *app)
{
    if (app->sel_entry < 0) return -1;
    int rc;
    if (app->trash_view)
        rc = fm_trash_delete(app->trash,
                             app->trash_items.v[app->sel_entry].id);
    else
        rc = fm_delete_permanent(app->listing.v[app->sel_entry].path);
    if (rc == 0) refresh(app);
    return rc;
}

int files_app_empty_trash(files_app *app)
{
    int rc = fm_trash_empty(app->trash);
    if (rc == 0 && app->trash_view) refresh(app);
    return rc;
}

void files_app_copy_selected(files_app *app, bool cut)
{
    for (size_t i = 0; i < app->n_clip; i++) free(app->clip[i]);
    free(app->clip);
    app->clip = NULL;
    app->n_clip = 0;
    app->clip_cut = cut;
    if (app->trash_view || app->sel_entry < 0) return;
    app->clip = malloc(sizeof(char *));
    app->clip[0] = strdup(app->listing.v[app->sel_entry].path);
    app->n_clip = 1;
    set_status(app, "%s: %s", cut ? "切り取り" : "コピー",
               app->listing.v[app->sel_entry].name);
    sync_context_bar(app);
}

int files_app_paste(files_app *app)
{
    if (app->trash_view || app->n_clip == 0) return -1;
    int rc = 0;
    for (size_t i = 0; i < app->n_clip; i++) {
        char out[FM_PATH_MAX];
        int r = app->clip_cut
                    ? fm_move(app->clip[i], app->cur, false, out,
                              sizeof(out))
                    : fm_copy(app->clip[i], app->cur, false, out,
                              sizeof(out), NULL);
        if (r != 0) rc = -1;
    }
    if (app->clip_cut) {
        for (size_t i = 0; i < app->n_clip; i++) free(app->clip[i]);
        free(app->clip);
        app->clip = NULL;
        app->n_clip = 0;
    }
    refresh(app);
    return rc;
}

void files_app_set_show_hidden(files_app *app, bool v)
{
    app->show_hidden = v;
    reg_set("files", "show_hidden", v ? "1" : "0");
    if (!app->trash_view) {
        app->listing_is_search = false;
        refresh(app);
    }
}

void files_app_set_grid(files_app *app, bool grid)
{
    app->grid_view = grid;
    reg_set("files", "view", grid ? "grid" : "list");
    refresh(app);
}

void files_app_set_sort(files_app *app, fm_sort_key key, bool desc)
{
    app->sort_key = key;
    app->sort_desc = desc;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)key);
    reg_set("files", "sort_key", buf);
    reg_set("files", "sort_desc", desc ? "1" : "0");
    if (!app->trash_view && !app->listing_is_search) refresh(app);
}

void files_app_set_group(files_app *app, fm_group g)
{
    app->group = g;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)g);
    reg_set("files", "group", buf);
    if (!app->trash_view && !app->listing_is_search) refresh(app);
}

int files_app_search(files_app *app, const char *query)
{
    if (app->trash_view) return -1;
    fm_listing_free(&app->listing);
    bool trunc = false;
    int rc = fm_search(app->cur, query, app->show_hidden, &app->listing,
                       512, &trunc);
    if (rc != 0) return -1;
    app->listing_is_search = true;
    fm_listing_sort(&app->listing, app->sort_key, app->sort_desc, true);
    refresh(app);
    if (trunc) set_status(app, "検索結果: %zu 件（上限で打ち切り）",
                          app->listing.n);
    return 0;
}

void files_app_set_launch_hook(files_app *app, files_launch_hook fn,
                               void *ud)
{
    app->hook = fn;
    app->hook_ud = ud;
}

/* ---------------------------------------------------------------- */
/* widget callbacks                                                  */
/* ---------------------------------------------------------------- */

static void on_back(tk_widget *w, void *ud) { (void)w; files_app_back(ud); }
static void on_forward(tk_widget *w, void *ud) { (void)w; files_app_forward(ud); }
static void on_up_btn(tk_widget *w, void *ud) { (void)w; files_app_up(ud); }

static void on_search_toggle(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    bool vis = tk_is_visible(app->search_row);
    tk_set_visible(app->search_row, !vis);
    if (!vis) tk_focus(app->root, app->search_field);
    tk_widget_invalidate(app->top);
    tk_root_layout(app->root, app->win_w, app->win_h);
}

static void on_search_submit(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    char q[256];
    tk_textfield_text(app->search_field, q, sizeof(q));
    if (q[0]) files_app_search(app, q);
    else files_app_refresh(app);
}

static void on_path_toggle(files_app *app)
{
    bool vis = tk_is_visible(app->path_row);
    tk_set_visible(app->path_row, !vis);
    if (!vis) tk_focus(app->root, app->path_field);
    tk_widget_invalidate(app->top);
    tk_root_layout(app->root, app->win_w, app->win_h);
}

static void on_go_path(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    char p[FM_PATH_MAX];
    tk_textfield_text(app->path_field, p, sizeof(p));
    struct stat st;
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
        files_app_navigate(app, p);
    else
        set_status(app, "フォルダがありません: %s", p);
}

static void on_list_select(tk_widget *w, void *ud)
{
    files_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) files_app_select_row(app, (size_t)sel);
}
static void on_list_activate(tk_widget *w, void *ud)
{
    files_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) {
        files_app_select_row(app, (size_t)sel);
        files_app_open_selected(app);
    }
}

static void on_grid_click(tk_widget *w, void *ud)
{
    files_app *app = ud;
    /* derive the entry index from the button's position among the
     * grid's children */
    for (size_t i = 0; i < tk_child_count(app->grid); i++) {
        if (tk_child_at(app->grid, i) == w) {
            if (app->sel_entry == (int)i) {
                files_app_open_selected(app);
            } else {
                app->sel_entry = (int)i;
                set_status(app, "選択: %s",
                           app->trash_view
                               ? app->trash_items.v[i].name
                               : app->listing.v[i].name);
                sync_context_bar(app);
            }
            return;
        }
    }
}

/* places: the sidebar became a menu (DESIGN-apps-envfit SS7.1) */
static void places_go(files_app *app, int sel)
{
    const char *home = getenv("HOME");
    char p[FM_PATH_MAX];
    switch (sel) {
    case 0: if (home) files_app_navigate(app, home); break;
    case 1:
        if (home) {
            snprintf(p, sizeof(p), "%s/Downloads", home);
            files_app_navigate(app, p);
        }
        break;
    case 2:
        if (home) {
            snprintf(p, sizeof(p), "%s/Documents", home);
            files_app_navigate(app, p);
        }
        break;
    case 3:
        if (home) {
            snprintf(p, sizeof(p), "%s/Pictures", home);
            files_app_navigate(app, p);
        }
        break;
    case 4:
        if (home) {
            snprintf(p, sizeof(p), "%s/Music", home);
            files_app_navigate(app, p);
        }
        break;
    case 5: {
        const char *sd = getenv("BD_FILES_SD_DIR");
        files_app_navigate(app, sd ? sd : "/media/sd");
        break;
    }
    case 6: files_app_open_trash(app); break;
    default: break;
    }
}

enum { PROMPT_FOLDER = 1, PROMPT_FILE = 2, PROMPT_RENAME = 3 };

static void start_prompt(files_app *app, int op, const char *title,
                         const char *initial)
{
    app->prompt_op = op;
    tk_label_set_text(app->prompt_title, title);
    tk_textfield_set_text(app->prompt_field, initial);
    tk_widget_invalidate(app->prompt_dlg);
    tk_dialog_show(app->root, app->prompt_dlg);
}

static void on_prompt_ok(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    char name[256];
    tk_textfield_text(app->prompt_field, name, sizeof(name));
    int rc = 0;
    switch (app->prompt_op) {
    case PROMPT_FOLDER: rc = files_app_new_folder(app, name); break;
    case PROMPT_FILE: rc = files_app_new_file(app, name); break;
    case PROMPT_RENAME: rc = files_app_rename_selected(app, name); break;
    default: break;
    }
    if (rc != 0) set_status(app, "失敗しました（同名が存在する可能性）");
    tk_dialog_close(app->root, app->prompt_dlg);
}

static void on_prompt_cancel(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    tk_dialog_close(app->root, app->prompt_dlg);
}

static void on_new_folder(files_app *app)
{
    start_prompt(app, PROMPT_FOLDER, "新規フォルダ", "新規フォルダ");
}
static void on_new_file(files_app *app)
{
    start_prompt(app, PROMPT_FILE, "新規ファイル", "新規ファイル.txt");
}
static void start_rename_prompt(files_app *app)
{
    if (app->sel_entry < 0 || app->trash_view) return;
    start_prompt(app, PROMPT_RENAME, "名前の変更",
                 app->listing.v[app->sel_entry].name);
}
static void on_rename(tk_widget *w, void *ud)
{
    (void)w;
    start_rename_prompt(ud);
}
static void on_open(tk_widget *w, void *ud)
{
    (void)w;
    files_app_open_selected(ud);
}
static void on_copy(tk_widget *w, void *ud)
{
    (void)w;
    files_app_copy_selected(ud, false);
}
static void on_delete(tk_widget *w, void *ud)
{
    (void)w;
    files_app_delete_selected(ud);
}
static void on_restore(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    if (app->trash_view) files_app_restore_selected(app);
}
static void on_perm_delete(tk_widget *w, void *ud)
{
    (void)w;
    files_app_delete_permanent_selected(ud);
}
static void on_paste(tk_widget *w, void *ud)
{
    (void)w;
    files_app_paste(ud);
}

static void on_props(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    if (app->sel_entry < 0 || app->trash_view) return;
    const fm_entry *e = &app->listing.v[app->sel_entry];
    fm_props p;
    if (fm_props_load(e->path, &p) != 0) return;
    char sz[32], sd[32];
    bd_format_size(p.size, sz, sizeof(sz));
    bd_format_size(p.size_on_disk, sd, sizeof(sd));
    char lines[10][512];
    snprintf(lines[0], 512, "名前: %s", p.name);
    snprintf(lines[1], 512, "種類: %s", p.kind);
    snprintf(lines[2], 512, "場所: %s", p.path);
    snprintf(lines[3], 512, "サイズ: %s (%llu バイト)",
             sz, (unsigned long long)p.size);
    snprintf(lines[4], 512, "ディスク上: %s", sd);
    if (p.is_dir)
        snprintf(lines[5], 512, "内容: ファイル %llu / フォルダ %llu",
                 (unsigned long long)p.n_files,
                 (unsigned long long)p.n_dirs);
    else if (p.img_w > 0)
        snprintf(lines[5], 512, "画像サイズ: %d x %d", p.img_w, p.img_h);
    else
        snprintf(lines[5], 512, "内容: -");
    char tbuf[64];
    struct tm tmv;
    time_t mt = (time_t)p.mtime;
    localtime_r(&mt, &tmv);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
    snprintf(lines[6], 512, "更新: %s", tbuf);
    mt = (time_t)p.ctime;
    localtime_r(&mt, &tmv);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
    snprintf(lines[7], 512, "作成: %s", tbuf);
    snprintf(lines[8], 512, "アクセス権: %s (%04o)", p.perms,
             p.mode & 07777);
    snprintf(lines[9], 512, "所有者: %d / グループ: %d%s%s", p.uid, p.gid,
             p.readonly_flag ? " / 読取専用" : "",
             p.hidden_flag ? " / 隠し" : "");
    for (int i = 0; i < 10; i++)
        tk_label_set_text(app->props_labels[i], lines[i]);
    tk_widget_invalidate(app->props_dlg);
    /* image dimensions come through the budgeted cache in grid
     * workflows; properties reads them directly from fm_props */
    int dw = 0, dh = 0;
    if (!p.is_dir && fm_thumbcache_dims(app->thumbs, e->path, &dw, &dh)) {
        char extra[128];
        snprintf(extra, sizeof(extra), "画像サイズ（キャッシュ）: %d x %d",
                 dw, dh);
        tk_label_set_text(app->props_labels[5], extra);
    }
    tk_widget_invalidate(app->props_dlg);
    tk_dialog_show(app->root, app->props_dlg);
}
static void on_props_close(tk_widget *w, void *ud)
{
    (void)w;
    files_app *app = ud;
    tk_dialog_close(app->root, app->props_dlg);
}

/* ---------------------------------------------------------------- */
/* menus (rebuilt fresh on every open - see the struct comment)      */
/* ---------------------------------------------------------------- */

enum {
    FMN_FWD = 1, FMN_UP, FMN_NEW_FOLDER, FMN_NEW_FILE,
    FMN_OPEN, FMN_COPY, FMN_CUT, FMN_PASTE, FMN_RENAME, FMN_DELETE,
    FMN_RESTORE, FMN_PERM, FMN_PROPS, FMN_EMPTY_TRASH,
    FMN_SORT_NAME, FMN_SORT_MTIME, FMN_SORT_SIZE, FMN_SORT_TYPE,
    FMN_SORT_DESC, FMN_GROUP_NONE, FMN_GROUP_TYPE, FMN_GROUP_DATE,
    FMN_HIDDEN, FMN_GRID, FMN_PATH
};

static void on_overflow_select(tk_widget *menu, void *ud)
{
    files_app *app = ud;
    int id = tk_menu_last_id(menu);
    switch (id) {
    case FMN_FWD: files_app_forward(app); break;
    case FMN_UP: files_app_up(app); break;
    case FMN_NEW_FOLDER: on_new_folder(app); break;
    case FMN_NEW_FILE: on_new_file(app); break;
    case FMN_OPEN: files_app_open_selected(app); break;
    case FMN_COPY: files_app_copy_selected(app, false); break;
    case FMN_CUT: files_app_copy_selected(app, true); break;
    case FMN_PASTE: files_app_paste(app); break;
    case FMN_RENAME: start_rename_prompt(app); break;
    case FMN_DELETE: files_app_delete_selected(app); break;
    case FMN_RESTORE: files_app_restore_selected(app); break;
    case FMN_PERM: files_app_delete_permanent_selected(app); break;
    case FMN_PROPS: on_props(NULL, app); break;
    case FMN_EMPTY_TRASH: files_app_empty_trash(app); break;
    case FMN_SORT_NAME:
        files_app_set_sort(app, FM_SORT_NAME, app->sort_desc); break;
    case FMN_SORT_MTIME:
        files_app_set_sort(app, FM_SORT_MTIME, app->sort_desc); break;
    case FMN_SORT_SIZE:
        files_app_set_sort(app, FM_SORT_SIZE, app->sort_desc); break;
    case FMN_SORT_TYPE:
        files_app_set_sort(app, FM_SORT_TYPE, app->sort_desc); break;
    case FMN_SORT_DESC:
        /* the check item auto-toggled before this callback */
        files_app_set_sort(app, app->sort_key,
                           tk_menu_item_checked(menu, FMN_SORT_DESC));
        break;
    case FMN_GROUP_NONE: files_app_set_group(app, FM_GROUP_NONE); break;
    case FMN_GROUP_TYPE: files_app_set_group(app, FM_GROUP_TYPE); break;
    case FMN_GROUP_DATE: files_app_set_group(app, FM_GROUP_DATE); break;
    case FMN_HIDDEN:
        files_app_set_show_hidden(app,
                                  tk_menu_item_checked(menu, FMN_HIDDEN));
        break;
    case FMN_GRID:
        files_app_set_grid(app, tk_menu_item_checked(menu, FMN_GRID));
        break;
    case FMN_PATH: on_path_toggle(app); break;
    default: break;
    }
}

static tk_widget *build_overflow_menu(files_app *app)
{
    tk_widget *m = tk_menu_new();
    tk_menu_set_on_select(m, on_overflow_select, app);
    bool sel = app->sel_entry >= 0;
    if (app->trash_view) {
        tk_menu_add_item(m, "復元", FMN_RESTORE);
        tk_menu_add_item(m, "完全削除", FMN_PERM);
        tk_menu_set_item_enabled(m, FMN_RESTORE, sel);
        tk_menu_set_item_enabled(m, FMN_PERM, sel);
        tk_menu_add_separator(m);
        tk_menu_add_item(m, "ゴミ箱を空にする", FMN_EMPTY_TRASH);
        return m;
    }
    tk_menu_add_item(m, "開く", FMN_OPEN);
    tk_menu_set_item_enabled(m, FMN_OPEN, sel);
    tk_menu_add_item(m, "進む", FMN_FWD);
    tk_menu_add_item(m, "上へ", FMN_UP);
    tk_menu_add_separator(m);
    tk_menu_add_item(m, "新規フォルダ", FMN_NEW_FOLDER);
    tk_menu_add_item(m, "新規ファイル", FMN_NEW_FILE);
    tk_menu_add_separator(m);
    tk_menu_add_item(m, "コピー", FMN_COPY);
    tk_menu_add_item(m, "切取", FMN_CUT);
    tk_menu_add_item(m, "貼付", FMN_PASTE);
    tk_menu_add_item(m, "名前変更", FMN_RENAME);
    tk_menu_add_item(m, "削除", FMN_DELETE);
    tk_menu_add_item(m, "情報", FMN_PROPS);
    tk_menu_set_item_enabled(m, FMN_COPY, sel);
    tk_menu_set_item_enabled(m, FMN_CUT, sel);
    tk_menu_set_item_enabled(m, FMN_PASTE, app->n_clip > 0);
    tk_menu_set_item_enabled(m, FMN_RENAME, sel);
    tk_menu_set_item_enabled(m, FMN_DELETE, sel);
    tk_menu_set_item_enabled(m, FMN_PROPS, sel);
    tk_menu_add_separator(m);
    tk_widget *sortm = tk_menu_new();
    tk_menu_set_on_select(sortm, on_overflow_select, app);
    tk_menu_add_check_item(sortm, "名前", FMN_SORT_NAME,
                           app->sort_key == FM_SORT_NAME);
    tk_menu_add_check_item(sortm, "更新日時", FMN_SORT_MTIME,
                           app->sort_key == FM_SORT_MTIME);
    tk_menu_add_check_item(sortm, "サイズ", FMN_SORT_SIZE,
                           app->sort_key == FM_SORT_SIZE);
    tk_menu_add_check_item(sortm, "種類", FMN_SORT_TYPE,
                           app->sort_key == FM_SORT_TYPE);
    tk_menu_add_separator(sortm);
    tk_menu_add_check_item(sortm, "降順", FMN_SORT_DESC, app->sort_desc);
    tk_menu_add_submenu(m, "並び替え", sortm);
    tk_widget *grpm = tk_menu_new();
    tk_menu_set_on_select(grpm, on_overflow_select, app);
    tk_menu_add_check_item(grpm, "グループなし", FMN_GROUP_NONE,
                           app->group == FM_GROUP_NONE);
    tk_menu_add_check_item(grpm, "種類別", FMN_GROUP_TYPE,
                           app->group == FM_GROUP_TYPE);
    tk_menu_add_check_item(grpm, "日付別", FMN_GROUP_DATE,
                           app->group == FM_GROUP_DATE);
    tk_menu_add_submenu(m, "グループ", grpm);
    tk_menu_add_check_item(m, "隠しファイル", FMN_HIDDEN,
                           app->show_hidden);
    tk_menu_add_check_item(m, "グリッド表示", FMN_GRID, app->grid_view);
    tk_menu_add_item(m, "パス入力", FMN_PATH);
    return m;
}

static void open_overflow_menu(files_app *app, tk_widget *anchor)
{
    if (app->overflow_menu) tk_widget_free(app->overflow_menu);
    app->overflow_menu = build_overflow_menu(app);
    tk_rect r = tk_widget_rect(anchor);
    tk_menu_popup(app->root, app->overflow_menu,
                  r.x + r.w, r.y + r.h);
}

static void on_more_btn(tk_widget *w, void *ud)
{
    open_overflow_menu(ud, w);
}
static void on_more_cell(tk_widget *w, void *ud)
{
    open_overflow_menu(ud, w);
}

static void on_places_select(tk_widget *menu, void *ud)
{
    files_app *app = ud;
    places_go(app, tk_menu_last_id(menu));
}

static void on_places_btn(tk_widget *w, void *ud)
{
    files_app *app = ud;
    if (app->places_menu) tk_widget_free(app->places_menu);
    tk_widget *m = tk_menu_new();
    app->places_menu = m;
    tk_menu_set_on_select(m, on_places_select, app);
    static const char *names[7] = { "ホーム", "ダウンロード",
                                    "ドキュメント", "画像", "音楽",
                                    "SDカード", "ゴミ箱" };
    static const char *const picons[7] = {
        "user-home", "folder-download", "folder-documents",
        "folder-pictures", "folder-music", "drive-removable-media",
        "user-trash"
    };
    for (int i = 0; i < 7; i++) {
        tk_menu_add_item(m, names[i], i);
        const uint32_t *ic = bd_icon_color(picons[i], 24);
        if (ic) tk_menu_set_item_icon(m, i, ic, 24, 24);
    }
    tk_rect r = tk_widget_rect(w);
    tk_menu_popup(app->root, m, r.x + r.w, r.y + r.h);
}

/* ---------------------------------------------------------------- */
/* construction                                                      */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
/* WinUI form helpers: breadcrumb segments + nav pane              */
/* ---------------------------------------------------------------- */

static void on_crumb(tk_widget *w, void *ud)
{
    (void)w;
    crumb_cb *cb = ud;
    if (cb->idx >= 0 && cb->idx < cb->app->n_crumb_paths)
        navigate_to(cb->app, cb->app->crumb_paths[cb->idx], true);
}

static void add_crumb(files_app *app, const char *label,
                      const char *path)
{
    int i = app->n_crumb_paths;
    if (i >= 40) return;
    app->crumb_paths[i] = strdup(path);
    app->crumb_cbs[i].app = app;
    app->crumb_cbs[i].idx = i;
    app->n_crumb_paths++;
    tk_widget *b = tk_button_new(label);
    tk_button_set_flat(b, true);
    tk_button_set_on_click(b, on_crumb, &app->crumb_cbs[i]);
    tk_add_child(app->crumb_box, b);
}

/* Rebuild the breadcrumb segments from app->cur (called from
 * update_header on every refresh in the WinUI form). */
static void rebuild_crumbs(files_app *app)
{
    if (!app->crumb_box) return;
    while (tk_child_count(app->crumb_box) > 0) {
        tk_widget *ch = tk_child_at(app->crumb_box, 0);
        tk_remove_child(app->crumb_box, ch);
        tk_widget_free(ch);
    }
    for (int i = 0; i < app->n_crumb_paths; i++) {
        free(app->crumb_paths[i]);
        app->crumb_paths[i] = NULL;
    }
    app->n_crumb_paths = 0;
    if (app->trash_view) {
        tk_add_child(app->crumb_box, tk_label_new("ゴミ箱"));
        return;
    }
    if (app->listing_is_search) {
        tk_add_child(app->crumb_box, tk_label_new("検索結果"));
        return;
    }
    /* collect segment offsets in cur first: with a deep path
     * the segments would overflow the command bar and paint
     * under its buttons, so beyond 5 crumbs the middle is
     * collapsed into an ellipsis (Explorer's behaviour) */
    int offs[40], lens[40], nseg = 0;
    for (const char *p = app->cur; *p && nseg < 40;) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        offs[nseg] = (int)(s - app->cur);
        lens[nseg] = (int)(p - s);
        nseg++;
    }
    /* Width-based collapse (int12): the count rule above is
     * not enough - four segments with a long middle name
     * (a temp-dir path is the everyday case) still overflowed
     * the command bar's flex allotment, and the box clipped
     * the last segment to ~10px (test_matrix floor24, WinUI
     * 850/854).  Measure the segment buttons and drop middle
     * segments into the ellipsis until the row fits the
     * crumb box's allotment (its last arranged width, or,
     * before the first layout, an estimate from the window
     * width calibrated to the host probe, which measured a
     * 322px allotment at an 854px window).  Root and the
     * current folder are never dropped. */
    int first = 0;
    bool ellipsis = false;
    if (nseg + 1 > 5) {
        ellipsis = true;
        first = nseg - 2;
    }
    {
        tk_rect cr = tk_widget_rect(app->crumb_box);
        int avail = cr.w > 0 ? cr.w : app->win_w - 532;
        if (avail < 120) avail = 120;
        /* segment label texts (root first) */
        char lbl[40][80];
        int nlbl = 0;
        snprintf(lbl[nlbl++], 80, "ルート");
        for (int i = 0; i < nseg; i++) {
            int llen = lens[i] < 79 ? lens[i] : 79;
            memcpy(lbl[nlbl], app->cur + offs[i], (size_t)llen);
            lbl[nlbl][llen] = '\0';
            nlbl++;
        }
        for (;;) {
            int total = 0;
            tk_widget *tmp = tk_button_new(lbl[0]);
            if (tmp) {
                total += tk_measure(tmp, (tk_constraints){
                    0, TK_UNLIMITED, 0, TK_UNLIMITED}).w;
                tk_widget_free(tmp);
            }
            if (ellipsis) {
                tk_widget *el = tk_label_new("…");
                if (el) {
                    total += tk_measure(el, (tk_constraints){
                        0, TK_UNLIMITED, 0, TK_UNLIMITED}).w;
                    tk_widget_free(el);
                }
                total += 2; /* crumb_box spacing */
            }
            for (int i = first; i < nseg; i++) {
                tk_widget *tb = tk_button_new(lbl[i + 1]);
                if (tb) {
                    total += tk_measure(tb, (tk_constraints){
                        0, TK_UNLIMITED, 0, TK_UNLIMITED}).w;
                    tk_widget_free(tb);
                }
                total += 2;
            }
            if (total <= avail || first >= nseg - 1) break;
            ellipsis = true;
            first++;
        }
    }
    add_crumb(app, "ルート", "/");
    if (ellipsis)
        tk_add_child(app->crumb_box, tk_label_new("…"));
    for (int i = first; i < nseg; i++) {
        char path[FM_PATH_MAX], label[80];
        int plen = offs[i] + lens[i];
        if (plen >= (int)sizeof(path)) plen = (int)sizeof(path) - 1;
        memcpy(path, app->cur, (size_t)plen);
        path[plen] = '\0';
        int llen = lens[i] < 79 ? lens[i] : 79;
        memcpy(label, app->cur + offs[i], (size_t)llen);
        label[llen] = '\0';
        add_crumb(app, label, path);
    }
}

static void on_nav_select(tk_widget *w, void *ud)
{
    files_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) places_go(app, sel);
}


/* Android-mode Back key (delivered by the WM as TK_KEY_BACK,
 * bubbled up to the top widget's event hook): dismiss open
 * dialogs and transient rows first, then walk the folder
 * history. Unconsumed at the history start so the shell can
 * have the key. */
static bool files_back_key(void *ud, tk_event *ev)
{
    files_app *app = ud;
    if (!ev || ev->type != TK_EV_KEY_DOWN || ev->key != TK_KEY_BACK)
        return false;
    if (tk_dialog_is_shown(app->root, app->prompt_dlg)) {
        tk_dialog_close(app->root, app->prompt_dlg);
        return true;
    }
    if (tk_dialog_is_shown(app->root, app->props_dlg)) {
        tk_dialog_close(app->root, app->props_dlg);
        return true;
    }
    if (tk_is_visible(app->search_row)) {
        tk_set_visible(app->search_row, false);
        tk_widget_invalidate(app->top);
        return true;
    }
    if (tk_is_visible(app->path_row)) {
        tk_set_visible(app->path_row, false);
        tk_widget_invalidate(app->top);
        return true;
    }
    if (fm_history_can_back(&app->hist)) {
        files_app_back(app);
        return true;
    }
    return false;
}

files_app *files_app_new(const char *start_dir, const char *data_root,
                         int w, int h)
{
    files_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->data_root = strdup(data_root);
    app->sel_entry = -1;
    app->sort_key = FM_SORT_NAME;

    /* prefs from brainreg */
    app->show_hidden = reg_get_bool("files", "show_hidden", false);
    app->grid_view = false;
    {
        char *v = reg_get("files", "view");
        if (v && strcmp(v, "grid") == 0) app->grid_view = true;
        free(v);
        app->sort_key = (fm_sort_key)reg_get_int("files", "sort_key", 0);
        app->sort_desc = reg_get_bool("files", "sort_desc", false);
        app->group = (fm_group)reg_get_int("files", "group", 0);
    }

    /* trash + caches */
    {
        char tp[FM_PATH_MAX];
        snprintf(tp, sizeof(tp), "%s/trash", data_root);
        int budget = reg_get_int("files", "trash_budget_bytes",
                                 32 << 20);
        app->trash = fm_trash_open(tp, (uint64_t)budget);
        if (!app->trash) { free(app->data_root); free(app); return NULL; }
    }
    app->thumbs = fm_thumbcache_new(256 * 1024);

    /* application catalog for open-with */
    app->cat = de_catalog_new("ja");
    const char *dirs = getenv("BD_FILES_APP_DIRS");
    if (dirs) {
        char *copy = strdup(dirs);
        for (char *tok = strtok(copy, ":"); tok;
             tok = strtok(NULL, ":"))
            de_catalog_load_dir(app->cat, tok);
        free(copy);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            char p[FM_PATH_MAX];
            snprintf(p, sizeof(p), "%s/.local/share/applications", home);
            de_catalog_load_dir(app->cat, p);
        }
        de_catalog_load_dir(app->cat, "/usr/share/applications");
    }

    /* ---- widgets ---- */
    app->winui = bd_app_winui(tk_theme_default());
    draw_row_icons(app);

    /* search row: opened by the header / command-bar search icon */
    app->search_row = tk_box_new(TK_DIR_HORIZONTAL, 9, 0); /* int14: field+button = C2-A 9px */
    app->search_field = tk_textfield_new("");
    tk_textfield_set_placeholder(app->search_field, "このフォルダを検索");
    tk_set_flex(app->search_field, 1);
    tk_add_child(app->search_row, app->search_field);
    tk_widget *search_go = btn("検索", on_search_submit, app);
    tk_button_set_variant(search_go, TK_BTN_TONAL);
    tk_add_child(app->search_row, search_go);
    tk_set_visible(app->search_row, false);

    /* path entry row: opened from the overflow menu (Ctrl+L
     * pattern - the raw path is shown once, in the header
     * caption / breadcrumbs, until editing is requested) */
    app->path_row = tk_box_new(TK_DIR_HORIZONTAL, 9, 0); /* int14: field+button = C2-A 9px */
    app->path_field = tk_textfield_new("");
    tk_set_flex(app->path_field, 1);
    tk_add_child(app->path_row, app->path_field);
    tk_widget *path_go = btn("移動", on_go_path, app);
    tk_button_set_variant(path_go, TK_BTN_TONAL);
    tk_add_child(app->path_row, path_go);
    tk_set_visible(app->path_row, false);

    /* content: the list (or the grid) owns the middle */
    app->content_box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_set_flex(app->content_box, 1);
    app->list = tk_listview_new();
    tk_listview_set_on_select(app->list, on_list_select, app);
    tk_listview_set_on_activate(app->list, on_list_activate, app);
    tk_set_flex(app->list, 1);
    tk_add_child(app->content_box, app->list);
    /* int14: empty-state guidance in the calc two-line form -
     * a single long caption clipped in the WinUI right column
     * (C10); state line + how-to line, each short enough for
     * the narrowest pane. */
    app->empty_label = tk_box_new(TK_DIR_VERTICAL, 2, 0);
    {
        tk_widget *e1 = tk_label_new("このフォルダは空です");
        tk_widget_set_text_style(e1, TK_TEXT_CAPTION);
        tk_add_child(app->empty_label, e1);
        tk_widget *e2 = tk_label_new(
            "「その他」メニューから新規フォルダを作成できます");
        tk_widget_set_text_style(e2, TK_TEXT_CAPTION);
        tk_add_child(app->empty_label, e2);
    }
    tk_set_visible(app->empty_label, false);
    tk_add_child(app->content_box, app->empty_label);

    /* context bar: the selection's actions. Bottom bar in the
     * Android form; a command row under the command bar in the
     * WinUI form (Explorer's contextual-commands position).
     * Icons are the real set icons via bdicon (borrowed). */
    app->context_bar = tk_bottombar_new();
    {
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "arrow_forward", "arrow_right"), 24, 24,
            "開く", on_open, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "content_copy", "copy"), 24, 24,
            "コピー", on_copy, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "drive_file_rename_outline", "rename"),
            24, 24, "名前変更", on_rename, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "delete", "delete"), 24, 24,
            "削除", on_delete, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "restore_from_trash", "arrow_undo"),
            24, 24, "復元", on_restore, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "delete_forever", "delete"), 24, 24,
            "完全削除", on_perm_delete, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "content_paste", "clipboard_paste"),
            24, 24, "貼付", on_paste, app);
        tk_bottombar_add_cell(app->context_bar,
            action_icon(app, "more_horiz", "more_horizontal"), 24, 24,
            "その他", on_more_cell, app);
        app->cell_open = tk_bottombar_cell(app->context_bar, 0);
        app->cell_copy = tk_bottombar_cell(app->context_bar, 1);
        app->cell_rename = tk_bottombar_cell(app->context_bar, 2);
        app->cell_delete = tk_bottombar_cell(app->context_bar, 3);
        app->cell_restore = tk_bottombar_cell(app->context_bar, 4);
        app->cell_perm = tk_bottombar_cell(app->context_bar, 5);
        app->cell_paste = tk_bottombar_cell(app->context_bar, 6);
        app->cell_more = tk_bottombar_cell(app->context_bar, 7);
    }
    tk_set_visible(app->context_bar, false);

    /* status row: item count + free space (SS3.3: state is one
     * caption line, never mixed with controls) */
    tk_widget *status_row = tk_box_new(TK_DIR_HORIZONTAL, 8, 0);
    app->status = tk_label_new("");
    tk_set_flex(app->status, 1);
    tk_add_child(status_row, app->status);
    app->space_label = tk_label_new("");
    tk_widget_set_text_style(app->space_label, TK_TEXT_CAPTION);
    tk_add_child(status_row, app->space_label);
    app->space_bar = tk_progress_new(0.0f);
    tk_widget_set_min_size(app->space_bar, 80, 0);
    tk_add_child(status_row, app->space_bar);

    tk_widget *top;
    if (app->winui) {
        /* WinUI form (Explorer structure): navigation pane on
         * the left with the places, command bar with back /
         * forward / up and breadcrumb segments on the right
         * column. No drill-down header. */
        top = tk_box_new(TK_DIR_HORIZONTAL, 4, 6);
        tk_widget *pane = tk_box_new(TK_DIR_VERTICAL, 2, 4);
        tk_box_set_pad_x(pane, 11); /* int14 C7: nav face 18px from
                                     * the window content edge and
                                     * (with LV_PAD_X) its icons 18px
                                     * inside the face - int13's 9 put
                                     * them at 16px/17px (round 5,
                                     * photo 21) */
        tk_widget_set_min_size(pane, 208, 0);
        tk_widget *cap = tk_label_new("場所");
        tk_widget_set_text_style(cap, TK_TEXT_CAPTION);
        tk_add_child(pane, cap);
        app->nav_list = tk_listview_new();
        {
            static const char *const names[7] = {
                "ホーム", "ダウンロード", "ドキュメント", "画像",
                "音楽", "SDカード", "ゴミ箱"
            };
            static const char *const icons[7] = {
                "user-home", "folder-download", "folder-documents",
                "folder-pictures", "folder-music",
                "drive-removable-media", "user-trash"
            };
            tk_listview_set_items(app->nav_list, names, 7);
            const uint32_t *icon_ptrs[7];
            for (int i = 0; i < 7; i++)
                icon_ptrs[i] = bd_icon_color(icons[i], 24);
            tk_listview_set_item_icons(app->nav_list, icon_ptrs,
                                       24, 24, 7);
        }
        tk_listview_set_on_select(app->nav_list, on_nav_select, app);
        tk_set_flex(app->nav_list, 1);
        tk_add_child(pane, app->nav_list);
        tk_add_child(top, pane);

        tk_widget *right = tk_box_new(TK_DIR_VERTICAL, 4, 6);
        tk_box_set_pad_x(right, BD_PAGE_PAD_X);
        tk_set_flex(right, 1);
        tk_widget *cmdbar = tk_box_new(TK_DIR_HORIZONTAL, 9, 0);
        tk_widget_set_min_size(cmdbar, 0, 56);
        tk_add_child(cmdbar, header_icon_btn("戻る",
            action_icon(app, "arrow_back", "arrow_left"),
            on_back, app));
        tk_add_child(cmdbar, header_icon_btn("進む",
            action_icon(app, "arrow_forward", "arrow_right"),
            on_forward, app));
        tk_add_child(cmdbar, header_icon_btn("上へ",
            action_icon(app, "arrow_upward", "arrow_up"),
            on_up_btn, app));
        app->crumb_box = tk_box_new(TK_DIR_HORIZONTAL, 2, 0);
        tk_set_flex(app->crumb_box, 1);
        tk_add_child(cmdbar, app->crumb_box);
        tk_add_child(cmdbar, header_icon_btn("検索",
            action_icon(app, "search", "search"),
            on_search_toggle, app));
        tk_add_child(cmdbar, header_icon_btn("その他",
            action_icon(app, "more_vert", "more_horizontal"),
            on_more_btn, app));
        tk_add_child(right, cmdbar);
        tk_add_child(right, app->search_row);
        tk_add_child(right, app->path_row);
        tk_add_child(right, app->context_bar);
        tk_add_child(right, app->content_box);
        tk_add_child(right, status_row);
        tk_add_child(top, right);
    } else {
        top = tk_box_new(TK_DIR_VERTICAL, 4, 6);
        bd_page_pad_x(top);

        /* Header (DESIGN-apps-envfit SS7.1): back icon, folder
         * name + path caption, then search / places / overflow
         * icons. The header is top's first child and Back is
         * its first child (UI test contract, SS5.1). */
        tk_widget *header = tk_box_new(TK_DIR_HORIZONTAL, 9, 0);
        tk_widget_set_min_size(header, 0, 56);
        uint32_t tint = bd_tint_of(tk_theme_default());
        tk_add_child(header, header_icon_btn("戻る",
            bd_icon("material/arrow_back", 24, tint), on_back, app));
        tk_widget *title_box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(title_box, 1);
        app->header_title = tk_label_new("");
        tk_widget_set_text_style(app->header_title, TK_TEXT_HEADING);
        tk_add_child(title_box, app->header_title);
        app->header_path = tk_label_new("");
        tk_widget_set_text_style(app->header_path, TK_TEXT_CAPTION);
        tk_add_child(title_box, app->header_path);
        tk_add_child(header, title_box);
        tk_add_child(header, header_icon_btn("検索",
            bd_icon("material/search", 24, tint),
            on_search_toggle, app));
        tk_add_child(header, header_icon_btn("場所", app->icon_dir,
                                             on_places_btn, app));
        tk_add_child(header, header_icon_btn("その他",
            bd_icon("material/more_vert", 24, tint),
            on_more_btn, app));
        tk_add_child(top, header);
        tk_add_child(top, app->search_row);
        tk_add_child(top, app->path_row);
        tk_add_child(top, app->content_box);
        tk_add_child(top, app->context_bar);
        tk_add_child(top, status_row);
    }

    /* prompt dialog */
    app->prompt_dlg = tk_dialog_new("入力");
    app->prompt_title = tk_label_new("");
    app->prompt_field = tk_textfield_new("");
    tk_add_child(app->prompt_dlg, app->prompt_title);
    tk_add_child(app->prompt_dlg, app->prompt_field);
    tk_widget *prompt_btns = tk_box_new(TK_DIR_HORIZONTAL, 9, 0);
    tk_widget *prompt_cancel = btn("キャンセル", on_prompt_cancel, app);
    tk_button_set_variant(prompt_cancel, TK_BTN_TEXT);
    tk_add_child(prompt_btns, prompt_cancel);
    tk_widget *prompt_ok = btn("OK", on_prompt_ok, app);
    tk_button_set_variant(prompt_ok, TK_BTN_FILLED);
    tk_add_child(prompt_btns, prompt_ok);
    tk_add_child(app->prompt_dlg, prompt_btns);

    /* properties dialog */
    app->props_dlg = tk_dialog_new("プロパティ");
    for (int i = 0; i < 10; i++) {
        app->props_labels[i] = tk_label_new("");
        tk_add_child(app->props_dlg, app->props_labels[i]);
    }
    tk_widget *props_close = btn("閉じる", on_props_close, app);
    tk_button_set_variant(props_close, TK_BTN_TONAL);
    tk_add_child(app->props_dlg, props_close);

    app->win_w = w;
    app->win_h = h;
    app->top = top;
    app->root = tk_root_new(top);
    tk_widget_set_event_hook(top, files_back_key, app);
    tk_root_layout(app->root, w, h);

    fm_history_init(&app->hist);
    navigate_to(app, start_dir, true);
    return app;
}

void files_app_free(files_app *app)
{
    if (!app) return;
    /* row icons are borrowed from the bdicon cache: not freed */
    for (int i = 0; i < app->n_crumb_paths; i++)
        free(app->crumb_paths[i]);
    if (app->root && app->overflow_menu)
        tk_menu_close(app->root, app->overflow_menu);
    if (app->root && app->places_menu)
        tk_menu_close(app->root, app->places_menu);
    if (app->overflow_menu) tk_widget_free(app->overflow_menu);
    if (app->places_menu) tk_widget_free(app->places_menu);
    tk_widget_free(app->prompt_dlg);
    tk_widget_free(app->props_dlg);
    tk_root_free(app->root);
    tk_widget_free(app->top);
    fm_listing_free(&app->listing);
    fm_trash_list_free(&app->trash_items);
    fm_trash_close(app->trash);
    fm_thumbcache_free(app->thumbs);
    if (app->cat) de_catalog_free(app->cat);
    fm_history_free(&app->hist);
    for (size_t i = 0; i < app->n_clip; i++) free(app->clip[i]);
    free(app->clip);
    free(app->row_map);
    free(app->data_root);
    free(app);
}

tk_root *files_app_root(files_app *app) { return app->root; }

/* ---------------------------------------------------------------- */
#ifdef BD_FILES_MAIN
int main(int argc, char **argv)
{
    bd_app_theme(); /* ui/mode from brainreg (REPORT-apps-dual-mode) */
    const char *home = getenv("HOME");
    char data[1024];
    const char *env_data = getenv("BRAINUX_FILES_DIR");
    if (env_data) snprintf(data, sizeof(data), "%s", env_data);
    else if (home) snprintf(data, sizeof(data), "%s/.local/share/brainux/files", home);
    else snprintf(data, sizeof(data), ".local/share/brainux/files");
    (void)bd_mkdir_p(data);
    const char *start = argc > 1 ? argv[1] : (home ? home : "/");

    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    /* Display first, real size (see bd_calc.c main note) */
    int vw, vh;
    bd_app_display *disp = bd_app_display_open_ex("ファイル", 0, 0, 480, 272, 480, 272, &vw, &vh);
    files_app *app = files_app_new(start, data, vw, vh);
    if (!app) { fprintf(stderr, "bd-files: init failed\n"); return 1; }

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, NULL, NULL, 20);
    } else {
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-files: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    files_app_free(app);
    if (font) tk_font_free(font);
    return 0;
}
#endif /* BD_FILES_MAIN */
