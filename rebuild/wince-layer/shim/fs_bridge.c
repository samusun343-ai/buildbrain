/* BrainCE Layer -- FS bridge implementation. See fs_bridge.h.
 * v1 (S0/M3): CE storage volumes via the read-only FAT reader;
 * writes always land in the overlay (the CE root mapping). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "fs_bridge.h"

static char g_root[PATH_MAX];
static int  g_root_ready = 0;

/* ---- volumes ---- */
#define CE_MAX_VOLUMES 8
typedef struct {
    CE_WCHAR name[32];      /* CE volume name, e.g. "Nand" */
    fatvol  *vol;
} ce_volume_rec;
static ce_volume_rec g_volumes[CE_MAX_VOLUMES];
static int g_nvolumes = 0;

static void volumes_init(void)
{
    const char *spec = getenv("CE_VOLUMES");
    if (!spec || !*spec)
        return;
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", spec);
    for (char *tok = strtok(buf, ";"); tok && g_nvolumes < CE_MAX_VOLUMES;
         tok = strtok(NULL, ";")) {
        char *eq = strchr(tok, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *src = eq + 1;
        uint64_t off = 0;
        char *at = strrchr(src, '@');
        if (at && at[1]) {
            int digits = 1;
            for (const char *p = at + 1; *p; p++)
                if (*p < '0' || *p > '9')
                    digits = 0;
            if (digits) {
                off = strtoull(at + 1, NULL, 10);
                *at = '\0';
            }
        }
        fatvol *v = fatvol_open(src, off);
        if (!v)
            continue;
        ce_volume_rec *r = &g_volumes[g_nvolumes];
        ce_u82w(tok, r->name, sizeof(r->name) / sizeof(r->name[0]));
        r->vol = v;
        g_nvolumes++;
    }
}

int ce_fs_volume_count(void) { return g_nvolumes; }

static int vol_name_eq(const CE_WCHAR *a, const CE_WCHAR *b)
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

fatvol *ce_fs_volume_of(const CE_WCHAR *ce_path, CE_WCHAR *rest,
                        size_t rest_units)
{
    if (!ce_path || g_nvolumes == 0)
        return NULL;
    const CE_WCHAR *p = ce_path;
    while (*p == '\\' || *p == '/')
        p++;
    CE_WCHAR first[64];
    size_t n = 0;
    const CE_WCHAR *q = p;
    while (*q && *q != '\\' && *q != '/') {
        if (n + 1 < sizeof(first) / sizeof(first[0]))
            first[n++] = *q;
        q++;
    }
    first[n] = 0;
    if (n == 0)
        return NULL;
    for (int i = 0; i < g_nvolumes; i++) {
        if (vol_name_eq(first, g_volumes[i].name)) {
            if (rest && rest_units) {
                size_t m = 0;
                while (q[m] && m + 1 < rest_units) {
                    rest[m] = q[m];
                    m++;
                }
                rest[m] = 0;
            }
            return g_volumes[i].vol;
        }
    }
    return NULL;
}

static int mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

int ce_fs_init(const char *root)
{
    if (!root)
        root = getenv("CE_FS_ROOT");
    if (!root || !*root)
        root = "./ce-fs";
    if (strlen(root) >= sizeof(g_root))
        return -1;
    strcpy(g_root, root);
    if (mkdir_p(g_root) != 0)
        return -1;
    g_root_ready = 1;
    volumes_init();
    return 0;
}

const char *ce_fs_root(void)
{
    return g_root_ready ? g_root : NULL;
}

int ce_w2u8(const CE_WCHAR *w, char *out, size_t out_size)
{
    size_t o = 0;
    if (!out || out_size == 0)
        return -1;
    for (size_t i = 0; w[i]; i++) {
        uint32_t cp = w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && w[i + 1] >= 0xDC00
            && w[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        char enc[4];
        int el;
        if (cp < 0x80) {
            enc[0] = (char)cp; el = 1;
        } else if (cp < 0x800) {
            enc[0] = (char)(0xC0 | (cp >> 6));
            enc[1] = (char)(0x80 | (cp & 0x3F)); el = 2;
        } else if (cp < 0x10000) {
            enc[0] = (char)(0xE0 | (cp >> 12));
            enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[2] = (char)(0x80 | (cp & 0x3F)); el = 3;
        } else {
            enc[0] = (char)(0xF0 | (cp >> 18));
            enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[3] = (char)(0x80 | (cp & 0x3F)); el = 4;
        }
        if (o + (size_t)el + 1 > out_size)
            return -1;
        memcpy(out + o, enc, el);
        o += el;
    }
    out[o] = '\0';
    return (int)o;
}

int ce_u82w(const char *u8, CE_WCHAR *out, size_t out_units)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)u8;
    while (*p) {
        uint32_t cp;
        int adv;
        if (*p < 0x80) { cp = *p; adv = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; adv = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; adv = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; adv = 4; }
        else { cp = 0xFFFD; adv = 1; }
        if (adv > 1) {
            for (int i = 1; i < adv; i++) {
                if ((p[i] & 0xC0) != 0x80) { cp = 0xFFFD; adv = 1; break; }
                cp = (cp << 6) | (p[i] & 0x3F);
            }
        }
        p += adv;
        if (cp > 0xFFFF) {  /* outside BMP: emit replacement */
            cp = 0xFFFD;
        }
        if (o + 2 > out_units)
            return -1;
        out[o++] = (CE_WCHAR)cp;
    }
    if (o + 1 > out_units)
        return -1;
    out[o] = 0;
    return (int)o;
}

int ce_fs_map(const CE_WCHAR *ce_path, char *out, size_t out_size)
{
    if (!g_root_ready || !ce_path)
        return -1;
    char u8[PATH_MAX];
    if (ce_w2u8(ce_path, u8, sizeof(u8)) < 0)
        return -1;
    /* Normalise: '/' and '\\' are separators; collapse repeats;
     * reject ".." components outright. A leading separator is
     * required for absolute CE paths but tolerated if missing. */
    char norm[PATH_MAX];
    size_t n = 0;
    const char *p = u8;
    while (*p) {
        while (*p == '/' || *p == '\\')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != '/' && *p != '\\')
            p++;
        size_t clen = (size_t)(p - start);
        if (clen == 2 && start[0] == '.' && start[1] == '.')
            return -1;
        if (clen == 1 && start[0] == '.')
            continue;
        if (n + 1 + clen + 1 > sizeof(norm))
            return -1;
        norm[n++] = '/';
        memcpy(norm + n, start, clen);
        n += clen;
    }
    norm[n] = '\0';
    size_t rl = strlen(g_root);
    if (rl + n + 1 > out_size)
        return -1;
    memcpy(out, g_root, rl);
    memcpy(out + rl, norm, n + 1);
    return 0;
}

/* ---- handle table ---- */
#define CE_MAX_HANDLES 128
static ce_handle_entry g_handles[CE_MAX_HANDLES];

CE_HANDLE ce_handle_new_file(int fd)
{
    for (int i = 0; i < CE_MAX_HANDLES; i++) {
        if (g_handles[i].kind == 0) {
            g_handles[i].kind = CE_HANDLE_KIND_FILE;
            g_handles[i].fd = fd;
            g_handles[i].find = NULL;
            return (CE_HANDLE)(uintptr_t)(i + 1);
        }
    }
    ce_set_lasterror(CE_ERROR_TOO_MANY_OPEN_FILES);
    return CE_INVALID_HANDLE_VALUE;
}

CE_HANDLE ce_handle_new_find(ce_find_state *fs)
{
    for (int i = 0; i < CE_MAX_HANDLES; i++) {
        if (g_handles[i].kind == 0) {
            g_handles[i].kind = CE_HANDLE_KIND_FIND;
            g_handles[i].fd = -1;
            g_handles[i].find = fs;
            return (CE_HANDLE)(uintptr_t)(i + 1);
        }
    }
    ce_set_lasterror(CE_ERROR_TOO_MANY_OPEN_FILES);
    return CE_INVALID_HANDLE_VALUE;
}

ce_handle_entry *ce_handle_get(CE_HANDLE h)
{
    uintptr_t v = (uintptr_t)h;
    if (v == 0 || v > CE_MAX_HANDLES)
        return NULL;
    if (g_handles[v - 1].kind == 0)
        return NULL;
    return &g_handles[v - 1];
}

int ce_handle_forget(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e)
        return -1;
    e->kind = 0;
    e->fd = -1;
    e->find = NULL;
    e->ptr = NULL;
    e->aux = 0;
    return 0;
}

CE_HANDLE ce_handle_new_vfile(ce_vfile *vf)
{
    for (int i = 0; i < CE_MAX_HANDLES; i++) {
        if (g_handles[i].kind == 0) {
            g_handles[i].kind = CE_HANDLE_KIND_VFILE;
            g_handles[i].fd = -1;
            g_handles[i].find = NULL;
            g_handles[i].ptr = vf;
            g_handles[i].aux = 0;
            return (CE_HANDLE)(uintptr_t)(i + 1);
        }
    }
    ce_set_lasterror(CE_ERROR_TOO_MANY_OPEN_FILES);
    return CE_INVALID_HANDLE_VALUE;
}

CE_HANDLE ce_handle_new_ptr(int kind, void *ptr, long aux)
{
    for (int i = 0; i < CE_MAX_HANDLES; i++) {
        if (g_handles[i].kind == 0) {
            g_handles[i].kind = kind;
            g_handles[i].fd = -1;
            g_handles[i].find = NULL;
            g_handles[i].ptr = ptr;
            g_handles[i].aux = aux;
            return (CE_HANDLE)(uintptr_t)(i + 1);
        }
    }
    ce_set_lasterror(CE_ERROR_TOO_MANY_OPEN_FILES);
    return CE_INVALID_HANDLE_VALUE;
}

/* ---- vfile: unified file access (overlay host file / volume) ---- */

static CE_DWORD ce_err_from_errno(int e)
{
    switch (e) {
    case ENOENT:  return CE_ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return CE_ERROR_PATH_NOT_FOUND;
    case EACCES:  return CE_ERROR_ACCESS_DENIED;
    case EEXIST:  return CE_ERROR_FILE_EXISTS;
    case ENOSPC:  return 112; /* ERROR_DISK_FULL */
    case ENFILE:
    case EMFILE:  return CE_ERROR_TOO_MANY_OPEN_FILES;
    default:      return CE_ERROR_INVALID_PARAMETER;
    }
}

static int mkdir_parents_of(const char *path)
{
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);
    char *slash = strrchr(tmp, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    return mkdir_p(tmp);
}

ce_vfile *ce_fs_open(const CE_WCHAR *ce_path, int oflags, CE_DWORD *err)
{
    char host[PATH_MAX];
    /* Write intent = access mode allows writing, or truncation.
     * O_CREAT alone with O_RDONLY (OPEN_ALWAYS for reading) must
     * NOT shadow an existing volume file with an empty overlay
     * file: it takes the read path first and only creates when the
     * file exists nowhere. */
    int write_intent = ((oflags & O_ACCMODE) != O_RDONLY)
                       || (oflags & O_TRUNC) != 0;
    if (ce_fs_map(ce_path, host, sizeof(host)) != 0) {
        if (err)
            *err = CE_ERROR_PATH_NOT_FOUND;
        return NULL;
    }
    if (!write_intent) {
        struct stat st;
        if (stat(host, &st) == 0 && S_ISREG(st.st_mode)) {
            int fd = open(host, oflags, 0666);
            if (fd >= 0) {
                ce_vfile *vf = calloc(1, sizeof(*vf));
                if (!vf) {
                    close(fd);
                    if (err)
                        *err = CE_ERROR_NOT_ENOUGH_MEMORY;
                    return NULL;
                }
                vf->fd = fd;
                return vf;
            }
        }
        CE_WCHAR rest[512];
        fatvol *vol = ce_fs_volume_of(ce_path, rest,
                                      sizeof(rest) / sizeof(rest[0]));
        if (vol) {
            fatvol_file *ff = fatvol_fopen_path(vol, rest);
            if (ff) {
                ce_vfile *vf = calloc(1, sizeof(*vf));
                if (!vf) {
                    fatvol_fclose(ff);
                    if (err)
                        *err = CE_ERROR_NOT_ENOUGH_MEMORY;
                    return NULL;
                }
                vf->is_fat = 1;
                vf->fd = -1;
                vf->ff = ff;
                return vf;
            }
        }
    }
    if (write_intent || (oflags & O_CREAT))
        mkdir_parents_of(host);
    int fd = open(host, oflags, 0666);
    if (fd < 0) {
        if (err)
            *err = ce_err_from_errno(errno);
        return NULL;
    }
    ce_vfile *vf = calloc(1, sizeof(*vf));
    if (!vf) {
        close(fd);
        if (err)
            *err = CE_ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    vf->fd = fd;
    return vf;
}

uint32_t ce_vfile_read(ce_vfile *vf, void *buf, uint32_t len)
{
    if (!vf || !buf)
        return 0;
    if (vf->is_fat)
        return fatvol_fread(vf->ff, buf, len);
    ssize_t r = read(vf->fd, buf, len);
    return r > 0 ? (uint32_t)r : 0;
}

uint32_t ce_vfile_write(ce_vfile *vf, const void *buf, uint32_t len)
{
    if (!vf || !buf || vf->is_fat)
        return 0;   /* volume files are read-only by construction */
    const uint8_t *p = buf;
    uint32_t done = 0;
    while (done < len) {
        ssize_t w = write(vf->fd, p + done, len - done);
        if (w <= 0)
            break;
        done += (uint32_t)w;
    }
    return done;
}

uint64_t ce_vfile_seek(ce_vfile *vf, int64_t off, int whence)
{
    if (!vf)
        return (uint64_t)-1;
    if (vf->is_fat) {
        int w = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? 1 : 2;
        int64_t r = fatvol_fseek(vf->ff, off, w);
        return r < 0 ? (uint64_t)-1 : (uint64_t)r;
    }
    off_t r = lseek(vf->fd, (off_t)off, whence);
    return r < 0 ? (uint64_t)-1 : (uint64_t)r;
}

uint32_t ce_vfile_size(ce_vfile *vf)
{
    if (!vf)
        return 0;
    if (vf->is_fat)
        return fatvol_fsize(vf->ff);
    struct stat st;
    if (fstat(vf->fd, &st) != 0)
        return 0;
    return (uint32_t)st.st_size;
}

int ce_vfile_trunc_here(ce_vfile *vf)
{
    if (!vf || vf->is_fat)
        return -1;
    off_t pos = lseek(vf->fd, 0, SEEK_CUR);
    if (pos < 0)
        return -1;
    return ftruncate(vf->fd, pos) == 0 ? 0 : -1;
}

void ce_vfile_close(ce_vfile *vf)
{
    if (!vf)
        return;
    if (vf->is_fat)
        fatvol_fclose(vf->ff);
    else if (vf->fd >= 0)
        close(vf->fd);
    free(vf);
}

int ce_fs_probe(const CE_WCHAR *ce_path, int *is_dir, uint32_t *size)
{
    char host[PATH_MAX];
    if (ce_fs_map(ce_path, host, sizeof(host)) == 0) {
        struct stat st;
        if (stat(host, &st) == 0) {
            if (is_dir)
                *is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            if (size)
                *size = (uint32_t)st.st_size;
            return 0;
        }
    }
    CE_WCHAR rest[512];
    fatvol *vol = ce_fs_volume_of(ce_path, rest,
                                  sizeof(rest) / sizeof(rest[0]));
    if (vol) {
        fatvol_stat_t fst;
        if (fatvol_stat_path(vol, rest, &fst) == 0) {
            if (is_dir)
                *is_dir = fst.is_dir;
            if (size)
                *size = fst.size;
            return 0;
        }
    }
    return -1;
}

/* ---- shared wide-string helpers ---- */

CE_WCHAR *ce_wdup(const CE_WCHAR *s)
{
    if (!s)
        return NULL;
    size_t n = ce_wcslen(s);
    CE_WCHAR *p = malloc((n + 1) * sizeof(CE_WCHAR));
    if (p)
        memcpy(p, s, (n + 1) * sizeof(CE_WCHAR));
    return p;
}

int ce_wcscasecmp(const CE_WCHAR *a, const CE_WCHAR *b)
{
    if (!a || !b)
        return (a == b) ? 0 : (a ? 1 : -1);
    for (;; a++, b++) {
        CE_WCHAR ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z')
            ca = (CE_WCHAR)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = (CE_WCHAR)(cb - 'a' + 'A');
        if (ca != cb)
            return (int)ca - (int)cb;
        if (!ca)
            return 0;
    }
}

int ce_vfile_host_fd(ce_vfile *vf)
{
    if (!vf || vf->is_fat)
        return -1;
    return vf->fd;
}
