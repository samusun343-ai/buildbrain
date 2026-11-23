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
CE_WCHAR *ce_wdup(const CE_WCHAR *s);
int  ce_wcscasecmp(const CE_WCHAR *a, const CE_WCHAR *b);
size_t ce_wcslen(const CE_WCHAR *w);

/* ---- handle table (files and find handles) ---- */
#define CE_HANDLE_KIND_FILE 1
#define CE_HANDLE_KIND_FIND 2
#define CE_HANDLE_KIND_VFILE 3    /* ce_vfile (host fd or volume file) */
#define CE_HANDLE_KIND_THREAD 4   /* ptr: ce_thread_rec (coredll_shim) */
#define CE_HANDLE_KIND_PROCESS 5  /* aux: pid */
#define CE_HANDLE_KIND_RSRC 6     /* ptr: ce_rsrc_rec (coredll_shim) */
#define CE_HANDLE_KIND_MUTEX 7    /* ptr: ce_mutex_rec (coredll_shim) */
#define CE_HANDLE_KIND_MAP 8      /* ptr: ce_map_rec (coredll_shim) */

typedef struct ce_find_state ce_find_state;
typedef struct ce_vfile ce_vfile;
typedef struct {
    int kind;
    int fd;               /* file kind (legacy) */
    ce_find_state *find;  /* find kind */
    void *ptr;            /* vfile / thread / resource kinds */
    long  aux;            /* process kind: pid */
} ce_handle_entry;

/* Allocate a handle for fd. Returns CE_INVALID_HANDLE_VALUE on
 * exhaustion (and sets ce lasterror). */
CE_HANDLE ce_handle_new_file(int fd);
CE_HANDLE ce_handle_new_find(ce_find_state *fs);
CE_HANDLE ce_handle_new_vfile(ce_vfile *vf);
CE_HANDLE ce_handle_new_ptr(int kind, void *ptr, long aux);
/* Look up; returns NULL for an invalid handle. */
ce_handle_entry *ce_handle_get(CE_HANDLE h);
/* Remove from the table (does not close). Returns 0 ok, -1 invalid. */
int  ce_handle_forget(CE_HANDLE h);

/* ---- CE storage volumes (S0 read path, DESIGN section 7) ----
 * Configured at ce_fs_init from the environment:
 *   CE_VOLUMES="Nand=/dev/mmcblk0p1;Nand2=/dev/loop0;..."
 * A source may carry an "@<byte offset>" suffix for volumes that
 * live inside a larger image file (host-side tests; in the guest
 * the loop driver pre-applies offsets). Volumes are READ-ONLY:
 * writes to a volume path are redirected to the overlay (the CE
 * root mapping) by ce_fs_open. */

#include "fatvol.h"

/* If ce_path's first component names a configured volume, return
 * that volume and copy the remaining in-volume path (starting with
 * the separator, or empty for the volume root) into rest. Else
 * return NULL. */
fatvol *ce_fs_volume_of(const CE_WCHAR *ce_path, CE_WCHAR *rest,
                        size_t rest_units);
int ce_fs_volume_count(void);

/* A file opened through the bridge: either a host file (overlay or
 * CE root) or a read-only volume file. */
struct ce_vfile {
    int          is_fat;
    int          fd;        /* host kind */
    fatvol_file *ff;        /* volume kind */
};

/* Open a CE path. oflags are POSIX open flags computed by the
 * caller from the CE disposition. Any write intent (O_WRONLY,
 * O_RDWR, O_CREAT, O_TRUNC) goes to the overlay/CE-root host path
 * only (parent directories are created); a pure read first tries
 * the overlay host file, then the volume. On failure returns NULL
 * and stores a CE_ERROR_* code in *err. */
ce_vfile *ce_fs_open(const CE_WCHAR *ce_path, int oflags, CE_DWORD *err);
int ce_vfile_host_fd(ce_vfile *vf);
uint32_t  ce_vfile_read(ce_vfile *vf, void *buf, uint32_t len);
uint32_t  ce_vfile_write(ce_vfile *vf, const void *buf, uint32_t len);
/* whence: SEEK_SET/CUR/END. Returns new position or (uint64_t)-1. */
uint64_t  ce_vfile_seek(ce_vfile *vf, int64_t off, int whence);
uint32_t  ce_vfile_size(ce_vfile *vf);
int       ce_vfile_trunc_here(ce_vfile *vf);  /* SetEndOfFile; host only */
void      ce_vfile_close(ce_vfile *vf);

/* Probe a CE path (overlay first, then volume). 0 = exists;
 * fills *is_dir / *size when non-NULL. */
int ce_fs_probe(const CE_WCHAR *ce_path, int *is_dir, uint32_t *size);

#endif
