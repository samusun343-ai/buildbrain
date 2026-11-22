#define _POSIX_C_SOURCE 200809L
#include "bd_tasks.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Layout: two forms, chosen by the theme main() installed
 * (style 2 = WinUI; the constructor reads the theme only).
 * Android form (Google Tasks): single column, list switching
 * via the top dropdown, quick-add row at the bottom. WinUI
 * form (Microsoft To Do): left list pane + right tasks/detail.
 * Completion circles and action icons are real icons from the
 * established vector sets (bdicon.h). */

struct tasks_app {
    tm_store *store;
    char today[11];
    tk_root *root;
    tk_widget *top;
    bool winui;                  /* style==2 theme: To Do 2-pane */
    tk_widget *sidebar;          /* WinUI only (NULL on Android) */
    tk_widget *view_combo;       /* Android only: list switcher */
    bool view_sync;              /* programmatic combo sync */
    tk_widget *quick_field;
    tk_widget *add_btn;
    tk_widget *task_list;
    tk_widget *status;
    /* detail editor */
    tk_widget *d_title;
    tk_widget *d_notes;
    tk_widget *d_due;
    tk_widget *d_time;
    tk_widget *d_section;
    tk_widget *d_url;
    tk_widget *d_prio;
    tk_widget *d_repeat;
    tk_widget *d_flag;
    tk_widget *list_name_field;
    /* view state */
    tm_view view;
    uint64_t view_list;
    const tm_task **rows;
    size_t n_rows;
    uint64_t selected_id;
    uint32_t *color_discs[8];  /* list colour swatches (18x18) */
    /* sidebar mapping: rows 0..4 smart views, 5.. user lists */
    uint64_t *sidebar_lists;
    size_t n_sidebar_lists;
};

static tk_widget *btn(const char *label, tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    return b;
}

/* Icons from the established vector sets via bdicon.h
 * (Material in Android mode, Fluent in WinUI mode). No
 * font-glyph or code-drawn icons. Buffers are borrowed from
 * the bdicon cache. */
static uint32_t rgb_of(tk_color c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static const uint32_t *mode_icon(tasks_app *app, const char *material,
                                 const char *fluent, tk_color tint)
{
    return bd_icon_mode(app->winui, material, fluent, 24, rgb_of(tint));
}

static const char *prio_marks(int p)
{
    switch (p) {
    case TM_PRIO_LOW: return "!";
    case TM_PRIO_MEDIUM: return "!!";
    case TM_PRIO_HIGH: return "!!!";
    default: return "";
    }
}

/* ---------------------------------------------------------------- */
/* sidebar + rows                                                    */
/* ---------------------------------------------------------------- */

/* Reminders-style list palette; the model stores only the
 * index, the colours are a presentation choice. Discs are
 * drawn once (round rect at full radius = a circle) and lent
 * to the sidebar rows as icons. */
static const tk_color LIST_PALETTE[8] = {
    { 224, 82, 77, 255 },   /* red */
    { 245, 158, 11, 255 },  /* orange */
    { 234, 197, 50, 255 },  /* yellow */
    { 76, 175, 80, 255 },   /* green */
    { 66, 133, 244, 255 },  /* blue */
    { 124, 93, 250, 255 },  /* purple */
    { 236, 120, 180, 255 }, /* pink */
    { 142, 142, 147, 255 }, /* gray */
};

static void draw_color_discs(tasks_app *app)
{
    for (int i = 0; i < 8; i++) {
        tk_canvas *cv = tk_canvas_new(18, 18);
        tk_canvas_fill_round_rect(cv, (tk_rect){ 1, 1, 16, 16 }, 8,
                                  LIST_PALETTE[i]);
        app->color_discs[i] = malloc(18 * 18 * sizeof(uint32_t));
        memcpy(app->color_discs[i], tk_canvas_pixels(cv),
               18 * 18 * sizeof(uint32_t));
        tk_canvas_free(cv);
    }
}

static void refresh_sidebar(tasks_app *app)
{
    free(app->sidebar_lists);
    app->sidebar_lists = NULL;
    app->n_sidebar_lists = tm_list_count(app->store);
    if (app->n_sidebar_lists)
        app->sidebar_lists = malloc(app->n_sidebar_lists *
                                     sizeof(uint64_t));
    size_t total = 5 + app->n_sidebar_lists;
    const char **items = malloc(total * sizeof(char *));
    char **owned = calloc(total, sizeof(char *));
    items[0] = "今日";
    items[1] = "予定";
    items[2] = "フラグ付き";
    items[3] = "すべて";
    items[4] = "完了済み";
    for (size_t i = 0; i < app->n_sidebar_lists; i++) {
        const tm_list *l = tm_list_at(app->store, i);
        app->sidebar_lists[i] = l->id;
        char buf[300];
        snprintf(buf, sizeof(buf), "%s (%zu)", l->name,
                 tm_list_open_count(app->store, l->id));
        items[5 + i] = owned[5 + i] = strdup(buf);
    }
    int sel = 3; /* All */
    switch (app->view) {
    case TM_VIEW_TODAY: sel = 0; break;
    case TM_VIEW_SCHEDULED: sel = 1; break;
    case TM_VIEW_FLAGGED: sel = 2; break;
    case TM_VIEW_ALL: sel = 3; break;
    case TM_VIEW_COMPLETED: sel = 4; break;
    case TM_VIEW_LIST:
        sel = 3;
        for (size_t i = 0; i < app->n_sidebar_lists; i++)
            if (app->sidebar_lists[i] == app->view_list) {
                sel = 5 + (int)i;
                break;
            }
        break;
    }
    if (app->sidebar) {
        tk_listview_set_items(app->sidebar, items, total);
        const uint32_t **icons = calloc(total, sizeof(uint32_t *));
        for (size_t i = 0; i < app->n_sidebar_lists; i++) {
            const tm_list *l = tm_list_at(app->store, i);
            int c = l ? l->color : 0;
            if (c < 0 || c > 7) c = 0;
            icons[5 + i] = app->color_discs[c];
        }
        tk_listview_set_item_icons(app->sidebar, icons, 18, 18,
                                   total);
        free(icons);
        tk_listview_select(app->sidebar, sel);
    }
    if (app->view_combo) {
        /* Android form: the same entries become the top
         * list-switcher dropdown (sidebar is collapsed) */
        app->view_sync = true;
        tk_combobox_set_items(app->view_combo, items, total);
        tk_combobox_select(app->view_combo, sel);
        app->view_sync = false;
    }
    for (size_t i = 0; i < total; i++) free(owned[i]);
    free(owned);
    free(items);
}

static void refresh_rows(tasks_app *app)
{
    free(app->rows);
    app->rows = NULL;
    app->n_rows = tm_query(app->store, app->view, app->view_list,
                           app->today, NULL, &app->rows);
    const char **items = malloc((app->n_rows ? app->n_rows : 1) *
                                sizeof(char *));
    char **owned = malloc((app->n_rows ? app->n_rows : 1) *
                          sizeof(char *));
    const uint32_t **icons = calloc(app->n_rows ? app->n_rows : 1,
                                    sizeof(uint32_t *));
    const tk_theme *theme = tk_theme_default();
    const uint32_t *ic_done = mode_icon(app, "check_circle",
                                        "checkmark_circle",
                                        theme->accent);
    const uint32_t *ic_open = mode_icon(app, "radio_button_unchecked",
                                        "circle", theme->text_dim);
    for (size_t i = 0; i < app->n_rows; i++) {
        const tm_task *t = app->rows[i];
        /* the completion circle is the row icon (real icon,
         * not a "[ ]" text substitute) */
        icons[i] = t->done_seq ? ic_done : ic_open;
        char buf[1024];
        char sub[64] = "";
        int done = 0, total = 0;
        tm_subtask_progress(app->store, t->id, &done, &total);
        if (total > 0) snprintf(sub, sizeof(sub), " [%d/%d]", done, total);
        snprintf(buf, sizeof(buf), "%s%s%s%s%s%s",
                 t->parent_id ? "  " : "",
                 t->title,
                 t->priority ? " " : "", prio_marks(t->priority),
                 t->flagged ? " [旗]" : "",
                 sub);
        if (t->due_date[0]) {
            char due[64];
            snprintf(due, sizeof(due), " (%s%s%s)", t->due_date,
                     t->due_time[0] ? " " : "",
                     t->due_time[0] ? t->due_time : "");
            strncat(buf, due, sizeof(buf) - strlen(buf) - 1);
        }
        if (t->section[0]) {
            char sec[300];
            snprintf(sec, sizeof(sec), " #%s", t->section);
            strncat(buf, sec, sizeof(buf) - strlen(buf) - 1);
        }
        items[i] = owned[i] = strdup(buf);
    }
    tk_listview_set_items(app->task_list, items, app->n_rows);
    tk_listview_set_item_icons(app->task_list, icons, 24, 24,
                               app->n_rows);
    free(icons);
    for (size_t i = 0; i < app->n_rows; i++) free(owned[i]);
    free(owned);
    free(items);
    char st[128];
    snprintf(st, sizeof(st), "%zu 件", app->n_rows);
    tk_label_set_text(app->status, st);
    /* setters do not auto-invalidate (SDK-verified pitfall):
     * without these, damage presentation would ship 0 bytes */
    tk_widget_invalidate(app->status);
    tk_widget_invalidate(app->task_list);
    /* restore selection */
    if (app->selected_id) {
        for (size_t i = 0; i < app->n_rows; i++)
            if (app->rows[i]->id == app->selected_id) {
                tk_listview_select(app->task_list, (int)i);
                break;
            }
    }
}

void tasks_app_refresh(tasks_app *app)
{
    refresh_sidebar(app);
    refresh_rows(app);
    if (app->sidebar) tk_widget_invalidate(app->sidebar);
    if (app->view_combo) tk_widget_invalidate(app->view_combo);
    tk_root_invalidate(app->root, app->top);
}

/* ---------------------------------------------------------------- */
/* detail editor                                                     */
/* ---------------------------------------------------------------- */

static void load_detail(tasks_app *app)
{
    const tm_task *t = app->selected_id
                           ? tm_task_find(app->store, app->selected_id)
                           : NULL;
    tk_textfield_set_text(app->d_title, t ? t->title : "");
    tk_textview_set_text(app->d_notes, t ? t->notes : "");
    tk_textfield_set_text(app->d_due, t ? t->due_date : "");
    tk_textfield_set_text(app->d_time, t ? t->due_time : "");
    tk_textfield_set_text(app->d_section, t ? t->section : "");
    tk_textfield_set_text(app->d_url, t ? t->url : "");
    if (t) {
        tk_combobox_select(app->d_prio, t->priority);
        tk_combobox_select(app->d_repeat, t->repeat);
        tk_checkbox_set_checked(app->d_flag, t->flagged);
    }
    /* all of the above are programmatic setters: invalidate the
     * detail widgets explicitly or the loaded values never paint */
    tk_widget_invalidate(app->d_title);
    tk_widget_invalidate(app->d_notes);
    tk_widget_invalidate(app->d_due);
    tk_widget_invalidate(app->d_time);
    tk_widget_invalidate(app->d_section);
    tk_widget_invalidate(app->d_url);
    tk_widget_invalidate(app->d_prio);
    tk_widget_invalidate(app->d_repeat);
    tk_widget_invalidate(app->d_flag);
}

int tasks_app_save_detail(tasks_app *app)
{
    if (!app->selected_id) return -1;
    const tm_task *cur = tm_task_find(app->store, app->selected_id);
    if (!cur) return -1;
    tm_task f = *cur; /* shallow: update dups before freeing */
    char buf[2048];
    tk_textfield_text(app->d_title, buf, sizeof(buf));
    f.title = buf;
    char nbuf[4096];
    tk_textview_text(app->d_notes, nbuf, sizeof(nbuf));
    f.notes = nbuf;
    char dbuf[16];
    tk_textfield_text(app->d_due, dbuf, sizeof(dbuf));
    snprintf(f.due_date, sizeof(f.due_date), "%s", dbuf);
    char tbuf[8];
    tk_textfield_text(app->d_time, tbuf, sizeof(tbuf));
    snprintf(f.due_time, sizeof(f.due_time), "%s", tbuf);
    char sbuf[256];
    tk_textfield_text(app->d_section, sbuf, sizeof(sbuf));
    f.section = sbuf;
    char ubuf[1024];
    tk_textfield_text(app->d_url, ubuf, sizeof(ubuf));
    f.url = ubuf;
    f.priority = tk_combobox_selected(app->d_prio);
    if (f.priority < 0) f.priority = TM_PRIO_NONE;
    f.repeat = tk_combobox_selected(app->d_repeat);
    if (f.repeat < 0) f.repeat = TM_REP_NONE;
    f.flagged = tk_checkbox_checked(app->d_flag);
    int rc = tm_task_update(app->store, &f);
    if (rc == 0) refresh_rows(app);
    return rc;
}

/* ---------------------------------------------------------------- */
/* actions                                                           */
/* ---------------------------------------------------------------- */

void tasks_app_set_view(tasks_app *app, tm_view view, uint64_t list_id)
{
    app->view = view;
    app->view_list = list_id;
    refresh_sidebar(app);
    refresh_rows(app);
}

tm_view tasks_app_view(const tasks_app *app) { return app->view; }
size_t tasks_app_row_count(tasks_app *app) { return app->n_rows; }

uint64_t tasks_app_row_task(tasks_app *app, size_t row)
{
    return row < app->n_rows ? app->rows[row]->id : 0;
}

void tasks_app_select_row(tasks_app *app, size_t row)
{
    if (row >= app->n_rows) return;
    tk_listview_select(app->task_list, (int)row);
    app->selected_id = app->rows[row]->id;
    load_detail(app);
}

uint64_t tasks_app_selected(tasks_app *app) { return app->selected_id; }

uint64_t tasks_app_quick_add(tasks_app *app, const char *title)
{
    if (!title || !*title) return 0;
    tm_task f;
    memset(&f, 0, sizeof(f));
    f.title = (char *)title;
    f.list_id = app->view == TM_VIEW_LIST
                    ? app->view_list
                    : tm_default_list(app->store);
    if (app->view == TM_VIEW_TODAY)
        snprintf(f.due_date, sizeof(f.due_date), "%s", app->today);
    if (app->view == TM_VIEW_FLAGGED) f.flagged = true;
    uint64_t id = tm_task_add(app->store, &f);
    refresh_sidebar(app);
    refresh_rows(app);
    return id;
}

int tasks_app_toggle_complete_selected(tasks_app *app)
{
    if (!app->selected_id) return -1;
    const tm_task *t = tm_task_find(app->store, app->selected_id);
    if (!t) return -1;
    int rc = t->done_seq ? tm_task_uncomplete(app->store, t->id)
                         : tm_task_complete(app->store, t->id, NULL);
    refresh_sidebar(app);
    refresh_rows(app);
    return rc;
}

int tasks_app_delete_selected(tasks_app *app)
{
    if (!app->selected_id) return -1;
    int rc = tm_task_delete(app->store, app->selected_id);
    app->selected_id = 0;
    load_detail(app);
    refresh_sidebar(app);
    refresh_rows(app);
    return rc;
}

uint64_t tasks_app_add_subtask(tasks_app *app, const char *title)
{
    if (!app->selected_id || !title || !*title) return 0;
    const tm_task *parent = tm_task_find(app->store, app->selected_id);
    if (!parent) return 0;
    tm_task f;
    memset(&f, 0, sizeof(f));
    f.title = (char *)title;
    f.list_id = parent->list_id;
    f.parent_id = parent->id;
    uint64_t id = tm_task_add(app->store, &f);
    refresh_rows(app);
    return id;
}

uint64_t tasks_app_new_list(tasks_app *app, const char *name)
{
    if (!name || !*name) return 0;
    uint64_t id = tm_list_create(app->store, name, 0);
    refresh_sidebar(app);
    return id;
}

int tasks_app_cycle_list_color(tasks_app *app)
{
    if (app->view != TM_VIEW_LIST) return -1;
    const tm_list *l = tm_list_find(app->store, app->view_list);
    if (!l) return -1;
    int rc = tm_list_set_color(app->store, l->id, (l->color + 1) % 8);
    if (rc == 0) refresh_sidebar(app);
    return rc;
}

int tasks_app_delete_current_list(tasks_app *app)
{
    if (app->view != TM_VIEW_LIST) return -1;
    int rc = tm_list_delete(app->store, app->view_list);
    if (rc == 0) tasks_app_set_view(app, TM_VIEW_ALL, 0);
    return rc;
}

/* ---------------------------------------------------------------- */
/* widget callbacks                                                  */
/* ---------------------------------------------------------------- */

static void on_sidebar_select(tk_widget *w, void *ud)
{
    tasks_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel < 0) return;
    if (sel < 5) {
        static const tm_view views[5] = {
            TM_VIEW_TODAY, TM_VIEW_SCHEDULED, TM_VIEW_FLAGGED,
            TM_VIEW_ALL, TM_VIEW_COMPLETED
        };
        tasks_app_set_view(app, views[sel], 0);
    } else if ((size_t)(sel - 5) < app->n_sidebar_lists) {
        tasks_app_set_view(app, TM_VIEW_LIST,
                           app->sidebar_lists[sel - 5]);
    }
}

/* Android list switcher: same row mapping as the sidebar
 * (0..4 smart views, 5.. user lists). */
static void on_view_combo_change(tk_widget *w, void *ud)
{
    tasks_app *app = ud;
    if (app->view_sync) return;   /* programmatic sync */
    int sel = tk_combobox_selected(w);
    if (sel < 0) return;
    if (sel < 5) {
        static const tm_view views[5] = {
            TM_VIEW_TODAY, TM_VIEW_SCHEDULED, TM_VIEW_FLAGGED,
            TM_VIEW_ALL, TM_VIEW_COMPLETED
        };
        tasks_app_set_view(app, views[sel], 0);
    } else if ((size_t)(sel - 5) < app->n_sidebar_lists) {
        tasks_app_set_view(app, TM_VIEW_LIST,
                           app->sidebar_lists[sel - 5]);
    }
}

static void on_task_select(tk_widget *w, void *ud)
{
    tasks_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) tasks_app_select_row(app, (size_t)sel);
}

static void on_task_activate(tk_widget *w, void *ud)
{
    tasks_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel >= 0) {
        tasks_app_select_row(app, (size_t)sel);
        tasks_app_toggle_complete_selected(app);
    }
}

static void on_add(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app *app = ud;
    char title[512];
    tk_textfield_text(app->quick_field, title, sizeof(title));
    if (!title[0]) return;
    tasks_app_quick_add(app, title);
    tk_textfield_set_text(app->quick_field, "");
    tk_widget_invalidate(app->quick_field);
}

static void on_toggle(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app_toggle_complete_selected(ud);
}
static void on_save_detail(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app *app = ud;
    if (tasks_app_save_detail(app) != 0) {
        tk_label_set_text(app->status, "保存に失敗（日付の形式を確認）");
        tk_widget_invalidate(app->status);
    }
}
static void on_delete(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app_delete_selected(ud);
}
static void on_subtask(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app *app = ud;
    char title[512];
    tk_textfield_text(app->quick_field, title, sizeof(title));
    if (title[0]) {
        tasks_app_add_subtask(app, title);
        tk_textfield_set_text(app->quick_field, "");
        tk_widget_invalidate(app->quick_field);
    }
}
static void on_new_list(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app *app = ud;
    char name[256];
    tk_textfield_text(app->list_name_field, name, sizeof(name));
    if (name[0]) {
        tasks_app_new_list(app, name);
        tk_textfield_set_text(app->list_name_field, "");
        tk_widget_invalidate(app->list_name_field);
    }
}
static void on_rename_list(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app *app = ud;
    if (app->view != TM_VIEW_LIST) return;
    char name[256];
    tk_textfield_text(app->list_name_field, name, sizeof(name));
    if (name[0]) {
        tm_list_rename(app->store, app->view_list, name);
        refresh_sidebar(app);
    }
}
static void on_delete_list(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app_delete_current_list(ud);
}

static void on_cycle_color(tk_widget *w, void *ud)
{
    (void)w;
    tasks_app_cycle_list_color(ud);
}

/* ---------------------------------------------------------------- */
/* construction                                                      */
/* ---------------------------------------------------------------- */

tasks_app *tasks_app_new(tm_store *store, const char *today, int w, int h)
{
    tasks_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->store = store;
    snprintf(app->today, sizeof(app->today), "%s", today);
    app->view = TM_VIEW_ALL;
    /* mode from the theme main() installed (or a test set):
     * style 2 = WinUI. The constructor never reads the reg. */
    app->winui = tk_theme_default()->style == 2;
    const tk_theme *theme = tk_theme_default();
    draw_color_discs(app);

    tk_widget *top;
    tk_widget *main_col;   /* task list + actions + detail */

    if (app->winui) {
        /* ============ WinUI: Microsoft To Do form ============
         * Left pane = smart lists + user lists; right = quick
         * add, task rows, actions, detail editor. */
        top = tk_box_new(TK_DIR_HORIZONTAL, 6, 6);

        tk_widget *left = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_set_flex(left, 1);
        app->sidebar = tk_listview_new();
        tk_listview_set_on_select(app->sidebar, on_sidebar_select,
                                  app);
        /* the sidebar must FLEX: at its desired height (one row
         * per smart view + list) it pushes the name field and
         * the list buttons off the bottom of a 480px-tall
         * window */
        tk_set_flex(app->sidebar, 1);
        tk_add_child(left, tk_label_new("リスト"));
        tk_add_child(left, app->sidebar);
        /* name field + colour cycle on one row, list actions
         * on the next: four buttons on one row overflow the
         * sidebar at 800x480 and clip the last one */
        tk_widget *nrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->list_name_field = tk_textfield_new("");
        tk_textfield_set_placeholder(app->list_name_field,
                                     "リスト名");
        tk_set_flex(app->list_name_field, 1);
        tk_add_child(nrow, app->list_name_field);
        tk_add_child(nrow, btn("色", on_cycle_color, app));
        tk_add_child(left, nrow);
        tk_widget *lrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_add_child(lrow, btn("作成", on_new_list, app));
        tk_add_child(lrow, btn("名前変更", on_rename_list, app));
        tk_widget *ldel = btn("削除", on_delete_list, app);
        const uint32_t *dic = mode_icon(app, "delete", "delete",
                                        theme->text);
        if (dic) {
            tk_button_set_icon(ldel, dic, 24, 24);
            tk_button_set_icon_layout(ldel, TK_BTN_ICON_ROW);
        }
        tk_add_child(lrow, ldel);
        tk_add_child(left, lrow);
        tk_add_child(top, left);

        main_col = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_set_flex(main_col, 2);
        tk_add_child(top, main_col);
    } else {
        /* ============ Android: Google Tasks form ============
         * Single column; the sidebar is collapsed into a top
         * list-switcher dropdown, the quick-add row sits at
         * the BOTTOM (below the detail editor). */
        top = tk_box_new(TK_DIR_VERTICAL, 4, 6);

        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_widget_set_min_size(hdr, 0, 56);
        tk_widget *title = tk_label_new("タスク");
        tk_widget_set_text_style(title, TK_TEXT_TITLE);
        tk_add_child(hdr, title);
        app->view_combo = tk_combobox_new();
        tk_combobox_set_on_change(app->view_combo,
                                  on_view_combo_change, app);
        tk_set_flex(app->view_combo, 1);
        tk_add_child(hdr, app->view_combo);
        tk_add_child(top, hdr);

        main_col = tk_box_new(TK_DIR_VERTICAL, 4, 0);
        tk_set_flex(main_col, 1);
        tk_add_child(top, main_col);
    }

    /* quick-add row: top of the main column in WinUI mode,
     * bottom of the screen in Android mode (added last there) */
    tk_widget *qrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->quick_field = tk_textfield_new("");
    tk_textfield_set_placeholder(app->quick_field, "新しいタスク");
    tk_set_flex(app->quick_field, 1);
    app->add_btn = btn("追加", on_add, app);
    {
        const uint32_t *aic = mode_icon(app, "add", "add",
                                        theme->text);
        if (aic) {
            tk_button_set_icon(app->add_btn, aic, 24, 24);
            tk_button_set_icon_layout(app->add_btn,
                                      TK_BTN_ICON_ROW);
        }
    }
    tk_add_child(qrow, app->quick_field);
    tk_add_child(qrow, app->add_btn);
    tk_add_child(qrow, btn("サブタスク", on_subtask, app));
    if (app->winui) tk_add_child(main_col, qrow);

    app->task_list = tk_listview_new();
    tk_listview_set_on_select(app->task_list, on_task_select, app);
    tk_listview_set_on_activate(app->task_list, on_task_activate,
                                app);
    tk_set_flex(app->task_list, 1);
    tk_add_child(main_col, app->task_list);

    tk_widget *arow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_widget *tog = btn("完了/未完了", on_toggle, app);
    {
        const uint32_t *cic = mode_icon(app, "check_circle",
                                        "checkmark_circle",
                                        theme->text);
        if (cic) {
            tk_button_set_icon(tog, cic, 24, 24);
            tk_button_set_icon_layout(tog, TK_BTN_ICON_ROW);
        }
    }
    tk_add_child(arow, tog);
    tk_widget *del = btn("削除", on_delete, app);
    {
        const uint32_t *dic = mode_icon(app, "delete", "delete",
                                        theme->text);
        if (dic) {
            tk_button_set_icon(del, dic, 24, 24);
            tk_button_set_icon_layout(del, TK_BTN_ICON_ROW);
        }
    }
    tk_add_child(arow, del);
    tk_add_child(main_col, arow);

    /* detail editor */
    tk_widget *dgrid = tk_box_new(TK_DIR_VERTICAL, 2, 0);
    app->d_title = tk_textfield_new("");
    tk_textfield_set_placeholder(app->d_title, "タイトル");
    tk_add_child(dgrid, app->d_title);
    app->d_notes = tk_textview_new("");
    tk_add_child(dgrid, app->d_notes);
    tk_widget *drow1 = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    app->d_due = tk_textfield_new("");
    tk_textfield_set_placeholder(app->d_due, "期限 YYYY-MM-DD");
    app->d_time = tk_textfield_new("");
    tk_textfield_set_placeholder(app->d_time, "HH:MM");
    app->d_section = tk_textfield_new("");
    tk_textfield_set_placeholder(app->d_section, "セクション");
    tk_add_child(drow1, app->d_due);
    tk_add_child(drow1, app->d_time);
    tk_add_child(drow1, app->d_section);
    tk_add_child(dgrid, drow1);
    tk_widget *drow2 = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    const char *prios[] = { "優先なし", "低", "中", "高" };
    app->d_prio = tk_combobox_new();
    tk_combobox_set_items(app->d_prio, prios, 4);
    const char *reps[] = { "繰り返しなし", "毎時", "毎日", "毎週",
                           "隔週", "毎月", "毎年" };
    app->d_repeat = tk_combobox_new();
    tk_combobox_set_items(app->d_repeat, reps, 7);
    app->d_flag = tk_checkbox_new("フラグ", false);
    app->d_url = tk_textfield_new("");
    tk_textfield_set_placeholder(app->d_url, "URL");
    tk_add_child(drow2, app->d_prio);
    tk_add_child(drow2, app->d_repeat);
    tk_add_child(drow2, app->d_flag);
    tk_add_child(drow2, app->d_url);
    tk_add_child(dgrid, drow2);
    tk_add_child(dgrid, btn("詳細を保存", on_save_detail, app));
    tk_add_child(main_col, dgrid);

    if (!app->winui) {
        /* Android: list management under the detail editor,
         * then the quick-add row at the very bottom */
        tk_widget *nrow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->list_name_field = tk_textfield_new("");
        tk_textfield_set_placeholder(app->list_name_field,
                                     "リスト名");
        tk_set_flex(app->list_name_field, 1);
        tk_add_child(nrow, app->list_name_field);
        tk_add_child(nrow, btn("色", on_cycle_color, app));
        tk_add_child(nrow, btn("作成", on_new_list, app));
        tk_add_child(nrow, btn("名前変更", on_rename_list, app));
        tk_widget *ldel = btn("削除", on_delete_list, app);
        const uint32_t *dic = mode_icon(app, "delete", "delete",
                                        theme->text);
        if (dic) {
            tk_button_set_icon(ldel, dic, 24, 24);
            tk_button_set_icon_layout(ldel, TK_BTN_ICON_ROW);
        }
        tk_add_child(nrow, ldel);
        tk_add_child(main_col, nrow);
        tk_add_child(main_col, qrow);
    }

    app->status = tk_label_new("");
    tk_add_child(main_col, app->status);

    app->top = top;
    app->root = tk_root_new(top);
    tk_root_layout(app->root, w, h);
    tasks_app_refresh(app);
    return app;
}

void tasks_app_free(tasks_app *app)
{
    if (!app) return;
    tk_root_free(app->root);
    tk_widget_free(app->top);
    free(app->rows);
    free(app->sidebar_lists);
    free(app);
}

bool tasks_app_winui(const tasks_app *app) { return app->winui; }

tk_root *tasks_app_root(tasks_app *app) { return app->root; }
tk_widget *tasks_app_quick_field(tasks_app *app) { return app->quick_field; }
tk_widget *tasks_app_add_button(tasks_app *app) { return app->add_btn; }

/* ---------------------------------------------------------------- */
#ifdef BD_TASKS_MAIN
int main(void)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    const char *home = getenv("HOME");
    char root[1024];
    const char *env_root = getenv("BRAINUX_TASKS_DIR");
    if (env_root) snprintf(root, sizeof(root), "%s", env_root);
    else if (home) snprintf(root, sizeof(root), "%s/.local/share/brainux/tasks", home);
    else snprintf(root, sizeof(root), ".local/share/brainux/tasks");
    tm_store *store = tm_open(root);
    if (!store) { fprintf(stderr, "bd-tasks: cannot open store\n"); return 1; }

    char today[11];
    {
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(today, sizeof(today), "%Y-%m-%d", &tmv);
    }

    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    /* Display first, real size (see bd_calc.c main note) */
    int vw, vh;
    bd_app_display *disp = bd_app_display_open_ex("リマインダー", 0, 0, 480, 272, 480, 272, &vw, &vh);
    tasks_app *app = tasks_app_new(store, today, vw, vh);

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, NULL, NULL, 20);
    } else {
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-tasks: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    tasks_app_free(app);
    tm_close(store);
    if (font) tk_font_free(font);
    return 0;
}
#endif /* BD_TASKS_MAIN */
