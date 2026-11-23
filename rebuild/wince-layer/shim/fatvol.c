/* BrainCE Layer -- read-only FAT12/16/32 volume reader.
 * See fatvol.h for the design contract. No write path exists here.
 */
#define _FILE_OFFSET_BITS 64
#include "fatvol.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct fatvol {
    int      fd;
    uint64_t base;          /* byte offset of the volume in source */
    char    *source;        /* owned copy of the source path */
    uint32_t bps;           /* bytes per sector */
    uint32_t spc;           /* sectors per cluster */
    uint32_t reserved;      /* reserved sectors before FAT #1 */
    uint32_t nfats;
    uint32_t fatsz;         /* sectors per FAT */
    uint32_t root_entries;  /* FAT12/16 root directory entries (0: FAT32) */
    uint32_t root_cluster;  /* FAT32 root directory start cluster */
    uint32_t total_sectors;
    int      is_fat32;
    uint64_t fat_off;       /* byte offset of FAT #1 (relative) */
    uint64_t root_off;      /* FAT12/16 root dir byte offset (relative) */
    uint64_t data_off;      /* first data cluster byte offset (relative) */
    /* one-sector FAT cache */
    uint32_t cache_sec;     /* sector index within the FAT, ~0u = none */
    uint8_t *cache;         /* bps bytes */
};

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int vread(const fatvol *v, void *buf, uint32_t len, uint64_t rel)
{
    uint8_t *p = buf;
    while (len) {
        ssize_t r = pread(v->fd, p, len, (off_t)(v->base + rel));
        if (r <= 0)
            return -1;
        p += r;
        len -= (uint32_t)r;
        rel += (uint64_t)r;
    }
    return 0;
}

fatvol *fatvol_open(const char *source, uint64_t offset)
{
    int fd = open(source, O_RDONLY);
    if (fd < 0)
        return NULL;
    uint8_t bs[512];
    if (pread(fd, bs, sizeof(bs), (off_t)offset) != (ssize_t)sizeof(bs)
        || bs[510] != 0x55 || bs[511] != 0xAA) {
        close(fd);
        return NULL;
    }
    fatvol *v = calloc(1, sizeof(*v));
    if (!v) {
        close(fd);
        return NULL;
    }
    v->fd = fd;
    v->base = offset;
    v->source = strdup(source);
    v->bps = rd16(bs + 11);
    v->spc = bs[13];
    v->reserved = rd16(bs + 14);
    v->nfats = bs[16];
    v->root_entries = rd16(bs + 17);
    v->total_sectors = rd16(bs + 19);
    if (!v->total_sectors)
        v->total_sectors = rd32(bs + 32);
    uint32_t fatsz16 = rd16(bs + 22);
    uint32_t fatsz32 = rd32(bs + 36);
    v->is_fat32 = (fatsz16 == 0);
    v->fatsz = v->is_fat32 ? fatsz32 : fatsz16;
    v->root_cluster = v->is_fat32 ? rd32(bs + 44) : 0;
    if ((v->bps != 512 && v->bps != 1024 && v->bps != 2048
         && v->bps != 4096) || v->spc == 0 || (v->spc & (v->spc - 1)) != 0
        || v->nfats == 0 || v->fatsz == 0 || v->total_sectors == 0) {
        fatvol_close(v);
        return NULL;
    }
    v->fat_off = (uint64_t)v->reserved * v->bps;
    uint64_t root_bytes = (uint64_t)v->root_entries * 32;
    v->root_off = v->fat_off + (uint64_t)v->nfats * v->fatsz * v->bps;
    v->data_off = v->root_off + root_bytes;
    v->cache_sec = 0xFFFFFFFFu;
    v->cache = malloc(v->bps);
    if (!v->cache) {
        fatvol_close(v);
        return NULL;
    }
    return v;
}

void fatvol_close(fatvol *v)
{
    if (!v)
        return;
    if (v->fd >= 0)
        close(v->fd);
    free(v->cache);
    free(v->source);
    free(v);
}

const char *fatvol_source(const fatvol *v) { return v->source; }

static uint64_t cluster_off(const fatvol *v, uint32_t cl)
{
    return v->data_off + (uint64_t)(cl - 2) * v->spc * v->bps;
}

static int fat_entry(fatvol *v, uint32_t cl, uint32_t *out)
{
    uint64_t byte_in_fat;
    if (v->is_fat32)
        byte_in_fat = (uint64_t)cl * 4;
    else
        byte_in_fat = (uint64_t)cl * 2;
    uint32_t sec = (uint32_t)(byte_in_fat / v->bps);
    uint32_t off = (uint32_t)(byte_in_fat % v->bps);
    if (sec != v->cache_sec) {
        if (vread(v, v->cache, v->bps, v->fat_off + (uint64_t)sec * v->bps)
            != 0)
            return -1;
        v->cache_sec = sec;
    }
    if (v->is_fat32)
        *out = rd32(v->cache + off) & 0x0FFFFFFFu;
    else
        *out = rd16(v->cache + off);
    return 0;
}

static int cluster_eoc(const fatvol *v, uint32_t cl)
{
    return v->is_fat32 ? (cl >= 0x0FFFFFF8u) : (cl >= 0xFFF8u);
}

/* ---- directory scanning ----
 * A directory is a byte stream of 32-byte entries, either the fixed
 * FAT12/16 root region or a cluster chain. We walk it entry by entry
 * with a small buffered reader. */

typedef struct {
    fatvol  *v;
    uint32_t cluster;       /* current cluster (FAT32/root-chain) */
    uint64_t fixed_off;     /* FAT12/16 root: current byte offset */
    uint64_t fixed_end;
    uint32_t in_cluster;    /* byte offset inside current cluster */
    int      fixed;         /* walking the fixed root region */
    int      done;
} dirwalk;

static void dirwalk_start(fatvol *v, dirwalk *w, uint32_t dir_cluster,
                          int is_root)
{
    memset(w, 0, sizeof(*w));
    w->v = v;
    if (is_root && !v->is_fat32) {
        w->fixed = 1;
        w->fixed_off = v->root_off;
        w->fixed_end = v->root_off + (uint64_t)v->root_entries * 32;
    } else {
        w->cluster = is_root ? v->root_cluster : dir_cluster;
    }
}

/* Read the next raw 32-byte entry. 1 = entry, 0 = end. */
static int dirwalk_next(dirwalk *w, uint8_t ent[32])
{
    fatvol *v = w->v;
    if (w->done)
        return 0;
    if (w->fixed) {
        if (w->fixed_off + 32 > w->fixed_end) {
            w->done = 1;
            return 0;
        }
        if (vread(v, ent, 32, w->fixed_off) != 0) {
            w->done = 1;
            return 0;
        }
        w->fixed_off += 32;
        return 1;
    }
    for (;;) {
        if (w->cluster < 2 || cluster_eoc(v, w->cluster)) {
            w->done = 1;
            return 0;
        }
        uint32_t cl_bytes = v->spc * v->bps;
        if (w->in_cluster + 32 <= cl_bytes) {
            if (vread(v, ent, 32,
                      cluster_off(v, w->cluster) + w->in_cluster) != 0) {
                w->done = 1;
                return 0;
            }
            w->in_cluster += 32;
            return 1;
        }
        uint32_t next;
        if (fat_entry(v, w->cluster, &next) != 0) {
            w->done = 1;
            return 0;
        }
        w->cluster = next;
        w->in_cluster = 0;
    }
}

/* One logical directory entry (LFN assembled). */
typedef struct {
    CE_WCHAR name[256];
    int      has_name;
    int      is_dir;
    int      is_volume;     /* volume label entry: skip everywhere */
    int      root;          /* this dent IS the volume root directory */
    uint32_t size;
    uint32_t start_cluster;
} dent;

static void sfn_name(const uint8_t ent[32], CE_WCHAR out[16])
{
    int n = 0;
    for (int i = 0; i < 8; i++) {
        if (ent[i] == ' ')
            break;
        out[n++] = (CE_WCHAR)(uint8_t)ent[i];
    }
    if (ent[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11; i++) {
            if (ent[i] == ' ')
                break;
            out[n++] = (CE_WCHAR)(uint8_t)ent[i];
        }
    }
    out[n] = 0;
}

/* Read the next logical entry. 1 = entry, 0 = end. */
static int dir_next_logical(dirwalk *w, dent *d)
{
    uint8_t ent[32];
    CE_WCHAR lfn[256];
    int lfn_len = 0;
    int have_lfn = 0;
    while (dirwalk_next(w, ent)) {
        if (ent[0] == 0x00)
            return 0;
        if (ent[0] == 0xE5) {
            have_lfn = 0;
            lfn_len = 0;
            continue;
        }
        uint8_t attr = ent[11];
        if (attr == 0x0F) {
            /* LFN entry: sequence in ent[0] low 5 bits, entries are
             * stored last-part first. */
            int seq = ent[0] & 0x1F;
            if (seq >= 1 && seq <= 20) {
                int base = (seq - 1) * 13;
                static const int pos[13] =
                    { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                for (int i = 0; i < 13; i++) {
                    uint16_t ch = rd16(ent + pos[i]);
                    if (base + i < 255)
                        lfn[base + i] = (CE_WCHAR)ch;
                }
                if (base + 13 > lfn_len)
                    lfn_len = base + 13;
                have_lfn = 1;
            }
            continue;
        }
        memset(d, 0, sizeof(*d));
        d->is_volume = (attr & 0x08) != 0;
        d->is_dir = (attr & 0x10) != 0;
        d->size = rd32(ent + 28);
        d->start_cluster = rd16(ent + 26);
        if (w->v->is_fat32)
            d->start_cluster |= (uint32_t)rd16(ent + 20) << 16;
        if (have_lfn) {
            int n = 0;
            while (n < lfn_len && lfn[n] != 0 && lfn[n] != 0xFFFF)
                n++;
            if (n > 255)
                n = 255;
            memcpy(d->name, lfn, (size_t)n * sizeof(CE_WCHAR));
            d->name[n] = 0;
            d->has_name = 1;
        } else {
            CE_WCHAR sfn[16];
            sfn_name(ent, sfn);
            size_t n = 0;
            while (sfn[n] && n < 255) {
                d->name[n] = sfn[n];
                n++;
            }
            d->name[n] = 0;
            d->has_name = 1;
        }
        return 1;
    }
    return 0;
}

static int name_eq(const CE_WCHAR *a, const CE_WCHAR *b)
{
    while (*a && *b) {
        CE_WCHAR ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z')
            ca = (CE_WCHAR)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = (CE_WCHAR)(cb - 'a' + 'A');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* Split off the first path component. Returns pointer to the rest
 * ("" when none) or NULL when the path is exhausted/root. comp is
 * NUL-terminated and at most comp_max-1 units. */
static const CE_WCHAR *next_comp(const CE_WCHAR *p, CE_WCHAR *comp,
                                 size_t comp_max)
{
    while (*p == '\\' || *p == '/')
        p++;
    if (!*p)
        return NULL;
    size_t n = 0;
    while (*p && *p != '\\' && *p != '/') {
        if (n + 1 < comp_max)
            comp[n++] = *p;
        p++;
    }
    comp[n] = 0;
    while (*p == '\\' || *p == '/')
        p++;
    return p;
}

static int find_in_dir(fatvol *v, uint32_t dir_cluster, int is_root,
                       const CE_WCHAR *comp, dent *out)
{
    dirwalk w;
    dirwalk_start(v, &w, dir_cluster, is_root);
    dent d;
    while (dir_next_logical(&w, &d)) {
        if (d.is_volume)
            continue;
        if (name_eq(d.name, comp)) {
            *out = d;
            return 0;
        }
    }
    return -1;
}

/* Resolve a full path to its directory entry. */
static int resolve(fatvol *v, const CE_WCHAR *path, dent *out)
{
    CE_WCHAR comp[256];
    const CE_WCHAR *rest = next_comp(path, comp, sizeof(comp) / sizeof(comp[0]));
    if (!rest) {
        /* root directory itself */
        memset(out, 0, sizeof(*out));
        out->is_dir = 1;
        out->root = 1;
        out->has_name = 1;
        out->start_cluster = v->is_fat32 ? v->root_cluster : 0;
        return 0;
    }
    uint32_t dir_cluster = 0;
    int is_root = 1;
    for (;;) {
        dent d;
        if (find_in_dir(v, dir_cluster, is_root, comp, &d) != 0)
            return -1;
        if (!*rest) {
            *out = d;
            return 0;
        }
        if (!d.is_dir)
            return -1;
        dir_cluster = d.start_cluster;
        is_root = 0;
        rest = next_comp(rest, comp, sizeof(comp) / sizeof(comp[0]));
        if (!rest)
            return -1;  /* trailing separator after a file/dir name */
    }
}

int fatvol_stat_path(fatvol *v, const CE_WCHAR *path, fatvol_stat_t *st)
{
    dent d;
    if (!v || !path || resolve(v, path, &d) != 0)
        return -1;
    st->is_dir = d.is_dir;
    st->size = d.size;
    st->start_cluster = d.start_cluster;
    return 0;
}

int fatvol_list(fatvol *v, const CE_WCHAR *dirpath, uint32_t *cursor,
                fatvol_dirent_t *out)
{
    dent dd;
    if (!v || !dirpath || resolve(v, dirpath, &dd) != 0 || !dd.is_dir)
        return -1;
    dirwalk w;
    dirwalk_start(v, &w, dd.start_cluster, dd.root);
    dent d;
    uint32_t idx = 0;
    while (dir_next_logical(&w, &d)) {
        if (d.is_volume)
            continue;
        if (idx == *cursor) {
            size_t n = 0;
            while (d.name[n] && n < 255) {
                out->name[n] = d.name[n];
                n++;
            }
            out->name[n] = 0;
            out->is_dir = d.is_dir;
            out->size = d.size;
            *cursor = idx + 1;
            return 1;
        }
        idx++;
    }
    return 0;
}

/* ---- sequential file reader ---- */

struct fatvol_file {
    fatvol  *v;
    uint32_t size;
    uint32_t pos;
    uint32_t start_cluster;
    uint32_t cur_cluster;   /* cluster holding pos (chain-walked) */
    uint32_t cur_index;     /* index of cur_cluster in the chain */
};

fatvol_file *fatvol_fopen_path(fatvol *v, const CE_WCHAR *path)
{
    dent d;
    if (!v || !path || resolve(v, path, &d) != 0 || d.is_dir)
        return NULL;
    fatvol_file *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->v = v;
    f->size = d.size;
    f->start_cluster = d.start_cluster;
    f->cur_cluster = d.start_cluster;
    f->cur_index = 0;
    f->pos = 0;
    return f;
}

static int file_seek_cluster(fatvol_file *f, uint32_t index)
{
    fatvol *v = f->v;
    uint32_t cl_bytes = v->spc * v->bps;
    (void)cl_bytes;
    if (index < f->cur_index) {
        f->cur_cluster = f->start_cluster;
        f->cur_index = 0;
    }
    while (f->cur_index < index) {
        uint32_t next;
        if (fat_entry(v, f->cur_cluster, &next) != 0)
            return -1;
        if (next < 2 || cluster_eoc(v, next))
            return -1;
        f->cur_cluster = next;
        f->cur_index++;
    }
    return 0;
}

uint32_t fatvol_fread(fatvol_file *f, void *buf, uint32_t len)
{
    if (!f || !buf)
        return 0;
    if (f->pos >= f->size)
        return 0;
    if (len > f->size - f->pos)
        len = f->size - f->pos;
    fatvol *v = f->v;
    uint32_t cl_bytes = v->spc * v->bps;
    uint32_t done = 0;
    uint8_t *p = buf;
    while (done < len) {
        uint32_t index = f->pos / cl_bytes;
        uint32_t in_cl = f->pos % cl_bytes;
        if (file_seek_cluster(f, index) != 0)
            break;
        uint32_t chunk = cl_bytes - in_cl;
        if (chunk > len - done)
            chunk = len - done;
        if (vread(v, p + done, chunk,
                  cluster_off(v, f->cur_cluster) + in_cl) != 0)
            break;
        f->pos += chunk;
        done += chunk;
    }
    return done;
}

int64_t fatvol_fseek(fatvol_file *f, int64_t off, int whence)
{
    if (!f)
        return -1;
    int64_t base = (whence == 0) ? 0 : (whence == 1) ? (int64_t)f->pos
                 : (int64_t)f->size;
    int64_t np = base + off;
    if (np < 0)
        np = 0;
    if (np > (int64_t)f->size)
        np = (int64_t)f->size;
    f->pos = (uint32_t)np;
    return np;
}

uint32_t fatvol_fsize(const fatvol_file *f) { return f ? f->size : 0; }
uint32_t fatvol_ftell(const fatvol_file *f) { return f ? f->pos : 0; }

void fatvol_fclose(fatvol_file *f) { free(f); }
