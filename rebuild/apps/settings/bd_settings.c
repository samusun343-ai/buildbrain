/* bd_settings - see bd_settings.h and
 * DESIGN-settings-rebuild.md (the researched design this
 * implements: AOSP Settings for the Android mode, Windows
 * 11 Settings for the WinUI mode, and the item mapping
 * table that decides what exists at all).
 *
 * Interaction model (kept from the 2026-11-20 rebuild, per
 * the user's judgement on the detached-editor design):
 * every row is edited where it sits - there is no editor
 * strip and no "next value" button anywhere.
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_settings.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 40
#define MAX_HITS 64
/* ------------------------------------------------------------------ */
/* Category / row icons                                                */
/*                                                                     */
/* Icons come from the established vector sets (Material Symbols       */
/* for the Android form, Fluent for the WinUI form), rasterised        */
/* offline with anti-aliasing - see assets/icons/MANIFEST.md and       */
/* apps/common/bdicon.h. Code-drawn glyphs are not used (user          */
/* direction 2026-11-21: they read as rough and home-made). The        */
/* Android form keeps the Material 3 Expressive treatment - a          */
/* category-colour disc with the white glyph composited over it;       */
/* the disc itself is a plain filled circle (a background shape,       */
/* rendered with an anti-aliased edge), not a glyph. The WinUI         */
/* form is the bare glyph in the theme text colour (the Fluent         */
/* line-icon treatment). Returned buffers are owned by the caller      */
/* (copies of the bdicon cache), as before.                            */
/* ------------------------------------------------------------------ */

enum {
    GLYPH_WIFI, GLYPH_DISPLAY, GLYPH_SOUND, GLYPH_MUTE,
    GLYPH_POWER, GLYPH_STORAGE, GLYPH_PERSONAL, GLYPH_MASCOT,
    GLYPH_KEYBOARD, GLYPH_TIME, GLYPH_ABOUT, GLYPH_WALLPAPER,
    GLYPH_PANEL, GLYPH_MODE, GLYPH_SLEEP, GLYPH_COUNT
};

static const char *glyph_icon_name(int glyph, bool winui)
{
    switch (glyph) {
    case GLYPH_WIFI:     return winui ? "wifi_1" : "wifi";
    case GLYPH_DISPLAY:  return winui ? "weather_sunny"
                                      : "brightness_high";
    case GLYPH_SOUND:    return winui ? "speaker_2" : "volume_up";
    case GLYPH_MUTE:     return winui ? "speaker_mute"
                                      : "volume_off";
    case GLYPH_POWER:    return winui ? "battery_6" : "battery_full";
    case GLYPH_STORAGE:  return "storage";
    case GLYPH_PERSONAL: return "person";
    case GLYPH_MASCOT:   return "star";
    case GLYPH_KEYBOARD: return "keyboard";
    case GLYPH_TIME:     return winui ? "clock" : "schedule";
    case GLYPH_ABOUT:    return "info";
    case GLYPH_WALLPAPER: return "image";
    case GLYPH_PANEL:    return winui ? "desktop"
                                      : "dock_to_bottom";
    case GLYPH_MODE:     return winui ? "desktop" : "devices";
    case GLYPH_SLEEP:    return winui ? "weather_moon" : "bedtime";
    default:             return "info";
    }
}

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
           (uint32_t)b;
}

/* composite a tinted glyph (straight alpha) over the disc
 * buffer, centred; standard "over" blending */
static void composite_glyph(uint32_t *dst, int S,
                            const uint32_t *g, int box)
{
    int off = (S - box) / 2;
    for (int y = 0; y < box; y++)
        for (int x = 0; x < box; x++) {
            uint32_t gp = g[y * box + x];
            double ga = (double)(gp >> 24) / 255.0;
            if (ga <= 0.0) continue;
            uint32_t *dp = &dst[(y + off) * S + (x + off)];
            double da = (double)(*dp >> 24) / 255.0;
            double oa = ga + da * (1.0 - ga);
            if (oa <= 0.0) { *dp = 0; continue; }
            uint32_t r = (uint32_t)(
                (((gp >> 16) & 0xFF) * ga +
                 ((*dp >> 16) & 0xFF) * da * (1.0 - ga)) / oa);
            uint32_t gg = (uint32_t)(
                (((gp >> 8) & 0xFF) * ga +
                 ((*dp >> 8) & 0xFF) * da * (1.0 - ga)) / oa);
            uint32_t b = (uint32_t)(
                ((gp & 0xFF) * ga +
                 (*dp & 0xFF) * da * (1.0 - ga)) / oa);
            *dp = ((uint32_t)(oa * 255.0) << 24) | (r << 16) |
                  (gg << 8) | b;
        }
}

/* Android form: colour disc + white glyph, 44x44 */
static uint32_t *make_swatch(int glyph, uint32_t disc_col)
{
    const int S = 44;
    uint32_t *px = calloc((size_t)S * S, sizeof(uint32_t));
    if (!px) return NULL;
    double c = (S - 1) / 2.0, rad = S / 2.0 - 0.5;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            double dx = x - c, dy = y - c;
            double cov = rad - sqrt(dx * dx + dy * dy) + 0.5;
            if (cov <= 0.0) continue;
            if (cov > 1.0) cov = 1.0;
            px[y * S + x] = ((uint32_t)(cov * 255.0) << 24) |
                            (disc_col & 0x00FFFFFFu);
        }
    char spec[96];
    snprintf(spec, sizeof(spec), "material/%s",
             glyph_icon_name(glyph, false));
    const uint32_t *g = bd_icon(spec, 26, 0xFFFFFFu);
    if (g) composite_glyph(px, S, g, 26);
    return px;
}

/* WinUI form: bare glyph in `col`, 28x28 */
static uint32_t *make_mono(int glyph, uint32_t col)
{
    const int S = 28;
    char spec[96];
    snprintf(spec, sizeof(spec), "fluent/%s",
             glyph_icon_name(glyph, true));
    const uint32_t *g = bd_icon(spec, S, col & 0x00FFFFFFu);
    if (!g) return NULL;
    uint32_t *px = malloc((size_t)S * S * sizeof(uint32_t));
    if (px) memcpy(px, g, (size_t)S * S * sizeof(uint32_t));
    return px;
}

static int page_glyph(int page)
{
    switch (page) {
    case SET_PAGE_WIFI:     return GLYPH_WIFI;
    case SET_PAGE_DISPLAY:  return GLYPH_DISPLAY;
    case SET_PAGE_SOUND:    return GLYPH_SOUND;
    case SET_PAGE_POWER:    return GLYPH_POWER;
    case SET_PAGE_STORAGE:  return GLYPH_STORAGE;
    case SET_PAGE_PERSONAL: return GLYPH_PERSONAL;
    case SET_PAGE_MASCOT:   return GLYPH_MASCOT;
    case SET_PAGE_KEYBOARD: return GLYPH_KEYBOARD;
    case SET_PAGE_DATETIME: return GLYPH_TIME;
    case SET_PAGE_ABOUT:    return GLYPH_ABOUT;
    default:                return GLYPH_ABOUT;
    }
}

static uint32_t page_disc_color(int page)
{
    switch (page) {
    case SET_PAGE_WIFI:     return argb(30, 136, 229);
    case SET_PAGE_DISPLAY:  return argb(251, 140, 0);
    case SET_PAGE_SOUND:    return argb(142, 36, 170);
    case SET_PAGE_POWER:    return argb(67, 160, 71);
    case SET_PAGE_STORAGE:  return argb(0, 137, 123);
    case SET_PAGE_PERSONAL: return argb(94, 53, 177);
    case SET_PAGE_MASCOT:   return argb(249, 168, 37);
    case SET_PAGE_KEYBOARD: return argb(84, 110, 122);
    case SET_PAGE_DATETIME: return argb(57, 73, 171);
    case SET_PAGE_ABOUT:    return argb(0, 131, 143);
    default:                return argb(96, 96, 96);
    }
}

/* WinUI card header icon per row id (falls back to the
 * page glyph) */
static int row_glyph(int page, const char *id)
{
    if (strcmp(id, "brightness") == 0) return GLYPH_DISPLAY;
    if (strcmp(id, "volume") == 0) return GLYPH_SOUND;
    if (strcmp(id, "mute") == 0) return GLYPH_MUTE;
    if (strcmp(id, "battery") == 0) return GLYPH_POWER;
    if (strcmp(id, "sleep") == 0) return GLYPH_SLEEP;
    if (strcmp(id, "ui_mode") == 0) return GLYPH_MODE;
    if (strcmp(id, "wallpaper") == 0) return GLYPH_WALLPAPER;
    if (strcmp(id, "panel") == 0) return GLYPH_PANEL;
    return page_glyph(page);
}

/* ------------------------------------------------------------------ */
/* App state                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    settings_app *app;
    int idx;
} row_cb;

typedef struct {
    settings_app *app;
    int idx;
    int choice;
} chip_cb;

typedef struct {
    tk_widget *box;      /* row container (in-card box / card) */
    tk_widget *value;    /* value caption (NULL when none) */
    tk_widget *slider;   /* SR_SLIDER */
    tk_widget *sw;       /* SR_TOGGLE */
    tk_widget *btn;      /* SR_ACTION */
    tk_widget *control;  /* SR_CYCLE: segmented / combobox */
} row_ui;

typedef struct {
    int page;
    char id[32];
    char label[96];
    char page_name[32];
} search_hit;

struct settings_app {
    tk_root *root;
    tk_widget *top;
    set_model *model;        /* owned */
    bd_backend *be;          /* borrowed */
    bd_wifi *wifi;           /* borrowed */
    sy_sys *sys;             /* borrowed */
    bool winui;
    bool compact;   /* short window: compact theme derivation */
    tk_theme compact_theme;   /* per-app storage for the
                                 derivation (a file-static copy
                                 broke mode switches within one
                                 process - ASan sweep finding) */

    /* chrome (built once; visibility switches views) */
    tk_widget *search;
    tk_widget *hdr_home;     /* Android: heading + search */
    tk_widget *hdr_page;     /* Android: app bar with back */
    tk_widget *appbar;
    tk_widget *cat_list;     /* Android: categories / results */
    tk_widget *nav;          /* WinUI: drawer */
    tk_widget *crumb;        /* WinUI: breadcrumb label */
    tk_widget *page_title;   /* WinUI: page title label */
    tk_widget *rows_scroll;
    tk_widget *rows_box;     /* scroll child, rebuilt per page */
    tk_widget *results_list; /* WinUI: search results list */
    tk_widget *status;
    tk_widget *psk;          /* live only while an AP row is
                              * expanded; owned by the row tree */

    int page;                /* SETTINGS_PAGE_HOME (-1) on the
                              * Android homepage */
    bool searching;
    char query[96];
    int win_w, win_h;

    set_row rows[MAX_ROWS];
    size_t n_rows;
    row_ui uis[MAX_ROWS];
    row_cb cbs[MAX_ROWS];
    chip_cb chip_cbs[MAX_ROWS][SET_MAX_CHOICES];
    int sel_idx;
    char sel_id[32];
    char expanded_id[32];    /* AP row with the PSK form open */
    bool building;           /* guards programmatic control sets */

    search_hit hits[MAX_HITS];
    size_t n_hits;
    int hit_sel;

    /* owned icon buffers (borrowed by list/drawer widgets) */
    uint32_t *cat_icons[SET_PAGE_COUNT];
    const uint32_t *cat_icon_ptrs[SET_PAGE_COUNT];
    uint32_t *nav_icons[SET_PAGE_COUNT];
    uint32_t *row_icons[MAX_ROWS];
};

/* navigation order per mode (DESIGN section 5.2 / 5.3) */
static const int android_order[SET_PAGE_COUNT] = {
    SET_PAGE_WIFI, SET_PAGE_DISPLAY, SET_PAGE_SOUND,
    SET_PAGE_POWER, SET_PAGE_STORAGE, SET_PAGE_PERSONAL,
    SET_PAGE_MASCOT, SET_PAGE_KEYBOARD, SET_PAGE_DATETIME,
    SET_PAGE_ABOUT
};
static const int winui_order[SET_PAGE_COUNT] = {
    SET_PAGE_DISPLAY, SET_PAGE_SOUND, SET_PAGE_POWER,
    SET_PAGE_STORAGE, SET_PAGE_WIFI, SET_PAGE_PERSONAL,
    SET_PAGE_MASCOT, SET_PAGE_KEYBOARD, SET_PAGE_DATETIME,
    SET_PAGE_ABOUT
};

static void set_text(tk_widget *w, const char *s)
{
    tk_label_set_text(w, s);
    tk_widget_invalidate(w);
}

static tk_widget *lbl(tk_widget *parent, const char *s)
{
    tk_widget *l = tk_label_new(s);
    tk_add_child(parent, l);
    return l;
}
static tk_widget *hbox(tk_widget *parent, int spacing)
{
    tk_widget *b = tk_box_new(TK_DIR_HORIZONTAL, spacing, 0);
    tk_add_child(parent, b);
    return b;
}
static tk_widget *vbox(tk_widget *parent, int spacing)
{
    tk_widget *b = tk_box_new(TK_DIR_VERTICAL, spacing, 0);
    tk_add_child(parent, b);
    return b;
}
static void clear_children(tk_widget *w)
{
    while (tk_child_count(w) > 0) {
        tk_widget *c = tk_child_at(w, tk_child_count(w) - 1);
        tk_remove_child(w, c);
        tk_widget_free(c);
    }
}


/* ---------------- homepage summaries ---------------- */

/* the one-line live summary a category shows on the Android
 * homepage (the AOSP controllers' dynamic summaries) */
static void page_summary(settings_app *app, int page,
                         char *out, size_t cap)
{
    set_row rows[MAX_ROWS];
    size_t n = set_model_rows(app->model, page, rows, MAX_ROWS);
    const set_row *r = NULL;
    out[0] = '\0';
    switch (page) {
    case SET_PAGE_WIFI: {
        const set_row *on = NULL, *ssid = NULL;
        for (size_t i = 0; i < n; i++) {
            if (strcmp(rows[i].id, "wifi_on") == 0) on = &rows[i];
            if (strcmp(rows[i].id, "ssid") == 0) ssid = &rows[i];
        }
        if (on && !on->toggle_on) snprintf(out, cap, "オフ");
        else if (ssid) snprintf(out, cap, "オン・%s", ssid->value);
        else snprintf(out, cap, "オン");
        break;
    }
    case SET_PAGE_DISPLAY:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "brightness") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "輝度 %s", r->value);
        break;
    case SET_PAGE_SOUND: {
        const set_row *vol = NULL, *mute = NULL;
        for (size_t i = 0; i < n; i++) {
            if (strcmp(rows[i].id, "volume") == 0) vol = &rows[i];
            if (strcmp(rows[i].id, "mute") == 0) mute = &rows[i];
        }
        if (mute && mute->toggle_on)
            snprintf(out, cap, "ミュート中");
        else if (vol) snprintf(out, cap, "音量 %s", vol->value);
        break;
    }
    case SET_PAGE_POWER:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "battery") == 0) r = &rows[i];
        if (r && r->value[0]) snprintf(out, cap, "電池 %s",
                                       r->value);
        else snprintf(out, cap, "電池の状態");
        break;
    case SET_PAGE_STORAGE:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "fs0") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "%s", r->value);
        break;
    case SET_PAGE_PERSONAL:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "ui_mode") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "表示モード: %s", r->value);
        break;
    case SET_PAGE_MASCOT:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "enabled") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "%s", r->toggle_on ? "オン"
                                                     : "オフ");
        break;
    case SET_PAGE_KEYBOARD:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "preset") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "リピート: %s", r->value);
        break;
    case SET_PAGE_DATETIME:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "local") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "%s", r->value);
        break;
    case SET_PAGE_ABOUT:
        for (size_t i = 0; i < n; i++)
            if (strcmp(rows[i].id, "os") == 0) r = &rows[i];
        if (r) snprintf(out, cap, "%s", r->value);
        break;
    default:
        break;
    }
}

/* ---------------- search ---------------- */

/* Android Settings search: every row of every page is
 * indexed; a hit shows the item name and its page name */
static void rebuild_hits(settings_app *app)
{
    app->n_hits = 0;
    app->hit_sel = -1;
    if (!app->query[0]) return;
    const int *order = app->winui ? winui_order : android_order;
    for (int oi = 0; oi < SET_PAGE_COUNT; oi++) {
        int page = order[oi];
        set_row rows[MAX_ROWS];
        size_t n = set_model_rows(app->model, page, rows,
                                  MAX_ROWS);
        for (size_t i = 0; i < n && app->n_hits < MAX_HITS;
             i++) {
            if (!strstr(rows[i].label, app->query) &&
                !strstr(rows[i].value, app->query) &&
                !strstr(set_page_name(page), app->query))
                continue;
            search_hit *h = &app->hits[app->n_hits++];
            h->page = page;
            snprintf(h->id, sizeof(h->id), "%s", rows[i].id);
            snprintf(h->label, sizeof(h->label), "%s",
                     rows[i].label);
            snprintf(h->page_name, sizeof(h->page_name), "%s",
                     set_page_name(page));
        }
    }
}

static void fill_results_list(settings_app *app, tk_widget *list)
{
    const char *items[MAX_HITS];
    const char *subs[MAX_HITS];
    for (size_t i = 0; i < app->n_hits; i++) {
        items[i] = app->hits[i].label;
        subs[i] = app->hits[i].page_name;
    }
    tk_listview_set_items(list, items, app->n_hits);
    tk_listview_set_subitems(list, subs, app->n_hits);
    tk_listview_set_item_icons(list, NULL, 0, 0, 0);
    tk_widget_invalidate(list);
}

/* ---------------- grouping ---------------- */

/* per-page section of a row (DESIGN section 5.1); pages are
 * flat in the model, the sections are a view concern - the
 * AOSP PreferenceCategory / WinUI section-header grouping */
static int row_group(int page, const set_row *r, int idx)
{
    switch (page) {
    case SET_PAGE_WIFI:
        return (strcmp(r->id, "wifi_on") == 0 ||
                strcmp(r->id, "state") == 0 ||
                strcmp(r->id, "ssid") == 0 ||
                strcmp(r->id, "ip") == 0) ? 0 : 1;
    case SET_PAGE_POWER:
        return strcmp(r->id, "battery") == 0 ? 0 : 1;
    case SET_PAGE_KEYBOARD:
        return strcmp(r->id, "longpress") == 0 ? 1 : 0;
    case SET_PAGE_PERSONAL:
        return strcmp(r->id, "ui_mode") == 0 ? 0 : 1;
    case SET_PAGE_ABOUT:
        if (strncmp(r->id, "fs", 2) == 0) return 2;
        return (strcmp(r->id, "mem") == 0 ||
                strcmp(r->id, "swap") == 0) ? 1 : 0;
    default:
        (void)idx;
        return 0;
    }
}

static const char *group_title(int page, int group)
{
    switch (page) {
    case SET_PAGE_WIFI:
        return group == 0 ? "接続" : "ネットワーク";
    case SET_PAGE_DISPLAY:  return "明るさ";
    case SET_PAGE_SOUND:    return "音量";
    case SET_PAGE_POWER:    return group == 0 ? "電池" : "スリープ";
    case SET_PAGE_STORAGE:  return "容量";
    case SET_PAGE_PERSONAL: return group == 0 ? "表示" : "デスクトップ";
    case SET_PAGE_MASCOT:   return "表示";
    case SET_PAGE_KEYBOARD: return group == 0 ? "キーリピート" : "長押し";
    case SET_PAGE_DATETIME: return "時刻";
    case SET_PAGE_ABOUT:
        return group == 0 ? "端末" : group == 1 ? "メモリ"
                                                : "ストレージ";
    default:                return "";
    }
}

static int page_group_count(int page)
{
    switch (page) {
    case SET_PAGE_WIFI:
    case SET_PAGE_POWER:
    case SET_PAGE_KEYBOARD:
    case SET_PAGE_PERSONAL:
        return 2;
    case SET_PAGE_ABOUT:
        return 3;
    default:
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Rows                                                                */
/* ------------------------------------------------------------------ */

static void rebuild_rows(settings_app *app);
static void show_page(settings_app *app, int page);
static void show_home(settings_app *app);
static void jump_to_hit(settings_app *app, int idx);

static const set_row *selected_row(settings_app *app)
{
    for (size_t i = 0; i < app->n_rows; i++)
        if (strcmp(app->rows[i].id, app->sel_id) == 0)
            return &app->rows[i];
    return NULL;
}

static void apply_selection_faces(settings_app *app)
{
    if (app->winui) return; /* cards are discrete; no row tint */
    const tk_theme *t = tk_theme_default();
    for (size_t i = 0; i < app->n_rows; i++) {
        if (!app->uis[i].box) continue;
        if ((int)i == app->sel_idx) {
            tk_widget_set_backdrop(app->uis[i].box, false,
                                   t->radius);
            tk_widget_set_backdrop_face(app->uis[i].box,
                                        &t->selection);
        } else {
            tk_widget_clear_backdrop(app->uis[i].box);
        }
        tk_widget_invalidate(app->uis[i].box);
    }
}

void settings_app_select_row(settings_app *app, int idx)
{
    if (!app) return;
    if (app->searching) {
        tk_widget *list = app->winui ? app->results_list
                                     : app->cat_list;
        if (app->n_hits == 0) return;
        if (idx < 0) idx = 0;
        if ((size_t)idx >= app->n_hits)
            idx = (int)app->n_hits - 1;
        app->hit_sel = idx;
        app->building = true;
        tk_listview_select(list, idx);
        app->building = false;
        return;
    }
    if (app->page == SETTINGS_PAGE_HOME) {
        if (idx < 0) idx = 0;
        if (idx >= SET_PAGE_COUNT) idx = SET_PAGE_COUNT - 1;
        app->building = true;
        tk_listview_select(app->cat_list, idx);
        app->building = false;
        return;
    }
    if (app->n_rows == 0) return;
    if (idx < 0) idx = 0;
    if ((size_t)idx >= app->n_rows) idx = (int)app->n_rows - 1;
    app->sel_idx = idx;
    snprintf(app->sel_id, sizeof(app->sel_id), "%s",
             app->rows[idx].id);
    apply_selection_faces(app);
}

/* refresh one row's value caption from the model (the slider
 * drag path: the row must NOT be rebuilt while its own slider
 * is being dragged) */
static void refresh_row_value(settings_app *app, int idx)
{
    set_row fresh[MAX_ROWS];
    size_t n = set_model_rows(app->model, app->page, fresh,
                              MAX_ROWS);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(fresh[i].id, app->rows[idx].id) != 0) continue;
        app->rows[idx] = fresh[i];
        if (app->uis[idx].value)
            set_text(app->uis[idx].value, fresh[i].value);
        return;
    }
}

static void on_slider(tk_widget *w, void *ud)
{
    row_cb *cb = ud;
    settings_app *app = cb->app;
    if (app->building) return;
    const set_row *r = &app->rows[cb->idx];
    if (r->kind != SR_SLIDER) return;
    app->sel_idx = cb->idx;
    snprintf(app->sel_id, sizeof(app->sel_id), "%s", r->id);
    apply_selection_faces(app);
    int value = r->slider_min +
        (int)(tk_slider_value(w) * (r->slider_max - r->slider_min) +
              0.5f);
    if (set_model_set_slider(app->model, app->page, r->id,
                             value) == 0)
        refresh_row_value(app, cb->idx);
}

static void choice_applied(settings_app *app, int idx)
{
    const set_row *r = &app->rows[idx];
    app->sel_idx = idx;
    snprintf(app->sel_id, sizeof(app->sel_id), "%s", r->id);
    rebuild_rows(app);
    set_text(app->status, "変更しました");
}

static void on_segmented(tk_widget *w, void *ud)
{
    row_cb *cb = ud;
    settings_app *app = cb->app;
    if (app->building) return;
    const set_row *r = &app->rows[cb->idx];
    if (r->kind != SR_CYCLE) return;
    int sel = tk_segmented_selected(w);
    if (sel < 0) return;
    if (set_model_set_choice(app->model, app->page, r->id,
                             sel) == 0)
        choice_applied(app, cb->idx);
}

static void on_combobox(tk_widget *w, void *ud)
{
    row_cb *cb = ud;
    settings_app *app = cb->app;
    if (app->building) return;
    const set_row *r = &app->rows[cb->idx];
    if (r->kind != SR_CYCLE) return;
    int sel = tk_combobox_selected(w);
    if (sel < 0) return;
    if (set_model_set_choice(app->model, app->page, r->id,
                             sel) == 0)
        choice_applied(app, cb->idx);
}

static void on_switch(tk_widget *w, void *ud)
{
    row_cb *cb = ud;
    settings_app *app = cb->app;
    if (app->building) return;
    const set_row *r = &app->rows[cb->idx];
    if (r->kind != SR_TOGGLE) return;
    bool on = tk_switch_on(w);
    if (set_model_set_toggle(app->model, app->page, r->id,
                             on) == 0) {
        app->sel_idx = cb->idx;
        snprintf(app->sel_id, sizeof(app->sel_id), "%s", r->id);
        rebuild_rows(app);
        set_text(app->status, "変更しました");
    } else {
        app->building = true;
        tk_switch_set_on(w, !on); /* revert: model refused */
        app->building = false;
    }
}

static bool ap_secured(const set_row *r)
{
    /* the AP row's value carries "<dbm> dBm <flags>"; the
     * flags are the supplicant's bracket list */
    return strstr(r->value, "PSK") || strstr(r->value, "WEP") ||
           strstr(r->value, "SAE") || strstr(r->value, "EAP");
}

static void run_action(settings_app *app, int idx)
{
    const set_row *r = &app->rows[idx];
    app->sel_idx = idx;
    snprintf(app->sel_id, sizeof(app->sel_id), "%s", r->id);
    /* a secured network row first expands its PSK form in
     * place; connecting happens from the form (or directly
     * for open networks and when the form is already open) */
    if (strncmp(r->id, "ap", 2) == 0 && ap_secured(r) &&
        strcmp(app->expanded_id, r->id) != 0) {
        snprintf(app->expanded_id, sizeof(app->expanded_id),
                 "%s", r->id);
        rebuild_rows(app);
        return;
    }
    char psk[64] = "";
    if (app->psk) tk_textfield_text(app->psk, psk, sizeof(psk));
    set_model_set_psk(app->model, psk);
    int rc = set_model_action(app->model, app->page, r->id,
                              r->label);
    app->expanded_id[0] = 0;
    rebuild_rows(app);
    set_text(app->status, rc == 0 ? "実行しました" : "失敗しました");
}

static void on_action_btn(tk_widget *w, void *ud)
{
    (void)w;
    row_cb *cb = ud;
    run_action(cb->app, cb->idx);
}

/* Row activation (Enter / double-tap path, public for tests):
 * the value changes in place on the row itself. */
void settings_app_activate_row(settings_app *app)
{
    if (!app) return;
    if (app->searching) {
        tk_widget *list = app->winui ? app->results_list
                                     : app->cat_list;
        int idx = tk_listview_selected(list);
        if (idx >= 0) jump_to_hit(app, idx);
        return;
    }
    if (app->page == SETTINGS_PAGE_HOME) {
        int idx = tk_listview_selected(app->cat_list);
        if (idx >= 0 && idx < SET_PAGE_COUNT)
            show_page(app, android_order[idx]);
        return;
    }
    const set_row *r = selected_row(app);
    if (!r) return;
    int idx = (int)(r - app->rows);
    if (r->kind == SR_CYCLE) {
        if (set_model_cycle(app->model, app->page, r->id) == 0) {
            rebuild_rows(app);
            set_text(app->status, "変更しました");
        }
    } else if (r->kind == SR_TOGGLE) {
        if (set_model_set_toggle(app->model, app->page, r->id,
                                 !r->toggle_on) == 0) {
            rebuild_rows(app);
            set_text(app->status, "変更しました");
        }
    } else if (r->kind == SR_ACTION) {
        run_action(app, idx);
    }
}

static void build_row(settings_app *app, tk_widget *parent,
                      int idx)
{
    const tk_theme *t = tk_theme_default();
    const set_row *r = &app->rows[idx];
    row_ui *u = &app->uis[idx];
    memset(u, 0, sizeof(*u));
    app->cbs[idx].app = app;
    app->cbs[idx].idx = idx;

    /* WinUI: the row IS a card (SettingsCard form). Android:
     * a plain box inside the group's card. */
    tk_widget *row;
    if (app->winui) {
        row = tk_card_new(TK_CARD_ELEVATED);
        /* cards default to non-stretch cross alignment
         * (unlike tk_box_new): the header and the slider
         * must span the card */
        tk_box_set_align(row, TK_ALIGN_STRETCH);
        tk_add_child(parent, row);
    } else {
        row = vbox(parent, 8);
    }
    u->box = row;

    /* header: [icon] texts left, the row's own control right */
    tk_widget *hdr = hbox(row, app->winui ? 0 : 8);
    tk_widget_set_min_size(hdr, 0, t->control_h);
    if (app->winui) {
        free(app->row_icons[idx]);
        app->row_icons[idx] =
            make_mono(row_glyph(app->page, r->id),
                      argb(t->text.r, t->text.g, t->text.b));
        tk_widget *iv = tk_imageview_new();
        if (app->row_icons[idx])
            tk_imageview_set_pixels(iv, app->row_icons[idx],
                                    28, 28);
        tk_widget_set_min_size(iv, 48, 0); /* icon + its margin
                                            * (SettingsCard
                                            * HeaderIconMargin) */
        tk_add_child(hdr, iv);
    }
    tk_widget *texts = vbox(hdr, 0);
    tk_set_flex(texts, 1);
    lbl(texts, r->label);
    /* WinUI info rows put the value at the right edge (the
     * About-page form); note rows keep it as a description
     * under the title in both modes */
    bool value_right = app->winui && r->kind == SR_INFO &&
                       strcmp(r->id, "note") != 0;
    if (r->value[0] && !value_right) {
        u->value = lbl(texts, r->value);
        tk_widget_set_text_style(u->value, TK_TEXT_CAPTION);
        tk_widget_set_text_color(u->value, t->text_dim);
    }

    switch (r->kind) {
    case SR_TOGGLE: {
        u->sw = tk_switch_new("", r->toggle_on);
        tk_switch_set_on_change(u->sw, on_switch, &app->cbs[idx]);
        tk_add_child(hdr, u->sw);
        break;
    }
    case SR_ACTION: {
        const char *bl = strncmp(r->id, "ap", 2) == 0 ? "接続"
            : strcmp(r->id, "scan") == 0 ? "スキャン"
            : strcmp(r->id, "disconnect") == 0 ? "切断" : "実行";
        u->btn = tk_button_new(bl);
        tk_button_set_on_click(u->btn, on_action_btn,
                               &app->cbs[idx]);
        tk_add_child(hdr, u->btn);
        break;
    }
    case SR_CYCLE:
        if (app->winui) {
            const char *items[SET_MAX_CHOICES];
            for (int i = 0; i < r->n_choices; i++)
                items[i] = r->choices[i];
            u->control = tk_combobox_new();
            tk_combobox_set_items(u->control, items,
                                  (size_t)r->n_choices);
            tk_widget_set_min_size(u->control, 168, 0);
            app->building = true;
            if (r->choice_cur >= 0)
                tk_combobox_select(u->control, r->choice_cur);
            app->building = false;
            tk_combobox_set_on_change(u->control, on_combobox,
                                      &app->cbs[idx]);
            tk_add_child(hdr, u->control);
        }
        break;
    case SR_INFO:
        if (value_right) {
            u->value = lbl(hdr, r->value);
            tk_widget_set_text_style(u->value, TK_TEXT_CAPTION);
            tk_widget_set_text_color(u->value, t->text_dim);
        }
        break;
    default:
        break;
    }

    if (r->kind == SR_SLIDER) {
        u->slider = tk_slider_new(0);
        tk_slider_set_on_change(u->slider, on_slider,
                                &app->cbs[idx]);
        tk_add_child(row, u->slider);
        app->building = true;
        float v = r->slider_max > r->slider_min
            ? (float)(r->slider_cur - r->slider_min) /
              (float)(r->slider_max - r->slider_min)
            : 0;
        tk_slider_set_value(u->slider, v);
        app->building = false;
    }
    if (r->kind == SR_CYCLE && !app->winui) {
        /* the M3 segmented button under the label: every
         * choice in place (the design's documented
         * equivalent of the ListPreference dialog) */
        u->control = tk_segmented_new(false);
        for (int i = 0; i < r->n_choices; i++)
            tk_segmented_add(u->control, r->choices[i]);
        app->building = true;
        if (r->choice_cur >= 0)
            tk_segmented_select(u->control, r->choice_cur);
        app->building = false;
        tk_segmented_set_on_change(u->control, on_segmented,
                                   &app->cbs[idx]);
        tk_add_child(row, u->control);
    }
    /* secured-network expansion: PSK form directly under the
     * tapped AP row */
    if (strncmp(r->id, "ap", 2) == 0 &&
        strcmp(app->expanded_id, r->id) == 0) {
        tk_widget *form = hbox(row, 8);
        app->psk = tk_textfield_new("");
        tk_textfield_set_placeholder(app->psk, "パスワード");
        tk_textfield_set_password(app->psk, true);
        tk_add_child(form, app->psk);
        tk_set_flex(app->psk, 1);
        tk_widget *go = tk_button_new("接続");
        tk_button_set_on_click(go, on_action_btn, &app->cbs[idx]);
        tk_add_child(form, go);
    }
}

static void rebuild_rows(settings_app *app)
{
    if (app->page < 0) return;
    app->n_rows = set_model_rows(app->model, app->page,
                                 app->rows, MAX_ROWS);
    app->psk = NULL;
    clear_children(app->rows_box);
    for (int i = 0; i < MAX_ROWS; i++) {
        free(app->row_icons[i]);
        app->row_icons[i] = NULL;
    }
    memset(app->uis, 0, sizeof(app->uis));
    /* selection follows the row id across rebuilds */
    app->sel_idx = -1;
    for (size_t i = 0; i < app->n_rows; i++)
        if (app->sel_id[0] &&
            strcmp(app->rows[i].id, app->sel_id) == 0)
            app->sel_idx = (int)i;

    int ngroups = page_group_count(app->page);
    for (int g = 0; g < ngroups; g++) {
        /* count rows in this group first */
        bool any = false;
        for (size_t i = 0; i < app->n_rows; i++)
            if (row_group(app->page, &app->rows[i], (int)i) == g)
                any = true;
        if (!any) continue;
        tk_widget *wrap = vbox(app->rows_box, app->winui ? 4 : 4);
        if (ngroups > 1 || app->winui) {
            tk_widget *h = lbl(wrap, group_title(app->page, g));
            if (app->winui) {
                tk_widget_set_text_style(h, TK_TEXT_HEADING);
            } else {
                const tk_theme *t = tk_theme_default();
                tk_widget_set_text_style(h, TK_TEXT_CAPTION);
                tk_widget_set_text_color(h, t->accent);
            }
        }
        tk_widget *card = NULL;
        if (!app->winui) {
            card = tk_card_new(TK_CARD_ELEVATED);
            tk_box_set_align(card, TK_ALIGN_STRETCH);
            tk_add_child(wrap, card);
        }
        bool first = true;
        for (size_t i = 0; i < app->n_rows; i++) {
            if (row_group(app->page, &app->rows[i], (int)i) != g)
                continue;
            if (app->winui) {
                build_row(app, wrap, (int)i);
            } else {
                if (!first)
                    tk_add_child(card, tk_separator_new(false));
                build_row(app, card, (int)i);
            }
            first = false;
        }
    }
    if (app->sel_idx < 0 && app->n_rows > 0) {
        app->sel_idx = 0;
        snprintf(app->sel_id, sizeof(app->sel_id), "%s",
                 app->rows[0].id);
    }
    apply_selection_faces(app);
    tk_root_layout(app->root, app->win_w, app->win_h);
    tk_widget_invalidate(app->top);
}

/* ------------------------------------------------------------------ */
/* Navigation / chrome                                                 */
/* ------------------------------------------------------------------ */

static void fill_cat_list(settings_app *app)
{
    if (app->searching) {
        fill_results_list(app, app->cat_list);
        return;
    }
    const char *items[SET_PAGE_COUNT];
    const char *subs[SET_PAGE_COUNT];
    char sumbuf[SET_PAGE_COUNT][128];
    for (int i = 0; i < SET_PAGE_COUNT; i++) {
        int page = android_order[i];
        items[i] = set_page_name(page);
        page_summary(app, page, sumbuf[i], sizeof(sumbuf[i]));
        subs[i] = sumbuf[i];
        app->cat_icon_ptrs[i] = app->cat_icons[page];
    }
    tk_listview_set_items(app->cat_list, items, SET_PAGE_COUNT);
    tk_listview_set_subitems(app->cat_list, subs, SET_PAGE_COUNT);
    tk_listview_set_item_icons(app->cat_list, app->cat_icon_ptrs,
                               44, 44, SET_PAGE_COUNT);
    tk_widget_invalidate(app->cat_list);
}

static void update_chrome(settings_app *app)
{
    if (app->winui) {
        char crumb[160];
        if (app->searching) {
            snprintf(crumb, sizeof(crumb), "設定 > 検索");
            set_text(app->crumb, crumb);
            set_text(app->page_title, "検索結果");
        } else {
            snprintf(crumb, sizeof(crumb), "設定 > %s",
                     set_page_name(app->page));
            set_text(app->crumb, crumb);
            set_text(app->page_title, set_page_name(app->page));
        }
        app->building = true;
        tk_drawer_select(app->nav, app->page);
        app->building = false;
        tk_set_visible(app->rows_scroll, !app->searching);
        tk_set_visible(app->results_list, app->searching);
    } else {
        bool home = app->page == SETTINGS_PAGE_HOME;
        tk_set_visible(app->hdr_home, home);
        tk_set_visible(app->hdr_page, !home);
        tk_set_visible(app->cat_list, home);
        tk_set_visible(app->rows_scroll, !home);
        if (!home)
            tk_appbar_set_title(app->appbar,
                                set_page_name(app->page));
    }
    tk_root_layout(app->root, app->win_w, app->win_h);
    tk_widget_invalidate(app->top);
}

static void show_page(settings_app *app, int page)
{
    app->page = page;
    app->sel_id[0] = 0;
    app->sel_idx = -1;
    app->expanded_id[0] = 0;
    if (app->searching) {
        app->searching = false;
        app->query[0] = 0;
        app->building = true;
        tk_searchbar_set_text(app->search, "");
        app->building = false;
    }
    rebuild_rows(app);
    update_chrome(app);
}

static void show_home(settings_app *app)
{
    app->page = SETTINGS_PAGE_HOME;
    app->sel_id[0] = 0;
    app->sel_idx = -1;
    app->expanded_id[0] = 0;
    fill_cat_list(app);
    update_chrome(app);
}

static void jump_to_hit(settings_app *app, int idx)
{
    if (idx < 0 || (size_t)idx >= app->n_hits) return;
    int page = app->hits[idx].page;
    char id[32];
    snprintf(id, sizeof(id), "%s", app->hits[idx].id);
    show_page(app, page);
    /* select the row the hit names */
    for (size_t i = 0; i < app->n_rows; i++) {
        if (strcmp(app->rows[i].id, id) == 0) {
            app->sel_idx = (int)i;
            snprintf(app->sel_id, sizeof(app->sel_id), "%s", id);
            apply_selection_faces(app);
            break;
        }
    }
}

static void on_search_change(tk_widget *w, void *ud)
{
    (void)w;
    settings_app *app = ud;
    if (app->building) return;
    tk_searchbar_text(app->search, app->query,
                      sizeof(app->query));
    app->searching = app->query[0] != '\0';
    rebuild_hits(app);
    if (app->winui) {
        if (app->searching) fill_results_list(app,
                                              app->results_list);
        update_chrome(app);
    } else {
        fill_cat_list(app);
        update_chrome(app);
    }
}

static void on_cat_select(tk_widget *w, void *ud)
{
    settings_app *app = ud;
    if (app->building) return;
    int idx = tk_listview_selected(w);
    if (idx < 0) return;
    if (app->searching) {
        jump_to_hit(app, idx);
        return;
    }
    if (idx < SET_PAGE_COUNT) show_page(app, android_order[idx]);
}

static void on_results_select(tk_widget *w, void *ud)
{
    settings_app *app = ud;
    if (app->building) return;
    int idx = tk_listview_selected(w);
    if (idx >= 0) jump_to_hit(app, idx);
}

static void on_nav_select(tk_widget *w, void *ud)
{
    settings_app *app = ud;
    if (app->building) return;
    int id = tk_drawer_last_id(w);
    if (id >= 0 && id < SET_PAGE_COUNT && id != app->page)
        show_page(app, id);
}

static void on_back(tk_widget *w, void *ud)
{
    (void)w;
    show_home(ud);
}

/* ---------------- construction ---------------- */


/* Android-mode Back key (TK_KEY_BACK via the WM, bubbled to
 * the top widget): a category page returns to the homepage,
 * mirroring the on-screen back button. The WinUI form has no
 * page stack (a page is always open), so the key is left
 * unconsumed there. */
static bool settings_back_key(void *ud, tk_event *ev)
{
    settings_app *app = ud;
    if (!ev || ev->type != TK_EV_KEY_DOWN || ev->key != TK_KEY_BACK)
        return false;
    if (app->winui) return false;
    if (settings_app_page(app) != SETTINGS_PAGE_HOME) {
        show_home(app);
        return true;
    }
    return false;
}

settings_app *settings_app_new(bd_backend *backend, bd_wifi *wifi,
                               sy_sys *sys, int w, int h)
{
    settings_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->be = backend;
    app->wifi = wifi;
    app->sys = sys;
    app->model = set_model_new(backend, wifi, sys);
    if (!app->model) { free(app); return NULL; }
    app->sel_idx = -1;
    app->win_w = w;
    app->win_h = h;

    /* Compact derivation (DESIGN-layout-contract.md 1.5): in
     * a window shorter than the theme's compact_below token,
     * fixed chrome costs shrink before content does - the
     * category list's two-line rows take the one-line height
     * (row_h2 -> row_h via tk_theme_compact) and the home
     * header tightens - so the home page keeps the four
     * categories the fullscreen build shows. The derivation
     * is computed from the CURRENT default theme on every
     * build (the caller installs the reg theme first); an
     * earlier first-build snapshot froze the mode for the
     * whole process and broke the winui test case. */
    const tk_theme *base_theme = tk_theme_default();
    app->compact = h > 0 && h < tk_theme_compact_below(base_theme);
    if (app->compact) {
        app->compact_theme = tk_theme_compact(base_theme);
        tk_theme_set_default(&app->compact_theme);
    }

    const tk_theme *t = tk_theme_default();
    app->winui = t->style == 2;

    /* icons for the lifetime of the app */
    uint32_t textcol = argb(t->text.r, t->text.g, t->text.b);
    for (int p = 0; p < SET_PAGE_COUNT; p++) {
        app->cat_icons[p] = make_swatch(page_glyph(p),
                                        page_disc_color(p));
        app->nav_icons[p] = make_mono(page_glyph(p), textcol);
    }

    tk_widget *outer;
    if (app->winui) {
        outer = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        app->top = outer;
        tk_widget *pane = tk_box_new(TK_DIR_VERTICAL, 8, 12);
        tk_widget_set_min_size(pane, 256, 0);
        tk_add_child(outer, pane);
        app->search = tk_searchbar_new("設定を検索");
        tk_searchbar_set_on_change(app->search, on_search_change,
                                   app);
        tk_add_child(pane, app->search);
        app->nav = tk_drawer_new("設定");
        for (int i = 0; i < SET_PAGE_COUNT; i++) {
            int page = winui_order[i];
            tk_drawer_add_item(app->nav, set_page_name(page),
                               page, app->nav_icons[page],
                               28, 28);
        }
        tk_drawer_set_on_select(app->nav, on_nav_select, app);
        /* int10 (independent review B): the drawer holds 11
         * destinations (712px desired) and cannot fit a
         * 368px window - the last rows were clipped at the
         * window edge (C5/C10). Wrap it in a scrollview
         * (wheel + drag) like every other long pane. */
        tk_widget *nav_scroll = tk_scrollview_new();
        tk_add_child(pane, nav_scroll);
        tk_set_flex(nav_scroll, 1);
        tk_drawer_attach(nav_scroll, app->nav);

        tk_widget *main_ = tk_box_new(TK_DIR_VERTICAL, 8, 16);
        tk_box_set_pad_x(main_, BD_PAGE_PAD_X);
        tk_add_child(outer, main_);
        tk_set_flex(main_, 1);
        app->crumb = lbl(main_, "");
        tk_widget_set_text_style(app->crumb, TK_TEXT_CAPTION);
        tk_widget_set_text_color(app->crumb, t->text_dim);
        app->page_title = lbl(main_, "");
        tk_widget_set_text_style(app->page_title, TK_TEXT_TITLE);
        app->rows_scroll = tk_scrollview_new();
        tk_add_child(main_, app->rows_scroll);
        tk_set_flex(app->rows_scroll, 1);
        app->results_list = tk_listview_new();
        tk_listview_set_on_select(app->results_list,
                                  on_results_select, app);
        tk_add_child(main_, app->results_list);
        tk_set_flex(app->results_list, 1);
        app->status = lbl(main_, "");
        app->page = SET_PAGE_DISPLAY;
    } else {
        outer = tk_box_new(TK_DIR_VERTICAL, app->compact ? 4 : 8,
                           app->compact ? 8 : 16);
        bd_page_pad_x(outer);
        app->top = outer;
        app->hdr_home = tk_box_new(TK_DIR_VERTICAL,
                                   app->compact ? 4 : 8, 0);
        tk_add_child(outer, app->hdr_home);
        tk_widget *title = lbl(app->hdr_home, "設定");
        tk_widget_set_text_style(title, app->compact ? TK_TEXT_TITLE
                                                     : TK_TEXT_DISPLAY);
        app->search = tk_searchbar_new("設定を検索");
        tk_searchbar_set_on_change(app->search, on_search_change,
                                   app);
        tk_add_child(app->hdr_home, app->search);
        app->appbar = tk_appbar_new("");
        tk_widget *back = tk_button_new("戻る");
        tk_button_set_on_click(back, on_back, app);
        tk_appbar_set_leading(app->appbar, back);
        app->hdr_page = app->appbar;
        tk_add_child(outer, app->appbar);
        app->cat_list = tk_listview_new();
        tk_listview_set_on_select(app->cat_list, on_cat_select,
                                  app);
        tk_add_child(outer, app->cat_list);
        tk_set_flex(app->cat_list, 1);
        app->rows_scroll = tk_scrollview_new();
        tk_add_child(outer, app->rows_scroll);
        tk_set_flex(app->rows_scroll, 1);
        app->status = lbl(outer, "");
        app->page = SETTINGS_PAGE_HOME;
    }
    app->rows_box = tk_box_new(TK_DIR_VERTICAL, 12, 0);
    tk_scrollview_set_child(app->rows_scroll, app->rows_box);

    app->root = tk_root_new(outer);
    tk_widget_set_event_hook(outer, settings_back_key, app);
    tk_root_layout(app->root, w, h);
    if (app->winui) {
        rebuild_rows(app);
        update_chrome(app);
    } else {
        fill_cat_list(app);
        update_chrome(app);
    }
    return app;
}

void settings_app_free(settings_app *app)
{
    if (!app) return;
    for (int p = 0; p < SET_PAGE_COUNT; p++) {
        free(app->cat_icons[p]);
        free(app->nav_icons[p]);
    }
    for (int i = 0; i < MAX_ROWS; i++) free(app->row_icons[i]);
    set_model_free(app->model);
    tk_root_free(app->root);
    tk_widget_free(app->top);
    free(app);
}

int settings_app_page(const settings_app *app) { return app->page; }
void settings_app_set_page(settings_app *app, int page)
{
    if (page == SETTINGS_PAGE_HOME && !app->winui) {
        show_home(app);
        return;
    }
    if (page < 0 || page >= SET_PAGE_COUNT) return;
    show_page(app, page);
}
size_t settings_app_rows(const settings_app *app)
{
    if (app->searching) return app->n_hits;
    if (app->page == SETTINGS_PAGE_HOME) return SET_PAGE_COUNT;
    return app->n_rows;
}
void settings_app_refresh(settings_app *app)
{
    if (app->searching) {
        rebuild_hits(app);
        if (app->winui) fill_results_list(app, app->results_list);
        else fill_cat_list(app);
    } else if (app->page == SETTINGS_PAGE_HOME) {
        fill_cat_list(app);
    } else {
        rebuild_rows(app);
    }
    update_chrome(app);
}

tk_root *settings_app_root(settings_app *app) { return app->root; }
tk_widget *settings_app_page_list(settings_app *app)
{
    return app->winui ? app->nav : app->cat_list;
}
tk_widget *settings_app_search_field(settings_app *app)
{
    return app->search;
}
tk_widget *settings_app_row_list(settings_app *app)
{
    return app->rows_scroll;
}
tk_widget *settings_app_editor_slider(settings_app *app)
{
    if (app->sel_idx < 0 ||
        (size_t)app->sel_idx >= app->n_rows)
        return NULL;
    return app->uis[app->sel_idx].slider;
}
tk_widget *settings_app_psk_field(settings_app *app)
{
    return app->psk;
}
tk_widget *settings_app_status_label(settings_app *app)
{
    return app->status;
}

#ifdef BD_SETTINGS_MAIN
#include <time.h>
int main(void)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    bd_backend *be = bd_backend_sysfs_new();
    bd_wifi *wifi = bd_wifi_new(NULL);
    sy_sys *sys = sy_open("/proc", "/var/log");
    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    /* Display first, built at its real size (see bd_calc.c
     * main note): the shared host probes the shell WM, then
     * fbdev, then nothing (bdapp_run.h). */
    int vw, vh;
    bd_app_display *disp = bd_app_display_open_ex("設定", 0, 0, 480, 272, 800, 600, &vw, &vh);
    settings_app *app = settings_app_new(be, wifi, sys, vw, vh);
    if (!app) { fprintf(stderr, "bd-settings: cannot build UI\n"); return 1; }

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, NULL, NULL, 20);
    } else {
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-settings: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    settings_app_free(app);
    sy_close(sys);
    bd_wifi_free(wifi);
    bd_backend_free(be);
    if (font) tk_font_free(font);
    return 0;
}
#endif
