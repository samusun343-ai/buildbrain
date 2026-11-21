/* bd_files - file manager UI on libbrainui2 (toolkit v2).
 *
 * Layout follows DESIGN-apps-envfit.md SS7.1 (Samsung My Files /
 * ChromeOS Files, fitted to this panel): ONE main screen - the
 * folder view. Header = back icon + folder name / path caption
 * + search / places / overflow icons; places are a menu, the
 * long tail of operations lives in the overflow menu (sort /
 * group submenus, hidden-files and grid check items, path
 * entry), and the selection's actions appear in a bottom
 * context bar. All filesystem behaviour lives in fmodel; this
 * file is presentation + wiring, and every control calls one
 * of the files_app_* action functions below (which is also
 * what the UI tests drive).
 *
 * Opening a file goes through the desktop catalog (shell's
 * desktop.h) + bdmime associations; a launch hook can intercept
 * the argv (tests do; production forks/execs).
 */
#ifndef BD_FILES_H
#define BD_FILES_H

#include <stdbool.h>
#include "tk.h"
#include "fmodel.h"

typedef struct files_app files_app;

/* data_root holds app-private state (trash). The application
 * catalog is loaded from $BD_FILES_APP_DIRS (colon-separated)
 * when set, else ~/.local/share/applications + /usr/share/
 * applications. brainreg (shell reg.h) supplies view prefs and
 * the trash budget; tests point BRAINREG_*_DIR at a temp dir. */
files_app *files_app_new(const char *start_dir, const char *data_root,
                         int w, int h);
void files_app_free(files_app *app);

tk_root *files_app_root(files_app *app);

/* state inspection (UI tests) */
const char *files_app_dir(files_app *app);      /* NULL in trash view */
bool files_app_in_trash(files_app *app);
size_t files_app_row_count(files_app *app);     /* incl. group headers */
int files_app_selected(files_app *app);         /* entry idx or -1 */
uint64_t files_app_trash_bytes(files_app *app);

/* actions (the buttons call these) */
void files_app_navigate(files_app *app, const char *path);
void files_app_back(files_app *app);
void files_app_forward(files_app *app);
void files_app_up(files_app *app);
void files_app_refresh(files_app *app);
void files_app_select_row(files_app *app, size_t row);
void files_app_open_selected(files_app *app);
void files_app_open_trash(files_app *app);
int files_app_new_folder(files_app *app, const char *name);
int files_app_new_file(files_app *app, const char *name);
int files_app_rename_selected(files_app *app, const char *new_name);
int files_app_delete_selected(files_app *app);   /* to trash */
int files_app_restore_selected(files_app *app);  /* trash view */
int files_app_delete_permanent_selected(files_app *app);
int files_app_empty_trash(files_app *app);
void files_app_copy_selected(files_app *app, bool cut);
int files_app_paste(files_app *app);
void files_app_set_show_hidden(files_app *app, bool v);
void files_app_set_grid(files_app *app, bool grid);
void files_app_set_sort(files_app *app, fm_sort_key key, bool desc);
void files_app_set_group(files_app *app, fm_group g);
int files_app_search(files_app *app, const char *query);

/* Intercept file launches: the hook receives the argv (valid only
 * during the call) and returns true when it "launched". */
typedef bool (*files_launch_hook)(void *ud, char *const argv[]);
void files_app_set_launch_hook(files_app *app, files_launch_hook fn,
                               void *ud);

#endif /* BD_FILES_H */
