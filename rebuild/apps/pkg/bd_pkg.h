/* bd_pkg - Package manager UI (toolkit v2 public API only).
 *
 * Three views over pkgmodel: Installed / Catalog (the .deb
 * directory - the offline software source) / Updates (with a
 * count label acting as the badge). Detail panel shows
 * version, architecture, section, size, maintainer and
 * description. Install/remove run through pkgmodel; a
 * dry-run checkbox shows the exact dpkg argv instead of
 * executing - the mode the device uses until the privileged
 * helper lands in OS integration.
 *
 * Layout branches on the display mode (constructor reads
 * tk_theme_default()->style; 2 = WinUI): Android keeps the
 * Play Store app-management form (single column, view tabs
 * on top); WinUI uses the Store / Settings apps form (left
 * navigation pane + command row + list). Install / update /
 * remove are icon+label buttons (download / refresh /
 * delete in Material, arrow_download / arrow_clockwise /
 * delete in Fluent, via bd_icon_mode).
 */
#ifndef BD_PKG_H
#define BD_PKG_H

#include "tk.h"
#include "pkgmodel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pkg_app pkg_app;

pkg_app *pkg_app_new(const char *catalog_dir,
                     const char *dpkg_root, const char *dpkg_admin,
                     pkg_runner runner, void *ud, int w, int h);
void pkg_app_free(pkg_app *app);
tk_root *pkg_app_root(pkg_app *app);

void pkg_app_refresh(pkg_app *app);
size_t pkg_app_rows(const pkg_app *app);
size_t pkg_app_updates(const pkg_app *app);
int pkg_app_view(const pkg_app *app);   /* 0 inst 1 catalog 2 updates */
const char *pkg_app_selected_name(const pkg_app *app);

/* widget accessors for tests */
tk_widget *pkg_app_list_widget(pkg_app *app);
tk_widget *pkg_app_view_button(pkg_app *app, int view);
tk_widget *pkg_app_install_button(pkg_app *app);
tk_widget *pkg_app_update_all_button(pkg_app *app);
tk_widget *pkg_app_remove_button(pkg_app *app);
tk_widget *pkg_app_dryrun_box(pkg_app *app);
tk_widget *pkg_app_updates_label(pkg_app *app);
tk_widget *pkg_app_detail_name(pkg_app *app);
tk_widget *pkg_app_log_label(pkg_app *app);
tk_widget *pkg_app_search_field(pkg_app *app);

#ifdef __cplusplus
}
#endif
#endif /* BD_PKG_H */
