/* test_osk - OSK v2 integration tests.
 *
 * Groups:
 *   R  romaji->kana state machine (pure)
 *   F  flick classification + tables (pure)
 *   C  widget + fallback backend: flick, toggle, dakuten, commit
 *   D  widget + libbrainime: flick-type a sentence, candidate bar
 *      selection, convert key, bunsetsu resize, segment commits
 *   K  physical keyboard path (key events routed to the OSK)
 *   U  unified path: synthetic evdev -> tk_kbd -> root -> OSK/field
 *   V  flick press preview popup
 *   W  composition segments in the target buffer (underline model)
 *   Q  QWERTY mode cells
 *   E  QWERTY is the default/primary layout: mode cycle order,
 *      English direct sub-mode, shift (one-shot/lock), key
 *      geometry at the 800 px design width
 *   G  gojuon mode cells (paged, 56 px cells)
 *   S  symbol mode cells + page toggle
 *   X  symbol pages full coverage: every character on every
 *      page can be input through the composition path and
 *      committed to the field; geometry at 800 px
 *   L  layout cache / diff behaviour + paint smoke + key geometry
 *   N  sink target: the OSK attached to a TK_W_SINK (the shell's
 *      system-wide path) - compositions, commits and key events
 *      leave through the sink callback instead of a local buffer
 *
 * Usage: test_osk <dict.bidf> [sidecar.bisc]
 * Prints one line per check; exit code is the failure count.
 */
#define _POSIX_C_SOURCE 200809L
#include "tk.h"
#include "tk_kbd.h"
#include "osk.h"
#include "brainime.h"
#include "osk_brainime.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---------------------------------------------------------------- */
/* UI fixture: fixed layout, TextField on top, OSK below             */
/* ---------------------------------------------------------------- */
typedef struct {
    tk_widget *top, *tf, *osk;
    tk_root *root;
} ui;

static ui ui_new_w(bi_engine *eng, int w)
{
    ui u;
    u.top = tk_fixed_new();
    u.tf = tk_textfield_new("");
    tk_set_fixed_rect(u.tf, (tk_rect){ 0, 0, w, 44 });
    u.osk = tk_osk_new();
    tk_set_fixed_rect(u.osk, (tk_rect){ 0, 44, w, 700 });
    tk_add_child(u.top, u.tf);
    tk_add_child(u.top, u.osk);
    tk_osk_attach(u.osk, u.tf);
    if (eng) tk_osk_set_backend(u.osk, osk_brainime_backend(), eng);
    u.root = tk_root_new(u.top);
    tk_root_layout(u.root, w, 800);
    return u;
}
static ui ui_new(bi_engine *eng) { return ui_new_w(eng, 480); }
static void ui_free(ui *u)
{
    tk_root_free(u->root);
    tk_widget_free(u->top);
}

static void field_text(ui *u, char *out, size_t cap)
{
    tk_textfield_text(u->tf, out, cap);
}
static void field_comp(ui *u, char *out, size_t cap)
{
    tc_composition(tk_textfield_buffer(u->tf), out, cap);
}

/* ---------------------------------------------------------------- */
/* Event helpers (synthetic evdev-equivalent events via the root)    */
/* ---------------------------------------------------------------- */
static void ptr(ui *u, tk_event_type type, int x, int y)
{
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.x = x;
    ev.y = y;
    tk_root_event(u->root, &ev);
}
static void tap_at(ui *u, int x, int y)
{
    ptr(u, TK_EV_POINTER_DOWN, x, y);
    ptr(u, TK_EV_POINTER_UP, x, y);
}
static void flick_at(ui *u, int x, int y, int dx, int dy)
{
    ptr(u, TK_EV_POINTER_DOWN, x, y);
    ptr(u, TK_EV_POINTER_UP, x + dx, y + dy);
}
static void key_ev(ui *u, tk_key key, const char *text)
{
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN;
    ev.key = key;
    ev.text = text;
    tk_root_event(u->root, &ev);
}
static void type_key(ui *u, char c)
{
    char buf[2] = { c, '\0' };
    key_ev(u, TK_KEY_NONE, buf);
}

/* Find a layout cell; returns 0 and centre coords on success. */
static int find_cell(ui *u, osk_cell_kind kind, int id,
                     const char *text, int *cx, int *cy)
{
    osk_cell cells[256];
    int n = tk_osk_layout(u->osk, cells, 256);
    for (int i = 0; i < n; i++) {
        if (cells[i].kind != kind) continue;
        if (id >= 0 && cells[i].id != id) continue;
        if (text && strcmp(cells[i].text, text) &&
            strcmp(cells[i].label, text)) continue;
        *cx = cells[i].r.x + cells[i].r.w / 2;
        *cy = cells[i].r.y + cells[i].r.h / 2;
        return 1;
    }
    return 0;
}
static int tap_cell(ui *u, osk_cell_kind kind, int id, const char *text)
{
    int x, y;
    if (!find_cell(u, kind, id, text, &x, &y)) return 0;
    tap_at(u, x, y);
    return 1;
}
static int flick_cell(ui *u, int key_id, int dx, int dy)
{
    int x, y;
    if (!find_cell(u, OSK_CELL_KANA_KEY, key_id, NULL, &x, &y)) return 0;
    flick_at(u, x, y, dx, dy);
    return 1;
}

/* ---------------------------------------------------------------- */
/* R: romaji unit                                                    */
/* ---------------------------------------------------------------- */
static void romaji_feed(const char *input, char *out, size_t cap)
{
    osk_romaji r;
    osk_romaji_init(&r);
    out[0] = '\0';
    char acc[256] = "";
    for (const char *p = input; *p; p++) {
        char o[64];
        osk_romaji_putc(&r, *p, o, sizeof(o));
        strcat(acc, o);
    }
    char o[64];
    osk_romaji_flush(&r, o, sizeof(o));
    strcat(acc, o);
    snprintf(out, cap, "%s", acc);
}
static void group_romaji(void)
{
    char out[256];
    static const struct { const char *in, *want; } cases[] = {
        { "kyou", "きょう" }, { "tenki", "てんき" },
        { "kitte", "きって" }, { "shinbun", "しんぶん" },
        { "kan", "かん" },     { "chotto", "ちょっと" },
        { "pasokon", "ぱそこん" }, { "gakkou", "がっこう" },
        { "a", "あ" },         { "n", "ん" },
        { "fi", "ふぃ" },      { "vu", "ゔ" },
        { "xtu", "っ" },       { "kyouhatenki", "きょうはてんき" },
        { "toukyou", "とうきょう" }, { "ohayou", "おはよう" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        romaji_feed(cases[i].in, out, sizeof(out));
        char what[64];
        snprintf(what, sizeof(what), "R romaji %s", cases[i].in);
        check_str(what, out, cases[i].want);
    }
}

/* ---------------------------------------------------------------- */
/* F: flick unit                                                     */
/* ---------------------------------------------------------------- */
static void group_flick_unit(void)
{
    check_int("F classify tap", osk_flick_classify(0, 0, 14), OSK_DIR_CENTER);
    check_int("F classify small", osk_flick_classify(13, 5, 14), OSK_DIR_CENTER);
    check_int("F classify right", osk_flick_classify(40, 0, 14), OSK_DIR_RIGHT);
    check_int("F classify left", osk_flick_classify(-40, 0, 14), OSK_DIR_LEFT);
    check_int("F classify up", osk_flick_classify(0, -40, 14), OSK_DIR_UP);
    check_int("F classify down", osk_flick_classify(0, 40, 14), OSK_DIR_DOWN);
    check_int("F classify diag h", osk_flick_classify(30, 10, 14), OSK_DIR_RIGHT);
    check_int("F classify diag v", osk_flick_classify(10, 30, 14), OSK_DIR_DOWN);
    check_str("F kana a-center", osk_flick_kana(0, OSK_DIR_CENTER), "あ");
    check_str("F kana a-left", osk_flick_kana(0, OSK_DIR_LEFT), "い");
    check_str("F kana a-up", osk_flick_kana(0, OSK_DIR_UP), "う");
    check_str("F kana a-right", osk_flick_kana(0, OSK_DIR_RIGHT), "え");
    check_str("F kana a-down", osk_flick_kana(0, OSK_DIR_DOWN), "お");
    check_str("F kana ka-right", osk_flick_kana(1, OSK_DIR_RIGHT), "け");
    check_str("F kana ya-up", osk_flick_kana(7, OSK_DIR_UP), "ゆ");
    check_str("F kana wa-down", osk_flick_kana(9, OSK_DIR_DOWN), "ゎ");
    check_true("F kana action key none", osk_flick_kana(10, OSK_DIR_CENTER) == NULL);
}

/* ---------------------------------------------------------------- */
/* C: widget + fallback backend                                      */
/* ---------------------------------------------------------------- */
static void group_fallback(void)
{
    ui u = ui_new(NULL);
    char buf[512];
    tk_osk_set_mode(u.osk, OSK_MODE_FLICK);  /* optional layout (M3) */
    check_true("C no backend", !tk_osk_has_backend(u.osk));
    check_true("C tap ka key", tap_cell(&u, OSK_CELL_KANA_KEY, 1, NULL));
    check_str("C reading ka", tk_osk_reading(u.osk), "か");
    field_comp(&u, buf, sizeof(buf));
    check_str("C composition ka", buf, "か");
    check_int("C fallback cand count", tk_osk_candidate_count(u.osk), 2);
    check_str("C fallback cand kata", tk_osk_candidate(u.osk, 1), "カ");
    check_true("C flick ka up", flick_cell(&u, 1, 0, -45));
    check_str("C reading kaku", tk_osk_reading(u.osk), "かく");
    /* toggle (multi-tap) mode */
    tk_osk_set_toggle_mode(u.osk, true);
    check_true("C toggle tap a1", tap_cell(&u, OSK_CELL_KANA_KEY, 0, NULL));
    check_true("C toggle tap a2", tap_cell(&u, OSK_CELL_KANA_KEY, 0, NULL));
    check_true("C toggle tap a3", tap_cell(&u, OSK_CELL_KANA_KEY, 0, NULL));
    check_str("C toggle cycled", tk_osk_reading(u.osk), "かくう");
    check_true("C toggle other key", tap_cell(&u, OSK_CELL_KANA_KEY, 1, NULL));
    check_str("C toggle sealed", tk_osk_reading(u.osk), "かくうか");
    tk_osk_set_toggle_mode(u.osk, false);
    /* dakuten key */
    check_true("C dakuten tap", tap_cell(&u, OSK_CELL_KANA_KEY, 11, NULL));
    check_str("C dakuten", tk_osk_reading(u.osk), "かくうが");
    /* backspace via function cell */
    check_true("C backspace cell",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_BACKSPACE, NULL));
    check_str("C after backspace", tk_osk_reading(u.osk), "かくう");
    /* commit via function cell */
    check_true("C commit cell",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_COMMIT, NULL));
    field_text(&u, buf, sizeof(buf));
    check_str("C committed text", buf, "かくう");
    check_str("C reading empty", tk_osk_reading(u.osk), "");
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* N: sink target (the shell's system-wide OSK path)                 */
/* ---------------------------------------------------------------- */
typedef struct {
    int n_comp, n_text, n_key;
    char last_comp[128];
    char all_text[256];
    tk_key last_key;
} sink_log;

static bool sink_cb(void *ud, tk_event *ev)
{
    sink_log *l = ud;
    if (ev->type == TK_EV_COMPOSITION) {
        l->n_comp++;
        snprintf(l->last_comp, sizeof(l->last_comp), "%s",
                 ev->text ? ev->text : "");
    } else if (ev->type == TK_EV_TEXT_INPUT && ev->text) {
        l->n_text++;
        strncat(l->all_text, ev->text,
                sizeof(l->all_text) - strlen(l->all_text) - 1);
    } else if (ev->type == TK_EV_KEY_DOWN) {
        l->n_key++;
        l->last_key = ev->key;
    }
    return true;
}

static void group_sink(void)
{
    sink_log log;
    memset(&log, 0, sizeof(log));
    ui u;
    u.top = tk_fixed_new();
    u.tf = tk_sink_new(sink_cb, &log);
    tk_set_fixed_rect(u.tf, (tk_rect){ 0, 0, 480, 44 });
    u.osk = tk_osk_new();
    tk_set_fixed_rect(u.osk, (tk_rect){ 0, 44, 480, 700 });
    tk_add_child(u.top, u.tf);
    tk_add_child(u.top, u.osk);
    tk_osk_attach(u.osk, u.tf);
    u.root = tk_root_new(u.top);
    tk_root_layout(u.root, 480, 800);

    check_true("N osk target is the sink", tk_osk_target(u.osk) == u.tf);
    /* with no composition in flight, backspace is forwarded to
     * the target as a key event (it deletes committed text) */
    check_true("N backspace cell",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_BACKSPACE, NULL));
    check_int("N key events", log.n_key, 1);
    check_int("N key is backspace", log.last_key, TK_KEY_BACKSPACE);
    /* QWERTY is the default mode: 'a' composes through the sink */
    check_true("N tap a", tap_cell(&u, OSK_CELL_LETTER, -1, "a"));
    check_int("N composition events", log.n_comp, 1);
    check_str("N composition is a", log.last_comp, "あ");
    check_int("N nothing committed yet", log.n_text, 0);
    /* commit: the text leaves as TEXT_INPUT */
    check_true("N commit cell",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_COMMIT, NULL));
    check_int("N committed events", log.n_text, 1);
    check_str("N committed text", log.all_text, "あ");
    /* the composition was cleared after the commit */
    check_str("N composition cleared", log.last_comp, "");
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* D: widget + libbrainime, full sentence scenario                   */
/* ---------------------------------------------------------------- */
static void flick_str(ui *u)
{
    /* きょうはてんき by flicks:
     * き = か LEFT, ょ = や RIGHT, う = あ UP, は = は CENTER,
     * て = た RIGHT, ん = わ UP, き = か LEFT */
    flick_cell(u, 1, -45, 0);
    flick_cell(u, 7, 45, 0);
    flick_cell(u, 0, 0, -45);
    flick_cell(u, 5, 0, 0);
    flick_cell(u, 3, 45, 0);
    flick_cell(u, 9, 0, -45);
    flick_cell(u, 1, -45, 0);
}

static void group_engine(bi_engine *eng)
{
    ui u = ui_new(eng);
    char buf[512];
    tk_osk_set_mode(u.osk, OSK_MODE_FLICK);  /* optional layout (M3) */
    check_true("D has backend", tk_osk_has_backend(u.osk));
    flick_str(&u);
    check_str("D reading", tk_osk_reading(u.osk), "きょうはてんき");
    check_str("D conversion", tk_osk_conversion(u.osk), "今日は天気");
    field_comp(&u, buf, sizeof(buf));
    check_str("D field composition", buf, "今日は天気");
    check_int("D segments", tk_osk_segment_count(u.osk), 3);
    check_str("D cand0", tk_osk_candidate(u.osk, 0), "今日");
    check_true("D cand count > 5", tk_osk_candidate_count(u.osk) > 5);
    /* candidate bar tap: 共 (index 1) */
    check_true("D tap cand kyou-2nd",
               tap_cell(&u, OSK_CELL_CANDIDATE, -1, "共"));
    check_str("D after cand tap", tk_osk_conversion(u.osk), "共は天気");
    check_int("D cand index", tk_osk_candidate_index(u.osk), 1);
    check_true("D tap cand back",
               tap_cell(&u, OSK_CELL_CANDIDATE, -1, "今日"));
    check_str("D back to best", tk_osk_conversion(u.osk), "今日は天気");
    /* convert key cycles candidates of segment 0 */
    check_true("D convert cell",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_CONVERT, NULL));
    check_int("D convert idx1", tk_osk_candidate_index(u.osk), 1);
    check_str("D convert text1", tk_osk_conversion(u.osk), "共は天気");
    check_true("D convert cell2",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_CONVERT, NULL));
    check_str("D convert text2", tk_osk_conversion(u.osk), "きょうは天気");
    /* bunsetsu resize: segment 0 きょう (3) -> 2 chars */
    tk_osk_resize_segment(u.osk, -1);
    check_str("D resize keeps reading", tk_osk_reading(u.osk),
              "きょうはてんき");
    check_true("D resize changes conversion",
               strcmp(tk_osk_conversion(u.osk), "きょうは天気") != 0);
    tk_osk_resize_segment(u.osk, +1);
    check_str("D resize back", tk_osk_conversion(u.osk), "今日は天気");
    /* segment-by-segment commit */
    tk_osk_commit(u.osk);
    field_text(&u, buf, sizeof(buf));
    check_str("D commit1 text", buf, "今日");
    check_str("D commit1 reading", tk_osk_reading(u.osk), "はてんき");
    field_comp(&u, buf, sizeof(buf));
    check_str("D commit1 comp", buf, "は天気");
    tk_osk_commit(u.osk);
    tk_osk_commit(u.osk);
    field_text(&u, buf, sizeof(buf));
    check_str("D commit all text", buf, "今日は天気");
    check_str("D commit all reading", tk_osk_reading(u.osk), "");
    field_comp(&u, buf, sizeof(buf));
    check_str("D comp cleared", buf, "");
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* K: physical keyboard path                                         */
/* ---------------------------------------------------------------- */
static void group_keyboard(bi_engine *eng)
{
    ui u = ui_new(eng);
    char buf[512];
    check_true("K focus osk", tk_focus(u.root, u.osk));
    const char *word = "tenki";
    for (const char *p = word; *p; p++) type_key(&u, *p);
    check_str("K reading", tk_osk_reading(u.osk), "てんき");
    check_str("K conversion", tk_osk_conversion(u.osk), "天気");
    key_ev(&u, TK_KEY_SPACE, NULL);
    check_str("K space converts", tk_osk_conversion(u.osk), "天鬼");
    check_int("K space idx", tk_osk_candidate_index(u.osk), 1);
    key_ev(&u, TK_KEY_DOWN, NULL);
    check_str("K down next", tk_osk_conversion(u.osk), "転機");
    key_ev(&u, TK_KEY_UP, NULL);
    check_str("K up prev", tk_osk_conversion(u.osk), "天鬼");
    key_ev(&u, TK_KEY_ENTER, NULL);
    field_text(&u, buf, sizeof(buf));
    check_str("K enter commits", buf, "天鬼");
    check_str("K reading empty", tk_osk_reading(u.osk), "");
    /* backspace eats romaji pending before the reading */
    type_key(&u, 's');
    check_str("K pending only", tk_osk_reading(u.osk), "");
    key_ev(&u, TK_KEY_BACKSPACE, NULL);
    type_key(&u, 's');
    type_key(&u, 'i');
    check_str("K pending cleared", tk_osk_reading(u.osk), "し");
    /* ESC cancels the preedit */
    const char *w2 = "kyou";
    for (const char *p = w2; *p; p++) type_key(&u, *p);
    check_str("K pre-esc reading", tk_osk_reading(u.osk), "しきょう");
    key_ev(&u, TK_KEY_ESCAPE, NULL);
    check_str("K esc clears", tk_osk_reading(u.osk), "");
    field_comp(&u, buf, sizeof(buf));
    check_str("K esc comp cleared", buf, "");
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* U: unified input path — synthetic evdev events through tk_kbd     */
/* and the root, proving the physical keyboard reaches the IME       */
/* composition through exactly the OSK's own path (extends K).       */
/* ---------------------------------------------------------------- */
typedef struct { tk_kbd *kbd; ui *u; } kbd_ui;

static void kbd_feed(kbd_ui *ku, int code, int value, long t)
{
    tk_event ev;
    if (tk_kbd_feed(ku->kbd, code, value, t, &ev))
        tk_root_event(ku->u->root, &ev);
}
static void kbd_tap(kbd_ui *ku, int code, long t)
{
    kbd_feed(ku, code, 1, t);
    kbd_feed(ku, code, 0, t + 1);
}
static void kbd_poll(kbd_ui *ku, long t)
{
    tk_event ev;
    while (tk_kbd_poll(ku->kbd, t, &ev))
        tk_root_event(ku->u->root, &ev);
}

static void group_unified(bi_engine *eng)
{
    ui u = ui_new(eng);
    char buf[512];
    tk_kbd *kbd = tk_kbd_new(NULL);  /* Brainux keymap, synthetic repeat */
    kbd_ui ku = { kbd, &u };
    check_true("U focus osk", tk_focus(u.root, u.osk));
    kbd_tap(&ku, KEY_T, 0); kbd_tap(&ku, KEY_E, 10);
    kbd_tap(&ku, KEY_N, 20); kbd_tap(&ku, KEY_K, 30);
    kbd_tap(&ku, KEY_I, 40);
    check_str("U reading", tk_osk_reading(u.osk), "てんき");
    check_str("U conversion", tk_osk_conversion(u.osk), "天気");
    kbd_tap(&ku, KEY_SPACE, 50);
    check_str("U space converts", tk_osk_conversion(u.osk), "天鬼");
    kbd_tap(&ku, KEY_ENTER, 60);
    field_text(&u, buf, sizeof(buf));
    check_str("U enter commits", buf, "天鬼");
    /* synthetic autorepeat through the whole stack: hold A */
    kbd_feed(&ku, KEY_A, 1, 1000);
    check_str("U hold a", tk_osk_reading(u.osk), "あ");
    kbd_poll(&ku, 1399);
    check_str("U no repeat yet", tk_osk_reading(u.osk), "あ");
    kbd_poll(&ku, 1400);
    check_str("U repeat typed", tk_osk_reading(u.osk), "ああ");
    kbd_feed(&ku, KEY_A, 0, 1410);
    /* a non-text key does not disturb the preedit (KEY_HOME is
     * the caret key; the built-in keyboard never emits it) */
    kbd_tap(&ku, KEY_HOME, 1500);
    check_str("U home key ignored by editor", tk_osk_reading(u.osk), "ああ");
    /* the physical home key emits KEY_ESC, which Brainux treats
     * as Escape: it cancels the composition */
    kbd_tap(&ku, KEY_ESC, 1510);
    check_str("U esc clears", tk_osk_reading(u.osk), "");
    /* the same physical path into a plain focused field (no OSK
     * in between): the Brainux keymap is US-based, so Shift+2
     * (the symbol+Shift+W level) produces '@', not the PC-JIS '"' */
    check_true("U focus field", tk_focus(u.root, u.tf));
    kbd_tap(&ku, KEY_A, 1600);
    kbd_feed(&ku, KEY_LEFTSHIFT, 1, 1610);
    kbd_tap(&ku, KEY_2, 1620);
    kbd_feed(&ku, KEY_LEFTSHIFT, 0, 1630);
    field_text(&u, buf, sizeof(buf));
    check_str("U field got keymap text", buf, "天鬼a@");
    tk_kbd_free(kbd);
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* V: flick press preview                                            */
/* ---------------------------------------------------------------- */
static void group_preview(void)
{
    ui u = ui_new(NULL);
    tk_osk_set_mode(u.osk, OSK_MODE_FLICK);  /* optional layout (M3) */
    int cx, cy;
    check_true("V find ka", find_cell(&u, OSK_CELL_KANA_KEY, 1, "か",
                                      &cx, &cy));
    int key = -1;
    osk_dir dir = OSK_DIR_CENTER;
    tk_rect pr;
    check_true("V no preview idle", !tk_osk_preview(u.osk, &key, &dir, &pr));
    ptr(&u, TK_EV_POINTER_DOWN, cx, cy);
    check_true("V preview on down", tk_osk_preview(u.osk, &key, &dir, &pr));
    check_int("V preview key", key, 1);
    check_int("V preview dir center", dir, OSK_DIR_CENTER);
    check_int("V preview w", pr.w, 156);
    check_int("V preview h", pr.h, 156);
    check_true("V preview inside widget",
               pr.x >= 0 && pr.y >= 44 && pr.x + pr.w <= 480 &&
               pr.y + pr.h <= 744);
    ptr(&u, TK_EV_POINTER_MOVE, cx + 40, cy);
    tk_osk_preview(u.osk, &key, &dir, &pr);
    check_int("V preview dir right", dir, OSK_DIR_RIGHT);
    ptr(&u, TK_EV_POINTER_MOVE, cx, cy - 40);
    tk_osk_preview(u.osk, &key, &dir, &pr);
    check_int("V preview dir up", dir, OSK_DIR_UP);
    /* paint: the selected (up) cell is accent, the centre is not */
    tk_canvas *cv = tk_canvas_new(480, 800);
    tk_root_paint(u.root, cv, tk_theme_default());
    tk_color acc = tk_theme_default()->accent;
    tk_color up_px = tk_canvas_pixel(cv, pr.x + 78, pr.y + 26);
    check_true("V selected cell accent",
               up_px.r == acc.r && up_px.g == acc.g && up_px.b == acc.b);
    tk_color ctr_px = tk_canvas_pixel(cv, pr.x + 78, pr.y + 78);
    check_true("V centre not accent",
               !(ctr_px.r == acc.r && ctr_px.g == acc.g &&
                 ctr_px.b == acc.b));
    tk_canvas_free(cv);
    /* release inputs the flicked kana and closes the preview
     * (か key flicked up = く per the FLICK table order) */
    ptr(&u, TK_EV_POINTER_UP, cx, cy - 40);
    check_str("V flick up inputs ku", tk_osk_reading(u.osk), "く");
    check_true("V preview closed", !tk_osk_preview(u.osk, &key, &dir, &pr));
    /* no preview for function cells */
    int fx, fy;
    check_true("V find bs",
               find_cell(&u, OSK_CELL_FUNC, OSK_FN_BACKSPACE, NULL,
                         &fx, &fy));
    ptr(&u, TK_EV_POINTER_DOWN, fx, fy);
    check_true("V no preview on func", !tk_osk_preview(u.osk, &key, &dir, &pr));
    ptr(&u, TK_EV_POINTER_UP, fx, fy);
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* W: composition segments reach the target's textcore buffer        */
/* ---------------------------------------------------------------- */
static void group_segments(bi_engine *eng)
{
    ui u = ui_new(eng);
    tc_buffer *b = tk_textfield_buffer(u.tf);
    size_t lens[8];
    tk_osk_input_kana(u.osk, "きょうはてんき");
    /* the engine segments 今日は天気 as 今日 / は / 天気 */
    size_t n = tc_composition_segments(b, lens, 8);
    check_int("W three segments", (long)n, 3);
    check_int("W seg0 bytes", (long)lens[0], 6);   /* 今日 */
    check_int("W seg1 bytes", (long)lens[1], 3);   /* は */
    check_int("W segs tile", (long)(lens[0] + lens[1] + lens[2]), 15);
    check_int("W active 0", tc_composition_active(b), 0);
    check_true("W not browsing", !tc_composition_highlight(b));
    tk_osk_convert_next(u.osk);
    check_true("W browsing after convert", tc_composition_highlight(b));
    check_int("W still three segments",
              (long)tc_composition_segments(b, lens, 8), 3);
    tk_osk_commit(u.osk);  /* commits 今日, leaves はてんき */
    n = tc_composition_segments(b, lens, 8);
    check_int("W two segments after commit", (long)n, 2);
    check_true("W browsing cleared", !tc_composition_highlight(b));
    ui_free(&u);

    /* fallback backend: single segment spanning the composition */
    ui u2 = ui_new(NULL);
    tc_buffer *b2 = tk_textfield_buffer(u2.tf);
    tk_osk_input_kana(u2.osk, "てんき");
    char comp[256];
    size_t clen = tc_composition(b2, comp, sizeof(comp));
    n = tc_composition_segments(b2, lens, 8);
    check_int("W fallback one segment", (long)n, 1);
    check_int("W fallback seg spans", (long)lens[0], (long)clen);
    ui_free(&u2);

    /* direct events: segment info is stored; a tiling mismatch
     * falls back to one unsegmented composition */
    ui u3 = ui_new(NULL);
    tc_buffer *b3 = tk_textfield_buffer(u3.tf);
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_COMPOSITION;
    ev.text = "あい";
    size_t good[2] = { 3, 3 };
    ev.seg_lens = good;
    ev.seg_count = 2;
    ev.seg_active = 1;
    ev.seg_highlight = true;
    tk_dispatch(u3.tf, &ev);
    n = tc_composition_segments(b3, lens, 8);
    check_int("W direct two segments", (long)n, 2);
    check_int("W direct active", tc_composition_active(b3), 1);
    check_true("W direct highlight", tc_composition_highlight(b3));
    size_t bad[1] = { 3 };
    ev.seg_lens = bad;
    ev.seg_count = 1;
    ev.seg_active = 0;
    ev.seg_highlight = false;
    tk_dispatch(u3.tf, &ev);
    n = tc_composition_segments(b3, lens, 8);
    check_int("W mismatch falls back", (long)n, 1);
    check_int("W fallback spans all", (long)lens[0], 6);
    ui_free(&u3);
}

/* ---------------------------------------------------------------- */
/* Q/G/S: other modes                                                */
/* ---------------------------------------------------------------- */
static void group_qwerty(void)
{
    ui u = ui_new(NULL);
    tk_osk_set_mode(u.osk, OSK_MODE_QWERTY);
    check_true("Q tap k", tap_cell(&u, OSK_CELL_LETTER, -1, "k"));
    check_true("Q tap y", tap_cell(&u, OSK_CELL_LETTER, -1, "y"));
    check_true("Q tap o", tap_cell(&u, OSK_CELL_LETTER, -1, "o"));
    check_str("Q reading kyo", tk_osk_reading(u.osk), "きょ");
    check_true("Q backspace cell",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_BACKSPACE, NULL));
    check_str("Q after bs", tk_osk_reading(u.osk), "き");
    check_true("Q bs2",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_BACKSPACE, NULL));
    check_str("Q empty", tk_osk_reading(u.osk), "");
    check_true("Q tap n1", tap_cell(&u, OSK_CELL_LETTER, -1, "n"));
    check_true("Q tap n2", tap_cell(&u, OSK_CELL_LETTER, -1, "n"));
    check_str("Q nn -> n", tk_osk_reading(u.osk), "ん");
    ui_free(&u);
}

static void group_gojuon(void)
{
    ui u = ui_new(NULL);
    tk_osk_set_mode(u.osk, OSK_MODE_GOJUON);
    check_int("G starts on page 0", tk_osk_gojuon_page(u.osk), 0);
    /* every gojuon cell on the page is >= 56 px high and wide */
    osk_cell cells[256];
    int n = tk_osk_layout(u.osk, cells, 256);
    int ok = 1, cnt = 0;
    for (int i = 0; i < n; i++)
        if (cells[i].kind == OSK_CELL_GOJUON) {
            cnt++;
            if (cells[i].r.h < 56 || cells[i].r.w < 56) ok = 0;
        }
    check_true("G cells >=56px", ok);
    check_int("G page0 has 25 cells", cnt, 25);
    check_true("G tap su", tap_cell(&u, OSK_CELL_GOJUON, -1, "す"));
    check_true("G tap shi", tap_cell(&u, OSK_CELL_GOJUON, -1, "し"));
    /* は-row kana are NOT on page 0 */
    int cx, cy;
    check_true("G ha not on page0",
               !find_cell(&u, OSK_CELL_GOJUON, -1, "は", &cx, &cy));
    /* switch to page 1 via the bottom-bar page cell */
    check_true("G page switch",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_GOJUON_PAGE, NULL));
    check_int("G now page 1", tk_osk_gojuon_page(u.osk), 1);
    check_true("G tap n", tap_cell(&u, OSK_CELL_GOJUON, -1, "ん"));
    check_str("G reading", tk_osk_reading(u.osk), "すしん");
    /* page 1 cells are also >= 56 px */
    n = tk_osk_layout(u.osk, cells, 256);
    ok = 1; cnt = 0;
    for (int i = 0; i < n; i++)
        if (cells[i].kind == OSK_CELL_GOJUON) {
            cnt++;
            if (cells[i].r.h < 56 || cells[i].r.w < 56) ok = 0;
        }
    check_true("G page1 cells >=56px", ok);
    check_int("G page1 has 25 cells", cnt, 25);
    ui_free(&u);
}

static void group_symbol(void)
{
    ui u = ui_new(NULL);
    char buf[256];
    tk_osk_set_mode(u.osk, OSK_MODE_SYMBOL);
    check_int("S starts on page 0", tk_osk_symbol_page(u.osk), 0);
    check_true("S tap 1", tap_cell(&u, OSK_CELL_SYMBOL, -1, "1"));
    check_true("S page toggle",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_SYMBOL_PAGE, NULL));
    check_int("S now page 1", tk_osk_symbol_page(u.osk), 1);
    check_true("S tap !", tap_cell(&u, OSK_CELL_SYMBOL, -1, "!"));
    check_str("S reading", tk_osk_reading(u.osk), "1!");
    check_true("S commit",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_COMMIT, NULL));
    field_text(&u, buf, sizeof(buf));
    check_str("S committed", buf, "1!");
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* E: QWERTY is the primary layout (M3 policy)                       */
/* ---------------------------------------------------------------- */
static void group_qwerty_primary(void)
{
    /* a fresh widget starts in QWERTY, not in a kana layout */
    ui u = ui_new(NULL);
    check_int("E default mode is QWERTY", tk_osk_mode(u.osk),
              OSK_MODE_QWERTY);
    check_true("E starts in Japanese (romaji) sub-mode",
               !tk_osk_direct(u.osk));
    /* mode cycle: QWERTY -> SYMBOL -> GOJUON -> FLICK -> QWERTY */
    check_true("E mode cell 1",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_MODE, NULL));
    check_int("E cycle to symbol", tk_osk_mode(u.osk), OSK_MODE_SYMBOL);
    check_true("E mode cell 2",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_MODE, NULL));
    check_int("E cycle to gojuon", tk_osk_mode(u.osk), OSK_MODE_GOJUON);
    check_true("E mode cell 3",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_MODE, NULL));
    check_int("E cycle to flick", tk_osk_mode(u.osk), OSK_MODE_FLICK);
    check_true("E mode cell 4",
               tap_cell(&u, OSK_CELL_FUNC, OSK_FN_MODE, NULL));
    check_int("E cycle back to qwerty", tk_osk_mode(u.osk),
              OSK_MODE_QWERTY);
    ui_free(&u);

    /* English direct sub-mode + shift, on the fallback backend */
    ui u2 = ui_new(NULL);
    char buf[512];
    check_true("E lang cell",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_LANG, NULL));
    check_true("E direct on", tk_osk_direct(u2.osk));
    check_true("E tap h", tap_cell(&u2, OSK_CELL_LETTER, -1, "h"));
    check_true("E tap i", tap_cell(&u2, OSK_CELL_LETTER, -1, "i"));
    field_text(&u2, buf, sizeof(buf));
    check_str("E direct commits at once", buf, "hi");
    check_str("E no reading in direct", tk_osk_reading(u2.osk), "");
    /* shift one-shot: next letter is capital, then it disarms */
    check_true("E shift cell",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_SHIFT, NULL));
    check_int("E shift one-shot", tk_osk_shift_state(u2.osk), 1);
    check_true("E tap A", tap_cell(&u2, OSK_CELL_LETTER, -1, "A"));
    check_int("E shift consumed", tk_osk_shift_state(u2.osk), 0);
    /* shift lock: two taps; letters stay capital */
    check_true("E shift tap 1",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_SHIFT, NULL));
    check_true("E shift tap 2",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_SHIFT, NULL));
    check_int("E shift lock", tk_osk_shift_state(u2.osk), 2);
    check_true("E tap B", tap_cell(&u2, OSK_CELL_LETTER, -1, "B"));
    check_true("E tap C", tap_cell(&u2, OSK_CELL_LETTER, -1, "C"));
    check_int("E lock persists", tk_osk_shift_state(u2.osk), 2);
    field_text(&u2, buf, sizeof(buf));
    check_str("E direct text", buf, "hiABC");
    /* backspace in direct mode deletes in the target */
    check_true("E direct backspace",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_BACKSPACE, NULL));
    field_text(&u2, buf, sizeof(buf));
    check_str("E direct after bs", buf, "hiAB");
    /* disarm shift, back to Japanese: the romaji path resumes */
    check_true("E shift off",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_SHIFT, NULL));
    check_int("E shift off state", tk_osk_shift_state(u2.osk), 0);
    check_true("E lang back",
               tap_cell(&u2, OSK_CELL_FUNC, OSK_FN_LANG, NULL));
    check_true("E direct off", !tk_osk_direct(u2.osk));
    check_true("E tap k", tap_cell(&u2, OSK_CELL_LETTER, -1, "k"));
    check_true("E tap a", tap_cell(&u2, OSK_CELL_LETTER, -1, "a"));
    check_str("E romaji resumes", tk_osk_reading(u2.osk), "か");
    ui_free(&u2);

    /* geometry at the 800 px design width: every QWERTY input
     * key (letters, shift, backspace) is >= 56 px both ways */
    ui u3 = ui_new_w(NULL, 800);
    osk_cell cells[256];
    int n = tk_osk_layout(u3.osk, cells, 256);
    int letters = 0, ok = 1, shift_ok = 0, bs_ok = 0;
    for (int i = 0; i < n; i++) {
        if (cells[i].kind == OSK_CELL_LETTER) {
            letters++;
            if (cells[i].r.w < 56 || cells[i].r.h < 56) ok = 0;
        }
        if (cells[i].kind == OSK_CELL_FUNC &&
            cells[i].id == OSK_FN_SHIFT)
            shift_ok = cells[i].r.w >= 56 && cells[i].r.h >= 56;
        if (cells[i].kind == OSK_CELL_FUNC &&
            cells[i].id == OSK_FN_BACKSPACE)
            bs_ok = cells[i].r.w >= 56 && cells[i].r.h >= 56;
    }
    check_int("E 26 letter keys", letters, 26);
    check_true("E letters >=56px @800", ok);
    check_true("E shift >=56px @800", shift_ok);
    check_true("E backspace >=56px @800", bs_ok);
    ui_free(&u3);
}

/* ---------------------------------------------------------------- */
/* X: symbol pages — every character, through the real path          */
/* ---------------------------------------------------------------- */
static void group_symbol_full(void)
{
    check_int("X page count", osk_symbol_page_count(), 8);
    int total = 0;
    for (int page = 0; page < osk_symbol_page_count(); page++) {
        ui u = ui_new_w(NULL, 800);
        char what[80];
        tk_osk_set_mode(u.osk, OSK_MODE_SYMBOL);
        tk_osk_set_symbol_page(u.osk, page);
        snprintf(what, sizeof(what), "X page %d selected", page);
        check_int(what, tk_osk_symbol_page(u.osk), page);
        check_true("X page has a name",
                   osk_symbol_page_name(page)[0] != '\0');
        /* collect the page's symbol cells from the public layout */
        osk_cell cells[256];
        int n = tk_osk_layout(u.osk, cells, 256);
        char want[1024] = "";
        int cnt = 0, geom_ok = 1;
        for (int i = 0; i < n; i++) {
            if (cells[i].kind != OSK_CELL_SYMBOL) continue;
            cnt++;
            if (cells[i].r.w < 56 || cells[i].r.h < 56) geom_ok = 0;
            strcat(want, cells[i].text);
        }
        snprintf(what, sizeof(what), "X page %d has symbols", page);
        check_true(what, cnt > 0);
        snprintf(what, sizeof(what), "X page %d cells >=56px @800",
                 page);
        check_true(what, geom_ok);
        /* tap every cell on the page through the pointer path */
        int tapped = 1;
        for (int i = 0; i < n; i++) {
            if (cells[i].kind != OSK_CELL_SYMBOL) continue;
            if (!tap_cell(&u, OSK_CELL_SYMBOL, -1, cells[i].text))
                tapped = 0;
        }
        snprintf(what, sizeof(what), "X page %d all cells tappable",
                 page);
        check_true(what, tapped);
        snprintf(what, sizeof(what), "X page %d reading", page);
        check_str(what, tk_osk_reading(u.osk), want);
        check_true("X commit",
                   tap_cell(&u, OSK_CELL_FUNC, OSK_FN_COMMIT, NULL));
        char buf[1024];
        field_text(&u, buf, sizeof(buf));
        snprintf(what, sizeof(what), "X page %d committed", page);
        check_str(what, buf, want);
        total += cnt;
        ui_free(&u);
    }
    check_true("X total symbol chars >= 250", total >= 250);
    printf("X total symbol characters covered: %d\n", total);

    /* the page key cycles through all pages and wraps */
    ui u = ui_new(NULL);
    tk_osk_set_mode(u.osk, OSK_MODE_SYMBOL);
    int wrapped = 1;
    for (int i = 0; i < osk_symbol_page_count(); i++) {
        if (!tap_cell(&u, OSK_CELL_FUNC, OSK_FN_SYMBOL_PAGE, NULL))
            wrapped = 0;
    }
    check_true("X page key wraps to page 0",
               wrapped && tk_osk_symbol_page(u.osk) == 0);
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* L: layout cache / diff behaviour                                  */
/* ---------------------------------------------------------------- */
static void group_layout(bi_engine *eng)
{
    ui u = ui_new(eng);
    osk_cell cells[256];
    tk_osk_layout(u.osk, cells, 256);
    long b0 = tk_osk_layout_builds(u.osk);
    check_true("L initial builds >= 2", b0 >= 2);
    tk_osk_layout(u.osk, cells, 256);
    tk_osk_layout(u.osk, cells, 256);
    tk_canvas *cv = tk_canvas_new(480, 800);
    tk_root_paint(u.root, cv, tk_theme_default());
    tk_root_paint(u.root, cv, tk_theme_default());
    check_int("L no rebuild on repaint/layout",
              tk_osk_layout_builds(u.osk), b0);
    /* one keystroke: only the candidate cache rebuilds (+1) */
    tk_osk_input_kana(u.osk, "てんき");
    tk_osk_layout(u.osk, cells, 256);
    check_int("L keystroke rebuilds cand only",
              tk_osk_layout_builds(u.osk), b0 + 1);
    /* candidate selection: no rebuild at all */
    tk_osk_convert_next(u.osk);
    tk_osk_layout(u.osk, cells, 256);
    check_int("L choose rebuilds nothing",
              tk_osk_layout_builds(u.osk), b0 + 1);
    /* mode change: only the grid cache rebuilds (+1) */
    tk_osk_set_mode(u.osk, OSK_MODE_QWERTY);
    tk_osk_layout(u.osk, cells, 256);
    check_int("L mode rebuilds grid only",
              tk_osk_layout_builds(u.osk), b0 + 2);
    tk_osk_set_mode(u.osk, OSK_MODE_FLICK);
    /* flick key geometry: >=56 px high, wide keys */
    int n = tk_osk_layout(u.osk, cells, 256);
    int ok = 0;
    for (int i = 0; i < n; i++)
        if (cells[i].kind == OSK_CELL_KANA_KEY && cells[i].id == 0)
            ok = cells[i].r.h >= 56 && cells[i].r.w >= 100;
    check_true("L flick key >=56px", ok);
    /* paint smoke: the OSK area is actually drawn */
    tk_root_paint(u.root, cv, tk_theme_default());
    long nonbg = 0;
    tk_color bgc = tk_theme_default()->bg;
    for (int y = 100; y < 360; y += 3)
        for (int x = 0; x < 480; x += 3) {
            tk_color p = tk_canvas_pixel(cv, x, y);
            if (p.r != bgc.r || p.g != bgc.g || p.b != bgc.b) nonbg++;
        }
    check_true("L paint draws keys", nonbg > 200);
    tk_canvas_free(cv);
    ui_free(&u);
}

/* ---------------------------------------------------------------- */
/* P: learning persistence under the device's reality — power cuts   */
/* mid-write, torn tails, garbage files.  The engine must always     */
/* start; only bonuses may be lost.                                  */
/* ---------------------------------------------------------------- */
static int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}
static void write_file(const char *path, const char *data, int append)
{
    FILE *f = fopen(path, append ? "a" : "w");
    if (f) { fputs(data, f); fclose(f); }
}
static void group_persistence(const char *dict)
{
    const char *path = "/tmp/osk_learn_test.tsv";
    char tmppath[300];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    remove(path);
    remove(tmppath);
    char buf[512];

    /* 1. learn through the OSK: commit かみ choosing 髪 */
    bi_engine *e1 = bi_engine_open(dict);
    check_true("P e1 open", e1 != NULL);
    check_true("P enable learning",
               e1 && bi_engine_enable_learning(e1, path) == 0);
    ui u = ui_new(e1);
    tk_osk_input_kana(u.osk, "かみ");
    int n = tk_osk_candidate_count(u.osk);
    int kami_idx = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(tk_osk_candidate(u.osk, i), "髪")) kami_idx = i;
    check_true("P candidate kami exists", kami_idx > 0);
    for (int i = 0; i < kami_idx; i++) tk_osk_convert_next(u.osk);
    check_int("P chose kami", tk_osk_candidate_index(u.osk), kami_idx);
    tk_osk_commit(u.osk);
    field_text(&u, buf, sizeof(buf));
    check_str("P committed kami", buf, "髪");
    ui_free(&u);
    bi_engine_close(e1);
    check_true("P history file written", file_exists(path));
    check_true("P no tmp residue after save", !file_exists(tmppath));

    /* 2. reopen: history loads, engine converts */
    bi_engine *e2 = bi_engine_open(dict);
    check_true("P enable after normal save",
               e2 && bi_engine_enable_learning(e2, path) == 0);
    bi_stats st2;
    bi_engine_stats(e2, &st2);
    check_true("P entries persisted", st2.learn_entries >= 1);
    int saved_entries = st2.learn_entries;
    bi_result *r = bi_convert(e2, "きょうはてんき");
    check_str("P conversion works", r ? bi_result_nbest(r, 0) : NULL,
              "今日は天気");
    if (r) bi_result_free(r);
    bi_engine_close(e2);

    /* 3. torn tail + bad lines appended (simulated power cut):
     *    a complete-looking line without '\n', a non-numeric
     *    count line, and a garbage line must ALL be discarded,
     *    the valid prefix retained, and the engine must start. */
    write_file(path, "あ\tあ\tXYZ\n", 1);          /* bad count */
    write_file(path, "\x01\x02\t\t\n", 1);         /* garbage */
    /* torn tail LAST: a complete-looking line cut off by the
     * power loss before its '\n' was written */
    write_file(path, "てんき\t天気\t7", 1);      /* no trailing \n */
    bi_engine *e3 = bi_engine_open(dict);
    check_true("P enable with torn tail",
               e3 && bi_engine_enable_learning(e3, path) == 0);
    bi_stats st3;
    bi_engine_stats(e3, &st3);
    check_int("P torn/bad lines discarded", st3.learn_entries,
              saved_entries);
    r = bi_convert(e3, "きょうはてんき");
    check_str("P conversion after corruption",
              r ? bi_result_nbest(r, 0) : NULL, "今日は天気");
    if (r) bi_result_free(r);
    bi_engine_close(e3);

    /* 4. garbage-only file: engine starts with empty history */
    write_file(path, "\x89\x50\x4e\x47 garbage \t\t\t no newlines", 0);
    bi_engine *e4 = bi_engine_open(dict);
    check_true("P enable garbage file",
               e4 && bi_engine_enable_learning(e4, path) == 0);
    bi_stats st4;
    bi_engine_stats(e4, &st4);
    check_int("P garbage -> 0 entries", st4.learn_entries, 0);
    bi_engine_close(e4);

    /* 5. missing file + stale tmp residue */
    remove(path);
    write_file(tmppath, "residue of an interrupted save", 0);
    bi_engine *e5 = bi_engine_open(dict);
    check_true("P enable missing file",
               e5 && bi_engine_enable_learning(e5, path) == 0);
    check_true("P stale tmp removed", !file_exists(tmppath));
    bi_engine_close(e5);
    remove(path);
}

/* Height fit (M4): arranged shorter than natural (the greeter
 * leaves the OSK ~250 px), the key rows must shrink so every
 * cell - including the bottom bar's commit key - stays inside
 * the widget rect instead of painting off the bottom edge. */
static void group_height_fit(void)
{
    printf("-- height fit --\n");
    ui u;
    u.top = tk_fixed_new();
    u.tf = tk_textfield_new("");
    tk_set_fixed_rect(u.tf, (tk_rect){ 0, 0, 800, 44 });
    u.osk = tk_osk_new();
    tk_osk_set_mode(u.osk, OSK_MODE_QWERTY);
    tk_set_fixed_rect(u.osk, (tk_rect){ 0, 44, 800, 220 });
    tk_add_child(u.top, u.tf);
    tk_add_child(u.top, u.osk);
    tk_osk_attach(u.osk, u.tf);
    u.root = tk_root_new(u.top);
    tk_root_layout(u.root, 800, 264);
    osk_cell cells[256];
    int n = tk_osk_layout(u.osk, cells, 256);
    check_true("HF cells exist", n > 0);
    int max_bottom = 0, letter_h = 0;
    bool have_commit = false;
    for (int i = 0; i < n; i++) {
        int b = cells[i].r.y + cells[i].r.h;
        if (b > max_bottom) max_bottom = b;
        if (cells[i].kind == OSK_CELL_LETTER) letter_h = cells[i].r.h;
        if (cells[i].kind == OSK_CELL_FUNC && cells[i].id == OSK_FN_COMMIT)
            have_commit = true;
    }
    check_true("HF all cells inside widget", max_bottom <= 44 + 220);
    /* UI-REDESIGN height contract: candidate 48 + function bar
     * 56 leave 116 px for 3 rows in a 220 px widget; the 48/44
     * steps do not fit, so rows shrink proportionally to 38. */
    check_true("HF rows shrunk to fit", letter_h < 56 && letter_h >= 36);
    check_true("HF commit cell present", have_commit);
    ui_free(&u);

    /* and at natural height the rows keep their 56 px */
    ui u2 = ui_new(NULL);
    tk_osk_set_mode(u2.osk, OSK_MODE_QWERTY);
    tk_root_layout(u2.root, 480, 800);
    n = tk_osk_layout(u2.osk, cells, 256);
    letter_h = 0;
    for (int i = 0; i < n; i++)
        if (cells[i].kind == OSK_CELL_LETTER) letter_h = cells[i].r.h;
    check_int("HF natural rows stay 56", letter_h, 56);
    ui_free(&u2);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s dict.bidf [sidecar.bisc]\n", argv[0]);
        return 2;
    }
    if (argc >= 3) setenv("BI_SIDECAR", argv[2], 0);
    group_height_fit();
    group_romaji();
    group_flick_unit();
    group_fallback();
    group_sink();
    group_qwerty();
    group_qwerty_primary();
    group_gojuon();
    group_symbol();
    group_symbol_full();
    group_preview();
    bi_engine *eng = bi_engine_open(argv[1]);
    check_true("engine open", eng != NULL);
    if (eng) {
        group_engine(eng);
        group_keyboard(eng);
        group_unified(eng);
        group_segments(eng);
        group_layout(eng);
        bi_engine_close(eng);
    }
    group_persistence(argv[1]);
    printf("test_osk: %d passed, %d failed\n", passes, fails);
    return fails;
}
