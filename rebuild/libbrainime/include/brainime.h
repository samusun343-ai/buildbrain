/* libbrainime — Brainux in-house Japanese conversion engine.
 *
 * A C library (no dependencies beyond libc) that turns a hiragana
 * reading into kanji-kana mixed text using the BIDF v2 dictionary
 * (LOUDS tries + token array, mmap'd read-only) and a
 * lattice + Viterbi converter with a hand-built connection-cost
 * model over the dictionary's 52 POS classes.
 *
 * Design notes live in ../DESIGN.md.  The dictionary file itself is
 * an asset produced by buildbrain's tools/ime pipeline; this library
 * only reads it.
 *
 * Threading: one bi_engine may be shared by threads only if calls
 * are serialised by the caller.  bi_result / bi_session objects are
 * single-threaded.
 */
#ifndef BRAINIME_H
#define BRAINIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bi_engine bi_engine;
typedef struct bi_result bi_result;
typedef struct bi_session bi_session;

/* ------------------------------------------------------------------ */
/* Engine lifecycle                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
	unsigned n_entries;	/* dictionary entries (tokens) */
	unsigned n_readings;	/* distinct readings */
	unsigned n_surfaces;	/* distinct surfaces */
	size_t	 file_bytes;	/* dictionary file size */
	int	 conn_from_dict; /* 1 if the BIDF conn section was
				    non-zero and is being used */
	int	 lexicalized;	/* M2: 1 if a .bisc sidecar with mozc
				    lid/rid ids + connection matrix
				    was loaded and is driving the
				    connection costs */
	int	 learn_entries;	/* M2: distinct (reading, surface)
				    pairs in the learning history */
	/* M3 additions (zero when a BIDF v2 file is opened) */
	int	 format_v3;	/* 1 if the file is a BIDF v3 (.bid3) */
	unsigned n_supp;	/* supplement entries (non-mozc sources) */
	unsigned n_bigram;	/* bigram pairs in the BIGR section */
	unsigned n_prior;	/* M7: tokens with a PRIO cost delta */
	unsigned long surf_hits;   /* surface-cache hits */
	unsigned long surf_misses; /* surface-cache misses */
} bi_stats;

/* Open a BIDF v2 dictionary, or a BIDF v3 (.bid3) file (M3:
 * dictionary + lexicalised connection matrix + supplement
 * vocabulary + corpus bigrams + reranker weights in one file,
 * every section read in place from the mapping.  Integrity:
 * the sections the loader itself parses (DICT, plus the small
 * control sections) are CRC32-verified at open and a corrupt
 * file is rejected there; the remaining sections are
 * CRC32-verified at first conversion, before their bytes are
 * ever used, and a mismatch fails the conversion (M4;
 * BI_CRC_FULL=1 restores verify-everything-at-open).
 * The file is mmap'd MAP_SHARED read-only: several processes
 * (shell, greeter, fcitx addon) share the same pages, which is
 * what keeps the resident cost near zero on a 128 MB machine.
 * Returns NULL on error. */
bi_engine *bi_engine_open(const char *bidf_path);
void bi_engine_close(bi_engine *e);
void bi_engine_stats(const bi_engine *e, bi_stats *out);

/* ------------------------------------------------------------------ */
/* Learning (M2, minimal)                                              */
/*                                                                     */
/* When enabled, every segment committed through a session is        */
/* recorded as a (reading, surface) pair in a small TSV history      */
/* file (reading <TAB> surface <TAB> count).  Dictionary nodes       */
/* whose (reading, surface) pair appears in the history get a        */
/* word-cost bonus in later conversions (capped, so history nudges   */
/* rather than overrides the dictionary).  Recording happens on      */
/* commit only — merely viewing candidates teaches nothing.          */
/*                                                                   */
/* Crash safety (the target resets/power-cuts routinely): saves      */
/* are atomic (write "<path>.tmp", fsync, rename, fsync the          */
/* directory) and loads accept only complete, strictly-shaped        */
/* lines — a torn tail left by a power cut is discarded at load.     */
/* A corrupt history can lose bonuses; it can NEVER make this        */
/* call or the engine fail: the only failure return is an            */
/* unwritable path, which merely disables learning.  The history     */
/* is capped at 4096 distinct pairs (no time-based expiry — the      */
/* device clock is untrusted).                                       */
/* Returns 0 on success, -1 if the file cannot be written.           */
/* ------------------------------------------------------------------ */
int bi_engine_enable_learning(bi_engine *e, const char *path);

/* ------------------------------------------------------------------ */
/* One-shot conversion                                                 */
/* ------------------------------------------------------------------ */

/* Convert a UTF-8 kana reading (hiragana; katakana is normalised,
 * ASCII/digit runs pass through with numeral candidates) to the
 * best kanji-kana mixed result.  Returns NULL on bad input. */
bi_result *bi_convert(bi_engine *e, const char *reading_utf8);

/* Convert with forced segment boundaries.  bounds[] are character
 * (not byte) offsets into the reading at which a segment cut is
 * required, sorted ascending, excluding 0 and the total length.
 * This is the foundation for bunsetsu resize in the OSK: the UI
 * collects the user's desired cuts and re-converts under them. */
bi_result *bi_convert_forced(bi_engine *e, const char *reading_utf8,
			     const int *bounds, int n_bounds);
void bi_result_free(bi_result *r);

/* N-best full-sentence alternatives (index 0 == best path). */
int bi_result_nbest_count(const bi_result *r);
const char *bi_result_nbest(const bi_result *r, int rank);
long bi_result_nbest_cost(const bi_result *r, int rank);

/* Segments of the best path. */
int bi_result_segment_count(const bi_result *r);
/* Character offset/length of segment i within the reading. */
int bi_result_segment_start(const bi_result *r, int i);
int bi_result_segment_length(const bi_result *r, int i);
const char *bi_result_segment_reading(const bi_result *r, int i);
const char *bi_result_segment_surface(const bi_result *r, int i);
int bi_result_segment_pos(const bi_result *r, int i); /* POS class */

/* Candidates for segment i of the best path: every dictionary
 * surface for the segment's reading span, best first, followed by
 * the as-typed hiragana and katakana forms.  Candidate 0 is the
 * surface the converter chose. */
int bi_result_candidate_count(const bi_result *r, int i);
const char *bi_result_candidate(const bi_result *r, int i, int j);

/* ------------------------------------------------------------------ */
/* Session API — the connection point for the OSK / toolkit v2         */
/*                                                                     */
/* The candidate UI needs exactly four things from the engine:        */
/*   1. a candidate list for the current segment (above, via result)  */
/*   2. choosing a candidate without reconverting the whole string    */
/*   3. moving / resizing segment boundaries and reconverting         */
/*   4. committing the first segment and continuing with the rest     */
/* bi_session wraps a preedit string plus a result and provides       */
/* those operations.  The OSK draws the strip; the engine owns the    */
/* lattice.                                                           */
/* ------------------------------------------------------------------ */

bi_session *bi_session_new(bi_engine *e);
void bi_session_free(bi_session *s);

/* Replace the whole preedit (kana).  Re-converts. */
int bi_session_set_preedit(bi_session *s, const char *reading_utf8);
const char *bi_session_preedit(const bi_session *s);
/* Current conversion (owned by the session; valid until the next
 * mutating call). */
const bi_result *bi_session_result(bi_session *s);

/* Choose candidate j for segment i.  Only that segment's surface
 * changes; segmentation and the other segments are untouched. */
int bi_session_choose(bi_session *s, int seg, int cand);

/* Resize segment i to new_len characters (the cut after segment i
 * moves; later cuts are dropped, earlier cuts are kept as forced
 * boundaries) and reconvert.  new_len must be >= 1 and leave at
 * least one character for the rest of the preedit. */
int bi_session_resize(bi_session *s, int seg, int new_len);

/* Commit segment 0: writes its (possibly user-chosen) surface into
 * out, drops its reading from the preedit and reconverts the rest.
 * Returns the byte length written, or -1 when nothing remains. */
int bi_session_commit_first(bi_session *s, char *out, size_t outsz);

/* Whole-current-conversion string with user choices applied. */
const char *bi_session_text(bi_session *s);

#ifdef __cplusplus
}
#endif
#endif /* BRAINIME_H */
