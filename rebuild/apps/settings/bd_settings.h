/* bd_settings - Settings UI (toolkit v2 public API only).
 *
 * Rebuilt from scratch 2026-11-21 (the previous UI was
 * discarded on the user's judgement; see
 * DESIGN-settings-rebuild.md for the researched design and
 * the item mapping table, REPORT-settings-rebuild.md for
 * the record). The structure follows the REAL settings
 * apps, per mode - not a colour swap:
 *
 *   Android mode (theme style 0/1): the AOSP Settings form.
 *   A homepage with a search bar and the category list
 *   (coloured icon + title + live summary, two-line rows);
 *   tapping a category drills into its page: back button +
 *   page title on the app bar, the page's rows grouped
 *   under category headers in cards, every row edited in
 *   place (switch in the row, slider under the label,
 *   segmented choices under the label, PSK form expanding
 *   under a secured Wi-Fi network row).
 *
 *   WinUI mode (theme style 2): the Windows 11 Settings
 *   form. A permanent left navigation pane (NavigationView
 *   form) with the search box at its top, a breadcrumb +
 *   page title over the content, and one SettingsCard-form
 *   card per row (header icon + title + description, the
 *   control at the right edge; choice rows carry a combo
 *   box, per the Microsoft guidelines).
 *
 * Search in both modes covers EVERY row of every page and
 * lists hits as "item name + page name"; choosing one jumps
 * to that page with the row selected (the Android Settings
 * search behaviour).
 *
 * All state lives in setmodel (brainreg / bd_backend /
 * bd_wifi), so the shell's quick settings and this app can
 * never disagree. The display-mode switch on the personal
 * page writes the brainreg mode key through setmodel; the
 * shell and the apps are the followers (design section 6).
 */
#ifndef BD_SETTINGS_H
#define BD_SETTINGS_H

#include "tk.h"
#include "setmodel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct settings_app settings_app;

/* settings_app_page() returns this while the Android-mode
 * homepage is showing (the WinUI shell has no homepage;
 * a page is always open there). */
#define SETTINGS_PAGE_HOME (-1)

settings_app *settings_app_new(bd_backend *backend, bd_wifi *wifi,
                               sy_sys *sys, int w, int h);
void settings_app_free(settings_app *app);
tk_root *settings_app_root(settings_app *app);

int settings_app_page(const settings_app *app);
void settings_app_set_page(settings_app *app, int page);
size_t settings_app_rows(const settings_app *app);
void settings_app_refresh(settings_app *app);

/* Select a row of the current view (a page row, a homepage
 * category, or a search hit - the same state a tap
 * produces). Out-of-range indices clamp. */
void settings_app_select_row(settings_app *app, int idx);

/* Activate the selected entry: on a page, fire the row's
 * own action (cycle to the next value, flip the toggle, or
 * run the action); on the homepage, open the selected
 * category; on search results, jump to the selected hit. */
void settings_app_activate_row(settings_app *app);

/* widget accessors for tests */
/* the page-navigation widget: the homepage category list
 * (Android mode) or the navigation drawer (WinUI mode) */
tk_widget *settings_app_page_list(settings_app *app);
/* the search widget (a tk_searchbar in both modes) */
tk_widget *settings_app_search_field(settings_app *app);
/* the rows scroll pane */
tk_widget *settings_app_row_list(settings_app *app);
/* the selected row's own inline slider, or NULL when the
 * selected row has none */
tk_widget *settings_app_editor_slider(settings_app *app);
tk_widget *settings_app_psk_field(settings_app *app);
tk_widget *settings_app_status_label(settings_app *app);

#ifdef __cplusplus
}
#endif
#endif /* BD_SETTINGS_H */
