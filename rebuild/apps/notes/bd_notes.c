#define _POSIX_C_SOURCE 200809L
#include "bd_notes.h"
#include "bdappmode.h"
#include "bdapp_run.h"
#include "bdicon.h"
#include "bdstore.h"
#include "brainime.h"
#include "osk.h"
#include "osk_brainime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Layout: two forms, chosen by the theme main() installed
 * (style 2 = WinUI; the constructor reads the theme only).
 *
 * Android form (DESIGN-apps-envfit.md SS7.2, Samsung Notes
 * fitted to this panel): three screens drilled between, never
 * panes shown side by side - a note LIST (big title + count,
 * icon-only header actions, note cards with previews, one
 * filled primary button at the bottom), a FOLDER screen (all
 * notes / folders / recently deleted as full-width rows), and
 * an EDITOR screen (back + folder name + search + overflow
 * menu; format bar below the editor; pin / attach / delete /
 * restore live in the menu).
 *
 * WinUI form (OneNote / Notepad): two panes side by side -
 * left = folder dropdown + search box + note list, right =
 * editor with the format bar at its top. Selecting a note
 * loads it in place.
 *
 * All icons are real icons from the established vector sets
 * (bdicon.h); no font glyphs or code-drawn shapes. The OSK,
 * when present, belongs to the editor. */

/* ---------------------------------------------------------------- */
/* app state                                                         */
/* ---------------------------------------------------------------- */

enum { SCR_LIST, SCR_FOLDERS, SCR_EDITOR };

struct notes_app {
    nm_store *store;
    tk_root *root;
    tk_widget *top;
    int win_w, win_h;
    int screen;
    bool winui;                  /* style==2 theme: 2-pane layout */
    tk_widget *scr_list, *scr_folders, *scr_editor;
    tk_widget *scr_panes;        /* WinUI: list pane + editor side
                                  * by side (replaces the screens) */
    tk_widget *folder_combo;     /* WinUI: folder switch dropdown
                                  * in the left pane's header */
    tk_widget *list_menu;        /* list-screen overflow menu */
    /* list screen */
    tk_widget *note_list;
    tk_widget *empty_label;      /* empty-state guidance
                                  * (eval F-14) */
    tk_widget *list_count;        /* header count caption */
    tk_widget *list_search_row;
    tk_widget *list_search_field;
    /* folder screen */
    tk_widget *folder_list;
    bool folder_sync;            /* programmatic select in flight */
    /* editor screen */
    tk_widget *editor;
    tk_widget *editor_folder;    /* header folder-name label */
    tk_widget *search_field;
    tk_widget *replace_field;
    tk_widget *search_bar;       /* toggled from the header */
    tk_widget *replace_bar;      /* expanded from the search bar */
    tk_widget *format_bar;       /* below the editor (SS7.2) */
    tk_widget *status;
    tk_widget *osk;
    tk_widget *editor_menu;      /* rebuilt fresh on every open */
    /* attach dialog */
    tk_widget *attach_dlg;
    tk_widget *attach_path;
    /* folder filter: -2 = all, -1 = recently deleted, >=0 = index
     * into folders[] */
    char **folders;
    size_t n_folders;
    int folder_sel;
    nm_note *notes;
    size_t n_notes;
    uint64_t current_id;
    bool dirty;
    bool loading;            /* suppress dirty during programmatic set */
};

static void refresh_lists(notes_app *app);
static void show_screen(notes_app *app, int scr);

/* ---------------------------------------------------------------- */
/* small widget helpers                                              */
/* ---------------------------------------------------------------- */

static tk_widget *btn(const char *label, tk_notify_cb cb, void *ud)
{
    tk_widget *b = tk_button_new(label);
    tk_button_set_on_click(b, cb, ud);
    return b;
}

static void field_text(tk_widget *field, char *out, size_t cap)
{
    tk_textfield_text(field, out, cap);
}

/* Icons come from the established vector sets via bdicon.h
 * (Material in Android mode, Fluent in WinUI mode), rasterised
 * offline with anti-aliasing. No font-glyph rasterisation and
 * no code-drawn shapes remain (user direction 2026-11-21).
 * Returned buffers are borrowed from the bdicon cache. */
static uint32_t rgb_of(tk_color c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static const uint32_t *mode_icon(notes_app *app, const char *material,
                                 const char *fluent, tk_color tint)
{
    return bd_icon_mode(app->winui, material, fluent, 24, rgb_of(tint));
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

static tk_widget *header_back_btn(notes_app *app, tk_notify_cb cb,
                                  void *ud)
{
    const tk_theme *t = tk_theme_default();
    tk_widget *b = header_icon_btn(
        "戻る", mode_icon(app, "arrow_back", "arrow_left", t->text),
        cb, ud);
    return b;
}

/* ---------------------------------------------------------------- */
/* screens                                                           */
/* ---------------------------------------------------------------- */

static void show_screen(notes_app *app, int scr)
{
    app->screen = scr;
    if (app->winui) {
        /* 2-pane form: list and editor are always both visible;
         * screen switches are selection changes only. */
        tk_widget_invalidate(app->top);
        if (app->root) tk_root_layout(app->root, app->win_w, app->win_h);
        return;
    }
    tk_set_visible(app->scr_list, scr == SCR_LIST);
    tk_set_visible(app->scr_folders, scr == SCR_FOLDERS);
    tk_set_visible(app->scr_editor, scr == SCR_EDITOR);
    tk_widget_invalidate(app->top);
    if (app->root) tk_root_layout(app->root, app->win_w, app->win_h);
}

/* ---------------------------------------------------------------- */
/* editor text helpers (public textcore API only)                    */
/* ---------------------------------------------------------------- */

static char *editor_text_dup(notes_app *app)
{
    tc_buffer *b = tk_textview_buffer(app->editor);
    size_t len = tc_length(b);
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    tc_text(b, buf, len + 1);
    return buf;
}

bool notes_editor_find_next(notes_app *app, const char *query)
{
    if (!query || !*query) return false;
    tc_buffer *b = tk_textview_buffer(app->editor);
    char *text = editor_text_dup(app);
    if (!text) return false;
    size_t qlen = strlen(query);
    size_t from = tc_cursor(b);
    if (tc_has_selection(b)) {
        size_t lo, hi;
        tc_selection_range(b, &lo, &hi);
        from = hi;
    }
    const char *hit = NULL;
    if (tc_length(b) >= qlen) {
        hit = strstr(text + from, query);
        if (!hit) hit = strstr(text, query); /* wrap */
    }
    if (!hit) { free(text); return false; }
    size_t off = (size_t)(hit - text);
    tc_move_to(b, off, false);
    tc_move_to(b, off + qlen, true);
    free(text);
    tk_textview_ensure_cursor_visible(app->editor);
    return true;
}

int notes_editor_replace_all(notes_app *app, const char *query,
                             const char *replacement)
{
    if (!query || !*query) return 0;
    tc_buffer *b = tk_textview_buffer(app->editor);
    int count = 0;
    size_t qlen = strlen(query);
    for (;;) {
        char *text = editor_text_dup(app);
        if (!text) break;
        size_t cur = count == 0 ? 0 : tc_cursor(b);
        const char *hit = tc_length(b) >= qlen ? strstr(text + cur, query)
                                               : NULL;
        if (!hit) { free(text); break; }
        size_t off = (size_t)(hit - text);
        free(text);
        tc_delete_range(b, off, off + qlen);
        tc_move_to(b, off, false);
        tc_insert(b, replacement);
        count++;
        if (count > 10000) break;
    }
    return count;
}

static void current_line_range(notes_app *app, size_t *line,
                                 size_t *start, size_t *len)
{
    tc_buffer *b = tk_textview_buffer(app->editor);
    size_t col = 0;
    tc_offset_to_linecol(b, tc_cursor(b), line, &col);
    tc_line_range(b, *line, start, len);
}

/* Toggle a line prefix ("# ", "- ", "1. ", "[ ] "). */
static void toggle_line_prefix(notes_app *app, const char *prefix)
{
    tc_buffer *b = tk_textview_buffer(app->editor);
    size_t line, start, len;
    current_line_range(app, &line, &start, &len);
    size_t plen = strlen(prefix);
    char *text = editor_text_dup(app);
    if (!text) return;
    if (len >= plen && strncmp(text + start, prefix, plen) == 0) {
        tc_delete_range(b, start, start + plen);
    } else {
        size_t cur = tc_cursor(b);
        tc_move_to(b, start, false);
        tc_insert(b, prefix);
        tc_move_to(b, cur + plen, false);
    }
    free(text);
}

void notes_editor_toggle_heading(notes_app *app)
{
    toggle_line_prefix(app, "# ");
}

void notes_editor_toggle_list(notes_app *app, bool numbered)
{
    toggle_line_prefix(app, numbered ? "1. " : "- ");
}

void notes_editor_wrap_selection(notes_app *app, const char *left,
                                 const char *right)
{
    tc_buffer *b = tk_textview_buffer(app->editor);
    size_t lo, hi;
    if (!tc_has_selection(b)) {
        tc_insert(b, left);
        size_t mid = tc_cursor(b);
        tc_insert(b, right);
        tc_move_to(b, mid, false);
        return;
    }
    tc_selection_range(b, &lo, &hi);
    tc_move_to(b, hi, false);
    tc_insert(b, right);
    tc_move_to(b, lo, false);
    tc_insert(b, left);
    /* re-select the wrapped content */
    tc_move_to(b, lo + strlen(left), false);
    tc_move_to(b, hi + strlen(left), true);
}

void notes_editor_toggle_checklist(notes_app *app)
{
    tc_buffer *b = tk_textview_buffer(app->editor);
    size_t line, start, len;
    current_line_range(app, &line, &start, &len);
    char *text = editor_text_dup(app);
    if (!text) return;
    if (len >= 4 && (strncmp(text + start, "[ ] ", 4) == 0 ||
                     strncmp(text + start, "[x] ", 4) == 0 ||
                     strncmp(text + start, "[X] ", 4) == 0)) {
        bool now = false;
        char *toggled = nm_checklist_toggle(text, line, &now);
        if (toggled) {
            /* replace just this line's checkbox char via the core */
            size_t pos = start + 1;
            tc_delete_range(b, pos, pos + 1);
            tc_move_to(b, pos, false);
            tc_insert(b, now ? "x" : " ");
            free(toggled);
        }
    } else {
        size_t cur = tc_cursor(b);
        tc_move_to(b, start, false);
        tc_insert(b, "[ ] ");
        tc_move_to(b, cur + 4, false);
    }
    free(text);
}

/* ---------------------------------------------------------------- */
/* list management                                                   */
/* ---------------------------------------------------------------- */

static const char *folder_filter_name(notes_app *app)
{
    if (app->folder_sel == -2) return NULL;         /* all */
    if (app->folder_sel == -1) return NULL;         /* deleted */
    if (app->folder_sel >= 0 &&
        (size_t)app->folder_sel < app->n_folders)
        return app->folders[app->folder_sel];
    return NULL;
}

static void refresh_folder_list(notes_app *app)
{
    nm_strv_free(app->folders, app->n_folders);
    app->folders = NULL;
    app->n_folders = nm_folders(app->store, &app->folders);
    size_t total = app->n_folders + 2;
    const char **items = malloc(total * sizeof(char *));
    items[0] = "すべてのノート";
    for (size_t i = 0; i < app->n_folders; i++)
        items[1 + i] = app->folders[i];
    items[total - 1] = "最近削除";
    int sel = app->folder_sel == -2 ? 0 :
              app->folder_sel == -1 ? (int)total - 1 :
              app->folder_sel + 1;
    app->folder_sync = true;   /* not a user tap: no navigation */
    if (app->folder_list)
        tk_listview_set_items(app->folder_list, items, total);
    if (app->folder_list) tk_listview_select(app->folder_list, sel);
    if (app->folder_combo) {
        tk_combobox_set_items(app->folder_combo, items, total);
        tk_combobox_select(app->folder_combo, sel);
    }
    app->folder_sync = false;
    free(items);
}

static void refresh_note_list(notes_app *app)
{
    nm_list_free(app->notes, app->n_notes);
    app->notes = NULL;
    app->n_notes = 0;
    bool deleted = app->folder_sel == -1;
    nm_list(app->store, folder_filter_name(app), deleted,
            NM_SORT_MODIFIED, &app->notes, &app->n_notes);
    const char **items = malloc((app->n_notes ? app->n_notes : 1) *
                                sizeof(char *));
    const char **subs = malloc((app->n_notes ? app->n_notes : 1) *
                               sizeof(char *));
    const uint32_t **icons = calloc(app->n_notes ? app->n_notes : 1,
                                    sizeof(uint32_t *));
    const tk_theme *theme = tk_theme_default();
    const uint32_t *pin = mode_icon(app, "push_pin", "pin",
                                    theme->accent);
    for (size_t i = 0; i < app->n_notes; i++) {
        items[i] = app->notes[i].title;
        subs[i] = app->notes[i].preview;
        /* pinned notes carry the real pin icon on their card */
        icons[i] = app->notes[i].pinned ? pin : NULL;
    }
    if (app->empty_label)
        tk_set_visible(app->empty_label, app->n_notes == 0);
    tk_listview_set_items(app->note_list, items, app->n_notes);
    tk_listview_set_subitems(app->note_list, subs, app->n_notes);
    tk_listview_set_item_icons(app->note_list, icons, 24, 24,
                               app->n_notes);
    free(icons);
    free(items);
    free(subs);
    /* keep selection on the current note when possible */
    int sel = -1;
    for (size_t i = 0; i < app->n_notes; i++)
        if (app->notes[i].id == app->current_id) { sel = (int)i; break; }
    if (sel >= 0) tk_listview_select(app->note_list, sel);
    char st[128];
    snprintf(st, sizeof(st), "%zu 件%s", app->n_notes,
             deleted ? "（最近削除）" : "");
    tk_label_set_text(app->list_count, st);
    /* setters do not auto-invalidate (SDK-verified pitfall) */
    tk_widget_invalidate(app->list_count);
    tk_widget_invalidate(app->note_list);
}

static void refresh_lists(notes_app *app)
{
    refresh_folder_list(app);
    refresh_note_list(app);
    if (app->folder_list) tk_widget_invalidate(app->folder_list);
    if (app->folder_combo) tk_widget_invalidate(app->folder_combo);
    /* list replacement + label updates above are setter-driven;
     * make sure the whole UI is marked damaged for presentation */
    tk_root_invalidate(app->root, app->top);
}

/* ---------------------------------------------------------------- */
/* note loading / autosave                                           */
/* ---------------------------------------------------------------- */

void notes_app_autosave(notes_app *app)
{
    if (!app->dirty || app->current_id == 0) return;
    char *text = editor_text_dup(app);
    if (!text) return;
    if (nm_save_body(app->store, app->current_id, text) == 0)
        app->dirty = false;
    free(text);
}

void notes_app_select(notes_app *app, uint64_t id)
{
    notes_app_autosave(app);
    char *body = nm_load_body(app->store, id);
    app->loading = true;
    tk_textview_set_text(app->editor, body ? body : "");
    app->loading = false;
    tk_widget_invalidate(app->editor); /* setter: no auto-invalidate */
    free(body);
    app->current_id = id;
    app->dirty = false;
    /* the editor header names the note's folder */
    nm_note m;
    if (nm_get(app->store, id, &m)) {
        tk_label_set_text(app->editor_folder, m.folder);
        tk_widget_invalidate(app->editor_folder);
        free(m.folder); free(m.title); free(m.preview);
    }
    refresh_note_list(app);
    /* selecting a note IS opening it (SS5.3): the editor
     * screen replaces the list */
    show_screen(app, SCR_EDITOR);
}

uint64_t notes_app_current(const notes_app *app) { return app->current_id; }
bool notes_app_dirty(const notes_app *app) { return app->dirty; }
void notes_app_refresh(notes_app *app) { refresh_lists(app); }

/* ---------------------------------------------------------------- */
/* callbacks                                                         */
/* ---------------------------------------------------------------- */

static void on_editor_change(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    if (!app->loading && app->current_id != 0) app->dirty = true;
}

static void on_folder_select(tk_widget *w, void *ud)
{
    notes_app *app = ud;
    if (app->folder_sync) return;   /* programmatic sync, not a tap */
    int sel = tk_listview_selected(w);
    if (sel < 0) return;
    notes_app_autosave(app);
    app->current_id = 0;
    app->loading = true;
    tk_textview_set_text(app->editor, "");
    app->loading = false;
    tk_widget_invalidate(app->editor);
    if (sel == 0) app->folder_sel = -2;
    else if (sel == (int)app->n_folders + 1) app->folder_sel = -1;
    else app->folder_sel = sel - 1;
    refresh_note_list(app);
    show_screen(app, SCR_LIST);
}

/* WinUI folder switch: the left pane's dropdown carries the
 * same mapping as the folder screen's rows (0 = all, 1..n =
 * folders, last = recently deleted). */
static void on_folder_combo_change(tk_widget *w, void *ud)
{
    notes_app *app = ud;
    if (app->folder_sync) return;   /* programmatic sync */
    int sel = tk_combobox_selected(w);
    if (sel < 0) return;
    notes_app_autosave(app);
    app->current_id = 0;
    app->loading = true;
    tk_textview_set_text(app->editor, "");
    app->loading = false;
    tk_widget_invalidate(app->editor);
    if (sel == 0) app->folder_sel = -2;
    else if (sel == (int)app->n_folders + 1) app->folder_sel = -1;
    else app->folder_sel = sel - 1;
    refresh_note_list(app);
}

static void on_note_select(tk_widget *w, void *ud)
{
    notes_app *app = ud;
    int sel = tk_listview_selected(w);
    if (sel < 0 || (size_t)sel >= app->n_notes) return;
    if (app->notes[sel].id == app->current_id) return;
    notes_app_select(app, app->notes[sel].id);
}

static void on_new_note(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    const char *folder = folder_filter_name(app);
    uint64_t id = nm_create(app->store,
                            folder ? folder : NM_DEFAULT_FOLDER, "");
    if (id == 0) return;
    if (app->folder_sel == -1) app->folder_sel = -2; /* left trash view */
    refresh_lists(app);
    notes_app_select(app, id);
    tk_focus(app->root, app->editor);
}

static void do_pin(notes_app *app)
{
    if (app->current_id == 0) return;
    nm_note m;
    if (!nm_get(app->store, app->current_id, &m)) return;
    bool pinned = m.pinned;
    free(m.folder); free(m.title); free(m.preview);
    nm_set_pinned(app->store, app->current_id, !pinned);
    refresh_note_list(app);
}

static void do_delete(notes_app *app)
{
    if (app->current_id == 0) return;
    uint64_t id = app->current_id;
    app->dirty = false; /* deleting discards the pending edit state */
    if (app->folder_sel == -1) nm_delete_permanent(app->store, id);
    else nm_delete(app->store, id, NULL);
    app->current_id = 0;
    app->loading = true;
    tk_textview_set_text(app->editor, "");
    app->loading = false;
    tk_widget_invalidate(app->editor);
    refresh_note_list(app);
    show_screen(app, SCR_LIST);
}

static void do_restore(notes_app *app)
{
    if (app->current_id == 0 || app->folder_sel != -1) return;
    nm_restore(app->store, app->current_id);
    app->current_id = 0;
    refresh_note_list(app);
    show_screen(app, SCR_LIST);
}

static void on_editor_back(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    notes_app_autosave(app);
    refresh_note_list(app);   /* titles/previews reflect the edit */
    show_screen(app, SCR_LIST);
}


/* Android-mode Back key (TK_KEY_BACK via the WM): editor ->
 * list (with autosave, like the on-screen back button),
 * folder screen -> list. The WinUI 2-pane form has no drill
 * stack, so the key is left unconsumed there. */
static bool notes_back_key(void *ud, tk_event *ev)
{
    notes_app *app = ud;
    if (!ev || ev->type != TK_EV_KEY_DOWN || ev->key != TK_KEY_BACK)
        return false;
    if (app->winui) return false;
    if (app->screen == SCR_EDITOR) {
        on_editor_back(NULL, app);
        return true;
    }
    if (app->screen == SCR_FOLDERS) {
        show_screen(app, SCR_LIST);
        return true;
    }
    return false;
}

/* list-screen overflow (more_vert): new note / folders */
static void on_folders_open(tk_widget *w, void *ud);
enum { NML_NEW = 1, NML_FOLDERS };

static void on_list_menu_select(tk_widget *menu, void *ud)
{
    notes_app *app = ud;
    switch (tk_menu_last_id(menu)) {
    case NML_NEW: on_new_note(NULL, app); break;
    case NML_FOLDERS: on_folders_open(NULL, app); break;
    default: break;
    }
}

static void on_list_more(tk_widget *w, void *ud)
{
    notes_app *app = ud;
    if (app->list_menu) tk_widget_free(app->list_menu);
    tk_widget *m = tk_menu_new();
    app->list_menu = m;
    tk_menu_set_on_select(m, on_list_menu_select, app);
    tk_menu_add_item(m, "新規ノート", NML_NEW);
    tk_menu_add_item(m, "フォルダ", NML_FOLDERS);
    {
        const tk_theme *t = tk_theme_default();
        const uint32_t *add = mode_icon(app, "add", "add", t->text);
        const uint32_t *fld = mode_icon(app, "folder", "folder",
                                        t->text);
        if (add) tk_menu_set_item_icon(m, NML_NEW, add, 24, 24);
        if (fld) tk_menu_set_item_icon(m, NML_FOLDERS, fld, 24, 24);
    }
    tk_rect r = tk_widget_rect(w);
    tk_menu_popup(app->root, m, r.x + r.w, r.y + r.h);
}

static void on_folders_open(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    refresh_folder_list(app);
    show_screen(app, SCR_FOLDERS);
}

static void on_folders_back(tk_widget *w, void *ud)
{
    (void)w;
    show_screen(ud, SCR_LIST);
}

static void on_list_search_toggle(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    bool vis = tk_is_visible(app->list_search_row);
    tk_set_visible(app->list_search_row, !vis);
    if (!vis) tk_focus(app->root, app->list_search_field);
    tk_widget_invalidate(app->top);
    tk_root_layout(app->root, app->win_w, app->win_h);
}

static void on_search_toggle(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    bool vis = tk_is_visible(app->search_bar);
    tk_set_visible(app->search_bar, !vis);
    if (vis) tk_set_visible(app->replace_bar, false);
    else tk_focus(app->root, app->search_field);
    tk_widget_invalidate(app->top);
    tk_root_layout(app->root, app->win_w, app->win_h);
}

static void on_replace_toggle(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    tk_set_visible(app->replace_bar, !tk_is_visible(app->replace_bar));
    tk_widget_invalidate(app->top);
    tk_root_layout(app->root, app->win_w, app->win_h);
}

/* Search order (kept from the pane UI): the open note first,
 * then the first note in the store that contains the query. */
static void do_search(notes_app *app, const char *q)
{
    if (!q[0]) return;
    if (app->current_id != 0 && notes_editor_find_next(app, q)) {
        if (app->screen != SCR_EDITOR) show_screen(app, SCR_EDITOR);
        return;
    }
    nm_hit *hits = NULL;
    size_t hn = 0;
    if (nm_search(app->store, q, &hits, &hn) == 0 && hn > 0) {
        app->folder_sel = -2;
        refresh_lists(app);
        notes_app_select(app, hits[0].id);
        notes_editor_find_next(app, q);
    }
    nm_search_free(hits, hn);
}

static void on_search(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    char q[256];
    field_text(app->search_field, q, sizeof(q));
    do_search(app, q);
}

static void on_list_search(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    char q[256];
    field_text(app->list_search_field, q, sizeof(q));
    do_search(app, q);
}

static void on_replace(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    char q[256], r[256];
    field_text(app->search_field, q, sizeof(q));
    field_text(app->replace_field, r, sizeof(r));
    if (!q[0] || app->current_id == 0) return;
    tc_buffer *b = tk_textview_buffer(app->editor);
    if (tc_has_selection(b)) {
        size_t lo, hi;
        tc_selection_range(b, &lo, &hi);
        char *sel = malloc(hi - lo + 1);
        if (sel) {
            tc_range_text(b, lo, hi, sel, hi - lo + 1);
            if (strcmp(sel, q) == 0) {
                tc_delete_range(b, lo, hi);
                tc_move_to(b, lo, false);
                tc_insert(b, r);
            }
            free(sel);
        }
    }
    notes_editor_find_next(app, q);
}

static void on_replace_all(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    char q[256], r[256];
    field_text(app->search_field, q, sizeof(q));
    field_text(app->replace_field, r, sizeof(r));
    if (!q[0] || app->current_id == 0) return;
    int n = notes_editor_replace_all(app, q, r);
    char st[128];
    snprintf(st, sizeof(st), "%d 件置換しました", n);
    tk_label_set_text(app->status, st);
    tk_widget_invalidate(app->status);
}

static void on_heading(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_toggle_heading(ud);
}
static void on_bold(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_wrap_selection(ud, "**", "**");
}
static void on_italic(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_wrap_selection(ud, "*", "*");
}
static void on_underline(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_wrap_selection(ud, "<u>", "</u>");
}
static void on_strike(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_wrap_selection(ud, "~~", "~~");
}
static void on_bullet(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_toggle_list(ud, false);
}
static void on_numbered(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_toggle_list(ud, true);
}
static void on_checklist(tk_widget *w, void *ud)
{
    (void)w;
    notes_editor_toggle_checklist(ud);
}

static void on_attach_ok(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    char path[1024];
    field_text(app->attach_path, path, sizeof(path));
    if (path[0] && app->current_id != 0) {
        char name[256];
        if (nm_attach_add(app->store, app->current_id, path,
                          name, sizeof(name)) == 0) {
            char st[320];
            snprintf(st, sizeof(st), "添付しました: %s", name);
            tk_label_set_text(app->status, st);
            tk_widget_invalidate(app->status);
        }
    }
    tk_dialog_close(app->root, app->attach_dlg);
}

static void on_attach_cancel(tk_widget *w, void *ud)
{
    (void)w;
    notes_app *app = ud;
    tk_dialog_close(app->root, app->attach_dlg);
}

static void do_attach(notes_app *app)
{
    if (app->current_id == 0) return;
    tk_textfield_set_text(app->attach_path, "");
    tk_dialog_show(app->root, app->attach_dlg);
}

/* editor overflow menu: pin / attach / delete / restore.
 * Rebuilt fresh on every open (check state = current pin). */
enum { NMN_PIN = 1, NMN_ATTACH, NMN_DELETE, NMN_RESTORE };

static void on_editor_menu_select(tk_widget *menu, void *ud)
{
    notes_app *app = ud;
    switch (tk_menu_last_id(menu)) {
    case NMN_PIN: do_pin(app); break;
    case NMN_ATTACH: do_attach(app); break;
    case NMN_DELETE: do_delete(app); break;
    case NMN_RESTORE: do_restore(app); break;
    default: break;
    }
}

static void on_editor_more(tk_widget *w, void *ud)
{
    notes_app *app = ud;
    if (app->editor_menu) tk_widget_free(app->editor_menu);
    tk_widget *m = tk_menu_new();
    app->editor_menu = m;
    tk_menu_set_on_select(m, on_editor_menu_select, app);
    bool pinned = false;
    nm_note meta;
    if (app->current_id != 0 && nm_get(app->store, app->current_id,
                                       &meta)) {
        pinned = meta.pinned;
        free(meta.folder); free(meta.title); free(meta.preview);
    }
    tk_menu_add_check_item(m, "ピン留め", NMN_PIN, pinned);
    tk_menu_add_item(m, "添付...", NMN_ATTACH);
    {
        const tk_theme *t = tk_theme_default();
        const uint32_t *pin = mode_icon(app, "push_pin", "pin", t->text);
        const uint32_t *att = mode_icon(app, "attach_file", "attach",
                                        t->text);
        if (pin) tk_menu_set_item_icon(m, NMN_PIN, pin, 24, 24);
        if (att) tk_menu_set_item_icon(m, NMN_ATTACH, att, 24, 24);
    }
    tk_menu_add_separator(m);
    tk_menu_add_item(m, "削除", NMN_DELETE);
    tk_menu_add_item(m, "復元", NMN_RESTORE);
    tk_menu_set_item_enabled(m, NMN_PIN, app->current_id != 0);
    tk_menu_set_item_enabled(m, NMN_ATTACH, app->current_id != 0);
    tk_menu_set_item_enabled(m, NMN_DELETE, app->current_id != 0);
    tk_menu_set_item_enabled(m, NMN_RESTORE,
                             app->current_id != 0 &&
                             app->folder_sel == -1);
    tk_rect r = tk_widget_rect(w);
    tk_menu_popup(app->root, m, r.x + r.w, r.y + r.h);
}

/* ---------------------------------------------------------------- */
/* construction                                                      */
/* ---------------------------------------------------------------- */

notes_app *notes_app_new(nm_store *store, int w, int h, bool with_osk)
{
    notes_app *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->store = store;
    app->folder_sel = -2;
    app->win_w = w;
    app->win_h = h;
    app->screen = SCR_LIST;
    /* mode from the theme main() installed (or a test set):
     * style 2 = WinUI. The constructor never reads the reg. */
    app->winui = tk_theme_default()->style == 2;

    tk_widget *top = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    const tk_theme *theme = tk_theme_default();

    if (app->winui) {
        /* ================= WinUI: 2 panes =================
         * OneNote / Notepad form: left pane = folder dropdown
         * + search box + note list + new-note button; right
         * pane = editor with its format bar on top. Selecting
         * a note loads it in place - no screen switching. */
        app->scr_panes = tk_box_new(TK_DIR_HORIZONTAL, 6, 8);
        tk_widget *left = tk_box_new(TK_DIR_VERTICAL, 4, 6);
        tk_widget_set_min_size(left, 264, 0);
        /* the box gives non-flex children their DESIRED width,
         * and the note list's desired width is its longest row
         * (~750px here) - that starved the editor to 84px in
         * the 854 shot. Both panes flex instead, weighted for
         * the design split 264:590 ~= 4:9 (min size floors the
         * left pane). */
        tk_set_flex(left, 4);
        {
            tk_widget *trow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
            tk_widget *title = tk_label_new("ノート");
            tk_widget_set_text_style(title, TK_TEXT_TITLE);
            tk_set_flex(title, 1);
            tk_add_child(trow, title);
            app->list_count = tk_label_new("");
            tk_widget_set_text_style(app->list_count,
                                     TK_TEXT_CAPTION);
            tk_add_child(trow, app->list_count);
            /* the new-note action lives in the pane header as
             * an icon button (the WinUI pane-header form -
             * Notepad/OneNote): as a full row under the list
             * it cost the list 56px of height, leaving it
             * 28px - less than one row - at the minimum
             * window height (proportion pass) */
            tk_widget *create = btn("", on_new_note, app);
            tk_button_set_variant(create, TK_BTN_FILLED);
            const uint32_t *plus = mode_icon(app, "add", "add",
                                             theme->on_accent);
            if (plus) {
                tk_button_set_icon(create, plus, 24, 24);
                tk_button_set_icon_layout(create, TK_BTN_ICON_ROW);
            }
            tk_widget_set_min_size(create, 48, 48);
            tk_add_child(trow, create);
            tk_add_child(left, trow);

            app->folder_combo = tk_combobox_new();
            tk_combobox_set_on_change(app->folder_combo,
                                      on_folder_combo_change, app);
            tk_add_child(left, app->folder_combo);

            app->list_search_row = tk_box_new(TK_DIR_HORIZONTAL,
                                              4, 0);
            app->list_search_field = tk_textfield_new("");
            tk_textfield_set_placeholder(app->list_search_field,
                                         "検索");
            tk_set_flex(app->list_search_field, 1);
            tk_add_child(app->list_search_row,
                         app->list_search_field);
            tk_widget *go = btn("検索", on_list_search, app);
            tk_button_set_variant(go, TK_BTN_TONAL);
            const uint32_t *sic = mode_icon(app, "search", "search",
                                            theme->text);
            if (sic) {
                tk_button_set_icon(go, sic, 24, 24);
                tk_button_set_icon_layout(go, TK_BTN_ICON_ROW);
            }
            tk_add_child(app->list_search_row, go);
            tk_add_child(left, app->list_search_row);

            app->note_list = tk_listview_new();
            tk_listview_set_on_select(app->note_list,
                                      on_note_select, app);
            tk_set_flex(app->note_list, 1);
            tk_add_child(left, app->note_list);
            app->empty_label = tk_label_new(
                "ノートがありません");
            tk_widget_set_text_style(app->empty_label,
                                     TK_TEXT_CAPTION);
            tk_set_visible(app->empty_label, false);
            tk_add_child(left, app->empty_label);
        }
        tk_add_child(app->scr_panes, left);
        /* the editor pane is built by the shared editor block
         * below and re-parented here */
        app->scr_editor = tk_box_new(TK_DIR_VERTICAL, 4, 6);
        tk_set_flex(app->scr_editor, 9);
        tk_add_child(app->scr_panes, app->scr_editor);
        tk_add_child(top, app->scr_panes);
        tk_set_flex(app->scr_panes, 1);
    } else {
    /* ---------- list screen (Android drill-down) ---------- */
    app->scr_list = tk_box_new(TK_DIR_VERTICAL, 4, 6);
    {
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_widget_set_min_size(hdr, 0, 56);
        tk_widget *tbox = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_set_flex(tbox, 1);
        tk_widget *title = tk_label_new("ノート");
        tk_widget_set_text_style(title, TK_TEXT_TITLE);
        tk_add_child(tbox, title);
        app->list_count = tk_label_new("");
        tk_widget_set_text_style(app->list_count, TK_TEXT_CAPTION);
        tk_add_child(tbox, app->list_count);
        tk_add_child(hdr, tbox);
        tk_add_child(hdr, header_icon_btn(
            "検索", mode_icon(app, "search", "search", theme->text),
            on_list_search_toggle, app));
        tk_add_child(hdr, header_icon_btn(
            "フォルダ",
            mode_icon(app, "folder", "folder", theme->text),
            on_folders_open, app));
        tk_add_child(hdr, header_icon_btn(
            "その他",
            mode_icon(app, "more_vert", "more_vertical", theme->text),
            on_list_more, app));
        tk_add_child(app->scr_list, hdr);

        app->list_search_row = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->list_search_field = tk_textfield_new("");
        tk_textfield_set_placeholder(app->list_search_field, "検索");
        tk_set_flex(app->list_search_field, 1);
        tk_add_child(app->list_search_row, app->list_search_field);
        tk_widget *go = btn("検索", on_list_search, app);
        tk_button_set_variant(go, TK_BTN_TONAL);
        tk_add_child(app->list_search_row, go);
        tk_set_visible(app->list_search_row, false);
        tk_add_child(app->scr_list, app->list_search_row);

        app->note_list = tk_listview_new();
        tk_listview_set_on_select(app->note_list, on_note_select, app);
        tk_set_flex(app->note_list, 1);
        tk_add_child(app->scr_list, app->note_list);
        app->empty_label = tk_label_new(
            "ノートがありません。「新規ノート」から作成できます");
        tk_widget_set_text_style(app->empty_label,
                                 TK_TEXT_CAPTION);
        tk_set_visible(app->empty_label, false);
        tk_add_child(app->scr_list, app->empty_label);

        /* the one primary action on this screen (SS7.2), in
         * the extended-FAB form: natural width, right edge
         * (proportion pass - it used to stretch the full
         * 838px width of the list screen) */
        tk_widget *create = btn("新規ノート", on_new_note, app);
        tk_button_set_variant(create, TK_BTN_FILLED);
        const uint32_t *plus = mode_icon(app, "add", "add",
                                         theme->on_accent);
        if (plus) {
            tk_button_set_icon(create, plus, 24, 24);
            tk_button_set_icon_layout(create, TK_BTN_ICON_ROW);
        }
        tk_widget_set_min_size(create, 0, 56);
        tk_widget *crow = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_widget *csp = tk_fixed_new();
        tk_set_flex(csp, 1);
        tk_add_child(crow, csp);
        tk_add_child(crow, create);
        tk_add_child(app->scr_list, crow);
    }
    tk_add_child(top, app->scr_list);
    tk_set_flex(app->scr_list, 1);
    }

    /* ---------- folder screen (Android only; the WinUI form
     * switches folders from the left pane's dropdown) ---------- */
    if (!app->winui) {
    app->scr_folders = tk_box_new(TK_DIR_VERTICAL, 4, 6);
    {
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_widget_set_min_size(hdr, 0, 56);
        tk_add_child(hdr, header_back_btn(app, on_folders_back, app));
        tk_widget *title = tk_label_new("フォルダ");
        tk_widget_set_text_style(title, TK_TEXT_HEADING);
        tk_add_child(hdr, title);
        tk_add_child(app->scr_folders, hdr);
        app->folder_list = tk_listview_new();
        tk_listview_set_on_select(app->folder_list, on_folder_select,
                                  app);
        tk_set_flex(app->folder_list, 1);
        tk_add_child(app->scr_folders, app->folder_list);
    }
    tk_add_child(top, app->scr_folders);
    tk_set_flex(app->scr_folders, 1);
    tk_set_visible(app->scr_folders, false);
    }

    /* ---------- editor (screen in Android mode, the right
     * pane in WinUI mode) ---------- */
    if (!app->winui)
        app->scr_editor = tk_box_new(TK_DIR_VERTICAL, 4, 6);
    {
        tk_widget *hdr = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        tk_widget_set_min_size(hdr, 0, 56);
        if (!app->winui)  /* panes have no back navigation */
            tk_add_child(hdr, header_back_btn(app, on_editor_back,
                                              app));
        app->editor_folder = tk_label_new("");
        tk_widget_set_text_style(app->editor_folder, TK_TEXT_HEADING);
        tk_set_flex(app->editor_folder, 1);
        tk_add_child(hdr, app->editor_folder);
        tk_add_child(hdr, header_icon_btn(
            "検索", mode_icon(app, "search", "search", theme->text),
            on_search_toggle, app));
        tk_add_child(hdr, header_icon_btn(
            "その他",
            mode_icon(app, "more_vert", "more_vertical", theme->text),
            on_editor_more, app));
        tk_add_child(app->scr_editor, hdr);

        app->search_bar = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->search_field = tk_textfield_new("");
        tk_textfield_set_placeholder(app->search_field, "検索");
        tk_set_flex(app->search_field, 1);
        tk_add_child(app->search_bar, app->search_field);
        tk_add_child(app->search_bar, btn("検索する", on_search, app));
        tk_add_child(app->search_bar, btn("置換", on_replace_toggle,
                                          app));
        tk_set_visible(app->search_bar, false);
        tk_add_child(app->scr_editor, app->search_bar);

        app->replace_bar = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        app->replace_field = tk_textfield_new("");
        tk_textfield_set_placeholder(app->replace_field, "置換後の文字列");
        tk_set_flex(app->replace_field, 1);
        tk_add_child(app->replace_bar, app->replace_field);
        tk_add_child(app->replace_bar, btn("置換", on_replace, app));
        tk_add_child(app->replace_bar, btn("全置換", on_replace_all,
                                           app));
        tk_set_visible(app->replace_bar, false);
        tk_add_child(app->scr_editor, app->replace_bar);

        app->editor = tk_textview_new("");
        tk_textview_set_on_change(app->editor, on_editor_change, app);
        tk_set_flex(app->editor, 1);

        /* format bar: BELOW the editor in the Android form
         * (SS7.2 screen order: header / search bar / editor /
         * format bar / OSK), at the TOP of the editor pane in
         * the WinUI form (OneNote / Notepad ribbon position).
         * The B/I/U labels are the conventional editing-command
         * notation, not icons. */
        tk_widget *frow = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
        struct { const char *glyph, *name; tk_notify_cb cb; } fmts[8] = {
            { "#", "見出し", on_heading },
            { "B", "太字", on_bold },
            { "I", "斜体", on_italic },
            { "U", "下線", on_underline },
            { "S", "取消線", on_strike },
            { "•", "箇条書き", on_bullet },
            { "1.", "番号付きリスト", on_numbered },
            { "[ ]", "チェックリスト", on_checklist },
        };
        for (int i = 0; i < 8; i++) {
            tk_widget *b = btn(fmts[i].glyph, fmts[i].cb, app);
            tk_button_set_flat(b, true);
            tk_widget_set_tooltip(b, fmts[i].name);
            tk_widget_set_min_size(b, 48, 48);
            tk_add_child(frow, b);
        }
        app->format_bar = frow;
        if (app->winui) {
            tk_add_child(app->scr_editor, frow);
            tk_add_child(app->scr_editor, app->editor);
        } else {
            tk_add_child(app->scr_editor, app->editor);
            tk_add_child(app->scr_editor, frow);
        }

        app->status = tk_label_new("");
        tk_widget_set_text_style(app->status, TK_TEXT_CAPTION);
        tk_add_child(app->scr_editor, app->status);

        if (with_osk) {
            app->osk = tk_osk_new();
            tk_osk_attach(app->osk, app->editor);
            tk_add_child(app->scr_editor, app->osk);
        }
    }
    if (!app->winui) {
        tk_add_child(top, app->scr_editor);
        tk_set_flex(app->scr_editor, 1);
        tk_set_visible(app->scr_editor, false);
    }

    /* attach dialog (path entry; a file-picker dialog is beyond
     * the toolkit's current public widgets - recorded as a gap) */
    app->attach_dlg = tk_dialog_new("ファイルを添付");
    app->attach_path = tk_textfield_new("");
    tk_textfield_set_placeholder(app->attach_path, "ファイルのパス");
    tk_add_child(app->attach_dlg, app->attach_path);
    tk_widget *abtns = tk_box_new(TK_DIR_HORIZONTAL, 4, 0);
    tk_widget *acancel = btn("キャンセル", on_attach_cancel, app);
    tk_button_set_variant(acancel, TK_BTN_TEXT);
    tk_add_child(abtns, acancel);
    tk_widget *aok = btn("添付", on_attach_ok, app);
    tk_button_set_variant(aok, TK_BTN_FILLED);
    tk_add_child(abtns, aok);
    tk_add_child(app->attach_dlg, abtns);

    app->top = top;
    app->root = tk_root_new(top);
    tk_widget_set_event_hook(top, notes_back_key, app);
    tk_root_layout(app->root, w, h);
    refresh_lists(app);
    return app;
}

void notes_app_free(notes_app *app)
{
    if (!app) return;
    notes_app_autosave(app);
    if (app->root && app->editor_menu)
        tk_menu_close(app->root, app->editor_menu);
    if (app->editor_menu) tk_widget_free(app->editor_menu);
    if (app->root && app->list_menu)
        tk_menu_close(app->root, app->list_menu);
    if (app->list_menu) tk_widget_free(app->list_menu);
    tk_widget_free(app->attach_dlg);
    tk_root_free(app->root);
    tk_widget_free(app->top);
    nm_list_free(app->notes, app->n_notes);
    nm_strv_free(app->folders, app->n_folders);
    free(app);
}

bool notes_app_winui(const notes_app *app) { return app->winui; }

tk_root *notes_app_root(notes_app *app) { return app->root; }
tk_widget *notes_app_editor(notes_app *app) { return app->editor; }
tk_widget *notes_app_format_bar(notes_app *app) { return app->format_bar; }
tk_widget *notes_app_osk(notes_app *app) { return app->osk; }

void notes_app_set_engine(notes_app *app, void *bi_engine)
{
    if (app->osk && bi_engine)
        tk_osk_set_backend(app->osk, osk_brainime_backend(), bi_engine);
}

/* ---------------------------------------------------------------- */
/* IME integration test hook                                         */
/* ---------------------------------------------------------------- */

int notes_ime_test(const char *dict, const char *bisc, const char *root)
{
    if (bisc) setenv("BI_SIDECAR", bisc, 1);
    bi_engine *eng = bi_engine_open(dict);
    if (!eng) { printf("I: engine open failed\n"); return 2; }
    nm_store *store = nm_open(root, 1 << 20);
    if (!store) { printf("I: store open failed\n"); return 3; }
    uint64_t id = nm_create(store, NULL, "");
    if (!id) { printf("I: create failed\n"); return 4; }
    notes_app *app = notes_app_new(store, 480, 800, true);
    if (!app || !app->osk) { printf("I: ui build failed\n"); return 5; }
    notes_app_set_engine(app, eng);
    notes_app_select(app, id);
    tk_focus(app->root, app->editor);

    /* SS7.2 screen order: the format bar sits BELOW the editor
     * (header / search bar / editor / format bar / OSK) */
    {
        tk_rect er = tk_widget_rect(app->editor);
        tk_rect fr = tk_widget_rect(app->format_bar);
        printf("I: editor rect y=%d h=%d, format bar y=%d h=%d\n",
               er.y, er.h, fr.y, fr.h);
        if (fr.y < er.y + er.h) {
            printf("I: format bar is not below the editor\n");
            return 12;
        }
    }

    /* type the reading through the OSK's unified romaji path */
    if (!tk_osk_input_romaji(app->osk, "kyouhatenki")) {
        printf("I: romaji input rejected\n");
        return 6;
    }
    const char *conv = tk_osk_conversion(app->osk);
    char comp[256];
    tc_composition(tk_textview_buffer(app->editor), comp, sizeof(comp));
    printf("I: conversion=%s composition=%s\n",
           conv ? conv : "(null)", comp);
    if (!conv || strcmp(conv, "今日は天気") != 0) return 7;
    if (strcmp(comp, conv) != 0) return 8; /* composition reached editor */

    /* commit every segment */
    int guard = 0;
    while (tk_osk_reading(app->osk)[0] && guard++ < 16)
        tk_osk_commit(app->osk);
    char text[256];
    tk_textview_text(app->editor, text, sizeof(text));
    printf("I: committed text=%s dirty=%d\n", text,
           notes_app_dirty(app) ? 1 : 0);
    if (strcmp(text, "今日は天気") != 0) return 9;
    if (!notes_app_dirty(app)) return 10; /* edit marked for autosave */

    notes_app_autosave(app);
    char *saved = nm_load_body(store, id);
    printf("I: saved body=%s\n", saved ? saved : "(null)");
    int rc = saved && strcmp(saved, "今日は天気") == 0 ? 0 : 11;
    free(saved);
    if (rc != 0) { notes_app_free(app); nm_close(store); bi_engine_close(eng); return rc; }

    /* Damage presentation: selecting another note is a
     * programmatic setter path (tk_textview_set_text + list
     * rebuild). With no events involved, paint_damage must still
     * produce rects, shipped bytes and a pixel change - the
     * setters do not auto-invalidate, the app does it. */
    {
        tk_font *font = tk_font_load("../fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load("fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load_default();
        if (font) tk_font_install(font);
        uint64_t id2 = nm_create(store, NULL, "別のノート\n中身が違います\n");
        notes_app_refresh(app);
        tk_canvas *canvas = tk_canvas_new(480, 800);
        tk_fb *fb = tk_fb_open_mem(480, 800, 16);
        const tk_theme *theme = tk_theme_default();
        tk_root_paint_damage(app->root, canvas, theme);
        tk_fb_flush_damage(fb, canvas);
        size_t total = 480u * 800u * 4u;
        uint32_t *before = malloc(total);
        memcpy(before, tk_canvas_pixels(canvas), total);
        notes_app_select(app, id2); /* no events */
        int painted = tk_root_paint_damage(app->root, canvas, theme);
        size_t shipped = tk_fb_flush_damage(fb, canvas);
        bool changed = memcmp(before, tk_canvas_pixels(canvas),
                              total) != 0;
        printf("I: damage painted=%d shipped=%zu pixels_changed=%d\n",
               painted, shipped, changed ? 1 : 0);
        free(before);
        tk_fb_close(fb);
        tk_canvas_free(canvas);
        if (font) tk_font_free(font);
        if (painted <= 0 || shipped == 0 || !changed) rc = 12;
    }
    notes_app_free(app);
    nm_close(store);
    bi_engine_close(eng);
    return rc;
}

/* ---------------------------------------------------------------- */
/* main (app binary only)                                            */
/* ---------------------------------------------------------------- */

#ifdef BD_NOTES_MAIN
static void notes_tick_cb(void *ud, uint64_t mono)
{
    (void)mono;
    notes_app_autosave((notes_app *)ud);
}

int main(int argc, char **argv)
{
    bd_app_theme(); /* display mode from brainreg ui/mode */
    const char *home = getenv("HOME");
    char root[1024];
    const char *env_root = getenv("BRAINUX_NOTES_DIR");
    if (env_root) snprintf(root, sizeof(root), "%s", env_root);
    else if (home) snprintf(root, sizeof(root), "%s/.local/share/brainux/notes", home);
    else snprintf(root, sizeof(root), ".local/share/brainux/notes");
    nm_store *store = nm_open(root, 32u << 20);
    if (!store) { fprintf(stderr, "bd-notes: cannot open store %s\n", root); return 1; }

    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);

    /* IME engine: dictionary from the ime pipeline, when present */
    bi_engine *eng = NULL;
    const char *dict = getenv("BRAINUX_IME_DICT");
    if (!dict) dict = "/usr/share/brainux/ime/brainux2.bidf";
    if (argc > 1) dict = argv[1];
    eng = bi_engine_open(dict);

    /* Display first, real size (see bd_calc.c main note) */
    int vw, vh;
    bd_app_display *disp = bd_app_display_open_ex("メモ", 0, 0, 480, 272, 480, 272, &vw, &vh);
    notes_app *app = notes_app_new(store, vw, vh, true);
    if (eng) notes_app_set_engine(app, eng);

    if (bd_app_display_live(disp)) {
        bd_app_display_run(disp, app->root, notes_tick_cb, app, 20);
    } else {
        /* headless smoke: build + one paint, then exit */
        tk_canvas *canvas = tk_canvas_new(vw, vh);
        if (canvas) {
            tk_root_paint(app->root, canvas, tk_theme_default());
            tk_canvas_free(canvas);
        }
        printf("bd-notes: UI built (headless, no /dev/fb0)\n");
    }
    bd_app_display_close(disp);
    notes_app_free(app);
    if (eng) bi_engine_close(eng);
    nm_close(store);
    if (font) tk_font_free(font);
    return 0;
}
#endif /* BD_NOTES_MAIN */
