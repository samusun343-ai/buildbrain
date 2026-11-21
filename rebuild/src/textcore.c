/* textcore implementation: gap buffer + undo + cluster movement.
 * See include/textcore.h and DESIGN-toolkit-v2.md section 8. */
#include "textcore.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* UTF-8 helpers                                                       */
/* ------------------------------------------------------------------ */

uint32_t tc_utf8_decode(const char *s, size_t len, size_t *out_len)
{
    if (len == 0) { if (out_len) *out_len = 0; return 0; }
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { if (out_len) *out_len = 1; return c; }
    int need; uint32_t cp;
    if ((c & 0xE0) == 0xC0) { need = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { need = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { need = 4; cp = c & 0x07; }
    else { if (out_len) *out_len = 1; return 0xFFFD; }
    if ((size_t)need > len) { if (out_len) *out_len = 1; return 0xFFFD; }
    for (int i = 1; i < need; i++) {
        unsigned char t = (unsigned char)s[i];
        if ((t & 0xC0) != 0x80) { if (out_len) *out_len = 1; return 0xFFFD; }
        cp = (cp << 6) | (t & 0x3F);
    }
    if (out_len) *out_len = (size_t)need;
    return cp;
}

bool tc_cp_is_combining(uint32_t cp)
{
    /* Combining diacriticals, marks, variation selectors, emoji
     * skin-tone modifiers, tag characters. Approximation of UAX #29
     * GB9/GB9a; documented in DESIGN section 8. */
    if (cp >= 0x0300 && cp <= 0x036F) return true;
    if (cp >= 0x0483 && cp <= 0x0489) return true;
    if (cp >= 0x0591 && cp <= 0x05BD) return true;
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp >= 0x0670 && cp <= 0x0670) return true;
    if (cp >= 0x06D6 && cp <= 0x06DC) return true;
    if (cp >= 0x0E31 && cp <= 0x0E3A) return true;
    if (cp >= 0x0E47 && cp <= 0x0E4E) return true;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;
    if (cp >= 0x3099 && cp <= 0x309A) return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;   /* VS1-16 */
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true; /* skin tones */
    if (cp >= 0xE0020 && cp <= 0xE007F) return true; /* tags */
    if (cp >= 0xE0100 && cp <= 0xE01EF) return true; /* VS17-256 */
    return false;
}

static bool cp_is_regional(uint32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

#define CP_ZWJ 0x200D

/* ------------------------------------------------------------------ */
/* Gap buffer                                                          */
/* ------------------------------------------------------------------ */

struct tc_buffer {
    char *buf;          /* storage */
    size_t cap;         /* allocated bytes */
    size_t gap_start;   /* == cursor-visible content split */
    size_t gap_end;     /* content resumes here */
    size_t cursor;      /* logical byte offset (content coords) */
    size_t anchor;      /* selection anchor (content coords) */
    tc_clipboard clip;  /* zeroed when unset */
    bool clip_set;
    char *composition;  /* malloc'd preedit string or NULL */
    /* Composition segmentation (M2): byte length per segment; the
     * lengths always sum to strlen(composition). NULL = unsegmented
     * (renderers treat the whole string as one segment). */
    size_t *comp_segs;
    size_t comp_nseg;
    int comp_active;    /* segment under conversion */
    bool comp_highlight; /* active segment is candidate-browsing */

    /* undo/redo */
    struct tc_op *undo; size_t undo_len, undo_cap;
    struct tc_op *redo; size_t redo_len, redo_cap;
    int last_op_kind;   /* for coalescing: 1=insert, 2=delete-back, 3=delete-fwd */
};

enum { OP_INSERT = 1, OP_DELETE = 2 };
struct tc_op {
    int kind;           /* OP_INSERT: text was inserted at pos.
                           OP_DELETE: text was deleted from pos. */
    size_t pos;
    char *text;         /* malloc'd, NUL-terminated */
    size_t len;
    size_t cursor_before, anchor_before;
    size_t cursor_after, anchor_after;
};

static size_t content_len(const tc_buffer *b)
{
    return b->cap - (b->gap_end - b->gap_start) > 0
        ? (b->gap_start) + (b->cap - b->gap_end)
        : 0;
}

/* raw byte at content offset */
static char byte_at(const tc_buffer *b, size_t off)
{
    if (off < b->gap_start) return b->buf[off];
    return b->buf[b->gap_end + (off - b->gap_start)];
}

static void gap_move(tc_buffer *b, size_t pos)
{
    if (pos < b->gap_start) {
        size_t n = b->gap_start - pos;
        memmove(b->buf + (b->gap_end - n), b->buf + pos, n);
        b->gap_start -= n; b->gap_end -= n;
    } else if (pos > b->gap_start) {
        size_t n = pos - b->gap_start;
        memmove(b->buf + b->gap_start, b->buf + b->gap_end, n);
        b->gap_start += n; b->gap_end += n;
    }
}

static bool gap_reserve(tc_buffer *b, size_t extra)
{
    size_t gap = b->gap_end - b->gap_start;
    if (gap >= extra) return true;
    size_t need = content_len(b) + extra;
    size_t ncap = b->cap ? b->cap : 64;
    while (ncap < need) ncap *= 2;
    char *nb = malloc(ncap);
    if (!nb) return false;
    size_t tail = b->cap - b->gap_end;
    memcpy(nb, b->buf, b->gap_start);
    memcpy(nb + (ncap - tail), b->buf + b->gap_end, tail);
    free(b->buf);
    b->buf = nb;
    b->gap_end = ncap - tail;
    b->cap = ncap;
    return true;
}

/* insert/delete at content level, no undo recording */
static void raw_insert(tc_buffer *b, size_t pos, const char *text, size_t len)
{
    if (!gap_reserve(b, len)) return;
    gap_move(b, pos);
    memcpy(b->buf + b->gap_start, text, len);
    b->gap_start += len;
}

static void raw_delete(tc_buffer *b, size_t pos, size_t len)
{
    gap_move(b, pos);
    b->gap_end += len; /* just widen the gap */
}

/* copy content range into a fresh malloc'd NUL-terminated string */
static char *dup_range(const tc_buffer *b, size_t start, size_t len)
{
    char *s = malloc(len + 1);
    if (!s) return NULL;
    for (size_t i = 0; i < len; i++) s[i] = byte_at(b, start + i);
    s[len] = '\0';
    return s;
}

/* ------------------------------------------------------------------ */
/* Codepoint / cluster navigation over content                         */
/* ------------------------------------------------------------------ */

static uint32_t cp_at(const tc_buffer *b, size_t off, size_t *len)
{
    size_t total = content_len(b);
    if (off >= total) { if (len) *len = 0; return 0; }
    char tmp[4]; size_t avail = total - off; if (avail > 4) avail = 4;
    for (size_t i = 0; i < avail; i++) tmp[i] = byte_at(b, off + i);
    return tc_utf8_decode(tmp, avail, len);
}

static uint32_t cp_before(const tc_buffer *b, size_t off, size_t *len)
{
    /* scan back over continuation bytes */
    size_t start = off;
    while (start > 0) {
        start--;
        unsigned char c = (unsigned char)byte_at(b, start);
        if ((c & 0xC0) != 0x80) break;
        if (off - start > 4) break;
    }
    size_t l; uint32_t cp = cp_at(b, start, &l);
    if (len) *len = l;
    return cp;
}

size_t tc_cp_len_at(const tc_buffer *b, size_t off)
{
    size_t l = 0; (void)cp_at(b, off, &l); return l ? l : 1;
}

size_t tc_cluster_before(const tc_buffer *b, size_t off)
{
    if (off == 0) return 0;
    size_t pos = off;
    /* walk back one codepoint at a time while it attaches */
    for (;;) {
        size_t l; uint32_t cp = cp_before(b, pos, &l);
        if (l == 0) break;
        size_t prev = pos - l;
        if (prev == 0) { pos = prev; break; }
        /* ZWJ: the codepoint before the ZWJ joins too */
        if (cp == CP_ZWJ) { pos = prev; continue; }
        /* combining / VS / modifier attaches to previous */
        if (tc_cp_is_combining(cp)) { pos = prev; continue; }
        /* if the codepoint before this one is ZWJ, we are mid-chain */
        {
            size_t l2; uint32_t before = cp_before(b, prev, &l2);
            if (before == CP_ZWJ) { pos = prev; continue; }
        }
        /* regional indicators pair up: count the run of RIs ending
         * at pos; if the run is even, this RI is the second of a
         * pair (go back 2), if odd it stands alone (go back 1). */
        if (cp_is_regional(cp)) {
            size_t run = 0; size_t p = pos;
            while (p > 0) {
                size_t l3; uint32_t c3 = cp_before(b, p, &l3);
                if (!cp_is_regional(c3)) break;
                run++; p -= l3;
            }
            int steps = (run % 2 == 0) ? 2 : 1;
            pos = prev;
            for (int s = 1; s < steps; s++) {
                size_t l4; (void)cp_before(b, pos, &l4);
                pos -= l4 ? l4 : 1;
            }
            break;
        }
        pos = prev; break;
    }
    return pos;
}

size_t tc_cluster_after(const tc_buffer *b, size_t off)
{
    size_t total = content_len(b);
    if (off >= total) return total;
    size_t pos = off;
    size_t l; uint32_t cp = cp_at(b, pos, &l);
    if (l == 0) return total;
    pos += l;
    bool first_was_regional = cp_is_regional(cp);
    int regional_count = first_was_regional ? 1 : 0;
    while (pos < total) {
        size_t l2; uint32_t c2 = cp_at(b, pos, &l2);
        if (l2 == 0) break;
        if (tc_cp_is_combining(c2)) { pos += l2; continue; }
        if (c2 == CP_ZWJ) {
            /* swallow ZWJ and the next codepoint */
            pos += l2;
            if (pos < total) {
                size_t l3; (void)cp_at(b, pos, &l3);
                pos += l3 ? l3 : 1;
            }
            continue;
        }
        if (first_was_regional && cp_is_regional(c2) && regional_count == 1) {
            pos += l2; regional_count++; continue;
        }
        break;
    }
    return pos;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

tc_buffer *tc_new(void)
{
    tc_buffer *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->cap = 64;
    b->buf = malloc(b->cap);
    if (!b->buf) { free(b); return NULL; }
    b->gap_start = 0;
    b->gap_end = b->cap;
    return b;
}

static void op_clear(struct tc_op *ops, size_t n)
{
    for (size_t i = 0; i < n; i++) free(ops[i].text);
}

void tc_free(tc_buffer *b)
{
    if (!b) return;
    op_clear(b->undo, b->undo_len);
    op_clear(b->redo, b->redo_len);
    free(b->undo); free(b->redo);
    free(b->composition);
    free(b->comp_segs);
    free(b->buf);
    free(b);
}

/* ------------------------------------------------------------------ */
/* Content access                                                      */
/* ------------------------------------------------------------------ */

size_t tc_length(const tc_buffer *b) { return content_len(b); }

size_t tc_count(const tc_buffer *b)
{
    size_t n = 0, off = 0, total = content_len(b);
    while (off < total) { off += tc_cp_len_at(b, off); n++; }
    return n;
}

size_t tc_text(const tc_buffer *b, char *out, size_t cap)
{
    return tc_range_text(b, 0, content_len(b), out, cap);
}

size_t tc_range_text(const tc_buffer *b, size_t start, size_t end,
                     char *out, size_t cap)
{
    size_t total = content_len(b);
    if (start > total) start = total;
    if (end > total) end = total;
    if (end < start) end = start;
    size_t len = end - start;
    if (out && cap) {
        size_t w = len < cap - 1 ? len : cap - 1;
        for (size_t i = 0; i < w; i++) out[i] = byte_at(b, start + i);
        out[w] = '\0';
    }
    return len;
}

void tc_set_text(tc_buffer *b, const char *utf8)
{
    if (!utf8) utf8 = "";
    /* reset buffer */
    b->gap_start = 0; b->gap_end = b->cap;
    raw_insert(b, 0, utf8, strlen(utf8));
    b->cursor = b->anchor = 0;
    op_clear(b->undo, b->undo_len); b->undo_len = 0;
    op_clear(b->redo, b->redo_len); b->redo_len = 0;
    b->last_op_kind = 0;
    tc_set_composition(b, NULL);
}

void tc_append(tc_buffer *b, const char *utf8)
{
    if (!utf8) return;
    raw_insert(b, content_len(b), utf8, strlen(utf8));
}

/* ------------------------------------------------------------------ */
/* Cursor / selection                                                  */
/* ------------------------------------------------------------------ */

size_t tc_cursor(const tc_buffer *b) { return b->cursor; }
size_t tc_anchor(const tc_buffer *b) { return b->anchor; }
bool tc_has_selection(const tc_buffer *b) { return b->cursor != b->anchor; }

void tc_selection_range(const tc_buffer *b, size_t *lo, size_t *hi)
{
    if (b->cursor < b->anchor) { *lo = b->cursor; *hi = b->anchor; }
    else { *lo = b->anchor; *hi = b->cursor; }
}

static size_t snap_boundary(const tc_buffer *b, size_t off)
{
    size_t total = content_len(b);
    if (off > total) off = total;
    /* if off is mid-codepoint, back up to its start */
    while (off > 0 && off < total) {
        unsigned char c = (unsigned char)byte_at(b, off);
        if ((c & 0xC0) != 0x80) break;
        off--;
    }
    return off;
}

void tc_move_to(tc_buffer *b, size_t byte_off, bool extend)
{
    byte_off = snap_boundary(b, byte_off);
    b->cursor = byte_off;
    if (!extend) b->anchor = byte_off;
    b->last_op_kind = 0;
}

static bool cp_is_word(uint32_t cp)
{
    if (cp == '_' ) return true;
    if (cp >= '0' && cp <= '9') return true;
    if (cp >= 'A' && cp <= 'Z') return true;
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 0x3040 && cp <= 0x30FF) return true; /* kana */
    if (cp >= 0x3400 && cp <= 0x9FFF) return true; /* CJK */
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true; /* hangul */
    if (cp >= 0xC0 && cp < 0x300) return true;     /* latin ext */
    return false;
}

static size_t line_start_of(const tc_buffer *b, size_t off)
{
    while (off > 0 && byte_at(b, off - 1) != '\n') off--;
    return off;
}

static size_t line_end_of(const tc_buffer *b, size_t off)
{
    size_t total = content_len(b);
    while (off < total && byte_at(b, off) != '\n') off++;
    return off;
}

void tc_move(tc_buffer *b, tc_move_unit unit, int dir, bool extend)
{
    size_t pos = b->cursor, total = content_len(b);
    if (dir == 0) dir = 1;
    switch (unit) {
    case TC_MOVE_CODEPOINT:
        if (dir < 0) { if (pos > 0) { size_t l; (void)cp_before(b, pos, &l); pos -= l ? l : 1; } }
        else { if (pos < total) pos += tc_cp_len_at(b, pos); }
        break;
    case TC_MOVE_CLUSTER:
        if (dir < 0) pos = tc_cluster_before(b, pos);
        else pos = tc_cluster_after(b, pos);
        break;
    case TC_MOVE_WORD:
        if (dir < 0) {
            while (pos > 0) { size_t l; uint32_t cp = cp_before(b, pos, &l); if (cp_is_word(cp)) break; pos -= l ? l : 1; }
            while (pos > 0) { size_t l; uint32_t cp = cp_before(b, pos, &l); if (!cp_is_word(cp)) break; pos -= l ? l : 1; }
        } else {
            while (pos < total) { size_t l; uint32_t cp = cp_at(b, pos, &l); if (cp_is_word(cp)) break; pos += l ? l : 1; }
            while (pos < total) { size_t l; uint32_t cp = cp_at(b, pos, &l); if (!cp_is_word(cp)) break; pos += l ? l : 1; }
        }
        break;
    case TC_MOVE_LINE_START: pos = line_start_of(b, pos); break;
    case TC_MOVE_LINE_END: pos = line_end_of(b, pos); break;
    case TC_MOVE_DOC_START: pos = 0; break;
    case TC_MOVE_DOC_END: pos = total; break;
    }
    tc_move_to(b, pos, extend);
}

void tc_select_all(tc_buffer *b)
{
    b->anchor = 0;
    b->cursor = content_len(b);
    b->last_op_kind = 0;
}

void tc_collapse_selection(tc_buffer *b, bool to_end)
{
    size_t lo, hi;
    lo = b->cursor < b->anchor ? b->cursor : b->anchor;
    hi = b->cursor < b->anchor ? b->anchor : b->cursor;
    b->cursor = b->anchor = to_end ? hi : lo;
}

/* ------------------------------------------------------------------ */
/* Undo machinery                                                      */
/* ------------------------------------------------------------------ */

#define TC_UNDO_MAX_OPS 512
#define TC_UNDO_MAX_BYTES (1u << 20)

static void push_op(struct tc_op **arr, size_t *len, size_t *cap,
                    const struct tc_op *op)
{
    if (*len >= *cap) {
        size_t ncap = *cap ? *cap * 2 : 32;
        struct tc_op *na = realloc(*arr, ncap * sizeof(*na));
        if (!na) return;
        *arr = na; *cap = ncap;
    }
    (*arr)[(*len)++] = *op;
    /* enforce caps by dropping the oldest */
    while (*len > TC_UNDO_MAX_OPS) {
        free((*arr)[0].text);
        memmove(*arr, *arr + 1, (*len - 1) * sizeof(**arr));
        (*len)--;
    }
}

static void record(tc_buffer *b, int kind, size_t pos, char *text, size_t len,
                   size_t cur_before, size_t anch_before,
                   size_t cur_after, size_t anch_after, int coalesce_kind)
{
    /* clear redo on any new edit */
    op_clear(b->redo, b->redo_len); b->redo_len = 0;

    if (coalesce_kind != 0 && b->last_op_kind == coalesce_kind && b->undo_len > 0) {
        struct tc_op *top = &b->undo[b->undo_len - 1];
        bool merge = false;
        if (kind == OP_INSERT && top->kind == OP_INSERT &&
            pos == top->pos + top->len) {
            /* append to the insert */
            char *nt = realloc(top->text, top->len + len + 1);
            if (nt) {
                memcpy(nt + top->len, text, len);
                top->len += len; nt[top->len] = '\0';
                top->text = nt; merge = true;
            }
        } else if (kind == OP_DELETE && top->kind == OP_DELETE &&
                   coalesce_kind == 2 && pos + len == top->pos) {
            /* backspace: prepend */
            char *nt = malloc(top->len + len + 1);
            if (nt) {
                memcpy(nt, text, len);
                memcpy(nt + len, top->text, top->len);
                nt[len + top->len] = '\0';
                free(top->text); top->text = nt;
                top->len += len; top->pos = pos; merge = true;
            }
        } else if (kind == OP_DELETE && top->kind == OP_DELETE &&
                   coalesce_kind == 3 && pos == top->pos) {
            char *nt = realloc(top->text, top->len + len + 1);
            if (nt) {
                memcpy(nt + top->len, text, len);
                top->len += len; nt[top->len] = '\0';
                top->text = nt; merge = true;
            }
        }
        if (merge) {
            top->cursor_after = cur_after;
            top->anchor_after = anch_after;
            free(text);
            b->last_op_kind = coalesce_kind;
            return;
        }
    }
    struct tc_op op;
    op.kind = kind; op.pos = pos; op.text = text; op.len = len;
    op.cursor_before = cur_before; op.anchor_before = anch_before;
    op.cursor_after = cur_after; op.anchor_after = anch_after;
    push_op(&b->undo, &b->undo_len, &b->undo_cap, &op);
    b->last_op_kind = coalesce_kind;
    (void)TC_UNDO_MAX_BYTES;
}

void tc_break_undo_group(tc_buffer *b) { b->last_op_kind = 0; }

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

void tc_insert(tc_buffer *b, const char *utf8)
{
    if (!utf8 || !*utf8) return;
    size_t lo, hi; tc_selection_range(b, &lo, &hi);
    size_t cur_before = b->cursor, anch_before = b->anchor;
    if (hi > lo) {
        char *del = dup_range(b, lo, hi - lo);
        raw_delete(b, lo, hi - lo);
        struct tc_op op; op.kind = OP_DELETE; op.pos = lo; op.text = del;
        op.len = hi - lo; op.cursor_before = cur_before;
        op.anchor_before = anch_before;
        op.cursor_after = lo; op.anchor_after = lo;
        op_clear(b->redo, b->redo_len); b->redo_len = 0;
        push_op(&b->undo, &b->undo_len, &b->undo_cap, &op);
        b->cursor = b->anchor = lo;
        b->last_op_kind = 0;
        cur_before = anch_before = lo;
    }
    size_t len = strlen(utf8);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, utf8, len + 1);
    raw_insert(b, b->cursor, utf8, len);
    b->cursor = b->anchor = b->cursor + len;
    record(b, OP_INSERT, cur_before, copy, len, cur_before, anch_before,
           b->cursor, b->anchor, 1);
}

bool tc_delete(tc_buffer *b, int dir)
{
    size_t lo, hi; tc_selection_range(b, &lo, &hi);
    if (hi > lo) {
        size_t cur_before = b->cursor, anch_before = b->anchor;
        char *del = dup_range(b, lo, hi - lo);
        raw_delete(b, lo, hi - lo);
        b->cursor = b->anchor = lo;
        record(b, OP_DELETE, lo, del, hi - lo, cur_before, anch_before,
               lo, lo, 0);
        return true;
    }
    if (dir < 0) {
        if (b->cursor == 0) return false;
        size_t start = tc_cluster_before(b, b->cursor);
        size_t cur_before = b->cursor;
        char *del = dup_range(b, start, b->cursor - start);
        size_t len = b->cursor - start;
        raw_delete(b, start, len);
        b->cursor = b->anchor = start;
        record(b, OP_DELETE, start, del, len, cur_before, cur_before,
               start, start, 2);
        return true;
    } else {
        size_t total = content_len(b);
        if (b->cursor >= total) return false;
        size_t end = tc_cluster_after(b, b->cursor);
        size_t cur_before = b->cursor;
        char *del = dup_range(b, b->cursor, end - b->cursor);
        size_t len = end - b->cursor;
        raw_delete(b, b->cursor, len);
        record(b, OP_DELETE, cur_before, del, len, cur_before, cur_before,
               cur_before, cur_before, 3);
        return true;
    }
}

void tc_delete_range(tc_buffer *b, size_t start, size_t end)
{
    start = snap_boundary(b, start);
    end = snap_boundary(b, end);
    if (end <= start) return;
    size_t cur_before = b->cursor, anch_before = b->anchor;
    char *del = dup_range(b, start, end - start);
    raw_delete(b, start, end - start);
    if (b->cursor > start) b->cursor = start;
    if (b->anchor > start) b->anchor = start;
    record(b, OP_DELETE, start, del, end - start, cur_before, anch_before,
           b->cursor, b->anchor, 0);
}

bool tc_undo(tc_buffer *b)
{
    if (b->undo_len == 0) return false;
    struct tc_op op = b->undo[--b->undo_len];
    if (op.kind == OP_INSERT) raw_delete(b, op.pos, op.len);
    else raw_insert(b, op.pos, op.text, op.len);
    b->cursor = op.cursor_before;
    b->anchor = op.anchor_before;
    push_op(&b->redo, &b->redo_len, &b->redo_cap, &op);
    b->last_op_kind = 0;
    return true;
}

bool tc_redo(tc_buffer *b)
{
    if (b->redo_len == 0) return false;
    struct tc_op op = b->redo[--b->redo_len];
    if (op.kind == OP_INSERT) raw_insert(b, op.pos, op.text, op.len);
    else raw_delete(b, op.pos, op.len);
    b->cursor = op.cursor_after;
    b->anchor = op.anchor_after;
    push_op(&b->undo, &b->undo_len, &b->undo_cap, &op);
    b->last_op_kind = 0;
    return true;
}

bool tc_can_undo(const tc_buffer *b) { return b->undo_len > 0; }
bool tc_can_redo(const tc_buffer *b) { return b->redo_len > 0; }

/* ------------------------------------------------------------------ */
/* Clipboard                                                           */
/* ------------------------------------------------------------------ */

void tc_set_clipboard(tc_buffer *b, const tc_clipboard *cb)
{
    if (cb) { b->clip = *cb; b->clip_set = true; }
    else { memset(&b->clip, 0, sizeof(b->clip)); b->clip_set = false; }
}

bool tc_copy(tc_buffer *b)
{
    if (!b->clip_set || !b->clip.set_text) return false;
    size_t lo, hi; tc_selection_range(b, &lo, &hi);
    if (hi == lo) return false;
    char *s = dup_range(b, lo, hi - lo);
    if (!s) return false;
    b->clip.set_text(b->clip.userdata, s, hi - lo);
    free(s);
    return true;
}

bool tc_cut(tc_buffer *b)
{
    if (!tc_copy(b)) return false;
    return tc_delete(b, -1); /* selection path ignores dir */
}

bool tc_paste(tc_buffer *b)
{
    if (!b->clip_set || !b->clip.get_text) return false;
    size_t need = b->clip.get_text(b->clip.userdata, NULL, 0);
    if (need == 0) return false;
    char *s = malloc(need + 1);
    if (!s) return false;
    b->clip.get_text(b->clip.userdata, s, need + 1);
    tc_break_undo_group(b);
    tc_insert(b, s);
    tc_break_undo_group(b);
    free(s);
    return true;
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

void tc_set_composition(tc_buffer *b, const char *utf8)
{
    tc_set_composition_segments(b, utf8, NULL, 0, 0, false);
}

void tc_set_composition_segments(tc_buffer *b, const char *utf8,
                                 const size_t *seg_lens, size_t n_seg,
                                 int active_seg, bool highlight)
{
    free(b->composition);
    free(b->comp_segs);
    b->composition = utf8 ? strdup(utf8) : NULL;
    b->comp_segs = NULL;
    b->comp_nseg = 0;
    b->comp_active = 0;
    b->comp_highlight = false;
    if (!b->composition || !seg_lens || n_seg == 0) return;
    /* Validate: the segment lengths must tile the string exactly.
     * A mismatch means the producer's segment view is stale - fall
     * back to one unsegmented composition rather than rendering
     * wrong boundaries. */
    size_t total = strlen(b->composition), sum = 0;
    for (size_t i = 0; i < n_seg; i++) {
        if (seg_lens[i] == 0 || seg_lens[i] > total) return;
        sum += seg_lens[i];
    }
    if (sum != total) return;
    b->comp_segs = malloc(n_seg * sizeof(size_t));
    if (!b->comp_segs) return;
    memcpy(b->comp_segs, seg_lens, n_seg * sizeof(size_t));
    b->comp_nseg = n_seg;
    b->comp_active = active_seg >= 0 && (size_t)active_seg < n_seg
                     ? active_seg : 0;
    b->comp_highlight = highlight;
}

size_t tc_composition(const tc_buffer *b, char *out, size_t cap)
{
    const char *s = b->composition ? b->composition : "";
    size_t len = strlen(s);
    if (out && cap) {
        size_t w = len < cap - 1 ? len : cap - 1;
        memcpy(out, s, w); out[w] = '\0';
    }
    return len;
}

size_t tc_composition_segments(const tc_buffer *b, size_t *lens_out,
                               size_t max)
{
    if (!b->comp_segs) {
        /* Unsegmented: report a single segment spanning the string. */
        if (lens_out && max > 0 && b->composition)
            lens_out[0] = strlen(b->composition);
        return b->composition ? 1 : 0;
    }
    if (lens_out) {
        size_t w = b->comp_nseg < max ? b->comp_nseg : max;
        memcpy(lens_out, b->comp_segs, w * sizeof(size_t));
    }
    return b->comp_nseg;
}

int tc_composition_active(const tc_buffer *b) { return b->comp_active; }
bool tc_composition_highlight(const tc_buffer *b) { return b->comp_highlight; }

/* ------------------------------------------------------------------ */
/* Line layout bridge                                                  */
/* ------------------------------------------------------------------ */

size_t tc_line_count(const tc_buffer *b)
{
    size_t n = 1, total = content_len(b);
    for (size_t i = 0; i < total; i++)
        if (byte_at(b, i) == '\n') n++;
    return n;
}

bool tc_line_range(const tc_buffer *b, size_t line, size_t *start, size_t *len)
{
    size_t total = content_len(b);
    size_t cur_line = 0, s = 0;
    for (size_t i = 0; i <= total; i++) {
        if (i == total || byte_at(b, i) == '\n') {
            if (cur_line == line) {
                if (start) *start = s;
                if (len) *len = i - s;
                return true;
            }
            cur_line++; s = i + 1;
        }
    }
    return false;
}

void tc_offset_to_linecol(const tc_buffer *b, size_t off, size_t *line, size_t *col_cp)
{
    off = snap_boundary(b, off);
    size_t ls = line_start_of(b, off);
    size_t ln = 0;
    for (size_t i = 0; i < ls; i++)
        if (byte_at(b, i) == '\n') ln++;
    size_t col = 0, p = ls;
    while (p < off) { p += tc_cp_len_at(b, p); col++; }
    if (line) *line = ln;
    if (col_cp) *col_cp = col;
}

size_t tc_linecol_to_offset(const tc_buffer *b, size_t line, size_t col_cp)
{
    size_t start, len;
    if (!tc_line_range(b, line, &start, &len)) {
        return content_len(b);
    }
    size_t p = start, end = start + len, col = 0;
    while (p < end && col < col_cp) { p += tc_cp_len_at(b, p); col++; }
    return p;
}

void tc_wrap_line(const tc_buffer *b, size_t line, int max_cols,
                  tc_width_fn width_fn, void *width_ud,
                  tc_wrap_cb cb, void *cb_ud)
{
    size_t start, len;
    if (!tc_line_range(b, line, &start, &len)) return;
    if (max_cols <= 0 || !width_fn) { cb(cb_ud, start, len); return; }
    size_t end = start + len;
    size_t seg_start = start, p = start;
    int cols = 0;
    size_t last_break = (size_t)-1; /* byte offset after last space */
    while (p < end) {
        size_t cl; uint32_t cp = cp_at(b, p, &cl);
        if (cl == 0) cl = 1;
        int w = width_fn(width_ud, cp);
        if (w < 0) w = 0;
        if (cp == ' ' || cp == 0x3000) last_break = p + cl;
        if (cols + w > max_cols && p > seg_start) {
            size_t cut = (last_break != (size_t)-1 && last_break > seg_start)
                         ? last_break : p;
            cb(cb_ud, seg_start, cut - seg_start);
            seg_start = cut;
            /* skip a leading space at the wrap point */
            if (seg_start < end && byte_at(b, seg_start) == ' ')
                seg_start += 1;
            p = seg_start; cols = 0; last_break = (size_t)-1;
            continue;
        }
        cols += w; p += cl;
    }
    cb(cb_ud, seg_start, end - seg_start);
}

/* --- search (M5) --------------------------------------------------
 * Plain substring search over the logical content. The gap buffer
 * gives O(1) random access (byte_at), so the scan never needs a
 * flattened copy of the document - important for the notes app,
 * which previously snapshot the whole text to strstr() it.
 * Case-insensitive matching folds ASCII A-Z only (documented
 * limit; Japanese text is unaffected by case). Returns the byte
 * offset of the first match at or after `from`, or -1. */
static bool find_match_at(const tc_buffer *b, size_t off,
                          const char *needle, size_t nlen, bool ci)
{
    for (size_t i = 0; i < nlen; i++) {
        char a = byte_at(b, off + i);
        char n = needle[i];
        if (ci) {
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (n >= 'A' && n <= 'Z') n = (char)(n - 'A' + 'a');
        }
        if (a != n) return false;
    }
    return true;
}

long tc_find(const tc_buffer *b, const char *needle, size_t from,
             bool case_insensitive)
{
    if (!b || !needle) return -1;
    size_t nlen = strlen(needle);
    size_t total = content_len(b);
    if (nlen == 0 || nlen > total) return -1;
    if (from > total - nlen) return -1;
    for (size_t i = from; i + nlen <= total; i++)
        if (find_match_at(b, i, needle, nlen, case_insensitive))
            return (long)i;
    return -1;
}

long tc_find_next(const tc_buffer *b, const char *needle,
                  bool case_insensitive)
{
    if (!b || !needle) return -1;
    /* start just past the cursor so repeated "find next" walks
     * forward; wrap once to the document start */
    long r = tc_find(b, needle, b->cursor + 1, case_insensitive);
    if (r < 0)
        r = tc_find(b, needle, 0, case_insensitive);
    return r;
}

/* Replace the match at byte offset `at` (nlen bytes) with
 * `replacement` (rlen bytes) and select the inserted text. */
static void replace_at(tc_buffer *b, size_t at, size_t nlen,
                       const char *replacement, size_t rlen)
{
    tc_delete_range(b, at, at + nlen);
    tc_move_to(b, at, false);
    if (rlen) tc_insert(b, replacement); /* NUL-terminated, rlen bytes */
    /* select the replacement: anchor at its start, cursor at end */
    tc_move_to(b, at, false);
    tc_move_to(b, at + rlen, true);
}

long tc_replace(tc_buffer *b, const char *needle,
                const char *replacement, bool case_insensitive)
{
    if (!b || !needle || !replacement) return -1;
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > content_len(b)) return -1;
    size_t rlen = strlen(replacement);
    /* an exact-match selection is replaced in place */
    if (tc_has_selection(b)) {
        size_t lo, hi;
        tc_selection_range(b, &lo, &hi);
        if (hi - lo == nlen &&
            find_match_at(b, lo, needle, nlen, case_insensitive)) {
            replace_at(b, lo, nlen, replacement, rlen);
            return (long)lo;
        }
    }
    long at = tc_find(b, needle, b->cursor, case_insensitive);
    if (at < 0) at = tc_find(b, needle, 0, case_insensitive);
    if (at < 0) return -1;
    replace_at(b, (size_t)at, nlen, replacement, rlen);
    return at;
}

int tc_replace_all(tc_buffer *b, const char *needle,
                   const char *replacement, bool case_insensitive)
{
    if (!b || !needle || !replacement) return 0;
    size_t nlen = strlen(needle);
    if (nlen == 0) return 0;
    size_t rlen = strlen(replacement);
    tc_break_undo_group(b);
    int count = 0;
    size_t pos = 0;
    for (;;) {
        long at = tc_find(b, needle, pos, case_insensitive);
        if (at < 0) break;
        replace_at(b, (size_t)at, nlen, replacement, rlen);
        count++;
        pos = (size_t)at + rlen; /* never rescan the replacement */
        if (rlen == 0 && nlen == 0) break; /* unreachable; safety */
    }
    return count;
}
