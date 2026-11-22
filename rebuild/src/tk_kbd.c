/* tk_kbd.c - physical keyboard layer. See tk_kbd.h for the design
 * and the three-source cross-check behind this table
 * (keyboard-research.md, KEYBOARD-FIX.md).
 *
 * The key definition table below is the single source of truth
 * for "what does this evdev code mean". Entries:
 *
 *   { code, name, kind, key, action(+arg), mod, lo, hi, repeatable }
 *
 * For TEXT entries lo/hi are the produced UTF-8 text for the
 * unshifted/shifted levels, following the Brainux Wiki keymap
 * (US face value). Letters ignore the table's case split at
 * runtime: Caps Lock XORs with Shift for a-z (external keyboards;
 * the built-in keyboard has no Caps key).
 *
 * Brain notes (PW-SH6 / PW-AJ2):
 *   - 48 physical keys, no digit row, no JIS extra keys. The
 *     driver is a GPIO matrix (linux-brain DTS); the symbol key
 *     is a driver-internal modifier that emits no event.
 *   - Borrowed codes are interpreted at face value (Brainux
 *     Wiki): the home key is Escape, the kokugo/kanji key is
 *     Tab, eiwa/waei is PageUp, My-dictionary is PageDown,
 *     history is Insert, list is Delete, the page-up key is
 *     Ctrl, the audio key is Alt. Their WinCE dictionary
 *     functions have no distinct signal and are not mapped.
 *   - The back key emits KEY_BACK (DTS assignment from commit
 *     b0ac9f045) and the page-down key emits KEY_SCROLLDOWN
 *     (assigned in the OS-internals phase; its contact was the
 *     last commented-out one in the DTS - see KEYBOARD-FIX.md
 *     section 5 and REPORT-os-internals-1.md).
 *   - The DTS symbol layer assigns media controls to the
 *     symbol-layer arrows and Space (also commit b0ac9f045):
 *     symbol+Left/Right emit KEY_VOLUMEDOWN / KEY_VOLUMEUP,
 *     symbol+Up/Down emit KEY_BRIGHTNESSUP / KEY_BRIGHTNESSDOWN
 *     and symbol+Space emits KEY_MUTE. They are mapped to the
 *     TK_ACTION_* receivers in the actions block below. The
 *     base layer never emits these codes: the same physical
 *     keys are the plain arrows and Space there.
 *   - Ghost-key suppression lives in the linux-brain driver and
 *     is out of scope here; this layer sees clean press/release
 *     pairs.
 */
#include "tk_kbd.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int code;
    const char *name;
    tk_kbd_kind kind;
    tk_key key;          /* EDIT */
    int action;          /* ACTION: tk_action (+ action_arg) */
    int action_arg;
    uint32_t mod;        /* MODIFIER: TK_MOD_* bit (0 for Caps Lock) */
    const char *lo, *hi; /* TEXT: unshifted / shifted UTF-8 */
    bool repeatable;
} kbd_entry;

#define T(c, n, lo, hi) \
    { c, n, TK_KBD_KIND_TEXT, TK_KEY_NONE, 0, 0, 0, lo, hi, true }
#define E(c, n, k) \
    { c, n, TK_KBD_KIND_EDIT, k, 0, 0, 0, NULL, NULL, true }
#define M(c, n, m) \
    { c, n, TK_KBD_KIND_MODIFIER, TK_KEY_NONE, 0, 0, m, NULL, NULL, false }
#define A(c, n, a) \
    { c, n, TK_KBD_KIND_ACTION, TK_KEY_NONE, a, 0, 0, NULL, NULL, false }
#define AF(c, n, fn) \
    { c, n, TK_KBD_KIND_ACTION, TK_KEY_NONE, TK_ACTION_FKEY_BASE, fn, 0, NULL, NULL, false }
#define I(c, n) \
    { c, n, TK_KBD_KIND_IGNORE, TK_KEY_NONE, 0, 0, 0, NULL, NULL, false }

static const kbd_entry KEY_TABLE[] = {
    /* --- modifiers --- */
    M(KEY_LEFTSHIFT,   "KEY_LEFTSHIFT (Brain: shift key)", TK_MOD_SHIFT),
    M(KEY_RIGHTSHIFT,  "KEY_RIGHTSHIFT (external keyboard)", TK_MOD_SHIFT),
    M(KEY_LEFTCTRL,    "KEY_LEFTCTRL (Brain: page-up key = Ctrl)", TK_MOD_CTRL),
    M(KEY_RIGHTCTRL,   "KEY_RIGHTCTRL (external keyboard)", TK_MOD_CTRL),
    M(KEY_LEFTALT,     "KEY_LEFTALT (Brain: audio key = Alt)", TK_MOD_ALT),
    M(KEY_RIGHTALT,    "KEY_RIGHTALT (external keyboard)", TK_MOD_ALT),
    M(KEY_CAPSLOCK,    "KEY_CAPSLOCK (external keyboard only)", 0),
    I(KEY_LEFTMETA,    "KEY_LEFTMETA"),
    I(KEY_RIGHTMETA,   "KEY_RIGHTMETA"),

    /* --- editing / navigation ---
     * The Brain dedicated keys arrive on the borrowed codes in
     * the first block and mean exactly what the codes say
     * (Brainux Wiki keymap). */
    E(KEY_ESC,       "KEY_ESC (Brain: home key = Escape)", TK_KEY_ESCAPE),
    E(KEY_TAB,       "KEY_TAB (Brain: kokugo/kanji key = Tab)", TK_KEY_TAB),
    E(KEY_PAGEUP,    "KEY_PAGEUP (Brain: eiwa/waei key)", TK_KEY_PAGE_UP),
    E(KEY_PAGEDOWN,  "KEY_PAGEDOWN (Brain: My-dictionary key)", TK_KEY_PAGE_DOWN),
    /* The physical page-down key has its own code so it stays
     * distinguishable from the My-dictionary key above; its
     * face value is still Page Down. KEY_SCROLLUP has no Brain
     * key and is mapped for external keyboards. */
    E(KEY_SCROLLDOWN, "KEY_SCROLLDOWN (Brain: page-down key)", TK_KEY_PAGE_DOWN),
    E(KEY_SCROLLUP,   "KEY_SCROLLUP", TK_KEY_PAGE_UP),
    E(KEY_BACK,      "KEY_BACK (Brain: back key)", TK_KEY_BACK),
    E(KEY_INSERT,    "KEY_INSERT (Brain: history key = Insert)", TK_KEY_INSERT),
    E(KEY_DELETE,    "KEY_DELETE (Brain: list key = Delete)", TK_KEY_DELETE),
    E(KEY_ENTER,     "KEY_ENTER (Brain: decide key)", TK_KEY_ENTER),
    E(KEY_KPENTER,   "KEY_KPENTER (external keyboard)", TK_KEY_ENTER),
    E(KEY_BACKSPACE, "KEY_BACKSPACE (Brain: delete key)", TK_KEY_BACKSPACE),
    E(KEY_LEFT,      "KEY_LEFT", TK_KEY_LEFT),
    E(KEY_RIGHT,     "KEY_RIGHT", TK_KEY_RIGHT),
    E(KEY_UP,        "KEY_UP", TK_KEY_UP),
    E(KEY_DOWN,      "KEY_DOWN", TK_KEY_DOWN),
    E(KEY_SPACE,     "KEY_SPACE (Brain: space/convert key)", TK_KEY_SPACE),
    /* KEY_HOME also comes from the built-in keyboard's
     * unmarked (0,6) contact (DTS assignment; the contact's
     * identity is unconfirmed, keyboard-research U3). KEY_END
     * never comes from the built-in keyboard. */
    E(KEY_HOME,      "KEY_HOME (Brain: unmarked (0,6) contact; also external)", TK_KEY_HOME),
    E(KEY_END,       "KEY_END (external keyboard only)", TK_KEY_END),

    /* --- digits ---
     * No digit row exists. These codes arrive only from the
     * driver's symbol layer (symbol+Q..P) and carry the Wiki
     * keymap's US levels: plain = digit, with Shift (symbol+
     * Shift+Q..P) = the US shifted glyph. */
    T(KEY_1, "KEY_1 (symbol+Q)", "1", "!"),
    T(KEY_2, "KEY_2 (symbol+W)", "2", "@"),
    T(KEY_3, "KEY_3 (symbol+E)", "3", "#"),
    T(KEY_4, "KEY_4 (symbol+R)", "4", "$"),
    T(KEY_5, "KEY_5 (symbol+T)", "5", "%"),
    T(KEY_6, "KEY_6 (symbol+Y)", "6", "^"),
    T(KEY_7, "KEY_7 (symbol+U)", "7", "&"),
    T(KEY_8, "KEY_8 (symbol+I)", "8", "*"),
    T(KEY_9, "KEY_9 (symbol+O)", "9", "("),
    T(KEY_0, "KEY_0 (symbol+P)", "0", ")"),

    /* --- letters --- */
    T(KEY_Q, "KEY_Q", "q", "Q"), T(KEY_W, "KEY_W", "w", "W"),
    T(KEY_E, "KEY_E", "e", "E"), T(KEY_R, "KEY_R", "r", "R"),
    T(KEY_T, "KEY_T", "t", "T"), T(KEY_Y, "KEY_Y", "y", "Y"),
    T(KEY_U, "KEY_U", "u", "U"), T(KEY_I, "KEY_I", "i", "I"),
    T(KEY_O, "KEY_O", "o", "O"), T(KEY_P, "KEY_P", "p", "P"),
    T(KEY_A, "KEY_A", "a", "A"), T(KEY_S, "KEY_S", "s", "S"),
    T(KEY_D, "KEY_D", "d", "D"), T(KEY_F, "KEY_F", "f", "F"),
    T(KEY_G, "KEY_G", "g", "G"), T(KEY_H, "KEY_H", "h", "H"),
    T(KEY_J, "KEY_J", "j", "J"), T(KEY_K, "KEY_K", "k", "K"),
    T(KEY_L, "KEY_L", "l", "L"),
    T(KEY_Z, "KEY_Z", "z", "Z"), T(KEY_X, "KEY_X", "x", "X"),
    T(KEY_C, "KEY_C", "c", "C"), T(KEY_V, "KEY_V", "v", "V"),
    T(KEY_B, "KEY_B", "b", "B"), T(KEY_N, "KEY_N", "n", "N"),
    T(KEY_M, "KEY_M", "m", "M"),

    /* --- the long-vowel key and the symbol-layer punctuation ---
     * KEY_MINUS is the physical "ー" key; the Brainux keymap
     * gives it the US face value - / _. The remaining codes
     * arrive only from the symbol layer (symbol+D/F/G/H/J/K/L/
     * N/M/minus) as the DTS's nominal codes, and the Wiki
     * keymap assigns them the US glyphs below (shift level =
     * symbol+Shift+key). None of them is a mode key: in
     * particular KEY_GRAVE is a plain glyph here, NOT an IME
     * toggle (there is no zenkaku/hankaku key on this hardware). */
    T(KEY_MINUS,      "KEY_MINUS (Brain: long-vowel key)", "-", "_"),
    T(KEY_GRAVE,      "KEY_GRAVE (symbol+D)", "`", "~"),
    T(KEY_EQUAL,      "KEY_EQUAL (symbol+F)", "=", "+"),
    T(KEY_BACKSLASH,  "KEY_BACKSLASH (symbol+G)", "\\", "|"),
    T(KEY_SEMICOLON,  "KEY_SEMICOLON (symbol+H)", ";", ":"),
    T(KEY_APOSTROPHE, "KEY_APOSTROPHE (symbol+J)", "'", "\""),
    T(KEY_LEFTBRACE,  "KEY_LEFTBRACE (symbol+K)", "[", "{"),
    T(KEY_RIGHTBRACE, "KEY_RIGHTBRACE (symbol+L)", "]", "}"),
    T(KEY_COMMA,      "KEY_COMMA (symbol+N)", ",", "<"),
    T(KEY_DOT,        "KEY_DOT (symbol+M)", ".", ">"),
    T(KEY_SLASH,      "KEY_SLASH (symbol+long-vowel key)", "/", "?"),

    /* --- actions ---
     * The power button is outside the key matrix; the Brainux
     * Wiki states it behaves like a PC power button, so a
     * KEY_POWER from its driver is the shell power action. */
    A(KEY_POWER,  "KEY_POWER (Brain: power button)", TK_ACTION_POWER),
    /* Media controls: emitted only from the DTS symbol layer
     * (symbol+arrows / symbol+Space, see the header note), so
     * they can never collide with the base layer's arrow/Space
     * face values. Like all ACTION entries they are press-only
     * and do not repeat; a shell that wants hold-to-adjust can
     * watch the long-press flag on the action event. */
    A(KEY_VOLUMEUP,       "KEY_VOLUMEUP (Brain: symbol+Right key)", TK_ACTION_VOLUME_UP),
    A(KEY_VOLUMEDOWN,     "KEY_VOLUMEDOWN (Brain: symbol+Left key)", TK_ACTION_VOLUME_DOWN),
    A(KEY_MUTE,           "KEY_MUTE (Brain: symbol+Space key)", TK_ACTION_MUTE),
    A(KEY_BRIGHTNESSUP,   "KEY_BRIGHTNESSUP (Brain: symbol+Up key)", TK_ACTION_BRIGHTNESS_UP),
    A(KEY_BRIGHTNESSDOWN, "KEY_BRIGHTNESSDOWN (Brain: symbol+Down key)", TK_ACTION_BRIGHTNESS_DOWN),

    /* --- F keys (no F-row on the Brain; external keyboards) --- */
    AF(KEY_F1, "KEY_F1", 1), AF(KEY_F2, "KEY_F2", 2),
    AF(KEY_F3, "KEY_F3", 3), AF(KEY_F4, "KEY_F4", 4),
    AF(KEY_F5, "KEY_F5", 5), AF(KEY_F6, "KEY_F6", 6),
    AF(KEY_F7, "KEY_F7", 7), AF(KEY_F8, "KEY_F8", 8),
    AF(KEY_F9, "KEY_F9", 9), AF(KEY_F10, "KEY_F10", 10),
    AF(KEY_F11, "KEY_F11", 11), AF(KEY_F12, "KEY_F12", 12),

    /* --- codes that name keys this hardware does not have ---
     * Listed so the table documents their absence (see
     * KEYBOARD-FIX.md D2/D3/D5). The JIS extra keys in
     * particular are the on-screen keyboard's territory: the
     * physical keyboard cannot produce yen, ro, or the IME
     * mode keys at all. */
    I(KEY_YEN,              "KEY_YEN (no yen key on the Brain keyboard)"),
    I(KEY_RO,               "KEY_RO (no ro key on the Brain keyboard)"),
    I(KEY_HENKAN,           "KEY_HENKAN (no henkan key; conversion is the space/convert key)"),
    I(KEY_MUHENKAN,         "KEY_MUHENKAN (no muhenkan key)"),
    I(KEY_ZENKAKUHANKAKU,   "KEY_ZENKAKUHANKAKU (no zenkaku/hankaku key)"),
    I(KEY_KATAKANAHIRAGANA, "KEY_KATAKANAHIRAGANA (no kana key)"),
    I(KEY_SEARCH,           "KEY_SEARCH (no search key; not emitted)"),
    I(KEY_HOMEPAGE,         "KEY_HOMEPAGE (not emitted; the home key is KEY_ESC)"),
    I(KEY_SOUND,            "KEY_SOUND (not emitted; the audio key is KEY_LEFTALT)"),
    I(KEY_MENU,             "KEY_MENU (not in the Brainux keymap)"),
    I(KEY_FORWARD,          "KEY_FORWARD (not emitted)"),
    I(KEY_SLEEP,            "KEY_SLEEP (not emitted)"),
    I(KEY_COMPOSE,          "KEY_COMPOSE"),
    I(KEY_SYSRQ,            "KEY_SYSRQ"),
    I(KEY_PRINT,            "KEY_PRINT"),
    I(KEY_PAUSE,            "KEY_PAUSE"),
    I(KEY_NUMLOCK,          "KEY_NUMLOCK"),
    I(KEY_SCROLLLOCK,       "KEY_SCROLLLOCK"),
};
#undef T
#undef E
#undef M
#undef A
#undef AF
#undef I

#define TABLE_N (sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]))

static const kbd_entry *find_entry(int code)
{
    for (size_t i = 0; i < TABLE_N; i++)
        if (KEY_TABLE[i].code == code) return &KEY_TABLE[i];
    return NULL;
}

/* ------------------------------------------------------------------ */

#define KBD_MAX_HELD 8

typedef struct {
    int code;
    const kbd_entry *e;
    long press_ms;
    long next_repeat_ms;  /* synthetic: when the next repeat is due */
    bool lp_fired;
    char text[8];         /* press-time text snapshot (TEXT kind) */
} held_key;

struct tk_kbd {
    tk_kbd_config cfg;
    uint32_t mods;
    bool caps;
    held_key held[KBD_MAX_HELD];
    int n_held;
    int latest;           /* held[] index of the repeat target, -1 */
    char rel_text[8];     /* release event's text snapshot: the
                             event points here, never at the
                             stack copy of the released key */
};

void tk_kbd_config_defaults(tk_kbd_config *cfg)
{
    cfg->layout = TK_KBD_LAYOUT_BRAIN;
    cfg->repeat_mode = TK_KBD_REPEAT_SYNTHETIC;
    cfg->repeat_delay_ms = 400;
    cfg->repeat_interval_ms = 50;
    cfg->longpress_ms = 600;
}

void tk_kbd_config_sanitize(tk_kbd_config *cfg)
{
    /* One fixed keymap: legacy JIS/US values coerce to BRAIN. */
    cfg->layout = TK_KBD_LAYOUT_BRAIN;
    if (cfg->repeat_mode != TK_KBD_REPEAT_KERNEL)
        cfg->repeat_mode = TK_KBD_REPEAT_SYNTHETIC;
    if (cfg->repeat_delay_ms < 100) cfg->repeat_delay_ms = 100;
    if (cfg->repeat_delay_ms > 2000) cfg->repeat_delay_ms = 2000;
    if (cfg->repeat_interval_ms < 10) cfg->repeat_interval_ms = 10;
    if (cfg->repeat_interval_ms > 1000) cfg->repeat_interval_ms = 1000;
    if (cfg->longpress_ms < 0) cfg->longpress_ms = 0;
    if (cfg->longpress_ms > 5000) cfg->longpress_ms = 5000;
}

tk_kbd *tk_kbd_new(const tk_kbd_config *cfg)
{
    tk_kbd *k = calloc(1, sizeof(*k));
    if (!k) return NULL;
    tk_kbd_config_defaults(&k->cfg);
    if (cfg) tk_kbd_set_config(k, cfg);
    k->latest = -1;
    return k;
}

void tk_kbd_free(tk_kbd *k) { free(k); }

void tk_kbd_set_config(tk_kbd *k, const tk_kbd_config *cfg)
{
    k->cfg = *cfg;
    tk_kbd_config_sanitize(&k->cfg);
}

void tk_kbd_config_of(const tk_kbd *k, tk_kbd_config *out) { *out = k->cfg; }
uint32_t tk_kbd_modifiers(const tk_kbd *k) { return k->mods; }
bool tk_kbd_caps(const tk_kbd *k) { return k->caps; }
int tk_kbd_held_count(const tk_kbd *k) { return k->n_held; }

static int held_index(const tk_kbd *k, int code)
{
    for (int i = 0; i < k->n_held; i++)
        if (k->held[i].code == code) return i;
    return -1;
}

/* Resolve the text a TEXT entry produces under the current state:
 * the Wiki keymap's two levels (plain / Shift). Letters treat
 * Caps Lock as XOR with Shift, like a PC keyboard. */
static const char *resolve_text(const tk_kbd *k, const kbd_entry *e)
{
    bool shift = (k->mods & TK_MOD_SHIFT) != 0;
    if (e->lo && e->lo[0] >= 'a' && e->lo[0] <= 'z' && e->lo[1] == '\0')
        shift = shift != k->caps;  /* letters: Caps XOR Shift */
    return shift ? e->hi : e->lo;
}

static void fill_event(const tk_kbd *k, const held_key *h, bool is_up,
                       bool repeat, bool longpress, tk_event *out)
{
    const kbd_entry *e = h->e;
    memset(out, 0, sizeof(*out));
    out->modifiers = k->mods;
    out->repeat = repeat;
    out->longpress = longpress;
    switch (e->kind) {
    case TK_KBD_KIND_TEXT:
        out->type = is_up ? TK_EV_KEY_UP : TK_EV_KEY_DOWN;
        out->text = h->text[0] ? h->text : NULL;
        break;
    case TK_KBD_KIND_EDIT:
        out->type = is_up ? TK_EV_KEY_UP : TK_EV_KEY_DOWN;
        out->key = e->key;
        break;
    case TK_KBD_KIND_ACTION:
        out->type = TK_EV_ACTION;
        out->action = e->action + e->action_arg;
        break;
    default:
        out->type = TK_EV_KEY_DOWN;
        break;
    }
}

bool tk_kbd_feed(tk_kbd *k, int code, int value, long now_ms, tk_event *out)
{
    const kbd_entry *e = find_entry(code);
    if (!e || e->kind == TK_KBD_KIND_IGNORE) return false;

    if (e->kind == TK_KBD_KIND_MODIFIER) {
        if (code == KEY_CAPSLOCK) {
            if (value == 1) k->caps = !k->caps;
            return false;
        }
        if (value) k->mods |= e->mod;
        else k->mods &= ~e->mod;
        return false;
    }

    if (value == 1) {  /* press */
        if (held_index(k, code) >= 0) return false;  /* already held */
        if (k->n_held >= KBD_MAX_HELD) return false;
        held_key *h = &k->held[k->n_held++];
        memset(h, 0, sizeof(*h));
        h->code = code;
        h->e = e;
        h->press_ms = now_ms;
        h->next_repeat_ms = now_ms + k->cfg.repeat_delay_ms;
        if (e->kind == TK_KBD_KIND_TEXT) {
            const char *t = resolve_text(k, e);
            if (t) {
                strncpy(h->text, t, sizeof(h->text) - 1);
                h->text[sizeof(h->text) - 1] = '\0';
            }
        }
        /* The newest held key is the repeat/long-press target.
         * Repeats additionally require the key to be repeatable;
         * long press applies to any key (the power key is the
         * canonical case). */
        k->latest = k->n_held - 1;
        fill_event(k, h, false, false, false, out);
        return true;
    }
    if (value == 2) {  /* kernel autorepeat */
        if (k->cfg.repeat_mode != TK_KBD_REPEAT_KERNEL) return false;
        int i = held_index(k, code);
        if (i < 0 || !e->repeatable) return false;
        fill_event(k, &k->held[i], false, true, false, out);
        return true;
    }
    if (value == 0) {  /* release */
        int i = held_index(k, code);
        if (i < 0) return false;
        held_key copy = k->held[i];
        /* remove, keeping order; fix the repeat target index */
        memmove(&k->held[i], &k->held[i + 1],
                (size_t)(k->n_held - i - 1) * sizeof(held_key));
        k->n_held--;
        if (k->latest == i)
            k->latest = k->n_held - 1;  /* most recent remaining, or -1 */
        else if (k->latest > i)
            k->latest--;
        /* Actions are press-only. */
        if (e->kind == TK_KBD_KIND_ACTION) return false;
        /* fill_event points out->text at the key's snapshot;
         * for a release that snapshot is `copy`, a stack local
         * - move it into the kbd's own slot so the pointer the
         * caller keeps stays valid after this call returns
         * (ASan stack-use-after-return via the session's key
         * forwarding, foundation audit). */
        memcpy(k->rel_text, copy.text, sizeof(k->rel_text));
        fill_event(k, &copy, true, false, false, out);
        if (out->text) out->text = k->rel_text;
        return true;
    }
    return false;
}

bool tk_kbd_poll(tk_kbd *k, long now_ms, tk_event *out)
{
    if (k->latest < 0 || k->latest >= k->n_held) return false;
    held_key *h = &k->held[k->latest];
    /* long press first: one notification per press */
    if (!h->lp_fired && k->cfg.longpress_ms > 0 &&
        now_ms - h->press_ms >= k->cfg.longpress_ms) {
        h->lp_fired = true;
        fill_event(k, h, false, false, true, out);
        return true;
    }
    if (k->cfg.repeat_mode == TK_KBD_REPEAT_SYNTHETIC &&
        h->e->repeatable && now_ms >= h->next_repeat_ms) {
        h->next_repeat_ms = now_ms + k->cfg.repeat_interval_ms;
        fill_event(k, h, false, true, false, out);
        return true;
    }
    return false;
}

/* --- table introspection --- */

size_t tk_kbd_table_count(void) { return TABLE_N; }

const tk_kbd_key_info *tk_kbd_table_at(size_t i)
{
    static tk_kbd_key_info info;  /* single-threaded UI use */
    if (i >= TABLE_N) return NULL;
    info.code = KEY_TABLE[i].code;
    info.name = KEY_TABLE[i].name;
    info.kind = KEY_TABLE[i].kind;
    info.repeatable = KEY_TABLE[i].repeatable;
    return &info;
}

bool tk_kbd_describe(int code, tk_kbd_key_info *out)
{
    const kbd_entry *e = find_entry(code);
    if (!e) return false;
    out->code = e->code;
    out->name = e->name;
    out->kind = e->kind;
    out->repeatable = e->repeatable;
    return true;
}
