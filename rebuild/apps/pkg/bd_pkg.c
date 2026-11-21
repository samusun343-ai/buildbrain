/* bd_pkg - see bd_pkg.h.
 *
 * Dual-mode (apps/common/bdappmode.h + bdicon.h): main() reads
 * the display mode from brainreg ui/mode via bd_app_theme();
 * the constructor only looks at tk_theme_default()->style
 * (2 = WinUI) and keeps it in app->winui. Android form = Play
 * Store app-management style (single column, view tabs on
 * top); WinUI form = Microsoft Store / Settings apps style
 * (left navigation pane + command row + list). Install /
 * remove / update actions carry real vector-set icons
 * (Material / Fluent) via bd_icon_mode. */
#define _POSIX_C_SOURCE 200809L
#include "bd_pkg.h"
#include "bdappmode.h"
#include "bdicon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { V_INST, V_CAT, V_UPD };

struct pkg_app {
    tk_root *root;
    tk_widget *top;
    pkg_sys *sys;
    bool winui;
    char catalog_dir[1024];

    tk_widget *view_btns[3];
    tk_widget *updates_label;
    tk_widget *search;
    tk_widget *list;
    tk_widget *d_name, *d_ver, *d_meta, *d_desc, *d_status;
    tk_widget *btn_install, *btn_remove, *btn_update_all, *dryrun;
    tk_widget *log;

    int view;
    char **row_labels;
    char **row_names;        /* package name per row */
    char **row_debs;         /* deb path per row (catalog/update) */
    size_t n_rows, cap_rows;
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

static void rows_clear(pkg_app *app)
{
    for (size_t i = 0; i < app->n_rows; i++) {
        free(app->row_labels[i]);
        free(app->row_names[i]);
        free(app->row_debs[i]);
    }
    app->n_rows = 0;
}
static void rows_push(pkg_app *app, const char *label, const char *name,
                      const char *deb)
{
    if (app->n_rows == app->cap_rows) {
        app->cap_rows = app->cap_rows ? app->cap_rows * 2 : 32;
        app->row_labels = realloc(app->row_labels,
                                  app->cap_rows * sizeof(char *));
        app->row_names = realloc(app->row_names,
                                 app->cap_rows * sizeof(char *));
        app->row_debs = realloc(app->row_debs,
                                app->cap_rows * sizeof(char *));
    }
    app->row_labels[app->n_rows] = strdup(label);
    app->row_names[app->n_rows] = strdup(name);
    app->row_debs[app->n_rows] = strdup(deb ? deb : "");
    app->n_rows++;
}

static bool name_matches(const pkg_entry *e, const char *q)
{
    return !q[0] || strstr(e->name, q) != NULL ||
           strstr(e->description, q) != NULL;
}

static void rebuild_rows(pkg_app *app)
{
    rows_clear(app);
    char q[128];
    field_text(app->search, q, sizeof(q));
    char lab[512];
    if (app->view == V_INST) {
        for (size_t i = 0; i < pkg_installed_count(app->sys); i++) {
            const pkg_entry *e = pkg_installed_at(app->sys, i);
            if (!name_matches(e, q)) continue;
            snprintf(lab, sizeof(lab), "%s  %s", e->name, e->version);
            rows_push(app, lab, e->name, NULL);
        }
    } else if (app->view == V_CAT) {
        for (size_t i = 0; i < pkg_catalog_count(app->sys); i++) {
            const pkg_entry *e = pkg_catalog_at(app->sys, i);
            if (!name_matches(e, q)) continue;
            snprintf(lab, sizeof(lab), "%s  %s", e->name, e->version);
            rows_push(app, lab, e->name, e->deb_path);
        }
    } else {
        for (size_t i = 0; i < pkg_updates_count(app->sys); i++) {
            const pkg_entry *ce = NULL;
            const pkg_entry *e = pkg_update_at(app->sys, i, &ce);
            if (!e || !ce) continue;
            if (!name_matches(e, q)) continue;
            snprintf(lab, sizeof(lab), "%s  %s → %s", e->name,
                     e->version, ce->version);
            rows_push(app, lab, e->name, ce->deb_path);
        }
    }
    tk_listview_set_items(app->list,
                          (const char *const *)app->row_labels,
                          app->n_rows);
    tk_widget_invalidate(app->list);
    char ub[64];
    size_t nu = pkg_updates_count(app->sys);
    snprintf(ub, sizeof(ub), "更新: %zu 件", nu);
    set_text(app->updates_label, ub);
}

static void refresh_detail(pkg_app *app)
{
    int idx = tk_listview_selected(app->list);
    if (idx < 0 || (size_t)idx >= app->n_rows) {
        set_text(app->d_name, "（未選択）");
        set_text(app->d_ver, "");
        set_text(app->d_meta, "");
        set_text(app->d_desc, "");
        set_text(app->d_status, "");
        return;
    }
    const char *name = app->row_names[idx];
    const pkg_entry *e = NULL;
    const pkg_entry *inst = pkg_installed_find(app->sys, name);
    /* find the exact entry for this row */
    if (app->view == V_CAT) {
        for (size_t i = 0; i < pkg_catalog_count(app->sys); i++) {
            const pkg_entry *c = pkg_catalog_at(app->sys, i);
            if (strcmp(c->deb_path, app->row_debs[idx]) == 0) e = c;
        }
    } else {
        e = inst;
    }
    if (!e) e = inst;
    if (!e) {
        set_text(app->d_name, name);
        return;
    }
    set_text(app->d_name, e->name);
    char buf[512];
    snprintf(buf, sizeof(buf), "バージョン %s / %s", e->version, e->arch);
    set_text(app->d_ver, buf);
    snprintf(buf, sizeof(buf), "分類 %s / サイズ %llu KB / %s",
             e->section, (unsigned long long)e->installed_size_kb,
             e->maintainer);
    set_text(app->d_meta, buf);
    set_text(app->d_desc, e->description);
    const pkg_entry *cat = pkg_catalog_find(app->sys, name);
    if (inst && cat && pkg_debver_cmp(cat->version, inst->version) > 0)
        snprintf(buf, sizeof(buf), "状態: インストール済み（更新 %s あり）",
                 cat->version);
    else if (inst)
        snprintf(buf, sizeof(buf), "状態: インストール済み");
    else
        snprintf(buf, sizeof(buf), "状態: 未インストール");
    set_text(app->d_status, buf);
}

static void on_select(tk_widget *w, void *ud)
{
    (void)w;
    refresh_detail(ud);
}
static void on_view(tk_widget *w, void *ud)
{
    pkg_app *app = ud;
    for (int i = 0; i < 3; i++)
        if (w == app->view_btns[i]) app->view = i;
    rebuild_rows(app);
    refresh_detail(app);
}
static void on_search(tk_widget *w, void *ud)
{
    (void)w;
    rebuild_rows(ud);
    refresh_detail(ud);
}
static void on_refresh(tk_widget *w, void *ud)
{
    (void)w;
    pkg_app_refresh(ud);
}

static void on_install(tk_widget *w, void *ud)
{
    (void)w;
    pkg_app *app = ud;
    int idx = tk_listview_selected(app->list);
    if (idx < 0 || (size_t)idx >= app->n_rows) return;
    const char *deb = app->row_debs[idx];
    if (!deb[0]) {
        set_text(app->log,
                 "インストールはカタログ/更新の一覧から選択してください");
        return;
    }
    char log[2048];
    bool dry = tk_checkbox_checked(app->dryrun);
    int rc = pkg_install(app->sys, deb, dry, log, sizeof(log));
    set_text(app->log, log);
    if (rc == 0 && !dry) {
        pkg_refresh_installed(app->sys);
        rebuild_rows(app);
        refresh_detail(app);
    }
}

/* Update All (reference flow: per-app Update + Update All):
 * installs every pending update in turn and reports a summary
 * in the log; a failure stops the run so the log names the
 * package that failed instead of burying it. */
static void on_update_all(tk_widget *w, void *ud)
{
    (void)w;
    pkg_app *app = ud;
    size_t nu = pkg_updates_count(app->sys);
    if (nu == 0) {
        set_text(app->log, "更新はありません");
        return;
    }
    bool dry = tk_checkbox_checked(app->dryrun);
    size_t done = 0;
    char log[2048];
    log[0] = 0;
    /* collect the deb paths first: installing refreshes the
     * installed set, which shifts the update indices */
    char **debs = calloc(nu, sizeof(char *));
    char **names = calloc(nu, sizeof(char *));
    for (size_t i = 0; i < nu; i++) {
        const pkg_entry *ce = NULL;
        const pkg_entry *e = pkg_update_at(app->sys, i, &ce);
        if (!e || !ce) continue;
        names[done] = strdup(e->name);
        debs[done] = strdup(ce->deb_path);
        done++;
    }
    size_t ok = 0;
    for (size_t i = 0; i < done; i++) {
        char one[1024];
        int rc = pkg_install(app->sys, debs[i], dry, one, sizeof(one));
        if (rc != 0) {
            snprintf(log, sizeof(log), "%s の更新に失敗: %s",
                     names[i], one);
            break;
        }
        ok++;
    }
    if (ok == done)
        snprintf(log, sizeof(log), "%zu 件すべて更新しました%s",
                 done, dry ? "（ドライラン）" : "");
    else if (log[0] == 0)
        snprintf(log, sizeof(log), "%zu / %zu 件を更新しました",
                 ok, done);
    for (size_t i = 0; i < done; i++) { free(debs[i]); free(names[i]); }
    free(debs);
    free(names);
    set_text(app->log, log);
    if (!dry && ok > 0) {
        pkg_refresh_installed(app->sys);
        rebuild_rows(app);
        refresh_detail(app);
    }
}

static void on_remove(tk_widget *w, void *ud)
{
    (void)w;
    pkg_app *app = ud;
    int idx = tk_listview_selected(app->list);
    if (idx < 0 || (size_t)idx >= app->n_rows) return;
    const char *name = app->row_names[idx];
    if (!pkg_installed_find(app->sys, name)) {
        set_text(app->log, "インストール済みではありません");
        return;
    }
    char log[2048];
    bool dry = tk_checkbox_checked(app->dryrun);
    int rc = pkg_remove(app->sys, name, dry, log, sizeof(log));
    set_text(app->log, log);
    if (rc == 0 && !dry) {
        pkg_refresh_installed(app->sys);
        rebuild_rows(app);
        refresh_detail(app);
    }
}

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

static void btn_icon(pkg_app *app, tk_widget *b, const char *mat,
                     const char *flu, tk_button_variant variant)
{
    const tk_theme *t = tk_theme_default();
    const uint32_t *px = bd_icon_mode(
        app->winui, mat, flu, 24,
        t ? theme_rgb(t->text) : 0x202020u);
    if (!px) return;
    tk_button_set_icon(b, px, 24, 24);
    tk_button_set_icon_layout(b, TK_BTN_ICON_ROW);
    tk_button_set_variant(b, variant);
}

static tk_widget *ibtn(pkg_app *app, tk_widget *parent,
                       const char *label, const char *mat,
                       const char *flu, tk_notify_cb cb,
                       tk_button_variant variant)
{
    tk_widget *b = btn(parent, label, cb, app);
    btn_icon(app, b, mat, flu, variant);
    return b;
}

pkg_app *pkg_app_new(const char *catalog_dir,
                     const char *dpkg_root, const char *dpkg_admin,
                     pkg_runner runner, void *ud, int w, int h)
{
    pkg_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    snprintf(app->catalog_dir, sizeof(app->catalog_dir), "%s",
             catalog_dir);
    app->sys = pkg_open(runner, ud, dpkg_root, dpkg_admin);
    if (!app->sys) { free(app); return NULL; }
    pkg_refresh_installed(app->sys);
    pkg_scan_catalog(app->sys, catalog_dir);

    app->winui = bd_app_winui(tk_theme_default());

    /* Structure branch:
     *  Android - single column: view tabs on top (Play Store
     *  app-management form), search, list, detail, actions.
     *  WinUI - left navigation pane (views stacked) + main
     *  column with a command row (search + refresh), list and
     *  detail side by side, actions, log (Store / Settings
     *  apps form). The same widgets exist in both forms. */
    tk_widget *top, *main_col;
    if (app->winui) {
        top = tk_box_new(TK_DIR_HORIZONTAL, 4, 4);
        app->top = top;
        tk_widget *nav = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_add_child(top, nav);
        tk_widget_set_min_size(nav, 208, 0);
        app->view_btns[0] = ibtn(app, nav, "インストール済み",
                                 "apps", "apps", on_view,
                                 TK_BTN_TONAL);
        app->view_btns[1] = ibtn(app, nav, "カタログ",
                                 "inventory_2", "library", on_view,
                                 TK_BTN_TONAL);
        app->view_btns[2] = ibtn(app, nav, "更新",
                                 "download_done", "arrow_download",
                                 on_view, TK_BTN_TONAL);
        app->updates_label = lbl(nav, "更新: 0 件");
        main_col = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_add_child(top, main_col);
        tk_set_flex(main_col, 1);
        tk_widget *crow = hbox(main_col);
        app->search = tk_textfield_new("");
        tk_textfield_set_placeholder(app->search, "パッケージ検索");
        tk_textfield_set_on_change(app->search, on_search, app);
        tk_add_child(crow, app->search);
        tk_set_flex(app->search, 1);
        ibtn(app, crow, "再読込", "refresh", "arrow_clockwise",
             on_refresh, TK_BTN_TONAL);
    } else {
        top = tk_box_new(TK_DIR_VERTICAL, 4, 4);
        app->top = top;
        main_col = top;
        tk_widget *vrow = hbox(top);
        app->view_btns[0] = ibtn(app, vrow, "インストール済み",
                                 "apps", "apps", on_view,
                                 TK_BTN_TONAL);
        app->view_btns[1] = ibtn(app, vrow, "カタログ",
                                 "inventory_2", "library", on_view,
                                 TK_BTN_TONAL);
        app->view_btns[2] = ibtn(app, vrow, "更新",
                                 "download_done", "arrow_download",
                                 on_view, TK_BTN_TONAL);
        app->updates_label = lbl(vrow, "更新: 0 件");
        ibtn(app, vrow, "再読込", "refresh", "arrow_clockwise",
             on_refresh, TK_BTN_TONAL);
        tk_widget *srow = hbox(top);
        app->search = tk_textfield_new("");
        tk_textfield_set_placeholder(app->search, "パッケージ検索");
        tk_textfield_set_on_change(app->search, on_search, app);
        tk_add_child(srow, app->search);
        tk_set_flex(app->search, 1);
    }

    tk_widget *mid;
    if (app->winui) {
        mid = hbox(main_col);
        tk_set_flex(mid, 1);
    } else {
        mid = NULL;
    }
    app->list = tk_listview_new();
    tk_listview_set_on_select(app->list, on_select, app);
    tk_add_child(app->winui ? mid : main_col, app->list);
    tk_set_flex(app->list, 1);
    tk_widget *det = tk_box_new(TK_DIR_VERTICAL, 3, 0);
    tk_add_child(app->winui ? mid : main_col, det);
    if (app->winui) tk_set_flex(det, 1);
    app->d_name = lbl(det, "（未選択）");
    app->d_ver = lbl(det, "");
    app->d_meta = lbl(det, "");
    app->d_desc = lbl(det, "");
    app->d_status = lbl(det, "");

    tk_widget *arow = hbox(main_col);
    app->btn_install = ibtn(app, arow, "インストール", "download",
                            "arrow_download", on_install,
                            TK_BTN_FILLED);
    app->btn_update_all = ibtn(app, arow, "全て更新", "refresh",
                               "arrow_clockwise", on_update_all,
                               TK_BTN_TONAL);
    app->btn_remove = ibtn(app, arow, "削除", "delete", "delete",
                           on_remove, TK_BTN_TONAL);
    app->dryrun = tk_checkbox_new("ドライラン（実行しない）", false);
    tk_add_child(arow, app->dryrun);
    app->log = lbl(main_col, "");

    app->root = tk_root_new(top);
    tk_root_layout(app->root, w, h);
    rebuild_rows(app);
    refresh_detail(app);
    return app;
}

void pkg_app_free(pkg_app *app)
{
    if (!app) return;
    pkg_close(app->sys);
    rows_clear(app);
    free(app->row_labels);
    free(app->row_names);
    free(app->row_debs);
    tk_root_free(app->root);
    tk_widget_free(app->top);
    free(app);
}

void pkg_app_refresh(pkg_app *app)
{
    pkg_refresh_installed(app->sys);
    pkg_scan_catalog(app->sys, app->catalog_dir);
    rebuild_rows(app);
    refresh_detail(app);
}

size_t pkg_app_rows(const pkg_app *app) { return app->n_rows; }
size_t pkg_app_updates(const pkg_app *app)
{
    return pkg_updates_count(app->sys);
}
int pkg_app_view(const pkg_app *app) { return app->view; }
const char *pkg_app_selected_name(const pkg_app *app)
{
    int idx = tk_listview_selected(app->list);
    if (idx < 0 || (size_t)idx >= app->n_rows) return NULL;
    return app->row_names[idx];
}

tk_root *pkg_app_root(pkg_app *app) { return app->root; }
tk_widget *pkg_app_list_widget(pkg_app *app) { return app->list; }
tk_widget *pkg_app_view_button(pkg_app *app, int view)
{
    return app->view_btns[view];
}
tk_widget *pkg_app_install_button(pkg_app *app) { return app->btn_install; }
tk_widget *pkg_app_update_all_button(pkg_app *app) { return app->btn_update_all; }
tk_widget *pkg_app_remove_button(pkg_app *app) { return app->btn_remove; }
tk_widget *pkg_app_dryrun_box(pkg_app *app) { return app->dryrun; }
tk_widget *pkg_app_updates_label(pkg_app *app) { return app->updates_label; }
tk_widget *pkg_app_detail_name(pkg_app *app) { return app->d_name; }
tk_widget *pkg_app_log_label(pkg_app *app) { return app->log; }
tk_widget *pkg_app_search_field(pkg_app *app) { return app->search; }

#ifdef BD_PKG_MAIN
int main(int argc, char **argv)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    const char *catalog = argc > 1 ? argv[1] : "packages";
    pkg_app *app = pkg_app_new(catalog, NULL, NULL, NULL, NULL,
                               800, 600);
    if (!app) return 1;
    pkg_app_free(app);
    return 0;
}
#endif
