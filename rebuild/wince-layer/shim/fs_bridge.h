/* BrainCE Layer -- FS bridge v0 (DESIGN-wince-layer.md section 5.1/7).
 *
 * v0 scope: map CE absolute paths (e.g. \CE-TEST\dir\file.bin) onto a
 * single host directory (the "CE root", on SD in the guest, a scratch
 * directory on the dev host). No eMMC access exists in this layer at
 * all. Path components ".." are rejected so a CE binary cannot escape
 * the CE root.
 */
#ifndef BRAINCE_FS_BRIDGE_H
#define BRAINCE_FS_BRIDGE_H

#include "ce_types.h"

/* Initialise the bridge. root may be NULL -> env CE_FS_ROOT ->
 * default "./ce-fs". Creates the root directory if needed.
 * Returns 0 on success. */
int  ce_fs_init(const char *root);
const char *ce_fs_root(void);

/* Convert a CE UTF-16 absolute path to a host path under the CE root.
 * Returns 0 on success, -1 on bad path (sets nothing). out is
 * NUL-terminated. */
int  ce_fs_map(const CE_WCHAR *ce_path, char *out, size_t out_size);

/* UTF-16 <-> UTF-8 helpers (BMP only; surrogates pass through as
 * U+FFFD on decode). Return number of units/bytes written excluding
 * the terminator, or -1 on overflow. */
int  ce_w2u8(const CE_WCHAR *w, char *out, size_t out_size);
int  ce_u82w(const char *u8, CE_WCHAR *out, size_t out_units);
size_t ce_wcslen(const CE_WCHAR *w);

/* ---- handle table (files and find handles) ---- */
#define CE_HANDLE_KIND_FILE 1
#define CE_HANDLE_KIND_FIND 2

typedef struct ce_find_state ce_find_state;
typedef struct {
    int kind;
    int fd;               /* file kind */
    ce_find_state *find;  /* find kind */
} ce_handle_entry;

/* Allocate a handle for fd. Returns CE_INVALID_HANDLE_VALUE on
 * exhaustion (and sets ce lasterror). */
CE_HANDLE ce_handle_new_file(int fd);
CE_HANDLE ce_handle_new_find(ce_find_state *fs);
/* Look up; returns NULL for an invalid handle. */
ce_handle_entry *ce_handle_get(CE_HANDLE h);
/* Remove from the table (does not close). Returns 0 ok, -1 invalid. */
int  ce_handle_forget(CE_HANDLE h);

#endif
