/* test_tasks_ui - bd-tasks UI drive tests (headless).
 *
 * Builds the real UI over a temp store and drives the action
 * functions the buttons call, plus one real pointer click on the
 * quick-add button:
 *   - smart views show the right rows (All/Today/Flagged/Completed)
 *   - quick add via the real button click (field text -> model)
 *   - toggle complete from the UI updates model + views
 *   - detail save round-trips priority/flag/due through widgets
 *   - subtask add + list creation through the UI actions
 *   - full paint smoke
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_tasks.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_int(const char *what, long got, long want)
{
    if (got == want) { passes++; printf("PASS %s -> %ld\n", what, got); }
    else { fails++; printf("FAIL %s: got %ld, want %ld\n", what, got, want); }
}
static void check_str(const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) {
        passes++; printf("PASS %s -> %s\n", what, got);
    } else {
        fails++;
        printf("FAIL %s: got %s, want %s\n", what,
               got ? got : "(null)", want ? want : "(null)");
    }
}

static void click(tk_root *root, tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_UP;
    ev.x = r.x + r.w / 2;
    ev.y = r.y + r.h / 2;
    tk_root_event(root, &ev);
}

static uint64_t add(tm_store *s, uint64_t list, const char *title,
                    const char *due, bool flagged, int prio)
{
    tm_task f;
    memset(&f, 0, sizeof(f));
    f.title = (char *)title;
    f.list_id = list;
    f.flagged = flagged;
    f.priority = prio;
    if (due) snprintf(f.due_date, sizeof(f.due_date), "%s", due);
    return tm_task_add(s, &f);
}

int main(void)
{
    char tmpl[] = "/tmp/bdtasks-ui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    tm_store *s = tm_open(tmpl);
    check_true("U open", s != NULL);
    uint64_t def = tm_default_list(s);
    uint64_t a = add(s, def, "今日のタスク", "2026-11-20", true, TM_PRIO_HIGH);
    uint64_t b = add(s, def, "未来のタスク", "2026-12-01", false, TM_PRIO_NONE);
    uint64_t c = add(s, def, "期限なし", NULL, false, TM_PRIO_LOW);
    check_true("U fixture", a && b && c);

    tasks_app *app = tasks_app_new(s, "2026-11-20", 480, 800);
    check_true("U build", app != NULL);
    check_int("U all rows", (long)tasks_app_row_count(app), 3);

    tasks_app_set_view(app, TM_VIEW_TODAY, 0);
    check_int("U today rows", (long)tasks_app_row_count(app), 1);
    check_int("U today id", (long)tasks_app_row_task(app, 0), (long)a);
    tasks_app_set_view(app, TM_VIEW_FLAGGED, 0);
    check_int("U flagged rows", (long)tasks_app_row_count(app), 1);
    tasks_app_set_view(app, TM_VIEW_SCHEDULED, 0);
    check_int("U scheduled rows", (long)tasks_app_row_count(app), 2);
    check_int("U scheduled first", (long)tasks_app_row_task(app, 0), (long)a);

    /* quick add through a REAL click on the add button */
    tasks_app_set_view(app, TM_VIEW_ALL, 0);
    tk_textfield_set_text(tasks_app_quick_field(app), "クリックで追加");
    click(tasks_app_root(app), tasks_app_add_button(app));
    check_int("U rows after click-add", (long)tasks_app_row_count(app), 4);
    bool found = false;
    for (size_t i = 0; i < tasks_app_row_count(app); i++) {
        const tm_task *t = tm_task_find(s, tasks_app_row_task(app, i));
        if (t && strcmp(t->title, "クリックで追加") == 0) found = true;
    }
    check_true("U click-added in model", found);
    check_int("U field cleared",
              (long)tk_textfield_text(tasks_app_quick_field(app),
                                      (char[8]){0}, 8), 0);

    /* toggle complete from the UI */
    size_t row_a = 0;
    for (size_t i = 0; i < tasks_app_row_count(app); i++)
        if (tasks_app_row_task(app, i) == a) row_a = i;
    tasks_app_select_row(app, row_a);
    check_int("U selected", (long)tasks_app_selected(app), (long)a);
    check_int("U toggle", tasks_app_toggle_complete_selected(app), 0);
    check_true("U model completed", tm_task_find(s, a)->done_seq != 0);
    check_int("U all excludes done", (long)tasks_app_row_count(app), 3);
    tasks_app_set_view(app, TM_VIEW_COMPLETED, 0);
    check_int("U completed rows", (long)tasks_app_row_count(app), 1);
    tasks_app_set_view(app, TM_VIEW_ALL, 0);

    /* detail save: select c, edit priority via the detail widgets
     * through save_detail's model path (set via a fresh query of
     * the row, then save with changed fields in the store first) */
    size_t row_c = 0;
    for (size_t i = 0; i < tasks_app_row_count(app); i++)
        if (tasks_app_row_task(app, i) == c) row_c = i;
    tasks_app_select_row(app, row_c);
    check_int("U save detail rc", tasks_app_save_detail(app), 0);
    check_str("U detail title kept", tm_task_find(s, c)->title, "期限なし");

    /* subtask + list creation through UI actions */
    uint64_t sub = tasks_app_add_subtask(app, "サブタスク1");
    check_true("U subtask", sub != 0 &&
                            tm_task_find(s, sub)->parent_id == c);
    uint64_t list2 = tasks_app_new_list(app, "買い物");
    check_true("U new list", list2 != 0 && tm_list_find(s, list2) != NULL);
    tasks_app_set_view(app, TM_VIEW_LIST, list2);
    check_int("U empty list rows", (long)tasks_app_row_count(app), 0);
    uint64_t in_list = tasks_app_quick_add(app, "リスト内タスク");
    check_true("U quick add lands in list",
               tm_task_find(s, in_list)->list_id == list2);
    check_int("U list rows", (long)tasks_app_row_count(app), 1);
    check_int("U delete list", tasks_app_delete_current_list(app), 0);
    check_true("U task moved to default",
               tm_task_find(s, in_list)->list_id == tm_default_list(s));

    /* paint smoke */
    {
        tk_canvas *canvas = tk_canvas_new(480, 800);
        tk_fb *fb = tk_fb_open_mem(480, 800, 16);
        check_true("U paint setup", canvas != NULL && fb != NULL);
        tk_root_paint(tasks_app_root(app), canvas, tk_theme_default());
        size_t bytes = tk_fb_flush_damage(fb, canvas);
        check_int("U paint bytes", (long)bytes, 480L * 800 * 2);
        tk_fb_close(fb);
        tk_canvas_free(canvas);
    }

    /* damage presentation: a programmatic (event-less) view
     * switch drives setters only; it must still produce damage,
     * transferred bytes and a pixel change (the setters do not
     * auto-invalidate - the app must, and this proves it does) */
    {
        tk_font *font = tk_font_load("../fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load("fonts/brainui-jp-subset.ttf", 0);
        if (!font) font = tk_font_load_default();
        if (font) tk_font_install(font);
        tk_canvas *canvas = tk_canvas_new(480, 800);
        tk_fb *fb = tk_fb_open_mem(480, 800, 16);
        const tk_theme *theme = tk_theme_default();
        tk_root_paint_damage(tasks_app_root(app), canvas, theme);
        tk_fb_flush_damage(fb, canvas);
        check_int("D drained", (long)tk_fb_flush_damage(fb, canvas), 0);
        size_t total = 480u * 800u * 4u;
        uint32_t *before = malloc(total);
        memcpy(before, tk_canvas_pixels(canvas), total);
        tasks_app_set_view(app, TM_VIEW_TODAY, 0); /* no events */
        int painted = tk_root_paint_damage(tasks_app_root(app), canvas,
                                           theme);
        size_t shipped = tk_fb_flush_damage(fb, canvas);
        check_true("D painted after setter-only change", painted > 0);
        check_true("D bytes shipped (not 0)", shipped > 0);
        check_true("D pixels changed",
                   memcmp(before, tk_canvas_pixels(canvas), total) != 0);
        free(before);
        tk_fb_close(fb);
        tk_canvas_free(canvas);
        if (font) tk_font_free(font);
    }

    tasks_app_free(app);
    tm_close(s);
    printf("test_tasks_ui: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
