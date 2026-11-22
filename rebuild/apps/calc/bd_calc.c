/* bd_calc - see bd_calc.h. The UI builds expression strings
 * from key tokens and hands them to calcmodel; the parser never
 * sees UI concepts like "2nd" (the key registry swaps label and
 * token together). All programmatic widget writes are followed
 * by an explicit invalidate (the wave-1 rule). */
#define _POSIX_C_SOURCE 200809L
#include "bd_calc.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- key registry ---------------- */

#define KEY_ACT_NONE 0
#define KEY_ACT_EVAL 1
#define KEY_ACT_ACC  2   /* AC/C */
#define KEY_ACT_NEG  3   /* +/- */
#define KEY_ACT_SECOND 4
#define KEY_ACT_DEG 5
#define KEY_ACT_MC 6
#define KEY_ACT_MPLUS 7
#define KEY_ACT_MMINUS 8
#define KEY_ACT_MR 9
#define KEY_ACT_RAND 10

typedef struct {
    tk_widget *btn;
    calc_app *app;
    char label[24];        /* visible label */
    char token[24];        /* text appended to the expression */
    int action;
    int page;              /* 0 std, 1 sci (both expression), 2 prog */
    int face;              /* sci grids: 0 both, 1 normal, 2 second */
} ckey;

#define MAX_KEYS 160

struct calc_app {
    char dir[1024];
    int win_w, win_h;
    bool winui;                 /* tk_theme_default()->style == 2,
                                 * read once in the constructor
                                 * (bdappmode contract: constructors
                                 * never read the registry) */
    int tab_idx;                /* 0 std, 1 sci, 2 prog, 3 conv */
    tk_widget *pages[4];
    tk_widget *mode_tabbar;     /* Android: mode selector strip */
    tk_widget *mode_title;      /* WinUI: current mode title */
    tk_widget *mode_menu;       /* WinUI: mode menu (popup) */
    tk_widget *mem_label;       /* WinUI pane: memory readout */
    calc_state st;
    kc_hist *hist;
    tk_root *root;
    tk_widget *top, *tabs;
    tk_widget *expr_field, *result_label;
    tk_widget *hist_list;
    tk_widget *hist_recent;     /* newest entry, caption (P3) */
    tk_widget *hist_dlg;        /* history sheet (dialog) */
    ckey keys[MAX_KEYS];
    size_t nkeys;
    bool second;
    bool just_evaluated;
    char result[128];
    tk_widget *sci_grid_normal, *sci_grid_second;
    tk_widget *rad_check;
    /* programmer */
    tk_widget *prog_field, *prog_display, *prog_bits, *prog_info;
    tk_widget *prog_base_combo, *prog_bitpos;
    int prog_base;
    uint64_t prog_value;
    bool prog_valid;
    /* conversion */
    tk_widget *conv_cat, *conv_from, *conv_to, *conv_value;
    tk_widget *conv_result_label, *conv_rate_field, *conv_rate_unit;
    char conv_result[512];
    calc_rate rates[16];
    size_t n_rates;
};

/* ---------------- small helpers ---------------- */

static void hist_recent_sync(calc_app *app);
static void mem_label_sync(calc_app *app);

static void set_label(tk_widget *w, const char *text)
{
    tk_label_set_text(w, text);
    tk_widget_invalidate(w);
}

static void field_set(tk_widget *w, const char *text)
{
    tk_textfield_set_text(w, text);
    tk_widget_invalidate(w);
}

static void field_get(tk_widget *w, char *out, size_t cap)
{
    tk_textfield_text(w, out, cap);
}

static void click_widget(calc_app *app, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(app->root, &ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_UP;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(app->root, &ev);
}

/* ---------------- dual-mode chrome helpers ---------------- */

/* Icon button from the established vector sets (bdicon.h):
 * Material in Android mode, Fluent in WinUI mode. The label
 * stays on the button (tooltip / test-visible name) but the
 * button is icon-only, so no text glyph substitutes for the
 * icon. Falls back to a plain text button when the icon file
 * is missing (bd_icon tolerates NULL, as before). */
static tk_widget *icon_btn(calc_app *app, const char *label,
                           const char *material, const char *fluent,
                           tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    const tk_theme *t = tk_theme_default();
    uint32_t tint = ((uint32_t)t->text.r << 16) |
                    ((uint32_t)t->text.g << 8) | (uint32_t)t->text.b;
    const uint32_t *px = bd_icon_mode(app->winui, material, fluent,
                                      24, tint);
    if (px) {
        tk_button_set_icon(b, px, 24, 24);
        tk_button_set_icon_only(b, true);
    }
    tk_button_set_on_click(b, cb, ud);
    return b;
}

static const char *const mode_names[4] = {
    "標準", "関数", "プログラマ", "変換"
};

/* ---------------- expression editing ---------------- */

static void expr_append(calc_app *app, const char *token)
{
    char buf[1024];
    field_get(app->expr_field, buf, sizeof(buf));
    if (app->just_evaluated) {
        /* typing a digit/constant after '=' starts fresh;
         * an operator continues from the result */
        char c = token[0];
        if ((c >= '0' && c <= '9') || c == '.' || c == '(' ||
            (c >= 'a' && c <= 'z'))
            buf[0] = 0;
        app->just_evaluated = false;
    }
    strncat(buf, token, sizeof(buf) - strlen(buf) - 1);
    field_set(app->expr_field, buf);
}

static void expr_delete_token(calc_app *app)
{
    char buf[1024];
    field_get(app->expr_field, buf, sizeof(buf));
    size_t n = strlen(buf);
    if (n == 0) return;
    size_t i = n;
    unsigned char c = (unsigned char)buf[i - 1];
    if ((c >= '0' && c <= '9') || c == '.') {
        while (i > 0 && ((buf[i-1] >= '0' && buf[i-1] <= '9') ||
                         buf[i-1] == '.'))
            i--;
        /* swallow an exponent tail's sign too (1.5E-3 -> 1.5E) is
         * still malformed; drop the whole literal incl. E part */
        if (i > 0 && (buf[i-1] == 'E' || buf[i-1] == 'e')) {
            i--;
            if (i > 0 && (buf[i-1] == '+' || buf[i-1] == '-')) i--;
            while (i > 0 && ((buf[i-1] >= '0' && buf[i-1] <= '9') ||
                             buf[i-1] == '.'))
                i--;
        }
    } else if (c >= 'a' && c <= 'z') {
        while (i > 0 && buf[i-1] >= 'a' && buf[i-1] <= 'z') i--;
    } else {
        i--;   /* single-char operator / paren / symbol */
    }
    buf[i] = 0;
    field_set(app->expr_field, buf);
}

/* Backspace key (both modes): a real icon key, not a text
 * glyph. Deletes one token from the expression field (the
 * programmer field loses its last character instead). */
static void on_backspace(tk_widget *w, void *ud)
{
    (void)w;
    calc_app *app = ud;
    if (app->tab_idx == 2 && app->prog_field) {
        char buf[1024];
        field_get(app->prog_field, buf, sizeof(buf));
        size_t n = strlen(buf);
        if (n) { buf[n - 1] = 0; field_set(app->prog_field, buf); }
        return;
    }
    expr_delete_token(app);
}

static void do_eval(calc_app *app)
{
    char buf[1024];
    field_get(app->expr_field, buf, sizeof(buf));
    double v;
    kc_err e = calc_eval(buf, &app->st, &v);
    if (e != KC_OK) {
        snprintf(app->result, sizeof(app->result), "%s",
                 calc_err_str(e));
        set_label(app->result_label, app->result);
        app->just_evaluated = false;
        return;
    }
    char res[64];
    calc_format(v, res, sizeof(res));
    snprintf(app->result, sizeof(app->result), "= %s", res);
    set_label(app->result_label, app->result);
    kc_hist_add(app->hist, buf, res);
    /* refresh history list */
    size_t n = kc_hist_count(app->hist);
    const char **items = malloc((n ? n : 1) * sizeof(char *));
    for (size_t i = 0; i < n; i++)
        items[i] = kc_hist_at(app->hist, n - 1 - i);  /* newest first */
    tk_listview_set_items(app->hist_list, items, n);
    free(items);
    tk_widget_invalidate(app->hist_list);
    hist_recent_sync(app);
    field_set(app->expr_field, res);
    app->just_evaluated = true;
}

static void do_negate(calc_app *app)
{
    char buf[1024];
    field_get(app->expr_field, buf, sizeof(buf));
    if (buf[0] == 0) return;
    char out[1056];
    if (strncmp(buf, "-(", 2) == 0 && buf[strlen(buf) - 1] == ')') {
        /* unwrap */
        size_t n = strlen(buf);
        memmove(out, buf + 2, n - 3);
        out[n - 3] = 0;
    } else {
        snprintf(out, sizeof(out), "-(%s)", buf);
    }
    field_set(app->expr_field, out);
    app->just_evaluated = false;
}

/* ---------------- key handling ---------------- */

static void on_rad_toggle(tk_widget *w, void *ud)
{
    calc_app *app = ud;
    app->st.deg = !tk_checkbox_checked(w);
}

static void on_key(tk_widget *w, void *ud)
{
    (void)w;
    ckey *k = ud;
    calc_app *app = k->app;
    switch (k->action) {
    case KEY_ACT_EVAL: do_eval(app); return;
    case KEY_ACT_ACC:
        if (app->just_evaluated) {
            field_set(app->expr_field, "");
            set_label(app->result_label, "");
            app->result[0] = 0;
            app->just_evaluated = false;
        } else {
            char buf[64];
            field_get(app->expr_field, buf, sizeof(buf));
            if (buf[0] == 0) {
                set_label(app->result_label, "");
                app->result[0] = 0;
            } else expr_delete_token(app);
        }
        return;
    case KEY_ACT_NEG: do_negate(app); return;
    case KEY_ACT_SECOND:
        /* the toolkit has no button-label setter (recorded as
         * an API gap in the wave-2 report), so the scientific
         * function grid exists in two faces and 2nd swaps the
         * visible grid */
        app->second = !app->second;
        tk_set_visible(app->sci_grid_normal, !app->second);
        tk_set_visible(app->sci_grid_second, app->second);
        tk_widget_invalidate(app->sci_grid_normal);
        tk_widget_invalidate(app->sci_grid_second);
        /* the newly visible grid has no arranged rects yet */
        tk_root_layout(app->root, app->win_w, app->win_h);
        return;
    case KEY_ACT_DEG:   /* unused: Deg/Rad is the rad checkbox */
        return;
    case KEY_ACT_MC: calc_mem_clear(&app->st); mem_label_sync(app); return;
    case KEY_ACT_MPLUS: {
        char buf[1024];
        field_get(app->expr_field, buf, sizeof(buf));
        double v;
        if (calc_eval(buf, &app->st, &v) == KC_OK) calc_mem_add(&app->st, v);
        mem_label_sync(app);
        return;
    }
    case KEY_ACT_MMINUS: {
        char buf[1024];
        field_get(app->expr_field, buf, sizeof(buf));
        double v;
        if (calc_eval(buf, &app->st, &v) == KC_OK) calc_mem_sub(&app->st, v);
        mem_label_sync(app);
        return;
    }
    case KEY_ACT_MR: {
        char res[64];
        calc_format(app->st.memory, res, sizeof(res));
        expr_append(app, res);
        return;
    }
    case KEY_ACT_RAND: {
        char res[64];
        double v = (double)rand() / (double)RAND_MAX;
        calc_format(v, res, sizeof(res));
        expr_append(app, res);
        return;
    }
    default:
        expr_append(app, k->token);
        return;
    }
}

static void on_prog_key(tk_widget *w, void *ud)
{
    (void)w;
    ckey *k = ud;
    calc_app *app = k->app;
    if (strcmp(k->token, "=") == 0) {
        char buf[1024];
        field_get(app->prog_field, buf, sizeof(buf));
        uint64_t v;
        kc_err e = calc_prog_eval(buf, app->prog_base, &v);
        if (e == KC_OK) {
            app->prog_value = v;
            app->prog_valid = true;
            char d[80];
            calc_prog_format(v, app->prog_base, d, sizeof(d));
            set_label(app->prog_display, d);
            field_set(app->prog_field, d);
            char b[96];
            calc_prog_bits(v, b, sizeof(b));
            set_label(app->prog_bits, b);
            char ci[96];
            calc_prog_char_info(v, ci, sizeof(ci));
            set_label(app->prog_info, ci);
        } else {
            set_label(app->prog_display, calc_err_str(e));
        }
        return;
    }
    if (strcmp(k->token, "AC") == 0) {
        field_set(app->prog_field, "");
        set_label(app->prog_display, "0");
        set_label(app->prog_bits, "");
        set_label(app->prog_info, "");
        app->prog_valid = false;
        return;
    }
    if (strcmp(k->token, "FLIP") == 0) {
        if (app->prog_valid) {
            int pos = (int)tk_spinbutton_value(app->prog_bitpos);
            app->prog_value ^= (uint64_t)1 << (pos & 63);
            char d[80];
            calc_prog_format(app->prog_value, app->prog_base, d, sizeof(d));
            set_label(app->prog_display, d);
            field_set(app->prog_field, d);
            char b[96];
            calc_prog_bits(app->prog_value, b, sizeof(b));
            set_label(app->prog_bits, b);
            char ci[96];
            calc_prog_char_info(app->prog_value, ci, sizeof(ci));
            set_label(app->prog_info, ci);
        }
        return;
    }
    char buf[1024];
    field_get(app->prog_field, buf, sizeof(buf));
    strncat(buf, k->token, sizeof(buf) - strlen(buf) - 1);
    field_set(app->prog_field, buf);
}

/* ---------------- construction ---------------- */

static ckey *add_key_face(calc_app *app, tk_widget *parent, const char *label,
                          const char *token, int action, int page, bool prog,
                          int face)
{
    if (app->nkeys >= MAX_KEYS) return NULL;
    ckey *k = &app->keys[app->nkeys++];
    memset(k, 0, sizeof(*k));
    k->app = app;
    k->action = action;
    k->page = page;
    k->face = face;
    snprintf(k->label, sizeof(k->label), "%s", label);
    snprintf(k->token, sizeof(k->token), "%s", token);
    k->btn = tk_button_new(label);
    tk_button_set_on_click(k->btn, prog ? on_prog_key : on_key, k);
    tk_add_child(parent, k->btn);
    return k;
}

static ckey *add_key(calc_app *app, tk_widget *parent, const char *label,
                     const char *token, int action, int page, bool prog)
{
    return add_key_face(app, parent, label, token, action, page, prog, 0);
}

static tk_widget *key_grid(calc_app *app, int cols)
{
    (void)app;
    return tk_grid_new(cols, 4, 4, 4);
}

/* Key faces (UI-REDESIGN 2.4): digits keep the plain surface,
 * the operator column sits on surface_alt, "=" is the accent
 * primary, and the clear key reads in the danger colour. */
static void face_op(ckey *k)
{
    if (k) tk_button_set_face(k->btn, &tk_theme_default()->surface_alt);
}
static void face_clear(ckey *k)
{
    if (k) tk_widget_set_text_color(k->btn, tk_theme_default()->danger);
}

static void build_std_pad(calc_app *app, tk_widget *page)
{
    tk_widget *g = key_grid(app, 4);
    face_clear(add_key(app, g, "AC/C", "", KEY_ACT_ACC, 0, false));
    add_key(app, g, "+/-", "", KEY_ACT_NEG, 0, false);
    add_key(app, g, "%", "%", KEY_ACT_NONE, 0, false);
    face_op(add_key(app, g, "÷", "/", KEY_ACT_NONE, 0, false));
    add_key(app, g, "7", "7", KEY_ACT_NONE, 0, false);
    add_key(app, g, "8", "8", KEY_ACT_NONE, 0, false);
    add_key(app, g, "9", "9", KEY_ACT_NONE, 0, false);
    face_op(add_key(app, g, "×", "*", KEY_ACT_NONE, 0, false));
    add_key(app, g, "4", "4", KEY_ACT_NONE, 0, false);
    add_key(app, g, "5", "5", KEY_ACT_NONE, 0, false);
    add_key(app, g, "6", "6", KEY_ACT_NONE, 0, false);
    face_op(add_key(app, g, "−", "-", KEY_ACT_NONE, 0, false));
    add_key(app, g, "1", "1", KEY_ACT_NONE, 0, false);
    add_key(app, g, "2", "2", KEY_ACT_NONE, 0, false);
    add_key(app, g, "3", "3", KEY_ACT_NONE, 0, false);
    face_op(add_key(app, g, "+", "+", KEY_ACT_NONE, 0, false));
    add_key(app, g, "0", "0", KEY_ACT_NONE, 0, false);
    add_key(app, g, ".", ".", KEY_ACT_NONE, 0, false);
    add_key(app, g, "(", "(", KEY_ACT_NONE, 0, false);
    add_key(app, g, ")", ")", KEY_ACT_NONE, 0, false);
    /* the grid fills the page (M4): keys stretch with the window
     * instead of leaving the lower half blank */
    tk_set_flex(g, 1);
    tk_add_child(page, g);
    /* "=" is a full-width accent key under the grid (M4 + P3) */
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    ckey *eq = add_key(app, row, "=", "", KEY_ACT_EVAL, 0, false);
    if (eq) {
        tk_set_flex(eq->btn, 1);
        tk_button_set_accent(eq->btn, true);
    }
    tk_add_child(page, row);
}

/* One face of the scientific function grid. `second` selects
 * the inverse labels/tokens at the swappable positions; every
 * position exists in both faces so the two grids are layout-
 * identical and 2nd just swaps which one is visible. */
static void build_sci_face(calc_app *app, tk_widget *g, bool second)
{
    int face = second ? 2 : 1;
    add_key_face(app, g, "(", "(", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, ")", ")", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, "mc", "", KEY_ACT_MC, 1, false, face);
    add_key_face(app, g, "m+", "", KEY_ACT_MPLUS, 1, false, face);
    add_key_face(app, g, "m-", "", KEY_ACT_MMINUS, 1, false, face);
    add_key_face(app, g, "mr", "", KEY_ACT_MR, 1, false, face);
    add_key_face(app, g, "2nd", "", KEY_ACT_SECOND, 1, false, face);
    add_key_face(app, g, "Rand", "", KEY_ACT_RAND, 1, false, face);
    add_key_face(app, g, "EE", "E", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, "xʸ", "^", KEY_ACT_NONE, 1, false, face);
    if (!second) {
        add_key_face(app, g, "x²", "^2", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "eˣ", "e^(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "10ˣ", "10^(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "√", "sqrt(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "³√", "cbrt(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "ln", "ln(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "log₁₀", "log10(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "log₂", "log2(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "sin", "sin(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "cos", "cos(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "tan", "tan(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "sinh", "sinh(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "cosh", "cosh(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "tanh", "tanh(", KEY_ACT_NONE, 1, false, face);
    } else {
        add_key_face(app, g, "x³", "^3", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "ln", "ln(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "log₁₀", "log10(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "x²", "^2", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "x³", "^3", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "eˣ", "e^(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "10ˣ", "10^(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "2ˣ", "2^(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "sin⁻¹", "asin(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "cos⁻¹", "acos(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "tan⁻¹", "atan(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "sinh⁻¹", "asinh(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "cosh⁻¹", "acosh(", KEY_ACT_NONE, 1, false, face);
        add_key_face(app, g, "tanh⁻¹", "atanh(", KEY_ACT_NONE, 1, false, face);
    }
    add_key_face(app, g, "ʸ√x", "^(1/", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, "x!", "!", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, "1/x", "^(-1)", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, "π", "pi", KEY_ACT_NONE, 1, false, face);
    add_key_face(app, g, "e", "e", KEY_ACT_NONE, 1, false, face);
}

static void build_sci_pad(calc_app *app, tk_widget *page)
{
    app->sci_grid_normal = key_grid(app, 5);
    build_sci_face(app, app->sci_grid_normal, false);
    tk_set_flex(app->sci_grid_normal, 1);
    tk_add_child(page, app->sci_grid_normal);
    app->sci_grid_second = key_grid(app, 5);
    build_sci_face(app, app->sci_grid_second, true);
    tk_set_flex(app->sci_grid_second, 1);
    tk_set_visible(app->sci_grid_second, false);
    tk_add_child(page, app->sci_grid_second);
    tk_widget *mrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_add_child(mrow, tk_label_new("角度:"));
    app->rad_check = tk_checkbox_new("ラジアン (Rad)", false);
    tk_checkbox_set_on_change(app->rad_check, on_rad_toggle, app);
    tk_add_child(mrow, app->rad_check);
    tk_add_child(mrow, tk_label_new("（未チェック = 度 Deg）"));
    tk_add_child(page, mrow);
    /* digits/operators shared with the standard pad, same faces */
    tk_widget *g2 = key_grid(app, 4);
    face_clear(add_key(app, g2, "AC/C", "", KEY_ACT_ACC, 1, false));
    add_key(app, g2, "+/-", "", KEY_ACT_NEG, 1, false);
    add_key(app, g2, "%", "%", KEY_ACT_NONE, 1, false);
    face_op(add_key(app, g2, "÷", "/", KEY_ACT_NONE, 1, false));
    add_key(app, g2, "7", "7", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "8", "8", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "9", "9", KEY_ACT_NONE, 1, false);
    face_op(add_key(app, g2, "×", "*", KEY_ACT_NONE, 1, false));
    add_key(app, g2, "4", "4", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "5", "5", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "6", "6", KEY_ACT_NONE, 1, false);
    face_op(add_key(app, g2, "−", "-", KEY_ACT_NONE, 1, false));
    add_key(app, g2, "1", "1", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "2", "2", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "3", "3", KEY_ACT_NONE, 1, false);
    face_op(add_key(app, g2, "+", "+", KEY_ACT_NONE, 1, false));
    add_key(app, g2, "0", "0", KEY_ACT_NONE, 1, false);
    add_key(app, g2, ".", ".", KEY_ACT_NONE, 1, false);
    add_key(app, g2, "M", "M", KEY_ACT_NONE, 1, false);
    ckey *eq2 = add_key(app, g2, "=", "", KEY_ACT_EVAL, 1, false);
    if (eq2) tk_button_set_accent(eq2->btn, true);
    tk_set_flex(g2, 1);
    tk_add_child(page, g2);
}

static void build_prog_pad(calc_app *app, tk_widget *page)
{
    app->prog_display = tk_label_new("0");
    tk_add_child(page, app->prog_display);
    app->prog_bits = tk_label_new("");
    tk_add_child(page, app->prog_bits);
    app->prog_info = tk_label_new("");
    tk_add_child(page, app->prog_info);
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_add_child(row, tk_label_new("基数"));
    app->prog_base_combo = tk_combobox_new();
    const char *bases[] = { "16", "10", "8", "2" };
    tk_combobox_set_items(app->prog_base_combo, bases, 4);
    tk_combobox_select(app->prog_base_combo, 0);
    tk_add_child(row, app->prog_base_combo);
    tk_add_child(row, tk_label_new("ビット位置"));
    app->prog_bitpos = tk_spinbutton_new(0, 0, 63, 1);
    tk_add_child(row, app->prog_bitpos);
    add_key(app, row, "反転", "FLIP", KEY_ACT_NONE, 2, true);
    tk_add_child(page, row);
    app->prog_field = tk_textfield_new("");
    tk_add_child(page, app->prog_field);
    tk_widget *g = key_grid(app, 4);
    const char *hexd = "ABCDEF";
    for (int i = 0; i < 6; i++) {
        char lab[2] = { hexd[i], 0 };
        add_key(app, g, lab, lab, KEY_ACT_NONE, 2, true);
    }
    add_key(app, g, "AND", " AND ", KEY_ACT_NONE, 2, true);
    add_key(app, g, "OR", " OR ", KEY_ACT_NONE, 2, true);
    add_key(app, g, "XOR", " XOR ", KEY_ACT_NONE, 2, true);
    add_key(app, g, "NOR", " NOR ", KEY_ACT_NONE, 2, true);
    add_key(app, g, "NOT", "NOT ", KEY_ACT_NONE, 2, true);
    add_key(app, g, "<<", "<<", KEY_ACT_NONE, 2, true);
    add_key(app, g, ">>", ">>", KEY_ACT_NONE, 2, true);
    add_key(app, g, "AC", "AC", KEY_ACT_NONE, 2, true);
    add_key(app, g, "7", "7", KEY_ACT_NONE, 2, true);
    add_key(app, g, "8", "8", KEY_ACT_NONE, 2, true);
    add_key(app, g, "9", "9", KEY_ACT_NONE, 2, true);
    add_key(app, g, "÷", "/", KEY_ACT_NONE, 2, true);
    add_key(app, g, "4", "4", KEY_ACT_NONE, 2, true);
    add_key(app, g, "5", "5", KEY_ACT_NONE, 2, true);
    add_key(app, g, "6", "6", KEY_ACT_NONE, 2, true);
    add_key(app, g, "×", "*", KEY_ACT_NONE, 2, true);
    add_key(app, g, "1", "1", KEY_ACT_NONE, 2, true);
    add_key(app, g, "2", "2", KEY_ACT_NONE, 2, true);
    add_key(app, g, "3", "3", KEY_ACT_NONE, 2, true);
    add_key(app, g, "−", "-", KEY_ACT_NONE, 2, true);
    add_key(app, g, "0", "0", KEY_ACT_NONE, 2, true);
    add_key(app, g, "(", "(", KEY_ACT_NONE, 2, true);
    add_key(app, g, ")", ")", KEY_ACT_NONE, 2, true);
    add_key(app, g, "+", "+", KEY_ACT_NONE, 2, true);
    add_key(app, g, "=", "=", KEY_ACT_NONE, 2, true);
    tk_set_flex(g, 1);
    tk_add_child(page, g);
}

static void conv_refresh_units(calc_app *app)
{
    int cat = tk_combobox_selected(app->conv_cat);
    if (cat < 0) cat = 0;
    size_t n = calc_conv_units((size_t)cat);
    const char **names = malloc((n ? n : 1) * sizeof(char *));
    for (size_t i = 0; i < n; i++) names[i] = calc_conv_unit_name((size_t)cat, i);
    tk_combobox_set_items(app->conv_from, names, n);
    tk_combobox_set_items(app->conv_to, names, n);
    free(names);
    tk_combobox_select(app->conv_from, 0);
    tk_combobox_select(app->conv_to, 0);
    tk_widget_invalidate(app->conv_from);
    tk_widget_invalidate(app->conv_to);
    bool cur = calc_conv_is_currency((size_t)cat);
    tk_set_visible(app->conv_rate_field, cur);
    tk_set_visible(app->conv_rate_unit, cur);
}

static void on_conv_cat(tk_widget *w, void *ud)
{
    (void)w;
    conv_refresh_units(ud);
}

static void on_convert(tk_widget *w, void *ud)
{
    (void)w;
    calc_app *app = ud;
    calc_app_convert_now(app);
}

static void on_swap(tk_widget *w, void *ud)
{
    (void)w;
    calc_app *app = ud;
    int f = tk_combobox_selected(app->conv_from);
    int t = tk_combobox_selected(app->conv_to);
    tk_combobox_select(app->conv_from, t);
    tk_combobox_select(app->conv_to, f);
    tk_widget_invalidate(app->conv_from);
    tk_widget_invalidate(app->conv_to);
}

static void on_rate_set(tk_widget *w, void *ud)
{
    (void)w;
    calc_app *app = ud;
    int cat = tk_combobox_selected(app->conv_cat);
    int from = tk_combobox_selected(app->conv_from);
    if (cat < 0 || from < 0) return;
    const char *unit = calc_conv_unit_name((size_t)cat, (size_t)from);
    char buf[64];
    field_get(app->conv_rate_field, buf, sizeof(buf));
    double rate = strtod(buf, NULL);
    if (unit && rate > 0) calc_app_set_rate(app, unit, rate);
}

static void build_conv_page(calc_app *app, tk_widget *page)
{
    size_t nc = calc_conv_categories();
    const char **names = malloc(nc * sizeof(char *));
    for (size_t i = 0; i < nc; i++) names[i] = calc_conv_category_name(i);
    app->conv_cat = tk_combobox_new();
    tk_combobox_set_items(app->conv_cat, names, nc);
    free(names);
    tk_combobox_select(app->conv_cat, 0);
    tk_combobox_set_on_change(app->conv_cat, on_conv_cat, app);
    tk_add_child(page, app->conv_cat);
    tk_widget *row = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->conv_from = tk_combobox_new();
    app->conv_to = tk_combobox_new();
    tk_add_child(row, app->conv_from);
    /* swap-units: real icon (material swap_horiz / fluent
     * arrow_swap), not a text-glyph substitute */
    tk_widget *swap = icon_btn(app, "入替", "swap_horiz",
                               "arrow_swap", on_swap, app);
    tk_add_child(row, swap);
    tk_add_child(row, app->conv_to);
    tk_add_child(page, row);
    app->conv_value = tk_textfield_new("");
    tk_add_child(page, app->conv_value);
    tk_widget *go = tk_button_new("変換");
    tk_button_set_on_click(go, on_convert, app);
    tk_add_child(page, go);
    app->conv_result_label = tk_label_new("");
    tk_add_child(page, app->conv_result_label);
    tk_widget *rrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->conv_rate_unit = tk_label_new("1単位 = (円)");
    tk_add_child(rrow, app->conv_rate_unit);
    app->conv_rate_field = tk_textfield_new("");
    tk_add_child(rrow, app->conv_rate_field);
    tk_widget *setb = tk_button_new("レート設定");
    tk_button_set_on_click(setb, on_rate_set, app);
    tk_add_child(rrow, setb);
    tk_add_child(page, rrow);
    conv_refresh_units(app);
}

static void on_hist_activate(tk_widget *w, void *ud)
{
    calc_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel < 0) return;
    calc_app_history_load(app, (size_t)sel);
    if (app->hist_dlg) tk_dialog_close(app->root, app->hist_dlg);
}

static void on_hist_open(tk_widget *w, void *ud)
{
    (void)w;
    calc_app *app = ud;
    /* WinUI mode: the history pane is always visible, there
     * is no sheet to open. */
    if (!app->hist_dlg) return;
    tk_dialog_show(app->root, app->hist_dlg);
}

static void on_hist_close(tk_widget *w, void *ud)
{
    (void)w;
    calc_app *app = ud;
    tk_dialog_close(app->root, app->hist_dlg);
}

/* WinUI right pane: memory readout under the history list. */
static void mem_label_sync(calc_app *app)
{
    if (!app->mem_label) return;
    char res[64], buf[96];
    calc_format(app->st.memory, res, sizeof(res));
    snprintf(buf, sizeof(buf), "メモリ: %s", res);
    set_label(app->mem_label, buf);
}

/* The main screen keeps only the newest history entry (caption);
 * the full tape lives in the history sheet. Called wherever the
 * list is refreshed. */
static void hist_recent_sync(calc_app *app)
{
    size_t n = kc_hist_count(app->hist);
    if (n == 0) {
        set_label(app->hist_recent, "");
        return;
    }
    char buf[1200];
    snprintf(buf, sizeof(buf), "履歴: %s", kc_hist_at(app->hist, n - 1));
    set_label(app->hist_recent, buf);
}

static void rates_path(const calc_app *app, char *out, size_t cap)
{
    snprintf(out, cap, "%s/rates.txt", app->dir);
}

static void rates_save(calc_app *app)
{
    char buf[2048];
    size_t w = 0;
    for (size_t i = 0; i < app->n_rates; i++)
        w += (size_t)snprintf(buf + w, sizeof(buf) - w, "%s\t%.10g\n",
                              app->rates[i].unit, app->rates[i].per_jpy);
    char path[1100];
    rates_path(app, path, sizeof(path));
    bd_write_atomic(path, buf, w);
}

static void rates_load(calc_app *app)
{
    char path[1100];
    rates_path(app, path, sizeof(path));
    size_t len;
    char *data = bd_read_all(path, &len);
    if (!data) return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line && app->n_rates < 16;
         line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        calc_rate *r = &app->rates[app->n_rates];
        snprintf(r->unit, sizeof(r->unit), "%s", line);
        r->per_jpy = strtod(tab + 1, NULL);
        if (r->per_jpy > 0) app->n_rates++;
    }
    free(data);
}

/* ---------------- mode (page) switching ---------------- */

/* Single page-switch path for both modes: only the selector
 * chrome differs (Android tab strip / WinUI menu + title);
 * the pages and the public tab indices are the same. */
static void calc_show_page(calc_app *app, int tab)
{
    if (tab < 0) tab = 0;
    if (tab > 3) tab = 3;
    app->tab_idx = tab;
    for (int i = 0; i < 4; i++)
        if (app->pages[i]) {
            tk_set_visible(app->pages[i], i == tab);
            tk_widget_invalidate(app->pages[i]);
        }
    if (app->mode_tabbar)
        tk_tabbar_select(app->mode_tabbar, tab);
    if (app->mode_title)
        set_label(app->mode_title, mode_names[tab]);
    if (app->root) tk_root_layout(app->root, app->win_w, app->win_h);
}

static void on_mode_tabbar(tk_widget *w, void *ud)
{
    calc_app *app = ud;
    int sel = tk_tabbar_selected(w);
    if (sel >= 0 && sel != app->tab_idx) calc_show_page(app, sel);
}

static void on_mode_menu_select(tk_widget *menu, void *ud)
{
    calc_app *app = ud;
    int id = tk_menu_last_id(menu);
    if (id >= 0 && id != app->tab_idx) calc_show_page(app, id);
}

static void on_mode_menu_open(tk_widget *w, void *ud)
{
    calc_app *app = ud;
    if (!app->mode_menu || !app->root) return;
    tk_rect r = tk_widget_rect(w);
    tk_menu_popup(app->root, app->mode_menu, r.x, r.y + r.h);
}

calc_app *calc_app_new(const char *dir, int w, int h)
{
    calc_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->win_w = w;
    app->win_h = h;
    snprintf(app->dir, sizeof(app->dir), "%s", dir);
    calc_state_init(&app->st);
    app->hist = kc_hist_open(dir);
    app->prog_base = 16;
    rates_load(app);

    /* Mode: the constructor reads ONLY the toolkit default
     * theme's style (set by main() via bd_app_theme(), or
     * directly by tests / shot harnesses) - never the registry. */
    app->winui = tk_theme_default()->style == 2;

    /* app->top is assembled at the end of the mode branch
     * below (single column in Android mode, split in WinUI). */

    /* Pages are built once; only the chrome around them and
     * the page selector differ by mode (calc_show_page). */
    tk_widget *p_std = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *p_sci = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *p_prog = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_widget *p_conv = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    app->pages[0] = p_std;
    app->pages[1] = p_sci;
    app->pages[2] = p_prog;
    app->pages[3] = p_conv;
    build_std_pad(app, p_std);
    build_sci_pad(app, p_sci);
    build_prog_pad(app, p_prog);
    build_conv_page(app, p_conv);
    app->hist_list = tk_listview_new();
    tk_listview_set_on_activate(app->hist_list, on_hist_activate, app);

    /* Left column: [WinUI: menu + title] / display / selector
     * (Android tab strip) / pages / history caption row. */
    tk_widget *left = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    tk_set_flex(left, 1);
    if (app->winui) {
        /* Windows Calculator form: a mode menu button (real
         * icon: fluent navigation / material menu) at the top
         * left, with the current mode name as the title. */
        tk_widget *trow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_widget *mbtn = icon_btn(app, "モード", "menu",
                                   "navigation", on_mode_menu_open,
                                   app);
        tk_add_child(trow, mbtn);
        app->mode_title = tk_label_new(mode_names[0]);
        tk_widget_set_text_style(app->mode_title, TK_TEXT_TITLE);
        tk_add_child(trow, app->mode_title);
        tk_add_child(left, trow);
        app->mode_menu = tk_menu_new();
        tk_menu_set_on_select(app->mode_menu, on_mode_menu_select,
                              app);
        for (int i = 0; i < 4; i++)
            tk_menu_add_item(app->mode_menu, mode_names[i], i);
    }
    /* Display (UI-REDESIGN 2.4): the expression line (still an
     * editable field - keyboard users type into it) over the
     * result in the display size, right-aligned. The backspace
     * icon key sits at the display's right end (both modes). */
    tk_widget *drow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->expr_field = tk_textfield_new("");
    tk_set_flex(app->expr_field, 1);
    tk_add_child(drow, app->expr_field);
    tk_add_child(drow, icon_btn(app, "BS", "backspace", "backspace",
                                on_backspace, app));
    tk_add_child(left, drow);
    app->result_label = tk_label_new("");
    tk_widget_set_text_style(app->result_label, TK_TEXT_DISPLAY);
    tk_label_set_align(app->result_label, TK_LABEL_ALIGN_END);
    tk_add_child(left, app->result_label);
    if (!app->winui) {
        /* Android form (Google Calculator lineage): the mode
         * selector is a tab strip under the display. */
        app->mode_tabbar = tk_tabbar_new();
        for (int i = 0; i < 4; i++)
            tk_tabbar_add(app->mode_tabbar, mode_names[i]);
        tk_tabbar_set_on_select(app->mode_tabbar, on_mode_tabbar,
                                app);
        tk_add_child(left, app->mode_tabbar);
    }
    app->tabs = tk_box_new(TK_DIR_VERTICAL, 4, 0);
    for (int i = 0; i < 4; i++) {
        tk_set_flex(app->pages[i], 1);
        tk_add_child(app->tabs, app->pages[i]);
    }
    tk_set_flex(app->tabs, 1);
    tk_add_child(left, app->tabs);
    /* History (UI-REDESIGN 2.4): the main screen shows only the
     * newest entry as a caption plus a button; the full tape is
     * a sheet (dialog) - it no longer eats half the window.
     * The button is the history ICON (material / fluent
     * "history") in both modes; in WinUI mode the pane below
     * is the primary history surface and this row stays as the
     * compact caption. */
    tk_widget *hrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->hist_recent = tk_label_new("");
    tk_widget_set_text_style(app->hist_recent, TK_TEXT_CAPTION);
    tk_set_flex(app->hist_recent, 1);
    tk_add_child(hrow, app->hist_recent);
    tk_widget *hbtn = icon_btn(app, "履歴", "history", "history",
                               on_hist_open, app);
    tk_add_child(hrow, hbtn);
    tk_add_child(left, hrow);

    if (app->winui) {
        /* Windows Calculator form: the keypad column on the
         * left, a history / memory pane on the right (about
         * 240px at the 854px panel width; proportional on
         * narrower windows, floor 160). */
        tk_widget *split = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_add_child(split, left);
        tk_widget *pane = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        int pane_w = w * 240 / 854;
        if (pane_w < 160) pane_w = 160;
        if (pane_w > w / 2) pane_w = w / 2;
        tk_widget_set_min_size(pane, pane_w, 0);
        tk_widget *ptitle = tk_label_new("履歴");
        tk_widget_set_text_style(ptitle, TK_TEXT_HEADING);
        tk_add_child(pane, ptitle);
        tk_set_flex(app->hist_list, 1);
        tk_add_child(pane, app->hist_list);
        app->mem_label = tk_label_new("");
        tk_add_child(pane, app->mem_label);
        tk_add_child(split, pane);
        app->top = split;
    } else {
        app->top = tk_box_new(TK_DIR_VERTICAL, 4, 4);
        tk_add_child(app->top, left);
        app->hist_dlg = tk_dialog_new("履歴");
        tk_widget_set_min_size(app->hist_list, 224, 192);
        tk_add_child(app->hist_dlg, app->hist_list);
        tk_widget *hclose = tk_button_new("閉じる");
        tk_button_set_on_click(hclose, on_hist_close, app);
        tk_add_child(app->hist_dlg, hclose);
    }
    /* seed history list (newest first) */
    size_t n = kc_hist_count(app->hist);
    if (n) {
        const char **items = malloc(n * sizeof(char *));
        for (size_t i = 0; i < n; i++)
            items[i] = kc_hist_at(app->hist, n - 1 - i);
        tk_listview_set_items(app->hist_list, items, n);
        free(items);
    }
    hist_recent_sync(app);
    mem_label_sync(app);
    calc_show_page(app, 0);
    app->root = tk_root_new(app->top);
    tk_root_layout(app->root, w, h);
    return app;
}

void calc_app_free(calc_app *app)
{
    if (!app) return;
    if (app->hist_dlg) tk_widget_free(app->hist_dlg);
    if (app->mode_menu) tk_widget_free(app->mode_menu);
    kc_hist_close(app->hist);
    free(app);
}

tk_root *calc_app_root(calc_app *app) { return app->root; }

int calc_app_tab(const calc_app *app) { return app->tab_idx; }
void calc_app_set_tab(calc_app *app, int tab) { calc_show_page(app, tab); }

int calc_app_press(calc_app *app, const char *label)
{
    int tab = calc_app_tab(app);
    for (size_t i = 0; i < app->nkeys; i++) {
        ckey *k = &app->keys[i];
        bool on_page = (tab == 2 && k->page == 2) ||
                       ((tab == 0 || tab == 1) && k->page == tab);
        if (on_page && tab == 1 && k->face != 0 &&
            ((k->face == 1 && app->second) || (k->face == 2 && !app->second)))
            on_page = false;
        if (on_page && strcmp(k->label, label) == 0) {
            click_widget(app, k->btn);
            return 0;
        }
    }
    return -1;
}

void calc_app_expr(const calc_app *app, char *out, size_t cap)
{
    tk_textfield_text(app->expr_field, out, cap);
}
void calc_app_set_expr(calc_app *app, const char *text)
{
    field_set(app->expr_field, text);
    app->just_evaluated = false;
}
void calc_app_result(const calc_app *app, char *out, size_t cap)
{
    snprintf(out, cap, "%s", app->result);
}
double calc_app_memory(const calc_app *app) { return app->st.memory; }
bool calc_app_deg(const calc_app *app) { return app->st.deg; }
bool calc_app_second(const calc_app *app) { return app->second; }

size_t calc_app_history_rows(const calc_app *app)
{
    return tk_listview_count(app->hist_list);
}

int calc_app_history_load(calc_app *app, size_t row)
{
    size_t n = kc_hist_count(app->hist);
    if (row >= n) return -1;
    char expr[1024];
    kc_hist_expr_at(app->hist, n - 1 - row, expr, sizeof(expr));
    field_set(app->expr_field, expr);
    app->just_evaluated = false;
    return 0;
}

void calc_app_prog_display(const calc_app *app, char *out, size_t cap)
{
    if (app->prog_valid)
        calc_prog_format(app->prog_value, app->prog_base, out, cap);
    else snprintf(out, cap, "0");
}
void calc_app_prog_bits(const calc_app *app, char *out, size_t cap)
{
    if (app->prog_valid) calc_prog_bits(app->prog_value, out, cap);
    else if (cap) out[0] = 0;
}
int calc_app_prog_base(const calc_app *app) { return app->prog_base; }
void calc_app_prog_set_base(calc_app *app, int base)
{
    app->prog_base = base;
    int idx = base == 16 ? 0 : base == 10 ? 1 : base == 8 ? 2 : 3;
    tk_combobox_select(app->prog_base_combo, idx);
    /* re-render current value in the new base */
    if (app->prog_valid) {
        char d[80];
        calc_prog_format(app->prog_value, base, d, sizeof(d));
        set_label(app->prog_display, d);
        field_set(app->prog_field, d);
    }
}

void calc_app_conv_set_category(calc_app *app, size_t cat)
{
    tk_combobox_select(app->conv_cat, (int)cat);
    conv_refresh_units(app);
}
void calc_app_conv_set_units(calc_app *app, size_t from, size_t to)
{
    tk_combobox_select(app->conv_from, (int)from);
    tk_combobox_select(app->conv_to, (int)to);
}
void calc_app_conv_set_value(calc_app *app, const char *text)
{
    field_set(app->conv_value, text);
}

kc_err calc_app_convert_now(calc_app *app)
{
    int cat = tk_combobox_selected(app->conv_cat);
    int from = tk_combobox_selected(app->conv_from);
    int to = tk_combobox_selected(app->conv_to);
    if (cat < 0) cat = 0;
    if (from < 0) from = 0;
    if (to < 0) to = 0;
    char buf[128];
    field_get(app->conv_value, buf, sizeof(buf));
    double v = strtod(buf, NULL), out = 0;
    kc_err e = calc_convert_ex((size_t)cat, (size_t)from, (size_t)to, v,
                               app->rates, app->n_rates, &out);
    if (e == KC_OK) {
        char res[64];
        calc_format(out, res, sizeof(res));
        snprintf(app->conv_result, sizeof(app->conv_result), "%s %s = %s %s",
                 buf, calc_conv_unit_name((size_t)cat, (size_t)from), res,
                 calc_conv_unit_name((size_t)cat, (size_t)to));
    } else {
        snprintf(app->conv_result, sizeof(app->conv_result), "%s",
                 calc_err_str(e));
    }
    set_label(app->conv_result_label, app->conv_result);
    return e;
}

void calc_app_conv_result(const calc_app *app, char *out, size_t cap)
{
    snprintf(out, cap, "%s", app->conv_result);
}

int calc_app_set_rate(calc_app *app, const char *unit, double per_jpy)
{
    if (!unit || per_jpy <= 0) return -1;
    for (size_t i = 0; i < app->n_rates; i++)
        if (strcmp(app->rates[i].unit, unit) == 0) {
            app->rates[i].per_jpy = per_jpy;
            rates_save(app);
            return 0;
        }
    if (app->n_rates >= 16) return -1;
    calc_rate *r = &app->rates[app->n_rates++];
    snprintf(r->unit, sizeof(r->unit), "%s", unit);
    r->per_jpy = per_jpy;
    rates_save(app);
    return 0;
}

tk_widget *calc_app_expr_field(calc_app *app) { return app->expr_field; }
tk_widget *calc_app_result_label(calc_app *app) { return app->result_label; }
tk_widget *calc_app_history_list(calc_app *app) { return app->hist_list; }
tk_widget *calc_app_conv_value_field(calc_app *app) { return app->conv_value; }

#ifdef BD_CALC_MAIN
int main(void)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    const char *home = getenv("HOME");
    char root[1024];
    const char *env_root = getenv("BRAINUX_CALC_DIR");
    if (env_root) snprintf(root, sizeof(root), "%s", env_root);
    else if (home)
        snprintf(root, sizeof(root), "%s/.local/share/brainux/calc", home);
    else snprintf(root, sizeof(root), ".local/share/brainux/calc");
    bd_mkdir_p(root);
    srand(12345);

    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    /* Open the display first and build at its real size: a
     * hardcoded size that differs from the display makes every
     * flush a no-op (integration phase-2 defect). The shared
     * host probes the shell WM, then fbdev, then nothing; the
     * fallback size is only for the headless smoke paint below
     * (bdapp_run.h). */
    int vw, vh;
    /* declared minimum 480x360 (not the generic 480x272):
     * the keypad's five rows plus display and tab bar need
     * ~360 px to stay tappable; below that the grid compresses
     * to nothing. The WM enforces the declared minimum, so a
     * floating calc window can never be shrunk into the
     * unusable zone (foundation audit, matrix SS7). */
    bd_app_display *disp = bd_app_display_open_ex("電卓", 0, 0, 480, 360, 480, 272, &vw, &vh);
    calc_app *app = calc_app_new(root, vw, vh);
    if (!app) { fprintf(stderr, "bd-calc: cannot build UI\n"); return 1; }

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, NULL, NULL, 100);
    } else {
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-calc: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    calc_app_free(app);
    if (font) tk_font_free(font);
    return 0;
}
#endif /* BD_CALC_MAIN */
