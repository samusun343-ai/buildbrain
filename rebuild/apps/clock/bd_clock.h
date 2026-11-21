/* bd_clock - bd-clock UI on libbrainui2 (toolkit v2 public API
 * only). Four pages (world clock / alarms / stopwatch / timers)
 * over clockmodel. Dual-mode (bdappmode.h): in Android mode a
 * bottom navigation bar (Google Clock form), in WinUI mode a
 * left navigation rail (Windows Clock form); the public tab
 * indices below are the same in both modes. Time is injected
 * through clock_app_tick() -
 * the UI never reads the system clock itself, so tests drive it
 * deterministically; main() supplies time() + CLOCK_MONOTONIC.
 */
#ifndef BD_CLOCK_H
#define BD_CLOCK_H

#include "clockmodel.h"
#include "tk.h"

typedef struct clock_app clock_app;

/* local_offset_min: the device's local UTC offset (minutes),
 * used for the local-time readout and alarm interpretation. */
clock_app *clock_app_new(ck_store *store, int local_offset_min,
                         int w, int h);
void clock_app_free(clock_app *app);
tk_root *clock_app_root(clock_app *app);

/* Advance to (wall_epoch_utc, mono_ms): polls the model,
 * refreshes time displays, surfaces ringing state. Returns the
 * number of model events surfaced by this tick. */
int clock_app_tick(clock_app *app, int64_t wall, int64_t mono_ms);

/* tabs: 0 world, 1 alarms, 2 stopwatch, 3 timers */
int clock_app_tab(const clock_app *app);
void clock_app_set_tab(clock_app *app, int tab);

/* world clock */
size_t clock_app_city_rows(const clock_app *app);
/* Add the builtin city currently chosen in the add combobox. */
int clock_app_add_chosen_city(clock_app *app);
int clock_app_remove_city_at(clock_app *app, size_t row);

/* alarms */
size_t clock_app_alarm_rows(const clock_app *app);
uint64_t clock_app_alarm_id_at(const clock_app *app, size_t row);
void clock_app_select_alarm_row(clock_app *app, size_t row);
/* Toggle the enabled flag of the alarm on `row` (the list's
 * activate action - no editor round-trip, per the reference). */
int clock_app_toggle_alarm_row(clock_app *app, size_t row);
/* Create an alarm from the editor widgets / save over the
 * selected one. Returns the alarm id or 0 on invalid input. */
uint64_t clock_app_editor_new(clock_app *app);
int clock_app_editor_save(clock_app *app);
int clock_app_delete_selected_alarm(clock_app *app);

/* stopwatch */
void clock_app_sw_toggle(clock_app *app, int64_t mono_ms);
void clock_app_sw_lap_or_reset(clock_app *app, int64_t mono_ms);
int clock_app_sw_laps(const clock_app *app);
bool clock_app_sw_running(const clock_app *app);

/* timers */
size_t clock_app_timer_rows(const clock_app *app);
/* Create a timer from the H/M/S + label editor widgets. */
uint64_t clock_app_editor_new_timer(clock_app *app);
int clock_app_timer_start(clock_app *app, size_t row,
                           int64_t wall, int64_t mono_ms);
int clock_app_timer_pause(clock_app *app, size_t row, int64_t mono_ms);
int clock_app_timer_resume(clock_app *app, size_t row,
                            int64_t wall, int64_t mono_ms);
int clock_app_timer_cancel(clock_app *app, size_t row);
int clock_app_timer_delete(clock_app *app, size_t row);

/* ringing banner */
bool clock_app_ringing(const clock_app *app);
int clock_app_snooze(clock_app *app, int64_t mono_ms);
int clock_app_dismiss(clock_app *app);

/* widget accessors for tests */
tk_widget *clock_app_city_list(clock_app *app);
tk_widget *clock_app_city_combo(clock_app *app);
tk_widget *clock_app_alarm_list(clock_app *app);
tk_widget *clock_app_sw_label(clock_app *app);
tk_widget *clock_app_status_label(clock_app *app);
tk_widget *clock_app_editor_hour(clock_app *app);
tk_widget *clock_app_editor_min(clock_app *app);
tk_widget *clock_app_editor_label(clock_app *app);
tk_widget *clock_app_timer_h(clock_app *app);
tk_widget *clock_app_timer_m(clock_app *app);
tk_widget *clock_app_timer_s(clock_app *app);
tk_widget *clock_app_timer_label(clock_app *app);
tk_widget *clock_app_timer_list(clock_app *app);

#endif /* BD_CLOCK_H */
