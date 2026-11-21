/* test_kbd - physical keyboard layer (tk_kbd) + virtual evdev
 * backend tests. All input is synthetic evdev (code, value, time)
 * triples; the clock is caller-driven, so repeat/long-press timing
 * is asserted exactly.
 *
 * The expectations encode the REAL Brain keyboard as Brainux
 * defines it (keyboard-research.md + Brainux Wiki keymap; see
 * KEYBOARD-FIX.md): borrowed evdev codes at face value, the
 * symbol layer's US glyph levels, and no JIS extra keys.
 *
 * Groups:
 *   T  key definition table introspection
 *   F  face value: borrowed codes mean what the codes say
 *   S  symbol layer glyphs (digits + nominal punctuation codes)
 *   L  letters + Caps Lock (external keyboards)
 *   M  modifiers (shift / the Ctrl and Alt keys)
 *   E  editing keys
 *   A  action keys (power, F keys)
 *   MD media keys: symbol-layer volume / mute / brightness actions
 *   R  repeat: synthetic generation, kernel passthrough, no doubling
 *   P  long press
 *   V  virtual evdev backend (production path, synthetic input)
 */
#include "tk.h"
#include "tk_kbd.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_str(const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) {
        passes++; printf("PASS %s -> %s\n", what, got);
    } else {
        fails++;
        printf("FAIL %s: got %s, want %s\n", what,
               got ? got : "(null)", want ? want : "(null)");
    }
}
static void check_int(const char *what, long got, long want)
{
    if (got == want) { passes++; printf("PASS %s -> %ld\n", what, got); }
    else { fails++; printf("FAIL %s: got %ld, want %ld\n", what, got, want); }
}

/* press+release a code, returning the press event */
static tk_event press(tk_kbd *k, int code, long t)
{
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    tk_kbd_feed(k, code, 1, t, &ev);
    return ev;
}
static void release(tk_kbd *k, int code, long t)
{
    tk_event ev;
    tk_kbd_feed(k, code, 0, t, &ev);
}

static void group_table(void)
{
    check_true("T table sizeable", tk_kbd_table_count() > 90);
    tk_kbd_key_info info;
    /* borrowed codes carry their face-value kinds */
    check_true("T describe esc", tk_kbd_describe(KEY_ESC, &info));
    check_int("T esc is edit", info.kind, TK_KBD_KIND_EDIT);
    check_true("T describe delete", tk_kbd_describe(KEY_DELETE, &info));
    check_int("T delete is edit", info.kind, TK_KBD_KIND_EDIT);
    check_true("T describe insert", tk_kbd_describe(KEY_INSERT, &info));
    check_int("T insert is edit", info.kind, TK_KBD_KIND_EDIT);
    check_true("T describe leftctrl", tk_kbd_describe(KEY_LEFTCTRL, &info));
    check_int("T leftctrl is modifier", info.kind, TK_KBD_KIND_MODIFIER);
    check_true("T describe leftalt", tk_kbd_describe(KEY_LEFTALT, &info));
    check_int("T leftalt is modifier", info.kind, TK_KBD_KIND_MODIFIER);
    check_true("T describe grave", tk_kbd_describe(KEY_GRAVE, &info));
    check_int("T grave is text", info.kind, TK_KBD_KIND_TEXT);
    check_true("T describe power", tk_kbd_describe(KEY_POWER, &info));
    check_int("T power is action", info.kind, TK_KBD_KIND_ACTION);
    /* the back key and the page-down key have DTS assignments
     * (KEY_BACK / KEY_SCROLLDOWN) and are edit keys, not drops */
    check_true("T describe back", tk_kbd_describe(KEY_BACK, &info));
    check_int("T back is edit", info.kind, TK_KBD_KIND_EDIT);
    check_true("T describe scrolldown",
               tk_kbd_describe(KEY_SCROLLDOWN, &info));
    check_int("T scrolldown is edit", info.kind, TK_KBD_KIND_EDIT);
    /* keys the hardware does not have are documented as absent */
    check_true("T describe yen", tk_kbd_describe(KEY_YEN, &info));
    check_int("T yen ignored (no such key)", info.kind, TK_KBD_KIND_IGNORE);
    check_true("T describe henkan", tk_kbd_describe(KEY_HENKAN, &info));
    check_int("T henkan ignored", info.kind, TK_KBD_KIND_IGNORE);
    /* the media keys exist on the DTS symbol layer and are
     * actions, not drops (see group MD) */
    check_true("T describe volumeup", tk_kbd_describe(KEY_VOLUMEUP, &info));
    check_int("T volumeup is action", info.kind, TK_KBD_KIND_ACTION);
    check_true("T describe volumedown",
               tk_kbd_describe(KEY_VOLUMEDOWN, &info));
    check_int("T volumedown is action", info.kind, TK_KBD_KIND_ACTION);
    check_true("T describe mute", tk_kbd_describe(KEY_MUTE, &info));
    check_int("T mute is action", info.kind, TK_KBD_KIND_ACTION);
    check_true("T describe brightnessup",
               tk_kbd_describe(KEY_BRIGHTNESSUP, &info));
    check_int("T brightnessup is action", info.kind, TK_KBD_KIND_ACTION);
    check_true("T describe brightnessdown",
               tk_kbd_describe(KEY_BRIGHTNESSDOWN, &info));
    check_int("T brightnessdown is action", info.kind,
              TK_KBD_KIND_ACTION);
    check_true("T describe unknown fails", !tk_kbd_describe(9999, &info));
    /* every table entry has a name */
    int named = 1;
    for (size_t i = 0; i < tk_kbd_table_count(); i++) {
        const tk_kbd_key_info *e = tk_kbd_table_at(i);
        if (!e || !e->name || !e->name[0]) named = 0;
    }
    check_true("T all named", named);
    /* the layout field is fixed: legacy values coerce to BRAIN */
    tk_kbd_config cfg;
    tk_kbd_config_defaults(&cfg);
    check_int("T default layout brain", cfg.layout, TK_KBD_LAYOUT_BRAIN);
    cfg.layout = TK_KBD_LAYOUT_US;
    tk_kbd_config_sanitize(&cfg);
    check_int("T legacy us coerced", cfg.layout, TK_KBD_LAYOUT_BRAIN);
}

static void group_face_value(void)
{
    tk_kbd *k = tk_kbd_new(NULL);
    tk_event ev;
    /* the home key is Escape (DTS: KEY_ESC; Wiki keymap: Esc) */
    ev = press(k, KEY_ESC, 0);
    check_int("F esc type", ev.type, TK_EV_KEY_DOWN);
    check_int("F esc is escape", ev.key, TK_KEY_ESCAPE);
    release(k, KEY_ESC, 1);
    /* the kokugo/kanji key is Tab, not a dictionary action */
    ev = press(k, KEY_TAB, 2);
    check_int("F tab", ev.key, TK_KEY_TAB);
    release(k, KEY_TAB, 3);
    /* eiwa/waei and My-dictionary are PageUp / PageDown */
    ev = press(k, KEY_PAGEUP, 4);
    check_int("F pageup", ev.key, TK_KEY_PAGE_UP);
    release(k, KEY_PAGEUP, 5);
    ev = press(k, KEY_PAGEDOWN, 6);
    check_int("F pagedown", ev.key, TK_KEY_PAGE_DOWN);
    release(k, KEY_PAGEDOWN, 7);
    /* the list key is Delete (forward delete), per the Wiki
     * keymap's "Del" - it must NOT become a shell action */
    ev = press(k, KEY_DELETE, 8);
    check_int("F delete type", ev.type, TK_EV_KEY_DOWN);
    check_int("F delete is delete key", ev.key, TK_KEY_DELETE);
    release(k, KEY_DELETE, 9);
    /* the history key is Insert - it must NOT be dropped */
    ev = press(k, KEY_INSERT, 10);
    check_int("F insert type", ev.type, TK_EV_KEY_DOWN);
    check_int("F insert is insert key", ev.key, TK_KEY_INSERT);
    release(k, KEY_INSERT, 11);
    /* the physical page-down key (DTS: KEY_SCROLLDOWN) is also
     * Page Down at face value, on its own evdev code so it is
     * distinguishable from the My-dictionary key above */
    ev = press(k, KEY_SCROLLDOWN, 12);
    check_int("F scrolldown type", ev.type, TK_EV_KEY_DOWN);
    check_int("F scrolldown is pagedown", ev.key, TK_KEY_PAGE_DOWN);
    release(k, KEY_SCROLLDOWN, 13);
    /* the back key (DTS: KEY_BACK) is a navigation key event */
    ev = press(k, KEY_BACK, 14);
    check_int("F back type", ev.type, TK_EV_KEY_DOWN);
    check_int("F back is back", ev.key, TK_KEY_BACK);
    release(k, KEY_BACK, 15);
    tk_kbd_free(k);
}

static void group_symbol(void)
{
    tk_kbd *k = tk_kbd_new(NULL);
    tk_event ev;
    /* digits arrive only via the symbol layer, as plain digits */
    ev = press(k, KEY_1, 0);
    check_str("S digit 1", ev.text, "1");
    release(k, KEY_1, 1);
    ev = press(k, KEY_0, 2);
    check_str("S digit 0", ev.text, "0");
    release(k, KEY_0, 3);
    /* symbol+Shift levels are the US glyphs (Wiki keymap):
     * symbol+Shift+W (KEY_2) is '@' - NOT the PC-JIS '"' */
    tk_kbd_feed(k, KEY_LEFTSHIFT, 1, 4, &ev);
    ev = press(k, KEY_2, 5);
    check_str("S shift+2 is @", ev.text, "@");
    release(k, KEY_2, 6);
    ev = press(k, KEY_1, 7);
    check_str("S shift+1 is !", ev.text, "!");
    release(k, KEY_1, 8);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 0, 9, &ev);
    /* nominal punctuation codes: US face value, both levels */
    ev = press(k, KEY_SEMICOLON, 10);
    check_str("S semicolon", ev.text, ";");
    release(k, KEY_SEMICOLON, 11);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 1, 12, &ev);
    ev = press(k, KEY_SEMICOLON, 13);
    check_str("S shift+semicolon is colon", ev.text, ":");
    release(k, KEY_SEMICOLON, 14);
    ev = press(k, KEY_LEFTBRACE, 15);
    check_str("S shift+leftbrace", ev.text, "{");
    release(k, KEY_LEFTBRACE, 16);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 0, 17, &ev);
    ev = press(k, KEY_LEFTBRACE, 18);
    check_str("S leftbrace", ev.text, "[");
    release(k, KEY_LEFTBRACE, 19);
    ev = press(k, KEY_APOSTROPHE, 20);
    check_str("S apostrophe", ev.text, "'");
    release(k, KEY_APOSTROPHE, 21);
    ev = press(k, KEY_SLASH, 22);
    check_str("S slash", ev.text, "/");
    release(k, KEY_SLASH, 23);
    ev = press(k, KEY_COMMA, 24);
    check_str("S comma", ev.text, ",");
    release(k, KEY_COMMA, 25);
    ev = press(k, KEY_DOT, 26);
    check_str("S dot", ev.text, ".");
    release(k, KEY_DOT, 27);
    /* KEY_GRAVE (symbol+D) is a plain glyph - critically NOT
     * an IME toggle action (no zenkaku/hankaku key exists) */
    check_true("S grave feeds", tk_kbd_feed(k, KEY_GRAVE, 1, 28, &ev));
    check_int("S grave is key not action", ev.type, TK_EV_KEY_DOWN);
    check_str("S grave glyph", ev.text, "`");
    release(k, KEY_GRAVE, 29);
    /* the long-vowel key has the US face value - / _ */
    ev = press(k, KEY_MINUS, 30);
    check_str("S minus key", ev.text, "-");
    release(k, KEY_MINUS, 31);
    tk_kbd_free(k);
}

static void group_letters(void)
{
    tk_kbd *k = tk_kbd_new(NULL);
    tk_event ev;
    ev = press(k, KEY_A, 0);
    check_str("L a", ev.text, "a");
    release(k, KEY_A, 1);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 1, 2, &ev);
    ev = press(k, KEY_A, 3);
    check_str("L shift+a", ev.text, "A");
    release(k, KEY_A, 4);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 0, 5, &ev);
    /* Caps Lock (external keyboards only): XORs with Shift */
    tk_kbd_feed(k, KEY_CAPSLOCK, 1, 6, &ev);
    tk_kbd_feed(k, KEY_CAPSLOCK, 0, 7, &ev);
    check_true("L caps on", tk_kbd_caps(k));
    ev = press(k, KEY_A, 8);
    check_str("L caps a", ev.text, "A");
    release(k, KEY_A, 9);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 1, 10, &ev);
    ev = press(k, KEY_A, 11);
    check_str("L caps+shift a", ev.text, "a");
    release(k, KEY_A, 12);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 0, 13, &ev);
    ev = press(k, KEY_1, 14);
    check_str("L caps does not shift digits", ev.text, "1");
    release(k, KEY_1, 15);
    tk_kbd_free(k);
}

static void group_modifiers(void)
{
    tk_kbd *k = tk_kbd_new(NULL);
    tk_event ev;
    check_true("M shift press silent",
               !tk_kbd_feed(k, KEY_LEFTSHIFT, 1, 0, &ev));
    check_int("M shift tracked", (long)tk_kbd_modifiers(k), TK_MOD_SHIFT);
    /* the page-up key IS Ctrl (Wiki, Sx5-Sx7): silent, tracked */
    check_true("M ctrl press silent",
               !tk_kbd_feed(k, KEY_LEFTCTRL, 1, 1, &ev));
    check_int("M ctrl tracked", (long)tk_kbd_modifiers(k),
              TK_MOD_SHIFT | TK_MOD_CTRL);
    ev = press(k, KEY_C, 2);
    check_int("M event mods", (long)ev.modifiers,
              TK_MOD_SHIFT | TK_MOD_CTRL);
    check_str("M ctrl+shift+c text", ev.text, "C");
    release(k, KEY_C, 3);
    tk_kbd_feed(k, KEY_LEFTCTRL, 0, 4, &ev);
    /* the audio key IS Alt (Wiki, Sx5-Sx7) */
    check_true("M alt press silent",
               !tk_kbd_feed(k, KEY_LEFTALT, 1, 5, &ev));
    check_int("M alt tracked", (long)tk_kbd_modifiers(k),
              TK_MOD_SHIFT | TK_MOD_ALT);
    tk_kbd_feed(k, KEY_LEFTALT, 0, 6, &ev);
    tk_kbd_feed(k, KEY_LEFTSHIFT, 0, 7, &ev);
    check_int("M mods cleared", (long)tk_kbd_modifiers(k), 0);
    tk_kbd_free(k);
}

static void group_edit(void)
{
    tk_kbd *k = tk_kbd_new(NULL);
    tk_event ev;
    ev = press(k, KEY_ENTER, 0);
    check_int("E enter", ev.key, TK_KEY_ENTER);
    release(k, KEY_ENTER, 1);
    ev = press(k, KEY_SPACE, 2);
    check_int("E space", ev.key, TK_KEY_SPACE);
    check_true("E space no text", ev.text == NULL);
    release(k, KEY_SPACE, 3);
    ev = press(k, KEY_BACKSPACE, 4);
    check_int("E backspace", ev.key, TK_KEY_BACKSPACE);
    release(k, KEY_BACKSPACE, 5);
    ev = press(k, KEY_UP, 6);
    check_int("E up", ev.key, TK_KEY_UP);
    release(k, KEY_UP, 7);
    /* KEY_UP is produced on release */
    tk_kbd_feed(k, KEY_LEFT, 1, 8, &ev);
    check_true("E release feeds", tk_kbd_feed(k, KEY_LEFT, 0, 9, &ev));
    check_int("E release type", ev.type, TK_EV_KEY_UP);
    check_int("E release key", ev.key, TK_KEY_LEFT);
    tk_kbd_free(k);
}

static void group_actions(void)
{
    tk_kbd *k = tk_kbd_new(NULL);
    tk_event ev;
    /* power: press-only action (Wiki: PC power button behaviour) */
    check_true("A power feeds", tk_kbd_feed(k, KEY_POWER, 1, 0, &ev));
    check_int("A power type", ev.type, TK_EV_ACTION);
    check_int("A power action", ev.action, TK_ACTION_POWER);
    check_true("A power release silent",
               !tk_kbd_feed(k, KEY_POWER, 0, 1, &ev));
    ev = press(k, KEY_F3, 2);
    check_int("A f3", ev.action, TK_ACTION_FKEY_BASE + 3);
    release(k, KEY_F3, 3);
    /* codes for keys this hardware lacks produce nothing */
    check_true("A unknown silent", !tk_kbd_feed(k, 9999, 1, 4, &ev));
    check_true("A yen silent", !tk_kbd_feed(k, KEY_YEN, 1, 5, &ev));
    check_true("A henkan silent", !tk_kbd_feed(k, KEY_HENKAN, 1, 6, &ev));
    tk_kbd_free(k);
}

static void group_media(void)
{
    tk_kbd_config cfg;
    tk_kbd_config_defaults(&cfg);
    cfg.longpress_ms = 0;  /* isolate: no long-press events here */
    tk_kbd *k = tk_kbd_new(&cfg);
    tk_event ev;
    /* The DTS symbol layer (imx28-pwsh6.dts keymap-symbol)
     * assigns media controls to symbol+arrows and symbol+Space;
     * the driver emits these codes only while the symbol key is
     * held. Each is a press-only TK_EV_ACTION on the matching
     * TK_ACTION_* receiver, like the power key. */
    check_true("MD volumeup feeds", tk_kbd_feed(k, KEY_VOLUMEUP, 1, 0, &ev));
    check_int("MD volumeup type", ev.type, TK_EV_ACTION);
    check_int("MD volumeup action", ev.action, TK_ACTION_VOLUME_UP);
    check_true("MD volumeup release silent",
               !tk_kbd_feed(k, KEY_VOLUMEUP, 0, 1, &ev));
    check_true("MD volumedown feeds",
               tk_kbd_feed(k, KEY_VOLUMEDOWN, 1, 2, &ev));
    check_int("MD volumedown type", ev.type, TK_EV_ACTION);
    check_int("MD volumedown action", ev.action, TK_ACTION_VOLUME_DOWN);
    check_true("MD volumedown release silent",
               !tk_kbd_feed(k, KEY_VOLUMEDOWN, 0, 3, &ev));
    check_true("MD mute feeds", tk_kbd_feed(k, KEY_MUTE, 1, 4, &ev));
    check_int("MD mute type", ev.type, TK_EV_ACTION);
    check_int("MD mute action", ev.action, TK_ACTION_MUTE);
    check_true("MD mute release silent",
               !tk_kbd_feed(k, KEY_MUTE, 0, 5, &ev));
    check_true("MD brightnessup feeds",
               tk_kbd_feed(k, KEY_BRIGHTNESSUP, 1, 6, &ev));
    check_int("MD brightnessup type", ev.type, TK_EV_ACTION);
    check_int("MD brightnessup action", ev.action,
              TK_ACTION_BRIGHTNESS_UP);
    check_true("MD brightnessup release silent",
               !tk_kbd_feed(k, KEY_BRIGHTNESSUP, 0, 7, &ev));
    check_true("MD brightnessdown feeds",
               tk_kbd_feed(k, KEY_BRIGHTNESSDOWN, 1, 8, &ev));
    check_int("MD brightnessdown type", ev.type, TK_EV_ACTION);
    check_int("MD brightnessdown action", ev.action,
              TK_ACTION_BRIGHTNESS_DOWN);
    check_true("MD brightnessdown release silent",
               !tk_kbd_feed(k, KEY_BRIGHTNESSDOWN, 0, 9, &ev));
    /* the base layer is untouched: the same physical keys emit
     * the plain arrow/Space codes there and keep their face
     * values - they must NOT turn into actions */
    ev = press(k, KEY_RIGHT, 10);
    check_int("MD base right type", ev.type, TK_EV_KEY_DOWN);
    check_int("MD base right is right", ev.key, TK_KEY_RIGHT);
    release(k, KEY_RIGHT, 11);
    ev = press(k, KEY_LEFT, 12);
    check_int("MD base left type", ev.type, TK_EV_KEY_DOWN);
    check_int("MD base left is left", ev.key, TK_KEY_LEFT);
    release(k, KEY_LEFT, 13);
    ev = press(k, KEY_UP, 14);
    check_int("MD base up type", ev.type, TK_EV_KEY_DOWN);
    check_int("MD base up is up", ev.key, TK_KEY_UP);
    release(k, KEY_UP, 15);
    ev = press(k, KEY_DOWN, 16);
    check_int("MD base down type", ev.type, TK_EV_KEY_DOWN);
    check_int("MD base down is down", ev.key, TK_KEY_DOWN);
    release(k, KEY_DOWN, 17);
    ev = press(k, KEY_SPACE, 18);
    check_int("MD base space type", ev.type, TK_EV_KEY_DOWN);
    check_int("MD base space is space", ev.key, TK_KEY_SPACE);
    release(k, KEY_SPACE, 19);
    /* actions do not repeat: a held media key produces nothing
     * from poll(), and kernel autorepeat is dropped as usual */
    check_true("MD held feeds", tk_kbd_feed(k, KEY_VOLUMEUP, 1, 1000, &ev));
    check_true("MD held no repeat", !tk_kbd_poll(k, 100000, &ev));
    check_true("MD kernel repeat dropped",
               !tk_kbd_feed(k, KEY_VOLUMEUP, 2, 100001, &ev));
    release(k, KEY_VOLUMEUP, 100002);
    tk_kbd_free(k);
}

static void group_repeat_synthetic(void)
{
    tk_kbd_config cfg;
    tk_kbd_config_defaults(&cfg);  /* delay 400, interval 50 */
    tk_kbd *k = tk_kbd_new(&cfg);
    tk_event ev;
    press(k, KEY_A, 1000);
    check_true("R nothing before delay", !tk_kbd_poll(k, 1399, &ev));
    check_true("R repeat at delay", tk_kbd_poll(k, 1400, &ev));
    check_true("R repeat flagged", ev.repeat && !ev.longpress);
    check_str("R repeat text", ev.text, "a");
    check_true("R nothing mid-interval", !tk_kbd_poll(k, 1449, &ev));
    check_true("R repeat at interval", tk_kbd_poll(k, 1450, &ev));
    /* kernel autorepeat is dropped in synthetic mode: no doubling */
    check_true("R kernel repeat dropped",
               !tk_kbd_feed(k, KEY_A, 2, 1460, &ev));
    check_true("R still synthetic only", tk_kbd_poll(k, 1500, &ev));
    check_true("R that was a repeat", ev.repeat);
    /* release stops everything */
    release(k, KEY_A, 1510);
    check_true("R silent after release", !tk_kbd_poll(k, 3000, &ev));
    /* duplicate press of a held key is ignored */
    press(k, KEY_B, 2000);
    check_int("R held count 1", tk_kbd_held_count(k), 1);
    check_true("R dup press ignored", !tk_kbd_feed(k, KEY_B, 1, 2001, &ev));
    release(k, KEY_B, 2002);
    /* repeat target falls back to the still-held earlier key */
    press(k, KEY_A, 3000);
    press(k, KEY_B, 3010);
    release(k, KEY_B, 3020);
    check_true("R fallback repeats a", tk_kbd_poll(k, 3400, &ev));
    check_str("R fallback text", ev.text, "a");
    release(k, KEY_A, 3410);
    tk_kbd_free(k);
}

static void group_repeat_kernel(void)
{
    tk_kbd_config cfg;
    tk_kbd_config_defaults(&cfg);
    cfg.repeat_mode = TK_KBD_REPEAT_KERNEL;
    cfg.longpress_ms = 0;
    tk_kbd *k = tk_kbd_new(&cfg);
    tk_event ev;
    press(k, KEY_A, 0);
    check_true("RK kernel repeat passes",
               tk_kbd_feed(k, KEY_A, 2, 100, &ev));
    check_true("RK flagged repeat", ev.repeat);
    check_str("RK repeat text", ev.text, "a");
    check_true("RK no synthetic", !tk_kbd_poll(k, 100000, &ev));
    release(k, KEY_A, 200);
    check_true("RK repeat after release dropped",
               !tk_kbd_feed(k, KEY_A, 2, 300, &ev));
    tk_kbd_free(k);
}

static void group_longpress(void)
{
    tk_kbd_config cfg;
    tk_kbd_config_defaults(&cfg);
    cfg.repeat_delay_ms = 2000;    /* max: keep repeats out of the way */
    cfg.longpress_ms = 600;
    tk_kbd *k = tk_kbd_new(&cfg);
    tk_event ev;
    press(k, KEY_A, 0);
    check_true("P nothing at 599", !tk_kbd_poll(k, 599, &ev));
    check_true("P fires at 600", tk_kbd_poll(k, 600, &ev));
    check_true("P flagged", ev.longpress && !ev.repeat);
    check_str("P text", ev.text, "a");
    check_true("P fires once", !tk_kbd_poll(k, 601, &ev));
    check_true("P still once later", !tk_kbd_poll(k, 1500, &ev));
    release(k, KEY_A, 1510);
    /* long press on an action key (power menu pattern) */
    press(k, KEY_POWER, 2000);
    check_true("P power nothing at 2599", !tk_kbd_poll(k, 2599, &ev));
    check_true("P power fires", tk_kbd_poll(k, 2600, &ev));
    check_int("P power type", ev.type, TK_EV_ACTION);
    check_int("P power action", ev.action, TK_ACTION_POWER);
    check_true("P power flagged", ev.longpress);
    release(k, KEY_POWER, 2610);
    /* disabled when longpress_ms = 0: repeats still occur, but no
     * event is ever flagged longpress */
    cfg.longpress_ms = 0;
    tk_kbd_set_config(k, &cfg);
    press(k, KEY_A, 7000);
    int saw_longpress = 0;
    for (long t = 7000; t <= 12000; t += 100)
        if (tk_kbd_poll(k, t, &ev) && ev.longpress) saw_longpress = 1;
    check_true("P disabled", !saw_longpress);
    release(k, KEY_A, 12010);
    tk_kbd_free(k);
}

static void group_virtual(void)
{
    tk_evdev *e = tk_evdev_new_virtual();
    check_true("V virtual created", e != NULL);
    tk_event ev;
    /* default config is the Brain keymap */
    check_true("V feed t", tk_evdev_feed_key(e, KEY_T, 1, 0));
    check_true("V queue full refuses",
               !tk_evdev_feed_key(e, KEY_E, 1, 0));
    check_true("V poll t", tk_evdev_poll_at(e, 0, &ev));
    check_int("V t type", ev.type, TK_EV_KEY_DOWN);
    check_str("V t text", ev.text, "t");
    check_true("V feed t up", tk_evdev_feed_key(e, KEY_T, 0, 10));
    check_true("V poll t up", tk_evdev_poll_at(e, 10, &ev));
    check_int("V t up type", ev.type, TK_EV_KEY_UP);
    /* a legacy layout value is accepted but coerced to BRAIN:
     * the built-in keyboard has exactly one keymap */
    tk_kbd_config cfg;
    tk_kbd_config_defaults(&cfg);
    cfg.layout = TK_KBD_LAYOUT_US;
    tk_evdev_kbd_set_config(e, &cfg);
    tk_kbd_config got;
    tk_evdev_kbd_config(e, &got);
    check_int("V layout coerced brain", got.layout, TK_KBD_LAYOUT_BRAIN);
    tk_evdev_feed_key(e, KEY_LEFTBRACE, 1, 20);
    check_true("V poll [", tk_evdev_poll_at(e, 20, &ev));
    check_str("V [", ev.text, "[");
    tk_evdev_feed_key(e, KEY_LEFTBRACE, 0, 21);
    tk_evdev_poll_at(e, 21, &ev);
    /* the home key (KEY_ESC) comes through as Escape */
    tk_evdev_feed_key(e, KEY_ESC, 1, 30);
    check_true("V poll esc", tk_evdev_poll_at(e, 30, &ev));
    check_int("V esc key", ev.key, TK_KEY_ESCAPE);
    tk_evdev_feed_key(e, KEY_ESC, 0, 31);
    tk_evdev_poll_at(e, 31, &ev);
    /* synthetic repeat through the backend under a driven clock */
    tk_evdev_feed_key(e, KEY_A, 1, 100);
    tk_evdev_poll_at(e, 100, &ev);
    check_true("V no repeat at 499", !tk_evdev_poll_at(e, 499, &ev));
    check_true("V repeat at 500", tk_evdev_poll_at(e, 500, &ev));
    check_true("V repeat flag", ev.repeat);
    tk_evdev_feed_key(e, KEY_A, 0, 510);
    tk_evdev_poll_at(e, 510, &ev);
    check_true("V silent after up", !tk_evdev_poll_at(e, 5000, &ev));
    tk_evdev_close(e);
}

int main(void)
{
    group_table();
    group_face_value();
    group_symbol();
    group_letters();
    group_modifiers();
    group_edit();
    group_actions();
    group_media();
    group_repeat_synthetic();
    group_repeat_kernel();
    group_longpress();
    group_virtual();
    printf("test_kbd: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
