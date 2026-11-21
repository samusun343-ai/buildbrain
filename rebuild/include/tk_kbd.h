/* tk_kbd.h - physical keyboard layer.
 *
 * Sits between raw evdev key events and toolkit tk_events. The
 * evdev backend (tk_evdev.c) only reads the device; everything
 * keyboard-semantic lives here, in one key definition table.
 *
 * The table describes the SHARP Brain built-in keyboard
 * (PW-SH6 / PW-AJ2, 48 physical keys) AS BRAINUX DEFINES IT.
 * Three sources were cross-checked (see keyboard-research.md and
 * KEYBOARD-FIX.md for the full record):
 *
 *   - linux-brain DTS (imx28-pwsh6.dts): which evdev code each
 *     physical key emits. The dedicated keys emit BORROWED codes:
 *     home=KEY_ESC, kokugo/kanji=KEY_TAB, eiwa/waei=KEY_PAGEUP,
 *     My-dictionary=KEY_PAGEDOWN, history=KEY_INSERT,
 *     list=KEY_DELETE, page-up=KEY_LEFTCTRL, audio=KEY_LEFTALT.
 *     The symbol key is a driver-internal modifier and emits no
 *     event; while it is held the driver emits the symbol-layer
 *     codes (KEY_1..KEY_0, KEY_GRAVE, KEY_EQUAL, ...) instead.
 *   - Brainux Wiki (wiki.brainux.org, "get-started" keymap):
 *     Brainux interprets those codes AT FACE VALUE, like a US
 *     keyboard: ESC is Escape, TAB is Tab, PgU/PgD scroll, Ins is
 *     Insert, Del is Delete, the page-up key IS the Ctrl key and
 *     the audio key IS the Alt key. All non-engraved characters
 *     are produced by symbol/shift combinations per the Wiki
 *     keymap (symbol+Q..P = 1..0, symbol+Shift+Q..P = ! @ # ...,
 *     symbol+H = ';', symbol+Shift+H = ':', ...). This layer
 *     follows the Wiki: it is the shipped platform specification,
 *     and it is what makes Esc/Ctrl/Tab available at all.
 *   - The keys' WinCE dictionary functions (home screen, kokugo
 *     dictionary, audio playback, the shift-frame sub-functions
 *     clear/text-size/volume) are firmware semantics of the
 *     dictionary OS. They have no distinct evdev signal and are
 *     NOT reproduced here; a shell that wants them must bind
 *     them as its own policy (e.g. global shortcuts), not via
 *     this table.
 *
 * Consequences worth knowing:
 *   - There is NO JIS/US layout switch. The built-in keyboard
 *     has exactly one keymap (the Brainux one above). The
 *     `layout` config field survives only so stored settings
 *     stay readable: JIS/US are deprecated values that
 *     tk_kbd_config_sanitize() coerces to TK_KBD_LAYOUT_BRAIN,
 *     and the table never branches on the field.
 *   - Keys the Brain keyboard does not have (the JIS extra keys
 *     - yen, ro, henkan, muhenkan, zenkaku/hankaku, kana) are
 *     listed as TK_KBD_KIND_IGNORE so the table itself documents
 *     their absence. Producing those characters is the
 *     on-screen keyboard's job.
 *   - The media controls DO exist, on the symbol layer: the
 *     DTS assigns symbol+Left/Right to volume down/up,
 *     symbol+Up/Down to brightness up/down and symbol+Space to
 *     mute, so KEY_VOLUMEUP / KEY_VOLUMEDOWN / KEY_MUTE /
 *     KEY_BRIGHTNESSUP / KEY_BRIGHTNESSDOWN arrive while the
 *     symbol key is held and are mapped to the matching
 *     TK_ACTION_* (press-only, like KEY_POWER). The base layer
 *     never emits them: the same physical keys are the plain
 *     arrows and Space there.
 *   - Caps Lock state is kept for external PC keyboards only;
 *     the built-in keyboard has no Caps key.
 *   - KEY_END / F1..F12 never come from the built-in keyboard;
 *     they are mapped at face value for external keyboards.
 *     KEY_HOME also arrives from external keyboards, and from
 *     the built-in keyboard's unmarked (0,6) matrix contact,
 *     which the DTS assigns KEY_HOME (the contact's identity is
 *     unconfirmed - keyboard-research U3). KEY_POWER is an
 *     ACTION: the Wiki states the power button behaves like a
 *     PC power button. The symbol-layer media keys above are
 *     ACTIONs as well.
 *   - The back key and the page-down key DO emit: their matrix
 *     contacts were commented out in old DTS revisions, but the
 *     linux-brain DTS the build pins assigns (0,3) KEY_BACK
 *     (commit b0ac9f045) and, since the OS-internals phase,
 *     (3,4) KEY_SCROLLDOWN - KEY_PAGEDOWN was deliberately not
 *     used for (3,4) because the My-dictionary key already owns
 *     it (KEYBOARD-FIX.md section 5). Both are mapped below at
 *     face value: KEY_BACK is TK_KEY_BACK, KEY_SCROLLDOWN is
 *     TK_KEY_PAGE_DOWN. In the DTS symbol layer the page-down
 *     assignment is duplicated (as KEY_PAGEDOWN's is); the
 *     back key has no symbol-layer entry - like KEY_HOME - so
 *     it is inert while the symbols key is held.
 *
 * Repeat: exactly ONE repeat source is ever active.
 * TK_KBD_REPEAT_KERNEL passes evdev value==2 events through
 * (flagged repeat=true). TK_KBD_REPEAT_SYNTHETIC (the default)
 * drops kernel repeats and generates repeats from the
 * configured delay/interval in tk_kbd_poll(). Doubling is
 * therefore impossible by construction.
 *
 * Long press: tk_kbd_poll() emits one extra KEY_DOWN with
 * longpress=true when a key has been held for longpress_ms.
 * Repeats continue afterwards; consumers that act on long press
 * (e.g. the shell's power menu) can ignore the repeats.
 *
 * The layer is clock-driven by the caller (now_ms is a monotonic
 * millisecond counter), which makes it fully testable with
 * synthetic evdev events - see tests/test_kbd.c. It performs no
 * allocation after creation and no I/O.
 *
 * Text pointers in produced events point into the layer's held-key
 * state and stay valid until the next tk_kbd_feed()/tk_kbd_poll()
 * call; dispatch synchronously (tk_root_event does).
 */
#ifndef TK_KBD_H
#define TK_KBD_H

#include "tk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The built-in keyboard has ONE fixed keymap (Brainux, above).
 * JIS/US are deprecated: they described a PC-keyboard distinction
 * that never applied to this hardware. sanitize() coerces any
 * non-BRAIN value to BRAIN so legacy stored settings load
 * cleanly; the value never changes the table's behaviour. */
typedef enum {
    TK_KBD_LAYOUT_BRAIN = 0,
    TK_KBD_LAYOUT_JIS = 1,  /* deprecated alias, coerced to BRAIN */
    TK_KBD_LAYOUT_US = 2    /* deprecated alias, coerced to BRAIN */
} tk_kbd_layout;
typedef enum {
    TK_KBD_REPEAT_SYNTHETIC = 0,  /* generate in tk_kbd_poll() */
    TK_KBD_REPEAT_KERNEL = 1      /* pass evdev value==2 through */
} tk_kbd_repeat_mode;

typedef struct tk_kbd_config {
    tk_kbd_layout layout;   /* always BRAIN after sanitize */
    tk_kbd_repeat_mode repeat_mode;
    int repeat_delay_ms;    /* first repeat after this long (def 400) */
    int repeat_interval_ms; /* then every this long (def 50) */
    int longpress_ms;       /* long-press notification (def 600, 0=off) */
} tk_kbd_config;

void tk_kbd_config_defaults(tk_kbd_config *cfg);
/* Clamp a config into the supported ranges (called by set/load). */
void tk_kbd_config_sanitize(tk_kbd_config *cfg);

typedef struct tk_kbd tk_kbd;

tk_kbd *tk_kbd_new(const tk_kbd_config *cfg); /* NULL cfg = defaults */
void tk_kbd_free(tk_kbd *k);
void tk_kbd_set_config(tk_kbd *k, const tk_kbd_config *cfg);
void tk_kbd_config_of(const tk_kbd *k, tk_kbd_config *out);

/* Feed one raw evdev key event: code is a KEY_* code, value is
 * 0 (release), 1 (press) or 2 (kernel autorepeat). Returns true and
 * fills *out when a tk_event should be dispatched. Modifier presses
 * and action-key releases produce no event. */
bool tk_kbd_feed(tk_kbd *k, int code, int value, long now_ms,
                 tk_event *out);
/* Produce due synthetic-repeat / long-press events. Call once per
 * main-loop iteration (and after every feed); returns true while an
 * event was produced - call again until it returns false. Always
 * returns false in KERNEL repeat mode except for long-press. */
bool tk_kbd_poll(tk_kbd *k, long now_ms, tk_event *out);

/* Current modifier mask (TK_MOD_*) and Caps Lock state. */
uint32_t tk_kbd_modifiers(const tk_kbd *k);
bool tk_kbd_caps(const tk_kbd *k);
/* Number of keys currently held (diagnostics/tests). */
int tk_kbd_held_count(const tk_kbd *k);

/* --- key definition table introspection (settings UI, docs) --- */
typedef enum {
    TK_KBD_KIND_TEXT = 0,  /* printable: carries UTF-8 text */
    TK_KBD_KIND_EDIT,      /* editing/navigation tk_key */
    TK_KBD_KIND_MODIFIER,  /* shift/ctrl/alt/caps: state only */
    TK_KBD_KIND_ACTION,    /* shell action (TK_EV_ACTION) */
    TK_KBD_KIND_IGNORE     /* known code, deliberately unmapped */
} tk_kbd_kind;

typedef struct {
    int code;
    const char *name;      /* "KEY_A", "KEY_ESC (Brain: home key)" ... */
    tk_kbd_kind kind;
    bool repeatable;
} tk_kbd_key_info;

size_t tk_kbd_table_count(void);
const tk_kbd_key_info *tk_kbd_table_at(size_t i);
bool tk_kbd_describe(int code, tk_kbd_key_info *out);

#endif /* TK_KBD_H */
