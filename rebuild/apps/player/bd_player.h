/* bd_player - Music player UI (toolkit v2 public API only).
 *
 * Dual mode (REPORT-apps-dual-mode-player.md): the Android
 * form is the three drilled screens below; the WinUI form
 * is the Windows Media Player layout (left navigation
 * pane, right-pane list / queue / now playing, resident
 * bottom transport bar). main() reads the mode through
 * bd_app_theme(); the constructor branches on the default
 * theme's style.
 *
 * Three drilled screens (DESIGN-apps-envfit.md SS7.3):
 * Library (tab row: Songs / Albums / Artists / Genres /
 * Playlists; value tabs drill into their tracks under a small
 * back + name row; a bottom context bar carries the selected
 * row's operations; a mini player sits at the bottom),
 * Now Playing (folder artwork via the toolkit image loader,
 * metadata, scrub bar with elapsed/total or -remaining,
 * transport - prev restarts the track when >3 s in, per the
 * reference behaviour - shuffle toggle, three-state repeat,
 * volume), and the Up Next queue screen (play-next /
 * play-later from the library context bar; up / down /
 * remove / clear on its own bar). Playlists are created and
 * filled through one dialog reached from the context bar.
 *
 * The audio sink is an injectable factory (tests use the
 * null sink; production uses ALSA). The engine is
 * pump-driven: player_app_tick() decodes a bounded chunk and
 * "pause" simply stops pumping, so the UI never blocks on
 * decoding. Playback state is restored from playmodel's
 * state.txt on startup (paused, at the saved position).
 */
#ifndef BD_PLAYER_H
#define BD_PLAYER_H

#include "tk.h"
#include "pengine.h"
#include "playmodel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct player_app player_app;

typedef pe_out *(*pl_out_factory)(void *ud, uint32_t rate,
                                  uint16_t channels);

player_app *player_app_new(const char *data_root,
                           const char *const *scan_dirs, size_t n_dirs,
                           pl_out_factory of, void *ud, int w, int h);
void player_app_free(player_app *app);
tk_root *player_app_root(player_app *app);

void player_app_tick(player_app *app, uint64_t mono_ms);
bool player_app_playing(const player_app *app);
const char *player_app_now_path(const player_app *app); /* or NULL */
size_t player_app_rows(const player_app *app);
int player_app_view(const player_app *app);   /* 0 songs 1 alb 2 art 3 gen 4 lists */
void player_app_set_view(player_app *app, int view);
void player_app_open_row(player_app *app, size_t row);  /* drill or play */
void player_app_play_selected(player_app *app);
void player_app_back(player_app *app);
void player_app_show_queue(player_app *app);
void player_app_show_now_playing(player_app *app);
/* Open an explicit file list (the .desktop %F path): the
 * list becomes the play context and the first file plays. */
int player_app_open_files(player_app *app,
                          const char *const *paths, size_t n);

/* widget accessors for tests */
tk_widget *player_app_list_widget(player_app *app);
tk_widget *player_app_queue_widget(player_app *app);
tk_widget *player_app_play_button(player_app *app);
tk_widget *player_app_next_button(player_app *app);
tk_widget *player_app_prev_button(player_app *app);
tk_widget *player_app_repeat_button(player_app *app);
tk_widget *player_app_repeat_label(player_app *app);
tk_widget *player_app_np_title(player_app *app);
/* The five views are segments of ONE tab bar widget, not
 * child buttons: view_button returns the tab bar itself;
 * click a segment's centre to switch views. */
tk_widget *player_app_view_button(player_app *app, int view);
tk_widget *player_app_tabbar(player_app *app);
tk_widget *player_app_queue_later_button(player_app *app);
tk_widget *player_app_queue_next_button(player_app *app);
/* context-bar cell that opens the playlist dialog, and the
 * mini player's drill-in title button */
tk_widget *player_app_add_to_playlist_button(player_app *app);
tk_widget *player_app_mini_title_button(player_app *app);
tk_widget *player_app_list_name_field(player_app *app);
tk_widget *player_app_list_create_button(player_app *app);
tk_widget *player_app_list_add_button(player_app *app);

#ifdef __cplusplus
}
#endif
#endif /* BD_PLAYER_H */
