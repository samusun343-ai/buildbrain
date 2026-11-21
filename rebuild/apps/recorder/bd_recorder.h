/* bd_recorder - Voice recorder UI (toolkit v2 public API only).
 *
 * Layout branches on the display mode (constructor reads
 * tk_theme_default()->style; 2 = WinUI):
 *  Android (Google Recorder form): view tabs (recordings /
 *  recently deleted) + search, single-column entry list,
 *  playback panel (waveform, play/pause, -15/+15 s, speed
 *  slider, rename, favorite, delete/restore/purge), trim
 *  panel, and the big centred record button at the bottom
 *  (pause / record / stop, level meter + elapsed above it).
 *  WinUI (Windows Voice Recorder form): recordings pane on
 *  the left (tabs + search + list), playback/waveform panel
 *  on the right, record controls in a bottom bar.
 * All transport / record / delete buttons carry real
 * vector-set icons (bd_icon_mode); no font glyphs.
 *
 * Audio backends are injectable factories so tests drive the
 * real UI with the synth source + null sink; production uses
 * ALSA capture/playback. The host drives time through
 * rec_app_tick() (monotonic ms): recording pumps the source,
 * playback pumps the player.
 */
#ifndef BD_RECORDER_H
#define BD_RECORDER_H

#include "tk.h"
#include "recmodel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rec_app rec_app;

/* Factories for tests; production passes NULL (ALSA defaults). */
typedef rc_source *(*rec_source_factory)(void *ud);
typedef rc_sink *(*rec_sink_factory)(void *ud);

rec_app *rec_app_new(const char *data_root,
                     rec_source_factory sf, rec_sink_factory kf, void *ud,
                     int w, int h);
void rec_app_free(rec_app *app);

void rec_app_tick(rec_app *app, uint64_t mono_ms);
bool rec_app_recording(const rec_app *app);
bool rec_app_playing(const rec_app *app);
const char *rec_app_selected(const rec_app *app);  /* entry name or NULL */
void rec_app_reload(rec_app *app);

/* Widget accessors (the UI tests drive the real widgets). */
tk_root *rec_app_root(rec_app *app);
tk_widget *rec_app_list_widget(rec_app *app);
tk_widget *rec_app_search_field(rec_app *app);
tk_widget *rec_app_tab_widget(rec_app *app, int deleted_view);
tk_widget *rec_app_record_button(rec_app *app);
tk_widget *rec_app_rec_pause_button(rec_app *app);
tk_widget *rec_app_rec_stop_button(rec_app *app);
tk_widget *rec_app_play_button(rec_app *app);
tk_widget *rec_app_delete_button(rec_app *app);
tk_widget *rec_app_restore_button(rec_app *app);
tk_widget *rec_app_purge_button(rec_app *app);
tk_widget *rec_app_status_label(rec_app *app);
tk_widget *rec_app_detail_label(rec_app *app);

#ifdef __cplusplus
}
#endif
#endif /* BD_RECORDER_H */
