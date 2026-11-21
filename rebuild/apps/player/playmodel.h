/* playmodel - bd-player library / playlists / play order.
 *
 * Library: recursive scan with a byte-budgeted metadata cache
 * (cache.tsv under the app root, atomically rewritten after
 * each scan). Files are re-probed only when (mtime, size)
 * change. When the serialized cache exceeds the budget, the
 * least-recently-scanned entries are evicted (the cache IS
 * the library index - on a 128 MB terminal an unbounded index
 * is not an option). Default budget 4 MiB (~thousands of
 * tracks); tests shrink it to prove eviction.
 *
 * Playlists: named path lists in playlists.tsv, atomic
 * rewrite on every mutation.
 *
 * Play order: a context list (the view the user started
 * playing from), an optional shuffle permutation (seeded -
 * deterministic in tests), a three-state repeat machine
 * (off/all/one; "one" binds only to automatic end-of-track
 * advance, never to a manual skip), and an Up Next queue
 * that takes precedence over the context order.
 *
 * Playback state (track, position, volume, shuffle, repeat)
 * persists atomically in state.txt so an abrupt reset loses
 * at most the last seconds - no clock-based expiry anywhere.
 */
#ifndef PLAYMODEL_H
#define PLAYMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char path[1024];
    char title[128];
    char artist[128];
    char album[128];
    char genre[64];
    uint32_t duration_ms;
    int64_t mtime;
    uint64_t size;
} pl_track;

enum { PL_F_TITLE, PL_F_ARTIST, PL_F_ALBUM, PL_F_GENRE, PL_F_PATH,
       PL_F_DURATION };

typedef struct pl_lib pl_lib;

pl_lib *pl_lib_open(const char *root);
void pl_lib_close(pl_lib *lib);
int pl_lib_scan(pl_lib *lib, const char *dir);
size_t pl_lib_count(const pl_lib *lib);
const pl_track *pl_lib_at(const pl_lib *lib, size_t i);
const pl_track *pl_lib_find(const pl_lib *lib, const char *path);
void pl_lib_sort(pl_lib *lib, int field, bool descending);
size_t pl_lib_probes(const pl_lib *lib);       /* pe_probe calls */
uint64_t pl_lib_cache_bytes(const pl_lib *lib);
void pl_lib_set_cache_budget(pl_lib *lib, uint64_t bytes);

/* Distinct values of a field, sorted; pointers are internal
 * (valid until the next scan/sort). */
size_t pl_lib_values(const pl_lib *lib, int field,
                     const char **out, size_t max);
/* Indices of tracks whose field equals value (case-sensitive),
 * in library order. */
size_t pl_lib_filter(const pl_lib *lib, int field, const char *value,
                     size_t *idx_out, size_t max);
size_t pl_lib_search(const pl_lib *lib, const char *needle,
                     size_t *idx_out, size_t max);

/* ---- playlists ---- */
size_t pl_lists_count(const pl_lib *lib);
const char *pl_lists_name(const pl_lib *lib, size_t i);
int pl_lists_create(pl_lib *lib, const char *name);
int pl_lists_delete(pl_lib *lib, const char *name);
int pl_lists_rename(pl_lib *lib, const char *old_name,
                    const char *new_name);
size_t pl_lists_items(const pl_lib *lib, const char *name);
const char *pl_lists_item(const pl_lib *lib, const char *name, size_t i);
int pl_lists_add(pl_lib *lib, const char *name, const char *path);
int pl_lists_remove_at(pl_lib *lib, const char *name, size_t i);
int pl_lists_move(pl_lib *lib, const char *name, size_t from, size_t to);

/* ---- play order ---- */
typedef enum { PL_REPEAT_OFF = 0, PL_REPEAT_ALL, PL_REPEAT_ONE } pl_repeat;

typedef struct pl_order pl_order;
pl_order *pl_order_new(void);
void pl_order_free(pl_order *o);
void pl_order_set_context(pl_order *o, const char *const *paths,
                          size_t n, size_t start);
void pl_order_set_shuffle(pl_order *o, bool on, uint32_t seed);
bool pl_order_shuffle(const pl_order *o);
void pl_order_set_repeat(pl_order *o, pl_repeat r);
pl_repeat pl_order_repeat(const pl_order *o);
const char *pl_order_current(const pl_order *o);
/* auto_eos: called because the track ended by itself.
 * Returns the next path or NULL when playback should stop. */
const char *pl_order_next(pl_order *o, bool auto_eos);
const char *pl_order_prev(pl_order *o);

void pl_order_queue_next(pl_order *o, const char *path);   /* play next */
void pl_order_queue_later(pl_order *o, const char *path);  /* append */
size_t pl_order_queue_count(const pl_order *o);
const char *pl_order_queue_at(const pl_order *o, size_t i);
int pl_order_queue_remove(pl_order *o, size_t i);
int pl_order_queue_move(pl_order *o, size_t from, size_t to);
void pl_order_queue_clear(pl_order *o);

/* ---- persisted playback state ---- */
typedef struct {
    char path[1024];
    uint64_t position_ms;
    double volume;      /* 0..1 */
    bool shuffle;
    pl_repeat repeat;
} pl_state;

int pl_state_save(const char *root, const pl_state *st);
int pl_state_load(const char *root, pl_state *st);

#ifdef __cplusplus
}
#endif
#endif /* PLAYMODEL_H */
