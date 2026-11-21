/* kbdsettings - keyboard settings in the brainreg store (M2).
 *
 * The physical keyboard layer (tk_kbd.h, part of libbrainui2) is
 * configured by a tk_kbd_config. This module is the bridge to the
 * settings store: section "keyboard" in brainreg.
 *
 *   keyboard/layout             "brain" (fixed; see below)
 *   keyboard/repeat_source      "synthetic" (default) | "kernel"
 *   keyboard/repeat_delay_ms    first repeat delay   (def 400)
 *   keyboard/repeat_interval_ms repeat interval      (def 50)
 *   keyboard/longpress_ms       long-press time      (def 600)
 *
 * About "layout": the built-in keyboard has exactly ONE keymap
 * (the Brainux keymap; see tk_kbd.h and KEYBOARD-FIX.md). There
 * is no JIS/US switch to configure - the old "jis"/"us" values
 * described a PC-keyboard distinction that never applied to
 * this hardware. Load therefore ignores any stored layout value
 * (legacy "jis"/"us" included) and save writes "brain", so the
 * store stays self-describing and no UI may offer a layout
 * choice that does not exist.
 *
 * Load starts from tk_kbd_config_defaults() and overrides with
 * whatever the store holds; save writes the full config back.
 * Values are sanitized (tk_kbd_config_sanitize) on both paths, so
 * a hand-edited or corrupt store can never produce an out-of-range
 * configuration. Writes go through reg_set, i.e. they are atomic
 * (tmp + fsync + rename) like every brainreg write.
 *
 * The session embedder applies the loaded config to its evdev
 * backend with tk_evdev_kbd_set_config(); the shell's quick
 * settings panel edits it through bd_shell_kbd_config() /
 * bd_shell_set_kbd_config() (shellui.h).
 */
#ifndef BD_KBDSETTINGS_H
#define BD_KBDSETTINGS_H

#include "tk_kbd.h"

void bd_kbd_settings_load(tk_kbd_config *out);
void bd_kbd_settings_save(const tk_kbd_config *cfg);

/* Repeat presets cycled by the settings UI. */
typedef struct {
    const char *name;      /* 遅い / 標準 / 速い */
    int delay_ms;
    int interval_ms;
} bd_kbd_repeat_preset;

int bd_kbd_repeat_preset_count(void);
const bd_kbd_repeat_preset *bd_kbd_repeat_preset_at(int i);
/* Index of the preset matching cfg's delay/interval, or -1 when
 * the values are custom (e.g. hand-edited in the store). */
int bd_kbd_repeat_preset_of(const tk_kbd_config *cfg);

#endif /* BD_KBDSETTINGS_H */
