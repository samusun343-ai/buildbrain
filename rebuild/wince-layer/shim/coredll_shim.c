/* BrainCE Layer -- coredll shim, M1 minimal set (34 functions).
 *
 * Implements the DESIGN-wince-layer.md section 9.1 set on top of
 * Linux: file I/O via the FS bridge v0, memory via malloc/mmap,
 * base services via clock_gettime/nanosleep, and the CRT/string
 * minimum the test binary imports. All entry points are named ce_*
 * internally so nothing interposes the host libc; the loader binds
 * CE imports to these addresses through shim_table.h (ordinals from
 * the census ordmap for this CE build).
 *
 * Unimplemented imports are bound to the stub pool (stub_pool.S);
 * every stub call is logged and counted, and ce_stub_report() prints
 * the totals the acceptance check requires to be zero.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <dirent.h>
#include <fnmatch.h>
#include <malloc.h>
#include <stdarg.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>

#include "ce_types.h"
#include "fs_bridge.h"
#include "shim_table.h"

__thread CE_DWORD ce_tls_lasterror = 0;

#ifndef CE_ERROR_PROC_NOT_FOUND
#define CE_ERROR_PROC_NOT_FOUND     127
#define CE_ERROR_RESOURCE_NOT_FOUND 1812
#endif

/* ================= stub machinery ================= */

extern char ce_stub_pool[];
#define CE_STUB_SLOTS 256

struct stub_rec {
    char dll[32];
    char name[64];
    int ordinal; /* -1 when bound by name */
    unsigned count;
};
static struct stub_rec g_stubs[CE_STUB_SLOTS];
static int g_stub_used = 0;
static unsigned g_stub_total = 0;
static FILE *g_stub_log = NULL;

int ce_stub_register(const char *dll, int ordinal, const char *name)
{
    if (g_stub_used >= CE_STUB_SLOTS)
        return -1;
    int slot = g_stub_used++;
    struct stub_rec *r = &g_stubs[slot];
    snprintf(r->dll, sizeof(r->dll), "%s", dll ? dll : "?");
    snprintf(r->name, sizeof(r->name), "%s", name ? name : "");
    r->ordinal = ordinal;
    r->count = 0;
    return slot;
}

void *ce_stub_addr(int slot)
{
    return (void *)(ce_stub_pool + slot * 8);
}

static void stub_log_line(const struct stub_rec *r)
{
    FILE *f = g_stub_log ? g_stub_log : stderr;
    if (r->name[0])
        fprintf(f, "[braince-stub] %s!%s (ord %d) call #%u\n",
                r->dll, r->name, r->ordinal, r->count);
    else
        fprintf(f, "[braince-stub] %s!#%d call #%u\n",
                r->dll, r->ordinal, r->count);
    fflush(f);
}

/* Entered from stub_pool.S with r0 = slot; lr = CE return address. */
unsigned ce_stub_common(unsigned slot)
{
    if (slot < (unsigned)g_stub_used) {
        struct stub_rec *r = &g_stubs[slot];
        r->count++;
        g_stub_total++;
        stub_log_line(r);
    }
    return 0;
}

void ce_stub_set_log(FILE *f) { g_stub_log = f; }
unsigned ce_stub_total(void) { return g_stub_total; }

void ce_stub_report(FILE *f)
{
    fprintf(f, "[braince] stub calls total=%u distinct=%d\n",
            g_stub_total, g_stub_used);
    for (int i = 0; i < g_stub_used; i++) {
        struct stub_rec *r = &g_stubs[i];
        if (r->count)
            fprintf(f, "[braince]   stub %s!%s (ord %d): %u calls\n",
                    r->dll, r->name, r->ordinal, r->count);
    }
}

/* ================= base services ================= */

CE_DWORD ce_GetLastError(void)
{
    return ce_tls_lasterror;
}

void ce_SetLastError(CE_DWORD e)
{
    ce_tls_lasterror = e;
}

CE_DWORD ce_GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return (CE_DWORD)(ms & 0xFFFFFFFFu);
}

void ce_Sleep(CE_DWORD ms)
{
    if (ms == 0) {
        sched_yield();
        return;
    }
    if (ms == 0xFFFFFFFFu) { /* INFINITE: park (unused by M1 tests) */
        for (;;)
            pause();
    }
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void ce_OutputDebugStringW(const CE_WCHAR *w)
{
    char buf[1024];
    if (!w) {
        fprintf(stderr, "[ce-ods] (null)\n");
        return;
    }
    if (ce_w2u8(w, buf, sizeof(buf)) < 0) {
        /* too long: emit a truncated copy */
        CE_WCHAR tmp[256];
        size_t n = ce_wcslen(w);
        if (n > 255)
            n = 255;
        memcpy(tmp, w, n * sizeof(CE_WCHAR));
        tmp[n] = 0;
        ce_w2u8(tmp, buf, sizeof(buf));
    }
    fprintf(stderr, "[ce-ods] %s\n", buf);
    fflush(stderr);
}

/* ================= memory ================= */

void *ce_LocalAlloc(CE_DWORD flags, size_t size)
{
    void *p;
    if (flags & CE_LMEM_ZEROINIT)
        p = calloc(1, size ? size : 1);
    else
        p = malloc(size ? size : 1);
    if (!p)
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
    return p;
}

void *ce_LocalFree(void *h)
{
    if (h)
        free(h);
    return NULL; /* CE: NULL return = success */
}

void *ce_LocalReAlloc(void *h, size_t size, CE_DWORD flags)
{
    size_t old = h ? malloc_usable_size(h) : 0;
    void *p = realloc(h, size ? size : 1);
    if (!p) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if ((flags & CE_LMEM_ZEROINIT) && size > old)
        memset((char *)p + old, 0, size - old);
    return p;
}

struct va_rec { void *addr; size_t size; };
static struct va_rec g_va[64];

void *ce_VirtualAlloc(void *addr, size_t size, CE_DWORD type,
                      CE_DWORD protect)
{
    (void)type; /* v0: RESERVE and COMMIT both map eagerly */
    if (size == 0) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t rsize = (size + page - 1) & ~(page - 1);
    int prot = PROT_READ | PROT_WRITE;
    if (protect == 0x01) prot = PROT_NONE;                    /* NOACCESS */
    else if (protect == 0x02) prot = PROT_READ;               /* READONLY */
    else if (protect == 0x20) prot = PROT_READ | PROT_EXEC;   /* EXECUTE_READ */
    else if (protect == 0x40 || protect == 0x80)
        prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *want = addr;
    if (want) {
        uintptr_t a = (uintptr_t)want & ~(uintptr_t)(page - 1);
        want = (void *)a;
        flags |= MAP_FIXED_NOREPLACE;
    }
    void *p = mmap(want, rsize, prot, flags, -1, 0);
    if (p == MAP_FAILED) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    for (size_t i = 0; i < sizeof(g_va) / sizeof(g_va[0]); i++) {
        if (!g_va[i].addr) {
            g_va[i].addr = p;
            g_va[i].size = rsize;
            break;
        }
    }
    return p;
}

CE_BOOL ce_VirtualFree(void *addr, size_t size, CE_DWORD type)
{
    if (type == CE_MEM_RELEASE) {
        for (size_t i = 0; i < sizeof(g_va) / sizeof(g_va[0]); i++) {
            if (g_va[i].addr == addr) {
                munmap(addr, g_va[i].size);
                g_va[i].addr = NULL;
                g_va[i].size = 0;
                return CE_TRUE;
            }
        }
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    (void)size;
    /* MEM_DECOMMIT in v0: pages stay mapped; report success. */
    return CE_TRUE;
}

/* ================= file I/O (via FS bridge) ================= */

static CE_DWORD ce_map_errno(int e, int is_open)
{
    switch (e) {
    case ENOENT:  return is_open ? CE_ERROR_FILE_NOT_FOUND
                                 : CE_ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return CE_ERROR_PATH_NOT_FOUND;
    case EACCES:  return CE_ERROR_ACCESS_DENIED;
    case EEXIST:  return CE_ERROR_FILE_EXISTS;
    case EINVAL:  return CE_ERROR_INVALID_PARAMETER;
    case ENOSPC:  return 112; /* ERROR_DISK_FULL */
    case ENFILE:
    case EMFILE:  return CE_ERROR_TOO_MANY_OPEN_FILES;
    default:      return CE_ERROR_INVALID_PARAMETER;
    }
}

CE_HANDLE ce_CreateFileW(const CE_WCHAR *name, CE_DWORD access,
                         CE_DWORD share, void *secattr,
                         CE_DWORD disposition, CE_DWORD flags,
                         CE_HANDLE tmpl)
{
    (void)share; (void)secattr; (void)flags; (void)tmpl;
    char path[PATH_MAX];
    if (!name || ce_fs_map(name, path, sizeof(path)) != 0) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_INVALID_HANDLE_VALUE;
    }
    int oflags = 0;
    int rd = (access & CE_GENERIC_READ) != 0;
    int wr = (access & CE_GENERIC_WRITE) != 0;
    if (rd && wr) oflags |= O_RDWR;
    else if (wr)  oflags |= O_WRONLY;
    else          oflags |= O_RDONLY;
    switch (disposition) {
    case CE_CREATE_NEW:        oflags |= O_CREAT | O_EXCL; break;
    case CE_CREATE_ALWAYS:     oflags |= O_CREAT | O_TRUNC; break;
    case CE_OPEN_EXISTING:     break;
    case CE_OPEN_ALWAYS:       oflags |= O_CREAT; break;
    case CE_TRUNCATE_EXISTING: oflags |= O_TRUNC; break;
    default:
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_INVALID_HANDLE_VALUE;
    }
    /* M3: the bridge resolves the path -- overlay/CE root for
     * writes, overlay-then-volume for reads (S0 read path). The
     * mapped host path computed above is only used for the
     * directory pre-checks here. */
    (void)path;
    {
        int is_dir = 0;
        int exists = ce_fs_probe(name, &is_dir, NULL) == 0;
        if (exists && is_dir) {
            /* M1: directory handles are not supported. */
            ce_set_lasterror(CE_ERROR_ACCESS_DENIED);
            return CE_INVALID_HANDLE_VALUE;
        }
        if (disposition == CE_CREATE_NEW && exists) {
            ce_set_lasterror(CE_ERROR_FILE_EXISTS);
            return CE_INVALID_HANDLE_VALUE;
        }
        if ((disposition == CE_OPEN_EXISTING
             || disposition == CE_TRUNCATE_EXISTING) && !exists) {
            ce_set_lasterror(CE_ERROR_FILE_NOT_FOUND);
            return CE_INVALID_HANDLE_VALUE;
        }
    }
    CE_DWORD err = 0;
    ce_vfile *vf = ce_fs_open(name, oflags, &err);
    if (!vf) {
        ce_set_lasterror(err ? err : CE_ERROR_FILE_NOT_FOUND);
        return CE_INVALID_HANDLE_VALUE;
    }
    return ce_handle_new_vfile(vf);
}

static ce_vfile *file_entry(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_VFILE || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return NULL;
    }
    return (ce_vfile *)e->ptr;
}

CE_BOOL ce_ReadFile(CE_HANDLE h, void *buf, CE_DWORD want,
                    CE_DWORD *got, void *overlapped)
{
    ce_vfile *vf = file_entry(h);
    if (!vf)
        return CE_FALSE;
    if (overlapped) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    uint32_t r = ce_vfile_read(vf, buf, want);
    if (got)
        *got = r;
    return CE_TRUE;
}

CE_BOOL ce_WriteFile(CE_HANDLE h, const void *buf, CE_DWORD want,
                     CE_DWORD *put, void *overlapped)
{
    ce_vfile *vf = file_entry(h);
    if (!vf)
        return CE_FALSE;
    if (overlapped) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    if (vf->is_fat) {
        /* volume files are read-only by construction */
        ce_set_lasterror(CE_ERROR_ACCESS_DENIED);
        if (put)
            *put = 0;
        return CE_FALSE;
    }
    uint32_t done = ce_vfile_write(vf, buf, want);
    if (put)
        *put = done;
    if (done < want) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_FALSE;
    }
    return CE_TRUE;
}

CE_DWORD ce_SetFilePointer(CE_HANDLE h, CE_LONG dist,
                           CE_LONG *dist_high, CE_DWORD method)
{
    ce_vfile *vf = file_entry(h);
    if (!vf)
        return CE_INVALID_SET_FILE_POINTER;
    int whence;
    switch (method) {
    case CE_FILE_BEGIN:   whence = SEEK_SET; break;
    case CE_FILE_CURRENT: whence = SEEK_CUR; break;
    case CE_FILE_END:     whence = SEEK_END; break;
    default:
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_INVALID_SET_FILE_POINTER;
    }
    int64_t off = dist;
    if (dist_high)
        off = (int64_t)(((uint64_t)(uint32_t)*dist_high << 32)
                        | (uint32_t)dist);
    uint64_t r = ce_vfile_seek(vf, off, whence);
    if (r == (uint64_t)-1) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_INVALID_SET_FILE_POINTER;
    }
    if (dist_high)
        *dist_high = (CE_LONG)(r >> 32);
    ce_set_lasterror(CE_ERROR_SUCCESS);
    return (CE_DWORD)(r & 0xFFFFFFFFu);
}

CE_DWORD ce_GetFileSize(CE_HANDLE h, CE_DWORD *high)
{
    ce_vfile *vf = file_entry(h);
    if (!vf)
        return (CE_DWORD)-1;
    uint32_t sz = ce_vfile_size(vf);
    if (high)
        *high = 0;
    ce_set_lasterror(CE_ERROR_SUCCESS);
    return sz;
}

CE_BOOL ce_FlushFileBuffers(CE_HANDLE h)
{
    ce_vfile *vf = file_entry(h);
    if (!vf)
        return CE_FALSE;
    if (vf->is_fat)
        return CE_TRUE;   /* nothing buffered on the read-only path */
    if (fsync(vf->fd) != 0) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_FALSE;
    }
    return CE_TRUE;
}

CE_BOOL ce_SetEndOfFile(CE_HANDLE h)
{
    ce_vfile *vf = file_entry(h);
    if (!vf)
        return CE_FALSE;
    if (ce_vfile_trunc_here(vf) != 0) {
        ce_set_lasterror(CE_ERROR_ACCESS_DENIED);
        return CE_FALSE;
    }
    return CE_TRUE;
}

struct ce_find_state {
    DIR *dir;
    char pattern[260];
    char dirpath[PATH_MAX];
    /* M3: enumeration of a CE volume directory (read-only FAT) */
    fatvol  *fv;
    CE_WCHAR fv_dir[260];
    uint32_t fv_cursor;
};

static CE_FILETIME ce_to_filetime(const struct timespec *ts)
{
    uint64_t v = ((uint64_t)ts->tv_sec + 11644473600ULL) * 10000000ULL
                 + (uint64_t)(ts->tv_nsec / 100);
    CE_FILETIME ft;
    ft.dwLowDateTime = (CE_DWORD)(v & 0xFFFFFFFFu);
    ft.dwHighDateTime = (CE_DWORD)(v >> 32);
    return ft;
}

static int ce_fill_find(const char *dirpath, const char *name,
                        CE_WIN32_FIND_DATAW *fd)
{
    char full[PATH_MAX];
    if (snprintf(full, sizeof(full), "%s/%s", dirpath, name)
            >= (int)sizeof(full))
        return -1;
    struct stat st;
    if (stat(full, &st) != 0)
        return -1;
    memset(fd, 0, sizeof(*fd));
    if (S_ISDIR(st.st_mode))
        fd->dwFileAttributes = CE_FILE_ATTRIBUTE_DIRECTORY;
    else if (!(st.st_mode & 0222))
        fd->dwFileAttributes = CE_FILE_ATTRIBUTE_READONLY;
    else
        fd->dwFileAttributes = CE_FILE_ATTRIBUTE_NORMAL;
    fd->ftCreationTime = ce_to_filetime(&st.st_mtim);
    fd->ftLastAccessTime = ce_to_filetime(&st.st_atim);
    fd->ftLastWriteTime = ce_to_filetime(&st.st_mtim);
    fd->nFileSizeHigh = (CE_DWORD)((uint64_t)st.st_size >> 32);
    fd->nFileSizeLow = (CE_DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
    if (ce_u82w(name, fd->cFileName, 260) < 0)
        return -1;
    return 0;
}

static int ce_wild_match(const char *pat, const char *name)
{
    /* CE/DOS semantics: "*.*" matches every name. */
    if (strcmp(pat, "*.*") == 0)
        return 1;
    return fnmatch(pat, name, 0) == 0;
}

/* Advance the find state to the next matching entry. 0 = filled. */
static int ce_find_next_match(struct ce_find_state *fs,
                              CE_WIN32_FIND_DATAW *fd)
{
    if (fs->fv) {
        fatvol_dirent_t ent;
        for (;;) {
            int r = fatvol_list(fs->fv, fs->fv_dir, &fs->fv_cursor,
                                &ent);
            if (r <= 0)
                return -1;
            char u8[780];
            if (ce_w2u8(ent.name, u8, sizeof(u8)) < 0)
                continue;
            if (!ce_wild_match(fs->pattern, u8))
                continue;
            memset(fd, 0, sizeof(*fd));
            fd->dwFileAttributes = ent.is_dir
                ? CE_FILE_ATTRIBUTE_DIRECTORY
                : (CE_FILE_ATTRIBUTE_READONLY
                   | CE_FILE_ATTRIBUTE_ARCHIVE);
            fd->nFileSizeLow = ent.size;
            size_t n = 0;
            while (ent.name[n] && n < 259) {
                fd->cFileName[n] = ent.name[n];
                n++;
            }
            fd->cFileName[n] = 0;
            return 0;
        }
    }
    struct dirent *de;
    while ((de = readdir(fs->dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0
            || strcmp(de->d_name, "..") == 0)
            continue;
        if (!ce_wild_match(fs->pattern, de->d_name))
            continue;
        if (ce_fill_find(fs->dirpath, de->d_name, fd) == 0)
            return 0;
    }
    return -1;
}

CE_HANDLE ce_FindFirstFileW(const CE_WCHAR *pattern,
                            CE_WIN32_FIND_DATAW *fd)
{
    char path[PATH_MAX];
    if (!pattern || !fd || ce_fs_map(pattern, path, sizeof(path)) != 0) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_INVALID_HANDLE_VALUE;
    }
    char *slash = strrchr(path, '/');
    if (!slash) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_INVALID_HANDLE_VALUE;
    }
    const char *pat = slash + 1;
    struct ce_find_state *fs = calloc(1, sizeof(*fs));
    if (!fs) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return CE_INVALID_HANDLE_VALUE;
    }
    snprintf(fs->pattern, sizeof(fs->pattern), "%s", pat);
    *slash = '\0';
    snprintf(fs->dirpath, sizeof(fs->dirpath), "%s", path);
    fs->dir = opendir(path[0] ? path : "/");
    if (!fs->dir) {
        /* M3: the directory may live on a CE volume instead of the
         * overlay. Split the original CE pattern into dir + name
         * and resolve the dir against the volume table. */
        const CE_WCHAR *ls = pattern;
        for (const CE_WCHAR *p = pattern; *p; p++)
            if (*p == '\\' || *p == '/')
                ls = p;
        CE_WCHAR dirw[260];
        size_t dn = (size_t)(ls - pattern);
        if (dn > 259)
            dn = 259;
        memcpy(dirw, pattern, dn * sizeof(CE_WCHAR));
        dirw[dn] = 0;
        CE_WCHAR rest[260];
        fatvol *vol = ce_fs_volume_of(dirw, rest,
                                      sizeof(rest) / sizeof(rest[0]));
        if (!vol) {
            free(fs);
            ce_set_lasterror(ce_map_errno(errno, 1));
            return CE_INVALID_HANDLE_VALUE;
        }
        fs->fv = vol;
        size_t rn = 0;
        while (rest[rn] && rn < 259) {
            fs->fv_dir[rn] = rest[rn];
            rn++;
        }
        fs->fv_dir[rn] = 0;
        if (rn == 0) {
            fs->fv_dir[0] = '\\';
            fs->fv_dir[1] = 0;
        }
        fs->fv_cursor = 0;
    }
    if (ce_find_next_match(fs, fd) != 0) {
        if (fs->dir)
            closedir(fs->dir);
        free(fs);
        ce_set_lasterror(CE_ERROR_FILE_NOT_FOUND);
        return CE_INVALID_HANDLE_VALUE;
    }
    CE_HANDLE h = ce_handle_new_find(fs);
    if (h == CE_INVALID_HANDLE_VALUE) {
        if (fs->dir)
            closedir(fs->dir);
        free(fs);
    }
    return h;
}

CE_BOOL ce_FindNextFileW(CE_HANDLE h, CE_WIN32_FIND_DATAW *fd)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_FIND || !fd) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    if (ce_find_next_match(e->find, fd) != 0) {
        ce_set_lasterror(CE_ERROR_FILE_NOT_FOUND);
        return CE_FALSE;
    }
    return CE_TRUE;
}

CE_BOOL ce_FindClose(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_FIND) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    if (e->find->dir)
        closedir(e->find->dir);
    free(e->find);
    ce_handle_forget(h);
    return CE_TRUE;
}

CE_BOOL ce_CloseHandle(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    if (e->kind == CE_HANDLE_KIND_FILE) {
        close(e->fd);
        ce_handle_forget(h);
        return CE_TRUE;
    }
    if (e->kind == CE_HANDLE_KIND_VFILE) {
        ce_vfile_close((ce_vfile *)e->ptr);
        ce_handle_forget(h);
        return CE_TRUE;
    }
    if (e->kind == CE_HANDLE_KIND_FIND)
        return ce_FindClose(h);
    if (e->kind == CE_HANDLE_KIND_MUTEX
        || e->kind == CE_HANDLE_KIND_MAP) {
        extern void ce_m3_close_sync_handle(ce_handle_entry *e);
        ce_m3_close_sync_handle(e);
        ce_handle_forget(h);
        return CE_TRUE;
    }
    if (e->kind == CE_HANDLE_KIND_THREAD
        || e->kind == CE_HANDLE_KIND_PROCESS
        || e->kind == CE_HANDLE_KIND_RSRC) {
        /* Records are pool-owned (threads/processes) or small
         * allocations (resources); the record cleanup lives with
         * the owning section below. Forgetting the handle is the
         * CE-visible effect; a still-running thread keeps its
         * record (ce_thread_rec pool is never compacted). */
        if (e->kind == CE_HANDLE_KIND_RSRC)
            free(e->ptr);
        ce_handle_forget(h);
        return CE_TRUE;
    }
    ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
    return CE_FALSE;
}

CE_BOOL ce_DeleteFileW(const CE_WCHAR *name)
{
    char path[PATH_MAX];
    if (!name || ce_fs_map(name, path, sizeof(path)) != 0) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_FALSE;
    }
    if (unlink(path) != 0) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_FALSE;
    }
    return CE_TRUE;
}

CE_BOOL ce_CreateDirectoryW(const CE_WCHAR *name, void *secattr)
{
    (void)secattr;
    char path[PATH_MAX];
    if (!name || ce_fs_map(name, path, sizeof(path)) != 0) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_FALSE;
    }
    if (mkdir(path, 0777) != 0) {
        if (errno == EEXIST)
            ce_set_lasterror(CE_ERROR_ALREADY_EXISTS);
        else
            ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_FALSE;
    }
    return CE_TRUE;
}

CE_DWORD ce_GetFileAttributesW(const CE_WCHAR *name)
{
    char path[PATH_MAX];
    if (!name || ce_fs_map(name, path, sizeof(path)) != 0) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_INVALID_FILE_ATTRIBUTES;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        /* fall back to the CE volumes (read-only storage) */
        int is_dir = 0;
        if (ce_fs_probe(name, &is_dir, NULL) == 0)
            return is_dir ? CE_FILE_ATTRIBUTE_DIRECTORY
                          : (CE_FILE_ATTRIBUTE_READONLY
                             | CE_FILE_ATTRIBUTE_ARCHIVE);
        ce_set_lasterror(ce_map_errno(errno, 1));
        return CE_INVALID_FILE_ATTRIBUTES;
    }
    if (S_ISDIR(st.st_mode))
        return CE_FILE_ATTRIBUTE_DIRECTORY;
    if (!(st.st_mode & 0222))
        return CE_FILE_ATTRIBUTE_READONLY;
    return CE_FILE_ATTRIBUTE_NORMAL;
}

/* ================= CRT / string minimum ================= */

void *ce_memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    for (size_t i = 0; i < n; i++)
        dd[i] = ss[i];
    return d;
}

void *ce_memset(void *d, int c, size_t n)
{
    unsigned char *dd = d;
    for (size_t i = 0; i < n; i++)
        dd[i] = (unsigned char)c;
    return d;
}

void *ce_memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) {
        for (size_t i = 0; i < n; i++)
            dd[i] = ss[i];
    } else {
        for (size_t i = n; i > 0; i--)
            dd[i - 1] = ss[i - 1];
    }
    return d;
}

int ce_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

size_t ce_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

size_t ce_wcslen(const CE_WCHAR *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

char *ce_strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != '\0')
        ;
    return r;
}

CE_WCHAR *ce_wcscpy(CE_WCHAR *d, const CE_WCHAR *s)
{
    CE_WCHAR *r = d;
    while ((*d = *s) != 0) {
        d++;
        s++;
    }
    return r;
}

CE_WCHAR *ce_wcscat(CE_WCHAR *d, const CE_WCHAR *s)
{
    CE_WCHAR *r = d;
    d += ce_wcslen(d);
    while ((*d = *s) != 0) {
        d++;
        s++;
    }
    return r;
}

int ce_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int ce_wcscmp(const CE_WCHAR *a, const CE_WCHAR *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

/* ================= module identity (M2, M3 module system) =====
 * The loader owns the module registry (loader/modules.c); these
 * functions delegate to it. HMODULE values are module bases. */

#include "../loader/module.h"

CE_HANDLE ce_GetModuleHandleW(const CE_WCHAR *name)
{
    void *base = ce_loader_module_handle(name);
    if (!base) {
        ce_set_lasterror(CE_ERROR_MOD_NOT_FOUND);
        return NULL;
    }
    return (CE_HANDLE)base;
}

CE_DWORD ce_GetModuleFileNameW(CE_HANDLE hmod, CE_WCHAR *buf, CE_DWORD max)
{
    if (!buf || max == 0)
        return 0;
    int n = ce_loader_module_filename(hmod, buf, (int)max);
    if (n <= 0 && hmod) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (CE_DWORD)n;
}

CE_HANDLE ce_LoadLibraryW(const CE_WCHAR *name)
{
    void *base = ce_loader_load_library(name);
    if (!base) {
        ce_set_lasterror(CE_ERROR_MOD_NOT_FOUND);
        return NULL;
    }
    return (CE_HANDLE)base;
}

void *ce_GetProcAddress(CE_HANDLE hmod, const char *name)
{
    if (!name)
        return NULL;
    long ordinal = -1;
    const char *nm = name;
    if (((uintptr_t)name >> 16) == 0) {
        ordinal = (long)(uintptr_t)name;
        nm = NULL;
    }
    void *r = ce_loader_get_proc(hmod, nm, ordinal);
    if (!r)
        ce_set_lasterror(CE_ERROR_PROC_NOT_FOUND);
    return r;
}

CE_BOOL ce_FreeLibrary(CE_HANDLE hmod)
{
    /* Modules stay mapped for the process lifetime (CE keeps DLLs
     * loaded per-process too once loaded by the loader; refcount
     * semantics are approximated as a no-op success). */
    return hmod ? CE_TRUE : CE_FALSE;
}

int ce_lstrcmpW(const CE_WCHAR *a, const CE_WCHAR *b)
{
    return ce_wcscmp(a, b);
}

int ce_lstrcmpiW(const CE_WCHAR *a, const CE_WCHAR *b)
{
    while (*a && *b) {
        CE_WCHAR ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (CE_WCHAR)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (CE_WCHAR)(cb + 32);
        if (ca != cb)
            return (int)ca - (int)cb;
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

/* Minimal wsprintfW: %s (wide and narrow via %S distinction not
 * supported; %s takes CE_WCHAR*), %d/%i, %u, %x/%X, %c, %%. No width
 * or precision support; unsupported specifiers are copied literally.
 * This covers the formatting CE utilities and test apps use. */
int ce_vwsprintfW(CE_WCHAR *out, const CE_WCHAR *fmt, va_list ap)
{
    CE_WCHAR *o = out;
    for (const CE_WCHAR *f = fmt; *f; f++) {
        if (*f != '%') {
            *o++ = *f;
            continue;
        }
        f++;
        if (*f == '%') {
            *o++ = '%';
            continue;
        }
        char num[32];
        const char *digits = "0123456789abcdef";
        switch (*f) {
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            unsigned u = v < 0 ? (unsigned)(-(v + 1)) + 1u : (unsigned)v;
            int n = 0;
            if (u == 0)
                num[n++] = '0';
            while (u) {
                num[n++] = (char)('0' + (u % 10));
                u /= 10;
            }
            if (v < 0)
                *o++ = '-';
            while (n)
                *o++ = (CE_WCHAR)num[--n];
            break;
        }
        case 'u': {
            unsigned u = va_arg(ap, unsigned);
            int n = 0;
            if (u == 0)
                num[n++] = '0';
            while (u) {
                num[n++] = (char)('0' + (u % 10));
                u /= 10;
            }
            while (n)
                *o++ = (CE_WCHAR)num[--n];
            break;
        }
        case 'x':
        case 'X': {
            unsigned u = va_arg(ap, unsigned);
            const char *dg = *f == 'X' ? "0123456789ABCDEF" : digits;
            int n = 0;
            if (u == 0)
                num[n++] = '0';
            while (u) {
                num[n++] = dg[u & 0xF];
                u >>= 4;
            }
            while (n)
                *o++ = (CE_WCHAR)num[--n];
            break;
        }
        case 's': {
            static const CE_WCHAR nullstr[] = { '(', 'n', 'u', 'l', 'l',
                                                ')', 0 };
            const CE_WCHAR *s = va_arg(ap, const CE_WCHAR *);
            if (!s)
                s = nullstr;
            while (*s)
                *o++ = *s++;
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            *o++ = (CE_WCHAR)c;
            break;
        }
        default:
            *o++ = '%';
            if (*f)
                *o++ = *f;
            break;
        }
    }
    *o = 0;
    return (int)(o - out);
}

int ce_wsprintfW(CE_WCHAR *out, const CE_WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = ce_vwsprintfW(out, fmt, ap);
    va_end(ap);
    return n;
}

/* ================= import resolution =================
 * shim_fns.h is declaration-only since M2; the table itself is
 * defined in shim_table_glue.c (see tools/gen_shim_table.py). */

#include "shim_fns.h"

/* Returns the address to write into the IAT. Sets *was_stub.
 * M1 hosts no real DLLs: anything that is not an implemented coredll
 * function becomes a logging stub. */
void *ce_resolve_import(const char *dll, int ordinal, const char *name,
                        int *was_stub)
{
    *was_stub = 0;
    if (dll && strcasecmp(dll, "coredll.dll") == 0) {
        /* Decorated C++ names are not valid C identifiers, so the
         * generated table cannot carry them; resolve the two the
         * hosted DLLs use by ordinal (census: ??2@YAPAXI@Z is
         * operator new, ??3@YAXPAX@Z is operator delete). */
        extern void *ce_op_new(size_t);
        extern void ce_op_delete(void *);
        if (ordinal == 1094)
            return (void *)ce_op_new;
        if (ordinal == 1095)
            return (void *)ce_op_delete;
        if (ordinal >= 0 && ordinal <= CE_ORD_INDEX_MAX) {
            int idx = ce_ord_index[ordinal];
            if (idx >= 0)
                return ce_shim_table[idx].fn;
        }
        if (name) {
            for (size_t i = 0; i < CE_SHIM_TABLE_COUNT; i++)
                if (strcmp(ce_shim_table[i].ce_name, name) == 0)
                    return ce_shim_table[i].fn;
        }
    }
    int slot = ce_stub_register(dll, ordinal, name);
    if (slot < 0) {
        fprintf(stderr, "[braince] stub pool exhausted for %s!%s\n",
                dll ? dll : "?", name ? name : "");
        return ce_stub_addr(0);
    }
    *was_stub = 1;
    return ce_stub_addr(slot);
}

/* ================= M3: threads / processes / sync ==============
 * Added for the first bundled app (TextMemoTool) and DLL hosting.
 * Thread and process records live in fixed pools; handles point at
 * them through the bridge handle table. */

#ifndef CE_WAIT_OBJECT_0
#define CE_WAIT_OBJECT_0 0u
#define CE_WAIT_TIMEOUT  258u
#define CE_WAIT_FAILED   0xFFFFFFFFu
#define CE_STILL_ACTIVE  259u
#define CE_INFINITE      0xFFFFFFFFu
#endif

typedef CE_DWORD (*ce_thread_proc)(void *param);

typedef struct ce_proc_rec ce_proc_rec;
struct ce_proc_rec {
    int       used;
    pid_t     pid;
    volatile int done;
    CE_DWORD  exit_code;
};

static uint32_t rd32le(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
#define rd32(p) rd32le(p)

typedef struct {
    int        used;
    pthread_t  tid;
    volatile int done;
    volatile int detached;
    CE_DWORD   exit_code;
    CE_DWORD   ce_tid;
} ce_thread_rec;

#define CE_MAX_THREADS 32
static ce_thread_rec g_threads[CE_MAX_THREADS];
static pthread_mutex_t g_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static CE_DWORD g_next_ce_tid = 2;   /* 1 = main thread */
static __thread CE_DWORD g_my_ce_tid = 0;

typedef struct {
    ce_thread_proc proc;
    void          *param;
    ce_thread_rec *rec;
} ce_thread_boot;

static void *ce_thread_tramp(void *arg)
{
    ce_thread_boot *boot = arg;
    ce_thread_rec *rec = boot->rec;
    g_my_ce_tid = rec->ce_tid;
    CE_DWORD rc = boot->proc(boot->param);
    rec->exit_code = rc;
    rec->done = 1;
    free(boot);
    return NULL;
}

CE_HANDLE ce_CreateThread(void *secattr, CE_DWORD stack,
                          ce_thread_proc proc, void *param,
                          CE_DWORD flags, CE_DWORD *tid_out)
{
    (void)secattr;
    (void)flags;   /* CREATE_SUSPENDED is not honoured in M3 */
    if (!proc) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    pthread_mutex_lock(&g_thread_lock);
    ce_thread_rec *rec = NULL;
    for (int i = 0; i < CE_MAX_THREADS; i++) {
        if (!g_threads[i].used) {
            rec = &g_threads[i];
            break;
        }
        if (g_threads[i].done && g_threads[i].detached) {
            rec = &g_threads[i];
            break;
        }
    }
    if (!rec) {
        pthread_mutex_unlock(&g_thread_lock);
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memset(rec, 0, sizeof(*rec));
    rec->used = 1;
    rec->ce_tid = g_next_ce_tid++;
    ce_thread_boot *boot = malloc(sizeof(*boot));
    if (!boot) {
        rec->used = 0;
        pthread_mutex_unlock(&g_thread_lock);
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    boot->proc = proc;
    boot->param = param;
    boot->rec = rec;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack >= 64 * 1024)
        pthread_attr_setstacksize(&attr, stack);
    int r = pthread_create(&rec->tid, &attr, ce_thread_tramp, boot);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        free(boot);
        rec->used = 0;
        pthread_mutex_unlock(&g_thread_lock);
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    pthread_mutex_unlock(&g_thread_lock);
    if (tid_out)
        *tid_out = rec->ce_tid;
    CE_HANDLE h = ce_handle_new_ptr(CE_HANDLE_KIND_THREAD, rec, 0);
    if (h == CE_INVALID_HANDLE_VALUE)
        return NULL;
    return h;
}

void ce_ExitThread(CE_DWORD code)
{
    /* Find our record to publish the exit code, then exit. On the
     * main thread pthread_exit would strand the process's other
     * threads; CE semantics end the process when the last thread
     * exits, which the loader's single-purpose processes
     * approximate by exiting outright from the main thread. */
    pthread_t self = pthread_self();
    pthread_mutex_lock(&g_thread_lock);
    for (int i = 0; i < CE_MAX_THREADS; i++) {
        if (g_threads[i].used
            && pthread_equal(g_threads[i].tid, self)) {
            g_threads[i].exit_code = code;
            g_threads[i].done = 1;
            pthread_mutex_unlock(&g_thread_lock);
            pthread_exit(NULL);
        }
    }
    pthread_mutex_unlock(&g_thread_lock);
    _exit((int)code & 0xFF);
}

CE_BOOL ce_TerminateThread(CE_HANDLE h, CE_DWORD code)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_THREAD || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    ce_thread_rec *rec = e->ptr;
    if (!rec->done) {
        /* Deferred cancellation: a CE thread in a tight compute
         * loop with no cancellation point keeps running until it
         * reaches one; the record is final either way. */
        pthread_cancel(rec->tid);
        rec->exit_code = code;
        rec->done = 1;
    }
    return CE_TRUE;
}

CE_BOOL ce_GetExitCodeThread(CE_HANDLE h, CE_DWORD *code)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || !code) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    if (e->kind == CE_HANDLE_KIND_THREAD && e->ptr) {
        ce_thread_rec *rec = e->ptr;
        *code = rec->done ? rec->exit_code : CE_STILL_ACTIVE;
        return CE_TRUE;
    }
    if (e->kind == CE_HANDLE_KIND_PROCESS && e->ptr) {
        ce_proc_rec *pr = e->ptr;
        *code = pr->done ? pr->exit_code : CE_STILL_ACTIVE;
        return CE_TRUE;
    }
    ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
    return CE_FALSE;
}

/* ---- processes ---- */

#define CE_MAX_PROCS 16
static ce_proc_rec g_procs[CE_MAX_PROCS];

static ce_proc_rec *proc_rec_new(pid_t pid)
{
    for (int i = 0; i < CE_MAX_PROCS; i++) {
        if (!g_procs[i].used) {
            g_procs[i].used = 1;
            g_procs[i].pid = pid;
            g_procs[i].done = 0;
            g_procs[i].exit_code = 0;
            return &g_procs[i];
        }
    }
    return NULL;
}

static void proc_poll(ce_proc_rec *pr)
{
    if (pr->done)
        return;
    int status = 0;
    pid_t r = waitpid(pr->pid, &status, WNOHANG);
    if (r == pr->pid) {
        pr->done = 1;
        pr->exit_code = WIFEXITED(status)
            ? (CE_DWORD)WEXITSTATUS(status)
            : (CE_DWORD)(0xC0000000u | (unsigned)WTERMSIG(status));
    } else if (r < 0 && errno == ECHILD) {
        pr->done = 1;
        pr->exit_code = 0;
    }
}

/* PROCESS_INFORMATION as CE apps lay it out. */
typedef struct {
    CE_HANDLE hProcess;
    CE_HANDLE hThread;
    CE_DWORD  dwProcessId;
    CE_DWORD  dwThreadId;
} ce_process_information;

CE_BOOL ce_CreateProcessW(const CE_WCHAR *app, const CE_WCHAR *cmd,
                          void *pattr, void *tattr, CE_BOOL inherit,
                          CE_DWORD flags, void *env, const CE_WCHAR *dir,
                          void *startup, ce_process_information *pi)
{
    (void)pattr; (void)tattr; (void)inherit; (void)flags; (void)env;
    (void)dir; (void)startup;
    if (!app && !cmd) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    const CE_WCHAR *target = app ? app : cmd;
    char host[PATH_MAX];
    if (ce_fs_map(target, host, sizeof(host)) != 0) {
        ce_set_lasterror(CE_ERROR_PATH_NOT_FOUND);
        return CE_FALSE;
    }
    struct stat st;
    if (stat(host, &st) != 0) {
        /* volume file: extract next to the spawn area, then run */
        CE_DWORD err = 0;
        ce_vfile *vf = ce_fs_open(target, O_RDONLY, &err);
        if (!vf) {
            ce_set_lasterror(CE_ERROR_FILE_NOT_FOUND);
            return CE_FALSE;
        }
        char u8[256];
        ce_w2u8(target, u8, sizeof(u8));
        const char *base = strrchr(u8, '\\');
        base = base ? base + 1 : u8;
        snprintf(host, sizeof(host), "%s/.spawn-%s", ce_fs_root(),
                 base);
        int fd = open(host, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd < 0) {
            ce_vfile_close(vf);
            ce_set_lasterror(CE_ERROR_FILE_NOT_FOUND);
            return CE_FALSE;
        }
        uint8_t buf[16384];
        uint32_t r;
        while ((r = ce_vfile_read(vf, buf, sizeof(buf))) > 0) {
            uint32_t off = 0;
            while (off < r) {
                ssize_t w = write(fd, buf + off, r - off);
                if (w <= 0)
                    break;
                off += (uint32_t)w;
            }
        }
        close(fd);
        ce_vfile_close(vf);
    }
    fprintf(stderr, "[braince] CreateProcessW: %s\n", host);
    pid_t pid = fork();
    if (pid < 0) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return CE_FALSE;
    }
    if (pid == 0) {
        const char *self = ce_loader_self_path();
        const char *root = ce_loader_fs_root_arg();
        const char *rom = ce_loader_rom_arg();
        if (rom && *rom)
            setenv("CE_ROM", rom, 1);
        if (rom && *rom)
            execl(self, self, "--fs-root", root, "--rom", rom, host,
                  (char *)NULL);
        else
            execl(self, self, "--fs-root", root, host, (char *)NULL);
        fprintf(stderr, "[braince] CreateProcessW: exec of %s "
                "failed (errno=%d)\n", self, errno);
        _exit(127);
    }
    ce_proc_rec *pr = proc_rec_new(pid);
    if (pi) {
        memset(pi, 0, sizeof(*pi));
        pi->dwProcessId = (CE_DWORD)pid;
        pi->dwThreadId = (CE_DWORD)pid;
        pi->hProcess = pr ? ce_handle_new_ptr(CE_HANDLE_KIND_PROCESS,
                                              pr, (long)pid) : NULL;
        pi->hThread = pr ? ce_handle_new_ptr(CE_HANDLE_KIND_PROCESS,
                                             pr, (long)pid) : NULL;
    }
    return CE_TRUE;
}

CE_BOOL ce_TerminateProcess(CE_HANDLE h, CE_DWORD code)
{
    if (!h || h == (CE_HANDLE)(uintptr_t)-1) {
        /* self: CE semantics are an immediate, unclean exit */
        fflush(NULL);
        _exit((int)code & 0xFF);
    }
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_PROCESS || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    ce_proc_rec *pr = e->ptr;
    if (!pr->done) {
        kill(pr->pid, SIGKILL);
        pr->exit_code = code;
    }
    return CE_TRUE;
}

CE_BOOL ce_GetExitCodeProcess(CE_HANDLE h, CE_DWORD *code)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_PROCESS || !e->ptr || !code) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    ce_proc_rec *pr = e->ptr;
    proc_poll(pr);
    *code = pr->done ? pr->exit_code : CE_STILL_ACTIVE;
    return CE_TRUE;
}

CE_DWORD ce_WaitForSingleObject(CE_HANDLE h, CE_DWORD ms)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_WAIT_FAILED;
    }
    if (e->kind == CE_HANDLE_KIND_THREAD && e->ptr) {
        ce_thread_rec *rec = e->ptr;
        if (rec->done)
            return CE_WAIT_OBJECT_0;
        if (ms == 0)
            return CE_WAIT_TIMEOUT;
        if (ms == CE_INFINITE) {
            if (!rec->detached) {
                pthread_join(rec->tid, NULL);
                rec->detached = 1;
            }
            while (!rec->done)
                sched_yield();
            return CE_WAIT_OBJECT_0;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(ms / 1000);
        ts.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        if (!rec->detached) {
            int r = pthread_timedjoin_np(rec->tid, NULL, &ts);
            if (r == 0) {
                rec->detached = 1;
                return CE_WAIT_OBJECT_0;
            }
            return CE_WAIT_TIMEOUT;
        }
        while (!rec->done) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > ts.tv_sec
                || (now.tv_sec == ts.tv_sec && now.tv_nsec >= ts.tv_nsec))
                return CE_WAIT_TIMEOUT;
            struct timespec pause = { 0, 2000000 };
            nanosleep(&pause, NULL);
        }
        return CE_WAIT_OBJECT_0;
    }
    if (e->kind == CE_HANDLE_KIND_PROCESS && e->ptr) {
        ce_proc_rec *pr = e->ptr;
        proc_poll(pr);
        if (pr->done)
            return CE_WAIT_OBJECT_0;
        if (ms == 0)
            return CE_WAIT_TIMEOUT;
        CE_DWORD waited = 0;
        for (;;) {
            int status = 0;
            pid_t r = waitpid(pr->pid, &status, WNOHANG);
            if (r == pr->pid) {
                pr->done = 1;
                pr->exit_code = WIFEXITED(status)
                    ? (CE_DWORD)WEXITSTATUS(status)
                    : (CE_DWORD)(0xC0000000u
                                 | (unsigned)WTERMSIG(status));
                return CE_WAIT_OBJECT_0;
            }
            if (r < 0 && errno == ECHILD) {
                pr->done = 1;
                return CE_WAIT_OBJECT_0;
            }
            if (ms != CE_INFINITE && waited >= ms)
                return CE_WAIT_TIMEOUT;
            struct timespec pause = { 0, 5000000 };
            nanosleep(&pause, NULL);
            waited += 5;
        }
    }
    if (e->kind == CE_HANDLE_KIND_MUTEX && e->ptr) {
        extern CE_DWORD ce_m3_wait_mutex(void *rec, CE_DWORD ms);
        return ce_m3_wait_mutex(e->ptr, ms);
    }
    ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
    return CE_WAIT_FAILED;
}

/* ---- critical sections: side table keyed by the app's pointer.
 * (CE CRITICAL_SECTION storage is app-owned and too small to hold
 * a pthread_mutex_t portably; the table also gives recursion via
 * PTHREAD_MUTEX_RECURSIVE, matching CE semantics.) ---- */

#define CE_MAX_CSECT 64
typedef struct {
    void *key;
    pthread_mutex_t mtx;
} ce_csect_rec;
static ce_csect_rec g_csects[CE_MAX_CSECT];
static pthread_mutex_t g_csect_lock = PTHREAD_MUTEX_INITIALIZER;

static ce_csect_rec *csect_find(void *key, int create)
{
    for (int i = 0; i < CE_MAX_CSECT; i++)
        if (g_csects[i].key == key)
            return &g_csects[i];
    if (!create)
        return NULL;
    for (int i = 0; i < CE_MAX_CSECT; i++) {
        if (!g_csects[i].key) {
            pthread_mutexattr_t at;
            pthread_mutexattr_init(&at);
            pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&g_csects[i].mtx, &at);
            pthread_mutexattr_destroy(&at);
            g_csects[i].key = key;
            return &g_csects[i];
        }
    }
    return NULL;
}

void ce_InitializeCriticalSection(void *cs)
{
    if (!cs)
        return;
    pthread_mutex_lock(&g_csect_lock);
    csect_find(cs, 1);
    pthread_mutex_unlock(&g_csect_lock);
}

void ce_EnterCriticalSection(void *cs)
{
    pthread_mutex_lock(&g_csect_lock);
    ce_csect_rec *r = csect_find(cs, 1);
    pthread_mutex_unlock(&g_csect_lock);
    if (r)
        pthread_mutex_lock(&r->mtx);
}

void ce_LeaveCriticalSection(void *cs)
{
    pthread_mutex_lock(&g_csect_lock);
    ce_csect_rec *r = csect_find(cs, 0);
    pthread_mutex_unlock(&g_csect_lock);
    if (r)
        pthread_mutex_unlock(&r->mtx);
}

CE_BOOL ce_TryEnterCriticalSection(void *cs)
{
    pthread_mutex_lock(&g_csect_lock);
    ce_csect_rec *r = csect_find(cs, 1);
    pthread_mutex_unlock(&g_csect_lock);
    if (!r)
        return CE_FALSE;
    return pthread_mutex_trylock(&r->mtx) == 0 ? CE_TRUE : CE_FALSE;
}

void ce_DeleteCriticalSection(void *cs)
{
    pthread_mutex_lock(&g_csect_lock);
    ce_csect_rec *r = csect_find(cs, 0);
    if (r) {
        pthread_mutex_destroy(&r->mtx);
        r->key = NULL;
    }
    pthread_mutex_unlock(&g_csect_lock);
}

/* ---- interlocked family ---- */

CE_LONG ce_InterlockedIncrement(volatile CE_LONG *p)
{
    return (CE_LONG)__sync_add_and_fetch((volatile int *)p, 1);
}

CE_LONG ce_InterlockedDecrement(volatile CE_LONG *p)
{
    return (CE_LONG)__sync_sub_and_fetch((volatile int *)p, 1);
}

CE_LONG ce_InterlockedExchange(volatile CE_LONG *p, CE_LONG v)
{
    return (CE_LONG)__sync_lock_test_and_set((volatile int *)p,
                                             (int)v);
}

CE_LONG ce_InterlockedCompareExchange(volatile CE_LONG *p, CE_LONG v,
                                      CE_LONG cmp)
{
    return (CE_LONG)__sync_val_compare_and_swap((volatile int *)p,
                                               (int)cmp, (int)v);
}

/* ---- resources ----
 * FindResourceW returns a handle-table record; LoadResource
 * returns the data pointer itself (this CE build has no
 * LockResource export: on CE the LoadResource result IS the
 * pointer to the resource data in the mapped image). */

typedef struct {
    void    *base;
    uint32_t data_rva;
    uint32_t size;
} ce_rsrc_rec;

static int res_name_eq(const uint8_t *img, uint32_t name_field,
                       const CE_WCHAR *want, int want_is_id,
                       uint32_t want_id)
{
    if (name_field & 0x80000000u) {
        if (want_is_id)
            return 0;
        const uint8_t *sp = img + (name_field & 0x7FFFFFFFu);
        uint16_t len;
        memcpy(&len, sp, 2);
        const CE_WCHAR *s = (const CE_WCHAR *)(sp + 2);
        size_t wl = ce_wcslen(want);
        if (len != wl)
            return 0;
        for (uint16_t i = 0; i < len; i++) {
            CE_WCHAR a = s[i], b = want[i];
            if (a >= 'a' && a <= 'z')
                a = (CE_WCHAR)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z')
                b = (CE_WCHAR)(b - 'a' + 'A');
            if (a != b)
                return 0;
        }
        return 1;
    }
    return want_is_id && name_field == want_id;
}

/* Walk one resource directory level; returns the offset field of
 * the matching entry (high bit = subdirectory), or 0. */
static uint32_t res_level(const uint8_t *img, uint32_t dir_rva,
                          const CE_WCHAR *want, int want_is_id,
                          uint32_t want_id, int any_first)
{
    const uint8_t *dir = img + dir_rva;
    uint16_t n_named, n_id;
    memcpy(&n_named, dir + 12, 2);
    memcpy(&n_id, dir + 14, 2);
    int n = n_named + n_id;
    for (int i = 0; i < n; i++) {
        const uint8_t *ent = dir + 16 + i * 8;
        uint32_t nm = rd32(ent);
        uint32_t off = rd32(ent + 4);
        if (any_first)
            return off;
        if (res_name_eq(img, nm, want, want_is_id, want_id))
            return off;
    }
    return 0;
}

CE_HANDLE ce_FindResourceW(CE_HANDLE hmod, const CE_WCHAR *name,
                           const CE_WCHAR *type)
{
    uint32_t rrva = 0, rsize = 0;
    void *base = hmod ? hmod : ce_loader_main_base();
    if (!base || ce_loader_module_resdir(base, &rrva, &rsize) != 0) {
        ce_set_lasterror(CE_ERROR_RESOURCE_NOT_FOUND);
        return NULL;
    }
    const uint8_t *img = base;
    int type_is_id = ((uintptr_t)type >> 16) == 0;
    int name_is_id = ((uintptr_t)name >> 16) == 0;
    uint32_t off = res_level(img, rrva, type, type_is_id,
                             (uint32_t)(uintptr_t)type, 0);
    if (!off || !(off & 0x80000000u))
        goto notfound;
    off = res_level(img, rrva + (off & 0x7FFFFFFFu), name, name_is_id,
                    (uint32_t)(uintptr_t)name, 0);
    if (!off || !(off & 0x80000000u))
        goto notfound;
    /* language level: first entry (neutral preferred by order) */
    off = res_level(img, rrva + (off & 0x7FFFFFFFu), NULL, 0, 0, 1);
    if (!off || (off & 0x80000000u))
        goto notfound;
    {
        const uint8_t *de = img + rrva + off;
        ce_rsrc_rec *rec = malloc(sizeof(*rec));
        if (!rec) {
            ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        rec->base = base;
        rec->data_rva = rd32(de);
        rec->size = rd32(de + 4);
        CE_HANDLE h = ce_handle_new_ptr(CE_HANDLE_KIND_RSRC, rec, 0);
        if (h == CE_INVALID_HANDLE_VALUE) {
            free(rec);
            return NULL;
        }
        return h;
    }
notfound:
    ce_set_lasterror(CE_ERROR_RESOURCE_NOT_FOUND);
    return NULL;
}

CE_HANDLE ce_LoadResource(CE_HANDLE hmod, CE_HANDLE hrsrc)
{
    (void)hmod;
    ce_handle_entry *e = ce_handle_get(hrsrc);
    if (!e || e->kind != CE_HANDLE_KIND_RSRC || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return NULL;
    }
    ce_rsrc_rec *rec = e->ptr;
    return (CE_HANDLE)((uint8_t *)rec->base + rec->data_rva);
}

CE_DWORD ce_SizeofResource(CE_HANDLE hmod, CE_HANDLE hrsrc)
{
    (void)hmod;
    ce_handle_entry *e = ce_handle_get(hrsrc);
    if (!e || e->kind != CE_HANDLE_KIND_RSRC || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return 0;
    }
    return ((ce_rsrc_rec *)e->ptr)->size;
}

/* ---- module API (W variant of GetProcAddress is the CE form) -- */

void *ce_GetProcAddressW(CE_HANDLE hmod, const CE_WCHAR *name)
{
    if (!name)
        return NULL;
    if (((uintptr_t)name >> 16) == 0) {
        void *r = ce_loader_get_proc(hmod, NULL,
                                     (long)(uintptr_t)name);
        if (!r)
            ce_set_lasterror(CE_ERROR_PROC_NOT_FOUND);
        return r;
    }
    char u8[256];
    if (ce_w2u8(name, u8, sizeof(u8)) < 0)
        return NULL;
    void *r = ce_loader_get_proc(hmod, u8, -1);
    if (!r)
        ce_set_lasterror(CE_ERROR_PROC_NOT_FOUND);
    return r;
}

/* ---- CRT / exception glue the bundled apps import ---- */

void ce_free(void *p)
{
    free(p);
}

static int g_xcpt_logged = 0;

CE_LONG ce__XcptFilter(CE_DWORD xcptnum, void *rec)
{
    if (!g_xcpt_logged) {
        g_xcpt_logged = 1;
        fprintf(stderr, "[braince] _XcptFilter called (code 0x%x); "
                "no SEH in this layer, returning "
                "EXCEPTION_CONTINUE_SEARCH\n", (unsigned)xcptnum);
    }
    (void)rec;
    return 0;   /* EXCEPTION_CONTINUE_SEARCH */
}

CE_LONG ce___C_specific_handler(void *rec, void *frame, void *ctx,
                                void *disp)
{
    (void)rec; (void)frame; (void)ctx; (void)disp;
    return 0;   /* ExceptionContinueSearch */
}

CE_DWORD ce___security_gen_cookie2(CE_DWORD seed)
{
    /* The CRT startup calls this to obtain the process cookie when
     * the static cookie is unset; any well-mixed value serves. */
    CE_DWORD v = seed ^ (CE_DWORD)(uintptr_t)&v ^ ce_GetTickCount()
                 ^ ((CE_DWORD)getpid() << 16);
    return v ? v : 0xBB40E64Eu;
}

/* ================= M3: mutexes, file mapping, waits ======= */

typedef struct {
    pthread_mutex_t m;
} ce_mutex_rec;

CE_HANDLE ce_CreateMutexW(void *attr, CE_BOOL initial_owner,
                          const CE_WCHAR *name)
{
    (void)attr;
    (void)name;   /* named cross-process mutexes: later phase */
    ce_mutex_rec *rec = malloc(sizeof(*rec));
    if (!rec) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rec->m, &at);
    pthread_mutexattr_destroy(&at);
    if (initial_owner)
        pthread_mutex_lock(&rec->m);
    CE_HANDLE h = ce_handle_new_ptr(CE_HANDLE_KIND_MUTEX, rec, 0);
    if (h == CE_INVALID_HANDLE_VALUE) {
        pthread_mutex_destroy(&rec->m);
        free(rec);
        return NULL;
    }
    return h;
}

CE_BOOL ce_ReleaseMutex(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_MUTEX || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return CE_FALSE;
    }
    ce_mutex_rec *rec = e->ptr;
    if (pthread_mutex_unlock(&rec->m) != 0) {
        ce_set_lasterror(288);   /* ERROR_NOT_OWNER */
        return CE_FALSE;
    }
    return CE_TRUE;
}

CE_DWORD ce_m3_wait_mutex(void *rec_v, CE_DWORD ms)
{
    ce_mutex_rec *rec = rec_v;
    if (ms == CE_INFINITE) {
        pthread_mutex_lock(&rec->m);
        return CE_WAIT_OBJECT_0;
    }
    if (ms == 0)
        return pthread_mutex_trylock(&rec->m) == 0
               ? CE_WAIT_OBJECT_0 : CE_WAIT_TIMEOUT;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(ms / 1000);
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_mutex_timedlock(&rec->m, &ts) == 0
           ? CE_WAIT_OBJECT_0 : CE_WAIT_TIMEOUT;
}

/* ---- file mapping ---- */

typedef struct {
    int fd;              /* dup'd file fd, or -1 (pagefile) */
    uint64_t size;
} ce_map_rec;

typedef struct ce_view {
    void *addr;
    size_t len;
    struct ce_view *next;
} ce_view;

static ce_view *g_views;

CE_HANDLE ce_CreateFileMappingW(CE_HANDLE hfile, void *attr,
                                CE_DWORD protect, CE_DWORD size_hi,
                                CE_DWORD size_lo, const CE_WCHAR *name)
{
    (void)attr;
    (void)protect;
    (void)name;
    ce_map_rec *rec = calloc(1, sizeof(*rec));
    if (!rec) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    rec->fd = -1;
    rec->size = ((uint64_t)size_hi << 32) | size_lo;
    if (hfile && hfile != CE_INVALID_HANDLE_VALUE) {
        ce_handle_entry *e = ce_handle_get(hfile);
        if (!e) {
            free(rec);
            ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
            return NULL;
        }
        if (e->kind == CE_HANDLE_KIND_FILE) {
            rec->fd = dup(e->fd);
        } else if (e->kind == CE_HANDLE_KIND_VFILE) {
            int hfd = ce_vfile_host_fd((ce_vfile *)e->ptr);
            if (hfd >= 0) {
                rec->fd = dup(hfd);
            } else {
                /* volume files are read through the FAT cursor;
                 * a mapping needs the bytes materialised */
                free(rec);
                ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
                return NULL;
            }
        } else {
            free(rec);
            ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
            return NULL;
        }
        if (rec->size == 0) {
            struct stat st;
            if (fstat(rec->fd, &st) == 0)
                rec->size = (uint64_t)st.st_size;
        }
    }
    if (rec->size == 0) {
        if (rec->fd >= 0)
            close(rec->fd);
        free(rec);
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    CE_HANDLE h = ce_handle_new_ptr(CE_HANDLE_KIND_MAP, rec, 0);
    if (h == CE_INVALID_HANDLE_VALUE) {
        if (rec->fd >= 0)
            close(rec->fd);
        free(rec);
        return NULL;
    }
    return h;
}

void *ce_MapViewOfFile(CE_HANDLE hmap, CE_DWORD access,
                       CE_DWORD off_hi, CE_DWORD off_lo,
                       size_t bytes)
{
    ce_handle_entry *e = ce_handle_get(hmap);
    if (!e || e->kind != CE_HANDLE_KIND_MAP || !e->ptr) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return NULL;
    }
    ce_map_rec *rec = e->ptr;
    uint64_t off = ((uint64_t)off_hi << 32) | off_lo;
    size_t len = bytes ? bytes : (size_t)(rec->size - off);
    int prot = PROT_READ;
    if (access & 0x0002u /* FILE_MAP_WRITE */)
        prot |= PROT_WRITE;
    void *p;
    if (rec->fd >= 0)
        p = mmap(NULL, len, prot, MAP_SHARED, rec->fd,
                 (off_t)off);
    else
        p = mmap(NULL, len, prot | PROT_WRITE,
                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    ce_view *v = malloc(sizeof(*v));
    if (v) {
        v->addr = p;
        v->len = len;
        v->next = g_views;
        g_views = v;
    }
    return p;
}

CE_BOOL ce_UnmapViewOfFile(const void *addr)
{
    ce_view **pp = &g_views;
    while (*pp) {
        if ((*pp)->addr == addr) {
            ce_view *dead = *pp;
            *pp = dead->next;
            munmap((void *)addr, dead->len);
            free(dead);
            return CE_TRUE;
        }
        pp = &(*pp)->next;
    }
    ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
    return CE_FALSE;
}

void ce_m3_close_sync_handle(ce_handle_entry *e)
{
    if (e->kind == CE_HANDLE_KIND_MUTEX && e->ptr) {
        ce_mutex_rec *rec = e->ptr;
        pthread_mutex_destroy(&rec->m);
        free(rec);
    } else if (e->kind == CE_HANDLE_KIND_MAP && e->ptr) {
        ce_map_rec *rec = e->ptr;
        if (rec->fd >= 0)
            close(rec->fd);
        free(rec);
    }
}

/* ---- MsgWaitForMultipleObjectsEx ---- */

CE_BOOL ce_PeekMessageW(CE_MSG *out, CE_HANDLE filter, CE_DWORD mn,
                        CE_DWORD mx, CE_DWORD remove);

static int handle_signaled(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e)
        return 0;
    if (e->kind == CE_HANDLE_KIND_THREAD && e->ptr)
        return ((ce_thread_rec *)e->ptr)->done != 0;
    if (e->kind == CE_HANDLE_KIND_PROCESS && e->ptr) {
        ce_proc_rec *pr = e->ptr;
        proc_poll(pr);
        return pr->done != 0;
    }
    if (e->kind == CE_HANDLE_KIND_MUTEX && e->ptr) {
        ce_mutex_rec *rec = e->ptr;
        if (pthread_mutex_trylock(&rec->m) == 0) {
            pthread_mutex_unlock(&rec->m);
            return 1;
        }
        return 0;
    }
    return 0;
}

CE_DWORD ce_MsgWaitForMultipleObjectsEx(CE_DWORD count,
                                        const CE_HANDLE *handles,
                                        CE_DWORD ms, CE_DWORD wake,
                                        CE_DWORD flags)
{
    (void)wake;
    (void)flags;
    CE_DWORD waited = 0;
    for (;;) {
        for (CE_DWORD i = 0; i < count; i++)
            if (handle_signaled(handles[i]))
                return CE_WAIT_OBJECT_0 + i;
        CE_MSG m;
        if (ce_PeekMessageW(&m, NULL, 0, 0, 0 /* PM_NOREMOVE */))
            return CE_WAIT_OBJECT_0 + count;
        if (ms != CE_INFINITE && waited >= ms)
            return CE_WAIT_TIMEOUT;
        struct timespec pause = { 0, 3000000 };
        nanosleep(&pause, NULL);
        waited += 3;
    }
}

/* ---- StringCch (safe string) family ---- */

CE_LONG ce_StringCchCopyW(CE_WCHAR *dst, size_t cch,
                          const CE_WCHAR *src)
{
    if (!dst || !src || cch == 0)
        return (CE_LONG)(int)0x80070057;   /* E_INVALIDARG */
    size_t i = 0;
    for (; i + 1 < cch && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return src[i] ? (CE_LONG)(int)0x8007007A /* STRSAFE_E_... */
                  : 0;                        /* S_OK */
}

CE_LONG ce_StringCchCopyExW(CE_WCHAR *dst, size_t cch,
                            const CE_WCHAR *src, CE_WCHAR **end,
                            size_t *remain, CE_DWORD flags)
{
    (void)flags;
    CE_LONG r = ce_StringCchCopyW(dst, cch, src);
    if (end)
        *end = dst + ce_wcslen(dst);
    if (remain)
        *remain = cch - ce_wcslen(dst);
    return r;
}

CE_LONG ce_StringCchPrintfW(CE_WCHAR *dst, size_t cch,
                            const CE_WCHAR *fmt, ...)
{
    if (!dst || !fmt || cch == 0)
        return (CE_LONG)(int)0x80070057;
    CE_WCHAR tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    extern int ce_vwsprintfW(CE_WCHAR *, const CE_WCHAR *, va_list);
    int n = ce_vwsprintfW(tmp, fmt, ap);
    va_end(ap);
    (void)n;
    size_t i = 0;
    for (; i + 1 < cch && tmp[i]; i++)
        dst[i] = tmp[i];
    dst[i] = 0;
    return tmp[i] ? (CE_LONG)(int)0x8007007A : 0;
}

CE_LONG ce_StringCchPrintfExW(CE_WCHAR *dst, size_t cch,
                              const CE_WCHAR *fmt, CE_WCHAR **end,
                              size_t *remain, CE_DWORD flags, ...)
{
    (void)flags;
    if (!dst || !fmt || cch == 0)
        return (CE_LONG)(int)0x80070057;
    CE_WCHAR tmp[2048];
    va_list ap;
    va_start(ap, flags);
    extern int ce_vwsprintfW(CE_WCHAR *, const CE_WCHAR *, va_list);
    ce_vwsprintfW(tmp, fmt, ap);
    va_end(ap);
    size_t i = 0;
    for (; i + 1 < cch && tmp[i]; i++)
        dst[i] = tmp[i];
    dst[i] = 0;
    if (end)
        *end = dst + i;
    if (remain)
        *remain = cch - i;
    return tmp[i] ? (CE_LONG)(int)0x8007007A : 0;
}

/* ---- LoadStringW (RT_STRING blocks, M3) ---- */

int ce_LoadStringW(CE_HANDLE hmod, CE_DWORD id, CE_WCHAR *buf,
                   int cch)
{
    /* RT_STRING blocks hold 16 strings; block id = (id >> 4) + 1,
     * each entry is a length word followed by that many UTF-16
     * code units (not NUL-terminated in the resource). */
    CE_HANDLE hrsrc = ce_FindResourceW(
        hmod, (const CE_WCHAR *)(uintptr_t)((id >> 4) + 1),
        (const CE_WCHAR *)(uintptr_t)6 /* RT_STRING */);
    if (!hrsrc) {
        if (buf && cch > 0)
            buf[0] = 0;
        return 0;
    }
    const uint16_t *p = ce_LoadResource(hmod, hrsrc);
    CE_DWORD size = ce_SizeofResource(hmod, hrsrc);
    if (!p || size < 2) {
        if (buf && cch > 0)
            buf[0] = 0;
        return 0;
    }
    const uint16_t *end = p + size / 2;
    unsigned want = id & 15;
    for (unsigned i = 0; i < 16 && p < end; i++) {
        unsigned len = *p++;
        if (p + len > end)
            len = (unsigned)(end - p);
        if (i == want) {
            if (!buf || cch == 0)
                return (int)len;
            int copy = (int)len;
            if (copy > cch - 1)
                copy = cch - 1;
            for (int k = 0; k < copy; k++)
                buf[k] = (CE_WCHAR)p[k];
            buf[copy] = 0;
            return copy;
        }
        p += len;
    }
    if (buf && cch > 0)
        buf[0] = 0;
    return 0;
}
