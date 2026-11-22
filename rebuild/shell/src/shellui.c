/* shellui.c - shell UI, UI-REDESIGN P2 layout.
 *
 * The shell chrome follows the redesign spec (UI-REDESIGN.md 2.3):
 * a 56px bottom panel (48px on narrow screens) with an icon
 * launcher button, an icon task area (pins + running tasks, 3px
 * running indicator, active face), a tray of symbolic icons that
 * opens quick settings, a bold clock that opens the notification
 * sheet, and a settings gear. The launcher and quick settings are
 * floating panels (opaque surface + 1px border + offset shadow,
 * the toolkit backdrop); the launcher is a search field, a pinned
 * icon grid, recents, Japanese category chips and a user/power
 * footer. No text-only buttons remain in the panel, and no
 * developer strings are shown.
 *
 * The model (catalog queries, pins/recents via ln_store, tasks,
 * notifications, backend/wifi plumbing) is unchanged from M1-M3.
 */
#include "shellui.h"
#include "kbdsettings.h"
#include "reg.h"
#include "wm.h" /* bd_win_state values for the band-cover poll */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DESK_MAX 12
#define PTASK_MAX 24
#define CHIP_MAX 12

/* UI-STYLE-v2 (One UI / Material You / ChromeOS direction, see
 * UI-STYLE-v2.md): the style flag on the default theme is the
 * single branch point for the v2 shell geometry/faces. Since
 * the UI v2 rollout, product entry points (braindesk2, the
 * greeter, every app main) select tk_theme_v2 via
 * tk_theme_set_default() before constructing their UI, so the
 * v2 branch is the product default path. */
static bool style_v2(void) { return tk_theme_default()->style == 1; }
/* WinUI / Fluent mode (style 2): the shell follows the same
 * mode key as everything else (thememode.h). Under this theme
 * the shelf takes the Windows 11 taskbar form, the launcher the
 * Start form, and the notification sheet anchors right like the
 * notification centre; the Android-mode geometry is untouched. */
static bool style_winui(void) { return tk_theme_default()->style == 2; }

struct bd_shell {
    int w, h;
    de_catalog *catalog;
    ln_store *store;
    ln_exec_fn exec_fn;
    void *exec_ud;

    tk_root *root;
    tk_widget *top;
    tk_widget *panel;
    tk_widget *band_cover;     /* shell surface under the floating
                                * shelf, shown while a window is up
                                * (proportion pass) */
    tk_widget *main_area;      /* Fixed: holds views + floating panels */
    tk_rect main_rect;         /* main area in screen coords (layout) */
    tk_rect desk_grid_rect;    /* desktop icon grid, screen coords */
    bool desk_grid_valid;
    tk_widget *desktop_view;   /* Fixed: wallpaper + icon grid */
    tk_widget *wallpaper_w;
    tk_widget *desk_grid;
    tk_widget *task_box;       /* panel: pins + running tasks */
    tk_widget *launcher_btn;   /* panel: Start / apps button */
    bool layout_settled;       /* one extra layout after
                                * start (see poll_windows) */
    tk_widget *win_sp1;        /* WinUI: fixed-width left spacer
                                * sized at layout so the Start +
                                * task cluster centres on the
                                * SCREEN (eval F-12), not on the
                                * space left of the tray */
    tk_widget *clock_btn;
    tk_widget *tray_wifi_btn, *tray_vol_btn, *tray_batt_btn;

    /* launcher (floating panel) */
    tk_widget *launcher_view;  /* Fixed with backdrop */
    tk_widget *search_field;
    tk_widget *home_box;       /* pins + recents container */
    tk_widget *pin_grid;       /* Fixed: 76px icon cells */
    tk_widget *rec_head;
    tk_widget *recent_list;
    tk_widget *results_scroll; /* ScrollView: all-apps grid */
    tk_widget *results_fixed;  /* Fixed inside the ScrollView */
    tk_widget *chip_box;       /* wrap box of category chips */
    tk_widget *lock_btn;       /* launcher footer + quick footer */
    tk_widget *quick_lock_btn;
    bool launcher_floating;    /* backdrop mode currently applied */

    /* quick settings (floating panel) */
    tk_widget *quick_panel;    /* Fixed with backdrop */
    tk_widget *wifi_label;     /* tile state caption */
    tk_widget *wifi_tile;      /* QS Wi-Fi tile (backdrop carrier) */
    tk_widget *wifi_tile_btn;  /* tile overlay button */
    tk_widget *wifi_tile_icon;
    tk_widget *who_label;      /* launcher footer user name */
    tk_widget *who_avatar;     /* launcher footer avatar disc */
    bool home_mode;            /* launcher shows home sections */
    tk_widget *bright_slider, *bright_value;
    tk_widget *vol_slider, *vol_value;
    tk_widget *qs_vol_icon;    /* QS volume row icon (state-linked) */
    tk_widget *quick_settings_btn;

    /* icon-asset keys of the state-linked icons (reload guard) */
    char ic_tray_wifi[160], ic_tray_vol[160], ic_tray_batt[160];
    char ic_tile_wifi[160], ic_qs_vol[160];

    /* notification sheet (floating panel, from the clock) */
    tk_widget *sheet_panel;
    tk_widget *sheet_labels[BD_NOTIFY_MAX];
    bool sheet_open;

    /* Android-mode 3-button navigation + app switcher
     * (style 1 only; the buttons are NULL in other modes) */
    tk_widget *nav_back_btn, *nav_home_btn, *nav_recents_btn;
    tk_widget *recents_panel;
    tk_widget *recents_box;
    tk_widget *recents_empty;
    bool recents_open;
    bool recents_dirty;   /* card closed: rebuild at next layout */
    bd_shell_nav_cb nav_cb; void *nav_ud;
    bd_shell_win_ops win_ops; void *win_ud; bool win_ops_set;

    tk_widget *toast_label;

    bd_view view;
    de_category category;
    char query[128];
    const de_entry **results;
    size_t n_results, cap_results;

    struct { tk_widget *btn; const de_entry *entry; } desk[DESK_MAX];
    int n_desk;
    const de_entry *pin_entries[64];
    size_t n_pin_entries;

    /* panel task buttons (pins + running), rebuilt on change */
    struct { tk_widget *btn; char id[96]; bool running; } ptasks[PTASK_MAX];
    int n_ptasks;

    int battery; bool charging;
    int volume; bool muted; bool wifi; bool quick_open;
    bd_wifi *wifi_ctl; /* not owned (M3) */
    bool wifi_connected;
    int wifi_signal_dbm;   /* live RSSI of the connected AP; 0 = none/unknown */
    char wifi_ssid[64];
    tk_kbd_config kbd_cfg;   /* "keyboard" settings (kbdsettings.h) */
    int brightness;
    bd_backend *backend; /* not owned */
    tk_font *font;           /* owned only when this shell installed it */
    bool font_owned;
    bd_assist *assist;
    bd_shell_page_cb page_cb; void *page_ud;
    bd_shell_lock_cb lock_cb; void *lock_ud;
    bool panel_top;
    tk_color wallpaper;
    char wallpaper_path[512]; /* set when shell/wallpaper is a path */
    ic_cache *icons;

    struct { char *id; char *name; bool from_win; } tasks[BD_TASK_MAX];
    /* from_win: the task exists because bd_shell_poll_windows
     * saw a matching open window (not because the shell
     * launched it); such tasks are removed again when their
     * window closes. Launch-added tasks keep the existing
     * launch-record lifecycle. */
    size_t n_tasks;
    int active_task; /* index or -1 */

    /* on-screen keyboard: own root (the session paints/routes it
     * above the WM windows), the OSK widget, and the sink widget
     * used as the attach target while serving a WM window */
    tk_root *osk_root;
    tk_widget *osk_top;
    tk_widget *osk;
    tk_widget *osk_sink;
    bool osk_open;
    bool osk_suppress;       /* physical-key latch */
    int osk_kind;            /* 0 none, 1 local editor, 2 remote */
    tk_widget *osk_idw;      /* local target identity */
    uint32_t osk_serial;     /* remote target identity (WM IM serial) */
    int osk_h;               /* last positioned natural height */
    bd_shell_osk_sink_cb osk_sink_cb;
    void *osk_sink_ud;

    struct {
        char *text;        /* display text (owned) */
        char *app;         /* sender app name (owned, may be NULL) */
        uint32_t id;       /* 0 = legacy post without an id */
        int urgency;       /* bd_nt_urgency */
    } notify[BD_NOTIFY_MAX];
    size_t n_notify;
    uint32_t notify_next_id;
};

/* OSK helpers used by bd_shell_new (defined with the OSK API
 * further down). */
static bool osk_sink_fwd(void *ud, tk_event *ev);
static void osk_reposition(bd_shell *sh);

/* ---------------- settings ---------------- */

static tk_color parse_color(const char *v, tk_color def)
{
    if (!v) return def;
    if (*v == '#') v++;
    unsigned int rgb;
    if (sscanf(v, "%06x", &rgb) == 1) {
        tk_color c = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8),
                       (uint8_t)rgb, 255 };
        return c;
    }
    return def;
}

static void load_settings(bd_shell *sh)
{
    char *wp = reg_get("shell", "wallpaper");
    /* UI-REDESIGN 2.2: the desktop background is a theme token;
     * the default is the bg-family desktop_bg, not the old dark
     * blue (which clashed with the light shell, diagnosis X8). */
    tk_color def = tk_theme_default()->desktop_bg;
    if (wp && wp[0] == '/') {
        /* image wallpaper: path kept, decoded once via the cache */
        snprintf(sh->wallpaper_path, sizeof(sh->wallpaper_path), "%s", wp);
        sh->wallpaper = (tk_color){ 48, 80, 122, 255 };
    } else {
        sh->wallpaper = parse_color(wp, def);
    }
    free(wp);
    char *pos = reg_get("shell", "panel_position");
    sh->panel_top = pos && strcmp(pos, "top") == 0;
    free(pos);
    sh->volume = reg_get_int("shell", "volume", 50);
    if (sh->volume < 0) sh->volume = 0;
    if (sh->volume > 100) sh->volume = 100;
    sh->muted = reg_get_bool("shell", "muted", false);
    sh->brightness = reg_get_int("shell", "brightness", 80);
    if (sh->brightness < 0) sh->brightness = 0;
    if (sh->brightness > 100) sh->brightness = 100;
    sh->wifi = reg_get_bool("shell", "wifi", true);
    bd_kbd_settings_load(&sh->kbd_cfg);
}

/* ---------------- icons ---------------- */

/* Resolve an entry's icon through the shared cache at `size`.
 * Returns borrowed pixels (cache-owned) or NULL. */
static const uint32_t *entry_icon(bd_shell *sh, const de_entry *e,
                                  int size, int *iw, int *ih)
{
    char path[512];
    if (!e || !e->icon) return NULL;
    if (!ic_resolve_icon_path(e->icon, path, sizeof(path))) return NULL;
    return ic_get(sh->icons, path, size, size, iw, ih);
}

/* --- icon assets (REPORT-status-icons sections 3 and 8) ---
 * Every symbolic icon in the shell is a render of an
 * established vector set, fetched and rasterized at 96x96 by
 * tools/render_icons.sh: Google Material Symbols for Android
 * mode, Microsoft Fluent UI System Icons (regular weight) for
 * WinUI mode, AOSP SystemUI vectors for the navigation trio.
 * Code-drawn raster glyphs are NOT used anywhere in the shell
 * any more - they were the user's complaint (rough edges,
 * off-style shapes, hard to read at 179ppi). The PNGs are
 * white shapes on transparency; the loader tints a shape to a
 * theme colour and area-average scales it to the exact
 * display box (tk_image_load_thumbnail), so edges stay
 * anti-aliased. A mode never mixes sets. */

static bool icon_path(const char *set, const char *name,
                      char *out, size_t cap)
{
    const char *roots[5];
    roots[0] = getenv("BD_SHELL_ASSETS");
    roots[1] = "assets";
    roots[2] = "../assets";
    roots[3] = "shell/assets";
    roots[4] = "/usr/share/braindesk/assets";
    for (size_t i = 0; i < 5; i++) {
        if (!roots[i] || !roots[i][0]) continue;
        snprintf(out, cap, "%s/status/%s/%s.png", roots[i], set, name);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return true; }
    }
    return false;
}

/* Icon load failures used to be completely silent: in the
 * shipped sd-v3 image the asset tree was unreadable by the
 * desktop user (0660 root:root under 0770 directories) and
 * every status icon quietly degraded to a text label, with
 * nothing in any log (integration phase 4). Report the first
 * failure per (set, name) on stderr - the session script folds
 * it into the session log - so a broken asset install is
 * diagnosable instead of invisible. Bounded at 32 distinct
 * icons per process. */
static void icon_load_failed(const char *set, const char *name,
                             const char *why)
{
    static char seen[32][128];
    static int n_seen;
    char key[128];
    snprintf(key, sizeof(key), "%s/%s", set, name);
    for (int i = 0; i < n_seen; i++)
        if (strcmp(seen[i], key) == 0) return;
    if (n_seen < 32) {
        snprintf(seen[n_seen], sizeof(seen[0]), "%s", key);
        n_seen++;
    }
    fprintf(stderr, "braindesk2: icon %s unavailable (%s)\n", key, why);
}

/* Load icon <set>/<name> for a box x box slot, tinted to
 * `tint` (the source renders are white shapes; only their
 * alpha is kept). Returns a malloc'd box*box canvas-packed
 * pixel buffer, or NULL when the asset is unavailable - the
 * caller then leaves the widget iconless rather than falling
 * back to a drawn glyph. */
bool bd_icon_pixels(const char *set, const char *name, int box,
                    tk_color tint, uint32_t **out_px)
{
    if (!set || !name || box <= 0 || !out_px) return false;
    char path[512];
    if (!icon_path(set, name, path, sizeof(path))) {
        icon_load_failed(set, name, "no readable asset path");
        return false;
    }
    uint32_t *px = NULL;
    int w = 0, h = 0;
    if (!tk_image_load_thumbnail(path, box, box, &px, &w, &h)) {
        icon_load_failed(set, name, "asset decode failed");
        return false;
    }
    uint32_t *out = calloc((size_t)box * (size_t)box, sizeof(uint32_t));
    if (!out) { free(px); return false; }
    int ox = (box - w) / 2, oy = (box - h) / 2;
    uint32_t rgb = ((uint32_t)tint.r << 16) | ((uint32_t)tint.g << 8) |
                   (uint32_t)tint.b;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t a = px[(size_t)y * (size_t)w + (size_t)x] >> 24;
            if (a)
                out[(size_t)(y + oy) * (size_t)box + (size_t)(x + ox)] =
                    (a << 24) | rgb;
        }
    free(px);
    *out_px = out;
    return true;
}

static tk_color icon_ink(void)    { return tk_theme_default()->text; }
static tk_color icon_dim(void)    { return tk_theme_default()->text_dim; }
static tk_color icon_danger(void) { return tk_theme_default()->danger; }

/* The active mode's set. The classic style (0) predates the
 * two product modes and follows the Material set. */
static const char *icon_set(void)
{
    return style_winui() ? "fluent" : "material";
}

/* Set a button's or imageview's icon from the asset sets,
 * reloading only when (kind, set, name, box, tint) changes;
 * `slot` is the caller's key buffer (NULL = always load,
 * for one-shot icons). */
static void shell_set_icon(tk_widget *w, char *slot, size_t slot_cap,
                           const char *set, const char *name,
                           tk_color tint, int box)
{
    char key[160];
    snprintf(key, sizeof(key), "%s/%s/%d/%02x%02x%02x",
             set, name, box, tint.r, tint.g, tint.b);
    if (slot && strcmp(slot, key) == 0) return;
    uint32_t *px = NULL;
    if (bd_icon_pixels(set, name, box, tint, &px)) {
        if (tk_widget_kind_of(w) == TK_W_BUTTON)
            tk_button_set_icon(w, px, box, box);
        else
            tk_imageview_set_pixels(w, px, box, box);
        free(px);
        /* Pin the key only on success: pinning a failed load
         * (the old behaviour) suppressed every later retry, so
         * a transiently unreadable asset set degraded the icon
         * for the whole session. */
        if (slot) snprintf(slot, slot_cap, "%s", key);
    }
}

/* Placeholder app icon: the mode set's "image" symbol,
 * dimmed. Used when an entry's icon cannot be resolved - the
 * cell stays icon-sized instead of degrading into a giant
 * text card. (Was a hand-drawn tile; replaced under the same
 * no-code-glyphs rule as the tray icons.) */
static void placeholder_icon(bd_shell *sh, uint32_t *px, int S)
{
    (void)sh;
    for (int i = 0; i < S * S; i++) px[i] = 0;
    uint32_t *ipx = NULL;
    if (bd_icon_pixels(icon_set(), "image", S, icon_dim(), &ipx)) {
        memcpy(px, ipx, (size_t)S * (size_t)S * sizeof(uint32_t));
        free(ipx);
    }
}

/* Wi-Fi strength steps from a live SIGNAL_POLL reading:
 * 3 arcs at >= -60 dBm down to 0 arcs below -80 (the fan
 * files themselves carry the dot + arcs). */
static int wifi_level_from_dbm(int dbm)
{
    if (dbm >= -60) return 3;
    if (dbm >= -70) return 2;
    if (dbm >= -80) return 1;
    return 0;
}

/* ---------------- tasks ---------------- */

static void panel_tasks_rebuild(bd_shell *sh);
static void wifi_icon_update(bd_shell *sh);
static void volume_icon_update(bd_shell *sh);

static void task_add(bd_shell *sh, const de_entry *e)
{
    for (size_t i = 0; i < sh->n_tasks; i++) {
        if (strcmp(sh->tasks[i].id, e->id) == 0) {
            sh->active_task = (int)i;
            panel_tasks_rebuild(sh);
            return;
        }
    }
    if (sh->n_tasks >= BD_TASK_MAX) return;
    sh->tasks[sh->n_tasks].id = strdup(e->id);
    sh->tasks[sh->n_tasks].name = strdup(e->name);
    sh->active_task = (int)sh->n_tasks;
    sh->n_tasks++;
    panel_tasks_rebuild(sh);
}

size_t bd_shell_task_count(const bd_shell *sh) { return sh->n_tasks; }

const char *bd_shell_active_task(const bd_shell *sh)
{
    if (sh->active_task < 0 || (size_t)sh->active_task >= sh->n_tasks) return NULL;
    return sh->tasks[sh->active_task].name;
}

bool bd_shell_cycle_task(bd_shell *sh)
{
    if (sh->n_tasks == 0) return false;
    sh->active_task = (sh->active_task + 1) % (int)sh->n_tasks;
    panel_tasks_rebuild(sh);
    return true;
}

bool bd_shell_close_task(bd_shell *sh, size_t idx)
{
    if (idx >= sh->n_tasks) return false;
    free(sh->tasks[idx].id); free(sh->tasks[idx].name);
    memmove(&sh->tasks[idx], &sh->tasks[idx + 1],
            (sh->n_tasks - idx - 1) * sizeof(sh->tasks[0]));
    sh->n_tasks--;
    if (sh->n_tasks == 0) sh->active_task = -1;
    else if (sh->active_task >= (int)sh->n_tasks) sh->active_task = (int)sh->n_tasks - 1;
    panel_tasks_rebuild(sh);
    return true;
}

/* ---------------- notifications ---------------- */

static void sheet_sync(bd_shell *sh)
{
    for (int i = 0; i < BD_NOTIFY_MAX; i++) {
        bool has = (size_t)i < sh->n_notify;
        tk_set_visible(sh->sheet_labels[i], has);
        if (has) tk_label_set_text(sh->sheet_labels[i],
                                   sh->notify[i].text);
    }
}

static void notify_clear_entry(bd_shell *sh, size_t i)
{
    free(sh->notify[i].text);
    free(sh->notify[i].app);
    sh->notify[i].text = NULL;
    sh->notify[i].app = NULL;
}

/* Shared add path. Takes ownership of `text`. When the list is
 * full, `evict` drops the oldest entry; without it the add
 * fails. Returns the new entry's index, or -1. */
static int notify_add(bd_shell *sh, const char *app, char *text,
                      int urgency, bool evict, bool with_id)
{
    if (sh->n_notify >= BD_NOTIFY_MAX) {
        if (!evict) { free(text); return -1; }
        notify_clear_entry(sh, 0);
        memmove(&sh->notify[0], &sh->notify[1],
                (sh->n_notify - 1) * sizeof(sh->notify[0]));
        sh->n_notify--;
    }
    size_t i = sh->n_notify++;
    sh->notify[i].text = text;
    sh->notify[i].app = app ? strdup(app) : NULL;
    sh->notify[i].urgency = urgency;
    sh->notify[i].id = with_id ? ++sh->notify_next_id : 0;
    tk_label_set_text(sh->toast_label, text);
    tk_set_visible(sh->toast_label, true);
    sheet_sync(sh);
    return (int)i;
}

bool bd_shell_notify(bd_shell *sh, const char *text)
{
    if (!text) return false;
    return notify_add(sh, NULL, strdup(text), 1 /* NORMAL */,
                      false, false) >= 0;
}

uint32_t bd_shell_notify_post(bd_shell *sh, const char *app,
                              const char *title, const char *body,
                              int urgency)
{
    if (!title || !*title) return 0;
    char *text;
    if (body && *body) {
        size_t cap = strlen(title) + strlen(body) + 4;
        text = malloc(cap);
        if (!text) return 0;
        snprintf(text, cap, "%s - %s", title, body);
    } else {
        text = strdup(title);
        if (!text) return 0;
    }
    int i = notify_add(sh, app, text, urgency, true, true);
    return i >= 0 ? sh->notify[i].id : 0;
}

bool bd_shell_notify_cancel(bd_shell *sh, uint32_t id)
{
    if (id == 0) return false;
    for (size_t i = 0; i < sh->n_notify; i++) {
        if (sh->notify[i].id != id) continue;
        notify_clear_entry(sh, i);
        memmove(&sh->notify[i], &sh->notify[i + 1],
                (sh->n_notify - i - 1) * sizeof(sh->notify[0]));
        sh->n_notify--;
        if (sh->n_notify == 0)
            tk_set_visible(sh->toast_label, false);
        sheet_sync(sh);
        return true;
    }
    return false;
}

size_t bd_shell_notify_count(const bd_shell *sh) { return sh->n_notify; }

const char *bd_shell_notify_at(const bd_shell *sh, size_t i)
{
    return i < sh->n_notify ? sh->notify[i].text : NULL;
}

uint32_t bd_shell_notify_id_at(const bd_shell *sh, size_t i)
{
    return i < sh->n_notify ? sh->notify[i].id : 0;
}

const char *bd_shell_notify_app_at(const bd_shell *sh, size_t i)
{
    return i < sh->n_notify ? sh->notify[i].app : NULL;
}

int bd_shell_notify_urgency_at(const bd_shell *sh, size_t i)
{
    return i < sh->n_notify ? sh->notify[i].urgency : -1;
}

void bd_shell_notify_dismiss(bd_shell *sh)
{
    for (size_t i = 0; i < sh->n_notify; i++)
        notify_clear_entry(sh, i);
    sh->n_notify = 0;
    tk_set_visible(sh->toast_label, false);
    sheet_sync(sh);
}

/* ---------------- launching ---------------- */

static bool shell_launch(bd_shell *sh, const de_entry *e)
{
    if (!sh->exec_fn) return false;
    bool ok = ln_launch(sh->store, e, sh->exec_fn, sh->exec_ud);
    if (ok) {
        task_add(sh, e);
        bd_shell_refresh_apps(sh); /* recents changed */
    }
    return ok;
}

bool bd_shell_activate_result(bd_shell *sh, size_t i)
{
    if (i >= sh->n_results) return false;
    return shell_launch(sh, sh->results[i]);
}

/* ---------------- launcher model -> widgets ---------------- */

/* Japanese category names (UI-REDESIGN 2.3): the freedesktop
 * category names are never shown raw in the Japanese UI. */
static const char *category_label(de_category cat)
{
    switch (cat) {
    case DE_CAT_ALL:         return "すべて";
    case DE_CAT_ACCESSORIES: return "アクセサリ";
    case DE_CAT_DEVELOPMENT: return "開発";
    case DE_CAT_EDUCATION:   return "教育";
    case DE_CAT_GAME:        return "ゲーム";
    case DE_CAT_GRAPHICS:    return "グラフィックス";
    case DE_CAT_NETWORK:     return "ネットワーク";
    case DE_CAT_OFFICE:      return "オフィス";
    case DE_CAT_AUDIOVIDEO:  return "音声と映像";
    case DE_CAT_SETTINGS:    return "設定";
    case DE_CAT_SYSTEM:      return "システム";
    case DE_CAT_OTHER:       return "その他";
    default:                 return "その他";
    }
}

static const de_category cat_order[] = {
    DE_CAT_ALL, DE_CAT_ACCESSORIES, DE_CAT_DEVELOPMENT, DE_CAT_EDUCATION,
    DE_CAT_GAME, DE_CAT_GRAPHICS, DE_CAT_NETWORK, DE_CAT_OFFICE,
    DE_CAT_AUDIOVIDEO, DE_CAT_SETTINGS, DE_CAT_SYSTEM, DE_CAT_OTHER
};

struct entry_ctx { bd_shell *sh; const de_entry *entry; };

static void on_entry_btn(tk_widget *w, void *ud)
{
    (void)w;
    struct entry_ctx *ctx = ud;
    shell_launch(ctx->sh, ctx->entry);
}

/* Launcher entries close the launcher on a successful launch
 * (the desktop's own icons use on_entry_btn and stay). */
static void on_entry_btn_close(tk_widget *w, void *ud)
{
    (void)w;
    struct entry_ctx *ctx = ud;
    if (shell_launch(ctx->sh, ctx->entry))
        bd_shell_show_launcher(ctx->sh, false);
}

/* entry contexts: rebuilt with the grids; entries are catalog-owned */
static struct entry_ctx pin_ctxs[64];
static struct entry_ctx res_ctxs[256];
static struct entry_ctx desk_ctxs[DESK_MAX];

static void clear_children(tk_widget *parent)
{
    while (tk_child_count(parent) > 0)
        tk_widget_free(tk_child_at(parent, 0));
}

static tk_widget *make_icon_cell_cb(bd_shell *sh, const de_entry *e,
                                    struct entry_ctx *ctx,
                                    void (*cb)(tk_widget *, void *));

/* One 76x76 icon cell (icon 48 + caption label) for entry e. */
static tk_widget *make_icon_cell(bd_shell *sh, const de_entry *e,
                                 struct entry_ctx *ctx)
{
    return make_icon_cell_cb(sh, e, ctx, on_entry_btn);
}

static tk_widget *make_icon_cell_cb(bd_shell *sh, const de_entry *e,
                                    struct entry_ctx *ctx,
                                    void (*cb)(tk_widget *, void *))
{
    tk_widget *b = tk_button_new(e->name);
    int iw = 0, ih = 0;
    const uint32_t *ipx = entry_icon(sh, e, 48, &iw, &ih);
    if (ipx) {
        tk_button_set_icon(b, ipx, iw, ih);
    } else {
        static uint32_t ph[48 * 48];
        placeholder_icon(sh, ph, 48);
        tk_button_set_icon(b, ph, 48, 48);
    }
    tk_widget_set_text_style(b, TK_TEXT_CAPTION);
    ctx->sh = sh;
    ctx->entry = e;
    tk_button_set_on_click(b, cb, ctx);
    return b;
}

void bd_shell_refresh_apps(bd_shell *sh)
{
    const de_entry *tmp[256];
    size_t m = de_catalog_query(sh->catalog, sh->category,
                                sh->query[0] ? sh->query : NULL,
                                tmp, 256);
    if (m > sh->cap_results) {
        sh->cap_results = m;
        sh->results = realloc(sh->results, m * sizeof(de_entry *));
    }
    sh->n_results = m < 256 ? m : 256;
    /* results may still be NULL when nothing matched yet;
     * memcpy requires a valid destination even for 0 bytes
     * (UBSan nonnull) */
    if (sh->n_results)
        memcpy(sh->results, tmp, sh->n_results * sizeof(de_entry *));

    /* pins, resolved against the catalog */
    sh->n_pin_entries = 0;
    size_t np = ln_pin_count(sh->store);
    for (size_t i = 0; i < np && sh->n_pin_entries < 64; i++) {
        const de_entry *e = de_catalog_find_id(sh->catalog, ln_pin_at(sh->store, i));
        if (e) sh->pin_entries[sh->n_pin_entries++] = e;
    }

    /* pin grid cells (positions assigned in the launcher layout) */
    clear_children(sh->pin_grid);
    size_t npin = sh->n_pin_entries < 8 ? sh->n_pin_entries : 8;
    for (size_t i = 0; i < npin; i++)
        tk_add_child(sh->pin_grid,
                     make_icon_cell_cb(sh, sh->pin_entries[i], &pin_ctxs[i],
                                       on_entry_btn_close));

    /* recents list: icon 24 + name rows */
    {
        const char *names[LN_RECENT_MAX];
        const uint32_t *icons[LN_RECENT_MAX];
        int iw[LN_RECENT_MAX], ih[LN_RECENT_MAX];
        size_t nr = 0;
        size_t nrec = ln_recent_count(sh->store);
        for (size_t i = 0; i < nrec && nr < LN_RECENT_MAX; i++) {
            const de_entry *e = de_catalog_find_id(sh->catalog,
                                                   ln_recent_at(sh->store, i));
            if (!e) continue;
            names[nr] = e->name;
            icons[nr] = entry_icon(sh, e, 24, &iw[nr], &ih[nr]);
            nr++;
        }
        tk_listview_set_items(sh->recent_list, names, nr);
        tk_listview_set_item_icons(sh->recent_list, icons,
                                   nr ? iw[0] : 0, nr ? ih[0] : 0, nr);
    }

    /* all-apps grid cells (positions assigned in the layout) */
    clear_children(sh->results_fixed);
    for (size_t i = 0; i < sh->n_results; i++)
        tk_add_child(sh->results_fixed,
                     make_icon_cell_cb(sh, sh->results[i], &res_ctxs[i],
                                       on_entry_btn_close));

    /* home vs results: a query or a non-ALL category switches the
     * middle area to the all-apps grid. In home mode the grid may
     * still fill the space under the home sections (decided by
     * the layout, which knows the heights); refresh only forces
     * it off when results take over the whole middle area. */
    bool results_mode = sh->query[0] != '\0' || sh->category != DE_CAT_ALL;
    sh->home_mode = !results_mode;
    tk_set_visible(sh->home_box, !results_mode);
    if (results_mode) tk_set_visible(sh->results_scroll, true);

    /* chip faces: the active category chip carries the selection.
     * UI v2 refinement: unselected chips also get a face (the
     * tonal surface_alt pill, the M3 filter-chip language) - as
     * bare text they read as loose labels with uneven rhythm,
     * not as a chip group. Classic keeps the faceless chips. */
    {
        const tk_theme *t = tk_theme_default();
        for (size_t i = 0; i < tk_child_count(sh->chip_box); i++) {
            tk_widget *chip = tk_child_at(sh->chip_box, i);
            tk_button_set_face(chip,
                cat_order[i] == sh->category ? &t->selection
                : style_v2() ? &t->surface_alt : NULL);
        }
    }
    panel_tasks_rebuild(sh);
}

int bd_shell_reload_catalog(bd_shell *sh)
{
    int changes = de_catalog_refresh(sh->catalog);
    if (changes) bd_shell_refresh_apps(sh);
    return changes;
}

void bd_shell_set_category(bd_shell *sh, de_category cat)
{
    sh->category = cat;
    bd_shell_refresh_apps(sh);
}

de_category bd_shell_category(const bd_shell *sh) { return sh->category; }

void bd_shell_set_query(bd_shell *sh, const char *query)
{
    snprintf(sh->query, sizeof(sh->query), "%s", query ? query : "");
    bd_shell_refresh_apps(sh);
}

size_t bd_shell_result_count(const bd_shell *sh) { return sh->n_results; }

const de_entry *bd_shell_result_at(const bd_shell *sh, size_t i)
{
    return i < sh->n_results ? sh->results[i] : NULL;
}

/* ---------------- callbacks ---------------- */

static void on_launcher_btn(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    bd_shell_show_launcher(sh, sh->view != BD_VIEW_LAUNCHER);
}

static void on_quick_btn(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    bd_shell_set_quick_open(sh, !sh->quick_open);
}

static void on_gear_btn(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    /* the gear opens the settings app; without one in the catalog
     * it falls back to quick settings */
    const de_entry *tmp[1];
    size_t m = de_catalog_query(sh->catalog, DE_CAT_SETTINGS, NULL, tmp, 1);
    if (m > 0) shell_launch(sh, tmp[0]);
    else bd_shell_set_quick_open(sh, !sh->quick_open);
}

/* Overlay open/close changes which subtrees are visible, and
 * the toolkit only assigns rects at layout time: a subtree
 * that was hidden at the last layout has zeroed rects and
 * neither paints nor hit-tests. Re-run the layout (idempotent,
 * sized from the last call) so a freshly opened overlay is
 * arranged in the same turn. No-op before the first layout. */
static void shell_relayout(bd_shell *sh)
{
    if (sh->w > 0 && sh->h > 0) bd_shell_layout(sh, sh->w, sh->h);
}

static void on_clock_btn(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    sh->sheet_open = !sh->sheet_open;
    tk_set_visible(sh->sheet_panel, sh->sheet_open);
    if (sh->sheet_open) bd_shell_set_quick_open(sh, false);
    shell_relayout(sh);
}

static void on_dismiss_btn(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    bd_shell_notify_dismiss(sh);
}

static void on_lock_btn(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    if (sh->lock_cb) sh->lock_cb(sh->lock_ud);
}

static void on_open_settings(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    bd_shell_set_quick_open(sh, false);
    on_gear_btn(w, sh);
}

static void on_search_change(tk_widget *w, void *ud)
{
    bd_shell *sh = ud;
    char buf[sizeof(sh->query)];
    tk_textfield_text(w, buf, sizeof(buf));
    bd_shell_set_query(sh, buf);
}

struct chip_ctx { bd_shell *sh; de_category cat; };
static struct chip_ctx chip_ctxs[CHIP_MAX];

static void on_chip(tk_widget *w, void *ud)
{
    (void)w;
    struct chip_ctx *ctx = ud;
    bd_shell_set_category(ctx->sh, ctx->cat);
}

static void on_recent_activate(tk_widget *w, void *ud)
{
    bd_shell *sh = ud;
    int idx = tk_listview_selected(w);
    if (idx < 0) return;
    size_t seen = 0;
    size_t nrec = ln_recent_count(sh->store);
    for (size_t i = 0; i < nrec; i++) {
        const de_entry *e = de_catalog_find_id(sh->catalog,
                                               ln_recent_at(sh->store, i));
        if (!e) continue;
        if ((int)seen++ == idx) {
            if (shell_launch(sh, e)) bd_shell_show_launcher(sh, false);
            return;
        }
    }
}

static void on_wifi_tile(tk_widget *w, void *ud)
{
    (void)w;
    bd_shell *sh = ud;
    bd_shell_set_wifi(sh, !sh->wifi);
}

static void on_bright_change(tk_widget *w, void *ud)
{
    bd_shell *sh = ud;
    bd_shell_set_brightness(sh, (int)(tk_slider_value(w) * 100.0f + 0.5f));
}

static void on_volume_change(tk_widget *w, void *ud)
{
    bd_shell *sh = ud;
    bd_shell_set_volume(sh, (int)(tk_slider_value(w) * 100.0f + 0.5f));
}

static void on_assist(void *ud, bd_assist_action act)
{
    bd_shell *sh = ud;
    switch (act) {
    case BD_ASSIST_VOL_UP:
        bd_shell_set_volume(sh, sh->volume + 10);
        break;
    case BD_ASSIST_VOL_DOWN:
        bd_shell_set_volume(sh, sh->volume - 10);
        break;
    case BD_ASSIST_BRIGHT_UP:
        bd_shell_set_brightness(sh, sh->brightness + 10);
        break;
    case BD_ASSIST_BRIGHT_DOWN:
        bd_shell_set_brightness(sh, sh->brightness - 10);
        break;
    case BD_ASSIST_PAGE_UP:
        if (sh->page_cb) sh->page_cb(sh->page_ud, TK_KEY_PAGE_UP);
        break;
    case BD_ASSIST_PAGE_DOWN:
        if (sh->page_cb) sh->page_cb(sh->page_ud, TK_KEY_PAGE_DOWN);
        break;
    case BD_ASSIST_LAUNCHER:
        bd_shell_show_launcher(sh, true);
        break;
    case BD_ASSIST_LOCK:
        if (sh->lock_cb) sh->lock_cb(sh->lock_ud);
        break;
    }
}

/* ---------------- panel tasks ---------------- */

struct ptask_ctx { bd_shell *sh; int slot; };
static struct ptask_ctx ptask_ctxs[PTASK_MAX];

static void on_ptask(tk_widget *w, void *ud)
{
    (void)w;
    struct ptask_ctx *ctx = ud;
    bd_shell *sh = ctx->sh;
    if (ctx->slot >= sh->n_ptasks) return;
    const char *id = sh->ptasks[ctx->slot].id;
    if (sh->ptasks[ctx->slot].running) {
        for (size_t i = 0; i < sh->n_tasks; i++)
            if (strcmp(sh->tasks[i].id, id) == 0) {
                sh->active_task = (int)i;
                break;
            }
        panel_tasks_rebuild(sh);
    } else {
        const de_entry *e = de_catalog_find_id(sh->catalog, id);
        if (e) shell_launch(sh, e);
    }
}

static void panel_tasks_rebuild(bd_shell *sh)
{
    if (!sh->task_box) return;
    clear_children(sh->task_box);
    sh->n_ptasks = 0;
    const tk_theme *t = tk_theme_default();

    /* the set: pins first, then running tasks that are not pinned */
    const char *ids[PTASK_MAX];
    bool running[PTASK_MAX];
    int n = 0;
    size_t np = ln_pin_count(sh->store);
    for (size_t i = 0; i < np && n < PTASK_MAX; i++) {
        const char *id = ln_pin_at(sh->store, i);
        bool run = false;
        for (size_t j = 0; j < sh->n_tasks; j++)
            if (strcmp(sh->tasks[j].id, id) == 0) run = true;
        ids[n] = id; running[n] = run; n++;
    }
    for (size_t j = 0; j < sh->n_tasks && n < PTASK_MAX; j++) {
        bool dup = false;
        for (int i = 0; i < n; i++)
            if (strcmp(ids[i], sh->tasks[j].id) == 0) dup = true;
        if (!dup) { ids[n] = sh->tasks[j].id; running[n] = true; n++; }
    }

    for (int i = 0; i < n; i++) {
        const de_entry *e = de_catalog_find_id(sh->catalog, ids[i]);
        if (!e) continue;
        tk_widget *b = tk_button_new(e->name);
        int iw = 0, ih = 0;
        const uint32_t *ipx = entry_icon(sh, e, 40, &iw, &ih);
        if (ipx) {
            tk_button_set_icon(b, ipx, iw, ih);
        } else {
            static uint32_t ph[40 * 40];
            placeholder_icon(sh, ph, 40);
            tk_button_set_icon(b, ph, 40, 40);
        }
        tk_button_set_icon_only(b, true);
        tk_button_set_flat(b, true);
        bool active = running[i] && sh->active_task >= 0 &&
                      (size_t)sh->active_task < sh->n_tasks &&
                      strcmp(sh->tasks[sh->active_task].id, ids[i]) == 0;
        if (active) tk_button_set_face(b, &t->surface_alt);
        if (running[i])
            tk_button_set_indicator(b, active ? &t->accent : &t->border);
        int slot = sh->n_ptasks;
        snprintf(sh->ptasks[slot].id, sizeof(sh->ptasks[slot].id), "%s", ids[i]);
        sh->ptasks[slot].btn = b;
        sh->ptasks[slot].running = running[i];
        sh->n_ptasks++;
        ptask_ctxs[slot].sh = sh;
        ptask_ctxs[slot].slot = slot;
        tk_button_set_on_click(b, on_ptask, &ptask_ctxs[slot]);
        tk_add_child(sh->task_box, b);
    }
    /* a rebuild must reach the screen: re-run the layout so
     * the new buttons get rects, and damage the panel so the
     * compositor repaints them (a tree-only change paints
     * nothing in the damage-driven guest path) */
    shell_relayout(sh);
    if (sh->panel) tk_widget_invalidate(sh->panel);
}

/* ---------------- construction ---------------- */

static tk_widget *tray_button(bd_shell *sh, const char *name)
{
    (void)sh;
    tk_widget *b = tk_button_new(name);
    tk_button_set_icon_only(b, true);
    tk_button_set_flat(b, true);
    tk_button_set_on_click(b, on_quick_btn, sh);
    return b;
}

/* nav buttons (Android mode): one callback, the action in the
 * context - the chip_ctx pattern. */
struct nav_ctx { bd_shell *sh; int act; };
static struct nav_ctx nav_ctxs[3];
static void on_nav_btn(tk_widget *w, void *ud)
{
    (void)w;
    struct nav_ctx *c = ud;
    bd_shell_nav(c->sh, (bd_nav_action)c->act);
}
static tk_widget *nav_button(bd_shell *sh, const char *name,
                             const char *icon, int act)
{
    tk_widget *b = tk_button_new(name);
    shell_set_icon(b, NULL, 0, "aosp", icon, icon_ink(), 28);
    tk_button_set_icon_only(b, true);
    tk_button_set_flat(b, true);
    /* one full touch target per key (eval F-18): the trio
     * measured ~36px centre-to-centre with ~20px glyphs,
     * too tight for the 48dp (54px) target the panel's
     * other controls use; 64px-wide targets put the
     * centres 68px apart */
    tk_widget_set_min_size(b, 64, 48);
    nav_ctxs[act].sh = sh;
    nav_ctxs[act].act = act;
    tk_button_set_on_click(b, on_nav_btn, &nav_ctxs[act]);
    return b;
}

bd_shell *bd_shell_new(int w, int h, de_catalog *catalog, ln_store *store,
                       ln_exec_fn exec_fn, void *exec_userdata)
{
    bd_shell *sh = calloc(1, sizeof(*sh));
    /* Product font (M4 fix): the toolkit paints NO text until a
     * font is installed, and until now nothing in the shell ever
     * installed one - the shell would have come up textless on the
     * device. Use the same product loader as the app mains
     * (tk_font_load_default + tk_font_install). Only install when
     * the embedder has not already installed a font (tests and the
     * screenshot harness install their own); a font installed here
     * is owned by the shell and freed in bd_shell_free. */
    if (!tk_font_current()) {
        sh->font = tk_font_load_default();
        if (sh->font) {
            tk_font_install(sh->font);
            sh->font_owned = true;
        }
    }
    sh->w = w; sh->h = h;
    sh->catalog = catalog;
    sh->store = store;
    sh->exec_fn = exec_fn;
    sh->exec_ud = exec_userdata;
    sh->view = BD_VIEW_DESKTOP;
    sh->category = DE_CAT_ALL;
    sh->battery = -1;
    sh->active_task = -1;
    sh->icons = ic_cache_new(2u * 1024u * 1024u); /* 2 MiB pixel budget */
    load_settings(sh);

    /* top: Fixed container; bd_shell_layout() positions the
     * panel and the main area explicitly (the classic geometry
     * is the old vertical box's: shelf flush, main fills the
     * rest; v2 floats the shelf - see bd_shell_layout) */
    tk_widget *top = tk_fixed_new();
    sh->top = top;

    /* Band cover (proportion pass): in v2 the shelf floats over
     * the desktop, so the strip around the pill shows the
     * wallpaper. That is the desktop's own look - but with an
     * app window up, the same near-black strip (the default
     * wallpaper is #102030) reads as a black band under a bright
     * window. While any window is non-minimized, this plain
     * widget covers the strip with the theme surface - the same
     * face the shelf's backdrop paints - so the panel zone reads
     * as one shell surface. bd_shell_poll_windows() drives the
     * visibility; bd_shell_layout() positions it. On the bare
     * desktop it stays hidden and the floating-shelf look is
     * unchanged. */
    sh->band_cover = tk_fixed_new();
    tk_widget_set_backdrop(sh->band_cover, true, 0);
    tk_set_visible(sh->band_cover, false);

    /* --- panel (UI-REDESIGN 2.3): icon chrome only.
     * UI-STYLE-v2: the shelf is a floating pill (backdrop with
     * shadow, full radius) over the desktop. --- */
    tk_widget *panel = tk_box_new(TK_DIR_HORIZONTAL, 4, w >= 600 ? 4 : 0);
    sh->panel = panel;
    if (style_v2())
        tk_widget_set_backdrop(panel, true, 999);
    else
        tk_widget_set_backdrop(panel, false, 0);
    tk_widget *launcher_btn = tk_button_new("アプリ");
    sh->launcher_btn = launcher_btn;
    /* the launcher button is the mode's own symbol: the
     * Windows logo (accent) in WinUI mode, Material's "apps"
     * grid in Android mode */
    if (style_winui())
        shell_set_icon(launcher_btn, NULL, 0, "fluent", "start",
                       tk_theme_default()->accent, 24);
    else
        shell_set_icon(launcher_btn, NULL, 0, "material", "apps",
                       icon_ink(), 24);
    tk_button_set_icon_only(launcher_btn, true);
    tk_button_set_flat(launcher_btn, true);
    tk_button_set_on_click(launcher_btn, on_launcher_btn, sh);
    /* WinUI: the taskbar centres the Start button + task group
     * (the Windows 11 default); a flex spacer on each side of
     * the group balances the tray at the right edge. Android /
     * classic keep the group at the left, tasks flexing. */
    if (style_winui()) {
        /* the left spacer is FIXED-width, sized in
         * bd_shell_layout so the Start + task cluster sits
         * centred on the screen (Windows 11 centres the
         * group on the taskbar itself; two flex spacers
         * would centre it in the space left of the tray,
         * which measured x=285 for the Start glyph - eval
         * F-12). The right spacer stays flex. */
        sh->win_sp1 = tk_fixed_new();
        tk_add_child(panel, sh->win_sp1);
    }
    tk_add_child(panel, launcher_btn);
    sh->task_box = tk_box_new(TK_DIR_HORIZONTAL, 2, 0);
    tk_set_flex(sh->task_box, style_winui() ? 0 : 1);
    tk_add_child(panel, sh->task_box);
    if (style_winui()) {
        tk_widget *sp = tk_fixed_new();
        tk_set_flex(sp, 1);
        tk_add_child(panel, sp);
    }
    /* Android-mode navigation trio (Back - Home - Recents, the
     * tablet form), between the task group and the tray. WinUI
     * mode has no nav cluster (taskbar form); classic neither. */
    if (style_v2()) {
        sh->nav_back_btn = nav_button(sh, "戻る", "nav_back", BD_NAV_BACK);
        sh->nav_home_btn = nav_button(sh, "ホーム", "nav_home", BD_NAV_HOME);
        sh->nav_recents_btn = nav_button(sh, "最近使ったアプリ",
                                         "nav_recent", BD_NAV_RECENTS);
        tk_add_child(panel, sh->nav_back_btn);
        tk_add_child(panel, sh->nav_home_btn);
        tk_add_child(panel, sh->nav_recents_btn);
    }
    /* tray: symbolic icons; every tray button opens quick
     * settings, so the tray as a whole is the quick-settings
     * button. The battery is ONE composite element - icon plus
     * percentage in a single button (the AOSP BatteryMeterView
     * arrangement); there is no separate percentage label. */
    sh->tray_wifi_btn = tray_button(sh, "Wi-Fi");
    sh->tray_vol_btn = tray_button(sh, "音量");
    sh->tray_batt_btn = tray_button(sh, "電池");
    tk_button_set_icon_only(sh->tray_batt_btn, false);
    /* icon LEFT of the percentage (the BatteryMeterView row),
     * not stacked above it */
    tk_button_set_icon_layout(sh->tray_batt_btn, TK_BTN_ICON_ROW);
    tk_button_set_label(sh->tray_batt_btn, "--%");
    tk_widget_set_text_style(sh->tray_batt_btn, TK_TEXT_CAPTION);
    tk_add_child(panel, sh->tray_wifi_btn);
    tk_add_child(panel, sh->tray_vol_btn);
    tk_add_child(panel, sh->tray_batt_btn);
    sh->clock_btn = tk_button_new("--:--");
    tk_button_set_flat(sh->clock_btn, true);
    tk_widget_set_text_style(sh->clock_btn, TK_TEXT_HEADING);
    tk_button_set_on_click(sh->clock_btn, on_clock_btn, sh);
    tk_add_child(panel, sh->clock_btn);
    tk_widget *gear = tk_button_new("設定");
    /* the shelf's settings button is shell furniture, so it
     * wears the mode set's monochrome gear like the other tray
     * symbols - NOT a full-colour app icon from the icon cache
     * (the cached "settings" art rendered as a teal gear with a
     * power-like mark that read as neither settings nor power
     * and clashed with the tray; REPORT section 8) */
    shell_set_icon(gear, NULL, 0, icon_set(), "settings",
                   icon_ink(), 24);
    tk_button_set_icon_only(gear, true);
    tk_button_set_flat(gear, true);
    tk_button_set_on_click(gear, on_gear_btn, sh);
    tk_add_child(panel, gear);

    /* --- main area: desktop + floating panels --- */
    tk_widget *main_area = tk_fixed_new();
    sh->main_area = main_area;

    /* desktop view */
    tk_widget *desk = tk_fixed_new();
    sh->desktop_view = desk;
    sh->wallpaper_w = tk_imageview_new();
    /* ImageView words use the canvas packing: a<<24|r<<16|g<<8|b. */
    uint32_t px = ((uint32_t)sh->wallpaper.a << 24) | ((uint32_t)sh->wallpaper.r << 16) |
                  ((uint32_t)sh->wallpaper.g << 8) | (uint32_t)sh->wallpaper.b;
    bool have_image = false;
    if (sh->wallpaper_path[0]) {
        int iw = 0, ih = 0;
        const uint32_t *ipx = ic_get(sh->icons, sh->wallpaper_path, w, h, &iw, &ih);
        if (ipx) {
            /* ImageView copies the pixels (one copy, into the
             * widget); decode+scale itself is not repeated. */
            tk_imageview_set_pixels(sh->wallpaper_w, ipx, iw, ih);
            have_image = true;
        }
    }
    if (!have_image)
        tk_imageview_set_pixels(sh->wallpaper_w, &px, 1, 1);
    tk_add_child(desk, sh->wallpaper_w);
    sh->desk_grid = tk_grid_new(4, 8, 8, 8);
    /* uniform tracks: desktop icon grids (Windows, macOS,
     * Android launchers) use one pitch for every column and
     * row; content-sized columns measured 115px vs 89px
     * between neighbours (eval F-07) */
    tk_grid_set_uniform(sh->desk_grid, true);
    tk_add_child(desk, sh->desk_grid);
    tk_add_child(main_area, desk);

    /* desktop icons: pinned entries if any, else first catalog apps */
    {
        const de_entry *icons[DESK_MAX];
        int ni = 0;
        size_t np = ln_pin_count(store);
        for (size_t i = 0; i < np && ni < DESK_MAX; i++) {
            const de_entry *e = de_catalog_find_id(catalog, ln_pin_at(store, i));
            if (e) icons[ni++] = e;
        }
        if (ni == 0) {
            const de_entry *all[DESK_MAX];
            size_t m = de_catalog_query(catalog, DE_CAT_ALL, NULL, all, DESK_MAX);
            for (size_t i = 0; i < m && ni < DESK_MAX; i++) icons[ni++] = all[i];
        }
        for (int i = 0; i < ni; i++) {
            tk_widget *b = make_icon_cell(sh, icons[i], &desk_ctxs[i]);
            /* labels sit on the wallpaper: white + 1px dark shadow
             * on dark wallpapers (UI-REDESIGN 2.3), theme text on
             * light ones */
            int lum = (299 * sh->wallpaper.r + 587 * sh->wallpaper.g +
                       114 * sh->wallpaper.b) / 1000;
            if (lum < 128) {
                tk_widget_set_text_color(b, (tk_color){ 245, 245, 250, 255 });
                tk_widget_set_text_shadow(b, true);
            }
            sh->desk[i].btn = b;
            sh->desk[i].entry = icons[i];
            tk_add_child(sh->desk_grid, b);
        }
        sh->n_desk = ni;
    }

    /* --- launcher: floating panel (UI-REDESIGN 2.3) --- */
    tk_widget *lv = tk_fixed_new();
    sh->launcher_view = lv;
    tk_widget_set_backdrop(lv, true, -1);
    sh->launcher_floating = true;
    sh->search_field = tk_textfield_new("");
    tk_textfield_set_placeholder(sh->search_field, "アプリを検索");
    tk_textfield_set_on_change(sh->search_field, on_search_change, sh);
    tk_add_child(lv, sh->search_field);
    sh->home_box = tk_fixed_new();
    {
        tk_widget *pin_head = tk_label_new("ピン留め");
        tk_widget_set_text_style(pin_head, TK_TEXT_TITLE);
        tk_set_fixed_rect(pin_head, (tk_rect){ 16, 0, 200, 28 });
        tk_add_child(sh->home_box, pin_head);
        sh->pin_grid = tk_fixed_new();
        tk_add_child(sh->home_box, sh->pin_grid);
        sh->rec_head = tk_label_new("最近使った");
        tk_widget_set_text_style(sh->rec_head, TK_TEXT_TITLE);
        tk_add_child(sh->home_box, sh->rec_head);
        sh->recent_list = tk_listview_new();
        tk_listview_set_on_activate(sh->recent_list, on_recent_activate, sh);
        tk_add_child(sh->home_box, sh->recent_list);
    }
    tk_add_child(lv, sh->home_box);
    sh->results_scroll = tk_scrollview_new();
    sh->results_fixed = tk_fixed_new();
    tk_add_child(sh->results_scroll, sh->results_fixed);
    tk_set_visible(sh->results_scroll, false);
    tk_add_child(lv, sh->results_scroll);
    sh->chip_box = tk_fixed_new();
    for (size_t i = 0; i < CHIP_MAX; i++) {
        tk_widget *chip = tk_button_new(category_label(cat_order[i]));
        tk_button_set_flat(chip, true);
        tk_widget_set_text_style(chip, TK_TEXT_CAPTION);
        chip_ctxs[i].sh = sh;
        chip_ctxs[i].cat = cat_order[i];
        tk_button_set_on_click(chip, on_chip, &chip_ctxs[i]);
        tk_add_child(sh->chip_box, chip);
    }
    tk_add_child(lv, sh->chip_box);
    {
        /* footer: avatar disc + user name (the Start-menu /
         * ChromeOS user chip language). Positions are assigned
         * in launcher_layout, which centres the name on the
         * footer's 48px band. */
        const char *user = getenv("USER");
        sh->who_avatar = tk_imageview_new();
        shell_set_icon(sh->who_avatar, NULL, 0, icon_set(),
                       style_winui() ? "person" : "account_circle",
                       icon_ink(), 32);
        tk_add_child(lv, sh->who_avatar);
        tk_widget *who = tk_label_new(user && *user ? user : "brain");
        tk_add_child(lv, who);
        sh->who_label = who;
        sh->lock_btn = tk_button_new("ロック");
        tk_button_set_on_click(sh->lock_btn, on_lock_btn, sh);
        tk_set_visible(sh->lock_btn, false);
        tk_add_child(lv, sh->lock_btn);
    }
    tk_set_visible(lv, false);
    tk_add_child(main_area, lv);

    /* --- quick settings: floating panel (UI-REDESIGN 2.3) --- */
    tk_widget *quick = tk_fixed_new();
    sh->quick_panel = quick;
    tk_widget_set_backdrop(quick, true, -1);
    {
        /* Wi-Fi tile: full-width, icon + label + state caption,
         * one overlay button for the whole tile. UI-STYLE-v2:
         * the tile is a full pill whose face is the tonal
         * container colour while Wi-Fi is on (the Android QS /
         * ChromeOS QS tile language) */
        tk_widget *tile = tk_fixed_new();
        sh->wifi_tile = tile;
        tk_widget_set_backdrop(tile, false,
                               style_v2() ? 999
                               : style_winui()
                                   ? tk_theme_default()->radius_card
                                   : 4);
        if ((style_v2() || style_winui()) && sh->wifi)
            tk_widget_set_backdrop_face(tile,
                                        &tk_theme_default()->container);
        tk_set_fixed_rect(tile, (tk_rect){ 12, 12, 336, 72 });
        tk_widget *wic = tk_imageview_new();
        /* 24px optical box, centred in the tile's icon column
         * (the same optical size as the tray icons) */
        tk_set_fixed_rect(wic, (tk_rect){ 20, 24, 24, 24 });
        tk_add_child(tile, wic);
        sh->wifi_tile_icon = wic;
        tk_widget *wl = tk_label_new("Wi-Fi");
        tk_set_fixed_rect(wl, (tk_rect){ 64, 10, 200, 24 });
        tk_add_child(tile, wl);
        sh->wifi_label = tk_label_new("未接続");
        tk_widget_set_text_style(sh->wifi_label, TK_TEXT_CAPTION);
        tk_set_fixed_rect(sh->wifi_label, (tk_rect){ 64, 38, 260, 20 });
        tk_add_child(tile, sh->wifi_label);
        sh->wifi_tile_btn = tk_button_new("");
        tk_button_set_flat(sh->wifi_tile_btn, true);
        tk_set_fixed_rect(sh->wifi_tile_btn, (tk_rect){ 0, 0, 336, 72 });
        tk_button_set_on_click(sh->wifi_tile_btn, on_wifi_tile, sh);
        tk_add_child(tile, sh->wifi_tile_btn);
        tk_add_child(quick, tile);

        /* slider rows: brightness and volume. UI v2 refinement:
         * the row icons are the symbolic glyph family (sun /
         * speaker, monochrome ink - the ChromeOS QS arrangement),
         * not theme files: the bundled Papirus "brightness" is a
         * multicolour disc that read as a pie at 24px. Geometry
         * on the 8px rhythm: icon column 12..36, slider 44..296,
         * value 304..348 - the value's right edge now aligns
         * with the tile's right edge (348) instead of overhanging
         * the panel's inner edge by 4px. */
        struct { tk_widget **slider; tk_widget **value;
                 float v; tk_notify_cb cb; } rows[2] = {
            { &sh->bright_slider, &sh->bright_value,
              sh->brightness / 100.0f, on_bright_change },
            { &sh->vol_slider, &sh->vol_value,
              sh->volume / 100.0f, on_volume_change },
        };
        /* row icons come from the mode's icon set; the volume
         * row icon then follows the effective level through
         * volume_icon_update (it used to be drawn once at a
         * fixed 60 and never updated - REPORT section 1) */
        for (int i = 0; i < 2; i++) {
            int y = 96 + i * 48;
            tk_widget *ic = tk_imageview_new();
            if (i == 0)
                shell_set_icon(ic, NULL, 0, icon_set(),
                               style_winui() ? "weather_sunny"
                                             : "brightness_6",
                               icon_ink(), 24);
            tk_set_fixed_rect(ic, (tk_rect){ 12, y + 12, 24, 24 });
            tk_add_child(quick, ic);
            if (i == 1) sh->qs_vol_icon = ic;
            tk_widget *sl = tk_slider_new(rows[i].v);
            tk_slider_set_on_change(sl, rows[i].cb, sh);
            tk_set_fixed_rect(sl, (tk_rect){ 44, y, 252, 48 });
            tk_add_child(quick, sl);
            *rows[i].slider = sl;
            tk_widget *val = tk_label_new("");
            tk_set_fixed_rect(val, (tk_rect){ 304, y + 12, 44, 24 });
            tk_add_child(quick, val);
            *rows[i].value = val;
        }
        sh->quick_settings_btn = tk_button_new("設定を開く");
        tk_button_set_on_click(sh->quick_settings_btn, on_open_settings, sh);
        if (style_v2()) {
            /* tonal pill (container face), no outline */
            tk_button_set_flat(sh->quick_settings_btn, true);
            tk_button_set_face(sh->quick_settings_btn,
                               &tk_theme_default()->container);
            tk_button_set_corner_radius(sh->quick_settings_btn, 999);
        }
        tk_set_fixed_rect(sh->quick_settings_btn,
                          (tk_rect){ 12, 204, 140, 48 });
        tk_add_child(quick, sh->quick_settings_btn);
        sh->quick_lock_btn = tk_button_new("ロック");
        tk_button_set_on_click(sh->quick_lock_btn, on_lock_btn, sh);
        tk_set_fixed_rect(sh->quick_lock_btn,
                          (tk_rect){ 360 - 12 - 96, 204, 96, 48 });
        tk_set_visible(sh->quick_lock_btn, false);
        tk_add_child(quick, sh->quick_lock_btn);
    }
    tk_set_visible(quick, false);
    tk_add_child(main_area, quick);

    /* --- notification sheet (clock tap, UI-REDESIGN 2.3) --- */
    tk_widget *sheet = tk_fixed_new();
    sh->sheet_panel = sheet;
    tk_widget_set_backdrop(sheet, true, -1);
    {
        tk_widget *head = tk_label_new("通知");
        tk_widget_set_text_style(head, TK_TEXT_TITLE);
        tk_set_fixed_rect(head, (tk_rect){ 16, 10, 200, 28 });
        tk_add_child(sheet, head);
        for (int i = 0; i < BD_NOTIFY_MAX; i++) {
            sh->sheet_labels[i] = tk_label_new("");
            tk_set_fixed_rect(sh->sheet_labels[i],
                              (tk_rect){ 16, 44 + i * 28, 328, 24 });
            tk_set_visible(sh->sheet_labels[i], false);
            tk_add_child(sheet, sh->sheet_labels[i]);
        }
        tk_widget *dis = tk_button_new("すべて消去");
        tk_button_set_on_click(dis, on_dismiss_btn, sh);
        tk_set_fixed_rect(dis, (tk_rect){ 360 - 12 - 120, 160, 120, 48 });
        tk_add_child(sheet, dis);
    }
    tk_set_visible(sheet, false);
    tk_add_child(main_area, sheet);

    /* --- app switcher (recents): floating panel whose cards
     * are built on open from the window bridge (the WM's list,
     * not the shell's launch model). Geometry in bd_shell_layout. */
    {
        tk_widget *rp = tk_fixed_new();
        sh->recents_panel = rp;
        tk_widget_set_backdrop(rp, true, -1);
        tk_widget *head = tk_label_new("最近使ったアプリ");
        tk_widget_set_text_style(head, TK_TEXT_TITLE);
        tk_set_fixed_rect(head, (tk_rect){ 16, 10, 300, 28 });
        tk_add_child(rp, head);
        sh->recents_empty = tk_label_new("起動中のアプリはありません");
        tk_set_fixed_rect(sh->recents_empty,
                          (tk_rect){ 16, 52, 420, 24 });
        tk_add_child(rp, sh->recents_empty);
        sh->recents_box = tk_fixed_new();
        tk_add_child(rp, sh->recents_box);
        tk_set_visible(rp, false);
        tk_add_child(main_area, rp);
    }

    /* toast: top-centre floating card over the desktop */
    sh->toast_label = tk_label_new("");
    tk_widget_set_backdrop(sh->toast_label, false, 8);
    tk_set_visible(sh->toast_label, false);
    tk_add_child(main_area, sh->toast_label);

    /* assist overlay floats above both views (added last) */
    sh->assist = bd_assist_new(on_assist, sh);
    tk_add_child(main_area, bd_assist_layer(sh->assist));

    /* assemble top in panel-position order; the band cover
     * sits under the panel and over the main area in both
     * arrangements (its rect never overlaps the main area) */
    if (sh->panel_top) {
        tk_add_child(top, sh->band_cover);
        tk_add_child(top, panel);
        tk_add_child(top, main_area);
    } else {
        tk_add_child(top, main_area);
        tk_add_child(top, sh->band_cover);
        tk_add_child(top, panel);
    }

    sh->root = tk_root_new(top);

    /* OSK: a full-screen Fixed in its own root holding only the
     * keyboard strip at the bottom. It starts closed; the
     * session's per-step bd_shell_osk_update() drives it. */
    sh->osk_top = tk_fixed_new();
    sh->osk = tk_osk_new();
    tk_add_child(sh->osk_top, sh->osk);
    sh->osk_sink = tk_sink_new(osk_sink_fwd, sh);
    sh->osk_root = tk_root_new(sh->osk_top);
    /* the OSK is the only focusable widget in its root: focus it
     * once, up front, so the first key tap does not pay a
     * focus-change repaint of the whole strip */
    (void)tk_focus(sh->osk_root, sh->osk);
    osk_reposition(sh);

    bd_shell_set_volume(sh, sh->volume); /* fills the label; persists same value */
    wifi_icon_update(sh);                /* initial tray + tile icons */
    bd_shell_set_battery(sh, sh->battery, sh->charging);
    bd_shell_set_brightness(sh, sh->brightness);
    bd_shell_refresh_apps(sh);
    panel_tasks_rebuild(sh);
    return sh;
}

void bd_shell_free(bd_shell *sh)
{
    if (!sh) return;
    /* assist struct first: its layer belongs to the widget tree
     * and is freed with it below */
    bd_assist_free(sh->assist);
    tk_root_free(sh->osk_root);
    tk_widget_free(sh->osk_top);
    tk_widget_free(sh->osk_sink);
    tk_root_free(sh->root);
    tk_widget_free(sh->top);
    for (size_t i = 0; i < sh->n_tasks; i++) { free(sh->tasks[i].id); free(sh->tasks[i].name); }
    for (size_t i = 0; i < sh->n_notify; i++) {
        free(sh->notify[i].text);
        free(sh->notify[i].app);
    }
    free(sh->results);
    ic_cache_free(sh->icons);
    if (sh->font_owned) tk_font_free(sh->font); /* also uninstalls */
    free(sh);
}

tk_root *bd_shell_root(bd_shell *sh) { return sh->root; }
tk_widget *bd_shell_top(bd_shell *sh) { return sh->top; }

/* ---------------- on-screen keyboard ---------------- */

static bool osk_sink_fwd(void *ud, tk_event *ev)
{
    bd_shell *sh = ud;
    if (!sh->osk_sink_cb) return false;
    return sh->osk_sink_cb(sh->osk_sink_ud, ev);
}

/* Natural height of the OSK in its current mode at the shell's
 * width: the lowest layout-cell bottom. The cells are the single
 * source of truth for geometry (osk.h), so this stays correct
 * across modes without duplicating the row constants here. The
 * widget must currently be arranged at least at natural height;
 * osk_reposition arranges it tall first when unsure. */
static int osk_natural_h(bd_shell *sh)
{
    osk_cell cells[128];
    int n = tk_osk_layout(sh->osk, cells, 128);
    tk_rect wr = tk_widget_rect(sh->osk);
    int bottom = 0;
    for (int i = 0; i < n; i++) {
        int b = cells[i].r.y + cells[i].r.h - wr.y;
        if (b > bottom) bottom = b;
    }
    return bottom > 0 ? bottom : wr.h;
}

static void osk_reposition(bd_shell *sh)
{
    if (!sh->osk) return;
    /* arrange tall once so the layout cells are built at natural
     * row heights, measure, then anchor the strip at the bottom */
    tk_set_fixed_rect(sh->osk, (tk_rect){ 0, 0, sh->w, sh->h });
    tk_root_layout(sh->osk_root, sh->w, sh->h);
    int h = osk_natural_h(sh);
    if (h > sh->h) h = sh->h;
    sh->osk_h = h;
    tk_set_fixed_rect(sh->osk, (tk_rect){ 0, sh->h - h, sh->w, h });
    tk_root_layout(sh->osk_root, sh->w, sh->h);
}

tk_root *bd_shell_osk_root(bd_shell *sh) { return sh->osk_root; }
tk_widget *bd_shell_osk_widget(bd_shell *sh) { return sh->osk; }
bool bd_shell_osk_open(const bd_shell *sh) { return sh->osk_open; }
int bd_shell_osk_height(const bd_shell *sh) { return sh->osk_h; }

tk_rect bd_shell_osk_rect(const bd_shell *sh)
{
    if (!sh->osk_open) return (tk_rect){ 0, 0, 0, 0 };
    return tk_widget_rect(sh->osk);
}

void bd_shell_osk_set_sink(bd_shell *sh, bd_shell_osk_sink_cb cb,
                           void *userdata)
{
    sh->osk_sink_cb = cb;
    sh->osk_sink_ud = userdata;
}

void bd_shell_osk_set_backend(bd_shell *sh, const osk_ime_backend *be,
                              void *engine)
{
    tk_osk_set_backend(sh->osk, be, engine);
}

/* Clear a composition the remote client may still be showing:
 * the OSK is leaving (or changing target) and the client field
 * must not keep a stale preedit. Goes through the same sink as
 * every other composition update. */
static void osk_clear_remote_composition(bd_shell *sh)
{
    if (!sh->osk_sink_cb) return;
    tk_event ev = {0};
    ev.type = TK_EV_COMPOSITION;
    ev.text = NULL; /* NULL = clear */
    (void)sh->osk_sink_cb(sh->osk_sink_ud, &ev);
}

static void osk_close(bd_shell *sh)
{
    if (!sh->osk_open) return;
    if (sh->osk_kind == 2) osk_clear_remote_composition(sh);
    sh->osk_open = false;
}

bool bd_shell_osk_update(bd_shell *sh, bool remote_active,
                         uint32_t remote_serial)
{
    tk_widget *fw = tk_focused(sh->root);
    tk_widget_kind k = fw ? tk_widget_kind_of(fw) : TK_W_ROOT;
    bool local = fw && (k == TK_W_TEXTFIELD || k == TK_W_TEXTVIEW);
    int kind = local ? 1 : (remote_active ? 2 : 0);
    tk_widget *idw = local ? fw : NULL;
    /* target identity: the widget for a local editor, the WM's
     * IM serial for a remote one - the serial changes even when
     * a focus cycle (inactive -> active) happened entirely
     * between two samples, which a plain active flag cannot
     * distinguish from "same field throughout" */
    bool same = kind == sh->osk_kind &&
                (kind != 1 || idw == sh->osk_idw) &&
                (kind != 2 || remote_serial == sh->osk_serial);
    if (!same) {
        /* a new input target clears the physical-key latch */
        if (sh->osk_open && sh->osk_kind == 2 && kind != 2)
            osk_clear_remote_composition(sh);
        sh->osk_kind = kind;
        sh->osk_idw = idw;
        sh->osk_serial = remote_serial;
        sh->osk_suppress = false;
    }
    bool want = kind != 0 && !sh->osk_suppress;
    if (want) {
        tk_widget *target = local ? fw : sh->osk_sink;
        if (tk_osk_target(sh->osk) != target)
            tk_osk_attach(sh->osk, target);
        if (!sh->osk_open) {
            sh->osk_open = true;
            osk_reposition(sh);
        } else {
            /* a mode switch while open changes the height */
            int h = osk_natural_h(sh);
            if (h != sh->osk_h) osk_reposition(sh);
        }
    } else {
        osk_close(sh);
    }
    return sh->osk_open;
}

void bd_shell_osk_suppress(bd_shell *sh)
{
    sh->osk_suppress = true;
    osk_close(sh);
}

/* ---------------- layout ---------------- */

tk_rect bd_shell_main_rect(const bd_shell *sh) { return sh->main_rect; }

bool bd_shell_desktop_grid_rect(const bd_shell *sh, tk_rect *out)
{
    if (!sh || !out || !sh->desk_grid_valid) return false;
    *out = sh->desk_grid_rect;
    return true;
}

/* Position the launcher's inner sections for panel rect w x h
 * (launcher-local coordinates). The middle area (home sections or
 * the results grid) sits between the search field and the chip
 * row; the footer is the last 48px. */
static void launcher_layout(bd_shell *sh, int w, int h)
{
    /* Vertical rhythm (UI v2 refinement, 8px grid): search at
     * 12..60, middle from 68, footer band = the last 48px,
     * chips directly above the footer with an 8px gap. */
    tk_set_fixed_rect(sh->search_field, (tk_rect){ 16, 12, w - 32, 48 });
    /* category chips: 40px pills on an even rhythm - 8px gaps
     * horizontally and vertically (the pilot used 4px, which
     * made the wrap look ragged); rows flow left-to-right,
     * wrapping at the panel's inner width. WinUI Start has no
     * category chip row (search + pinned + recommended only),
     * so the row is hidden and its height reclaimed. */
    tk_set_visible(sh->chip_box, !style_winui());
    int chip_h = 0;
    if (!style_winui()) {
        int x = 0, y = 0;
        for (size_t i = 0; i < tk_child_count(sh->chip_box); i++) {
            tk_widget *chip = tk_child_at(sh->chip_box, i);
            tk_size ms = tk_measure(chip,
                (tk_constraints){ 0, TK_UNLIMITED, 0, TK_UNLIMITED });
            if (x > 0 && x + ms.w > w - 32) { x = 0; y += 48; }
            tk_set_fixed_rect(chip, (tk_rect){ x, y, ms.w, 40 });
            x += ms.w + 8;
        }
        chip_h = y + 40;
    }
    int mid_y = 68, mid_h = h - 48 - chip_h - 8 - mid_y;
    if (mid_h < 0) mid_h = 0;
    tk_set_fixed_rect(sh->chip_box,
                      (tk_rect){ 16, h - 48 - chip_h, w - 32, chip_h });
    tk_set_fixed_rect(sh->home_box, (tk_rect){ 0, mid_y, w, mid_h });
    /* home sections: pin heading (28) + 8 + the pin grid (76px
     * rows); recents take what remains under their heading and
     * hide when it cannot hold a row (an empty framed box under
     * the heading reads as a defect). Whatever the home sections
     * leave of the middle area goes to the all-apps grid - the
     * ChromeOS launcher shape (suggestions on top, the full
     * library scrolling below) - so the panel never shows a
     * blank void under a short pin list. */
    int home_h = 0;
    {
        int rows = (int)((tk_child_count(sh->pin_grid) + 3) / 4);
        if (rows < 1) rows = 1;
        if (rows > 2) rows = 2;
        /* never let the grid spill into the chip row: when the
         * middle area cannot hold all pin rows (more chip rows,
         * smaller panel), show only the rows that fit and hide
         * the remaining cells - they stay on the shelf and in
         * the all-apps grid */
        int fit = (mid_h - 36) / 76;
        if (fit < 1) fit = 1;
        if (rows > fit) rows = fit;
        for (size_t i = 0; i < tk_child_count(sh->pin_grid); i++)
            tk_set_visible(tk_child_at(sh->pin_grid, i),
                           (int)i < rows * 4);
        int grid_h = rows * 76;
        tk_set_fixed_rect(sh->pin_grid, (tk_rect){ 16, 36, w - 32, grid_h });
        int rec_y = 36 + grid_h + 8;
        int list_h = mid_h - (rec_y + 36);
        bool show_rec = list_h >= 40 && ln_recent_count(sh->store) > 0;
        tk_set_visible(sh->rec_head, show_rec);
        tk_set_visible(sh->recent_list, show_rec);
        if (show_rec) {
            tk_set_fixed_rect(sh->rec_head,
                              (tk_rect){ 16, rec_y, 200, 28 });
            tk_set_fixed_rect(sh->recent_list,
                              (tk_rect){ 16, rec_y + 36, w - 32, list_h });
            home_h = rec_y + 36 + list_h;
        } else {
            home_h = 36 + grid_h;
        }
    }
    if (sh->home_mode) {
        int rem = mid_h - home_h - 8;
        if (rem >= 64) {
            tk_set_visible(sh->results_scroll, true);
            tk_set_fixed_rect(sh->results_scroll,
                              (tk_rect){ 16, mid_y + home_h + 8,
                                         w - 32, rem });
        } else {
            tk_set_visible(sh->results_scroll, false);
        }
    } else {
        tk_set_fixed_rect(sh->results_scroll,
                          (tk_rect){ 16, mid_y, w - 32, mid_h });
    }
    /* results + pin grids: cells take an EQUAL SHARE of the
     * panel's inner width (the ChromeOS launcher form - the
     * icon and caption centre in their share), so a short row
     * spans the panel instead of huddling at the left with a
     * void at the right. The 76px minimum cell drives the
     * column count; the share widens the cells, never the
     * icons. */
    {
        int cols = (w - 32) / 76;
        if (cols < 1) cols = 1;
        int cell_w = (w - 32) / cols;
        for (size_t i = 0; i < tk_child_count(sh->results_fixed); i++) {
            int cx = (int)(i % (size_t)cols) * cell_w;
            int cy = (int)(i / (size_t)cols) * 84;
            tk_set_fixed_rect(tk_child_at(sh->results_fixed, i),
                              (tk_rect){ cx, cy, cell_w, 84 });
        }
        /* pin grid cells: 4 columns sharing the full width */
        int pin_cell_w = (w - 32) / 4;
        for (size_t i = 0; i < tk_child_count(sh->pin_grid); i++) {
            int cx = (int)(i % 4) * pin_cell_w;
            int cy = (int)(i / 4) * 76;
            tk_set_fixed_rect(tk_child_at(sh->pin_grid, i),
                              (tk_rect){ cx, cy, pin_cell_w, 76 });
        }
    }
    /* footer (48px band): avatar disc + user name centred on
     * the band at the left, lock button at the right */
    tk_set_fixed_rect(sh->who_avatar, (tk_rect){ 16, h - 40, 32, 32 });
    {
        tk_size ms = tk_measure(sh->who_label,
            (tk_constraints){ 0, TK_UNLIMITED, 0, TK_UNLIMITED });
        tk_set_fixed_rect(sh->who_label,
                          (tk_rect){ 56, h - 24 - ms.h / 2, 240, ms.h });
    }
    tk_set_fixed_rect(sh->lock_btn,
                      (tk_rect){ w - 16 - 96, h - 48, 96, 48 });
}

static void recents_rebuild(bd_shell *sh);

void bd_shell_layout(bd_shell *sh, int w, int h)
{
    /* top is a Fixed container: position the shelf and the main
     * area explicitly. Classic (panel_margin 0): the shelf is
     * flush with the screen edge and the main area fills the
     * rest - exactly the geometry the old vertical box produced.
     * UI-STYLE-v2: the shelf floats with panel_margin around it
     * (ChromeOS shelf) and the desktop extends underneath it. */
    const tk_theme *th = tk_theme_default();
    int mgn = th->panel_margin;
    tk_size psz = tk_measure(sh->panel,
                             (tk_constraints){ 0, w, 0, TK_UNLIMITED });
    int ph = psz.h;
    if (sh->panel_top) {
        tk_set_fixed_rect(sh->panel, (tk_rect){ mgn, mgn, w - 2 * mgn, ph });
        tk_set_fixed_rect(sh->main_area,
                          (tk_rect){ 0, ph + mgn, w, h - ph - mgn });
        /* band strip: the panel's whole row, edge to edge */
        tk_set_fixed_rect(sh->band_cover, (tk_rect){ 0, 0, w, ph + mgn });
    } else {
        tk_set_fixed_rect(sh->panel,
                          (tk_rect){ mgn, h - mgn - ph, w - 2 * mgn, ph });
        tk_set_fixed_rect(sh->main_area, (tk_rect){ 0, 0, w, h - ph - mgn });
        tk_set_fixed_rect(sh->band_cover,
                          (tk_rect){ 0, h - ph - mgn, w, ph + mgn });
    }

    /* WinUI: size the fixed left spacer so the Start + task
     * cluster centres on the SCREEN (eval F-12). The sizing
     * is a feedback correction on the PREVIOUS layout's
     * arranged rects (the session lays out every step, so
     * it settles exactly, with no dependence on measured
     * desired widths); the tray's arranged left edge caps
     * the spacer so the cluster can never push into it. */
    if (style_winui() && sh->win_sp1 && sh->launcher_btn &&
        sh->task_box) {
        tk_rect lr = tk_widget_rect(sh->launcher_btn);
        if (lr.w > 0) {
            tk_rect tr = tk_widget_rect(sh->task_box);
            int left = lr.x;
            int right = tr.w > 0 ? tr.x + tr.w : lr.x + lr.w;
            int delta = w / 2 - (left + right) / 2;
            tk_rect sr = tk_widget_rect(sh->win_sp1);
            int nw = (sr.w > 0 ? sr.w : 0) + delta;
            tk_rect wr = tk_widget_rect(sh->tray_wifi_btn);
            if (wr.w > 0) {
                int max_sp1 = (sr.w > 0 ? sr.w : 0) + (wr.x - right);
                if (nw > max_sp1) nw = max_sp1;
            }
            if (nw < 0) nw = 0;
            tk_widget_set_min_size(sh->win_sp1, nw, 0);
        }
    }

    tk_root_layout(sh->root, w, h);
    tk_rect mr = tk_widget_rect(sh->main_area);
    sh->main_rect = mr;
    /* In v2 the wallpaper (and with it the desktop surface)
     * extends under the floating shelf, so the shelf's rounded
     * corners and shadow sit on the desktop instead of the black
     * canvas clear. */
    int desk_h = mr.h;
    if (style_v2() && !sh->panel_top) desk_h = mr.h + ph + mgn;
    tk_set_fixed_rect(sh->desktop_view, (tk_rect){0, 0, mr.w, desk_h});
    tk_set_fixed_rect(sh->wallpaper_w, (tk_rect){0, 0, mr.w, desk_h});
    /* The icon grid hugs its content (M4): the grid stretches its
     * tracks to fill whatever rect it is given, so handing it the
     * whole desktop turned two icons into room-filling cards. */
    {
        tk_size gs = tk_measure(sh->desk_grid,
                                (tk_constraints){0, TK_UNLIMITED, 0, TK_UNLIMITED});
        int gw = gs.w < mr.w - 32 ? gs.w : mr.w - 32;
        int gh = gs.h < mr.h - 32 ? gs.h : mr.h - 32;
        if (gw < 0) gw = 0;
        if (gh < 0) gh = 0;
        tk_set_fixed_rect(sh->desk_grid, (tk_rect){16, 16, gw, gh});
        /* Screen-coordinate copy for embedders (the mascot
         * floor): the grid sits at (16,16) inside desktop_view,
         * which itself sits at the main area's origin. */
        sh->desk_grid_rect = (tk_rect){ mr.x + 16, mr.y + 16, gw, gh };
        sh->desk_grid_valid = gw > 0 && gh > 0;
    }
    /* launcher: floating panel 600x408 above the panel (bottom
     * panel) or below it (top panel); a full sheet when the main
     * area is too small for the floating form */
    {
        bool floating = mr.w >= 600 && mr.h >= 424;
        int lh = 408;
        /* UI-STYLE-v2: the floating shelf takes 8px of main-area
         * height, which would drop the launcher to a full sheet;
         * keep the floating card, 8px shorter */
        if (!floating && (style_v2() || style_winui()) &&
            mr.w >= 600 && mr.h >= 400) {
            floating = true;
            lh = mr.h - 16;
        }
        if (floating != sh->launcher_floating) {
            sh->launcher_floating = floating;
            tk_widget_set_backdrop(sh->launcher_view, floating,
                                   floating ? -1 : 0);
        }
        tk_rect lr;
        if (floating) {
            int ly = sh->panel_top ? 8 : mr.h - 8 - lh;
            /* WinUI: Start is centred over the taskbar (the
             * Windows 11 default alignment) */
            int lx = style_winui() ? (mr.w - 600) / 2 : 8;
            if (lx < 0) lx = 0;
            lr = (tk_rect){ lx, ly, 600, lh };
        } else {
            lr = (tk_rect){ 0, 0, mr.w, mr.h };
        }
        tk_set_fixed_rect(sh->launcher_view, lr);
        launcher_layout(sh, lr.w, lr.h);
    }
    /* quick settings: 360 wide, anchored to the panel side */
    {
        int qw = mr.w - 16 < 360 ? mr.w - 16 : 360;
        if (qw < 200) qw = mr.w;
        int qx = mr.w - qw - 8;
        int qy = sh->panel_top ? 8 : mr.h - 8 - 264;
        tk_set_fixed_rect(sh->quick_panel, (tk_rect){ qx, qy, qw, 264 });
    }
    /* notification sheet: 360 wide, panel side. Centred in
     * Android mode; WinUI anchors it to the right edge like the
     * Windows notification centre (opened from the clock). */
    {
        int sw = mr.w - 16 < 360 ? mr.w - 16 : 360;
        int sx = style_winui() ? mr.w - sw - 8 : (mr.w - sw) / 2;
        int sy = sh->panel_top ? 8 : mr.h - 8 - 216;
        tk_set_fixed_rect(sh->sheet_panel, (tk_rect){ sx, sy, sw, 216 });
    }
    /* app switcher: a wide centred panel above the shelf
     * (Android's overview is a full-screen surface; at this
     * panel size a centred card area is the device fit - the
     * cards themselves are Android's: thumbnail + title + close) */
    if (sh->recents_open && sh->recents_dirty) {
        sh->recents_dirty = false;
        recents_rebuild(sh);
    }
    {
        int pw = mr.w - 16 < 664 ? mr.w - 16 : 664;
        if (pw < 240) pw = mr.w;
        int ph2 = 368;
        if (ph2 > mr.h - 16) ph2 = mr.h - 16;
        int px = (mr.w - pw) / 2;
        int py = sh->panel_top ? 8 : mr.h - 8 - ph2;
        tk_set_fixed_rect(sh->recents_panel, (tk_rect){ px, py, pw, ph2 });
        tk_set_fixed_rect(sh->recents_box, (tk_rect){ 0, 44, pw, ph2 - 44 });
    }
    /* toast: top centre, at most 480 wide */
    {
        const tk_theme *t = tk_theme_default();
        const char *txt = tk_label_text(sh->toast_label);
        int tw = txt ? tk_text_width(t, txt) + 32 : 0;
        if (tw > 480) tw = 480;
        if (tw > mr.w) tw = mr.w;
        tk_set_fixed_rect(sh->toast_label,
                          (tk_rect){ (mr.w - tw) / 2, 12, tw, 40 });
    }
    bd_assist_layout(sh->assist, mr.w, mr.h);
    tk_root_layout(sh->root, w, h);
    /* the OSK strip tracks the screen size too */
    sh->w = w; sh->h = h;
    osk_reposition(sh);
}

/* ---------------- views + status ---------------- */

bd_view bd_shell_view(const bd_shell *sh) { return sh->view; }

/* ---------------- navigation + app switcher ---------------- */

#define RECENTS_MAX_CARDS 6
#define RECENT_THUMB_W 184
#define RECENT_THUMB_H 104

struct rec_ctx { bd_shell *sh; uint32_t id; };
static struct rec_ctx rec_card_ctxs[RECENTS_MAX_CARDS];
static struct rec_ctx rec_close_ctxs[RECENTS_MAX_CARDS];

static void recents_rebuild(bd_shell *sh);

static void on_rec_card(tk_widget *w, void *ud)
{
    (void)w;
    struct rec_ctx *c = ud;
    if (c->sh->win_ops_set && c->sh->win_ops.activate)
        c->sh->win_ops.activate(c->sh->win_ud, c->id);
    bd_shell_set_recents_open(c->sh, false);
}

static void on_rec_close(tk_widget *w, void *ud)
{
    (void)w;
    struct rec_ctx *c = ud;
    if (c->sh->win_ops_set && c->sh->win_ops.close)
        c->sh->win_ops.close(c->sh->win_ud, c->id);
    /* NOT rebuilt here: this handler runs inside the click
     * dispatch of the very button a rebuild would free. The
     * rebuild is deferred to the next bd_shell_layout (the
     * session lays out every step), and the window may take a
     * moment to exit anyway. */
    c->sh->recents_dirty = true;
}

static void recents_rebuild(bd_shell *sh)
{
    clear_children(sh->recents_box);
    bd_shell_win_entry ents[RECENTS_MAX_CARDS];
    size_t n = 0;
    if (sh->win_ops_set && sh->win_ops.list)
        n = sh->win_ops.list(sh->win_ud, ents, RECENTS_MAX_CARDS);
    tk_set_visible(sh->recents_empty, n == 0);
    /* newest (top of the z-order) first, as in Android recents */
    for (size_t k = 0; k < n; k++) {
        const bd_shell_win_entry *e = &ents[n - 1 - k];
        tk_widget *card = tk_fixed_new();
        tk_widget_set_backdrop(card, false, 8);
        if (e->focused)
            tk_widget_set_backdrop_face(card,
                                        &tk_theme_default()->selection);
        static uint32_t thumb[RECENT_THUMB_W * RECENT_THUMB_H];
        bool have = sh->win_ops.thumbnail &&
                    sh->win_ops.thumbnail(sh->win_ud, e->id, thumb,
                                          RECENT_THUMB_W, RECENT_THUMB_H);
        if (!have) { /* nothing committed yet: a plain card face */
            tk_color sa = tk_theme_default()->surface_alt;
            uint32_t fill = 0xFF000000u | ((uint32_t)sa.r << 16) |
                            ((uint32_t)sa.g << 8) | (uint32_t)sa.b;
            for (int i = 0; i < RECENT_THUMB_W * RECENT_THUMB_H; i++)
                thumb[i] = fill;
        }
        tk_widget *tb = tk_button_new(e->title);
        tk_button_set_icon(tb, thumb, RECENT_THUMB_W, RECENT_THUMB_H);
        tk_button_set_icon_only(tb, true);
        tk_button_set_flat(tb, true);
        rec_card_ctxs[k].sh = sh;
        rec_card_ctxs[k].id = e->id;
        tk_button_set_on_click(tb, on_rec_card, &rec_card_ctxs[k]);
        tk_set_fixed_rect(tb, (tk_rect){ 8, 8, RECENT_THUMB_W,
                                         RECENT_THUMB_H });
        tk_add_child(card, tb);
        tk_widget *tl = tk_label_new(e->title[0] ? e->title : "（無題）");
        tk_set_fixed_rect(tl, (tk_rect){ 8, 118, 140, 20 });
        tk_add_child(card, tl);
        tk_widget *cb = tk_button_new("×");
        tk_button_set_flat(cb, true);
        rec_close_ctxs[k].sh = sh;
        rec_close_ctxs[k].id = e->id;
        tk_button_set_on_click(cb, on_rec_close, &rec_close_ctxs[k]);
        tk_set_fixed_rect(cb, (tk_rect){ 156, 114, 36, 28 });
        tk_add_child(card, cb);
        tk_set_fixed_rect(card, (tk_rect){ 16 + (int)(k % 3) * 208,
                                           8 + (int)(k / 3) * 158,
                                           200, 148 });
        tk_add_child(sh->recents_box, card);
    }
}

bool bd_shell_recents_open(const bd_shell *sh) { return sh->recents_open; }

/* The overlay surfaces that float above the windows, in
 * stacking order. Visibility is the open state: every opener
 * toggles its panel's visibility. */
static size_t overlay_panels(const bd_shell *sh, tk_widget **out)
{
    size_t n = 0;
    if (sh->launcher_view && tk_is_visible(sh->launcher_view))
        out[n++] = sh->launcher_view;
    if (sh->recents_panel && tk_is_visible(sh->recents_panel))
        out[n++] = sh->recents_panel;
    if (sh->quick_panel && tk_is_visible(sh->quick_panel))
        out[n++] = sh->quick_panel;
    if (sh->sheet_panel && tk_is_visible(sh->sheet_panel))
        out[n++] = sh->sheet_panel;
    return n;
}

bool bd_shell_overlay_open(const bd_shell *sh)
{
    tk_widget *p[4];
    return overlay_panels(sh, p) > 0;
}

size_t bd_shell_overlay_rects(const bd_shell *sh, tk_rect *out,
                              size_t max)
{
    tk_widget *p[4];
    size_t n = overlay_panels(sh, p);
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        tk_rect r = tk_widget_rect(p[i]);
        if (r.w > 0 && r.h > 0) out[k++] = r;
    }
    return k;
}

bool bd_shell_overlay_hit(const bd_shell *sh, int x, int y)
{
    tk_widget *p[4];
    size_t n = overlay_panels(sh, p);
    for (size_t i = 0; i < n; i++) {
        tk_rect r = tk_widget_rect(p[i]);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return true;
    }
    return false;
}

void bd_shell_set_recents_open(bd_shell *sh, bool open)
{
    sh->recents_open = open;
    tk_set_visible(sh->recents_panel, open);
    if (open) {
        bd_shell_set_quick_open(sh, false);
        sh->sheet_open = false;
        tk_set_visible(sh->sheet_panel, false);
        if (sh->view == BD_VIEW_LAUNCHER)
            bd_shell_show_launcher(sh, false);
        recents_rebuild(sh);
    }
    shell_relayout(sh);
}

void bd_shell_set_nav_sink(bd_shell *sh, bd_shell_nav_cb cb, void *userdata)
{
    sh->nav_cb = cb;
    sh->nav_ud = userdata;
}

void bd_shell_set_win_ops(bd_shell *sh, const bd_shell_win_ops *ops,
                          void *userdata)
{
    if (ops) {
        sh->win_ops = *ops;
        sh->win_ops_set = true;
    } else {
        memset(&sh->win_ops, 0, sizeof(sh->win_ops));
        sh->win_ops_set = false;
    }
    sh->win_ud = userdata;
}

void bd_shell_nav(bd_shell *sh, bd_nav_action act)
{
    switch (act) {
    case BD_NAV_BACK:
        /* Shell panels consume Back first (Android's dismissal
         * order); only with nothing open does Back leave for
         * the focused window through the sink. */
        if (sh->recents_open) { bd_shell_set_recents_open(sh, false); return; }
        if (sh->quick_open) { bd_shell_set_quick_open(sh, false); return; }
        if (sh->sheet_open) {
            sh->sheet_open = false;
            tk_set_visible(sh->sheet_panel, false);
            return;
        }
        if (sh->view == BD_VIEW_LAUNCHER) {
            bd_shell_show_launcher(sh, false);
            return;
        }
        if (sh->nav_cb) sh->nav_cb(sh->nav_ud, act);
        break;
    case BD_NAV_HOME:
        bd_shell_set_recents_open(sh, false);
        bd_shell_set_quick_open(sh, false);
        sh->sheet_open = false;
        tk_set_visible(sh->sheet_panel, false);
        bd_shell_show_launcher(sh, false);
        /* the session minimizes the WM windows so the desktop
         * is actually reached */
        if (sh->nav_cb) sh->nav_cb(sh->nav_ud, act);
        break;
    case BD_NAV_RECENTS:
        bd_shell_set_recents_open(sh, !sh->recents_open);
        break;
    }
}

void bd_shell_show_launcher(bd_shell *sh, bool show)
{
    sh->view = show ? BD_VIEW_LAUNCHER : BD_VIEW_DESKTOP;
    tk_set_visible(sh->launcher_view, show);
    if (show) {
        bd_shell_set_quick_open(sh, false);
        bd_shell_set_recents_open(sh, false);
        sh->sheet_open = false;
        tk_set_visible(sh->sheet_panel, false);
        tk_focus(sh->root, sh->search_field);
    }
    shell_relayout(sh);
}

void bd_shell_set_clock(bd_shell *sh, const char *hhmm)
{
    tk_button_set_label(sh->clock_btn, hhmm);
}

void bd_shell_set_battery(bd_shell *sh, int pct, bool charging)
{
    sh->battery = pct;
    sh->charging = charging;
    if (!tk_is_visible(sh->tray_batt_btn))
        tk_set_visible(sh->tray_batt_btn, true);
    /* one composite element: the percentage is the battery
     * button's own label; the level step and the charging bolt
     * are the icon file's shape - no "+" suffix anywhere
     * (REPORT-status-icons). Steps: Material battery_android_0..6
     * + _full (15% bands), Fluent battery_0..10 (10% bands);
     * charging swaps in the set's bolt file; <= 20% tints
     * danger; unknown keeps an empty frame, dimmed. */
    char buf[32];
    if (pct < 0) snprintf(buf, sizeof(buf), "--%%");
    else snprintf(buf, sizeof(buf), "%d%%", pct);
    tk_button_set_label(sh->tray_batt_btn, buf);
    char nm[32];
    const char *name;
    tk_color tint = icon_ink();
    if (pct < 0) {
        name = style_winui() ? "battery_0" : "battery_android_frame_full";
        tint = icon_dim();
    } else if (charging) {
        name = style_winui() ? "battery_charge" : "battery_android_bolt";
    } else if (style_winui()) {
        int idx = (pct + 5) / 10;
        if (idx > 10) idx = 10;
        snprintf(nm, sizeof(nm), "battery_%d", idx);
        name = nm;
        if (pct <= 20) tint = icon_danger();
    } else {
        if (pct >= 95) {
            name = "battery_android_full";
        } else {
            int idx = pct / 15;
            if (idx > 6) idx = 6;
            snprintf(nm, sizeof(nm), "battery_android_%d", idx);
            name = nm;
        }
        if (pct <= 20) tint = icon_danger();
    }
    shell_set_icon(sh->tray_batt_btn, sh->ic_tray_batt,
                   sizeof(sh->ic_tray_batt), icon_set(), name, tint, 24);
}

int bd_shell_battery(const bd_shell *sh) { return sh->battery; }

/* The tray icon shows the EFFECTIVE level, so mute is visible
 * at all times (the muted file carries the x / slash). The
 * quick-settings row icon follows the same state. Steps are
 * the sets' own: mute file, one-wave file (<= 50), two-wave
 * file (> 50). */
static void volume_icon_update(bd_shell *sh)
{
    int eff = sh->muted ? 0 : sh->volume;
    const char *name;
    if (style_winui())
        name = eff <= 0 ? "speaker_mute"
             : eff <= 50 ? "speaker_1" : "speaker_2";
    else
        name = eff <= 0 ? "volume_off"
             : eff <= 50 ? "volume_down" : "volume_up";
    shell_set_icon(sh->tray_vol_btn, sh->ic_tray_vol,
                   sizeof(sh->ic_tray_vol), icon_set(), name,
                   icon_ink(), 24);
    if (sh->qs_vol_icon)
        shell_set_icon(sh->qs_vol_icon, sh->ic_qs_vol,
                       sizeof(sh->ic_qs_vol), icon_set(), name,
                       icon_ink(), 24);
}

/* Refresh both Wi-Fi icons (tray + QS tile) from the current
 * state: radio on/off, connection, live signal. Called from
 * bd_shell_set_wifi and bd_shell_poll_wifi - the two places the
 * inputs change. Fluent numbers its fans from the top
 * (wifi_1 = full fan, wifi_4 = bare dot - measured from the
 * source renders, REPORT section 8), so the level order is
 * inverted for that set. An unknown reading shows the full
 * fan: connection is a fact, the strength is not invented. */
static void wifi_icon_update(bd_shell *sh)
{
    const char *name;
    tk_color tint = icon_ink();
    if (!sh->wifi) {
        name = "wifi_off";
        tint = icon_dim();
    } else if (!sh->wifi_connected) {
        name = style_winui() ? "wifi_warning" : "signal_wifi_0_bar";
        tint = icon_dim();
    } else if (style_winui()) {
        static const char *names[4] = {
            "wifi_4", "wifi_3", "wifi_2", "wifi_1"
        };
        name = sh->wifi_signal_dbm == 0
                   ? "wifi_1"
                   : names[wifi_level_from_dbm(sh->wifi_signal_dbm)];
    } else {
        static const char *names[4] = {
            "network_wifi_1_bar", "network_wifi_2_bar",
            "network_wifi_3_bar", "network_wifi"
        };
        name = sh->wifi_signal_dbm == 0
                   ? "network_wifi"
                   : names[wifi_level_from_dbm(sh->wifi_signal_dbm)];
    }
    shell_set_icon(sh->tray_wifi_btn, sh->ic_tray_wifi,
                   sizeof(sh->ic_tray_wifi), icon_set(), name, tint, 24);
    if (sh->wifi_tile_icon)
        shell_set_icon(sh->wifi_tile_icon, sh->ic_tile_wifi,
                       sizeof(sh->ic_tile_wifi), icon_set(), name,
                       tint, 24);
}

void bd_shell_set_volume(bd_shell *sh, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    /* Unmute-on-adjust: an explicit change to a DIFFERENT level
     * clears mute (every input path - session keys, QS slider,
     * assist buttons - funnels through here, so the rule lives
     * in exactly one place). Setting the same level (the
     * creation-time label fill) leaves mute alone. */
    if (pct != sh->volume && sh->muted) {
        sh->muted = false;
        reg_set("shell", "muted", "false");
    }
    sh->volume = pct;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    tk_label_set_text(sh->vol_value, buf);
    tk_slider_set_value(sh->vol_slider, pct / 100.0f);
    volume_icon_update(sh);
    char v[16];
    snprintf(v, sizeof(v), "%d", pct);
    reg_set("shell", "volume", v);
    if (sh->backend)
        bd_backend_volume_set(sh->backend, sh->muted ? 0 : pct);
    bd_assist_set_values(sh->assist, sh->volume, sh->brightness);
}

int bd_shell_volume(const bd_shell *sh) { return sh->volume; }

void bd_shell_set_muted(bd_shell *sh, bool muted)
{
    if (sh->muted == muted) return;
    sh->muted = muted;
    reg_set("shell", "muted", muted ? "true" : "false");
    volume_icon_update(sh);
    /* The backend has no mute switch in this stack (it writes
     * the ALSA Master value), so the effective level is the
     * mute: 0 while muted, the preserved level on unmute. The
     * QS slider/label and the assist line keep showing the set
     * level - it is what unmute restores. */
    if (sh->backend)
        bd_backend_volume_set(sh->backend, muted ? 0 : sh->volume);
}

bool bd_shell_muted(const bd_shell *sh) { return sh->muted; }

int bd_shell_effective_volume(const bd_shell *sh)
{
    return sh->muted ? 0 : sh->volume;
}

bool bd_shell_quick_open(const bd_shell *sh) { return sh->quick_open; }

void bd_shell_set_quick_open(bd_shell *sh, bool open)
{
    sh->quick_open = open;
    tk_set_visible(sh->quick_panel, open);
    /* the assist button yields the bottom-right corner to the
     * quick panel (its footer buttons live there) */
    bd_assist_set_avoid_right(sh->assist, open ? 368 : 0);
    if (sh->main_rect.w > 0)
        bd_assist_layout(sh->assist, sh->main_rect.w, sh->main_rect.h);
    if (open) {
        /* footer buttons appear only when they can act */
        tk_set_visible(sh->quick_lock_btn, sh->lock_cb != NULL);
        sh->sheet_open = false;
        tk_set_visible(sh->sheet_panel, false);
        bd_shell_set_recents_open(sh, false);
    }
    shell_relayout(sh);
}

void bd_shell_set_wifi(bd_shell *sh, bool on)
{
    sh->wifi = on;
    reg_set("shell", "wifi", on ? "true" : "false");
    /* M3: the toggle is real when a supplicant handle is attached.
     * Failures are silent (offline-by-default device; the status
     * line will simply keep showing 未接続). */
    if (sh->wifi_ctl) {
        if (on) (void)bd_wifi_reconnect(sh->wifi_ctl);
        else (void)bd_wifi_disconnect(sh->wifi_ctl);
        bd_shell_poll_wifi(sh);
    }
    if (!on) tk_label_set_text(sh->wifi_label, "未接続");
    /* UI-STYLE-v2: the QS tile face follows the state (tonal
     * container while on, plain surface while off). The face is
     * on the tile's backdrop, under the icon/labels - the
     * overlay button itself stays transparent. */
    if ((style_v2() || style_winui()) && sh->wifi_tile)
        tk_widget_set_backdrop_face(sh->wifi_tile,
                                    on ? &tk_theme_default()->container
                                       : NULL);
    if (!on) { /* the radio is off: nothing is connected */
        sh->wifi_connected = false;
        sh->wifi_signal_dbm = 0;
    }
    wifi_icon_update(sh);
}

void bd_shell_attach_wifi(bd_shell *sh, bd_wifi *w)
{
    sh->wifi_ctl = w;
    if (!w) return;
    /* Apply the saved preference once: the reg value is the
     * user's intent, the supplicant state is volatile. */
    if (sh->wifi) (void)bd_wifi_reconnect(w);
    else (void)bd_wifi_disconnect(w);
    bd_shell_poll_wifi(sh);
}

int bd_shell_poll_wifi(bd_shell *sh)
{
    if (!sh->wifi_ctl) return 0;
    bd_wifi_status st;
    if (bd_wifi_get_status(sh->wifi_ctl, &st) < 0) {
        sh->wifi_connected = false;
        sh->wifi_signal_dbm = 0;
        sh->wifi_ssid[0] = 0;
        tk_label_set_text(sh->wifi_label, "未接続");
        wifi_icon_update(sh);
        return 0;
    }
    sh->wifi_connected = st.connected;
    /* live strength of the connected AP (STATUS has no signal
     * field; SIGNAL_POLL is the source). A failed reading keeps
     * 0 = unknown, which the glyph draws as a full fan - the
     * connection is the fact, the strength is not invented. */
    sh->wifi_signal_dbm = 0;
    if (st.connected) {
        int dbm;
        if (bd_wifi_signal_dbm(sh->wifi_ctl, &dbm) == 0)
            sh->wifi_signal_dbm = dbm;
    }
    snprintf(sh->wifi_ssid, sizeof(sh->wifi_ssid), "%s", st.ssid);
    if (st.connected && st.ssid[0])
        tk_label_set_text(sh->wifi_label, st.ssid);
    else if (st.state[0] && strcmp(st.state, "DISCONNECTED") != 0)
        tk_label_set_text(sh->wifi_label, "接続中");
    else
        tk_label_set_text(sh->wifi_label, "未接続");
    wifi_icon_update(sh);
    return 1;
}

bool bd_shell_wifi_connected(const bd_shell *sh) { return sh->wifi_connected; }

int bd_shell_wifi_signal_dbm(const bd_shell *sh) { return sh->wifi_signal_dbm; }

const char *bd_shell_wifi_ssid(const bd_shell *sh) { return sh->wifi_ssid; }

void bd_shell_kbd_config(const bd_shell *sh, tk_kbd_config *out)
{
    *out = sh->kbd_cfg;
}

void bd_shell_set_kbd_config(bd_shell *sh, const tk_kbd_config *cfg)
{
    /* Keyboard settings live in the settings app now (UI-REDESIGN
     * P2-4 removed the quick-panel repeat row); the shell keeps
     * storing the config for the session to apply. */
    sh->kbd_cfg = *cfg;
    tk_kbd_config_sanitize(&sh->kbd_cfg);
    bd_kbd_settings_save(&sh->kbd_cfg);
}

bool bd_shell_wifi(const bd_shell *sh) { return sh->wifi; }
bool bd_shell_panel_on_top(const bd_shell *sh) { return sh->panel_top; }
tk_color bd_shell_wallpaper(const bd_shell *sh) { return sh->wallpaper; }
ic_cache *bd_shell_icon_cache(bd_shell *sh) { return sh->icons; }

/* Band cover driver (proportion pass): show the shell-surface
 * strip under the floating shelf while at least one window is
 * non-minimized, hide it on the bare desktop. Polled through
 * the same win_ops bridge the app switcher uses; returns 1
 * when the visibility changed. Without win_ops (headless shell
 * tests) the cover simply stays hidden. */
int bd_shell_poll_windows(bd_shell *sh)
{
    if (!sh || !sh->band_cover) return 0;
    if (!sh->layout_settled) {
        sh->layout_settled = true;
        shell_relayout(sh);
    }
    bool want = false;
    int changed = 0;
    if (sh->win_ops_set && sh->win_ops.list) {
        bd_shell_win_entry ents[16];
        size_t n = sh->win_ops.list(sh->win_ud, ents, 16);
        for (size_t i = 0; i < n; i++)
            if (ents[i].state != BD_WIN_MINIMIZED) { want = true; break; }
        /* Reconcile the panel task list with the windows
         * that actually exist (eval F-17): tasks were only
         * recorded for launches through THIS shell instance,
         * so an app started any other way - or still open
         * across a shell restart / mode switch - never got
         * a taskbar button. A window whose title names a
         * catalog entry joins the task list; task_add
         * dedups, and nothing is removed here (the existing
         * launch-driven lifecycle is unchanged). */
        const de_entry *all[256];
        size_t na = de_catalog_query(sh->catalog, DE_CAT_ALL,
                                     NULL, all, 256);
        for (size_t i = 0; i < n; i++) {
            if (!ents[i].title[0]) continue;
            for (size_t j = 0; j < na; j++) {
                if (strcmp(all[j]->name, ents[i].title) == 0) {
                    size_t before = sh->n_tasks;
                    task_add(sh, all[j]);
                    if (sh->n_tasks != before) {
                        sh->tasks[sh->n_tasks - 1].from_win = true;
                        changed = 1;
                    }
                    break;
                }
            }
        }
        /* window-synced tasks whose window has closed leave
         * the list again (a taskbar button must not outlive
         * its window); iterate backwards for removal */
        for (size_t t = sh->n_tasks; t-- > 0;) {
            if (!sh->tasks[t].from_win) continue;
            const de_entry *e =
                de_catalog_find_id(sh->catalog, sh->tasks[t].id);
            bool alive = false;
            if (e) {
                for (size_t i = 0; i < n; i++)
                    if (strcmp(ents[i].title, e->name) == 0) {
                        alive = true;
                        break;
                    }
            }
            if (!alive && bd_shell_close_task(sh, t)) changed = 1;
        }
        /* the focused window owns the active marker (a
         * reconcile add must not steal it) */
        for (size_t i = 0; i < n; i++) {
            if (!ents[i].focused) continue;
            for (size_t t = 0; t < sh->n_tasks; t++) {
                const de_entry *e =
                    de_catalog_find_id(sh->catalog, sh->tasks[t].id);
                if (e && strcmp(e->name, ents[i].title) == 0 &&
                    sh->active_task != (int)t) {
                    sh->active_task = (int)t;
                    panel_tasks_rebuild(sh);
                    changed = 1;
                }
            }
        }
    }
    if (tk_is_visible(sh->band_cover) != want) {
        tk_set_visible(sh->band_cover, want);
        tk_widget_invalidate(sh->band_cover);
        changed = 1;
    }
    return changed;
}

bool bd_shell_band_cover_visible(const bd_shell *sh)
{
    return sh && sh->band_cover && tk_is_visible(sh->band_cover);
}

/* ---------------- hardware backend + assist (M2) ---------------- */

void bd_shell_attach_backend(bd_shell *sh, bd_backend *be)
{
    sh->backend = be;
}

int bd_shell_poll_backend(bd_shell *sh)
{
    if (!sh->backend) return 0;
    int updated = 0;
    bd_battery_info bi;
    if (bd_backend_battery(sh->backend, &bi) && bi.present) {
        bd_shell_set_battery(sh, bi.capacity, bi.charging);
        updated++;
    } else if (tk_is_visible(sh->tray_batt_btn)) {
        /* no battery device on this machine: the tray
         * element hides instead of showing a permanent
         * "--%" (eval F-08; a desktop with no battery shows
         * no battery icon). bd_shell_set_battery() un-hides
         * it if a battery ever reports in. */
        tk_set_visible(sh->tray_batt_btn, false);
        tk_widget_invalidate(sh->tray_batt_btn);
        updated++;
    }
    /* While muted the shell drives the hardware to 0 itself;
     * reading that 0 back would destroy the preserved set
     * level, so the volume field is not synced then. */
    int vol = bd_backend_volume_get(sh->backend);
    if (!sh->muted && vol >= 0 && vol != sh->volume) {
        sh->volume = vol;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", vol);
        tk_label_set_text(sh->vol_value, buf);
        tk_slider_set_value(sh->vol_slider, vol / 100.0f);
        bd_assist_set_values(sh->assist, sh->volume, sh->brightness);
        updated++;
    }
    int br = bd_backend_brightness_get(sh->backend);
    if (br >= 0 && br != sh->brightness) {
        sh->brightness = br;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", br);
        tk_label_set_text(sh->bright_value, buf);
        tk_slider_set_value(sh->bright_slider, br / 100.0f);
        bd_assist_set_values(sh->assist, sh->volume, sh->brightness);
        updated++;
    }
    return updated;
}

void bd_shell_set_brightness(bd_shell *sh, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    sh->brightness = pct;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    tk_label_set_text(sh->bright_value, buf);
    tk_slider_set_value(sh->bright_slider, pct / 100.0f);
    char v[16];
    snprintf(v, sizeof(v), "%d", pct);
    reg_set("shell", "brightness", v);
    if (sh->backend) bd_backend_brightness_set(sh->backend, pct);
    bd_assist_set_values(sh->assist, sh->volume, sh->brightness);
}

int bd_shell_brightness(const bd_shell *sh) { return sh->brightness; }

bool bd_shell_assist_open(const bd_shell *sh)
{
    return bd_assist_open(sh->assist);
}

void bd_shell_set_assist_open(bd_shell *sh, bool open)
{
    bd_assist_set_open(sh->assist, open);
}

void bd_shell_set_page_sink(bd_shell *sh, bd_shell_page_cb cb, void *userdata)
{
    sh->page_cb = cb;
    sh->page_ud = userdata;
}

void bd_shell_set_lock_cb(bd_shell *sh, bd_shell_lock_cb cb, void *userdata)
{
    sh->lock_cb = cb;
    sh->lock_ud = userdata;
    tk_set_visible(sh->lock_btn, cb != NULL);
}
