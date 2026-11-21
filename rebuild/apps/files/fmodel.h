/* fmodel - bd-files file-manager model (no toolkit dependency).
 *
 * Everything the file manager does to the filesystem lives here,
 * pure C over POSIX + bdstore, so it is unit-testable headlessly:
 * directory listing/sorting/grouping, scoped recursive search,
 * file operations, a capacity-budgeted trash, properties, an
 * image-dimension LRU cache, navigation history and breadcrumbs.
 *
 * Safety rules (device reality):
 *   - Copies never write the destination in place: each file is
 *     copied to a temp name in the destination directory, fsynced
 *     and renamed over the final name (bd_copy_atomic). A power
 *     cut mid-copy leaves the old destination (or nothing), never
 *     a half-written file under its final name.
 *   - The trash evicts ONLY by capacity: when the trash exceeds
 *     its byte budget, the oldest-inserted items (by insertion
 *     sequence number, not by clock) are permanently deleted
 *     until it fits. There is no age-based deletion anywhere -
 *     the device clock is untrusted.
 */
#ifndef FMODEL_H
#define FMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define FM_PATH_MAX 4096

typedef struct {
    char *name;              /* basename */
    char *path;              /* full path */
    bool is_dir;
    bool is_link;
    bool hidden;             /* basename starts with '.' */
    uint64_t size;           /* bytes (dirs: their own st_size) */
    int64_t mtime;           /* seconds since epoch (display/sort) */
    mode_t mode;
    char kind[32];           /* "Folder", "PNG image", ... */
} fm_entry;

typedef struct {
    fm_entry *v;
    size_t n;
    char *path;              /* directory these entries belong to */
} fm_listing;

/* Load a directory listing (unsorted; call fm_listing_sort).
 * Hidden entries are skipped unless show_hidden. Returns 0 or -1
 * with errno set. */
int fm_listing_load(fm_listing *l, const char *path, bool show_hidden);
void fm_listing_free(fm_listing *l);

typedef enum {
    FM_SORT_NAME = 0, FM_SORT_MTIME, FM_SORT_SIZE, FM_SORT_TYPE
} fm_sort_key;

/* Sort in place. dirs_first keeps folders above files (Finder /
 * Explorer behaviour). desc reverses the key order (dirs_first is
 * NOT reversed, matching both references). */
void fm_listing_sort(fm_listing *l, fm_sort_key key, bool desc,
                     bool dirs_first);

/* Grouping (applied on top of a sort): returns a group label id
 * for an entry. FM_GROUP_TYPE groups by kind string;
 * FM_GROUP_DATE buckets mtimes into Today / Yesterday / Earlier
 * relative to the caller-supplied day boundaries (seconds since
 * epoch of today's and yesterday's local midnight). Grouping is a
 * display/ordering aid only - nothing is ever deleted by date. */
typedef enum { FM_GROUP_NONE = 0, FM_GROUP_TYPE, FM_GROUP_DATE } fm_group;
int fm_entry_group(const fm_entry *e, fm_group g,
                   int64_t today_start, int64_t yesterday_start,
                   char *label, size_t cap);

/* Recursive search below `root` for names containing `query`
 * (case-insensitive). Results are capped at `cap` entries and the
 * walk at FM_SEARCH_MAX_DEPTH levels; *truncated reports whether
 * either cap cut the result short. Returns 0 or -1. */
#define FM_SEARCH_MAX_DEPTH 12
int fm_search(const char *root, const char *query, bool show_hidden,
              fm_listing *out, size_t cap, bool *truncated);

/* ---------------- file operations ---------------- */

/* Create a folder / empty file inside `dir`. On success the full
 * path is written to out. Fails with EEXIST when the name is taken
 * (the UI offers "name 2" via bd_unique_path). */
int fm_mkdir(const char *dir, const char *name, char *out, size_t cap);
int fm_create_file(const char *dir, const char *name, char *out, size_t cap);

/* Rename `path` to `new_name` (same directory). */
int fm_rename(const char *path, const char *new_name,
              char *out, size_t cap);

/* Copy `src` into directory `dst_dir`. Files are copied atomically
 * (see header). Directories are copied recursively; a failure part
 * way leaves the files already copied in place and returns -1 with
 * *copied_out (when non-NULL) reporting how many files landed.
 * overwrite=false fails with EEXIST when the target name exists. */
int fm_copy(const char *src, const char *dst_dir, bool overwrite,
            char *out, size_t cap, int *copied_out);

/* Move `src` into `dst_dir`: rename when possible, recursive
 * copy + remove across devices. */
int fm_move(const char *src, const char *dst_dir, bool overwrite,
            char *out, size_t cap);

/* Permanently delete a file or directory tree. */
int fm_delete_permanent(const char *path);

/* ---------------- trash (capacity-budgeted) ---------------- */

typedef struct fm_trash fm_trash;

typedef struct {
    uint64_t id;             /* insertion sequence number */
    char *name;              /* display name (original basename) */
    char *orig_path;         /* where restore puts it back */
    uint64_t size;           /* bytes occupied */
    bool is_dir;
} fm_trash_entry;

typedef struct {
    fm_trash_entry *v;
    size_t n;
} fm_trash_list;

/* Open (creating if needed) a trash rooted at `root` with a byte
 * budget. Layout: files/<id>_<name>, info/<id>.info. On open,
 * orphaned files/infos left by an interrupted operation are
 * reconciled: a files/ entry without info is adopted with an
 * unknown origin (restore falls back to the trash's "restored"
 * folder), an info without files/ is dropped. */
fm_trash *fm_trash_open(const char *root, uint64_t budget_bytes);
void fm_trash_close(fm_trash *t);
uint64_t fm_trash_budget(const fm_trash *t);
uint64_t fm_trash_total_bytes(const fm_trash *t);

/* Move `path` into the trash, then enforce the budget by evicting
 * lowest-sequence items permanently. *evicted_out (optional)
 * reports how many items the budget enforcement removed. */
int fm_trash_add(fm_trash *t, const char *path, int *evicted_out);

int fm_trash_list_all(fm_trash *t, fm_trash_list *out);
void fm_trash_list_free(fm_trash_list *l);

/* Restore item `id` to its original path. When the original name
 * is taken, a unique "name 2" variant is used. When the origin is
 * unknown (adopted orphan), restores into `fallback_dir`. */
int fm_trash_restore(fm_trash *t, uint64_t id, const char *fallback_dir,
                     char *out, size_t cap);

/* Permanently delete one item, or everything (id = 0). */
int fm_trash_delete(fm_trash *t, uint64_t id);
int fm_trash_empty(fm_trash *t);

/* ---------------- properties ---------------- */

typedef struct {
    char name[256];
    char path[FM_PATH_MAX];
    char kind[32];
    bool is_dir;
    bool is_link;
    uint64_t size;           /* recursive total for directories */
    uint64_t size_on_disk;   /* st_blocks based, recursive */
    uint64_t n_files, n_dirs;
    bool walk_truncated;     /* directory walk hit its cap */
    int64_t ctime, mtime, atime;
    unsigned mode;           /* permission bits + type */
    int uid, gid;
    bool readonly_flag;      /* no write bit for owner */
    bool hidden_flag;
    char perms[11];          /* "drwxr-xr-x" style */
    int img_w, img_h;        /* image dimensions, 0 when unknown */
} fm_props;

int fm_props_load(const char *path, fm_props *out);

/* ---------------- image dimension cache (budgeted LRU) -------- */

/* Parses just enough of a PNG/JPEG/GIF header to learn its pixel
 * dimensions - no image decode, a few dozen bytes read. Returns
 * false when the format is unknown or the file is too short. */
bool fm_image_dims(const char *path, int *w, int *h);

typedef struct fm_thumbcache fm_thumbcache;

/* LRU cache of image dimensions keyed by path, invalidated when
 * the file's mtime/size changes. Memory is budgeted: each entry
 * accounts a fixed estimate plus its key length, and the least
 * recently used entries are evicted past `budget_bytes`. This is
 * the wave-1 thumbnail story: the toolkit's public API decodes
 * whole images into widgets but exposes no scaled-decode or
 * pixel read-back, so grid cells show dimension badges + type
 * icons; the gap is recorded in the wave-1 report. */
fm_thumbcache *fm_thumbcache_new(size_t budget_bytes);
void fm_thumbcache_free(fm_thumbcache *c);
bool fm_thumbcache_dims(fm_thumbcache *c, const char *path, int *w, int *h);
size_t fm_thumbcache_count(const fm_thumbcache *c);
size_t fm_thumbcache_bytes(const fm_thumbcache *c);
unsigned long fm_thumbcache_hits(const fm_thumbcache *c);
unsigned long fm_thumbcache_misses(const fm_thumbcache *c);

/* ---------------- navigation history + breadcrumbs ------------ */

typedef struct {
    char **v;
    size_t n, cap, pos;      /* pos = index of current entry */
} fm_history;

void fm_history_init(fm_history *h);
void fm_history_free(fm_history *h);
/* Push a new current location, dropping any forward entries.
 * Pushing the current path again is a no-op. */
void fm_history_push(fm_history *h, const char *path);
const char *fm_history_current(const fm_history *h);
const char *fm_history_back(fm_history *h);    /* NULL when at start */
const char *fm_history_forward(fm_history *h); /* NULL when at end */
bool fm_history_can_back(const fm_history *h);
bool fm_history_can_forward(const fm_history *h);

/* Breadcrumbs: "/a/b/c" has 3 crumbs ("a","b","c") plus the root.
 * fm_crumb_at writes crumb idx's label and cumulative path
 * (idx 0 = "/"). Returns false when idx is out of range. */
int fm_crumb_count(const char *path);
bool fm_crumb_at(const char *path, int idx,
                 char *label, size_t lcap, char *cpath, size_t pcap);

#endif /* FMODEL_H */
