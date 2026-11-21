/* bd_notes - notes app UI on libbrainui2 (toolkit v2).
 *
 * Layout follows DESIGN-apps-envfit.md SS7.2 (Samsung Notes
 * fitted to this panel): three screens drilled between - a note
 * LIST (title + count header, note cards with previews, one
 * filled "new note" primary button), a FOLDER screen, and an
 * EDITOR screen (back + folder name + search + overflow menu,
 * glyph format bar, OSK when enabled). Selecting a note opens
 * the editor screen (notes_app_select performs the switch).
 * The editor is a toolkit TextView backed by textcore; the OSK
 * (toolkit widget, osk.h) attaches to it and routes
 * physical-keyboard, flick and romaji input through
 * libbrainime compositions into the same buffer - one input
 * path for every source.
 *
 * Autosave: the editor's on_change marks the note dirty; saving
 * (nm_save_body, atomic) happens on note switch, folder switch,
 * explicit notes_app_autosave() (the main loop calls it when the
 * UI goes idle) and at close. There is no save button, matching
 * the reference.
 */
#ifndef BD_NOTES_H
#define BD_NOTES_H

#include <stdbool.h>
#include <stdint.h>
#include "tk.h"
#include "nmodel.h"

typedef struct notes_app notes_app;

/* Build the UI over an open store. with_osk adds the OSK widget
 * at the bottom (attached to the editor). w/h is the root size. */
notes_app *notes_app_new(nm_store *store, int w, int h, bool with_osk);
void notes_app_free(notes_app *app);

/* True when built under the WinUI (style 2) theme: the
 * 2-pane layout is in use instead of the drill-down screens.
 * The mode is read from the theme only, never from the reg. */
bool notes_app_winui(const notes_app *app);

tk_root *notes_app_root(notes_app *app);
tk_widget *notes_app_editor(notes_app *app);   /* TextView */
tk_widget *notes_app_format_bar(notes_app *app); /* below the editor */
tk_widget *notes_app_osk(notes_app *app);      /* may be NULL */

/* Attach a libbrainime engine (bi_engine *) to the OSK backend.
 * The app does not own the engine. No-op without an OSK. */
void notes_app_set_engine(notes_app *app, void *bi_engine);

/* Select a note in the UI (autosaves the current one first). */
void notes_app_select(notes_app *app, uint64_t id);
uint64_t notes_app_current(const notes_app *app);
bool notes_app_dirty(const notes_app *app);
void notes_app_autosave(notes_app *app);       /* save if dirty */
void notes_app_refresh(notes_app *app);        /* rebuild lists */

/* Editor operations (also wired to the toolbar buttons). */
bool notes_editor_find_next(notes_app *app, const char *query);
int notes_editor_replace_all(notes_app *app, const char *query,
                             const char *replacement);
void notes_editor_toggle_heading(notes_app *app);
void notes_editor_wrap_selection(notes_app *app, const char *left,
                               const char *right);
void notes_editor_toggle_list(notes_app *app, bool numbered);
void notes_editor_toggle_checklist(notes_app *app);

/* IME integration test hook (used by tests/test_notes.c): builds
 * the full UI over a fresh store at `root`, types romaji through
 * the OSK with the libbrainime backend, verifies the composition
 * in the editor buffer, commits, autosaves and re-reads the note
 * from disk. Returns 0 on success. */
int notes_ime_test(const char *dict, const char *bisc,
                   const char *root);

#endif /* BD_NOTES_H */
