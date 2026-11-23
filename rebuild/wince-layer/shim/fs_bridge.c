/* BrainCE Layer -- FS bridge v0 implementation. See fs_bridge.h. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>

#include "fs_bridge.h"

static char g_root[PATH_MAX];
static int  g_root_ready = 0;

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
    return 0;
}
