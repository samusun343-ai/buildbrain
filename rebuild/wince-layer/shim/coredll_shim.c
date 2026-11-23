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
#include <sys/stat.h>
#include <sys/mman.h>
#include <limits.h>

#include "ce_types.h"
#include "fs_bridge.h"
#include "shim_table.h"

__thread CE_DWORD ce_tls_lasterror = 0;

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
    int fd = open(path, oflags, 0666);
    if (fd < 0) {
        ce_set_lasterror(ce_map_errno(errno, 1));
        return CE_INVALID_HANDLE_VALUE;
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        /* M1: directory handles are not supported. */
        close(fd);
        ce_set_lasterror(CE_ERROR_ACCESS_DENIED);
        return CE_INVALID_HANDLE_VALUE;
    }
    return ce_handle_new_file(fd);
}

static ce_handle_entry *file_entry(CE_HANDLE h)
{
    ce_handle_entry *e = ce_handle_get(h);
    if (!e || e->kind != CE_HANDLE_KIND_FILE) {
        ce_set_lasterror(CE_ERROR_INVALID_HANDLE);
        return NULL;
    }
    return e;
}

CE_BOOL ce_ReadFile(CE_HANDLE h, void *buf, CE_DWORD want,
                    CE_DWORD *got, void *overlapped)
{
    ce_handle_entry *e = file_entry(h);
    if (!e)
        return CE_FALSE;
    if (overlapped) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    ssize_t r = read(e->fd, buf, want);
    if (r < 0) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_FALSE;
    }
    if (got)
        *got = (CE_DWORD)r;
    return CE_TRUE;
}

CE_BOOL ce_WriteFile(CE_HANDLE h, const void *buf, CE_DWORD want,
                     CE_DWORD *put, void *overlapped)
{
    ce_handle_entry *e = file_entry(h);
    if (!e)
        return CE_FALSE;
    if (overlapped) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    const char *p = (const char *)buf;
    CE_DWORD done = 0;
    while (done < want) {
        ssize_t w = write(e->fd, p + done, want - done);
        if (w < 0) {
            ce_set_lasterror(ce_map_errno(errno, 0));
            if (put)
                *put = done;
            return CE_FALSE;
        }
        done += (CE_DWORD)w;
    }
    if (put)
        *put = done;
    return CE_TRUE;
}

CE_DWORD ce_SetFilePointer(CE_HANDLE h, CE_LONG dist,
                           CE_LONG *dist_high, CE_DWORD method)
{
    ce_handle_entry *e = file_entry(h);
    if (!e)
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
    off_t r = lseek(e->fd, (off_t)off, whence);
    if (r == (off_t)-1) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_INVALID_SET_FILE_POINTER;
    }
    if (dist_high)
        *dist_high = (CE_LONG)((uint64_t)r >> 32);
    ce_set_lasterror(CE_ERROR_SUCCESS);
    return (CE_DWORD)((uint64_t)r & 0xFFFFFFFFu);
}

CE_DWORD ce_GetFileSize(CE_HANDLE h, CE_DWORD *high)
{
    ce_handle_entry *e = file_entry(h);
    if (!e)
        return (CE_DWORD)-1;
    struct stat st;
    if (fstat(e->fd, &st) != 0) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return (CE_DWORD)-1;
    }
    if (high)
        *high = (CE_DWORD)((uint64_t)st.st_size >> 32);
    ce_set_lasterror(CE_ERROR_SUCCESS);
    return (CE_DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
}

CE_BOOL ce_FlushFileBuffers(CE_HANDLE h)
{
    ce_handle_entry *e = file_entry(h);
    if (!e)
        return CE_FALSE;
    if (fsync(e->fd) != 0) {
        ce_set_lasterror(ce_map_errno(errno, 0));
        return CE_FALSE;
    }
    return CE_TRUE;
}

struct ce_find_state {
    DIR *dir;
    char pattern[260];
    char dirpath[PATH_MAX];
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
        free(fs);
        ce_set_lasterror(ce_map_errno(errno, 1));
        return CE_INVALID_HANDLE_VALUE;
    }
    if (ce_find_next_match(fs, fd) != 0) {
        closedir(fs->dir);
        free(fs);
        ce_set_lasterror(CE_ERROR_FILE_NOT_FOUND);
        return CE_INVALID_HANDLE_VALUE;
    }
    CE_HANDLE h = ce_handle_new_find(fs);
    if (h == CE_INVALID_HANDLE_VALUE) {
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
    if (e->kind == CE_HANDLE_KIND_FIND)
        return ce_FindClose(h);
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

/* ================= module identity (M2) =================
 * The loader registers the main image so GetModuleHandleW(NULL) and
 * GetModuleFileNameW behave the way CE apps expect. No other module
 * exists in the M2 process model, so a named lookup fails honestly. */

static void *g_main_image_base;
static CE_WCHAR g_main_image_path[260];

void ce_set_main_image(void *base, const CE_WCHAR *path)
{
    g_main_image_base = base;
    if (path) {
        size_t i = 0;
        while (path[i] && i < 259) {
            g_main_image_path[i] = path[i];
            i++;
        }
        g_main_image_path[i] = 0;
    }
}

CE_HANDLE ce_GetModuleHandleW(const CE_WCHAR *name)
{
    if (!name)
        return (CE_HANDLE)g_main_image_base;
    ce_set_lasterror(CE_ERROR_MOD_NOT_FOUND);
    return NULL;
}

CE_DWORD ce_GetModuleFileNameW(CE_HANDLE hmod, CE_WCHAR *buf, CE_DWORD max)
{
    if (hmod && hmod != (CE_HANDLE)g_main_image_base) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!buf || max == 0)
        return 0;
    size_t n = ce_wcslen(g_main_image_path);
    size_t c = n < (size_t)max - 1 ? n : (size_t)max - 1;
    memcpy(buf, g_main_image_path, c * sizeof(CE_WCHAR));
    buf[c] = 0;
    return (CE_DWORD)c;
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
int ce_wsprintfW(CE_WCHAR *out, const CE_WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
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
    va_end(ap);
    return (int)(o - out);
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
