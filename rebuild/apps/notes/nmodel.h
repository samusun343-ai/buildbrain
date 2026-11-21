/* nmodel - bd-notes notes model (no toolkit dependency).
 *
 * Storage (all inside the Brainux area on the microSD):
 *   <root>/notes/<folder>/<id>.txt    note body, pure text
 *   <root>/notes/<folder>/<id>.meta   key=value sidecar (pinned,
 *                                     creation/modified sequence)
 *   <root>/deleted/<id>.txt|.meta     Recently Deleted
 *   <root>/attachments/<id>/<name>    attachment files
 *   <root>/nextid, <root>/seq         monotonic counters
 *
 * Design choices driven by the device reality:
 *   - No central index file exists, so no single file can corrupt
 *     the whole library: the index is rebuilt by scanning at open,
 *     bodies without a meta get a synthesized meta, metas without
 *     a body are dropped. Titles are derived from the body's first
 *     line at scan time (read of the first bytes only), never
 *     stored, so title and body can never disagree.
 *   - Ordering uses a persisted monotonic sequence counter, not
 *     the clock: "sort by modified" is sequence order. The wall
 *     clock is untrusted on this device.
 *   - Every write is atomic (bdstore): body first, then meta.
 *     A crash between them leaves a stale sequence number, never
 *     a torn note.
 *   - Recently Deleted evicts by BYTES against a budget, oldest
 *     deletion-sequence first. No age-based deletion (the old
 *     "30 days" rule is meaningless with an untrusted clock).
 */
#ifndef NMODEL_H
#define NMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NM_PATH_MAX 4096
#define NM_DEFAULT_FOLDER "Notes"

typedef struct {
    uint64_t id;
    char *folder;            /* owning folder ("" never used;
                                default is NM_DEFAULT_FOLDER) */
    bool pinned;
    bool deleted;
    uint64_t created_seq;
    uint64_t modified_seq;
    uint64_t deleted_seq;    /* only when deleted */
    char *title;             /* first non-empty line of the body */
    char *preview;           /* first bytes after the title line */
    uint64_t body_bytes;
} nm_note;

typedef struct nm_store nm_store;

/* Open (creating if needed) the store at `root`. trash_budget is
 * the Recently Deleted byte budget. */
nm_store *nm_open(const char *root, uint64_t trash_budget_bytes);
void nm_close(nm_store *s);
const char *nm_root(const nm_store *s);

/* ---------------- folders ---------------- */
/* Folder names: non-empty, no '/', not "." or "..". */
bool nm_folder_name_valid(const char *name);
int nm_folder_create(nm_store *s, const char *name);
int nm_folder_rename(nm_store *s, const char *old_name,
                     const char *new_name);
/* Deleting a folder moves its notes to the default folder
 * (Apple Notes deletes the folder, not the notes' content when
 * they are account notes; moving keeps data safe on a device
 * with no cloud copy). The default folder cannot be deleted. */
int nm_folder_delete(nm_store *s, const char *name);
/* Malloc'd array of malloc'd folder names (default first, then
 * alphabetical). Free with nm_strv_free. */
size_t nm_folders(nm_store *s, char ***out);
void nm_strv_free(char **v, size_t n);

/* ---------------- notes ---------------- */

/* Create a note in `folder` with `body` (may be ""). Returns the
 * new id, or 0 on failure. */
uint64_t nm_create(nm_store *s, const char *folder, const char *body);

/* Load a note's full body (malloc'd, caller frees). Deleted notes
 * are loadable too (for preview/restore). NULL when missing. */
char *nm_load_body(nm_store *s, uint64_t id);

/* Replace a note's body atomically and bump its modified
 * sequence. This is the autosave path: callers invoke it on edit
 * idle / note switch / close. */
int nm_save_body(nm_store *s, uint64_t id, const char *body);

int nm_set_pinned(nm_store *s, uint64_t id, bool pinned);
int nm_move_folder(nm_store *s, uint64_t id, const char *folder);

/* Move to Recently Deleted, then enforce the byte budget.
 * *evicted_out (optional) reports permanent deletions. */
int nm_delete(nm_store *s, uint64_t id, int *evicted_out);
int nm_restore(nm_store *s, uint64_t id);
int nm_delete_permanent(nm_store *s, uint64_t id);
int nm_empty_deleted(nm_store *s);
uint64_t nm_deleted_bytes(nm_store *s);

typedef enum {
    NM_SORT_MODIFIED = 0,    /* modified sequence, newest first */
    NM_SORT_CREATED,
    NM_SORT_TITLE
} nm_sort;

/* List notes. folder == NULL lists all folders. include_deleted
 * lists the Recently Deleted instead of live notes (folder is
 * ignored then). Pinned notes always sort above unpinned ones.
 * Results are malloc'd; free with nm_list_free. */
int nm_list(nm_store *s, const char *folder, bool include_deleted,
            nm_sort sort, nm_note **out, size_t *n_out);
void nm_list_free(nm_note *v, size_t n);
bool nm_get(nm_store *s, uint64_t id, nm_note *out); /* single note */

/* ---------------- search ---------------- */

typedef struct {
    uint64_t id;
    char *title;
    char *snippet;           /* context around the first body hit,
                                or the title when only it matched */
    bool in_title;
} nm_hit;

int nm_search(nm_store *s, const char *query, nm_hit **out, size_t *n_out);
void nm_search_free(nm_hit *v, size_t n);

/* ---------------- attachments ---------------- */

/* Copy `src_path` into the note's attachment dir atomically.
 * Name conflicts get a "name 2" variant. out receives the stored
 * name. */
int nm_attach_add(nm_store *s, uint64_t id, const char *src_path,
                  char *out_name, size_t cap);
size_t nm_attach_list(nm_store *s, uint64_t id, char ***out);
int nm_attach_remove(nm_store *s, uint64_t id, const char *name);
int nm_attach_path(nm_store *s, uint64_t id, const char *name,
                   char *out, size_t cap);

/* ---------------- checklist helpers (body text level) --------- */

/* Toggle the checkbox on logical line `line_idx` of `body`:
 * "[ ] " -> "[x] ", "[x] "/"[X] " -> "[ ] ". Returns a malloc'd
 * new body, or NULL when the line has no checkbox. *now_checked
 * (optional) receives the resulting state. */
char *nm_checklist_toggle(const char *body, size_t line_idx,
                          bool *now_checked);
void nm_checklist_count(const char *body, size_t *done, size_t *total);

#endif /* NMODEL_H */
