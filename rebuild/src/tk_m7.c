/* tk_m7.c - the M7 completion pass (AUDIT-toolkit.md section 4):
 * the components and platform services the first pass recorded
 * as missing - Card, SearchBar (+suggestion dropdown), Segmented
 * buttons, Carousel, Navigation rail, the determinate Ring,
 * AppBar, Navigation drawer, Bottom/side sheet, FAB menu, Split
 * button - plus the shared clipboard, the i18n catalogs, the
 * accessibility semantics tree and the widget side of drag &
 * drop. The WinUI/Fluent mode (theme style 2) branches live
 * here and in the older paints: same widget tree, the theme
 * alone selects the mode.
 *
 * Spec sources (fetched 2026-11-20, see
 * REPORT-toolkit-complete.md): m3.material.io component pages
 * (search, cards, bottom sheets, navigation drawer/rail,
 * segmented buttons, carousel, sliders, progress indicators,
 * dialogs, menus, snackbar, color roles), developer.android.com
 * (drag & drop, clipboard, AutoCompleteTextView, accessibility,
 * localization), Microsoft Learn WinUI/Fluent (type ramp,
 * corner radii, themeresources colour values). Dimensions are
 * the reference values converted to this panel. As written
 * (M7) the conversion used the then-assumed 187ppi (1dp =
 * 1.169px, 48dp = 56px; WinUI scale x1.43). The panel
 * premise was corrected in M21/M22 (AUDIT SS20/SS22):
 * 854x480 at ~179.2ppi, so 1dp = 1.12px (48dp = 53.8px,
 * touch floor 54px) and the WinUI scale is x1.344.
 * Dimensions in this file whose comments still carried an
 * old-premise conversion are retained values; each site
 * now states the corrected conversion, and the list is in
 * REPORT-toolkit-m23.md.
 */
#include "tk_internal.h"
#include "textcore.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <strings.h>

/* ---------------------------------------------------------------- */
/* small helpers                                                     */

static tk_widget *m7_leaf(tk_widget_kind k, const char *text,
                          bool focusable)
{
    tk_widget *w = tk_widget_new(k);
    if (!w) return NULL;
    w->text = tk_strdup(text);
    w->can_focus = focusable;
    return w;
}

/* A theme role with the pre-M7 fallback: roles added in M7 are
 * zero-alpha in hand-built themes, and zero means "unset". */
static tk_color role(tk_color c, tk_color fb) { return c.a ? c : fb; }

static void plot(tk_canvas *c, int x, int y, tk_color col)
{
    if (col.a == 255) tk_canvas_put_pixel(c, x, y, col);
    else if (col.a) tk_canvas_blend_pixel(c, x, y, col);
}

/* Bresenham line, 1px (callers offset for thickness). */
static void m7_line(tk_canvas *c, int x0, int y0, int x1, int y1,
                    tk_color col)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void m7_line2(tk_canvas *c, int x0, int y0, int x1, int y1,
                     tk_color col)
{
    m7_line(c, x0, y0, x1, y1, col);
    m7_line(c, x0 + 1, y0, x1 + 1, y1, col);
}

/* The M3 check mark in a 16px box at (x, y). */
static void m7_check(tk_canvas *c, int x, int y, tk_color col)
{
    m7_line2(c, x + 1, y + 8, x + 6, y + 13, col);
    m7_line2(c, x + 6, y + 13, x + 15, y + 3, col);
}

/* Nearest-neighbour blit of canvas-packed pixels (the same rule
 * as the widgets.c blit, needed here for item icons). */
static void m7_blit(tk_canvas *c, const uint32_t *px, int iw, int ih,
                    tk_rect d)
{
    if (!px || iw <= 0 || ih <= 0 || d.w <= 0 || d.h <= 0) return;
    for (int dy = 0; dy < d.h; dy++) {
        int sy = dy * ih / d.h;
        for (int dx = 0; dx < d.w; dx++) {
            int sx = dx * iw / d.w;
            uint32_t v = px[(size_t)sy * iw + sx];
            tk_color col = { (uint8_t)(v >> 16), (uint8_t)(v >> 8),
                             (uint8_t)v, (uint8_t)(v >> 24) };
            if (col.a == 255) tk_canvas_put_pixel(c, d.x + dx, d.y + dy, col);
            else if (col.a) tk_canvas_blend_pixel(c, d.x + dx, d.y + dy, col);
        }
    }
}

/* Badge pill/dot at (x, y) = the anchor's top-right (M3 badge:
 * error-coloured; count text in the on-error white). */
static void m7_badge(tk_canvas *c, const tk_theme *t, int x, int y,
                     int count)
{
    tk_color bgc = { 186, 26, 26, 255 };
    tk_color fgc = { 255, 255, 255, 255 };
    if (count < 0) {
        tk_canvas_fill_round_rect(c, (tk_rect){ x - 4, y - 4, 8, 8 },
                                  4, bgc);
    } else if (count > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", count > 99 ? 99 : count);
        int tw = tk_text_width_st(t, TK_TEXT_CAPTION, buf);
        int bw = tw + 10 < 18 ? 18 : tw + 10;
        tk_rect q = { x - bw / 2, y - 9, bw, 18 };
        tk_canvas_fill_round_rect(c, q, 9, bgc);
        tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                               q.x + (bw - tw) / 2,
                               q.y + (18 - tk_line_height_st(
                                          t, TK_TEXT_CAPTION)) / 2,
                               buf, fgc);
    }
}

/* Grow the borrowed per-item icon table (ListView's fields). */
static void m7_set_item_icon(tk_widget *w, int idx, const uint32_t *px,
                             int iw, int ih)
{
    if (!w || idx < 0) return;
    if ((size_t)idx >= w->n_item_icons) {
        size_t nc = (size_t)idx + 1;
        const uint32_t **ni = realloc(w->item_icons,
                                      nc * sizeof(*ni));
        if (!ni) return;
        for (size_t i = w->n_item_icons; i < nc; i++) ni[i] = NULL;
        w->item_icons = ni;
        w->n_item_icons = nc;
    }
    w->item_icons[idx] = px;
    if (iw > 0 && ih > 0) { w->item_icon_w = iw; w->item_icon_h = ih; }
}

static const uint32_t *m7_item_icon(const tk_widget *w, int idx)
{
    if (!w || idx < 0 || (size_t)idx >= w->n_item_icons) return NULL;
    return w->item_icons[idx];
}

/* Replace a widget's items[] with copies of the given strings. */
static void m7_set_items(tk_widget *w, const char *const *items,
                         size_t n)
{
    if (!w) return;
    for (size_t i = 0; i < w->n_items; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL;
    w->n_items = 0;
    if (!items || !n) return;
    w->items = calloc(n, sizeof(char *));
    if (!w->items) return;
    for (size_t i = 0; i < n; i++)
        w->items[i] = tk_strdup(items[i]);
    w->n_items = n;
}

static void m7_add_item(tk_widget *w, const char *label)
{
    if (!w) return;
    char **ni = realloc(w->items, (w->n_items + 1) * sizeof(char *));
    if (!ni) return;
    w->items = ni;
    w->items[w->n_items++] = tk_strdup(label);
}

/* ---------------------------------------------------------------- */
/* Shared clipboard (M7): one primary clip, text only - the
 * Android ClipboardManager model. The same store backs the
 * textcore provider installed on new buffers by default, so a
 * copy in any toolkit editor pastes into any other. */

static char *g_clip; /* owned; NULL = empty */

static void clip_set(void *ud, const char *text, size_t len)
{
    (void)ud;
    char *p = malloc(len + 1);
    if (!p) return;
    memcpy(p, text, len);
    p[len] = '\0';
    free(g_clip);
    g_clip = p;
}

static size_t clip_get(void *ud, char *out, size_t cap)
{
    (void)ud;
    if (!g_clip) {
        if (cap) out[0] = '\0';
        return 0;
    }
    size_t n = strlen(g_clip);
    if (cap) {
        size_t cpy = n < cap - 1 ? n : cap - 1;
        memcpy(out, g_clip, cpy);
        out[cpy] = '\0';
    }
    return n;
}

const tc_clipboard tk_shared_clipboard = { NULL, clip_set, clip_get };

void tk_clipboard_set_text(const char *text)
{
    clip_set(NULL, text ? text : "", text ? strlen(text) : 0);
}

const char *tk_clipboard_text(void) { return g_clip; }

bool tk_clipboard_has_text(void) { return g_clip && g_clip[0]; }

void tk_clipboard_clear(void)
{
    free(g_clip);
    g_clip = NULL;
}

/* ---------------------------------------------------------------- */
/* Localisation (M7): per-locale string catalogs with the
 * Android fallback chain (exact locale -> language -> default
 * catalog -> the key itself). The default catalog is the first
 * registered, like res/values. */

typedef struct { char *key, *text; } i18n_pair;
typedef struct {
    char *name;
    i18n_pair *pairs;
    size_t n, cap;
} i18n_locale;

static i18n_locale *g_locales;
static size_t g_n_locales, g_cap_locales;
static char *g_cur_locale; /* owned; NULL = default catalog */

void tk_i18n_add_locale(const char *locale,
                        const tk_i18n_entry *entries, size_t n)
{
    if (!locale || !entries) return;
    i18n_locale *loc = NULL;
    for (size_t i = 0; i < g_n_locales; i++)
        if (strcmp(g_locales[i].name, locale) == 0) {
            loc = &g_locales[i];
            break;
        }
    if (!loc) {
        if (g_n_locales == g_cap_locales) {
            size_t nc = g_cap_locales ? g_cap_locales * 2 : 4;
            i18n_locale *nl = realloc(g_locales, nc * sizeof(*nl));
            if (!nl) return;
            g_locales = nl;
            g_cap_locales = nc;
        }
        loc = &g_locales[g_n_locales++];
        loc->name = tk_strdup(locale);
        loc->pairs = NULL;
        loc->n = 0;
        loc->cap = 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (!entries[i].key || !entries[i].text) continue;
        i18n_pair *p = NULL;
        for (size_t j = 0; j < loc->n; j++)
            if (strcmp(loc->pairs[j].key, entries[i].key) == 0) {
                p = &loc->pairs[j];
                break;
            }
        if (p) {
            free(p->text);
            p->text = tk_strdup(entries[i].text);
            continue;
        }
        if (loc->n == loc->cap) {
            size_t nc = loc->cap ? loc->cap * 2 : 16;
            i18n_pair *np = realloc(loc->pairs, nc * sizeof(*np));
            if (!np) return;
            loc->pairs = np;
            loc->cap = nc;
        }
        loc->pairs[loc->n].key = tk_strdup(entries[i].key);
        loc->pairs[loc->n].text = tk_strdup(entries[i].text);
        loc->n++;
    }
}

void tk_i18n_set_locale(const char *locale)
{
    free(g_cur_locale);
    g_cur_locale = tk_strdup(locale);
}

const char *tk_i18n_locale(void)
{
    if (g_cur_locale) return g_cur_locale;
    return g_n_locales ? g_locales[0].name : "";
}

static const char *i18n_find(const char *locale, const char *key)
{
    if (!locale) return NULL;
    for (size_t i = 0; i < g_n_locales; i++) {
        if (strcmp(g_locales[i].name, locale) != 0) continue;
        for (size_t j = 0; j < g_locales[i].n; j++)
            if (strcmp(g_locales[i].pairs[j].key, key) == 0)
                return g_locales[i].pairs[j].text;
        return NULL;
    }
    return NULL;
}

/* The fallback chain; NULL when no catalog carries the key. */
static const char *i18n_lookup(const char *key)
{
    const char *cur = g_cur_locale;
    if (cur) {
        const char *s = i18n_find(cur, key);
        if (s) return s;
        /* language part of e.g. ja-JP / en_US */
        char lang[16];
        size_t i = 0;
        while (cur[i] && cur[i] != '-' && cur[i] != '_' &&
               i < sizeof(lang) - 1) {
            lang[i] = cur[i];
            i++;
        }
        lang[i] = '\0';
        if (i && strcmp(lang, cur) != 0) {
            s = i18n_find(lang, key);
            if (s) return s;
        }
    }
    if (g_n_locales) {
        const char *s = i18n_find(g_locales[0].name, key);
        if (s) return s;
    }
    return NULL;
}

const char *tk_tr(const char *key)
{
    if (!key) return "";
    const char *s = i18n_lookup(key);
    return s ? s : key;
}

const char *tk_tr_plural(const char *key, long count)
{
    if (!key) return "";
    /* Plural rules of the registered locales: Japanese has a
     * single form (always "other"); English-like locales pick
     * "one" exactly for count == 1. */
    const char *loc = tk_i18n_locale();
    bool one = !(loc[0] == 'j' && loc[1] == 'a') && count == 1;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s.%s", key, one ? "one" : "other");
    const char *s = i18n_lookup(buf);
    if (s) return s;
    return tk_tr(key);
}

void tk_i18n_clear(void)
{
    for (size_t i = 0; i < g_n_locales; i++) {
        free(g_locales[i].name);
        for (size_t j = 0; j < g_locales[i].n; j++) {
            free(g_locales[i].pairs[j].key);
            free(g_locales[i].pairs[j].text);
        }
        free(g_locales[i].pairs);
    }
    free(g_locales);
    g_locales = NULL;
    g_n_locales = 0;
    g_cap_locales = 0;
    free(g_cur_locale);
    g_cur_locale = NULL;
}

/* ---------------------------------------------------------------- */
/* Accessibility semantics (M7): the AccessibilityNodeInfo / UIA
 * model - role, label (content description first), state text,
 * bounds, enabled/focused/heading flags, collected in traversal
 * order; announcements are the live regions. */

void tk_widget_set_content_description(tk_widget *w, const char *text)
{
    if (!w) return;
    free(w->a11y_label);
    w->a11y_label = tk_strdup(text);
    tk_widget_invalidate(w);
}

const char *tk_widget_content_description(const tk_widget *w)
{
    return w ? w->a11y_label : NULL;
}

void tk_widget_set_a11y_role(tk_widget *w, tk_a11y_role role)
{
    if (!w) return;
    w->a11y_role = (int)role;
}

void tk_widget_set_a11y_heading(tk_widget *w, bool heading)
{
    if (!w) return;
    w->a11y_heading = heading;
}

static tk_a11y_role a11y_role_of(const tk_widget *w)
{
    if (w->a11y_role) return (tk_a11y_role)w->a11y_role;
    switch (w->kind) {
    case TK_W_BUTTON:    return TK_ROLE_BUTTON;
    case TK_W_CHECKBOX:  return TK_ROLE_CHECKBOX;
    case TK_W_RADIO:     return TK_ROLE_RADIO;
    case TK_W_SWITCH:    return TK_ROLE_SWITCH;
    case TK_W_SLIDER:    return TK_ROLE_SLIDER;
    case TK_W_LABEL:     return w->a11y_heading ? TK_ROLE_HEADING
                                                : TK_ROLE_TEXT;
    case TK_W_TEXTFIELD: return TK_ROLE_EDITTEXT;
    case TK_W_TEXTVIEW:  return TK_ROLE_EDITTEXT;
    case TK_W_SEARCHBAR: return TK_ROLE_EDITTEXT;
    case TK_W_LISTVIEW:  return TK_ROLE_LIST;
    case TK_W_TREEVIEW:  return TK_ROLE_LIST;
    case TK_W_MENU:      return TK_ROLE_MENU;
    case TK_W_DIALOG:    return TK_ROLE_DIALOG;
    case TK_W_IMAGEVIEW: return TK_ROLE_IMAGE;
    case TK_W_PROGRESS:  return TK_ROLE_PROGRESS;
    case TK_W_RING:      return TK_ROLE_PROGRESS;
    case TK_W_SPINNER:   return TK_ROLE_PROGRESS;
    case TK_W_TABBAR:    return TK_ROLE_TAB;
    case TK_W_TABS:      return TK_ROLE_TAB;
    case TK_W_SEGMENTED: return TK_ROLE_BUTTON;
    case TK_W_CHIP:      return TK_ROLE_BUTTON;
    default:             return TK_ROLE_NONE;
    }
}

static char g_lbl_bufs[8][128];
static unsigned g_lbl_idx;

static const char *a11y_label_of(const tk_widget *w)
{
    if (w->a11y_label) return w->a11y_label;
    switch (w->kind) {
    case TK_W_TEXTFIELD:
    case TK_W_TEXTVIEW:
    case TK_W_SEARCHBAR:
        if (w->buffer) {
            /* buffer text lives in a rotating scratch buffer:
             * a collect emits at most a handful of editors, and
             * the pointer stays valid until the next collect */
            char *b = g_lbl_bufs[g_lbl_idx++ % 8];
            tc_text(w->buffer, b, 128);
            if (b[0]) return b;
        }
        return w->placeholder;
    case TK_W_LISTVIEW:
    case TK_W_TABBAR:
    case TK_W_SEGMENTED:
    case TK_W_NAVRAIL:
    case TK_W_CAROUSEL:
        if (w->sel >= 0 && (size_t)w->sel < w->n_items)
            return w->items[w->sel];
        return w->text;
    case TK_W_DRAWER:
        if (w->sel >= 0 && (size_t)w->sel < w->n_menu_items)
            return w->menu_items[w->sel].label;
        return w->text;
    default:
        return w->text;
    }
}

static char g_pct_bufs[8][16];
static unsigned g_pct_idx;

static const char *a11y_state_of(const tk_widget *w)
{
    if (!w->enabled) return "disabled";
    switch (w->kind) {
    case TK_W_CHECKBOX:
        return w->checked ? "checked" : "unchecked";
    case TK_W_SWITCH:
        return w->checked ? "on" : "off";
    case TK_W_RADIO:
        return w->selected ? "selected" : "";
    case TK_W_BUTTON:
        return w->btn_toggle ? (w->checked ? "checked" : "unchecked")
                             : "";
    case TK_W_TABBAR:
    case TK_W_SEGMENTED:
    case TK_W_NAVRAIL:
        return "selected";
    case TK_W_SLIDER: {
        /* range-mode sliders report their value in range
         * units; plain sliders hold a 0..1 fraction */
        int pct;
        if (w->sld_range && w->fmax > w->fmin)
            pct = (int)((w->fvalue - w->fmin) /
                        (w->fmax - w->fmin) * 100.0f + 0.5f);
        else
            pct = (int)(w->fvalue * 100.0f + 0.5f);
        char *b = g_pct_bufs[g_pct_idx++ % 8];
        snprintf(b, 16, "%d%%", pct);
        return b;
    }
    case TK_W_PROGRESS:
    case TK_W_RING: {
        char *b = g_pct_bufs[g_pct_idx++ % 8];
        snprintf(b, 16, "%d%%", (int)(w->fvalue * 100.0f + 0.5f));
        return b;
    }
    default:
        return "";
    }
}

static void a11y_walk(tk_root *r, tk_widget *w, tk_a11y_node *out,
                      size_t cap, size_t *count)
{
    if (!w || !w->visible) return;
    tk_a11y_role role = a11y_role_of(w);
    if (role != TK_ROLE_NONE || w->a11y_label) {
        if (out && *count < cap) {
            tk_a11y_node *nd = &out[*count];
            nd->widget = w;
            nd->role = role;
            nd->label = a11y_label_of(w);
            nd->state = a11y_state_of(w);
            nd->bounds = w->rect;
            nd->enabled = w->enabled;
            nd->focused = (r && r->focused == w);
            nd->heading = w->a11y_heading || role == TK_ROLE_HEADING;
        }
        (*count)++;
    }
    for (size_t i = 0; i < w->n_children; i++)
        a11y_walk(r, w->children[i], out, cap, count);
}

size_t tk_a11y_collect(tk_root *r, tk_a11y_node *out, size_t cap)
{
    size_t count = 0;
    if (!r) return 0;
    a11y_walk(r, r->top, out, cap, &count);
    for (size_t i = 0; i < r->n_overlays; i++)
        a11y_walk(r, r->overlays[i], out, cap, &count);
    return count;
}

const char *tk_a11y_role_name(tk_a11y_role role)
{
    switch (role) {
    case TK_ROLE_BUTTON:   return "android.widget.Button";
    case TK_ROLE_CHECKBOX: return "android.widget.CheckBox";
    case TK_ROLE_RADIO:    return "android.widget.RadioButton";
    case TK_ROLE_SWITCH:   return "android.widget.Switch";
    case TK_ROLE_SLIDER:   return "android.widget.SeekBar";
    case TK_ROLE_TEXT:     return "android.widget.TextView";
    case TK_ROLE_EDITTEXT: return "android.widget.EditText";
    case TK_ROLE_LIST:     return "android.widget.ListView";
    case TK_ROLE_TAB:      return "android.widget.TabWidget";
    case TK_ROLE_IMAGE:    return "android.widget.ImageView";
    case TK_ROLE_HEADING:  return "android.widget.TextView";
    case TK_ROLE_PROGRESS: return "android.widget.ProgressBar";
    case TK_ROLE_DIALOG:   return "android.app.Dialog";
    case TK_ROLE_LINK:     return "android.widget.TextView";
    default:               return "android.view.View";
    }
}

const char *tk_a11y_uia_type(tk_a11y_role role)
{
    switch (role) {
    case TK_ROLE_BUTTON:   return "Button";
    case TK_ROLE_CHECKBOX: return "CheckBox";
    case TK_ROLE_RADIO:    return "RadioButton";
    case TK_ROLE_SWITCH:   return "Button"; /* UIA: Toggle pattern */
    case TK_ROLE_SLIDER:   return "Slider";
    case TK_ROLE_TEXT:     return "Text";
    case TK_ROLE_EDITTEXT: return "Edit";
    case TK_ROLE_LIST:     return "List";
    case TK_ROLE_LISTITEM: return "ListItem";
    case TK_ROLE_TAB:      return "Tab";
    case TK_ROLE_IMAGE:    return "Image";
    case TK_ROLE_HEADING:  return "Text";
    case TK_ROLE_PROGRESS: return "ProgressBar";
    case TK_ROLE_MENU:     return "Menu";
    case TK_ROLE_MENUITEM: return "MenuItem";
    case TK_ROLE_DIALOG:   return "Window";
    case TK_ROLE_LINK:     return "Hyperlink";
    default:               return "Custom";
    }
}

void tk_a11y_announce(tk_root *r, const char *text)
{
    if (!r) return;
    snprintf(r->announce_buf, sizeof(r->announce_buf), "%s",
             text ? text : "");
}

const char *tk_a11y_last_announcement(const tk_root *r)
{
    return r ? r->announce_buf : "";
}

/* ---------------------------------------------------------------- */
/* Drag and drop (M7): the widget side. The gesture itself is
 * driven by the root (tk_core.c): it copies the payload at press
 * time, follows the pointer with this shadow and resolves the
 * drop against the hit chain's on_drop callbacks. */

void tk_widget_set_drag_source(tk_widget *w, const char *mime,
                               const void *data, size_t len,
                               const char *label)
{
    if (!w) return;
    tk_widget_clear_drag_source(w);
    if (!mime) return;
    if (len > 65536) len = 65536; /* documented 64KB payload cap */
    w->drag_mime = tk_strdup(mime);
    w->drag_label = tk_strdup(label);
    w->drag_len = len;
    w->drag_data = NULL;
    if (data && len) {
        w->drag_data = malloc(len);
        if (w->drag_data) memcpy(w->drag_data, data, len);
        else w->drag_len = 0;
    }
}

void tk_widget_clear_drag_source(tk_widget *w)
{
    if (!w) return;
    free(w->drag_mime);
    free(w->drag_data);
    free(w->drag_label);
    w->drag_mime = NULL;
    w->drag_data = NULL;
    w->drag_label = NULL;
    w->drag_len = 0;
}

void tk_widget_set_on_drop(tk_widget *w, tk_drop_cb cb, void *ud)
{
    if (!w) return;
    w->drop_cb = cb;
    w->drop_ud = ud;
}

bool tk_drag_active(const tk_root *r) { return r && r->drag_on; }

tk_rect tk_drag_shadow_rect(const tk_root *r)
{
    const tk_theme *t = tk_theme_default();
    int tw = r->drag_label[0] ? tk_text_width(t, r->drag_label) : 0;
    return (tk_rect){ r->drag_x - 10, r->drag_y - 46,
                      tw + 36, 36 };
}

void tk_m7_paint_drag(tk_root *r, tk_canvas *c, const tk_theme *t)
{
    if (!r->drag_on) return;
    tk_rect q = tk_drag_shadow_rect(r);
    tk_color bgc = role(t->inverse_surface, t->text);
    tk_color fgc = role(t->inverse_on_surface, t->bg);
    if (t->style == 2) bgc = role(t->card_bg, t->surface);
    if (t->style == 2) fgc = t->text;
    tk_canvas_fill_round_rect(c, (tk_rect){ q.x + 2, q.y + 3,
                                            q.w, q.h },
                              18, (tk_color){ 0, 0, 0, 48 });
    tk_canvas_fill_round_rect(c, q, 18, bgc);
    if (r->drag_label[0]) {
        int tw = tk_text_width(t, r->drag_label);
        tk_canvas_push_clip(c, q);
        tk_canvas_draw_text(c, t, q.x + (q.w - tw) / 2,
                            q.y + (q.h - tk_line_height(t)) / 2,
                            r->drag_label, fgc);
        tk_canvas_pop_clip(c);
    }
}

/* A pointer-down modality swallowed outside the modal widget:
 * only the modal sheet treats it as a dismissal. */
void tk_m7_overlay_outside_down(tk_root *r, tk_widget *modal)
{
    if (modal && modal->kind == TK_W_SHEET)
        tk_sheet_close(r, modal);
}

/* ---------------------------------------------------------------- */
/* Suggestion dropdown (M7): shared by the TextField's
 * autocomplete and the SearchBar's SearchView form. Prefix
 * matches (ASCII case-insensitive) of the buffer text, capped
 * at 8, in a combo-style auto_free menu owned via open_sub. */

static void suggest_select(tk_widget *menu, void *ud)
{
    (void)ud;
    tk_widget *owner = menu->owner_ref;
    if (!owner) return;
    owner->open_sub = NULL;
    int id = tk_menu_last_id(menu);
    char picked[256];
    picked[0] = '\0';
    for (size_t i = 0; i < menu->n_menu_items; i++)
        if (!menu->menu_items[i].separator &&
            menu->menu_items[i].id == id && menu->menu_items[i].label)
            snprintf(picked, sizeof(picked), "%s",
                     menu->menu_items[i].label);
    if (!picked[0] || !owner->buffer) return;
    tc_set_text(owner->buffer, picked);
    tc_move_to(owner->buffer, (size_t)-1, false);
    tk_widget_invalidate(owner);
    tk_invoke(owner->cb1, owner, owner->cb1_ud);
    if (owner->kind == TK_W_SEARCHBAR)
        tk_invoke(owner->cb2, owner, owner->cb2_ud);
}

void tk_m7_suggest_refresh(tk_root *r, tk_widget *w)
{
    if (!r || !w || !w->buffer) return;
    if (w->open_sub) tk_menu_close(r, w->open_sub);
    if (!w->n_items) return;
    char txt[512];
    tc_text(w->buffer, txt, sizeof(txt));
    if (!txt[0]) return;
    if (w->kind == TK_W_TEXTFIELD &&
        (int)tc_count(w->buffer) < (w->tf_threshold > 0
                                     ? w->tf_threshold : 1))
        return;
    size_t tl = strlen(txt);
    tk_widget *m = tk_menu_new();
    if (!m) return;
    int shown = 0;
    for (size_t i = 0; i < w->n_items && shown < 8; i++) {
        const char *cand = w->items[i];
        if (!cand) continue;
        if (strncasecmp(cand, txt, tl) == 0 && strcmp(cand, txt) != 0) {
            tk_menu_add_item(m, cand, (int)i);
            shown++;
        }
    }
    if (!shown) {
        tk_widget_free(m);
        return;
    }
    m->auto_free = true;
    m->owner_ref = w;
    m->i1 = w->rect.w;
    tk_menu_set_on_select(m, suggest_select, NULL);
    tk_menu_popup(r, m, w->rect.x, w->rect.y + w->rect.h);
    /* open_sub is assigned only AFTER the popup: the popup
     * shifts focus to the menu, which delivers FOCUS_OUT to
     * the owner - and the owner's FOCUS_OUT handler closes
     * open_sub. Assigning it here keeps the just-opened menu
     * from being closed by its own opening. */
    w->open_sub = m;
}

/* ---------------------------------------------------------------- */
/* SearchBar (M7) */

tk_widget *tk_searchbar_new(const char *placeholder)
{
    tk_widget *w = m7_leaf(TK_W_SEARCHBAR, NULL, true);
    if (!w) return NULL;
    w->buffer = tc_new();
    if (w->buffer) tc_set_clipboard(w->buffer, &tk_shared_clipboard);
    w->placeholder = tk_strdup(placeholder);
    return w;
}

tc_buffer *tk_searchbar_buffer(tk_widget *w) { return w->buffer; }

void tk_searchbar_set_text(tk_widget *w, const char *text)
{
    if (!w || !w->buffer) return;
    tc_set_text(w->buffer, text ? text : "");
    tk_widget_invalidate(w);
}

size_t tk_searchbar_text(const tk_widget *w, char *out, size_t cap)
{
    if (!w || !w->buffer) {
        if (cap) out[0] = '\0';
        return 0;
    }
    return tc_text(w->buffer, out, cap);
}

void tk_searchbar_set_suggestions(tk_widget *w,
                                  const char *const *items, size_t n)
{
    m7_set_items(w, items, n);
}

void tk_searchbar_set_on_search(tk_widget *w, tk_notify_cb cb,
                                void *ud)
{
    if (!w) return;
    w->cb2 = cb;
    w->cb2_ud = ud;
}

void tk_searchbar_set_on_change(tk_widget *w, tk_notify_cb cb,
                                void *ud)
{
    if (!w) return;
    w->cb1 = cb;
    w->cb1_ud = ud;
}

bool tk_searchbar_suggestions_open(const tk_widget *w)
{
    return w && w->open_sub != NULL;
}

static void searchbar_paint(tk_widget *w, tk_canvas *c,
                            const tk_theme *t)
{
    tk_rect r = w->rect;
    if (t->style == 2) {
        /* WinUI AutoSuggestBox: control fill + stroke, 4epx */
        tk_canvas_fill_round_rect(c, r, t->radius,
                                  role(t->control_fill, t->surface));
        tk_canvas_stroke_round_rect(c, r, t->radius, 1,
                                    role(t->control_stroke,
                                         t->border));
    } else if (t->style == 1) {
        tk_canvas_fill_round_rect(c, r, r.h / 2,
                                  role(t->container_high,
                                       t->surface_alt));
    } else {
        tk_canvas_fill_round_rect(c, r, t->radius, t->surface_alt);
    }
    /* leading search glyph: ring + handle, onSurfaceVariant */
    int cx = r.x + 24, cy = r.y + r.h / 2;
    for (int a = 0; a < 360; a += 12) {
        float ang = a * 0.0174533f;
        int px = cx + (int)(7.0f * cosf(ang));
        int py = cy + (int)(7.0f * sinf(ang));
        tk_canvas_fill_round_rect(c, (tk_rect){ px - 1, py - 1, 3, 3 },
                                  1, t->text_dim);
    }
    m7_line2(c, cx + 5, cy + 5, cx + 11, cy + 11, t->text_dim);
    /* text / placeholder */
    char txt[512];
    tc_text(w->buffer, txt, sizeof(txt));
    bool empty = txt[0] == '\0';
    int x0 = r.x + 48;
    int tw_avail = r.w - 48 - (empty ? 16 : 48);
    if (tw_avail < 0) tw_avail = 0;
    tk_canvas_push_clip(c, (tk_rect){ x0, r.y, tw_avail, r.h });
    int lh = tk_w_text_lh(w, t);
    if (empty) {
        if (w->placeholder)
            tk_w_draw_text(c, w, t, x0, r.y + (r.h - lh) / 2,
                           w->placeholder, t->text_dim);
    } else {
        /* M9: while an IME composition is live the text is
         * drawn in three parts around the CARET - prefix,
         * the shared formal preedit painter (segmented
         * underlines, active-segment accent, candidate
         * selection wash: the same rendering the text field
         * uses), suffix - instead of the M7 form that
         * appended the preedit after the committed text with
         * a flat accent underline */
        char comp[256];
        bool composing = tc_composition(w->buffer, comp,
                                        sizeof(comp)) && comp[0];
        if (composing) {
            size_t cur = tc_cursor(w->buffer);
            if (cur > strlen(txt)) cur = strlen(txt);
            char pre[512];
            size_t pn = cur < sizeof(pre) - 1 ? cur
                                              : sizeof(pre) - 1;
            memcpy(pre, txt, pn);
            pre[pn] = '\0';
            int ty = r.y + (r.h - lh) / 2;
            tk_w_draw_text(c, w, t, x0, ty, pre, t->text);
            int x = x0 + tk_w_text_w(w, t, pre);
            x += tk_draw_composition(c, t, w->buffer, x, ty,
                                     t->text, false);
            tk_w_draw_text(c, w, t, x, ty, txt + cur, t->text);
        } else {
            tk_w_draw_text(c, w, t, x0, r.y + (r.h - lh) / 2,
                           txt, t->text);
        }
    }
    tk_canvas_pop_clip(c);
    /* trailing clear affordance while text is present */
    if (!empty) {
        int qx = r.x + r.w - 24, qy = cy;
        m7_line2(c, qx - 6, qy - 6, qx + 6, qy + 6, t->text_dim);
        m7_line2(c, qx - 6, qy + 6, qx + 6, qy - 6, t->text_dim);
    }
}

static bool searchbar_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    switch (ev->type) {
    case TK_EV_POINTER_DOWN: {
        if (!tk_rect_contains(w->rect, ev->x, ev->y)) return false;
        char txt[64];
        tc_text(w->buffer, txt, sizeof(txt));
        if (txt[0] && ev->x >= w->rect.x + w->rect.w - 48) {
            tc_set_text(w->buffer, "");
            if (w->open_sub && root) tk_menu_close(root, w->open_sub);
            tk_widget_invalidate(w);
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        char full[4096];
        tc_text(w->buffer, full, sizeof(full));
        size_t off = tk_offset_at_x(tk_theme_default(), full,
                                    ev->x - w->rect.x - 48);
        tc_move_to(w->buffer, off,
                   (ev->modifiers & TK_MOD_SHIFT) != 0);
        return true;
    }
    case TK_EV_KEY_DOWN:
        if (ev->key == TK_KEY_ENTER) {
            if (w->open_sub && root) tk_menu_close(root, w->open_sub);
            tk_invoke(w->cb2, w, w->cb2_ud);
            return true;
        }
        if (ev->key == TK_KEY_ESCAPE) {
            if (w->open_sub && root) {
                tk_menu_close(root, w->open_sub);
                return true;
            }
            return false;
        }
        break;
    case TK_EV_FOCUS_OUT:
        if (w->open_sub && root) tk_menu_close(root, w->open_sub);
        return false;
    default:
        break;
    }
    {
        char before[512], after[512];
        tc_text(w->buffer, before, sizeof(before));
        bool handled = tk_text_edit_key(w, ev, false);
        tc_text(w->buffer, after, sizeof(after));
        if (handled && root && strcmp(before, after) != 0)
            tk_m7_suggest_refresh(root, w);
        return handled;
    }
}

/* ---------------------------------------------------------------- */
/* Segmented buttons (M7) */

tk_widget *tk_segmented_new(bool multi_select)
{
    tk_widget *w = m7_leaf(TK_W_SEGMENTED, NULL, true);
    if (!w) return NULL;
    w->seg_multi = multi_select;
    w->sel = -1;
    return w;
}

void tk_segmented_add(tk_widget *w, const char *label)
{
    m7_add_item(w, label);
}

void tk_segmented_set_item_icon(tk_widget *w, int idx,
                                const uint32_t *px, int iw, int ih)
{
    m7_set_item_icon(w, idx, px, iw, ih);
}

int tk_segmented_selected(const tk_widget *w)
{
    return w ? w->sel : -1;
}

uint32_t tk_segmented_mask(const tk_widget *w)
{
    return w ? w->seg_mask : 0;
}

/* M17: the selected segment's check appears/disappears with
 * a fade + slot expansion (the selection glyph transition on
 * the toolkit's selection pair), like the filter chip's
 * check. One segment animates at a time: starting a new fade
 * cancels the running one (which snaps to its end state) -
 * the reference cross-fades the old and new checks
 * simultaneously; the simplification is recorded in AUDIT
 * SS15. */
/* the animation's user data is the widget itself (its
 * seg_anim_idx / seg_anim_in fields carry the segment and
 * direction): tk_anim_cancel never runs the done callback,
 * so a heap user-data block would leak on every interrupted
 * fade. */
static void seg_fade_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->glyph_pos = v / 1000.0f;
    tk_widget_invalidate(w);
}

static void seg_fade_done(void *ud)
{
    tk_widget *w = ud;
    w->glyph_pos = w->seg_anim_in ? 1.0f : 0.0f;
    w->seg_anim_idx = -1;
    w->seg_anim = NULL;
    tk_widget_invalidate(w);
}

static void seg_check_anim(tk_widget *w, int idx, bool in)
{
    if (w->seg_anim) {
        tk_anim_cancel(w->seg_anim);
        w->seg_anim = NULL;
        /* the cancelled fade's segment snaps to its end */
        if (w->seg_anim_idx >= 0)
            w->glyph_pos = w->seg_anim_in ? 1.0f : 0.0f;
    }
    w->seg_anim_idx = idx;
    w->seg_anim_in = in;
    tk_root *r = tk_widget_root(w);
    if (!r || r->anim_now == 0 ||
        !tk_root_animations_enabled(r)) {
        w->glyph_pos = in ? 1.0f : 0.0f;
        w->seg_anim_idx = -1;
        return;
    }
    uint32_t dur;
    tk_ease_fn ease;
    tk_anim_selection_pair(&dur, &ease);
    w->seg_anim = tk_anim_start(r, in ? 0 : 1000, in ? 1000 : 0,
                                (int)dur, ease, seg_fade_cb,
                                seg_fade_done, w);
    /* a NULL handle: delivered synchronously (seg_fade_done
     * already ran) */
}

void tk_segmented_select(tk_widget *w, int idx)
{
    if (!w || idx < 0 || (size_t)idx >= w->n_items) return;
    if (w->seg_multi) {
        if (!(w->seg_mask & (1u << idx))) {
            w->seg_mask |= (1u << idx);
            seg_check_anim(w, idx, true);
        }
    } else if (w->sel != idx) {
        w->sel = idx;
        seg_check_anim(w, idx, true);
    }
    tk_widget_invalidate(w);
}

void tk_segmented_set_mask(tk_widget *w, uint32_t mask)
{
    if (!w) return;
    uint32_t changed = w->seg_mask ^ mask;
    w->seg_mask = mask;
    for (int i = 0; i < 32 && (size_t)i < w->n_items; i++) {
        if (changed & (1u << i)) {
            seg_check_anim(w, i, (mask & (1u << i)) != 0);
            break; /* one fade at a time (see seg_check_anim) */
        }
    }
    tk_widget_invalidate(w);
}

void tk_segmented_set_on_change(tk_widget *w, tk_notify_cb cb,
                                void *ud)
{
    if (!w) return;
    w->cb1 = cb;
    w->cb1_ud = ud;
}

static int seg_width(const tk_widget *w, const tk_theme *t)
{
    int mx = 56; /* the widest segment sets the equal width */
    for (size_t i = 0; i < w->n_items; i++) {
        int cw = tk_w_text_w(w, t, w->items[i]);
        if (m7_item_icon(w, (int)i)) cw += 28;
        if (cw + 28 > mx) mx = cw + 28;
    }
    return mx;
}

static bool seg_is_sel(const tk_widget *w, int i)
{
    return w->seg_multi ? (w->seg_mask & (1u << i)) != 0
                        : w->sel == i;
}

static void segmented_paint(tk_widget *w, tk_canvas *c,
                            const tk_theme *t)
{
    tk_rect r = w->rect;
    if (!w->n_items) return;
    int sw = r.w / (int)w->n_items;
    int rad = t->style == 1 ? r.h / 2 : t->radius;
    /* selected fills first (under the outline and dividers) */
    for (size_t i = 0; i < w->n_items; i++) {
        if (!seg_is_sel(w, (int)i)) continue;
        tk_rect q = { r.x + (int)i * sw + 1, r.y + 1,
                      (i + 1 == w->n_items ? r.x + r.w
                                           : r.x + (int)(i + 1) * sw)
                          - (r.x + (int)i * sw) - 2,
                      r.h - 2 };
        tk_color fill = t->style == 2 ? t->accent
                                      : role(t->container,
                                             t->surface_alt);
        if (i == 0 || i + 1 == w->n_items) {
            tk_canvas_fill_round_rect(c, q, rad > 1 ? rad - 1 : 1,
                                      fill);
            /* square off the inner corners */
            if (i == 0)
                tk_canvas_fill_rect(c, (tk_rect){ q.x + q.w / 2,
                                                  q.y, q.w - q.w / 2,
                                                  q.h }, fill);
            else
                tk_canvas_fill_rect(c, (tk_rect){ q.x, q.y,
                                                  q.w / 2, q.h },
                                    fill);
        } else {
            tk_canvas_fill_rect(c, q, fill);
        }
    }
    /* M14: the multi-select keyboard cursor - a 10% state
     * layer on the cursor segment while the widget holds
     * keyboard focus (the single-stop mapping of the
     * per-segment tab stops the reference platforms use) */
    if (w->seg_multi && w->seg_focused && w->seg_cursor >= 0 &&
        (size_t)w->seg_cursor < w->n_items) {
        int i = w->seg_cursor;
        tk_rect q = { r.x + i * sw + 1, r.y + 1,
                      (i + 1 == (int)w->n_items ? r.x + r.w
                                               : r.x + (i + 1) * sw)
                          - (r.x + i * sw) - 2,
                      r.h - 2 };
        tk_color layer = t->text; layer.a = 26;
        tk_canvas_fill_rect(c, q, layer);
    }
    /* dividers between two unselected segments */
    for (size_t i = 1; i < w->n_items; i++) {
        if (seg_is_sel(w, (int)i - 1) || seg_is_sel(w, (int)i))
            continue;
        tk_canvas_fill_rect(c, (tk_rect){ r.x + (int)i * sw,
                                          r.y + 1, 1, r.h - 2 },
                            t->border_line);
    }
    /* container outline: outline role (M3) / control stroke */
    tk_canvas_stroke_round_rect(c, r, rad, 1,
                                t->style == 2
                                    ? role(t->control_stroke,
                                           t->border)
                                    : t->border_line);
    /* content: check (selected) or icon, then the label */
    int lh = tk_w_text_lh(w, t);
    for (size_t i = 0; i < w->n_items; i++) {
        tk_rect q = { r.x + (int)i * sw, r.y,
                      (i + 1 == w->n_items ? r.x + r.w
                                           : r.x + (int)(i + 1) * sw)
                          - (r.x + (int)i * sw),
                      r.h };
        bool sel = seg_is_sel(w, (int)i);
        tk_color fg = !w->enabled ? t->text_dim
                      : (sel && t->style == 2) ? t->on_accent
                                               : t->text;
        /* M17: while this segment's check fade runs, the
         * check's alpha follows glyph_pos and a clip window
         * of glyph_pos x 24 reveals it (the fade + expand of
         * the reference's check appearance, within the slot
         * this layout has always reserved - the resting
         * geometry, label position included, is unchanged) */
        float gp = sel ? 1.0f : 0.0f;
        if ((int)i == w->seg_anim_idx) gp = w->glyph_pos;
        int lead = (int)(gp * 24.0f + 0.5f);
        int content = tk_w_text_w(w, t, w->items[i]) + 24;
        int x = q.x + (q.w - content) / 2;
        if (x < q.x + 4) x = q.x + 4;
        if (gp > 0.0f) {
            tk_color cg = fg;
            cg.a = (uint8_t)((float)cg.a * gp);
            bool clip = gp < 0.999f;
            if (clip)
                tk_canvas_push_clip(c, (tk_rect){ x, q.y, lead,
                                                  q.h });
            m7_check(c, x, q.y + (q.h - 16) / 2, cg);
            if (clip) tk_canvas_pop_clip(c);
        } else if (m7_item_icon(w, (int)i)) {
            m7_blit(c, m7_item_icon(w, (int)i), w->item_icon_w,
                    w->item_icon_h,
                    (tk_rect){ x, q.y + (q.h - 20) / 2, 20, 20 });
        }
        tk_canvas_push_clip(c, q);
        tk_w_draw_text(c, w, t, x + 24, q.y + (q.h - lh) / 2,
                       w->items[i], fg);
        tk_canvas_pop_clip(c);
    }
}

static bool segmented_event(tk_widget *w, tk_root *root,
                            tk_event *ev)
{
    (void)root;
    if (!w->n_items) return false;
    if (ev->type == TK_EV_POINTER_DOWN &&
        tk_rect_contains(w->rect, ev->x, ev->y)) {
        int sw = w->rect.w / (int)w->n_items;
        w->i1 = sw > 0 ? (ev->x - w->rect.x) / sw : -1;
        if (w->i1 >= (int)w->n_items) w->i1 = (int)w->n_items - 1;
        w->pressed = true;
        return true;
    }
    if (ev->type == TK_EV_POINTER_UP && w->pressed) {
        w->pressed = false;
        if (!w->enabled) return true;
        int sw = w->rect.w / (int)w->n_items;
        int idx = (sw > 0 && tk_rect_contains(w->rect, ev->x, ev->y))
                      ? (ev->x - w->rect.x) / sw : -1;
        if (idx >= (int)w->n_items) idx = (int)w->n_items - 1;
        if (idx >= 0 && idx == w->i1) {
            /* M17: through the setters, so the check fade
             * runs (the direct field flips here previously
             * bypassed it) */
            if (w->seg_multi)
                tk_segmented_set_mask(w,
                                      w->seg_mask ^ (1u << idx));
            else
                tk_segmented_select(w, idx);
            tk_widget_invalidate(w);
            tk_invoke(w->cb1, w, w->cb1_ud);
        }
        return true;
    }
    if (ev->type == TK_EV_FOCUS_IN && w->seg_multi) {
        w->seg_focused = true;
        tk_widget_invalidate(w);
        return false;
    }
    if (ev->type == TK_EV_FOCUS_OUT && w->seg_multi) {
        w->seg_focused = false;
        tk_widget_invalidate(w);
        return false;
    }
    if (ev->type == TK_EV_KEY_DOWN && !w->seg_multi) {
        if (ev->key == TK_KEY_LEFT || ev->key == TK_KEY_RIGHT) {
            int d = ev->key == TK_KEY_RIGHT ? 1 : -1;
            int ns = w->sel + d;
            if (ns < 0) ns = 0;
            if (ns >= (int)w->n_items) ns = (int)w->n_items - 1;
            if (ns != w->sel) {
                /* M17: through the setter (check fade) */
                tk_segmented_select(w, ns);
                tk_widget_invalidate(w);
                tk_invoke(w->cb1, w, w->cb1_ud);
            }
            return true;
        }
    }
    if (ev->type == TK_EV_KEY_DOWN && w->seg_multi) {
        /* M14: the multi form's keyboard model - arrows move
         * the cursor segment, Space/Enter toggles it (the
         * APG toolbar pattern; on the reference platforms
         * each segment is its own tab stop, here the widget
         * is a single stop so the cursor plays that role) */
        if (ev->key == TK_KEY_LEFT || ev->key == TK_KEY_RIGHT) {
            int d = ev->key == TK_KEY_RIGHT ? 1 : -1;
            int ns = w->seg_cursor + d;
            if (ns < 0) ns = 0;
            if (ns >= (int)w->n_items) ns = (int)w->n_items - 1;
            if (ns != w->seg_cursor) {
                w->seg_cursor = ns;
                tk_widget_invalidate(w);
            }
            return true;
        }
        if (ev->key == TK_KEY_ENTER || ev->key == TK_KEY_SPACE) {
            if (w->seg_cursor >= 0 &&
                (size_t)w->seg_cursor < w->n_items) {
                /* M17: through the setter (check fade) */
                tk_segmented_set_mask(w, w->seg_mask ^
                                         (1u << w->seg_cursor));
                tk_widget_invalidate(w);
                tk_invoke(w->cb1, w, w->cb1_ud);
            }
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- */
/* Carousel (M7): multi-browse - the current item is large
 * (248px), the rest small (64px, the 56dp small-item ceiling at
 * this panel), 8dp spacing, 28dp item corners. The scroll
 * offset lives in scroll_y, the index in sel.
 * M9: the item sizing is CONTINUOUS in the scroll offset, as
 * the spec requires ("items change size as they move through
 * the carousel layout", M3 carousel specs fetched
 * 2026-11-20): item i's focus factor falls linearly from 1 at
 * its slot centre to 0 one step away, and its width
 * interpolates small->large by that factor. Positions
 * accumulate the interpolated widths. At rest
 * (scroll_y = sel * step) the settled geometry is exactly the
 * M7 form: the selected item at 248, the rest at 64. */

#define CAR_LARGE 248
#define CAR_SMALL 64
#define CAR_GAP 9 /* 8dp */

tk_widget *tk_carousel_new(void)
{
    tk_widget *w = m7_leaf(TK_W_CAROUSEL, NULL, true);
    if (w) w->sel = 0;
    return w;
}

void tk_carousel_add(tk_widget *w, const char *label,
                     const uint32_t *px, int iw, int ih)
{
    if (!w) return;
    m7_add_item(w, label);
    if (px) m7_set_item_icon(w, (int)w->n_items - 1, px, iw, ih);
}

int tk_carousel_index(const tk_widget *w) { return w ? w->sel : -1; }

/* M13: the settle - after a drag or a programmatic jump the
 * offset eases to the selected item's snap point over the
 * mode's large pair (a traversing surface: M3 300 ms
 * Emphasized decelerate / WinUI 250 ms). Without a live
 * animation clock the offset snaps, as every other surface
 * does. A new drag cancels a running settle. */
static void car_off_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->scroll_y = v;
    tk_widget_invalidate(w);
}
static void car_off_done(void *ud)
{
    ((tk_widget *)ud)->anim_handle = NULL;
}
static void car_settle(tk_widget *w, tk_root *r, int target)
{
    if (w->anim_handle) {
        tk_anim_cancel(w->anim_handle);
        w->anim_handle = NULL;
    }
    if (!r || r->anim_now == 0 || !tk_root_animations_enabled(r) ||
        w->scroll_y == target) {
        w->scroll_y = target;
        return;
    }
    uint32_t dur; tk_ease_fn ease;
    tk_anim_mode_pair(true, &dur, &ease);
    w->anim_handle = tk_anim_start(r, w->scroll_y, target, dur,
                                   ease, car_off_cb, car_off_done,
                                   w);
    if (!w->anim_handle) w->scroll_y = target;
}

void tk_carousel_set_index(tk_widget *w, int idx)
{
    if (!w || idx < 0 || (size_t)idx >= w->n_items) return;
    w->sel = idx;
    car_settle(w, tk_widget_root(w), idx * (CAR_SMALL + CAR_GAP));
    tk_widget_invalidate(w);
}

void tk_carousel_set_on_select(tk_widget *w, tk_notify_cb cb,
                               void *ud)
{
    if (!w) return;
    w->cb1 = cb;
    w->cb1_ud = ud;
}

static float car_f(const tk_widget *w, int i)
{
    int step = CAR_SMALL + CAR_GAP;
    int d = i * step - w->scroll_y;
    if (d < 0) d = -d;
    if (d >= step) return 0.0f;
    return 1.0f - (float)d / (float)step;
}

static int car_w(const tk_widget *w, int i)
{
    return CAR_SMALL +
           (int)((CAR_LARGE - CAR_SMALL) * car_f(w, i) + 0.5f);
}

static int car_x(const tk_widget *w, int i)
{
    int x = -w->scroll_y;
    for (int j = 0; j < i; j++) x += car_w(w, j) + CAR_GAP;
    return x;
}

static void carousel_paint(tk_widget *w, tk_canvas *c,
                           const tk_theme *t)
{
    tk_rect r = w->rect;
    tk_canvas_push_clip(c, r);
    for (size_t i = 0; i < w->n_items; i++) {
        int iw = car_w(w, (int)i);
        int ix = r.x + car_x(w, (int)i);
        if (ix + iw < r.x || ix > r.x + r.w) continue;
        tk_rect q = { ix, r.y, iw, r.h };
        const uint32_t *px = m7_item_icon(w, (int)i);
        if (px) {
            tk_canvas_fill_round_rect(c, q, 33,
                                      role(t->container_high,
                                           t->surface_alt));
            m7_blit(c, px, w->item_icon_w, w->item_icon_h, q);
        } else {
            tk_canvas_fill_round_rect(c, q, 33,
                                      role(t->container_high,
                                           t->surface_alt));
        }
        /* label over the bottom edge, on the on-image fill */
        if (w->items[i] && w->items[i][0] && iw >= 100) {
            tk_canvas_fill_rect(c, (tk_rect){ ix, r.y + r.h - 34,
                                              iw, 34 },
                                (tk_color){ 0, 0, 0, 110 });
            tk_canvas_push_clip(c, q);
            tk_canvas_draw_text(c, t, ix + 12,
                                r.y + r.h - 34 +
                                    (34 - tk_line_height(t)) / 2,
                                w->items[i],
                                (tk_color){ 255, 255, 255, 255 });
            tk_canvas_pop_clip(c);
        }
    }
    tk_canvas_pop_clip(c);
}

static bool carousel_event(tk_widget *w, tk_root *root,
                           tk_event *ev)
{
    if (!w->n_items) return false;
    int max_off = ((int)w->n_items - 1) * (CAR_SMALL + CAR_GAP);
    if (ev->type == TK_EV_KEY_DOWN &&
        (ev->key == TK_KEY_LEFT || ev->key == TK_KEY_RIGHT)) {
        /* M14: the keyboard model - Left/Right move between
         * items, clamped at the ends; set_index starts the
         * same settle the pointer fling ends in */
        int d = ev->key == TK_KEY_RIGHT ? 1 : -1;
        int ns = tk_carousel_index(w) + d;
        if (ns < 0) ns = 0;
        if (ns >= (int)w->n_items) ns = (int)w->n_items - 1;
        if (ns != tk_carousel_index(w))
            tk_carousel_set_index(w, ns);
        return true;
    }
    switch (ev->type) {
    case TK_EV_POINTER_DOWN:
        if (!tk_rect_contains(w->rect, ev->x, ev->y)) return false;
        /* M13: a new drag takes the offset from any running
         * settle */
        if (w->anim_handle) {
            tk_anim_cancel(w->anim_handle);
            w->anim_handle = NULL;
        }
        w->ptr_down = true;
        w->car_start_x = ev->x;
        w->car_start_off = w->scroll_y;
        return true;
    case TK_EV_POINTER_MOVE:
        if (!w->ptr_down) return false;
        w->scroll_y = w->car_start_off - (ev->x - w->car_start_x);
        if (w->scroll_y < 0) w->scroll_y = 0;
        if (w->scroll_y > max_off) w->scroll_y = max_off;
        tk_widget_invalidate(w);
        return true;
    case TK_EV_POINTER_UP: {
        if (!w->ptr_down) return false;
        w->ptr_down = false;
        int dx = ev->x - w->car_start_x;
        if (dx < 6 && dx > -6) {
            /* tap: the item under the point becomes current */
            for (size_t i = 0; i < w->n_items; i++) {
                int ix = w->rect.x + car_x(w, (int)i);
                if (ev->x >= ix && ev->x < ix + car_w(w, (int)i)) {
                    if ((int)i != w->sel) {
                        w->sel = (int)i;
                        car_settle(w, root, /* M13: settle */
                                   (int)i * (CAR_SMALL + CAR_GAP));
                        tk_widget_invalidate(w);
                        tk_invoke(w->cb1, w, w->cb1_ud);
                    }
                    break;
                }
            }
        } else {
            int idx = (w->scroll_y + (CAR_SMALL + CAR_GAP) / 2) /
                      (CAR_SMALL + CAR_GAP);
            if (idx < 0) idx = 0;
            if (idx >= (int)w->n_items) idx = (int)w->n_items - 1;
            if (idx != w->sel) {
                w->sel = idx;
                tk_invoke(w->cb1, w, w->cb1_ud);
            }
            car_settle(w, root, /* M13: settle to the snap point */
                       w->sel * (CAR_SMALL + CAR_GAP));
            tk_widget_invalidate(w);
        }
        return true;
    }
    default:
        return false;
    }
}

/* ---------------------------------------------------------------- */
/* Navigation rail (M7) */

#define RAIL_W 94  /* 80dp */
#define RAIL_CELL 64

tk_widget *tk_navrail_new(void)
{
    tk_widget *w = m7_leaf(TK_W_NAVRAIL, NULL, true);
    if (w) w->sel = -1;
    return w;
}

void tk_navrail_add(tk_widget *w, const char *label,
                    const uint32_t *px, int iw, int ih)
{
    if (!w) return;
    m7_add_item(w, label);
    if (px) m7_set_item_icon(w, (int)w->n_items - 1, px, iw, ih);
    /* keep the per-item badge table parallel to items[] */
    int *nb = realloc(w->tab_badges, w->n_items * sizeof(int));
    if (nb) {
        nb[w->n_items - 1] = 0;
        w->tab_badges = nb;
    }
}

void tk_navrail_set_item_badge(tk_widget *w, int idx, int count)
{
    if (!w || !w->tab_badges || idx < 0 ||
        (size_t)idx >= w->n_items)
        return;
    w->tab_badges[idx] = count;
    tk_widget_invalidate(w);
}

int tk_navrail_selected(const tk_widget *w) { return w ? w->sel : -1; }

void tk_navrail_select(tk_widget *w, int idx)
{
    if (!w || idx < 0 || (size_t)idx >= w->n_items) return;
    int prev = w->sel;
    w->sel = idx;
    /* M16: the displayed indicator position follows the
     * selection - eased by tk_ind_anim_to when a live
     * animation clock is present, snapped otherwise (the
     * first selection always snaps) */
    if (prev < 0) w->ind_pos = (float)idx;
    tk_ind_anim_to(w, idx);
    tk_widget_invalidate(w);
}

void tk_navrail_set_on_select(tk_widget *w, tk_notify_cb cb,
                              void *ud)
{
    if (!w) return;
    w->cb1 = cb;
    w->cb1_ud = ud;
}

static void navrail_paint(tk_widget *w, tk_canvas *c,
                          const tk_theme *t)
{
    tk_rect r = w->rect;
    tk_canvas_fill_rect(c, r, t->style == 2
                                  ? role(t->layer, t->surface_alt)
                                  : t->surface);
    if (t->style == 2)
        tk_canvas_fill_rect(c, (tk_rect){ r.x + r.w - 1, r.y, 1,
                                          r.h }, t->border_line);
    int lh = tk_line_height_st(t, TK_TEXT_CAPTION);
    /* M16: while the selection animation runs, the indicator
     * is drawn from the DISPLAYED position (ind_pos, item
     * units): the WinUI form slides the subtle fill + accent
     * bar, the M3 form crossfades the pill between the two
     * items (the Compose NavigationRailItem indicator is a
     * fade, not a slide). At rest the per-item static form
     * below paints it, pixel-identical to pre-M16. */
    bool ind_anim = w->sel >= 0 &&
                    (w->anim_handle != NULL ||
                     w->ind_pos != (float)w->sel);
    if (ind_anim) {
        float pos = w->ind_pos;
        int lo = (int)pos;
        if (lo < 0) lo = 0;
        float frac = pos - (float)lo;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        int cx = r.x + r.w / 2;
        if (t->style == 2) {
            int cy = r.y + 8 + (int)(pos * (float)RAIL_CELL + 0.5f);
            tk_canvas_fill_rect(c, (tk_rect){ r.x, cy, r.w,
                                              RAIL_CELL },
                                role(t->subtle, t->surface_alt));
            tk_canvas_fill_rect(c, (tk_rect){ r.x, cy + 20, 3,
                                              24 }, t->accent);
        } else {
            tk_color base = t->style == 1
                ? t->selection
                : role(t->container, t->surface_alt);
            for (int pass = 0; pass < 2; pass++) {
                int idx = lo + pass;
                float f = pass == 0 ? 1.0f - frac : frac;
                if (f <= 0.001f || (size_t)idx >= w->n_items)
                    continue;
                tk_color col = base;
                col.a = (uint8_t)(col.a * f + 0.5f);
                int cy = r.y + 8 + idx * RAIL_CELL;
                tk_canvas_fill_round_rect(c,
                    (tk_rect){ cx - 32, cy + 2, 64, 37 }, 18, col);
            }
        }
    }
    for (size_t i = 0; i < w->n_items; i++) {
        tk_rect cell = { r.x, r.y + 8 + (int)i * RAIL_CELL, r.w,
                         RAIL_CELL };
        bool sel = (int)i == w->sel;
        int cx = r.x + r.w / 2;
        if (sel && !ind_anim && t->style == 2) {
            tk_canvas_fill_rect(c, cell,
                                role(t->subtle, t->surface_alt));
            tk_canvas_fill_rect(c, (tk_rect){ r.x, cell.y + 20, 3,
                                              24 }, t->accent);
        } else if (sel && !ind_anim) {
            /* M3 pill active indicator around the icon.
             * M13 role fix: the indicator is secondaryContainer
             * (the theme's selection token), NOT
             * primaryContainer - NavigationRailItemDefaults
             * (developer.android.com, fetched 2026-11-20):
             * indicatorColor = ItemActiveIndicator =
             * secondaryContainer. Classic keeps its fallback. */
            tk_canvas_fill_round_rect(c,
                (tk_rect){ cx - 32, cell.y + 2, 64, 37 }, 18,
                t->style == 1 ? t->selection
                              : role(t->container, t->surface_alt));
        }
        /* M13: the pressed item's state layer (M3 pressed
         * 10%): over the indicator for the selected item
         * (content role onSecondaryContainer), over the pill
         * region in onSurface for an unselected one. The
         * WinUI form's pressed state is the subtle fill at
         * the pressed alpha, as the button family does it. */
        if (w->pressed && (int)i == w->i1 && w->enabled) {
            if (t->style == 2) {
                tk_color pf = role(t->subtle, t->surface_alt);
                pf.a = 204;
                tk_canvas_fill_rect(c, cell, pf);
            } else {
                tk_color o = sel
                    ? (t->on_selection.a ? t->on_selection : t->text)
                    : t->text;
                o.a = 26;
                tk_canvas_fill_round_rect(c,
                    (tk_rect){ cx - 32, cell.y + 2, 64, 37 }, 18, o);
            }
        }
        const uint32_t *px = m7_item_icon(w, (int)i);
        if (px)
            m7_blit(c, px, w->item_icon_w, w->item_icon_h,
                    (tk_rect){ cx - 12, cell.y + 8, 24, 24 });
        if (w->tab_badges && w->tab_badges[i])
            m7_badge(c, t, cx + 14, cell.y + 8, w->tab_badges[i]);
        if (w->items[i]) {
            int tw = tk_text_width_st(t, TK_TEXT_CAPTION,
                                      w->items[i]);
            /* M13: a disabled rail's labels take the M3
             * disabled content form (onSurface at 38%) -
             * NavigationRailItemDefaults applies DisabledAlpha
             * to the icon / label colours; the indicator
             * colour has no disabled variant and stays. */
            tk_color lc = sel ? t->text : t->text_dim;
            if (!w->enabled && t->style == 1) {
                lc = t->text;
                lc.a = 97;
            }
            tk_canvas_push_clip(c, cell);
            tk_canvas_draw_text_st(c, t, TK_TEXT_CAPTION,
                                   cx - tw / 2, cell.y + 40,
                                   w->items[i], lc);
            tk_canvas_pop_clip(c);
        }
        (void)lh;
    }
}

static bool navrail_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    (void)root;
    if (ev->type == TK_EV_POINTER_DOWN &&
        tk_rect_contains(w->rect, ev->x, ev->y)) {
        int idx = (ev->y - w->rect.y - 8) / RAIL_CELL;
        w->i1 = (idx >= 0 && (size_t)idx < w->n_items) ? idx : -1;
        w->pressed = true;
        tk_widget_invalidate(w); /* M13: pressed layer paints */
        return true;
    }
    if (ev->type == TK_EV_POINTER_UP && w->pressed) {
        w->pressed = false;
        tk_widget_invalidate(w);
        int idx = tk_rect_contains(w->rect, ev->x, ev->y)
                      ? (ev->y - w->rect.y - 8) / RAIL_CELL : -1;
        if (idx == w->i1 && idx >= 0 && w->enabled) {
            w->sel = idx;
            tk_widget_invalidate(w);
            tk_invoke(w->cb1, w, w->cb1_ud);
        }
        return true;
    }
    if (ev->type == TK_EV_KEY_DOWN &&
        (ev->key == TK_KEY_UP || ev->key == TK_KEY_DOWN)) {
        int ns = w->sel + (ev->key == TK_KEY_DOWN ? 1 : -1);
        if (ns < 0) ns = 0;
        if (ns >= (int)w->n_items) ns = (int)w->n_items - 1;
        if (ns != w->sel && ns >= 0) {
            w->sel = ns;
            tk_widget_invalidate(w);
            tk_invoke(w->cb1, w, w->cb1_ud);
        }
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------- */
/* Ring (M7): circular determinate progress. The arc is plotted
 * as dots (the spinner's technique - the canvas has no arc
 * primitive), the track in the container-highest role, the arc
 * in the accent. */

tk_widget *tk_ring_new(float value)
{
    tk_widget *w = m7_leaf(TK_W_RING, NULL, false);
    if (!w) return NULL;
    w->fvalue = value < 0 ? 0 : value > 1 ? 1 : value;
    w->size_px = 0; /* M20: unset - the measure resolves the
                     * theme's default diameter (see below) */
    return w;
}

float tk_ring_value(const tk_widget *w) { return w ? w->fvalue : 0; }

void tk_ring_set_value(tk_widget *w, float v)
{
    if (!w) return;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    if (w->fvalue != v) {
        w->fvalue = v;
        tk_widget_invalidate(w);
    }
}

void tk_ring_set_size(tk_widget *w, int px)
{
    if (!w) return;
    w->size_px = px;
    tk_widget_invalidate(w);
}

/* M9: one continuous arc - discs of the stroke diameter
 * stepped at ~0.5px of arc so they overlap into a solid
 * stroke (the M7 form stepped 5px discs at 3-6 degrees,
 * which read as a dotted ring). */
static void ring_arc(tk_canvas *c, int cx, int cy, int rad,
                     int stroke, float from_deg, float to_deg,
                     tk_color col)
{
    if (to_deg <= from_deg) return;
    float step = 28.6f / (float)(rad > 0 ? rad : 1); /* ~0.5px */
    if (step > 4.0f) step = 4.0f;
    /* M21: stamp centres are rounded, not truncated - the
     * truncation biased every stamp up to 1px toward the
     * centre on the negative side, so the ink band sat off
     * the nominal radius asymmetrically (AUDIT SS18.2's
     * residual +1px at crossings). Rounding centres the
     * band on the path; the remaining width wobble is the
     * binary-raster quantization of a curved stroke. */
    for (float a = from_deg; a <= to_deg; a += step) {
        float ang = a * 0.0174533f;
        int px = cx + (int)roundf(rad * cosf(ang));
        int py = cy + (int)roundf(rad * sinf(ang));
        tk_canvas_fill_round_rect(c,
            (tk_rect){ px - stroke / 2, py - stroke / 2,
                       stroke, stroke },
            stroke / 2, col);
    }
    float ang = to_deg * 0.0174533f;
    int px = cx + (int)roundf(rad * cosf(ang));
    int py = cy + (int)roundf(rad * sinf(ang));
    tk_canvas_fill_round_rect(c,
        (tk_rect){ px - stroke / 2, py - stroke / 2,
                   stroke, stroke },
        stroke / 2, col);
}

static void ring_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    tk_rect r = w->rect;
    int s = r.w < r.h ? r.w : r.h;
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    /* M9: the stroke is the spec's - M3 circular progress
     * track 4dp = 5px; WinUI ProgressRingStrokeThickness
     * 4epx = 6px at the device scale (themeresources,
     * fetched 2026-11-20). The WinUI ring has NO track
     * circle (its Background is transparent); the M3 ring's
     * track is surfaceContainerHighest. */
    int stroke = t->style == 2 ? 6 : 5;
    int rad = s / 2 - 4;
    if (rad < 4) return;
    if (t->style != 2)
        ring_arc(c, cx, cy, rad, stroke, 0.0f, 360.0f,
                 role(t->container_highest, t->surface_alt));
    float sweep = w->fvalue * 360.0f;
    if (sweep > 0.5f)
        ring_arc(c, cx, cy, rad, stroke, -90.0f, -90.0f + sweep,
                 t->accent);
}

/* ---------------------------------------------------------------- */
/* Card (M7): a container kind - layout is the core's box path;
 * only the face lives here. */

tk_widget *tk_card_new(tk_card_variant v)
{
    tk_widget *w = tk_widget_new(TK_W_CARD);
    if (!w) return NULL;
    w->dir = TK_DIR_VERTICAL;
    w->padding = 19; /* 16dp */
    w->pad_x = 19; w->pad_y = 19;
    w->spacing = 8;
    w->card_variant = (int)v;
    return w;
}

void tk_card_set_variant(tk_widget *w, tk_card_variant v)
{
    if (!w || w->kind != TK_W_CARD) return;
    if (w->card_variant != (int)v) {
        w->card_variant = (int)v;
        tk_widget_invalidate(w);
    }
}

tk_card_variant tk_card_variant_of(const tk_widget *w)
{
    return w ? (tk_card_variant)w->card_variant : TK_CARD_ELEVATED;
}

static void card_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    tk_rect r = w->rect;
    if (t->style == 2) {
        /* Fluent card: CardBackground fill + card stroke */
        tk_canvas_fill_round_rect(c, r, t->radius_card,
                                  role(t->card_bg, t->surface));
        tk_canvas_stroke_round_rect(c, r, t->radius_card, 1,
                                    role(t->control_stroke,
                                         t->border));
        return;
    }
    int rad = t->style == 1 ? 14 : t->radius_card; /* 12dp = 14px */
    switch ((tk_card_variant)w->card_variant) {
    case TK_CARD_ELEVATED:
        tk_canvas_fill_round_rect(c, (tk_rect){ r.x + 1, r.y + 3,
                                                r.w, r.h },
                                  rad, (tk_color){ 0, 0, 0, 40 });
        tk_canvas_fill_round_rect(c, r, rad,
                                  role(t->container_low, t->surface));
        break;
    case TK_CARD_FILLED:
        tk_canvas_fill_round_rect(c, r, rad,
                                  role(t->container_highest,
                                       t->surface_alt));
        break;
    case TK_CARD_OUTLINED:
    default:
        tk_canvas_fill_round_rect(c, r, rad, t->surface);
        tk_canvas_stroke_round_rect(c, r, rad, 1, t->border_line);
        break;
    }
    if (t->style == 1 && w->cb1 && (w->pressed || w->ripple_on)) {
        /* M17: a clickable M3 card's pressed visual is the
         * ripple in the content colour - the disc is the
         * state layer (cards have no uniform layer under it).
         * Cards without an on_click stay inert, exactly as
         * before. */
        tk_color rl = t->text; rl.a = 26;
        tk_ripple_circle(w, c, r, rad, rl);
    }
}

/* M17: the clickable card's pointer handling. Before M17 a
 * card had no event path at all (tk_m7_event had no CARD
 * case): it could not be pressed and its on_click never
 * fired from pointer input. Events reach the card through
 * the dispatch chain when no child consumed them, so a
 * child button inside a clickable card still wins its own
 * presses. */
static bool card_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    if (!w->cb1) return false; /* not clickable: inert */
    if (ev->type == TK_EV_POINTER_DOWN &&
        tk_rect_contains(w->rect, ev->x, ev->y)) {
        w->pressed = true;
        tk_ripple_press(w, root, ev->x, ev->y);
        tk_widget_invalidate(w);
        return true;
    }
    if (ev->type == TK_EV_POINTER_UP) {
        bool was = w->pressed; w->pressed = false;
        tk_ripple_release(w, root);
        tk_widget_invalidate(w);
        if (was && root && root->suppress_click) return true;
        if (was && tk_rect_contains(w->rect, ev->x, ev->y)) {
            tk_invoke(w->cb1, w, w->cb1_ud);
            return true;
        }
        return was;
    }
    return false;
}

/* ---------------------------------------------------------------- */
/* AppBar (M7): container kind - the core lays out the leading
 * widget and the trailing actions; the face and title live
 * here. */

tk_widget *tk_appbar_new(const char *title)
{
    tk_widget *w = tk_widget_new(TK_W_APPBAR);
    if (!w) return NULL;
    w->text = tk_strdup(title);
    return w;
}

void tk_appbar_set_title(tk_widget *w, const char *title)
{
    if (!w) return;
    free(w->text);
    w->text = tk_strdup(title);
    tk_widget_invalidate(w);
}

const char *tk_appbar_title(const tk_widget *w)
{
    return w ? w->text : NULL;
}

void tk_appbar_set_leading(tk_widget *w, tk_widget *child)
{
    if (!w || !child) return;
    if (w->n_children == 0) {
        tk_add_child(w, child);
    } else {
        /* insert at index 0 (tk_add_child only appends) */
        if (w->n_children == w->cap_children) {
            size_t nc = w->cap_children ? w->cap_children * 2 : 4;
            tk_widget **nw = realloc(w->children,
                                     nc * sizeof(*nw));
            if (!nw) return;
            w->children = nw;
            w->cap_children = nc;
        }
        memmove(&w->children[1], &w->children[0],
                w->n_children * sizeof(*w->children));
        w->children[0] = child;
        child->parent = w;
        w->n_children++;
    }
    w->appbar_leading = true;
    tk_widget_invalidate(w);
}

void tk_appbar_set_scrolled(tk_widget *w, bool scrolled)
{
    if (!w) return;
    if (w->appbar_scrolled != scrolled) {
        w->appbar_scrolled = scrolled;
        tk_widget_invalidate(w);
    }
}

static void appbar_paint(tk_widget *w, tk_canvas *c,
                         const tk_theme *t)
{
    tk_rect r = w->rect;
    if (t->style == 2) {
        /* CommandBar: layer fill + bottom divider */
        tk_canvas_fill_rect(c, r, role(t->layer, t->surface_alt));
        tk_canvas_fill_rect(c, (tk_rect){ r.x, r.y + r.h - 1, r.w,
                                          1 }, t->border_line);
    } else if (t->style == 1) {
        /* surface; surfaceContainer (the ladder's middle
         * step, the theme's container_mid) once the content
         * scrolls - M9: the M3 top app bar's scrolled
         * container colour is surfaceContainer, not the low
         * step the M7 form approximated with */
        tk_canvas_fill_rect(c, r, w->appbar_scrolled
                                      ? role(t->container_mid,
                                             t->surface_alt)
                                      : t->surface);
    } else {
        tk_canvas_fill_rect(c, r, t->surface);
    }
    if (w->text) {
        int tx = r.x + 2 * t->unit;
        if (w->appbar_leading && w->n_children &&
            w->children[0]->rect.w > 0)
            tx = w->children[0]->rect.x + w->children[0]->rect.w +
                 t->unit;
        int lh = tk_line_height_st(t, TK_TEXT_TITLE);
        tk_canvas_push_clip(c, r);
        tk_canvas_draw_text_st(c, t, TK_TEXT_TITLE, tx,
                               r.y + (r.h - lh) / 2, w->text,
                               t->text);
        tk_canvas_pop_clip(c);
    }
}

/* ---------------------------------------------------------------- */
/* Navigation drawer (M7): an overlay kind - the core arranges
 * it over the whole root; the scrim (modal form) and the panel
 * are both painted here. Items live in menu_items[] (the drawer
 * add path is tk_menu_add_item's storage), the selection index
 * in sel, the last picked id in menu_last_id. */

#define DRAWER_ROW 64 /* 56dp row (62.7px at ~179.2ppi) + breathing room */
#define DRAWER_PAD 33 /* 28dp (31.4px at ~179.2ppi; 33 retained) */

tk_widget *tk_drawer_new(const char *headline)
{
    /* M15: the drawer is focusable - its items are controls
     * in both references (NavigationViewItem; the M3 drawer
     * item), reached and operated from the keyboard through
     * the widget's own cursor (see drawer_event). */
    tk_widget *w = m7_leaf(TK_W_DRAWER, headline, true);
    if (w) {
        w->sel = -1;
        w->menu_last_id = -1;
        w->menu_hover = -1; /* keyboard cursor: none yet */
    }
    return w;
}

void tk_drawer_add_item(tk_widget *w, const char *label, int id,
                        const uint32_t *px, int iw, int ih)
{
    if (!w) return;
    tk_menu_add_item(w, label, id);
    if (px && w->n_menu_items) {
        tk_menu_item *it = &w->menu_items[w->n_menu_items - 1];
        it->icon = px;
        it->icon_w = iw;
        it->icon_h = ih;
    }
}

void tk_drawer_add_separator(tk_widget *w)
{
    if (!w) return;
    tk_menu_add_separator(w);
}

void tk_drawer_set_item_badge(tk_widget *w, int id, int count)
{
    if (!w) return;
    for (size_t i = 0; i < w->n_menu_items; i++)
        if (!w->menu_items[i].separator && w->menu_items[i].id == id) {
            w->menu_items[i].badge = count;
            tk_widget_invalidate(w);
            return;
        }
}

void tk_drawer_select(tk_widget *w, int id)
{
    if (!w) return;
    for (size_t i = 0; i < w->n_menu_items; i++)
        if (!w->menu_items[i].separator && w->menu_items[i].id == id) {
            w->sel = (int)i;
            tk_widget_invalidate(w);
            return;
        }
}

int tk_drawer_selected(const tk_widget *w)
{
    if (!w || w->sel < 0 || (size_t)w->sel >= w->n_menu_items)
        return -1;
    return w->menu_items[w->sel].id;
}

int tk_drawer_last_id(const tk_widget *w)
{
    return w ? w->menu_last_id : -1;
}

void tk_drawer_set_on_select(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (!w) return;
    w->cb1 = cb;
    w->cb1_ud = ud;
}

/* M15: keyboard support. The cursor lives in menu_hover
 * (the menu family's field): Up/Down move it over the
 * selectable items, stopping at the ends (the focus-
 * navigation model of both references - no wrap); Enter /
 * Space activate exactly like a tap. */
static int drawer_first_selectable(const tk_widget *w)
{
    for (size_t i = 0; i < w->n_menu_items; i++)
        if (!w->menu_items[i].separator && w->menu_items[i].enabled)
            return (int)i;
    return -1;
}

static void drawer_cursor_sync(tk_widget *w)
{
    /* the cursor starts on the selection, else on the first
     * selectable item; an existing cursor is kept */
    if (w->sel >= 0 && (size_t)w->sel < w->n_menu_items &&
        !w->menu_items[w->sel].separator &&
        w->menu_items[w->sel].enabled)
        w->menu_hover = w->sel;
    else if (w->menu_hover < 0 ||
             (size_t)w->menu_hover >= w->n_menu_items ||
             w->menu_items[w->menu_hover].separator ||
             !w->menu_items[w->menu_hover].enabled)
        w->menu_hover = drawer_first_selectable(w);
}

static bool drawer_activate(tk_widget *w, tk_root *root, int idx)
{
    if (!root || idx < 0 || (size_t)idx >= w->n_menu_items)
        return false;
    tk_menu_item *it = &w->menu_items[idx];
    if (it->separator || !it->enabled) return false;
    w->sel = idx;
    w->menu_last_id = it->id;
    tk_widget_invalidate(w);
    tk_invoke(w->cb1, w, w->cb1_ud);
    if (w->drawer_modal) tk_drawer_close(root, w);
    return true;
}

static int drawer_panel_w(const tk_widget *w); /* below */

/* M12: drawer entrance / exit slides. The panel travels its
 * own width along x; the offset lives in drawer_off and is
 * applied by drawer_paint (the scrim fades with the same
 * progress). Durations are the mode's large-surface pair
 * (M3 medium2 300 ms Emphasized decelerate / WinUI Control-
 * Normal 250 ms). The exit engages only when the root's
 * animation clock advanced while the drawer was up - the
 * same live-loop gate as the dialog exit - so headless and
 * one-shot callers keep the synchronous close. */
static void drawer_off_cb(void *ud, int v)
{
    tk_widget *w = ud;
    w->drawer_off = v;
    tk_widget_invalidate(w);
}
static void drawer_enter_done(void *ud)
{
    tk_widget *w = ud;
    w->anim_handle = NULL;
    w->drawer_off = 0;
    tk_widget_invalidate(w);
}
static void drawer_exit_done(void *ud)
{
    tk_widget *w = ud;
    tk_root *r = tk_widget_root(w);
    w->anim_handle = NULL;
    w->drawer_closing = false;
    w->drawer_off = 0;
    if (r) tk_root_remove_overlay(r, w);
}

void tk_drawer_show(tk_root *r, tk_widget *w, bool modal)
{
    if (!r || !w) return;
    if (w->anim_handle) { tk_anim_cancel(w->anim_handle); w->anim_handle = NULL; }
    w->drawer_modal = modal;
    w->drawer_standard = false;
    w->drawer_closing = false;
    w->drawer_off = 0;
    w->show_clock = r->anim_now;
    tk_root_add_overlay(r, w);
    if (modal) r->modal = w;
    tk_root_layout(r, r->w, r->h);
    /* M15: the modal form takes the keyboard focus (the
     * references' modal drawer is a focus scope of its own);
     * closing hands it back through focus_return (see
     * tk_root_remove_overlay). The modeless form leaves the
     * focus where it is. */
    if (modal) {
        w->focus_return = (r->focused && r->focused != w)
                              ? r->focused : NULL;
        drawer_cursor_sync(w);
        tk_focus(r, w);
    } else {
        w->focus_return = NULL;
    }
    /* entrance slide: the modal form only - the standard form
     * is a persistent in-layout panel and does not travel */
    if (modal && r->anim_now != 0 &&
        tk_root_animations_enabled(r)) {
        int pw = drawer_panel_w(w);
        if (pw > 0) {
            uint32_t dur; tk_ease_fn ease;
            tk_anim_mode_pair(true, &dur, &ease);
            w->drawer_off = -pw;
            tk_widget_invalidate(w);
            w->anim_handle = tk_anim_start(r, -pw, 0, dur, ease,
                                           drawer_off_cb,
                                           drawer_enter_done, w);
            if (!w->anim_handle) w->drawer_off = 0;
        }
    }
}

/* M9: the M3 standard drawer - a persistent panel inside the
 * layout. Attached as a regular child (typically of a
 * horizontal box): the box gives it its measured width
 * (<= 360dp) and the full height, it paints square-cornered
 * with no scrim, and selection never dismisses it. */
void tk_drawer_attach(tk_widget *parent, tk_widget *w)
{
    if (!parent || !w) return;
    w->drawer_standard = true;
    w->drawer_modal = false;
    tk_add_child(parent, w);
}

bool tk_drawer_is_standard(const tk_widget *w)
{
    return w && w->drawer_standard;
}

void tk_drawer_close(tk_root *r, tk_widget *w)
{
    if (!r || !w) return;
    if (w->drawer_closing) return; /* exit already running */
    if (!w->drawer_standard &&
        r->anim_now > w->show_clock &&
        tk_root_animations_enabled(r) &&
        tk_root_has_overlay(r, w)) {
        int pw = drawer_panel_w(w);
        if (pw > 0) {
            uint32_t dur; tk_ease_fn ease;
            tk_anim_mode_pair(true, &dur, &ease);
            if (w->anim_handle) { tk_anim_cancel(w->anim_handle); w->anim_handle = NULL; }
            w->drawer_closing = true;
            w->anim_handle = tk_anim_start(r, w->drawer_off, -pw,
                                           dur, ease,
                                           drawer_off_cb,
                                           drawer_exit_done, w);
            /* a NULL handle means the exit was delivered
             * synchronously (drawer_off already at -pw):
             * drawer_exit_done has removed the overlay */
            return;
        }
    }
    tk_root_remove_overlay(r, w);
}

bool tk_drawer_is_shown(const tk_root *r, const tk_widget *w)
{
    return tk_root_has_overlay(r, w);
}

static int drawer_panel_w(const tk_widget *w)
{
    /* M9: in the standard (in-layout) form the widget's own
     * rect IS the panel */
    if (w->drawer_standard) return w->rect.w;
    int pw = w->desired.w;
    if (pw <= 0) pw = 360;
    if (pw > w->rect.w) pw = w->rect.w;
    return pw;
}

static void drawer_paint(tk_widget *w, tk_canvas *c,
                         const tk_theme *t)
{
    tk_rect r = w->rect;
    int pw = drawer_panel_w(w);
    /* M12: while the entrance / exit slide runs, the panel is
     * shifted by drawer_off and the scrim fades in proportion */
    int ox = r.x + w->drawer_off;
    if (w->drawer_modal) {
        tk_color sc = role(t->smoke, (tk_color){ 0, 0, 0, 77 });
        if (w->drawer_off < 0 && pw > 0) {
            int a = sc.a * (pw + w->drawer_off) / pw;
            sc.a = (uint8_t)(a < 0 ? 0 : a);
        }
        tk_canvas_fill_rect(c, r, sc);
    }
    tk_rect panel = { ox, r.y, pw, r.h };
    if (t->style == 2) {
        tk_canvas_fill_rect(c, panel, role(t->layer, t->surface));
        tk_canvas_fill_rect(c, (tk_rect){ ox + pw - 1, r.y, 1,
                                          r.h }, t->border_line);
    } else if (t->style == 1) {
        /* surface-container-low; the modal form rounds the
         * far side by 16dp, the standard (in-layout) form is
         * square-cornered - M3 navigation drawer specs */
        tk_color fill = role(t->container_low, t->surface);
        if (w->drawer_standard) {
            tk_canvas_fill_rect(c, panel, fill);
        } else {
            tk_canvas_fill_round_rect(c, panel, 19, fill);
            tk_canvas_fill_rect(c,
                (tk_rect){ ox, r.y, pw - 19, r.h }, fill);
        }
    } else {
        tk_canvas_fill_rect(c, panel, t->surface);
    }
    int y = r.y + 16;
    if (w->text) {
        tk_canvas_draw_text_st(c, t, TK_TEXT_TITLE,
                               ox + DRAWER_PAD, y + 8, w->text,
                               t->text_dim);
        y += 56;
    }
    int lh = tk_w_text_lh(w, t);
    for (size_t i = 0; i < w->n_menu_items; i++) {
        tk_menu_item *it = &w->menu_items[i];
        if (it->separator) {
            tk_canvas_fill_rect(c, (tk_rect){ ox + DRAWER_PAD,
                                              y + 8, pw - 2 * DRAWER_PAD,
                                              1 }, t->border_line);
            y += 17;
            continue;
        }
        bool sel = (int)i == w->sel;
        if (sel && t->style == 2) {
            tk_canvas_fill_rect(c, (tk_rect){ ox, y, pw,
                                              DRAWER_ROW },
                                role(t->subtle, t->surface_alt));
            tk_canvas_fill_rect(c, (tk_rect){ ox, y + 20, 3, 24 },
                                t->accent);
        } else if (sel) {
            /* M13 role fix: the M3 drawer indicator is
             * secondaryContainer (selection), NOT
             * primaryContainer - the same role the rail
             * indicator takes (NavigationDrawerItemDefaults
             * in the M3 spec set). Classic keeps its fallback. */
            tk_canvas_fill_round_rect(c,
                (tk_rect){ ox + 12, y + 4, pw - 24, DRAWER_ROW - 8 },
                (DRAWER_ROW - 8) / 2,
                t->style == 1 ? t->selection
                              : role(t->container, t->surface_alt));
        }
        /* M15: the keyboard cursor row, while the drawer
         * holds the focus (the focus ring frames the panel;
         * the cursor marks the item the keys will activate).
         * M3 form: the onSurface state layer at the 8% hover
         * opacity in the item's pill; WinUI form: the subtle
         * fill the selected row takes, without its accent
         * bar. */
        if (!sel && (int)i == w->menu_hover) {
            tk_root *dr = tk_widget_root(w);
            if (dr && dr->focused == w) {
                if (t->style == 2)
                    tk_canvas_fill_rect(c,
                        (tk_rect){ ox, y, pw, DRAWER_ROW },
                        role(t->subtle, t->surface_alt));
                else {
                    tk_color hov = t->text; hov.a = 20; /* 8% */
                    tk_canvas_fill_round_rect(c,
                        (tk_rect){ ox + 12, y + 4, pw - 24,
                                   DRAWER_ROW - 8 },
                        (DRAWER_ROW - 8) / 2, hov);
                }
            }
        }
        int tx = ox + DRAWER_PAD;
        if (it->icon) {
            m7_blit(c, it->icon, it->icon_w, it->icon_h,
                    (tk_rect){ tx, y + (DRAWER_ROW - 24) / 2,
                               24, 24 });
            tx += 36;
        }
        /* M13: the M3 active label takes onSecondaryContainer
         * (the theme's on_selection token; falls back to
         * onSurface where unset), and a disabled item takes
         * the disabled content form (onSurface at 38%) - the
         * pre-M13 paint used the unselected colour for both
         * the disabled label and (by role) the active one. */
        tk_color fg;
        if (!it->enabled && t->style == 1) {
            fg = t->text;
            fg.a = 97; /* onSurface 38% */
        } else if (!it->enabled && t->style == 2 && t->text_dis.a) {
            fg = t->text_dis; /* TextFillColorDisabled */
        } else if (!it->enabled) {
            fg = t->text_dim;
        } else if (sel && t->style == 1) {
            fg = t->on_selection.a ? t->on_selection : t->text;
        } else {
            fg = sel ? t->text : t->text_dim;
        }
        if (it->label)
            tk_canvas_draw_text(c, t, tx,
                                y + (DRAWER_ROW - lh) / 2, it->label,
                                fg);
        if (it->badge)
            m7_badge(c, t, ox + pw - DRAWER_PAD, y + DRAWER_ROW / 2,
                     it->badge);
        y += DRAWER_ROW;
    }
}

static bool drawer_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    /* M15: keyboard operation (the drawer itself holds the
     * focus; see tk_drawer_new). */
    if (ev->type == TK_EV_FOCUS_IN) {
        drawer_cursor_sync(w);
        tk_widget_invalidate(w);
        return false; /* focus events are not consumed */
    }
    if (ev->type == TK_EV_KEY_DOWN) {
        if (w->drawer_closing) return true; /* exit running */
        switch (ev->key) {
        case TK_KEY_DOWN: case TK_KEY_UP: {
            int dir = ev->key == TK_KEY_DOWN ? 1 : -1;
            if (w->menu_hover < 0) {
                w->menu_hover = drawer_first_selectable(w);
            } else {
                int nxt = w->menu_hover;
                for (;;) {
                    nxt += dir;
                    if (nxt < 0 || (size_t)nxt >= w->n_menu_items) {
                        nxt = w->menu_hover; /* stop at the ends */
                        break;
                    }
                    if (!w->menu_items[nxt].separator &&
                        w->menu_items[nxt].enabled)
                        break;
                }
                w->menu_hover = nxt;
            }
            tk_widget_invalidate(w);
            return true;
        }
        case TK_KEY_ENTER: case TK_KEY_SPACE:
            return drawer_activate(w, root, w->menu_hover);
        case TK_KEY_ESCAPE:
            /* the modal form dismisses (the references' Back /
             * Esc); the standard form is a persistent panel */
            if (w->drawer_modal) {
                tk_drawer_close(root, w);
                return true;
            }
            return false;
        default:
            return false;
        }
    }
    if (ev->type != TK_EV_POINTER_DOWN) return false;
    if (w->drawer_closing) return true; /* M12: exit running */
    int pw = drawer_panel_w(w);
    if (ev->x > w->rect.x + w->drawer_off + pw) {
        if (w->drawer_modal) {
            tk_drawer_close(root, w);
            return true;
        }
        return false; /* standard form: the tap passes by */
    }
    int y = w->rect.y + 16 + (w->text ? 56 : 0);
    for (size_t i = 0; i < w->n_menu_items; i++) {
        tk_menu_item *it = &w->menu_items[i];
        if (it->separator) {
            y += 17;
            continue;
        }
        if (ev->y >= y && ev->y < y + DRAWER_ROW) {
            if (it->enabled) (void)drawer_activate(w, root, (int)i);
            return true;
        }
        y += DRAWER_ROW;
    }
    return true; /* panel taps are the drawer's */
}

/* ---------------------------------------------------------------- */
/* Bottom / side sheet (M7): a container kind - the core's box
 * layout fills it (with the 30px handle zone reserved at the
 * top/left), layout_overlay anchors it; the face, the handle
 * and the dismiss drag live here. on_dismiss is cb1. */

tk_widget *tk_sheet_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_SHEET);
    if (!w) return NULL;
    w->dir = TK_DIR_VERTICAL;
    w->padding = 19;
    w->pad_x = 19; w->pad_y = 19;
    w->spacing = 8;
    /* M15: the sheet surface itself can hold the focus - the
     * fallback stop of the modal form (see tk_sheet_show),
     * like the dialog surface. */
    w->can_focus = true;
    return w;
}

void tk_sheet_set_edge(tk_widget *w, int edge)
{
    if (!w || w->kind != TK_W_SHEET) return;
    w->sheet_edge = edge;
    tk_widget_invalidate(w);
}

int tk_sheet_edge(const tk_widget *w)
{
    return w ? w->sheet_edge : 0;
}

void tk_sheet_show(tk_root *r, tk_widget *w, bool modal)
{
    if (!r || !w) return;
    w->sheet_modal = modal;
    w->sheet_off = 0;
    w->sheet_drag = false;
    w->sheet_closing = false;
    w->show_clock = r->anim_now;
    tk_root_add_overlay(r, w);
    if (modal) r->modal = w;
    tk_root_layout(r, r->w, r->h);
    /* M15: the modal form is focusable as a whole in the
     * references (M3 ModalBottomSheetProperties: isFocusable
     * defaults to true - the sheet receives key presses; like
     * a dialog it disables everything behind it), so showing
     * it moves the focus inside and closing returns it via
     * focus_return (tk_root_remove_overlay). The modeless
     * form leaves the focus where it is. */
    if (modal) {
        w->focus_return = (r->focused && r->focused != w &&
                           !tk_is_descendant(w, r->focused))
                              ? r->focused : NULL;
        (void)tk_focus_first(r, w);
    } else {
        w->focus_return = NULL;
    }
    /* M12: entrance slide along the dismiss axis (the panel
     * travels its own span), with the mode's large-surface
     * pair. No live clock / reduced motion: it simply appears,
     * as before. */
    if (r->anim_now != 0 && tk_root_animations_enabled(r)) {
        uint32_t dur; tk_ease_fn ease;
        tk_anim_mode_pair(true, &dur, &ease);
        if (w->sheet_edge == 1)
            tk_overlay_slide_in_x(r, w, w->rect.w, dur, ease);
        else
            tk_overlay_slide_in_dur(r, w, w->rect.h, dur, ease);
    }
}

/* M12: sheet exit completion - the slide-out machinery has
 * restored the geometry; remove the overlay and deliver
 * on_dismiss, exactly as the synchronous close does. */
static void sheet_exit_done(void *ud)
{
    tk_widget *w = ud;
    tk_root *r = tk_widget_root(w);
    w->sheet_closing = false;
    if (r) tk_root_remove_overlay(r, w);
    tk_invoke(w->cb1, w, w->cb1_ud); /* on_dismiss */
}

void tk_sheet_close(tk_root *r, tk_widget *w)
{
    if (!r || !w || !tk_root_has_overlay(r, w)) return;
    if (w->sheet_closing) return; /* exit already running */
    /* M12: exit slide along the dismiss axis, gated on a live
     * clock that advanced while the sheet was up (the dialog /
     * drawer gate); otherwise the historical synchronous
     * close. The drag-dismiss path funnels through here too:
     * the slide starts from the dragged position. */
    if (r->anim_now > w->show_clock &&
        tk_root_animations_enabled(r)) {
        uint32_t dur; tk_ease_fn ease;
        tk_anim_mode_pair(true, &dur, &ease);
        w->sheet_closing = true;
        if (w->sheet_edge == 1)
            tk_overlay_slide_out(r, w, w->rect.w, 0, dur, ease,
                                 sheet_exit_done, w);
        else
            tk_overlay_slide_out(r, w, 0, w->rect.h, dur, ease,
                                 sheet_exit_done, w);
        return;
    }
    tk_root_remove_overlay(r, w);
    tk_invoke(w->cb1, w, w->cb1_ud); /* on_dismiss */
}

bool tk_sheet_is_shown(const tk_root *r, const tk_widget *w)
{
    return tk_root_has_overlay(r, w);
}

void tk_sheet_set_on_dismiss(tk_widget *w, tk_notify_cb cb, void *ud)
{
    if (!w) return;
    w->cb1 = cb;
    w->cb1_ud = ud;
}

static void sheet_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    tk_rect r = w->rect;
    bool side = w->sheet_edge == 1;
    int rad = t->style == 2 ? t->radius_overlay : 33; /* 28dp */
    tk_color fill = t->style == 2 ? role(t->layer, t->surface)
                                  : role(t->container_low,
                                         t->surface);
    tk_canvas_fill_round_rect(c, r, rad, fill);
    if (side)
        tk_canvas_fill_rect(c, (tk_rect){ r.x + r.w / 2, r.y,
                                          r.w - r.w / 2, r.h }, fill);
    else
        tk_canvas_fill_rect(c, (tk_rect){ r.x, r.y + r.h / 2,
                                          r.w, r.h - r.h / 2 }, fill);
    if (t->style == 2)
        tk_canvas_stroke_round_rect(c, r, rad, 1,
                                    role(t->control_stroke,
                                         t->border));
    /* the drag handle (32dp x 4dp pill). M13 colour fix: in
     * the M3 mode the handle is onSurfaceVariant at 40%
     * opacity (the bottom sheet spec's drag-handle role),
     * NOT the opaque outline colour the pre-M13 paint used.
     * The WinUI / classic forms keep the border role. */
    tk_color handle = t->border;
    if (t->style == 1) {
        handle = t->text_dim; /* onSurfaceVariant */
        handle.a = 102;       /* 40% */
    }
    if (side)
        tk_canvas_fill_round_rect(c, (tk_rect){ r.x + 8,
                                                r.y + r.h / 2 - 18,
                                                5, 37 }, 2, handle);
    else
        tk_canvas_fill_round_rect(c, (tk_rect){ r.x + r.w / 2 - 18,
                                                r.y + 8, 37, 5 },
                                  2, handle);
}

static bool sheet_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    bool side = w->sheet_edge == 1;
    switch (ev->type) {
    case TK_EV_KEY_DOWN:
        /* M15: the modal form dismisses on Esc - the M3 Back
         * key (shouldDismissOnBackPress defaults to true;
         * this toolkit's Back is Esc) and the WinUI Flyout's
         * Esc light-dismiss. Keys bubble up from the sheet's
         * content, so this fires wherever inside the focus
         * rests, after the focused control's own handling. */
        if (ev->key == TK_KEY_ESCAPE && w->sheet_modal && root) {
            tk_sheet_close(root, w);
            return true;
        }
        return false;
    case TK_EV_POINTER_DOWN: {
        /* M12: a touch stops an entrance slide where it is;
         * the drag / layout paths own the position from here */
        if (w->anim_handle) { tk_anim_cancel(w->anim_handle); w->anim_handle = NULL; }
        bool in_handle = side ? (ev->x < w->rect.x + 30)
                              : (ev->y < w->rect.y + 30);
        if (in_handle && tk_rect_contains(w->rect, ev->x, ev->y)) {
            w->sheet_drag = true;
            w->sheet_start = side ? ev->x : ev->y;
            return true;
        }
        return false;
    }
    case TK_EV_POINTER_MOVE:
        if (!w->sheet_drag) return false;
        {
            int d = (side ? ev->x : ev->y) - w->sheet_start;
            if (d < 0) d = 0;
            if (d != w->sheet_off) {
                tk_widget_invalidate(w);
                w->sheet_off = d;
                tk_rect nr = w->rect;
                if (side) nr.x += d;
                else nr.y += d;
                tk_arrange(w, nr);
                tk_widget_invalidate(w);
            }
        }
        return true;
    case TK_EV_POINTER_UP:
        if (!w->sheet_drag) return false;
        w->sheet_drag = false;
        {
            int span = side ? w->rect.w : w->rect.h;
            if (w->sheet_off > span / 3) {
                w->sheet_off = 0;
                tk_sheet_close(root, w);
            } else {
                w->sheet_off = 0;
                if (root) tk_root_layout(root, root->w, root->h);
            }
        }
        return true;
    default:
        return false;
    }
}

/* ---------------------------------------------------------------- */
/* FAB menu (M7): a composed overlay - a vertical box of tonal
 * buttons over a full-screen scrim label whose event hook
 * dismisses. Per-item callbacks live in the box's ext (magic-
 * tagged so tk_m7_free_widget can release the table). */

#define FAB_MAGIC 0x4641424du /* "FABM" */
typedef struct { tk_notify_cb cb; void *ud; } fab_item_cb;
typedef struct {
    uint32_t magic;
    fab_item_cb *items;
    size_t n, cap;
    tk_widget *scrim; /* overlay while shown (auto_free) */
    /* M13: exit slide state (the dialog exit gate's fields) */
    bool closing;
    uint32_t show_clock;
} fab_state;

/* M15: the menu's keyboard model - the popup-menu model of
 * both references (and of this toolkit's own Menu, #21):
 * the focus sits on an item, Up/Down move between items
 * (wrapping, as in a menu), Tab cycles inside the popup
 * while it is up, Esc dismisses. The hook rides the box, so
 * keys reach it by bubbling from whichever item button
 * holds the focus; the invoker's focus returns on hide
 * through the box's focus_return (tk_fabmenu_show). */
static bool fab_box_hook(void *ud, tk_event *ev)
{
    tk_widget *menu = (tk_widget *)ud;
    if (!ev || ev->type != TK_EV_KEY_DOWN) return false;
    fab_state *st = menu ? menu->ext : NULL;
    if (!st || st->magic != FAB_MAGIC || st->closing)
        return false;
    tk_root *r = tk_widget_root(menu);
    if (!r || !tk_fabmenu_is_shown(r, menu)) return false;
    if (ev->key == TK_KEY_ESCAPE) {
        tk_fabmenu_hide(r, menu);
        return true;
    }
    if (ev->key != TK_KEY_DOWN && ev->key != TK_KEY_UP &&
        ev->key != TK_KEY_TAB)
        return false;
    int dir = (ev->key == TK_KEY_UP ||
               (ev->key == TK_KEY_TAB &&
                (ev->modifiers & TK_MOD_SHIFT))) ? -1 : 1;
    size_t ni = 0; long cur = -1;
    for (size_t i = 0; i < menu->n_children; i++) {
        tk_widget *ch = menu->children[i];
        if (!ch->enabled || !ch->visible) continue;
        if (ch == r->focused) cur = (long)ni;
        ni++;
    }
    if (ni == 0) return false;
    long nxt = cur < 0 ? (dir > 0 ? 0 : (long)ni - 1)
                       : (cur + dir + (long)ni) % (long)ni;
    size_t k = 0;
    for (size_t i = 0; i < menu->n_children; i++) {
        tk_widget *ch = menu->children[i];
        if (!ch->enabled || !ch->visible) continue;
        if ((long)k == nxt) { tk_focus(r, ch); break; }
        k++;
    }
    return true;
}

tk_widget *tk_fabmenu_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_BOX);
    if (!w) return NULL;
    fab_state *st = calloc(1, sizeof(*st));
    if (!st) {
        tk_widget_free(w);
        return NULL;
    }
    st->magic = FAB_MAGIC;
    w->ext = st;
    w->dir = TK_DIR_VERTICAL;
    w->spacing = 5; /* 4dp */
    w->padding = 8;
    w->pad_x = 8; w->pad_y = 8;
    w->backdrop = true;
    w->backdrop_shadow = true;
    tk_widget_set_event_hook(w, fab_box_hook, w);
    return w;
}

static void fab_item_click(tk_widget *btn, void *ud)
{
    tk_widget *menu = (tk_widget *)ud;
    fab_state *st = menu->ext;
    if (!st || st->magic != FAB_MAGIC) return;
    for (size_t i = 0; i < menu->n_children && i < st->n; i++) {
        if (menu->children[i] != btn) continue;
        tk_notify_cb cb = st->items[i].cb;
        void *cud = st->items[i].ud;
        tk_root *r = tk_widget_root(menu);
        if (r) tk_fabmenu_hide(r, menu);
        if (cb) cb(btn, cud);
        return;
    }
}

int tk_fabmenu_add_item(tk_widget *w, const uint32_t *px, int iw,
                        int ih, const char *label,
                        tk_notify_cb cb, void *ud)
{
    if (!w || !w->ext) return -1;
    fab_state *st = w->ext;
    if (st->magic != FAB_MAGIC) return -1;
    tk_widget *btn = tk_button_new(label);
    if (!btn) return -1;
    tk_button_set_variant(btn, TK_BTN_TONAL);
    if (px) {
        tk_button_set_icon(btn, px, iw, ih);
        tk_button_set_icon_layout(btn, TK_BTN_ICON_ROW);
    }
    tk_button_set_on_click(btn, fab_item_click, w);
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 4;
        fab_item_cb *ni = realloc(st->items, nc * sizeof(*ni));
        if (!ni) {
            tk_widget_free(btn);
            return -1;
        }
        st->items = ni;
        st->cap = nc;
    }
    st->items[st->n].cb = cb;
    st->items[st->n].ud = ud;
    tk_add_child(w, btn);
    return (int)st->n++;
}

static bool fab_scrim_hook(void *ud, tk_event *ev)
{
    tk_widget *menu = (tk_widget *)ud;
    if (ev->type == TK_EV_POINTER_DOWN) {
        tk_root *r = tk_widget_root(menu);
        if (r) tk_fabmenu_hide(r, menu);
        return true;
    }
    return ev->type == TK_EV_POINTER_UP || ev->type == TK_EV_POINTER_MOVE;
}

void tk_fabmenu_show(tk_root *r, tk_widget *w)
{
    if (!r || !w || !w->ext) return;
    fab_state *st = w->ext;
    if (st->magic != FAB_MAGIC || tk_fabmenu_is_shown(r, w)) return;
    st->closing = false;
    st->show_clock = r->anim_now;
    tk_widget *scrim = tk_label_new(NULL);
    if (scrim) {
        scrim->fixed_rect = (tk_rect){ 0, 0, r->w, r->h };
        scrim->auto_free = true;
        tk_widget_set_event_hook(scrim, fab_scrim_hook, w);
        st->scrim = scrim;
        tk_root_add_overlay(r, scrim);
    }
    tk_root_add_floating(r, w, 3, 16, 88);
    tk_root_layout(r, r->w, r->h);
    /* M13: entrance - the menu rises into place over the
     * mode's small pair (the overlay entrance mechanism; a
     * no-op without a live animation clock) */
    if (w->rect.h > 0) tk_overlay_slide_in(r, w, w->rect.h);
    /* M15: the popup takes the keyboard focus to its first
     * item (the menu model); hide hands it back through the
     * box's focus_return - tk_root_remove_floating funnels
     * into the overlay restore. */
    w->focus_return = (r->focused && r->focused != w &&
                       !tk_is_descendant(w, r->focused))
                          ? r->focused : NULL;
    for (size_t i = 0; i < w->n_children; i++) {
        tk_widget *ch = w->children[i];
        if (ch->enabled && ch->visible) { tk_focus(r, ch); break; }
    }
}

/* M13: the exit slide lands - only now does the menu leave
 * the root (the dialog exit pattern). */
static void fab_slide_done(void *ud)
{
    tk_widget *w = ud;
    fab_state *st = w->ext;
    if (st && st->magic == FAB_MAGIC) st->closing = false;
    tk_root *r = tk_widget_root(w);
    if (r && tk_root_is_floating(r, w)) tk_root_remove_floating(r, w);
}

void tk_fabmenu_hide(tk_root *r, tk_widget *w)
{
    if (!r || !w || !w->ext) return;
    fab_state *st = w->ext;
    if (st->magic != FAB_MAGIC) return;
    if (st->closing) return; /* exit already running */
    if (st->scrim) {
        /* M14 fix: tk_root_remove_overlay does NOT free
         * auto_free widgets - the caller owns that (the
         * menu close paths do it explicitly). This site
         * dropped the scrim on the floor, leaking one
         * widget per show/hide cycle. */
        tk_root_remove_overlay(r, st->scrim);
        tk_widget_free(st->scrim);
        st->scrim = NULL;
    }
    if (!tk_root_is_floating(r, w)) return;
    /* M13: exit - the menu slides back down and is removed
     * when the slide lands, but only when the animation clock
     * advanced while it was up (a live session loop); a
     * headless / same-tick hide stays synchronous, the
     * behaviour existing callers were written against (the
     * dialog exit gate, reused). */
    if (r->anim_now > st->show_clock &&
        tk_root_animations_enabled(r) && w->rect.h > 0) {
        uint32_t dur; tk_ease_fn ease;
        tk_anim_mode_pair(false, &dur, &ease);
        st->closing = true;
        tk_overlay_slide_out(r, w, 0, w->rect.h, dur, ease,
                             fab_slide_done, w);
        return;
    }
    tk_root_remove_floating(r, w);
}

bool tk_fabmenu_is_shown(const tk_root *r, const tk_widget *w)
{
    return tk_root_is_floating(r, w);
}

/* ---------------------------------------------------------------- */
/* Split button (M7): a composed pair - filled main button +
 * filled chevron button, 2dp apart; the menu rides in the
 * box's ovf_menu (owned, freed by the core). The chevron is a
 * procedural bitmap (the font subset carries no reliable
 * triangle glyph). */

static uint32_t g_chevron[16 * 16];
static bool g_chevron_ready;

static const uint32_t *chevron_px(void)
{
    if (g_chevron_ready) return g_chevron;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            /* distance to the two strokes (2,5)-(8,11)-(14,5) */
            float d1 = fabsf((float)y - (5.0f + (x - 2) * 1.0f));
            float d2 = fabsf((float)y - (11.0f - (x - 8) * 1.0f));
            float d = x <= 8 ? (x >= 2 ? d1 : 99.0f)
                             : (x <= 14 ? d2 : 99.0f);
            g_chevron[y * 16 + x] = d < 1.6f
                ? 0xFFFFFFFFu : 0x00000000u;
        }
    g_chevron_ready = true;
    return g_chevron;
}

/* M8: the chevron rotates 180 degrees as the menu opens and
 * back as it closes - the M3 split-button guideline ("The menu
 * button rotates inwards 180° when opened and closed", standard
 * motion scheme), fetched 2026-11-20 from
 * m3.material.io/components/split-button/guidelines. Driven on
 * tk_anim exactly like the Switch knob: chev_pos 0..1 maps to
 * 0..180 degrees at paint (tk_canvas_draw_image_rot). */
static void split_chev_cb(void *ud, int v)
{
    tk_widget *arrow = (tk_widget *)ud;
    arrow->chev_pos = v / 1000.0f;
    tk_widget_invalidate(arrow);
}
static void split_chev_done(void *ud)
{
    tk_widget *arrow = (tk_widget *)ud;
    arrow->anim_handle = NULL;
    arrow->chev_anim = false;
    tk_widget_invalidate(arrow);
}
static void split_chev_drive(tk_widget *arrow, bool open)
{
    if (!arrow) return;
    float target = open ? 1.0f : 0.0f;
    tk_root *r = tk_widget_root(arrow);
    if (!r || !tk_root_animations_enabled(r)) {
        arrow->chev_pos = target;
        arrow->chev_anim = false;
        return;
    }
    if (arrow->anim_handle) {
        tk_anim_cancel(arrow->anim_handle);
        arrow->anim_handle = NULL;
    }
    int from = (int)(arrow->chev_pos * 1000.0f + 0.5f);
    int to = open ? 1000 : 0;
    if (from == to) {
        arrow->chev_pos = target;
        arrow->chev_anim = false;
        return;
    }
    arrow->chev_anim = true;
    arrow->anim_handle = tk_anim_start(r, from, to, 200,
                                       tk_ease_in_out_cubic,
                                       split_chev_cb, split_chev_done,
                                       arrow);
    if (!arrow->anim_handle) arrow->chev_anim = false;
}

/* Called from every menu-removal path (tk_menu_close and
 * tk_close_all_menus in the core): if this menu belongs to a
 * split button, its chevron rotates home. */
void tk_split_on_menu_closed(tk_widget *menu)
{
    if (menu && menu->split_arrow)
        split_chev_drive(menu->split_arrow, false);
}

static void split_arrow_click(tk_widget *btn, void *ud)
{
    tk_widget *box = (tk_widget *)ud;
    tk_widget *menu = box->ovf_menu;
    tk_root *r = tk_widget_root(btn);
    if (!menu || !r) return;
    if (tk_menu_is_open(r, menu)) {
        /* toggle: a second click on the arrow closes its menu
         * (the close hook rotates the chevron home) */
        tk_menu_close(r, menu);
        return;
    }
    tk_size d = tk_measure(menu, (tk_constraints){ 0, TK_UNLIMITED,
                                                  0, TK_UNLIMITED });
    int x = btn->rect.x + btn->rect.w - d.w;
    if (x < 0) x = 0;
    tk_menu_popup(r, menu, x, btn->rect.y + btn->rect.h + 5);
    menu->scale_ox = 1000; /* M20: right-aligned to the button,
                            * so the entrance grows from the
                            * menu's right edge (anchor side) */
    split_chev_drive(btn, true);
}

tk_widget *tk_splitbutton_new(const char *label, tk_notify_cb cb,
                              void *ud)
{
    tk_widget *box = tk_widget_new(TK_W_BOX);
    if (!box) return NULL;
    box->dir = TK_DIR_HORIZONTAL;
    box->spacing = 2; /* 2dp */
    /* M18: both segments fill the bar's height (the
     * reference split form). The icon-only chevron button's
     * desired height is its 24px icon form, so without the
     * stretch it sat as a 24px square at the top of the
     * 56px bar - found by the M18 dimension audit. */
    tk_box_set_align(box, TK_ALIGN_STRETCH);
    tk_widget *main_btn = tk_button_new(label);
    tk_widget *arrow = tk_button_new(NULL);
    if (!main_btn || !arrow) {
        tk_widget_free(box);
        return NULL;
    }
    tk_button_set_variant(main_btn, TK_BTN_FILLED);
    tk_button_set_on_click(main_btn, cb, ud);
    tk_button_set_variant(arrow, TK_BTN_FILLED);
    tk_button_set_icon(arrow, chevron_px(), 16, 16);
    tk_button_set_icon_only(arrow, true);
    /* M13: the M3 split button's inner corners are the XS
     * shape (4dp = 5px at this scale), not the full outer
     * radius both halves previously wore (the split-button
     * token table: inner corner 4dp for the S/M sizes, the
     * 2dp segment gap already matched). The face painter
     * applies the asymmetry in the M3 mode only. */
    main_btn->btn_split_side = 1;
    main_btn->btn_split_inner = 5;
    arrow->btn_split_side = 2;
    arrow->btn_split_inner = 5;
    tk_button_set_on_click(arrow, split_arrow_click, box);
    tk_add_child(box, main_btn);
    tk_add_child(box, arrow);
    return box;
}

void tk_splitbutton_set_menu(tk_widget *w, tk_widget *menu)
{
    if (!w) return;
    tk_widget *arrow = (w->n_children > 1) ? w->children[1] : NULL;
    if (w->ovf_menu && w->ovf_menu != menu) {
        /* unlink the old menu before it is freed */
        if (w->ovf_menu->split_arrow == arrow && arrow)
            arrow->split_menu = NULL;
        w->ovf_menu->split_arrow = NULL;
        tk_widget_free(w->ovf_menu);
    }
    w->ovf_menu = menu;
    /* M8: link arrow <-> menu both ways for the chevron rotation
     * (the arrow paints rotated while its menu is open; every
     * menu close path drives it home via the back-link) */
    if (arrow) arrow->split_menu = menu;
    if (menu) menu->split_arrow = arrow;
}

tk_widget *tk_splitbutton_main(const tk_widget *w)
{
    return (w && w->n_children > 0) ? w->children[0] : NULL;
}

tk_widget *tk_splitbutton_arrow(const tk_widget *w)
{
    return (w && w->n_children > 1) ? w->children[1] : NULL;
}

/* ---------------------------------------------------------------- */
/* M7 dispatch + cleanup                                             */

tk_size tk_m7_measure(tk_widget *w, tk_constraints c)
{
    (void)c;
    const tk_theme *t = tk_theme_default();
    switch (w->kind) {
    case TK_W_SEARCHBAR:
        /* M18: the M3 SearchBar container height is a token
         * value of its own - 56dp (SearchBarTokens
         * ContainerHeight, androidx material3 tokens,
         * fetched 2026-11-20), 62.7px at the corrected
         * ~179.2ppi premise; the implemented 65px is
         * retained (REPORT-toolkit-m23.md) - not the
         * control height. The WinUI AutoSuggestBox form is
         * the TextBox floor form and the classic theme keeps
         * the control height. */
        return (tk_size){ 280, t->style == 1 ? 65 : t->control_h };
    case TK_W_SEGMENTED:
        /* M18: the M3 outlined segmented button height is
         * 40dp (OutlinedSegmentedButton Tokens, fetched
         * 2026-11-20), 44.8px at the corrected ~179.2ppi
         * premise; the implemented 47px is retained
         * (REPORT-toolkit-m23.md). The other modes keep
         * the toolkit's 48px form (no first-party segmented
         * height in the WinUI sources). */
        if (!w->n_items) return (tk_size){ 0, t->style == 1 ? 47 : 48 };
        return (tk_size){ seg_width(w, t) * (int)w->n_items,
                          t->style == 1 ? 47 : 48 };
    case TK_W_CAROUSEL:
        return (tk_size){ 360, 112 };
    case TK_W_NAVRAIL: {
        /* int10 (independent review C): rail labels are
         * centred and clipped to the cell, so a label wider
         * than the 80dp rail (the clock's ストップウォッチ,
         * ~110px at caption size) lost its first glyph at
         * the window edge (C10). The WinUI form sizes the
         * rail to its widest label, as NavigationView's
         * open pane does; the M3 form keeps the spec rail. */
        int rw = RAIL_W;
        if (t->style == 2) {
            for (size_t i = 0; i < w->n_items; i++)
                if (w->items[i]) {
                    int tw = tk_text_width_st(t, TK_TEXT_CAPTION,
                                              w->items[i]) + 16;
                    if (tw > rw) rw = tw;
                }
        }
        return (tk_size){ rw, (int)w->n_items * RAIL_CELL + 16 };
    }
    case TK_W_RING: {
        /* M20: an explicit size (tk_ring_set_size) is honoured
         * as-is. The unset default is the theme's: the M3
         * diameter is CircularProgressIndicatorTokens.Size
         * 40dp (ProgressIndicator.kt:
         * CircularIndicatorDiameter = Tokens.Size; token
         * value re-verified 2026-11-21), 44.8px at the
         * corrected ~179.2ppi premise; the implemented
         * 47px is retained (REPORT-toolkit-m23.md) - the
         * pre-M20 default 48px matched neither this
         * conversion nor a 48dp conversion (53.8px). The
         * WinUI form keeps the
         * toolkit's 48px (ProgressRing has no size token -
         * its A cell rests on the stroke pin). */
        int s = w->size_px > 0 ? w->size_px
                               : (t->style == 1 ? 47 : 48);
        return (tk_size){ s, s };
    }
    case TK_W_DRAWER: {
        int mw = 280;
        if (w->text) {
            int tw = tk_text_width_st(t, TK_TEXT_TITLE, w->text) +
                     2 * DRAWER_PAD;
            if (tw > mw) mw = tw;
        }
        for (size_t i = 0; i < w->n_menu_items; i++) {
            tk_menu_item *it = &w->menu_items[i];
            if (it->separator || !it->label) continue;
            int tw = DRAWER_PAD + (it->icon ? 36 : 0) +
                     tk_text_width(t, it->label) + DRAWER_PAD;
            if (tw > mw) mw = tw;
        }
        if (mw > 421) mw = 421; /* 360dp */
        int h = 16 + (w->text ? 56 : 0);
        for (size_t i = 0; i < w->n_menu_items; i++)
            h += w->menu_items[i].separator ? 17 : DRAWER_ROW;
        return (tk_size){ mw, h };
    }
    default:
        return (tk_size){ 80, 48 };
    }
}

void tk_m7_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    switch (w->kind) {
    case TK_W_CARD:      card_paint(w, c, t); break;
    case TK_W_SEARCHBAR: searchbar_paint(w, c, t); break;
    case TK_W_SEGMENTED: segmented_paint(w, c, t); break;
    case TK_W_CAROUSEL:  carousel_paint(w, c, t); break;
    case TK_W_NAVRAIL:   navrail_paint(w, c, t); break;
    case TK_W_RING:      ring_paint(w, c, t); break;
    case TK_W_APPBAR:    appbar_paint(w, c, t); break;
    case TK_W_DRAWER:    drawer_paint(w, c, t); break;
    case TK_W_SHEET:     sheet_paint(w, c, t); break;
    default: break;
    }
}

bool tk_m7_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    switch (w->kind) {
    case TK_W_SEARCHBAR: return searchbar_event(w, root, ev);
    case TK_W_CARD:      return card_event(w, root, ev);
    case TK_W_SEGMENTED: return segmented_event(w, root, ev);
    case TK_W_CAROUSEL:  return carousel_event(w, root, ev);
    case TK_W_NAVRAIL:   return navrail_event(w, root, ev);
    case TK_W_DRAWER:    return drawer_event(w, root, ev);
    case TK_W_SHEET:     return sheet_event(w, root, ev);
    default:             return false;
    }
}

void tk_m7_free_widget(tk_widget *w)
{
    free(w->a11y_label);
    w->a11y_label = NULL;
    free(w->tf_prefix);
    free(w->tf_suffix);
    free(w->tf_error_msg);
    w->tf_prefix = w->tf_suffix = w->tf_error_msg = NULL;
    if (w->tf_ac_menu) {
        tk_widget_free(w->tf_ac_menu);
        w->tf_ac_menu = NULL;
    }
    free(w->drag_mime);
    free(w->drag_data);
    free(w->drag_label);
    w->drag_mime = NULL;
    w->drag_data = NULL;
    w->drag_label = NULL;
    free(w->model_expanded);
    w->model_expanded = NULL;
    w->n_model_expanded = w->cap_model_expanded = 0;
    free(w->node_model_id);
    w->node_model_id = NULL;
    w->n_node_model_id = w->cap_node_model_id = 0;
    if (w->kind == TK_W_BOX && w->ext &&
        *(const uint32_t *)w->ext == FAB_MAGIC) {
        fab_state *st = w->ext;
        free(st->items);
        st->items = NULL;
        st->n = st->cap = 0;
    }
}
