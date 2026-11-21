/* textcore - shared text editing core for libbrainui2.
 *
 * UTF-8 text buffer (gap buffer), cursor + selection, undo/redo,
 * clipboard via callbacks, and a line-layout bridge for views.
 * See DESIGN-toolkit-v2.md section 8 for the design rationale and
 * the documented limits of the grapheme approximation.
 */
#ifndef TEXTCORE_H
#define TEXTCORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tc_buffer tc_buffer;

/* Clipboard abstraction: the core never touches an OS clipboard.
 * The host/backend supplies set/get callbacks. */
typedef struct {
    void *userdata;
    /* Store text (NUL-terminated UTF-8, len bytes excluding NUL). */
    void (*set_text)(void *userdata, const char *text, size_t len);
    /* Fetch text: writes up to cap-1 bytes + NUL, returns full length. */
    size_t (*get_text)(void *userdata, char *out, size_t cap);
} tc_clipboard;

/* Width function for layout: columns occupied by one codepoint. */
typedef int (*tc_width_fn)(void *userdata, uint32_t codepoint);

tc_buffer *tc_new(void);
void tc_free(tc_buffer *b);

/* --- content --- */
/* Replace the whole content; clears undo history and selection. */
void tc_set_text(tc_buffer *b, const char *utf8);
/* Append without touching cursor (utility for loaders). */
void tc_append(tc_buffer *b, const char *utf8);
size_t tc_length(const tc_buffer *b);          /* bytes */
size_t tc_count(const tc_buffer *b);           /* codepoints */
/* Copy the full text into out (cap includes NUL). Returns bytes needed. */
size_t tc_text(const tc_buffer *b, char *out, size_t cap);
/* Copy range [start,end) in bytes. Returns bytes written (excl. NUL). */
size_t tc_range_text(const tc_buffer *b, size_t start, size_t end,
                     char *out, size_t cap);

/* --- cursor & selection (all offsets are byte offsets, always kept
 *     on codepoint boundaries) --- */
size_t tc_cursor(const tc_buffer *b);
size_t tc_anchor(const tc_buffer *b);          /* selection anchor */
bool tc_has_selection(const tc_buffer *b);
void tc_selection_range(const tc_buffer *b, size_t *lo, size_t *hi);
/* Move cursor to byte offset (clamped, snapped back to a boundary).
 * extend=true keeps the anchor (extends selection). */
void tc_move_to(tc_buffer *b, size_t byte_off, bool extend);
/* Movement units. */
typedef enum {
    TC_MOVE_CODEPOINT,
    TC_MOVE_CLUSTER,     /* grapheme-cluster approximation */
    TC_MOVE_WORD,
    TC_MOVE_LINE_START,
    TC_MOVE_LINE_END,
    TC_MOVE_DOC_START,
    TC_MOVE_DOC_END
} tc_move_unit;
void tc_move(tc_buffer *b, tc_move_unit unit, int dir, bool extend);
void tc_select_all(tc_buffer *b);
void tc_collapse_selection(tc_buffer *b, bool to_end);

/* --- editing (all recorded in undo history) --- */
/* Insert UTF-8 text at cursor, replacing any selection. */
void tc_insert(tc_buffer *b, const char *utf8);
/* Delete selection, or dir<0: cluster before cursor, dir>0: cluster
 * after cursor. Returns true if something was deleted. */
bool tc_delete(tc_buffer *b, int dir);
/* Delete an explicit byte range (must be boundary-aligned). */
void tc_delete_range(tc_buffer *b, size_t start, size_t end);

/* --- search (M5) --- */
/* First match of `needle` (UTF-8 bytes) at or after byte offset
 * `from`; returns its byte offset or -1. case_insensitive folds
 * ASCII A-Z only. The scan reads through the gap buffer - no
 * flattened copy of the document is made. */
long tc_find(const tc_buffer *b, const char *needle, size_t from,
             bool case_insensitive);
/* "Find next" from just past the cursor, wrapping once to the
 * document start. Returns the match offset or -1. Does not move
 * the cursor (callers select the match with tc_move_to). */
long tc_find_next(const tc_buffer *b, const char *needle,
                  bool case_insensitive);
/* Replace one occurrence (audit M6: search had find but no
 * replace at all). If the current selection is exactly a match,
 * it is replaced; otherwise the next match at/after the cursor
 * (wrapping once) is replaced. The replacement text ends up
 * selected, so repeated calls walk forward through the document.
 * Returns the byte offset of the replacement, or -1 when there
 * is no match. The edit goes through the undo history. */
long tc_replace(tc_buffer *b, const char *needle,
                const char *replacement, bool case_insensitive);
/* Replace every non-overlapping occurrence, scanning from the
 * document start; the scan resumes after each inserted
 * replacement (so a replacement that itself contains the needle
 * cannot loop). Returns the number of replacements made. Each
 * replacement is its own undo step; the group is broken first so
 * a replace-all never merges with preceding typing. */
int tc_replace_all(tc_buffer *b, const char *needle,
                   const char *replacement, bool case_insensitive);

/* --- undo/redo --- */
bool tc_undo(tc_buffer *b);
bool tc_redo(tc_buffer *b);
bool tc_can_undo(const tc_buffer *b);
bool tc_can_redo(const tc_buffer *b);
/* Break coalescing (e.g. on cursor jump or explicit user action). */
void tc_break_undo_group(tc_buffer *b);

/* --- clipboard operations (no-ops returning false when the buffer
 *     has no clipboard provider or nothing is selected) --- */
bool tc_copy(tc_buffer *b);
bool tc_cut(tc_buffer *b);
bool tc_paste(tc_buffer *b);
void tc_set_clipboard(tc_buffer *b, const tc_clipboard *cb);

/* --- IME composition (preedit): not part of the buffer content --- */
void tc_set_composition(tc_buffer *b, const char *utf8); /* NULL clears */
size_t tc_composition(const tc_buffer *b, char *out, size_t cap);
/* Segmented composition (M2): seg_lens are byte lengths tiling the
 * composition string exactly (otherwise the segmentation is dropped
 * and the string is kept unsegmented). active_seg is the segment
 * under conversion; highlight marks it candidate-browsing. The plain
 * tc_set_composition() above is the single-segment form. */
void tc_set_composition_segments(tc_buffer *b, const char *utf8,
                                 const size_t *seg_lens, size_t n_seg,
                                 int active_seg, bool highlight);
/* Segment byte lengths: returns the segment count (1 for an
 * unsegmented composition, 0 when there is none) and writes up to
 * `max` lengths into lens_out. */
size_t tc_composition_segments(const tc_buffer *b, size_t *lens_out,
                               size_t max);
int tc_composition_active(const tc_buffer *b);
bool tc_composition_highlight(const tc_buffer *b);

/* --- line layout bridge --- */
size_t tc_line_count(const tc_buffer *b);
/* Byte range of logical line `line` (excluding the trailing '\n'). */
bool tc_line_range(const tc_buffer *b, size_t line, size_t *start, size_t *len);
void tc_offset_to_linecol(const tc_buffer *b, size_t off, size_t *line, size_t *col_cp);
size_t tc_linecol_to_offset(const tc_buffer *b, size_t line, size_t col_cp);
/* Enumerate wrapped lines for a logical line: calls cb for each
 * visual line with its byte start/length (excluding newline).
 * Width is measured with width_fn in columns; max_cols<=0 means no wrap. */
typedef void (*tc_wrap_cb)(void *userdata, size_t start, size_t len);
void tc_wrap_line(const tc_buffer *b, size_t line, int max_cols,
                  tc_width_fn width_fn, void *width_ud,
                  tc_wrap_cb cb, void *cb_ud);

/* --- UTF-8 helpers (also used by the toolkit) --- */
/* Decode the codepoint starting at s (len bytes available).
 * Returns codepoint and stores its byte length in *out_len (1 on error). */
uint32_t tc_utf8_decode(const char *s, size_t len, size_t *out_len);
/* Byte length of the codepoint starting at byte offset `off`. */
size_t tc_cp_len_at(const tc_buffer *b, size_t off);
/* True if cp attaches to the previous cluster (combining mark,
 * variation selector, emoji modifier, ZWJ handling is in cluster fns). */
bool tc_cp_is_combining(uint32_t cp);
/* Byte offset of the start of the cluster ending at `off` (off>0),
 * and of the end of the cluster starting at `off`. */
size_t tc_cluster_before(const tc_buffer *b, size_t off);
size_t tc_cluster_after(const tc_buffer *b, size_t off);

#endif /* TEXTCORE_H */
