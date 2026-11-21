/* bd_calc - bd-calc UI on libbrainui2 (toolkit v2 public API
 * only). Four modes over calcmodel: 標準 (basic pad), 関数
 * (scientific pad with 2nd/Deg-Rad), プログラマ (programmer),
 * 変換 (unit conversion). Dual-mode (bdappmode.h): in Android
 * mode the modes sit under a tab strip and the history tape
 * is a sheet opened from the history icon; in WinUI mode a
 * mode menu + title sits at the top left and the history /
 * memory pane is always visible on the right. The public
 * mode indices below are the same in both modes.
 *
 * Keys are pressed in tests through calc_app_press(), which
 * dispatches real pointer events at the button's arranged rect
 * (the same path a finger takes) - there is no test-only back
 * door into the evaluator.
 */
#ifndef BD_CALC_H
#define BD_CALC_H

#include "calcmodel.h"
#include "tk.h"

typedef struct calc_app calc_app;

/* dir: store directory (history.txt + rates.txt live there). */
calc_app *calc_app_new(const char *dir, int w, int h);
void calc_app_free(calc_app *app);
tk_root *calc_app_root(calc_app *app);

/* tabs: 0 standard, 1 scientific, 2 programmer, 3 convert */
int calc_app_tab(const calc_app *app);
void calc_app_set_tab(calc_app *app, int tab);

/* Press the key whose CURRENT visible label is `label` on the
 * active expression/programmer page. Returns 0 when found. */
int calc_app_press(calc_app *app, const char *label);

/* expression page state */
void calc_app_expr(const calc_app *app, char *out, size_t cap);
void calc_app_set_expr(calc_app *app, const char *text);
void calc_app_result(const calc_app *app, char *out, size_t cap);
double calc_app_memory(const calc_app *app);
bool calc_app_deg(const calc_app *app);
bool calc_app_second(const calc_app *app);
size_t calc_app_history_rows(const calc_app *app);
/* Load history row i's expression into the expression field. */
int calc_app_history_load(calc_app *app, size_t i);

/* programmer page state */
void calc_app_prog_display(const calc_app *app, char *out, size_t cap);
void calc_app_prog_bits(const calc_app *app, char *out, size_t cap);
int calc_app_prog_base(const calc_app *app);
void calc_app_prog_set_base(calc_app *app, int base);

/* conversion page */
void calc_app_conv_set_category(calc_app *app, size_t cat);
void calc_app_conv_set_units(calc_app *app, size_t from, size_t to);
void calc_app_conv_set_value(calc_app *app, const char *text);
kc_err calc_app_convert_now(calc_app *app);
void calc_app_conv_result(const calc_app *app, char *out, size_t cap);
/* currency: set the JPY rate for a unit name (persisted). */
int calc_app_set_rate(calc_app *app, const char *unit, double per_jpy);

/* widget accessors for tests */
tk_widget *calc_app_expr_field(calc_app *app);
tk_widget *calc_app_result_label(calc_app *app);
tk_widget *calc_app_history_list(calc_app *app);
tk_widget *calc_app_conv_value_field(calc_app *app);

#endif /* BD_CALC_H */
