/* BrainCE Layer -- read-only FAT12/16/32 volume reader.
 *
 * Purpose (DESIGN-wince-layer.md section 7): give the FS bridge a
 * read path into the CE storage volumes (\Nand, \Nand2, \Nand3,
 * \Nand4) that is read-only BY CONSTRUCTION -- this file contains no
 * write path at all; the source is opened O_RDONLY and every access
 * is a pread. Long file names are decoded from the FAT LFN entries
 * as UTF-16 and compared against CE paths in UTF-16, so Japanese
 * names resolve exactly (the guest kernel has no NLS modules for
 * these volumes; see REPORT-wince-s0.md).
 *
 * The source may be a block device (/dev/mmcblk0p1, a loop device)
 * or a regular file (host-side tests against an image copy), with
 * an optional byte offset for volumes that live inside a larger
 * image (the two gap volumes \Nand2 / \Nand4).
 */
#ifndef BRAINCE_FATVOL_H
#define BRAINCE_FATVOL_H

#include "ce_types.h"
#include <stdint.h>

typedef struct fatvol fatvol;

typedef struct {
    int      is_dir;
    uint32_t size;
    uint32_t start_cluster;
} fatvol_stat_t;

/* Open and parse the BPB. Returns NULL on any inconsistency. */
fatvol *fatvol_open(const char *source, uint64_t offset);
void    fatvol_close(fatvol *v);
const char *fatvol_source(const fatvol *v);

/* Resolve a path inside the volume. Components are separated by
 * '\' or '/'; a leading separator is optional; "" or "\" is the
 * root directory. Name matching is case-insensitive for ASCII and
 * exact otherwise (LFN entries carry the true UTF-16 names).
 * Returns 0 on success. */
int fatvol_stat_path(fatvol *v, const CE_WCHAR *path, fatvol_stat_t *st);

typedef struct fatvol_file fatvol_file;
fatvol_file *fatvol_fopen_path(fatvol *v, const CE_WCHAR *path);
uint32_t fatvol_fread(fatvol_file *f, void *buf, uint32_t len);
/* whence: 0 = SET, 1 = CUR, 2 = END. Returns new position or -1. */
int64_t  fatvol_fseek(fatvol_file *f, int64_t off, int whence);
uint32_t fatvol_fsize(const fatvol_file *f);
uint32_t fatvol_ftell(const fatvol_file *f);
void     fatvol_fclose(fatvol_file *f);

typedef struct {
    CE_WCHAR name[256];
    int      is_dir;
    uint32_t size;
} fatvol_dirent_t;

/* Iterate a directory. *cursor is an opaque entry index owned by
 * the caller (start at 0). Returns 1 when an entry was produced,
 * 0 at end of directory, -1 on error. */
int fatvol_list(fatvol *v, const CE_WCHAR *dirpath, uint32_t *cursor,
                fatvol_dirent_t *out);

#endif
