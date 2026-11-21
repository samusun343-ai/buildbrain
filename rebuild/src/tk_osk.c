/* tk_osk.c - OSK v2 widget: on-screen keyboard + Japanese input
 * integration for libbrainui2.  Public API: include/osk.h.
 *
 * Design summary (see osk/REPORT.md for the full account):
 *  - One reading buffer, three input sources (on-screen cells,
 *    physical key events routed to this widget, romaji feeds).
 *  - Conversion is delegated to an injected osk_ime_backend
 *    session (libbrainime in production; a kana-only fallback is
 *    built in so the widget is usable and testable standalone).
 *  - The attached target editor receives TK_EV_COMPOSITION for
 *    preedit display and TK_EV_TEXT_INPUT for commits, exactly
 *    the events textcore-backed widgets already understand.
 *  - Layout is computed by one function (osk_build_cells) shared
 *    by painting, hit testing and the public tk_osk_layout().
 *  - Layout policy (M3): QWERTY is the primary/default layout
 *    (Japanese = romaji through the IME, English = direct
 *    sub-mode); the symbol pages are the "+alpha" superset of
 *    the physical keyboard; flick/gojuon are optional layouts.
 *    The input path stays integrated with the physical keyboard
 *    (same target events), but the OSK layout is not bound to
 *    the physical key arrangement.
 */
#define _POSIX_C_SOURCE 200809L
#include "tk_internal.h"
#include "osk.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */
/* Candidate bar 48 and bottom (function) bar 56: the height
 * contract of UI-REDESIGN 2.4 (candidate 48 + key rows 56 +
 * function row 56; rows compress 56 -> 48 -> 44 to fit, see
 * mode_row_h_fit). */
#define OSK_CAND_H   48
#define OSK_BAR_H    56
#define OSK_FLICK_ROW_H 60
#define OSK_FUNC_W   72
#define OSK_QWERTY_ROW_H 56
/* Gojuon cells are 56 px high like every other input key (M2).
 * The 10-row table is split into two 5-row pages (あ-な / は-わ)
 * to make that possible in the available height; see REPORT-m2. */
#define OSK_GOJUON_ROW_H 56
#define OSK_GOJUON_ROWS 5     /* rows per page */
#define OSK_SYM_ROW_H 56
#define OSK_TAP_THRESHOLD 14   /* px; below this a gesture is a tap */
#define OSK_DEFAULT_W 480
/* Flick press preview popup: a 3x3 cross of 52 px cells. */
#define OSK_PREVIEW_CELL 52
/* Hard caps for the two layout caches.  These are fixed-size
 * arrays inside the (single-allocation) widget state: nothing in
 * the OSK layer accumulates without bound, and the eviction
 * policy is generation invalidation — a cache window is replaced
 * wholesale when its inputs change, never appended to. */
#define OSK_MAX_GRID_CELLS 160  /* key grid + bottom bar (max: symbol 45) */
#define OSK_MAX_CAND_CELLS 40   /* candidate bar window (max seen: ~12) */
#define OSK_READ_MAX 512

/* Internal layout cell: label/text are POINTERS (into static
 * tables, string literals, or session-owned candidate storage) —
 * the paint/hit-test hot path copies no strings at all.  Only the
 * public tk_osk_layout() snapshot materialises char arrays.
 * `buf` backs the one synthesised case (QWERTY letter cells). */
typedef struct {
    tk_rect r;
    osk_cell_kind kind;
    int id;
    const char *label;
    const char *text;
    char buf[4];
} osk_icell;

typedef struct {
    tk_widget *target;
    const osk_ime_backend *be;
    void *engine;
    void *session;
    char reading[OSK_READ_MAX];
    size_t reading_len;      /* strlen(reading), maintained */
    /* Current conversion text: a pointer into session-owned
     * storage, refreshed after every mutating backend call.
     * Never copied per keystroke; valid until the next mutation,
     * and every consumer (composition dispatch) is synchronous. */
    const char *conv;
    osk_romaji romaji;
    osk_mode mode;
    /* QWERTY sub-state (M3): direct = English mode (letters are
     * committed to the target as-is, no romaji/IME round trip);
     * shift = on-screen shift for the QWERTY grid
     * (0 off, 1 one-shot, 2 lock). */
    bool direct;
    int shift;
    bool toggle_mode;
    int toggle_key;          /* flick key being cycled, -1 none */
    int toggle_idx;          /* cycle position within the key */
    int cand_index;          /* chosen candidate for segment 0 */
    int cand_page;
    int sym_page;
    int gojuon_page;         /* 0: あ-な行, 1: は-わ行 */
    /* Candidate-browsing state: true between a candidate choice
     * (convert key / candidate tap) and the next reconversion,
     * commit or cancel.  Carried into the composition event as
     * seg_highlight so editors render the active segment as
     * selected while the user is choosing. */
    bool cand_browsing;
    /* pointer press tracking (cell copied at DOWN) */
    bool press_active;
    bool press_valid;
    osk_icell press_cell;
    int press_x, press_y;
    int cur_x, cur_y;        /* latest pointer position while held */
    long last_convert_us;
    /* Two independent layout caches, so a candidate update never
     * rebuilds the key grid and a mode change never rebuilds the
     * candidate bar:
     *   cand_cells — depends on reading/candidates/cand_page.
     *   grid_cells — depends on mode/sym_page/toggle label.
     * Each is rebuilt only when its own generation counter or the
     * widget rect changes.  Candidate *selection* changes bump
     * neither (paint reads cand_index live), so a candidate tap
     * costs no re-layout and no re-measure at all. */
    osk_icell cand_cells[OSK_MAX_CAND_CELLS];
    int n_cand_cells;
    osk_icell grid_cells[OSK_MAX_GRID_CELLS];
    int n_grid_cells;
    unsigned cand_gen, grid_gen;
    unsigned layout_cand_gen, layout_grid_gen;
    /* The candidate bar's CONTENT also depends on whether the
     * symbol category chips own it (symbol mode, no reading) and
     * which symbol page is selected; those ride in the cache key
     * instead of bumping cand_gen, so a mode change that does not
     * affect the bar (e.g. flick -> qwerty mid-composition) still
     * rebuilds the grid only. */
    int layout_cand_sym;     /* symbol mode at last cand build */
    int layout_cand_page;    /* sym_page at last cand build */
    tk_rect layout_cand_rect, layout_grid_rect;
    long layout_builds;      /* diagnostics: actual cache rebuilds */
} osk_state;

static osk_state *osk_st(tk_widget *w)
{
    return (w && w->kind == TK_W_OSK) ? (osk_state *)w->ext : NULL;
}

/* ------------------------------------------------------------------ */
/* Small UTF-8 helpers (all kana handled here are 3-byte sequences,    */
/* but the helpers are written generically)                            */
/* ------------------------------------------------------------------ */
static size_t u8_len(const char *s)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int u8_count(const char *s)
{
    int n = 0;
    while (*s) { s += u8_len(s); n++; }
    return n;
}

/* Byte offset of the last codepoint, or -1 when empty. */
static int u8_last_off(const char *s)
{
    int last = -1;
    size_t i = 0;
    while (s[i]) { last = (int)i; i += u8_len(s + i); }
    return last;
}

static void str_append(char *dst, size_t cap, const char *src)
{
    size_t dl = strlen(dst), sl = strlen(src);
    if (dl + sl + 1 > cap) sl = cap - dl - 1;
    memcpy(dst + dl, src, sl);
    dst[dl + sl] = '\0';
}

/* Reading-buffer helpers that maintain st->reading_len, so the
 * per-keystroke append path never rescans the string. */
static void osk_reading_set(osk_state *st, const char *s)
{
    if (!s) s = "";
    size_t l = strlen(s);
    if (l >= sizeof(st->reading)) l = sizeof(st->reading) - 1;
    memcpy(st->reading, s, l);
    st->reading[l] = '\0';
    st->reading_len = l;
}

static void osk_reading_append(osk_state *st, const char *s)
{
    size_t sl = strlen(s);
    if (st->reading_len + sl + 1 > sizeof(st->reading))
        sl = sizeof(st->reading) - st->reading_len - 1;
    memcpy(st->reading + st->reading_len, s, sl);
    st->reading_len += sl;
    st->reading[st->reading_len] = '\0';
}

/* hiragana -> katakana copy (U+3041..U+3096 shift by 0x60) */
static void to_katakana(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    while (*in && o + 4 < cap) {
        size_t l = u8_len(in);
        if (l == 3) {
            uint32_t cp = ((uint32_t)(in[0] & 0x0F) << 12) |
                          ((uint32_t)(in[1] & 0x3F) << 6) |
                          (uint32_t)(in[2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096) {
                cp += 0x60;
                out[o++] = (char)(0xE0 | (cp >> 12));
                out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[o++] = (char)(0x80 | (cp & 0x3F));
                in += 3;
                continue;
            }
        }
        memcpy(out + o, in, l);
        o += l;
        in += l;
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* Flick tables: [key][dir] with dir order CENTER, LEFT, UP, RIGHT,     */
/* DOWN.  The same order is the multi-tap cycle order.                 */
/* Keys 10 (symbols) and 11 (dakuten/small) are action keys and        */
/* return NULL here; they are handled specially.                       */
/* ------------------------------------------------------------------ */
static const char *const FLICK[10][OSK_NDIR] = {
    { "あ", "い", "う", "え", "お" },
    { "か", "き", "く", "け", "こ" },
    { "さ", "し", "す", "せ", "そ" },
    { "た", "ち", "つ", "て", "と" },
    { "な", "に", "ぬ", "ね", "の" },
    { "は", "ひ", "ふ", "へ", "ほ" },
    { "ま", "み", "む", "め", "も" },
    { "や", "ゃ", "ゆ", "ょ", "よ" },
    { "ら", "り", "る", "れ", "ろ" },
    { "わ", "を", "ん", "ー", "ゎ" },
};

const char *osk_flick_kana(int key, osk_dir dir)
{
    if (key < 0 || key > 9 || dir < 0 || dir >= OSK_NDIR) return NULL;
    return FLICK[key][dir];
}

const char *osk_flick_key_label(int key)
{
    static const char *const labels[12] = {
        "あ", "か", "さ", "た", "な", "は", "ま", "や", "ら", "わ",
        "、", "゛"
    };
    if (key < 0 || key > 11) return "";
    return labels[key];
}

osk_dir osk_flick_classify(int dx, int dy, int tap_threshold_px)
{
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    if (ax < tap_threshold_px && ay < tap_threshold_px)
        return OSK_DIR_CENTER;
    if (ax > ay) return dx > 0 ? OSK_DIR_RIGHT : OSK_DIR_LEFT;
    return dy > 0 ? OSK_DIR_DOWN : OSK_DIR_UP;
}

/* ------------------------------------------------------------------ */
/* Dakuten / handakuten / small-kana transforms on the last kana of    */
/* the reading (all targets are 3-byte UTF-8, replaced in place).      */
/* ------------------------------------------------------------------ */
struct kana_pair { const char *plain, *marked; };
static const struct kana_pair DAKUTEN[] = {
    {"か","が"},{"き","ぎ"},{"く","ぐ"},{"け","げ"},{"こ","ご"},
    {"さ","ざ"},{"し","じ"},{"す","ず"},{"せ","ぜ"},{"そ","ぞ"},
    {"た","だ"},{"ち","ぢ"},{"つ","づ"},{"て","で"},{"と","ど"},
    {"は","ば"},{"ひ","び"},{"ふ","ぶ"},{"へ","べ"},{"ほ","ぼ"},
    {"う","ゔ"},
};
static const struct kana_pair HANDAKUTEN[] = {
    {"は","ぱ"},{"ひ","ぴ"},{"ふ","ぷ"},{"へ","ぺ"},{"ほ","ぽ"},
};
static const struct kana_pair SMALL[] = {
    {"あ","ぁ"},{"い","ぃ"},{"う","ぅ"},{"え","ぇ"},{"お","ぉ"},
    {"や","ゃ"},{"ゆ","ゅ"},{"よ","ょ"},{"つ","っ"},{"わ","ゎ"},
};

static const char *pair_find(const struct kana_pair *tab, int n,
                             const char *cp, int field)
{
    for (int i = 0; i < n; i++) {
        const char *a = field == 0 ? tab[i].plain : tab[i].marked;
        if (!strncmp(cp, a, 3)) return field == 0 ? tab[i].marked
                                                  : tab[i].plain;
    }
    return NULL;
}

enum { TR_DAKUTEN, TR_HANDAKUTEN, TR_SMALL };
static void osk_transform_last(osk_state *st, int tr)
{
    int off = u8_last_off(st->reading);
    if (off < 0) return;
    char *cp = st->reading + off;
    if (u8_len(cp) != 3) return;
    const char *rep = NULL;
    if (tr == TR_SMALL) {
        rep = pair_find(SMALL, 10, cp, 0);
        if (!rep) rep = pair_find(SMALL, 10, cp, 1);
    } else if (tr == TR_HANDAKUTEN) {
        rep = pair_find(HANDAKUTEN, 5, cp, 0);
        if (!rep) rep = pair_find(HANDAKUTEN, 5, cp, 1);
    } else {
        /* plain -> dakuten -> (は行 only) handakuten -> plain */
        rep = pair_find(DAKUTEN, 21, cp, 0);
        if (!rep) {
            const char *plain = pair_find(DAKUTEN, 21, cp, 1);
            if (plain) {
                const char *h = pair_find(HANDAKUTEN, 5, plain, 0);
                rep = h ? h : plain;
            }
        }
        if (!rep) rep = pair_find(HANDAKUTEN, 5, cp, 1);
    }
    if (rep) memcpy(cp, rep, 3);
}

/* ------------------------------------------------------------------ */
/* Romaji -> kana                                                      */
/* ------------------------------------------------------------------ */
struct romaji_ent { const char *r, *k; };
static const struct romaji_ent ROMAJI[] = {
    {"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"},
    {"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},
    {"kya","きゃ"},{"kyu","きゅ"},{"kyo","きょ"},
    {"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
    {"gya","ぎゃ"},{"gyu","ぎゅ"},{"gyo","ぎょ"},
    {"sa","さ"},{"si","し"},{"su","す"},{"se","せ"},{"so","そ"},
    {"shi","し"},{"sha","しゃ"},{"shu","しゅ"},{"sho","しょ"},
    {"sya","しゃ"},{"syu","しゅ"},{"syo","しょ"},
    {"za","ざ"},{"zi","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},
    {"ji","じ"},{"ja","じゃ"},{"ju","じゅ"},{"jo","じょ"},
    {"jya","じゃ"},{"jyu","じゅ"},{"jyo","じょ"},
    {"zya","じゃ"},{"zyu","じゅ"},{"zyo","じょ"},
    {"ta","た"},{"ti","ち"},{"tu","つ"},{"te","て"},{"to","と"},
    {"chi","ち"},{"tsu","つ"},{"cha","ちゃ"},{"chu","ちゅ"},{"cho","ちょ"},
    {"tya","ちゃ"},{"tyu","ちゅ"},{"tyo","ちょ"},
    {"cya","ちゃ"},{"cyu","ちゅ"},{"cyo","ちょ"},
    {"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
    {"dya","ぢゃ"},{"dyu","ぢゅ"},{"dyo","ぢょ"},
    {"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},
    {"nya","にゃ"},{"nyu","にゅ"},{"nyo","にょ"},
    {"ha","は"},{"hi","ひ"},{"hu","ふ"},{"he","へ"},{"ho","ほ"},
    {"fu","ふ"},{"fa","ふぁ"},{"fi","ふぃ"},{"fe","ふぇ"},{"fo","ふぉ"},
    {"hya","ひゃ"},{"hyu","ひゅ"},{"hyo","ひょ"},
    {"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},
    {"bya","びゃ"},{"byu","びゅ"},{"byo","びょ"},
    {"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
    {"pya","ぴゃ"},{"pyu","ぴゅ"},{"pyo","ぴょ"},
    {"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},
    {"mya","みゃ"},{"myu","みゅ"},{"myo","みょ"},
    {"ya","や"},{"yu","ゆ"},{"yo","よ"},
    {"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},
    {"rya","りゃ"},{"ryu","りゅ"},{"ryo","りょ"},
    {"wa","わ"},{"wi","うぃ"},{"we","うぇ"},{"wo","を"},
    {"nn","ん"},
    {"va","ゔぁ"},{"vi","ゔぃ"},{"vu","ゔ"},{"ve","ゔぇ"},{"vo","ゔぉ"},
    {"xa","ぁ"},{"xi","ぃ"},{"xu","ぅ"},{"xe","ぇ"},{"xo","ぉ"},
    {"xya","ゃ"},{"xyu","ゅ"},{"xyo","ょ"},{"xtu","っ"},
    {"la","ぁ"},{"li","ぃ"},{"lu","ぅ"},{"le","ぇ"},{"lo","ぉ"},
    {"lya","ゃ"},{"lyu","ゅ"},{"lyo","ょ"},{"ltu","っ"},
    {"lya","ゃ"},
};
#define N_ROMAJI (sizeof(ROMAJI) / sizeof(ROMAJI[0]))

void osk_romaji_init(osk_romaji *r) { r->plen = 0; r->pending[0] = '\0'; }
bool osk_romaji_pending(const osk_romaji *r) { return r->plen > 0; }

static bool romaji_is_consonant(char c)
{
    return c && strchr("bcdfghjklmnpqrstvwxyz", c) != NULL;
}

/* Try to resolve the pending fragment; appends kana to out.
 * Returns false when the fragment must wait for more input. */
static bool romaji_resolve(osk_romaji *r, char *out, size_t outsz)
{
    /* exact match? */
    for (size_t i = 0; i < N_ROMAJI; i++)
        if ((int)strlen(ROMAJI[i].r) == r->plen &&
            !strncmp(ROMAJI[i].r, r->pending, r->plen)) {
            str_append(out, outsz, ROMAJI[i].k);
            r->plen = 0; r->pending[0] = '\0';
            return true;
        }
    /* proper prefix of some entry? -> wait */
    for (size_t i = 0; i < N_ROMAJI; i++)
        if ((int)strlen(ROMAJI[i].r) > r->plen &&
            !strncmp(ROMAJI[i].r, r->pending, r->plen))
            return false;
    /* longest entry that is a proper prefix -> emit, keep the rest */
    int best = 0; const char *bk = NULL;
    for (size_t i = 0; i < N_ROMAJI; i++) {
        int el = (int)strlen(ROMAJI[i].r);
        if (el < r->plen && el > best &&
            !strncmp(ROMAJI[i].r, r->pending, el)) {
            best = el; bk = ROMAJI[i].k;
        }
    }
    if (bk) {
        str_append(out, outsz, bk);
        memmove(r->pending, r->pending + best, r->plen - best);
        r->plen -= best;
        r->pending[r->plen] = '\0';
        return true;
    }
    /* "n" before a consonant (not y) -> ん */
    if (r->plen >= 2 && r->pending[0] == 'n' &&
        romaji_is_consonant(r->pending[1]) && r->pending[1] != 'y') {
        str_append(out, outsz, "ん");
        memmove(r->pending, r->pending + 1, r->plen - 1);
        r->plen -= 1;
        r->pending[r->plen] = '\0';
        return true;
    }
    /* doubled consonant -> っ (keep one for the syllable) */
    if (r->plen >= 2 && r->pending[0] == r->pending[1] &&
        romaji_is_consonant(r->pending[0]) && r->pending[0] != 'n') {
        str_append(out, outsz, "っ");
        memmove(r->pending, r->pending + 1, r->plen - 1);
        r->plen -= 1;
        r->pending[r->plen] = '\0';
        return true;
    }
    /* "tch..." -> っ + ch... */
    if (r->plen >= 3 && !strncmp(r->pending, "tch", 3)) {
        str_append(out, outsz, "っ");
        memmove(r->pending, r->pending + 1, r->plen - 1);
        r->plen -= 1;
        r->pending[r->plen] = '\0';
        return true;
    }
    /* fallback: emit the first char literally and continue */
    if (r->plen > 0) {
        char one[2] = { r->pending[0], '\0' };
        str_append(out, outsz, one);
        memmove(r->pending, r->pending + 1, r->plen - 1);
        r->plen -= 1;
        r->pending[r->plen] = '\0';
        return true;
    }
    return false;
}

int osk_romaji_putc(osk_romaji *r, char c, char *out, size_t outsz)
{
    if (outsz) out[0] = '\0';
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c == '-' || c == ',' || c == '.') {
        /* resolve a held "n" first, then the punctuation itself */
        if (r->plen == 1 && r->pending[0] == 'n') {
            str_append(out, outsz, "ん");
            r->plen = 0; r->pending[0] = '\0';
        } else if (r->plen > 0) {
            str_append(out, outsz, r->pending);
            r->plen = 0; r->pending[0] = '\0';
        }
        str_append(out, outsz, c == '-' ? "ー" : c == ',' ? "、" : "。");
        return (int)strlen(out);
    }
    if (r->plen >= 7) { /* pathological: flush one char literally */
        char one[2] = { r->pending[0], '\0' };
        str_append(out, outsz, one);
        memmove(r->pending, r->pending + 1, r->plen - 1);
        r->plen -= 1;
        r->pending[r->plen] = '\0';
    }
    r->pending[r->plen++] = c;
    r->pending[r->plen] = '\0';
    while (r->plen > 0 && romaji_resolve(r, out, outsz))
        ;
    return (int)strlen(out);
}

int osk_romaji_flush(osk_romaji *r, char *out, size_t outsz)
{
    if (outsz) out[0] = '\0';
    if (r->plen == 1 && r->pending[0] == 'n')
        str_append(out, outsz, "ん");
    else if (r->plen > 0)
        str_append(out, outsz, r->pending);
    r->plen = 0; r->pending[0] = '\0';
    return (int)strlen(out);
}

/* ------------------------------------------------------------------ */
/* Symbol pages (M3): the "+alpha" of the OSK.  The physical Brain    */
/* keyboard (JIS, see tk_kbd.c) directly produces ASCII letters,      */
/* digits and the halfwidth ASCII symbols only; everything below      */
/* exists so the OSK is a strict superset of it.  Pages are           */
/* categories, 4 rows x 10 columns; the last cell of row 3 is the     */
/* page-cycle key (emitted by the builder, not the table), and a      */
/* NULL entry leaves the position empty.  The full inventory with     */
/* per-character physical-key reachability is in osk/REPORT-m3.md.    */
/* ------------------------------------------------------------------ */
#define OSK_SYMBOL_PAGES 8
static const char *const SYM_PAGE_NAMES[OSK_SYMBOL_PAGES] = {
    "基本", "半角", "全角", "単位", "アクセント", "大文字",
    "ギリシャ", "ギリシャ大"
};
static const char *const SYM_PAGES[OSK_SYMBOL_PAGES][4][10] = {
    {   /* 0 基本: digits, Japanese punctuation, common operators */
        {"1","2","3","4","5","6","7","8","9","0"},
        {"、","。","・","ー","…","‥","！","？","：","；"},
        {"＋","－","＝","／","＼","％","＃","＆","＊","＠"},
        {"（","）","「","」","『","』","［","］","｛",NULL},
    },
    {   /* 1 半角: the complete halfwidth ASCII symbol set + marks */
        {"!","\"","#","$","%","&","'","(",")","*"},
        {"+",",","-",".","/",":",";","<","=",">"},
        {"?","@","[","\\","]","^","_","`","{","|"},
        {"}","~","§","¶","©","®","™","•","—",NULL},
    },
    {   /* 2 全角: fullwidth forms, brackets, Japanese text marks */
        {"【","】","〈","〉","《","》","〔","〕","«","»"},
        {"‹","›","＂","＇","｀","＾","～","＿","｜","￥"},
        {"＄","，","．","＜","＞","※","゛","゜","々","〃"},
        {"ゝ","ゞ","ヽ","ヾ","｟","｠","　",NULL,NULL,NULL},
    },
    {   /* 3 単位: arrows, currency, units, math, shapes */
        {"←","→","↑","↓","↖","↗","↘","↙","⇐","⇒"},
        {"¥","€","$","£","¢","°","℃","℉","′","″"},
        {"−","×","÷","≠","≤","≥","±","∞","√","∑"},
        {"●","○","◎","★","☆","♪","▲","△","▼",NULL},
    },
    {   /* 4 アクセント: accented Latin, lowercase (+ first caps) */
        {"à","á","â","ä","ã","å","æ","ç","è","é"},
        {"ê","ë","ì","í","î","ï","ñ","ò","ó","ô"},
        {"ö","õ","ø","ù","ú","û","ü","ý","ÿ","ß"},
        {"À","Á","Â","Ä","Ã","Å","Æ","Ç","È",NULL},
    },
    {   /* 5 大文字: remaining accented capitals + Latin extended */
        {"É","Ê","Ë","Ì","Í","Î","Ï","Ñ","Ò","Ó"},
        {"Ô","Ö","Õ","Ø","Ù","Ú","Û","Ü","Ý","Ÿ"},
        {"ā","ē","ī","ō","ū","Ā","Ē","Ī","Ō","Ū"},
        {"œ","Œ","ł","Ł","đ","Đ","ħ","Ħ","Š",NULL},
    },
    {   /* 6 ギリシャ: Greek lowercase (+ first capitals) */
        {"α","β","γ","δ","ε","ζ","η","θ","ι","κ"},
        {"λ","μ","ν","ξ","π","ρ","σ","τ","υ","φ"},
        {"χ","ψ","ω","ς","Α","Β","Γ","Δ","Ε","Ζ"},
        {"Η","Θ","Ι","Κ","Λ","Μ","Ν","Ξ","Π",NULL},
    },
    {   /* 7 ギリシャ大: remaining capitals + letter variants */
        {"Ρ","Σ","Τ","Υ","Φ","Χ","Ψ","Ω","ϑ","ϕ"},
        {"ϖ","ϰ","ϱ","ϵ","ϒ","ϓ","ϔ","µ",NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
    },
};

int osk_symbol_page_count(void) { return OSK_SYMBOL_PAGES; }

const char *osk_symbol_page_name(int page)
{
    if (page < 0 || page >= OSK_SYMBOL_PAGES) return "";
    return SYM_PAGE_NAMES[page];
}

/* ------------------------------------------------------------------ */
/* Built-in fallback backend: no dictionary, one segment, candidates   */
/* are the reading itself and its katakana form.  Exists so the        */
/* widget works (and is testable) before an engine is attached.        */
/* ------------------------------------------------------------------ */
typedef struct {
    char reading[OSK_READ_MAX];
    char kata[OSK_READ_MAX];
    int chosen;
} fb_session;

static void *fb_new(void *engine)
{
    (void)engine;
    return calloc(1, sizeof(fb_session));
}
static void fb_free(void *s) { free(s); }
static int fb_set(void *s, const char *kana)
{
    fb_session *f = s;
    snprintf(f->reading, sizeof(f->reading), "%s", kana ? kana : "");
    to_katakana(f->reading, f->kata, sizeof(f->kata));
    f->chosen = 0;
    return 0;
}
static const char *fb_text(void *s)
{
    fb_session *f = s;
    return f->chosen == 1 ? f->kata : f->reading;
}
static const char *fb_preedit(void *s) { return ((fb_session *)s)->reading; }
static int fb_segcount(void *s) { return ((fb_session *)s)->reading[0] ? 1 : 0; }
static const char *fb_segreading(void *s, int seg)
{
    (void)seg;
    return ((fb_session *)s)->reading;
}
static const char *fb_segtext(void *s, int seg)
{
    (void)seg;
    return fb_text(s);  /* single segment: the whole conversion */
}
static int fb_candcount(void *s, int seg)
{
    (void)seg;
    return ((fb_session *)s)->reading[0] ? 2 : 0;
}
static const char *fb_cand(void *s, int seg, int j)
{
    fb_session *f = s;
    (void)seg;
    return j == 1 ? f->kata : f->reading;
}
static int fb_choose(void *s, int seg, int j)
{
    (void)seg;
    ((fb_session *)s)->chosen = j;
    return 0;
}
static int fb_resize(void *s, int seg, int len)
{
    (void)s; (void)seg; (void)len;
    return -1; /* single segment cannot be resized */
}
static int fb_commit(void *s, char *out, size_t outsz)
{
    fb_session *f = s;
    const char *t = fb_text(s);
    if (!t[0]) return -1;
    snprintf(out, outsz, "%s", t);
    f->reading[0] = '\0';
    f->kata[0] = '\0';
    f->chosen = 0;
    return (int)strlen(out);
}
static const osk_ime_backend FB_BACKEND = {
    .session_new = fb_new, .session_free = fb_free,
    .session_set_reading = fb_set, .session_text = fb_text,
    .session_preedit = fb_preedit, .segment_count = fb_segcount,
    .segment_reading = fb_segreading, .segment_text = fb_segtext,
    .candidate_count = fb_candcount,
    .candidate = fb_cand, .choose = fb_choose, .resize = fb_resize,
    .commit_first = fb_commit,
};

/* ------------------------------------------------------------------ */
/* Session plumbing                                                    */
/* ------------------------------------------------------------------ */
static void osk_push_composition(osk_state *st)
{
    if (!st->target) return;
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_COMPOSITION;
    /* st->conv points into session storage; the dispatch below is
     * synchronous and the editor copies the string into textcore
     * inside the call — that single boundary copy is the only one
     * on the OSK -> editor path. */
    ev.text = st->reading[0] ? st->conv : NULL;
    /* Per-segment byte lengths for the editors' underline
     * rendering (M2).  The lengths live on this stack frame and
     * are borrowed by the event only for the synchronous dispatch
     * below; textcore copies them.  Any inconsistency (backend
     * without segment_text, segment surfaces not tiling the
     * conversion text) falls back to a single-segment event. */
    size_t seg_lens[32];
    if (ev.text && st->be->segment_text) {
        int n = st->be->segment_count(st->session);
        size_t sum = 0;
        bool ok = n > 0 && n <= 32;
        for (int i = 0; ok && i < n; i++) {
            const char *t = st->be->segment_text(st->session, i);
            if (!t) { ok = false; break; }
            seg_lens[i] = strlen(t);
            sum += seg_lens[i];
        }
        if (ok && sum == strlen(ev.text)) {
            ev.seg_lens = seg_lens;
            ev.seg_count = n;
            ev.seg_active = 0;   /* the OSK operates on segment 0 */
            ev.seg_highlight = st->cand_browsing;
        }
    }
    tk_dispatch(st->target, &ev);
}

static void osk_push_text(osk_state *st, const char *text)
{
    if (!st->target || !text || !text[0]) return;
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT;
    ev.text = text;
    tk_dispatch(st->target, &ev);
}

static void osk_push_key(osk_state *st, tk_key key)
{
    if (!st->target) return;
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN;
    ev.key = key;
    tk_dispatch(st->target, &ev);
}

/* Reconvert the current reading and refresh the target preedit. */
static void osk_sync(osk_state *st)
{
    if (!st->session) return;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    st->be->session_set_reading(st->session, st->reading);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    st->last_convert_us = (long)(t1.tv_sec - t0.tv_sec) * 1000000L +
                          (t1.tv_nsec - t0.tv_nsec) / 1000;
    /* No copy: keep the session-owned pointer. */
    st->conv = st->be->session_text(st->session);
    st->cand_index = 0;
    st->cand_page = 0;
    st->cand_browsing = false;    /* fresh conversion: not browsing */
    st->cand_gen++;               /* candidate list changed -> re-layout */
    osk_push_composition(st);
}

/* Seal any pending romaji fragment into the reading.  With
 * sync=false the caller is about to append/sync itself, so the
 * seal merges into that single reconversion instead of paying
 * for a second one. */
static void osk_seal_romaji(osk_state *st, bool sync)
{
    if (!osk_romaji_pending(&st->romaji)) return;
    char out[32];
    osk_romaji_flush(&st->romaji, out, sizeof(out));
    if (out[0]) {
        osk_reading_append(st, out);
        if (sync) osk_sync(st);
    }
}

static void osk_seal_toggle(osk_state *st) { st->toggle_key = -1; }

/* ------------------------------------------------------------------ */
/* Public widget API                                                   */
/* ------------------------------------------------------------------ */
tk_widget *tk_osk_new(void)
{
    tk_widget *w = tk_widget_new(TK_W_OSK);
    if (!w) return NULL;
    osk_state *st = calloc(1, sizeof(osk_state));
    if (!st) { free(w); return NULL; }
    st->be = &FB_BACKEND;
    st->session = FB_BACKEND.session_new(NULL);
    st->mode = OSK_MODE_QWERTY;   /* primary layout (M3) */
    st->layout_cand_gen = ~0u;   /* force first layout build */
    st->layout_grid_gen = ~0u;
    st->toggle_key = -1;
    w->ext = st;
    w->can_focus = true;
    return w;
}

void tk_osk_free_state(void *ext)
{
    osk_state *st = ext;
    if (!st) return;
    if (st->session && st->be) st->be->session_free(st->session);
    st->session = NULL;
}

void tk_osk_attach(tk_widget *osk, tk_widget *target)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    st->target = target;
    if (st->reading[0]) osk_push_composition(st);
}

tk_widget *tk_osk_target(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->target : NULL;
}

void tk_osk_set_backend(tk_widget *osk, const osk_ime_backend *be,
                        void *engine)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    if (st->session && st->be) st->be->session_free(st->session);
    st->be = be ? be : &FB_BACKEND;
    st->engine = engine;
    st->session = st->be->session_new(engine);
    if (st->reading[0]) osk_sync(st);
}

bool tk_osk_has_backend(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st && st->be != &FB_BACKEND;
}

void tk_osk_set_mode(tk_widget *osk, osk_mode m)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    osk_seal_toggle(st);
    st->mode = m;
    st->sym_page = 0;
    st->gojuon_page = 0;
    st->shift = 0;
    st->grid_gen++;
}

void tk_osk_set_direct(tk_widget *osk, bool direct)
{
    osk_state *st = osk_st(osk);
    if (!st || st->direct == direct) return;
    st->direct = direct;
    st->grid_gen++;               /* LANG/space bar labels change */
}

bool tk_osk_direct(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->direct : false;
}

int tk_osk_shift_state(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->shift : 0;
}

int tk_osk_symbol_page(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->sym_page : 0;
}

void tk_osk_set_symbol_page(tk_widget *osk, int page)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    page %= OSK_SYMBOL_PAGES;
    if (page < 0) page += OSK_SYMBOL_PAGES;
    if (st->sym_page == page) return;
    st->sym_page = page;
    st->grid_gen++;
}

int tk_osk_gojuon_page(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->gojuon_page : 0;
}

void tk_osk_set_gojuon_page(tk_widget *osk, int page)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    page = page ? 1 : 0;
    if (st->gojuon_page == page) return;
    st->gojuon_page = page;
    st->grid_gen++;
}

osk_mode tk_osk_mode(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->mode : OSK_MODE_FLICK;
}

void tk_osk_set_toggle_mode(tk_widget *osk, bool toggle)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    st->toggle_mode = toggle;
    osk_seal_toggle(st);
    st->grid_gen++;               /* the 打鍵方式 cell label changes */
}

bool tk_osk_toggle_mode(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->toggle_mode : false;
}

const char *tk_osk_reading(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->reading : "";
}

const char *tk_osk_conversion(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    /* Session-owned storage; valid until the next OSK operation
     * that reconverts or mutates the session. */
    return (st && st->conv) ? st->conv : "";
}

int tk_osk_segment_count(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    if (!st || !st->session || !st->reading[0]) return 0;
    return st->be->segment_count(st->session);
}

int tk_osk_candidate_count(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    if (!st || !st->session || !st->reading[0]) return 0;
    return st->be->candidate_count(st->session, 0);
}

const char *tk_osk_candidate(const tk_widget *osk, int idx)
{
    osk_state *st = osk_st((tk_widget *)osk);
    if (!st || !st->session || !st->reading[0]) return NULL;
    if (idx < 0 || idx >= st->be->candidate_count(st->session, 0))
        return NULL;
    return st->be->candidate(st->session, 0, idx);
}

int tk_osk_candidate_index(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->cand_index : 0;
}

long tk_osk_last_convert_us(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->last_convert_us : 0;
}

long tk_osk_layout_builds(const tk_widget *osk)
{
    osk_state *st = osk_st((tk_widget *)osk);
    return st ? st->layout_builds : 0;
}

/* ------------------------------------------------------------------ */
/* Input operations                                                    */
/* ------------------------------------------------------------------ */
void tk_osk_input_kana(tk_widget *osk, const char *kana_utf8)
{
    osk_state *st = osk_st(osk);
    if (!st || !kana_utf8 || !kana_utf8[0]) return;
    osk_seal_romaji(st, false);  /* merged into the sync below */
    osk_seal_toggle(st);
    osk_reading_append(st, kana_utf8);
    osk_sync(st);
}

/* Batched text feed: a whole string (paste-like romaji input, a
 * multi-char TEXT_INPUT event) is converted through the romaji
 * machine into the reading with ONE reconversion at the end,
 * never one per character. */
static void osk_type_text(osk_state *st, const char *text)
{
    bool dirty = false;
    for (const char *p = text; *p; p++) {
        if (st->mode == OSK_MODE_SYMBOL) {
            char s[2] = { *p, '\0' };
            osk_reading_append(st, s);
            dirty = true;
            continue;
        }
        char out[64];
        osk_romaji_putc(&st->romaji, *p, out, sizeof(out));
        if (out[0]) {
            osk_reading_append(st, out);
            dirty = true;
        }
    }
    if (dirty) {
        osk_seal_toggle(st);
        osk_sync(st);
    }
}

bool tk_osk_input_romaji(tk_widget *osk, const char *romaji)
{
    osk_state *st = osk_st(osk);
    if (!st || !romaji) return false;
    bool ok = true;
    for (const char *p = romaji; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              c == '-' || c == ',' || c == '.'))
            ok = false;
    }
    osk_type_text(st, romaji);
    return ok;
}

/* True when QWERTY is in its English (direct) sub-mode: input
 * bypasses the romaji/IME reading and is committed to the
 * target as text.  The delivery channel is the same one every
 * commit uses (TK_EV_TEXT_INPUT to the target) — the path is
 * integrated, the layouts stay distinct. */
static bool osk_direct_active(const osk_state *st)
{
    return st->mode == OSK_MODE_QWERTY && st->direct;
}

/* One char from a key event / QWERTY cell. */
static void osk_type_char(osk_state *st, char c)
{
    char s[2] = { c, '\0' };
    if (osk_direct_active(st))
        osk_push_text(st, s);
    else
        osk_type_text(st, s);
    /* One-shot shift is consumed by the letter it produced (in
     * Japanese mode the romaji machine lowercases it anyway, but
     * the key labels must drop back); the grid re-layouts once. */
    if (st->shift == 1 &&
        ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        st->shift = 0;
        st->grid_gen++;
    }
}

/* After a candidate choice: refresh the conversion pointer and
 * re-push the preedit.  Choosing changes no candidate list and no
 * layout, so the generation counter is untouched — the next paint
 * reuses the cached cells and only the selection highlight and
 * the target composition change. */
static void osk_after_choose(osk_state *st)
{
    st->conv = st->be->session_text(st->session);
    st->cand_browsing = true;     /* user is choosing a candidate */
    osk_push_composition(st);
}

void tk_osk_convert_next(tk_widget *osk)
{
    osk_state *st = osk_st(osk);
    if (!st || !st->session || !st->reading[0]) return;
    osk_seal_romaji(st, true);
    int n = st->be->candidate_count(st->session, 0);
    if (n <= 0) return;
    st->cand_index = (st->cand_index + 1) % n;
    st->be->choose(st->session, 0, st->cand_index);
    osk_after_choose(st);
}

void tk_osk_convert_prev(tk_widget *osk)
{
    osk_state *st = osk_st(osk);
    if (!st || !st->session || !st->reading[0]) return;
    osk_seal_romaji(st, true);
    int n = st->be->candidate_count(st->session, 0);
    if (n <= 0) return;
    st->cand_index = (st->cand_index + n - 1) % n;
    st->be->choose(st->session, 0, st->cand_index);
    osk_after_choose(st);
}

void tk_osk_resize_segment(tk_widget *osk, int delta_chars)
{
    osk_state *st = osk_st(osk);
    if (!st || !st->session || !st->reading[0] || delta_chars == 0) return;
    osk_seal_romaji(st, true);
    const char *seg = st->be->segment_reading(st->session, 0);
    if (!seg) return;
    int cur = u8_count(seg);
    int total = u8_count(st->reading);
    int nl = cur + delta_chars;
    if (nl < 1 || nl >= total) return;
    if (st->be->resize(st->session, 0, nl) != 0) return;
    /* The session may hold a different preedit after a forced
     * reconversion; copying it back is the one place the reading
     * string is re-materialised, and it happens per resize, not
     * per keystroke. */
    osk_reading_set(st, st->be->session_preedit(st->session));
    st->conv = st->be->session_text(st->session);
    st->cand_index = 0;
    st->cand_page = 0;
    st->cand_browsing = false;
    st->cand_gen++;
    osk_push_composition(st);
}

void tk_osk_commit(tk_widget *osk)
{
    osk_state *st = osk_st(osk);
    if (!st || !st->session) return;
    osk_seal_romaji(st, true);
    osk_seal_toggle(st);
    if (!st->reading[0]) {
        osk_push_key(st, TK_KEY_ENTER);
        return;
    }
    char out[512];
    int n = st->be->commit_first(st->session, out, sizeof(out));
    if (n <= 0) return;
    out[sizeof(out) - 1] = '\0';
    /* TEXT_INPUT clears the target composition in the editor
     * handler; the remaining preedit is re-pushed right after. */
    osk_push_text(st, out);
    osk_reading_set(st, st->be->session_preedit(st->session));
    st->conv = st->be->session_text(st->session);
    st->cand_index = 0;
    st->cand_page = 0;
    st->cand_browsing = false;
    st->cand_gen++;
    osk_push_composition(st);
}

void tk_osk_backspace(tk_widget *osk)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    osk_seal_toggle(st);
    if (st->romaji.plen > 0) {
        st->romaji.pending[--st->romaji.plen] = '\0';
        return;
    }
    if (st->reading[0]) {
        int off = u8_last_off(st->reading);
        if (off >= 0) {
            st->reading[off] = '\0';
            st->reading_len = (size_t)off;
        }
        osk_sync(st);
        return;
    }
    osk_push_key(st, TK_KEY_BACKSPACE);
}

void tk_osk_cursor(tk_widget *osk, int dir)
{
    osk_state *st = osk_st(osk);
    if (!st) return;
    if (st->reading[0]) {
        tk_osk_resize_segment(osk, dir);
        return;
    }
    osk_push_key(st, dir < 0 ? TK_KEY_LEFT : TK_KEY_RIGHT);
}

/* Cancel the whole preedit (ESC). */
static void osk_cancel(osk_state *st)
{
    osk_romaji_init(&st->romaji);
    osk_seal_toggle(st);
    st->reading[0] = '\0';
    st->reading_len = 0;
    if (st->session) st->be->session_set_reading(st->session, "");
    st->conv = NULL;
    st->cand_browsing = false;
    st->cand_gen++;
    osk_push_composition(st);
}

/* ------------------------------------------------------------------ */
/* Layout: one builder for paint / hit-test / public introspection     */
/* ------------------------------------------------------------------ */
/* Pointer-based: label/text are stored as references, never
 * copied.  The referents (static tables, literals, session-owned
 * candidate strings) outlive the layout cache, whose generation
 * counter is bumped by exactly the operations that invalidate
 * them. */
/* Cells are routed by kind into the two caches: candidate kinds
 * into the candidate-bar cache, everything else into the key-grid
 * cache.  Both are fixed-size; a full cache drops extra cells
 * (the caps are sized well above the real maxima, see osk.h). */
static osk_icell *cell_push(osk_state *st, osk_cell_kind kind, int id,
                            tk_rect r, const char *label, const char *text)
{
    osk_icell *arr;
    int *n, max;
    if (kind == OSK_CELL_CANDIDATE || kind == OSK_CELL_CAND_PAGE ||
        kind == OSK_CELL_SYM_CAT) {
        arr = st->cand_cells; n = &st->n_cand_cells;
        max = OSK_MAX_CAND_CELLS;
    } else {
        arr = st->grid_cells; n = &st->n_grid_cells;
        max = OSK_MAX_GRID_CELLS;
    }
    if (*n >= max) return NULL;
    osk_icell *c = &arr[(*n)++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->id = id;
    c->r = r;
    c->label = label;
    c->text = text;
    return c;
}

static osk_icell *cell_push_letter(osk_state *st, tk_rect r, char ch)
{
    osk_icell *c = cell_push(st, OSK_CELL_LETTER, ch, r, NULL, NULL);
    if (c) {
        c->buf[0] = ch;
        c->buf[1] = '\0';
        c->label = c->buf;
        c->text = c->buf;
    }
    return c;
}

static void build_candidates(osk_state *st, tk_rect wr)
{
    tk_rect bar = { wr.x, wr.y, wr.w, OSK_CAND_H };
    if (!st->session || !st->reading[0]) return;
    int n = st->be->candidate_count(st->session, 0);
    if (n <= 0) return;
    const tk_theme *t = tk_theme_default();
    int page = st->cand_page;
    if (page < 0) page = 0;
    if (page >= n) page = n - 1;
    /* Single pass: measure each candidate width ONCE into a local
     * array, decide whether the page buttons need room, then emit
     * cells.  (The previous version built the bar twice on
     * overflow, re-measuring every glyph.)  Because the whole
     * layout is generation-cached, even this single pass runs at
     * most once per reading change. */
    enum { MAXW = 96 };
    const char *cands[MAXW];
    int widths[MAXW];
    int m = 0;
    for (int i = page; i < n && m < MAXW; i++) {
        const char *cand = st->be->candidate(st->session, 0, i);
        if (!cand) break;
        /* candidate chips (UI-REDESIGN 2.4): pill chips with 12px
         * of horizontal padding, separated by 8px of space only -
         * no separator glyphs */
        int cw = tk_text_width(t, cand) + 24;
        if (cw < 44) cw = 44;
        cands[m] = cand;
        widths[m] = cw;
        m++;
    }
    int total = 4;
    for (int i = 0; i < m; i++) total += widths[i] + 8;
    bool overflow = page + m < n;
    if (total > bar.w - 8) overflow = true; /* fits nowhere: buttons needed */
    bool need_btns = overflow || page > 0;
    int limit = bar.x + bar.w - 4 - (need_btns ? 68 : 0);
    int x = bar.x + 4;
    for (int i = 0; i < m; i++) {
        if (x + widths[i] > limit && i > 0) { overflow = true; break; }
        cell_push(st, OSK_CELL_CANDIDATE, page + i,
                  (tk_rect){ x, bar.y + 4, widths[i], OSK_CAND_H - 8 },
                  cands[i], cands[i]);
        x += widths[i] + 8;
    }
    if (page > 0)
        cell_push(st, OSK_CELL_CAND_PAGE, -1,
                  (tk_rect){ bar.x + bar.w - 68, bar.y + 4, 30,
                             OSK_CAND_H - 8 }, "<", NULL);
    if (overflow)
        cell_push(st, OSK_CELL_CAND_PAGE, +1,
                  (tk_rect){ bar.x + bar.w - 34, bar.y + 4, 30,
                             OSK_CAND_H - 8 }, ">", NULL);
}

/* Mode cycle (M3): QWERTY is the primary layout, the symbol
 * pages are one step away, and the kana-native layouts
 * (gojuon, flick) are the optional tail of the cycle. */
static osk_mode osk_next_mode(osk_mode m)
{
    switch (m) {
    case OSK_MODE_QWERTY: return OSK_MODE_SYMBOL;
    case OSK_MODE_SYMBOL: return OSK_MODE_GOJUON;
    case OSK_MODE_GOJUON: return OSK_MODE_FLICK;
    case OSK_MODE_FLICK:
    default:              return OSK_MODE_QWERTY;
    }
}

static const char *osk_mode_short_name(osk_mode m)
{
    switch (m) {
    case OSK_MODE_QWERTY: return "QWERTY";
    case OSK_MODE_SYMBOL: return "記号";
    case OSK_MODE_GOJUON: return "50音";
    case OSK_MODE_FLICK:  return "フリック";
    }
    return "";
}

static void build_bottom_bar(osk_state *st, tk_rect wr, int main_h)
{
    int y = wr.y + OSK_CAND_H + main_h;
    /* The mode cell names the layout it switches TO (the next
     * one in the cycle above).  Gojuon mode gains a page-switch
     * cell and QWERTY mode a language cell (Japanese romaji <->
     * English direct), making 6 cells; the bar is shared chrome
     * at the contract height OSK_BAR_H (56). */
    struct { int id; const char *label; } items[6];
    int n = 0;
    items[n].id = OSK_FN_MODE;
    items[n].label = osk_mode_short_name(osk_next_mode(st->mode));
    n++;
    if (st->mode == OSK_MODE_GOJUON) {
        items[n].id = OSK_FN_GOJUON_PAGE;
        items[n].label = st->gojuon_page == 0 ? "は-わ行" : "あ-な行";
        n++;
    }
    if (st->mode == OSK_MODE_QWERTY) {
        items[n].id = OSK_FN_LANG;
        items[n].label = st->direct ? "かな" : "英数";
        n++;
    }
    items[n].id = OSK_FN_LEFT;  items[n].label = "<"; n++;
    items[n].id = OSK_FN_SPACE;
    items[n].label = osk_direct_active(st) ? "空白" : "変換/空白";
    n++;
    items[n].id = OSK_FN_RIGHT; items[n].label = ">"; n++;
    items[n].id = OSK_FN_COMMIT; items[n].label = "確定"; n++;
    int wc = wr.w / n;
    for (int i = 0; i < n; i++) {
        tk_rect r = { wr.x + i * wc, y,
                      (i == n - 1 ? wr.x + wr.w - (wr.x + i * wc) : wc),
                      OSK_BAR_H };
        cell_push(st, OSK_CELL_FUNC, items[i].id, r, items[i].label, NULL);
    }
}

static int mode_rows(const osk_state *st)
{
    switch (st->mode) {
    case OSK_MODE_FLICK:  return 4;
    case OSK_MODE_QWERTY: return 3;
    case OSK_MODE_GOJUON: return OSK_GOJUON_ROWS;
    case OSK_MODE_SYMBOL: return 4;
    }
    return 0;
}

static int mode_row_unit(const osk_state *st)
{
    switch (st->mode) {
    case OSK_MODE_FLICK:  return OSK_FLICK_ROW_H;
    case OSK_MODE_QWERTY: return OSK_QWERTY_ROW_H;
    case OSK_MODE_GOJUON: return OSK_GOJUON_ROW_H;
    case OSK_MODE_SYMBOL: return OSK_SYM_ROW_H;
    }
    return 0;
}

static int mode_main_h(const osk_state *st)
{
    return mode_rows(st) * mode_row_unit(st);
}

/* Height fit (M4): when the widget is arranged SHORTER than its
 * natural height (e.g. the greeter, where the user list, password
 * field and login button share 480 px with the OSK), shrink the
 * key rows so the whole keyboard - including the bottom bar -
 * fits inside the widget rect instead of painting its last row
 * off the bottom edge. Rows never grow past their natural height
 * and never shrink below 1 px; hit testing uses the same built
 * cells, so paint and touch stay consistent. */
static int mode_row_h_fit(const osk_state *st, tk_rect wr)
{
    int unit = mode_row_unit(st);
    int rows = mode_rows(st);
    if (rows <= 0) return unit;
    int avail = wr.h - OSK_CAND_H - OSK_BAR_H;
    if (avail >= rows * unit) return unit;
    /* stepwise compression first (UI-REDESIGN 2.4: 56 -> 48 ->
     * 44), proportional shrink only as the last resort so the
     * keyboard ALWAYS fits its rect and never paints a row off
     * the bottom edge */
    static const int steps[] = { 48, 44 };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
        if (steps[i] < unit && avail >= rows * steps[i])
            return steps[i];
    int h = avail / rows;
    return h > 0 ? h : 1;
}

/* Symbol-mode category chips (UI-REDESIGN 2.4): the 8 symbol
 * categories become a chip row in the candidate bar (which is
 * otherwise empty in symbol mode), instead of a single small
 * "記号: 基本" text label. Built into the candidate cell array;
 * shown only while no reading is active (candidates own the bar
 * then). Padding/gap shrink on narrow widgets so all 8 chips
 * stay on one row at 480 px too. */
static void build_sym_chips(osk_state *st, tk_rect wr)
{
    const tk_theme *t = tk_theme_default();
    int pad = wr.w >= 600 ? 12 : 8;
    int gap = wr.w >= 600 ? 8 : 4;
    int x = wr.x + 4;
    for (int i = 0; i < OSK_SYMBOL_PAGES; i++) {
        int cw = tk_text_width(t, SYM_PAGE_NAMES[i]) + 2 * pad;
        cell_push(st, OSK_CELL_SYM_CAT, i,
                  (tk_rect){ x, wr.y + 4, cw, OSK_CAND_H - 8 },
                  SYM_PAGE_NAMES[i], NULL);
        x += cw + gap;
    }
}

static bool osk_rect_eq(tk_rect a, tk_rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* Candidate-bar cache: rebuilt only when the reading/candidates
 * or the candidate page changed (or the widget moved/resized). */
static void osk_build_cand(osk_state *st, tk_rect wr)
{
    int sym = st->mode == OSK_MODE_SYMBOL;
    if (st->layout_cand_gen == st->cand_gen &&
        st->layout_cand_sym == sym &&
        st->layout_cand_page == st->sym_page &&
        osk_rect_eq(st->layout_cand_rect, wr))
        return;
    st->layout_cand_gen = st->cand_gen;
    st->layout_cand_sym = sym;
    st->layout_cand_page = st->sym_page;
    st->layout_cand_rect = wr;
    st->n_cand_cells = 0;
    st->layout_builds++;
    if (sym && !st->reading[0])
        build_sym_chips(st, wr);
    else
        build_candidates(st, wr);
}

/* Key-grid cache: rebuilt only on mode/page/toggle-label change
 * (or resize).  Typing never touches it. */
static void osk_build_grid(osk_state *st, tk_rect wr)
{
    if (st->layout_grid_gen == st->grid_gen &&
        osk_rect_eq(st->layout_grid_rect, wr))
        return;
    st->layout_grid_gen = st->grid_gen;
    st->layout_grid_rect = wr;
    st->n_grid_cells = 0;
    st->layout_builds++;
    int top = wr.y + OSK_CAND_H;
    int row_h = mode_row_h_fit(st, wr);
    int main_h = row_h * mode_rows(st);
    switch (st->mode) {
    case OSK_MODE_FLICK: {
        int gw = wr.w - OSK_FUNC_W;
        int kw = gw / 3;
        for (int k = 0; k < 12; k++) {
            int col = k % 3, row = k / 3;
            tk_rect r = { wr.x + col * kw, top + row * row_h,
                          col == 2 ? wr.x + gw - (wr.x + col * kw) : kw,
                          row_h };
            cell_push(st, OSK_CELL_KANA_KEY, k, r,
                      osk_flick_key_label(k), osk_flick_kana(k, OSK_DIR_CENTER));
        }
        struct { int id; const char *label; } fn[4] = {
            { OSK_FN_BACKSPACE, "削除" },
            { OSK_FN_CONVERT, "変換" },
            { OSK_FN_TOGGLE_MODE, st->toggle_mode ? "トグル" : "フリック" },
            { OSK_FN_COMMIT, "確定" },
        };
        for (int i = 0; i < 4; i++) {
            tk_rect r = { wr.x + gw, top + i * row_h,
                          OSK_FUNC_W, row_h };
            cell_push(st, OSK_CELL_FUNC, fn[i].id, r, fn[i].label, NULL);
        }
        break;
    }
    case OSK_MODE_QWERTY: {
        /* Row 3 carries the shift key (left) and backspace
         * (right) around zxcvbnm, like a physical keyboard;
         * letter case follows the shift state.  At the 854 px
         * design width the narrowest row (10 keys) is 85 px per
         * key; at 480 px it is 48 px (see osk.h). */
        static const char *const rows[3] = {
            "qwertyuiop", "asdfghjkl", "zxcvbnm"
        };
        for (int row = 0; row < 3; row++) {
            int n = (int)strlen(rows[row]);
            int lead = row == 2 ? 1 : 0;   /* shift cell */
            int trail = row == 2 ? 1 : 0;  /* backspace cell */
            int cols = n + lead + trail;
            int kw = wr.w / cols;
            int x = wr.x;
            int y = top + row * row_h;
            if (lead) {
                cell_push(st, OSK_CELL_FUNC, OSK_FN_SHIFT,
                          (tk_rect){ x, y, kw, row_h },
                          "⇧", NULL);
                x += kw;
            }
            for (int i = 0; i < n; i++) {
                char ch = rows[row][i];
                if (st->shift) ch = (char)(ch - 'a' + 'A');
                cell_push_letter(st,
                                 (tk_rect){ x, y, kw, row_h },
                                 ch);
                x += kw;
            }
            if (trail) {
                cell_push(st, OSK_CELL_FUNC, OSK_FN_BACKSPACE,
                          (tk_rect){ x, y, wr.x + wr.w - x,
                                     row_h },
                          "削除", NULL);
            }
        }
        break;
    }
    case OSK_MODE_GOJUON: {
        /* One page of 5 rows; the flick key index IS the gojuon
         * row, so page 0 shows rows 0-4 (あ か さ た な) and
         * page 1 rows 5-9 (は ま や ら わ). */
        int base = st->gojuon_page * OSK_GOJUON_ROWS;
        for (int row = 0; row < OSK_GOJUON_ROWS; row++) {
            int grow = base + row;
            for (int col = 0; col < 5; col++) {
                const char *kana = osk_flick_kana(grow, (osk_dir)col);
                if (grow == 9) {
                    /* わ row: わ を ん ー ゎ (display order) */
                    static const char *const wa[5] = {
                        "わ", "を", "ん", "ー", "ゎ"
                    };
                    kana = wa[col];
                }
                if (!kana) continue;
                int kw = wr.w / 5;
                tk_rect r = { wr.x + col * kw, top + row * row_h,
                              col == 4 ? wr.x + wr.w - (wr.x + col * kw) : kw,
                              row_h };
                cell_push(st, OSK_CELL_GOJUON, 0, r, kana, kana);
            }
        }
        break;
    }
    case OSK_MODE_SYMBOL: {
        /* Tables live with the other static tables (SYM_PAGES);
         * the last cell of the grid is the page-cycle key and
         * its label names the page it switches TO.  NULL table
         * entries leave the position empty.  The ideographic
         * space is painted with a visible label (its glyph is
         * whitespace) but inputs the space itself. */
        const char *const (*pg)[10] = SYM_PAGES[st->sym_page];
        int kw = wr.w / 10;
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 10; col++) {
                tk_rect r = { wr.x + col * kw, top + row * row_h,
                              col == 9 ? wr.x + wr.w - (wr.x + col * kw) : kw,
                              row_h };
                if (row == 3 && col == 9) {
                    cell_push(st, OSK_CELL_FUNC, OSK_FN_SYMBOL_PAGE, r,
                              SYM_PAGE_NAMES[(st->sym_page + 1) %
                                             OSK_SYMBOL_PAGES], NULL);
                } else if (pg[row][col]) {
                    const char *label = pg[row][col];
                    if (!strcmp(label, "　")) label = "全角空白";
                    cell_push(st, OSK_CELL_SYMBOL, 0, r, label,
                              pg[row][col]);
                }
            }
        break;
    }
    }
    build_bottom_bar(st, wr, main_h);
}

static void osk_build_cells(osk_state *st, tk_rect wr)
{
    osk_build_cand(st, wr);
    osk_build_grid(st, wr);
}

int tk_osk_layout(const tk_widget *osk, osk_cell *out, int max)
{
    osk_state *st = osk_st((tk_widget *)osk);
    if (!st) return 0;
    osk_build_cells(st, osk->rect);
    /* Snapshot conversion: the ONLY place cell strings are
     * copied, and only when a caller explicitly asks for the
     * public layout (tests, future shell code). */
    int total = st->n_cand_cells + st->n_grid_cells;
    int n = total < max ? total : max;
    for (int i = 0; i < n; i++) {
        const osk_icell *src = i < st->n_cand_cells
            ? &st->cand_cells[i]
            : &st->grid_cells[i - st->n_cand_cells];
        memset(&out[i], 0, sizeof(out[i]));
        out[i].r = src->r;
        out[i].kind = src->kind;
        out[i].id = src->id;
        if (src->label)
            snprintf(out[i].label, sizeof(out[i].label), "%s", src->label);
        if (src->text)
            snprintf(out[i].text, sizeof(out[i].text), "%s", src->text);
    }
    return total;
}

tk_size tk_osk_measure(tk_widget *w, tk_constraints c)
{
    osk_state *st = osk_st(w);
    int width = OSK_DEFAULT_W;
    if (c.max_w > 0 && c.max_w < TK_UNLIMITED) width = c.max_w;
    if (c.min_w > width) width = c.min_w;
    int h = OSK_CAND_H + (st ? mode_main_h(st) : 4 * OSK_FLICK_ROW_H) +
            OSK_BAR_H;
    return (tk_size){ width, h };
}

/* ------------------------------------------------------------------ */
/* Paint                                                               */
/* ------------------------------------------------------------------ */
static void draw_cell_label(tk_canvas *c, const tk_theme *t, tk_rect r,
                            const char *label, tk_color fg)
{
    int tw = tk_text_width(t, label);
    tk_canvas_draw_text(c, t, r.x + (r.w - tw) / 2,
                        r.y + (r.h - tk_line_height(t)) / 2, label, fg);
}

/* Label at an explicit pixel size (symbol-page glyphs are drawn
 * larger than body text so they stay legible for finger picking,
 * UI-REDESIGN 2.4). */
static void draw_cell_label_px(tk_canvas *c, const tk_theme *t, tk_rect r,
                               const char *label, tk_color fg, int px)
{
    int tw = tk_text_width_px(t, px, label);
    tk_canvas_draw_text_px(c, t, px, false, r.x + (r.w - tw) / 2,
                           r.y + (r.h - tk_line_height_px(t, px)) / 2,
                           label, fg);
}

static void paint_cells(osk_state *st, tk_canvas *c, const tk_theme *t,
                        osk_icell *arr, int n);

/* Flick press preview state (M2): while a kana key (0..9) is held
 * in flick mode, the preview is the key's five kana in a 3x3 cross
 * popup. The selected direction follows the live pointer position
 * through the same classifier the release path uses, so what the
 * preview highlights is exactly what a release would input. */
static bool osk_preview_state(const osk_state *st, tk_rect wr,
                              int *key, osk_dir *dir, tk_rect *rect)
{
    if (!st->press_active || !st->press_valid) return false;
    if (st->mode != OSK_MODE_FLICK) return false;
    if (st->press_cell.kind != OSK_CELL_KANA_KEY) return false;
    if (st->press_cell.id < 0 || st->press_cell.id > 9) return false;
    osk_dir d = osk_flick_classify(st->cur_x - st->press_x,
                                   st->cur_y - st->press_y,
                                   OSK_TAP_THRESHOLD);
    int cs = OSK_PREVIEW_CELL;
    tk_rect kr = st->press_cell.r;
    int x = kr.x + kr.w / 2 - (3 * cs) / 2;
    int y = kr.y - 3 * cs - 4;          /* above the key */
    if (y < wr.y) y = kr.y + kr.h + 4;  /* top row: below instead */
    if (x < wr.x) x = wr.x;
    if (x + 3 * cs > wr.x + wr.w) x = wr.x + wr.w - 3 * cs;
    if (y + 3 * cs > wr.y + wr.h) y = wr.y + wr.h - 3 * cs;
    if (y < wr.y) y = wr.y;
    if (key) *key = st->press_cell.id;
    if (dir) *dir = d;
    if (rect) *rect = (tk_rect){ x, y, 3 * cs, 3 * cs };
    return true;
}

bool tk_osk_preview(const tk_widget *osk, int *key, osk_dir *dir,
                    tk_rect *rect)
{
    osk_state *st = osk_st((tk_widget *)osk);
    if (!st) return false;
    return osk_preview_state(st, osk->rect, key, dir, rect);
}

static void paint_preview(osk_state *st, tk_canvas *c, const tk_theme *t,
                          tk_rect wr)
{
    int key;
    osk_dir dir;
    tk_rect pr;
    if (!osk_preview_state(st, wr, &key, &dir, &pr)) return;
    int cs = OSK_PREVIEW_CELL;
    tk_canvas_fill_round_rect(c, pr, t->radius, t->surface);
    tk_canvas_stroke_round_rect(c, pr, t->radius, 1, t->border);
    /* cross offsets per direction: CENTER, LEFT, UP, RIGHT, DOWN */
    static const int offs[OSK_NDIR][2] = {
        { 1, 1 }, { 0, 1 }, { 1, 0 }, { 2, 1 }, { 1, 2 }
    };
    for (int d = 0; d < OSK_NDIR; d++) {
        const char *kana = osk_flick_kana(key, (osk_dir)d);
        if (!kana) continue;
        tk_rect cell = { pr.x + offs[d][0] * cs, pr.y + offs[d][1] * cs,
                         cs, cs };
        bool sel = (osk_dir)d == dir;
        if (sel)
            tk_canvas_fill_round_rect(c, cell, t->radius, t->accent);
        draw_cell_label(c, t, cell, kana, sel ? t->on_accent : t->text);
    }
}

void tk_osk_paint(tk_widget *w, tk_canvas *c, const tk_theme *t)
{
    osk_state *st = osk_st(w);
    if (!st) return;
    tk_rect wr = w->rect;
    tk_canvas_fill_rect(c, wr, t->bg);
    /* candidate bar background */
    tk_canvas_fill_rect(c, (tk_rect){ wr.x, wr.y, wr.w, OSK_CAND_H },
                        t->surface_alt);
    osk_build_cells(st, wr);
    if (!st->reading[0] && st->mode != OSK_MODE_SYMBOL) {
        /* symbol mode shows its category chips in the bar instead
         * of a text label (build_sym_chips) */
        static const char *const mode_names[4] = {
            "フリック入力", "ローマ字入力", "50音", "数字・記号"
        };
        const char *name = mode_names[st->mode];
        if (st->mode == OSK_MODE_QWERTY && st->direct)
            name = "英数入力";
        tk_canvas_draw_text(c, t, wr.x + t->unit,
                            wr.y + (OSK_CAND_H - tk_line_height(t)) / 2,
                            name, t->text_dim);
    }
    paint_cells(st, c, t, st->cand_cells, st->n_cand_cells);
    paint_cells(st, c, t, st->grid_cells, st->n_grid_cells);
    paint_preview(st, c, t, wr);
}

/* Chip corner radius (M24, following the dual-mode audit in
 * osk/REPORT-osk-dual-mode.md section 3.3): Material (style 1)
 * chips are pills, WinUI (style 2) chips take the theme's button
 * radius. Other styles keep the historical pill. Key faces below
 * simply use t->radius, which is 12 in the Material themes and
 * 6 in the WinUI themes. */
static int chip_radius(const tk_theme *t, int h)
{
    if (t->style == 2) {
        int rb = t->radius_button >= 0 ? t->radius_button : t->radius;
        return rb < h / 2 ? rb : h / 2;
    }
    return h / 2;
}

static void paint_cells(osk_state *st, tk_canvas *c, const tk_theme *t,
                        osk_icell *arr, int n)
{
    /* Key faces are inset by half the inter-key gap (4 px at
     * >= 600 px wide, 2 px below) with the theme's corner radius,
     * so the keyboard reads as separate keys instead of a bordered
     * spreadsheet (UI-REDESIGN 2.4; radius was a hard-coded 6 until
     * M24, which matched only the WinUI themes by coincidence).
     * Hit rects are unchanged. */
    int half_gap = st->layout_grid_rect.w >= 600 ? 2 : 1;
    for (int i = 0; i < n; i++) {
        osk_icell *cell = &arr[i];
        bool pressed = st->press_valid &&
                       cell->kind == st->press_cell.kind &&
                       cell->id == st->press_cell.id &&
                       cell->r.x == st->press_cell.r.x &&
                       cell->r.y == st->press_cell.r.y;
        if (cell->kind == OSK_CELL_CANDIDATE) {
            /* chip: pill in Material, theme button radius in
             * WinUI (chip_radius); selected = accent face */
            bool sel = cell->id == st->cand_index;
            tk_canvas_fill_round_rect(c, cell->r,
                                      chip_radius(t, cell->r.h),
                                      sel ? t->accent : t->surface);
            draw_cell_label(c, t, cell->r, cell->label,
                            sel ? t->on_accent : t->text);
            continue;
        }
        if (cell->kind == OSK_CELL_SYM_CAT) {
            bool sel = cell->id == st->sym_page;
            int cr = chip_radius(t, cell->r.h);
            tk_canvas_fill_round_rect(c, cell->r, cr,
                                      sel ? t->accent : t->surface);
            if (!sel) {
                /* the frame follows the fill's corners; in the
                 * pill styles the historical square frame is
                 * kept, in WinUI it rounds with the chip */
                if (t->style == 2)
                    tk_canvas_stroke_round_rect(c, cell->r, cr, 1,
                                                t->border);
                else
                    tk_canvas_stroke_rect(c, cell->r, 1, t->border);
            }
            draw_cell_label(c, t, cell->r, cell->label,
                            sel ? t->on_accent : t->text);
            continue;
        }
        /* an armed shift key (one-shot or lock) stays highlighted
         * so the state is visible even though it is not pressed */
        bool shift_armed = cell->kind == OSK_CELL_FUNC &&
                           cell->id == OSK_FN_SHIFT && st->shift != 0;
        tk_rect face = { cell->r.x + half_gap, cell->r.y + half_gap,
                         cell->r.w - 2 * half_gap,
                         cell->r.h - 2 * half_gap };
        tk_canvas_fill_round_rect(c, face, t->radius,
                                  pressed || shift_armed ? t->selection
                                                         : t->surface);
        tk_canvas_stroke_round_rect(c, face, t->radius, 1, t->border);
        if (cell->kind == OSK_CELL_SYMBOL)
            draw_cell_label_px(c, t, cell->r, cell->label, t->text, 20);
        else
            draw_cell_label(c, t, cell->r, cell->label, t->text);
        if (cell->kind == OSK_CELL_KANA_KEY && cell->id < 10) {
            /* flick hints at the four edges */
            static const osk_dir dirs[4] = {
                OSK_DIR_LEFT, OSK_DIR_UP, OSK_DIR_RIGHT, OSK_DIR_DOWN
            };
            for (int d = 0; d < 4; d++) {
                const char *h = osk_flick_kana(cell->id, dirs[d]);
                if (!h) continue;
                int hw = tk_text_width(t, h);
                int hx = cell->r.x + (cell->r.w - hw) / 2;
                int hy = cell->r.y + (cell->r.h - tk_line_height(t)) / 2;
                if (dirs[d] == OSK_DIR_LEFT) hx = cell->r.x + 3;
                if (dirs[d] == OSK_DIR_RIGHT) hx = cell->r.x + cell->r.w - hw - 3;
                if (dirs[d] == OSK_DIR_UP) hy = cell->r.y + 1;
                if (dirs[d] == OSK_DIR_DOWN) hy = cell->r.y + cell->r.h - tk_line_height(t) - 1;
                tk_canvas_draw_text(c, t, hx, hy, h, t->text_dim);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */
static osk_icell *cell_at(osk_state *st, int x, int y)
{
    /* grid first (it was later in the combined array, hence
     * higher hit priority), then the candidate bar */
    for (int i = st->n_grid_cells - 1; i >= 0; i--)
        if (tk_rect_contains(st->grid_cells[i].r, x, y))
            return &st->grid_cells[i];
    for (int i = st->n_cand_cells - 1; i >= 0; i--)
        if (tk_rect_contains(st->cand_cells[i].r, x, y))
            return &st->cand_cells[i];
    return NULL;
}

static void osk_do_func(osk_state *st, tk_widget *w, int fn)
{
    switch (fn) {
    case OSK_FN_BACKSPACE: tk_osk_backspace(w); break;
    case OSK_FN_CONVERT:   tk_osk_convert_next(w); break;
    case OSK_FN_COMMIT:    tk_osk_commit(w); break;
    case OSK_FN_SPACE:
        if (st->reading[0]) tk_osk_convert_next(w);
        else osk_push_text(st, " ");
        break;
    case OSK_FN_LEFT:      tk_osk_cursor(w, -1); break;
    case OSK_FN_RIGHT:     tk_osk_cursor(w, +1); break;
    case OSK_FN_MODE:
        tk_osk_set_mode(w, osk_next_mode(st->mode));
        break;
    case OSK_FN_TOGGLE_MODE:
        tk_osk_set_toggle_mode(w, !st->toggle_mode);
        break;
    case OSK_FN_SYMBOL_PAGE:
        st->sym_page = (st->sym_page + 1) % OSK_SYMBOL_PAGES;
        st->grid_gen++;
        break;
    case OSK_FN_SHIFT:
        st->shift = (st->shift + 1) % 3;  /* off -> one-shot -> lock */
        st->grid_gen++;
        break;
    case OSK_FN_LANG:
        tk_osk_set_direct(w, !st->direct);
        break;
    case OSK_FN_GOJUON_PAGE:
        tk_osk_set_gojuon_page(w, st->gojuon_page ? 0 : 1);
        break;
    }
}

/* A flick key was released with direction `dir`. */
static void osk_flick_key_input(osk_state *st, int key, osk_dir dir)
{
    if (key == 10) { /* symbol key */
        static const char *const sym[OSK_NDIR] = {
            "、", "。", "！", "？", "・"
        };
        osk_seal_toggle(st);
        osk_reading_append(st, sym[dir]);
        osk_sync(st);
        return;
    }
    if (key == 11) { /* dakuten / handakuten / small */
        osk_seal_toggle(st);
        osk_transform_last(st, dir == OSK_DIR_UP ? TR_HANDAKUTEN :
                               dir == OSK_DIR_CENTER ? TR_DAKUTEN :
                               TR_SMALL);
        osk_sync(st);
        return;
    }
    const char *kana = osk_flick_kana(key, dir);
    if (!kana) return;
    osk_seal_toggle(st);
    osk_reading_append(st, kana);
    osk_sync(st);
}

/* Multi-tap: tap on `key` cycles through its five kana in place. */
static void osk_toggle_key_input(osk_state *st, int key)
{
    if (key >= 10) { /* action keys behave as in flick mode */
        osk_flick_key_input(st, key, OSK_DIR_CENTER);
        return;
    }
    if (st->toggle_key == key) {
        st->toggle_idx = (st->toggle_idx + 1) % OSK_NDIR;
        int off = u8_last_off(st->reading);
        if (off >= 0) {
            st->reading[off] = '\0';
            st->reading_len = (size_t)off;
        }
    } else {
        st->toggle_key = key;
        st->toggle_idx = 0;
    }
    const char *kana = osk_flick_kana(key, (osk_dir)st->toggle_idx);
    if (kana) osk_reading_append(st, kana);
    osk_sync(st);
}

/* Damage narrowing: a key event on the OSK changes at most the
 * pressed cell, the flick preview and the candidate bar - not
 * the whole strip. Declaring those rects to the core (via
 * tk_root_damage_hint) keeps the per-key repaint and framebuffer
 * transfer proportional to the change (~86 KB instead of the
 * full strip's ~435 KB at 800x272, ~465 KB at the panel's
 * 854 width). Actions that change the
 * layout itself (mode/shift/page: grid_gen moves) keep the
 * full-rect damage. */
static tk_rect osk_bar_rect(const tk_widget *w)
{
    return (tk_rect){ w->rect.x, w->rect.y, w->rect.w, OSK_CAND_H };
}

static tk_rect osk_preview_rect(tk_widget *w)
{
    int key;
    osk_dir dir;
    tk_rect r = { 0, 0, 0, 0 };
    if (tk_osk_preview(w, &key, &dir, &r)) return r;
    return (tk_rect){ 0, 0, 0, 0 };
}

bool tk_osk_event(tk_widget *w, tk_root *root, tk_event *ev)
{
    osk_state *st = osk_st(w);
    if (!st || !w->enabled) return false;
    switch (ev->type) {
    case TK_EV_POINTER_DOWN: {
        if (!tk_rect_contains(w->rect, ev->x, ev->y)) return false;
        osk_build_cells(st, w->rect);
        osk_icell *cell = cell_at(st, ev->x, ev->y);
        st->press_active = true;
        st->press_valid = cell != NULL;
        if (cell) st->press_cell = *cell;
        st->press_x = ev->x;
        st->press_y = ev->y;
        st->cur_x = ev->x;
        st->cur_y = ev->y;
        if (cell && cell->kind == OSK_CELL_CANDIDATE) {
            /* Candidate selection happens on DOWN for
             * responsiveness.  It changes neither the candidate
             * list nor the layout: no generation bump, no
             * re-measure — only the highlight and the target's
             * composition update. */
            st->be->choose(st->session, 0, cell->id);
            st->cand_index = cell->id;
            osk_after_choose(st);
        } else if (cell && cell->kind == OSK_CELL_CAND_PAGE) {
            int n = tk_osk_candidate_count(w);
            st->cand_page += cell->id;
            if (st->cand_page < 0) st->cand_page = 0;
            if (n > 0 && st->cand_page >= n) st->cand_page = n - 1;
            st->cand_gen++;
        }
        if (cell) {
            if (cell->kind == OSK_CELL_CANDIDATE ||
                cell->kind == OSK_CELL_CAND_PAGE)
                tk_root_damage_hint(root, osk_bar_rect(w));
            tk_root_damage_hint(root, cell->r);
            tk_root_damage_hint(root, osk_preview_rect(w));
        }
        return true;
    }
    case TK_EV_POINTER_MOVE:
        if (st->press_active) {
            st->cur_x = ev->x;
            st->cur_y = ev->y;
            if (st->press_valid) {
                tk_root_damage_hint(root, st->press_cell.r);
                tk_root_damage_hint(root, osk_preview_rect(w));
            }
        }
        return st->press_active;
    case TK_EV_POINTER_UP: {
        if (!st->press_active) return false;
        tk_rect prev = osk_preview_rect(w); /* repaint over it */
        unsigned grid0 = st->grid_gen;
        st->press_active = false;
        if (!st->press_valid) return true;
        osk_icell cell = st->press_cell;
        st->press_valid = false;
        switch (cell.kind) {
        case OSK_CELL_CANDIDATE:
        case OSK_CELL_CAND_PAGE:
            break; /* handled on DOWN */
        case OSK_CELL_SYM_CAT:
            if (tk_rect_contains(cell.r, ev->x, ev->y) &&
                cell.id != st->sym_page) {
                st->sym_page = cell.id;
                st->grid_gen++;
            }
            break;
        case OSK_CELL_KANA_KEY: {
            osk_dir dir = osk_flick_classify(ev->x - st->press_x,
                                             ev->y - st->press_y,
                                             OSK_TAP_THRESHOLD);
            if (st->toggle_mode && dir == OSK_DIR_CENTER)
                osk_toggle_key_input(st, cell.id);
            else
                osk_flick_key_input(st, cell.id, dir);
            break;
        }
        case OSK_CELL_LETTER:
            if (tk_rect_contains(cell.r, ev->x, ev->y))
                osk_type_char(st, cell.text[0]);
            break;
        case OSK_CELL_GOJUON:
        case OSK_CELL_SYMBOL:
            if (tk_rect_contains(cell.r, ev->x, ev->y)) {
                osk_seal_toggle(st);
                osk_reading_append(st, cell.text);
                osk_sync(st);
            }
            break;
        case OSK_CELL_FUNC:
            if (tk_rect_contains(cell.r, ev->x, ev->y))
                osk_do_func(st, w, cell.id);
            break;
        default:
            break;
        }
        if (st->grid_gen != grid0) {
            tk_root_damage_hint(root, w->rect); /* layout changed */
        } else {
            tk_root_damage_hint(root, osk_bar_rect(w));
            tk_root_damage_hint(root, cell.r);
            tk_root_damage_hint(root, prev);
        }
        return true;
    }
    case TK_EV_TEXT_INPUT: {
        /* Physical input routed to the OSK.  In Japanese mode it
         * takes the romaji path, batched into a single
         * reconversion for the whole string (see osk_type_text);
         * in the English direct sub-mode it is committed as-is
         * (the physical keyboard's own shift/caps handling in
         * tk_kbd has already produced the final characters, so
         * the on-screen shift state is not consulted here). */
        if (!ev->text) return false;
        tk_root_damage_hint(root, osk_bar_rect(w));
        if (osk_direct_active(st))
            osk_push_text(st, ev->text);
        else
            osk_type_text(st, ev->text);
        return true;
    }
    case TK_EV_KEY_DOWN: {
        /* every key path below changes at most the composition
         * display (the candidate bar) */
        tk_root_damage_hint(root, osk_bar_rect(w));
        switch (ev->key) {
        case TK_KEY_BACKSPACE: tk_osk_backspace(w); return true;
        case TK_KEY_DELETE:
            if (!st->reading[0]) osk_push_key(st, TK_KEY_DELETE);
            return true;
        case TK_KEY_ENTER: tk_osk_commit(w); return true;
        case TK_KEY_SPACE:
            if (st->reading[0]) tk_osk_convert_next(w);
            else osk_push_text(st, " ");
            return true;
        case TK_KEY_LEFT:  tk_osk_cursor(w, -1); return true;
        case TK_KEY_RIGHT: tk_osk_cursor(w, +1); return true;
        case TK_KEY_UP:    tk_osk_convert_prev(w); return true;
        case TK_KEY_DOWN:  tk_osk_convert_next(w); return true;
        case TK_KEY_ESCAPE: osk_cancel(st); return true;
        default:
            if (ev->text && ev->text[0] &&
                !(ev->modifiers & (TK_MOD_CTRL | TK_MOD_ALT))) {
                osk_type_char(st, ev->text[0]);
                return true;
            }
            return false;
        }
    }
    default:
        return false;
    }
}
