/* assist.c - see assist.h.
 *
 * Hit-test note: the toolkit's hit test returns a container when
 * no child is hit, so a full-area overlay layer would swallow
 * every click meant for the views below. The layer is therefore
 * sized to the bounding box of the floating button plus (when
 * open) the panel, and nothing more; clicks outside that box fall
 * through to the desktop/launcher underneath.
 */
#include "assist.h"
#include "shellui.h" /* bd_icon_pixels */

#include <stdio.h>
#include <stdlib.h>

#define BTN 56          /* floating button size (px) */
#define EDGE 12         /* gap from the right edge */
#define ROW_H 56        /* panel row height: every target >= 56px */
#define PANEL_W 320     /* sheet width (UI-REDESIGN 2.3) */
#define STATUS_H 36     /* current-values line at the sheet top */
#define N_ACTIONS 8

struct item_ctx { bd_assist *a; bd_assist_action act; };

struct bd_assist {
    bd_assist_cb cb;
    void *ud;
    tk_widget *layer;     /* Fixed, sized to fab+panel bounds */
    tk_widget *fab;       /* floating action button */
    tk_widget *panel;     /* Fixed: action grid, hidden when closed */
    tk_widget *status;    /* current volume/brightness line */
    tk_widget *items[N_ACTIONS];
    struct item_ctx ctxs[N_ACTIONS];
    bool open;
    int w, h;             /* main-area size from the last layout */
    int avoid_right;      /* px strip to keep clear on the right */
    tk_rect fab_abs;      /* in main-area coordinates */
    tk_rect panel_abs;
};

static const char *labels[N_ACTIONS] = {
    "音量＋", "音量−", "輝度＋", "輝度−",
    "前ページ", "次ページ", "ランチャー", "ロック"
};

static bool closes_panel(bd_assist_action act)
{
    return act == BD_ASSIST_LAUNCHER || act == BD_ASSIST_LOCK;
}

static void on_item(tk_widget *w, void *ud)
{
    (void)w;
    struct item_ctx *c = ud;
    if (closes_panel(c->act)) bd_assist_set_open(c->a, false);
    if (c->a->cb) c->a->cb(c->a->ud, c->act);
}

static void on_fab(tk_widget *w, void *ud)
{
    (void)w;
    bd_assist *a = ud;
    bd_assist_set_open(a, !a->open);
}

static void relayout(bd_assist *a)
{
    if (a->w <= 0 || a->h <= 0) return;
    /* UI v2 refinement: when the shelf floats (theme
     * panel_margin > 0, the ChromeOS form), the button docks
     * into the shelf's own corner grid - same right margin as
     * the shelf, one margin above the main area's bottom edge -
     * instead of floating at an unrelated offset mid-edge.
     * The classic flush shelf keeps the original geometry. */
    int pm = tk_theme_default()->panel_margin;
    int edge = pm > 0 ? pm : EDGE;
    int bottom = pm > 0 ? pm : 24;
    int right = a->w - a->avoid_right;
    if (right < BTN + edge) right = a->w;
    a->fab_abs = (tk_rect){ right - BTN - edge, a->h - BTN - bottom,
                            BTN, BTN };
    int ph = STATUS_H + ROW_H * (N_ACTIONS / 2) + 4 * 3 + 16;
    int py = a->fab_abs.y - ph - 8;
    if (py < 0) py = 0;
    a->panel_abs = (tk_rect){ right - PANEL_W - edge, py, PANEL_W, ph };

    tk_rect box = a->fab_abs;
    if (a->open) {
        int x0 = a->panel_abs.x < box.x ? a->panel_abs.x : box.x;
        int y0 = a->panel_abs.y < box.y ? a->panel_abs.y : box.y;
        int x1 = a->panel_abs.x + a->panel_abs.w > box.x + box.w ?
            a->panel_abs.x + a->panel_abs.w : box.x + box.w;
        int y1 = a->panel_abs.y + a->panel_abs.h > box.y + box.h ?
            a->panel_abs.y + a->panel_abs.h : box.y + box.h;
        box = (tk_rect){ x0, y0, x1 - x0, y1 - y0 };
    }
    tk_set_fixed_rect(a->layer, box);
    tk_set_fixed_rect(a->fab, (tk_rect){ a->fab_abs.x - box.x,
                                         a->fab_abs.y - box.y,
                                         BTN, BTN });
    tk_set_fixed_rect(a->panel, (tk_rect){ a->panel_abs.x - box.x,
                                           a->panel_abs.y - box.y,
                                           a->panel_abs.w, a->panel_abs.h });
    tk_set_fixed_rect(a->status, (tk_rect){ 12, 6, PANEL_W - 24, 24 });
    for (int i = 0; i < N_ACTIONS; i++) {
        int col = i % 2, row = i / 2;
        tk_set_fixed_rect(a->items[i],
                          (tk_rect){ 8 + col * ((PANEL_W - 24) / 2 + 8),
                                     STATUS_H + row * (ROW_H + 4),
                                     (PANEL_W - 24) / 2, ROW_H });
    }
}

/* FAB glyph: a tap target (ring + centre dot) in the theme
 * accent, generated programmatically - reads as "touch assist"
 * without text. 40x40, glyph centred. (The accent was hardcoded
 * to the classic theme's blue, which clashed with the v2
 * palette; the classic accent is the same value, so the classic
 * rendering is unchanged.) */
/* The FAB symbol is the mode set's hand icon (Material
 * pan_tool / Fluent hand_right), accent-tinted, loaded through
 * the shell's icon-asset path - not a code-drawn mark
 * (REPORT-status-icons section 8). */

bd_assist *bd_assist_new(bd_assist_cb cb, void *userdata)
{
    bd_assist *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->cb = cb;
    a->ud = userdata;
    a->layer = tk_fixed_new();
    /* Circular floating button (UI-REDESIGN 2.3): 56px, radius
     * 28, icon only - the label string remains as its name. */
    a->fab = tk_button_new("補助");
    {
        bool winui = tk_theme_default()->style == 2;
        uint32_t *fab_px = NULL;
        if (bd_icon_pixels(winui ? "fluent" : "material",
                           winui ? "hand_right" : "pan_tool",
                           28, tk_theme_default()->accent, &fab_px)) {
            tk_button_set_icon(a->fab, fab_px, 28, 28);
            free(fab_px);
        }
    }
    tk_button_set_icon_only(a->fab, true);
    tk_button_set_corner_radius(a->fab, 28);
    tk_button_set_on_click(a->fab, on_fab, a);
    tk_add_child(a->layer, a->fab);
    /* the panel is a floating sheet: surface + border + shadow */
    a->panel = tk_fixed_new();
    tk_widget_set_backdrop(a->panel, true, -1);
    a->status = tk_label_new("");
    tk_widget_set_text_style(a->status, TK_TEXT_CAPTION);
    tk_add_child(a->panel, a->status);
    for (int i = 0; i < N_ACTIONS; i++) {
        a->items[i] = tk_button_new(labels[i]);
        a->ctxs[i].a = a;
        a->ctxs[i].act = (bd_assist_action)i;
        tk_button_set_on_click(a->items[i], on_item, &a->ctxs[i]);
        tk_add_child(a->panel, a->items[i]);
    }
    tk_set_visible(a->panel, false);
    tk_add_child(a->layer, a->panel);
    return a;
}

void bd_assist_set_avoid_right(bd_assist *a, int px)
{
    if (!a || a->avoid_right == px) return;
    a->avoid_right = px;
    relayout(a);
}

void bd_assist_set_values(bd_assist *a, int volume, int brightness)
{
    if (!a || !a->status) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "音量 %d%% ・ 輝度 %d%%", volume, brightness);
    tk_label_set_text(a->status, buf);
}

void bd_assist_free(bd_assist *a)
{
    if (!a) return;
    /* If the layer was attached to a parent it is owned by that
     * tree and freed with it; only free it here when standalone. */
    if (a->layer && !tk_parent(a->layer)) tk_widget_free(a->layer);
    free(a);
}

tk_widget *bd_assist_layer(bd_assist *a) { return a->layer; }

tk_rect bd_assist_button_rect(const bd_assist *a) { return a->fab_abs; }
tk_rect bd_assist_panel_rect(const bd_assist *a) { return a->panel_abs; }

void bd_assist_layout(bd_assist *a, int w, int h)
{
    a->w = w; a->h = h;
    relayout(a);
}

bool bd_assist_open(const bd_assist *a) { return a->open; }

void bd_assist_set_open(bd_assist *a, bool open)
{
    a->open = open;
    tk_set_visible(a->panel, open);
    relayout(a); /* layer bounds follow the panel's visibility */
}
