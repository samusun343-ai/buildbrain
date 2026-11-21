/* assist - single-finger assist overlay for shell v2 (M2).
 *
 * The Brain touch panel recognizes exactly one finger, and the
 * user asked for an assist tool for that constraint. This is a
 * fresh implementation on toolkit v2 (the old scenes.c assist was
 * discarded in the audit): a 56px floating button pinned to the
 * right edge opens a panel of large (>=56px tall) buttons -
 * volume, brightness, page up/down, launcher, lock - all reachable
 * and tappable with one finger, no gestures, no multi-touch, no
 * long-press required.
 *
 * The overlay is pure UI: actions leave through the callback. The
 * shell wires volume/brightness to the backend (and persists
 * volume through reg), page keys to the focused window, launcher
 * to its own view switch, lock to the session's lock hook.
 * Volume/brightness actions keep the panel open (repeated taps
 * adjust further); launcher/lock close it.
 */
#ifndef BD_ASSIST_H
#define BD_ASSIST_H

#include <stdbool.h>
#include "tk.h"

typedef enum {
    BD_ASSIST_VOL_UP = 0, BD_ASSIST_VOL_DOWN,
    BD_ASSIST_BRIGHT_UP, BD_ASSIST_BRIGHT_DOWN,
    BD_ASSIST_PAGE_UP, BD_ASSIST_PAGE_DOWN,
    BD_ASSIST_LAUNCHER, BD_ASSIST_LOCK
} bd_assist_action;

typedef void (*bd_assist_cb)(void *userdata, bd_assist_action action);

typedef struct bd_assist bd_assist;

/* Build the overlay. layer() returns a Fixed widget sized by
 * bd_assist_layout(); add it as the last child of a Fixed parent
 * that covers the shell's main area so it floats above the views. */
bd_assist *bd_assist_new(bd_assist_cb cb, void *userdata);
void bd_assist_free(bd_assist *a);
tk_widget *bd_assist_layer(bd_assist *a);
void bd_assist_layout(bd_assist *a, int w, int h);

bool bd_assist_open(const bd_assist *a);
void bd_assist_set_open(bd_assist *a, bool open);

/* Current values shown at the top of the sheet (UI-REDESIGN 2.3:
 * every assist row's effect must be visible where it is made).
 * The shell calls this whenever volume/brightness change. */
void bd_assist_set_values(bd_assist *a, int volume, int brightness);

/* Keep the button/sheet out of a right-side strip of `px` pixels
 * (e.g. while the quick-settings panel occupies the bottom-right
 * corner, its footer buttons must stay tappable). 0 restores the
 * default corner position. */
void bd_assist_set_avoid_right(bd_assist *a, int px);

/* Geometry (for tests/diagnostics): rects in layer coordinates. */
tk_rect bd_assist_button_rect(const bd_assist *a);
tk_rect bd_assist_panel_rect(const bd_assist *a);

#endif /* BD_ASSIST_H */
