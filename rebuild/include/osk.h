/* osk - OSK v2: on-screen keyboard + Japanese input integration
 * for libbrainui2 (toolkit v2).
 *
 * One input path for three sources:
 *   - the on-screen QWERTY / symbol / flick / gojuon keys
 *   - the physical keyboard (key events routed to the OSK widget)
 *   - direct romaji feeds (tk_osk_input_romaji)
 * All three feed the same reading buffer; the reading is handed to
 * an IME backend session (libbrainime via osk/osk_brainime.c, or a
 * built-in kana-only fallback), the conversion is shown in the
 * attached target TextField/TextView as a textcore composition
 * (preedit), candidates are shown in the OSK candidate bar, and
 * commits go to the target as TK_EV_TEXT_INPUT.  The path is
 * shared (integrated) with the physical keyboard, but the OSK is
 * NOT a copy of the physical layout: it is a superset of it.
 *
 * Layout policy (M3, user decision 2026-11-20): the primary
 * layout is QWERTY.  Japanese is typed as QWERTY romaji through
 * the IME; an English (direct) sub-mode sends Latin text straight
 * to the target; the symbol pages carry everything the physical
 * keyboard cannot produce (fullwidth forms, brackets, arrows,
 * units, accented Latin, Greek, ...).  Flick and gojuon remain
 * available as optional layouts but are no longer the default
 * path.  Mode cycle order: QWERTY -> symbols -> gojuon -> flick.
 *
 * The widget never links an IME engine itself: the backend is a
 * vtable (osk_ime_backend) mirroring the bi_session API, injected
 * with tk_osk_set_backend().  That keeps libbrainui2 free of the
 * dictionary dependency and lets the shell pick the engine.
 *
 * Layout constants: the Brain panel is 854 px wide in landscape
 * (M22 premise correction, AUDIT SS20), which is the design
 * width: key widths are computed from the actual width
 * (wr.w / 10), so QWERTY letter keys are 85 px wide there,
 * symbol cells likewise, and every row is 56 px high, so all
 * input keys stay above the toolkit's 54 px touch floor in
 * both dimensions.  At the minimum width of 480 px the
 * heights stay 56 px but a 10-column row is physically
 * limited to 48 px per key.  Candidate bar 40 px,
 * flick keys 60 px high, gojuon cells 56 px high x ~96 px wide
 * (the table is split into two pages, あ-な行 / は-わ行, since
 * M2), bottom bar 44 px.
 */
#ifndef OSK_H
#define OSK_H

#include <stdbool.h>
#include <stddef.h>
#include "tk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* IME backend vtable (mirrors the libbrainime session API)            */
/* ------------------------------------------------------------------ */
typedef struct {
    /* Create a session bound to an engine handle (backend-owned
     * meaning; for libbrainime this is a bi_engine *). */
    void *(*session_new)(void *engine);
    void (*session_free)(void *session);
    /* Replace the whole reading (kana) and reconvert. */
    int (*session_set_reading)(void *session, const char *kana);
    /* Current conversion text with user choices applied. */
    const char *(*session_text)(void *session);
    /* Current reading (kana) as held by the session. */
    const char *(*session_preedit)(void *session);
    int (*segment_count)(void *session);
    /* Kana reading of segment `seg` (for bunsetsu resize maths). */
    const char *(*segment_reading)(void *session, int seg);
    /* Surface (converted) text of segment `seg`. OPTIONAL (may be
     * NULL): when present, the OSK attaches per-segment byte
     * lengths to the composition event so editors can render
     * per-segment underlines; when absent the composition is a
     * single segment. The concatenation of all segment surfaces
     * must equal session_text(). */
    const char *(*segment_text)(void *session, int seg);
    int (*candidate_count)(void *session, int seg);
    const char *(*candidate)(void *session, int seg, int cand);
    /* Choose candidate `cand` for segment `seg` (no reconversion). */
    int (*choose)(void *session, int seg, int cand);
    /* Resize segment `seg` to new_len characters and reconvert. */
    int (*resize)(void *session, int seg, int new_len);
    /* Commit segment 0: writes its surface to out (bytes, excl NUL
     * accounting is the caller's; out is NUL-terminated), drops it
     * from the reading and reconverts the rest.  Returns bytes
     * written, or -1 when nothing remains. */
    int (*commit_first)(void *session, char *out, size_t outsz);
} osk_ime_backend;

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    OSK_MODE_FLICK = 0,   /* 12-key flick (+ toggle multi-tap);
                           * optional layout since M3 */
    OSK_MODE_QWERTY,      /* PRIMARY (default): romaji via QWERTY,
                           * or direct Latin in English sub-mode */
    OSK_MODE_GOJUON,      /* 50-on table; optional layout */
    OSK_MODE_SYMBOL       /* symbol pages: the "+alpha" superset */
} osk_mode;

/* Flick directions; index into the per-key kana table. */
typedef enum {
    OSK_DIR_CENTER = 0, OSK_DIR_LEFT, OSK_DIR_UP, OSK_DIR_RIGHT,
    OSK_DIR_DOWN, OSK_NDIR
} osk_dir;

/* Pure helpers (unit-tested without a widget). */
/* Classify a flick gesture from its down->up displacement.
 * Below the tap threshold (in px) the result is OSK_DIR_CENTER. */
osk_dir osk_flick_classify(int dx, int dy, int tap_threshold_px);
/* Kana for flick key `key` (0..11) in direction `dir`, or NULL
 * when that direction produces nothing (dakuten/small keys). */
const char *osk_flick_kana(int key, osk_dir dir);
/* Display label (centre kana / key name) for flick key `key`. */
const char *osk_flick_key_label(int key);

/* ------------------------------------------------------------------ */
/* Romaji -> kana state machine (shared by QWERTY keys and the         */
/* physical keyboard path).  Feed one ASCII char at a time; kana is    */
/* appended to `out` (NUL-terminated, may stay empty while a syllable  */
/* is pending).  osk_romaji_flush resolves a trailing pending "n" to   */
/* ん and any other pending fragment literally.                        */
/* ------------------------------------------------------------------ */
typedef struct {
    char pending[8];      /* unconverted romaji fragment */
    int plen;
} osk_romaji;

void osk_romaji_init(osk_romaji *r);
/* Returns bytes of kana appended to out. */
int osk_romaji_putc(osk_romaji *r, char c, char *out, size_t outsz);
int osk_romaji_flush(osk_romaji *r, char *out, size_t outsz);
/* True while an unresolved fragment is held. */
bool osk_romaji_pending(const osk_romaji *r);

/* ------------------------------------------------------------------ */
/* Widget                                                              */
/* ------------------------------------------------------------------ */
tk_widget *tk_osk_new(void);
/* Attach the target editor (TextField or TextView).  Compositions
 * and commits are delivered to it as toolkit events. */
void tk_osk_attach(tk_widget *osk, tk_widget *target);
tk_widget *tk_osk_target(const tk_widget *osk);
/* Inject the IME backend + engine handle; creates a session.
 * Passing a NULL backend restores the built-in kana-only fallback
 * (single segment; candidates = reading and its katakana form).
 * The engine handle is NOT owned by the widget. */
void tk_osk_set_backend(tk_widget *osk, const osk_ime_backend *be,
                        void *engine);
bool tk_osk_has_backend(const tk_widget *osk);

void tk_osk_set_mode(tk_widget *osk, osk_mode m);
osk_mode tk_osk_mode(const tk_widget *osk);
/* English (direct) sub-mode of QWERTY: letter keys and typed
 * text go straight to the target as committed text, bypassing
 * the romaji/IME reading.  Japanese QWERTY input is the default
 * (direct == false).  Only meaningful in OSK_MODE_QWERTY. */
void tk_osk_set_direct(tk_widget *osk, bool direct);
bool tk_osk_direct(const tk_widget *osk);
/* QWERTY shift state: 0 = off, 1 = one-shot (consumed by the
 * next letter), 2 = lock.  Driven by the on-screen shift key;
 * the physical keyboard's own shift/caps handling lives in
 * tk_kbd and is unaffected. */
int tk_osk_shift_state(const tk_widget *osk);
/* Symbol pages (OSK_MODE_SYMBOL): category pages of characters
 * the physical keyboard cannot produce.  The on-screen page key
 * cycles through them; these accessors expose the same state. */
int tk_osk_symbol_page(const tk_widget *osk);
void tk_osk_set_symbol_page(tk_widget *osk, int page);
int osk_symbol_page_count(void);
const char *osk_symbol_page_name(int page);
/* Gojuon page (0 = あ-な行, 1 = は-わ行); the table is paged so
 * every cell can be 56 px high. */
int tk_osk_gojuon_page(const tk_widget *osk);
void tk_osk_set_gojuon_page(tk_widget *osk, int page);
/* Flick press preview (M2): while a flick key (id 0..9) is held,
 * an enlarged cross popup showing the key's five kana is shown
 * with the currently selected direction highlighted. Returns true
 * while the preview is active and fills key/dir/rect (any output
 * pointer may be NULL). rect is in root coordinates. */
bool tk_osk_preview(const tk_widget *osk, int *key, osk_dir *dir,
                    tk_rect *rect);
/* Flick keys: false = flick gestures, true = multi-tap cycling. */
void tk_osk_set_toggle_mode(tk_widget *osk, bool toggle);
bool tk_osk_toggle_mode(const tk_widget *osk);

/* Feed a romaji string through the same path as QWERTY keys /
 * physical keyboard.  Returns false if any char was rejected. */
bool tk_osk_input_romaji(tk_widget *osk, const char *romaji);
/* Append kana (or any UTF-8) directly to the reading and reconvert.
 * This is what flick/gojuon cells call internally. */
void tk_osk_input_kana(tk_widget *osk, const char *kana_utf8);

/* Current reading (kana) held by the OSK; "" when idle. */
const char *tk_osk_reading(const tk_widget *osk);
/* Current conversion display text (== target composition). */
const char *tk_osk_conversion(const tk_widget *osk);
/* Candidate introspection for segment 0 (the active segment). */
int tk_osk_segment_count(const tk_widget *osk);
int tk_osk_candidate_count(const tk_widget *osk);
const char *tk_osk_candidate(const tk_widget *osk, int idx);
/* Index of the candidate currently chosen for segment 0. */
int tk_osk_candidate_index(const tk_widget *osk);

/* Programmatic equivalents of the on-screen keys. */
void tk_osk_convert_next(tk_widget *osk);  /* 変換: next candidate */
void tk_osk_convert_prev(tk_widget *osk);
void tk_osk_resize_segment(tk_widget *osk, int delta_chars);
void tk_osk_commit(tk_widget *osk);        /* 確定: commit segment 0 */
void tk_osk_backspace(tk_widget *osk);     /* 削除 */
void tk_osk_cursor(tk_widget *osk, int dir); /* target cursor move */

/* Time (microseconds) of the last reconversion, for perf probes. */
long tk_osk_last_convert_us(const tk_widget *osk);
/* Diagnostics: number of times a layout cache (candidate bar or
 * key grid) was actually rebuilt.  Repeated paints, pointer
 * events and candidate *selections* must not move this counter;
 * it exists so the diff/caching behaviour is testable, not just
 * claimed. */
long tk_osk_layout_builds(const tk_widget *osk);

/* ------------------------------------------------------------------ */
/* Layout cells: the single source of truth for painting, hit          */
/* testing and tests.  tk_osk_layout() enumerates the cells of the     */
/* current mode at the widget's arranged rect.                         */
/* ------------------------------------------------------------------ */
typedef enum {
    OSK_CELL_CANDIDATE,   /* id = candidate index (segment 0) */
    OSK_CELL_CAND_PAGE,   /* id = -1 prev page, +1 next page */
    OSK_CELL_KANA_KEY,    /* id = flick key 0..11; text = centre kana */
    OSK_CELL_LETTER,      /* text = one romaji letter (QWERTY) */
    OSK_CELL_GOJUON,      /* text = kana */
    OSK_CELL_SYMBOL,      /* text = digit/symbol/letter */
    OSK_CELL_FUNC,        /* id = osk_func */
    OSK_CELL_SYM_CAT      /* id = symbol page index (category chip
                           * in the candidate bar, symbol mode) */
} osk_cell_kind;

typedef enum {
    OSK_FN_BACKSPACE = 1, OSK_FN_CONVERT, OSK_FN_COMMIT, OSK_FN_MODE,
    OSK_FN_LEFT, OSK_FN_RIGHT, OSK_FN_SPACE, OSK_FN_TOGGLE_MODE,
    OSK_FN_SYMBOL_PAGE, OSK_FN_GOJUON_PAGE,
    OSK_FN_SHIFT,         /* QWERTY row 3: off -> one-shot -> lock */
    OSK_FN_LANG           /* QWERTY: Japanese (romaji) <-> English */
} osk_func;

typedef struct {
    tk_rect r;
    osk_cell_kind kind;
    int id;
    char label[16];       /* what is painted on the cell */
    char text[16];        /* what the cell inputs (kana/char), if any */
} osk_cell;

int tk_osk_layout(const tk_widget *osk, osk_cell *out, int max);

#ifdef __cplusplus
}
#endif
#endif /* OSK_H */
