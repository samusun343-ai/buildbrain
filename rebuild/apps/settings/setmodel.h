/* setmodel - bd-settings model: pages and rows over the
 * SAME sources of truth the shell uses - brainreg (via the
 * shell's reg + kbdsettings bridges), bd_backend (brightness/
 * volume/battery - the identical object the quick settings
 * drive, so there is no second store), bd_wifi, and sysmodel
 * for the About page. Nothing here keeps a private copy of a
 * setting: every row reads through and writes through.
 *
 * Pages: keyboard / display / sound / Wi-Fi / power /
 * datetime / mascot / about / personal / storage (the last
 * two added in the 2026-11-21 rebuild - see
 * DESIGN-settings-rebuild.md sections 3-5 for the item
 * mapping table that justifies every page and row).
 * The personal page writes shell/wallpaper,
 * shell/panel_position and the display-mode key (section 6
 * of the design: the mode key is provisional until the
 * shell side fixes the canonical key). The sound page's
 * mute row mirrors bd_shell_set_muted exactly (brainreg
 * shell/muted plus the backend driven to the effective
 * level), and its volume slider persists shell/volume and
 * unmute-on-adjust like bd_shell_set_volume, so the quick
 * settings and this app can never disagree.
 * The mascot page writes the
 * brainreg "mascot" section the session reads at startup
 * (enabled / pack / size / fullscreen - see shell session.c);
 * changes therefore take effect from the next session start,
 * which the page's note row states. Rows are uniform: info (read-only),
 * slider (ranged int), cycle (discrete values, enumerated
 * in the row for in-place chip selection), toggle (boolean
 * switch), action (scan/connect). Every row carries its
 * current value and its own editor; the UI renders rows
 * generically and nothing is edited away from its row.
 *
 * Datetime edits only datetime/local_offset_min (the value
 * bd-clock displays by) - setting the system clock itself
 * needs privilege and is an OS-integration item, recorded
 * in the wave-2 report.
 */
#ifndef SETMODEL_H
#define SETMODEL_H

#include "backend.h"
#include "sysmodel.h"
#include "wifi.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SET_PAGE_KEYBOARD, SET_PAGE_DISPLAY, SET_PAGE_SOUND,
    SET_PAGE_WIFI, SET_PAGE_POWER, SET_PAGE_DATETIME,
    SET_PAGE_MASCOT, SET_PAGE_ABOUT,
    /* appended in the 2026-11-21 rebuild; the values above
     * are unchanged for existing callers (shot_apps) */
    SET_PAGE_PERSONAL, SET_PAGE_STORAGE, SET_PAGE_COUNT
};

typedef enum {
    SR_INFO, SR_SLIDER, SR_CYCLE, SR_ACTION, SR_TOGGLE
} set_row_kind;

#define SET_MAX_CHOICES 8

typedef struct {
    char id[32];
    char label[96];
    char value[128];
    set_row_kind kind;
    int slider_min, slider_max, slider_cur;   /* SR_SLIDER */
    bool toggle_on;                           /* SR_TOGGLE */
    /* SR_CYCLE: the full choice list, rendered by the UI as
     * chips inside the row so the value is chosen in place.
     * choice_cur is -1 when the current value matches no
     * choice (a hand-edited / custom value). */
    int n_choices;
    int choice_cur;
    char choices[SET_MAX_CHOICES][40];
} set_row;

typedef struct set_model set_model;

/* backend/wifi/sys are borrowed (any may be NULL except that
 * pages degrade to info rows without them). */
set_model *set_model_new(bd_backend *backend, bd_wifi *wifi,
                         sy_sys *sys);
void set_model_free(set_model *m);

const char *set_page_name(int page);
size_t set_model_rows(set_model *m, int page, set_row *out, size_t max);

int set_model_set_slider(set_model *m, int page, const char *id,
                         int value);
int set_model_cycle(set_model *m, int page, const char *id);
/* SR_CYCLE: choose a specific entry of the row's choice list
 * (the chip path; set_model_cycle is the step-to-next form). */
int set_model_set_choice(set_model *m, int page, const char *id,
                         int idx);
/* SR_TOGGLE: set the switch state in place. */
int set_model_set_toggle(set_model *m, int page, const char *id,
                         bool on);
/* action rows: "scan" / "connect" (arg = SSID; PSK set with
 * set_model_set_psk first, empty = open network). */
int set_model_action(set_model *m, int page, const char *id,
                     const char *arg);
void set_model_set_psk(set_model *m, const char *psk);

/* direct accessors used by the UI for live values */
int set_brightness(set_model *m);
int set_volume(set_model *m);

#ifdef __cplusplus
}
#endif
#endif /* SETMODEL_H */
