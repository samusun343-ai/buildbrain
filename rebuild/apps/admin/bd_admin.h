/* bd_admin - System monitor / admin UI (toolkit v2 public
 * API only). Tabs over sysmodel: Processes (sortable,
 * SIGTERM/SIGKILL actions on the selection), Storage
 * (mounts with statvfs sizes), Memory (meminfo), Logs
 * (file picker + bounded tail view with a substring
 * filter). Refresh is explicit (button) plus a slow
 * auto-refresh through admin_app_tick().
 *
 * Layout branches on the display mode (constructor reads
 * tk_theme_default()->style; 2 = WinUI): Android keeps the
 * single-column tab switch in Material form; WinUI uses
 * the Task Manager form (left navigation pane + content,
 * the process sort row as the table's column header).
 * Refresh / terminate / force-terminate are icon+label
 * buttons (refresh / close in Material, arrow_clockwise /
 * dismiss in Fluent, via bd_icon_mode).
 */
#ifndef BD_ADMIN_H
#define BD_ADMIN_H

#include "tk.h"
#include "sysmodel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct admin_app admin_app;

admin_app *admin_app_new(sy_sys *sys, int w, int h);
void admin_app_free(admin_app *app);
tk_root *admin_app_root(admin_app *app);

void admin_app_refresh(admin_app *app);
void admin_app_tick(admin_app *app, uint64_t mono_ms);
int admin_app_tab(const admin_app *app);   /* 0 proc 1 storage 2 mem 3 logs */
void admin_app_set_tab(admin_app *app, int tab);
size_t admin_app_proc_rows(const admin_app *app);
int admin_app_selected_pid(const admin_app *app);   /* -1 none */

/* widget accessors for tests */
tk_widget *admin_app_tab_button(admin_app *app, int tab);
tk_widget *admin_app_proc_list(admin_app *app);
tk_widget *admin_app_sort_button(admin_app *app, int field);
tk_widget *admin_app_term_button(admin_app *app);
tk_widget *admin_app_kill_button(admin_app *app);
tk_widget *admin_app_status_label(admin_app *app);
tk_widget *admin_app_storage_list(admin_app *app);
tk_widget *admin_app_mem_label(admin_app *app);
tk_widget *admin_app_log_list(admin_app *app);
tk_widget *admin_app_log_view(admin_app *app);
tk_widget *admin_app_log_filter(admin_app *app);
tk_widget *admin_app_log_load_button(admin_app *app);

#ifdef __cplusplus
}
#endif
#endif /* BD_ADMIN_H */
