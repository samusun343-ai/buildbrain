/* bd_tasks - reminders/tasks UI on libbrainui2 (toolkit v2).
 *
 * Layout follows the reference (Apple Reminders): left sidebar =
 * smart lists (Today / Scheduled / Flagged / All / Completed)
 * above the user's named lists with open-count badges; right =
 * quick-add field, task rows (completion state, title, priority
 * marks !/!!/!!!, flag, due), and a detail editor for the
 * selected task (notes, URL, due date/time, priority, flag,
 * repeat, section, subtasks).
 *
 * "Today" is injected by the caller (the app binary derives it
 * from localtime; tests pin it), keeping the model deterministic.
 * Due dates are display/sort data only - nothing expires by date.
 */
#ifndef BD_TASKS_H
#define BD_TASKS_H

#include <stdbool.h>
#include <stdint.h>
#include "tk.h"
#include "tmodel.h"

typedef struct tasks_app tasks_app;

tasks_app *tasks_app_new(tm_store *store, const char *today, int w, int h);
void tasks_app_free(tasks_app *app);

/* True when built under the WinUI (style 2) theme: the To Do
 * 2-pane layout is in use; otherwise the single-column Google
 * Tasks form. The mode is read from the theme only. */
bool tasks_app_winui(const tasks_app *app);

tk_root *tasks_app_root(tasks_app *app);
tk_widget *tasks_app_quick_field(tasks_app *app);
tk_widget *tasks_app_add_button(tasks_app *app);

/* view control */
void tasks_app_set_view(tasks_app *app, tm_view view, uint64_t list_id);
tm_view tasks_app_view(const tasks_app *app);
void tasks_app_refresh(tasks_app *app);

/* rows of the current view */
size_t tasks_app_row_count(tasks_app *app);
uint64_t tasks_app_row_task(tasks_app *app, size_t row); /* 0 if none */
void tasks_app_select_row(tasks_app *app, size_t row);
uint64_t tasks_app_selected(tasks_app *app);

/* actions (the buttons call these) */
uint64_t tasks_app_quick_add(tasks_app *app, const char *title);
int tasks_app_toggle_complete_selected(tasks_app *app);
int tasks_app_delete_selected(tasks_app *app);
uint64_t tasks_app_add_subtask(tasks_app *app, const char *title);
int tasks_app_save_detail(tasks_app *app);   /* detail fields -> model */
uint64_t tasks_app_new_list(tasks_app *app, const char *name);
int tasks_app_delete_current_list(tasks_app *app);
int tasks_app_cycle_list_color(tasks_app *app); /* palette 0..7 cycle */

#endif /* BD_TASKS_H */
