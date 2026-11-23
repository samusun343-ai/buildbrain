/* BrainCE Layer -- in-memory registry (M3).
 *
 * Hosted DLLs (commctrl at attach time, apps later) read CE
 * registry settings through coredll's Reg* exports. The layer
 * keeps a process-local tree with the documented semantics:
 * predefined root handles, missing keys/values report
 * ERROR_FILE_NOT_FOUND (callers fall back to their defaults,
 * which is the designed CE behaviour on a fresh device), and
 * the two-phase size query of RegQueryValueExW. Persistence
 * and cross-process sharing are M5 concerns; nothing here
 * touches the CE volumes (read-only by S0 design).
 */
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ce_types.h"
#include "fs_bridge.h"   /* ce_set_lasterror, ce_w2u8/u82w helpers */

#define CE_ERROR_SUCCESS          0
#define CE_ERROR_FILE_NOT_FOUND_R 2
#define CE_ERROR_MORE_DATA_R      234
#define CE_ERROR_INVALID_HANDLE_R 6

#define REG_SZ_L     1
#define REG_BINARY_L 3
#define REG_DWORD_L  4

typedef struct ce_regval {
    CE_WCHAR *name;
    CE_DWORD type;
    unsigned char *data;
    CE_DWORD size;
    struct ce_regval *next;
} ce_regval;

typedef struct ce_regkey {
    CE_WCHAR *name;
    struct ce_regkey *subkeys;
    struct ce_regkey *next;
    ce_regval *values;
} ce_regkey;

typedef struct {
    ce_regkey *node;
    int root;
} ce_reghandle;

static ce_regkey g_roots[4];   /* CR, CU, LM, U */
static int g_roots_init;

static void roots_init(void)
{
    if (g_roots_init)
        return;
    g_roots_init = 1;
    static const char *names[4] = {
        "HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER",
        "HKEY_LOCAL_MACHINE", "HKEY_USERS"
    };
    for (int i = 0; i < 4; i++) {
        CE_WCHAR w[32];
        ce_u82w(names[i], w, 32);
        g_roots[i].name = ce_wdup(w);
    }
}

static int root_index(CE_HANDLE h)
{
    switch ((uintptr_t)h) {
    case 0x80000000u: return 0;
    case 0x80000001u: return 1;
    case 0x80000002u: return 2;
    case 0x80000003u: return 3;
    default: return -1;
    }
}

static ce_regkey *find_sub(ce_regkey *k, const CE_WCHAR *name)
{
    for (ce_regkey *s = k->subkeys; s; s = s->next)
        if (ce_wcscasecmp(s->name, name) == 0)
            return s;
    return NULL;
}

static ce_regkey *make_sub(ce_regkey *k, const CE_WCHAR *name)
{
    ce_regkey *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->name = ce_wdup(name);
    s->next = k->subkeys;
    k->subkeys = s;
    return s;
}

/* Walk a backslash path; optionally create missing components. */
static ce_regkey *walk(ce_regkey *from, const CE_WCHAR *path,
                       int create)
{
    ce_regkey *cur = from;
    if (!path)
        return cur;
    CE_WCHAR buf[512];
    size_t n = ce_wcslen(path);
    if (n >= 512)
        return NULL;
    memcpy(buf, path, (n + 1) * sizeof(CE_WCHAR));
    CE_WCHAR *p = buf;
    while (*p) {
        CE_WCHAR *sep = p;
        while (*sep && *sep != '\\')
            sep++;
        CE_WCHAR save = *sep;
        *sep = 0;
        if (*p) {
            ce_regkey *nx = find_sub(cur, p);
            if (!nx) {
                if (!create)
                    return NULL;
                nx = make_sub(cur, p);
                if (!nx)
                    return NULL;
            }
            cur = nx;
        }
        *sep = save;
        p = save ? sep + 1 : sep;
    }
    return cur;
}

static ce_regkey *key_of(CE_HANDLE h, int *is_handle)
{
    int ri = root_index(h);
    if (ri >= 0) {
        *is_handle = 0;
        return &g_roots[ri];
    }
    ce_reghandle *rh = (ce_reghandle *)h;
    if (rh && rh->node) {
        *is_handle = 1;
        return rh->node;
    }
    return NULL;
}

CE_LONG ce_RegOpenKeyExW(CE_HANDLE hkey, const CE_WCHAR *sub,
                         CE_DWORD opts, CE_DWORD sam,
                         CE_HANDLE *out)
{
    (void)opts;
    (void)sam;
    roots_init();
    if (!out)
        return CE_ERROR_INVALID_HANDLE_R;
    int ih;
    ce_regkey *base = key_of(hkey, &ih);
    if (!base)
        return CE_ERROR_INVALID_HANDLE_R;
    ce_regkey *k = walk(base, sub, 0);
    if (!k)
        return CE_ERROR_FILE_NOT_FOUND_R;
    ce_reghandle *rh = calloc(1, sizeof(*rh));
    if (!rh)
        return 8;   /* ERROR_NOT_ENOUGH_MEMORY */
    rh->node = k;
    *out = (CE_HANDLE)rh;
    return CE_ERROR_SUCCESS;
}

CE_LONG ce_RegCreateKeyExW(CE_HANDLE hkey, const CE_WCHAR *sub,
                           CE_DWORD res, CE_WCHAR *cls, CE_DWORD opts,
                           CE_DWORD sam, void *sec,
                           CE_HANDLE *out, CE_DWORD *disp)
{
    (void)res; (void)cls; (void)opts; (void)sam; (void)sec;
    roots_init();
    if (!out)
        return CE_ERROR_INVALID_HANDLE_R;
    int ih;
    ce_regkey *base = key_of(hkey, &ih);
    if (!base)
        return CE_ERROR_INVALID_HANDLE_R;
    ce_regkey *k = walk(base, sub, 1);
    if (!k)
        return 8;
    ce_reghandle *rh = calloc(1, sizeof(*rh));
    if (!rh)
        return 8;
    rh->node = k;
    *out = (CE_HANDLE)rh;
    if (disp)
        *disp = 1;   /* REG_CREATED_NEW_KEY (approximation) */
    return CE_ERROR_SUCCESS;
}

CE_LONG ce_RegCloseKey(CE_HANDLE hkey)
{
    if (root_index(hkey) >= 0)
        return CE_ERROR_SUCCESS;
    ce_reghandle *rh = (ce_reghandle *)hkey;
    if (!rh)
        return CE_ERROR_INVALID_HANDLE_R;
    free(rh);
    return CE_ERROR_SUCCESS;
}

static ce_regval *find_val(ce_regkey *k, const CE_WCHAR *name)
{
    if (!name)
        name = (const CE_WCHAR *)L"";
    for (ce_regval *v = k->values; v; v = v->next)
        if (ce_wcscasecmp(v->name, name) == 0)
            return v;
    return NULL;
}

CE_LONG ce_RegQueryValueExW(CE_HANDLE hkey, const CE_WCHAR *name,
                            CE_DWORD *res, CE_DWORD *type,
                            unsigned char *data, CE_DWORD *cb)
{
    (void)res;
    roots_init();
    int ih;
    ce_regkey *k = key_of(hkey, &ih);
    if (!k)
        return CE_ERROR_INVALID_HANDLE_R;
    ce_regval *v = find_val(k, name);
    if (!v)
        return CE_ERROR_FILE_NOT_FOUND_R;
    if (type)
        *type = v->type;
    if (!cb)
        return CE_ERROR_SUCCESS;
    if (!data) {
        *cb = v->size;
        return CE_ERROR_SUCCESS;
    }
    if (*cb < v->size) {
        *cb = v->size;
        return CE_ERROR_MORE_DATA_R;
    }
    memcpy(data, v->data, v->size);
    *cb = v->size;
    return CE_ERROR_SUCCESS;
}

CE_LONG ce_RegSetValueExW(CE_HANDLE hkey, const CE_WCHAR *name,
                          CE_DWORD res, CE_DWORD type,
                          const unsigned char *data, CE_DWORD cb)
{
    (void)res;
    roots_init();
    int ih;
    ce_regkey *k = key_of(hkey, &ih);
    if (!k)
        return CE_ERROR_INVALID_HANDLE_R;
    ce_regval *v = find_val(k, name);
    if (!v) {
        v = calloc(1, sizeof(*v));
        if (!v)
            return 8;
        v->name = ce_wdup(name ? name : (const CE_WCHAR *)L"");
        v->next = k->values;
        k->values = v;
    }
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->type = type;
    if (cb > 0) {
        v->data = malloc(cb);
        if (!v->data)
            return 8;
        memcpy(v->data, data, cb);
        v->size = cb;
    }
    return CE_ERROR_SUCCESS;
}

CE_LONG ce_RegDeleteValueW(CE_HANDLE hkey, const CE_WCHAR *name)
{
    roots_init();
    int ih;
    ce_regkey *k = key_of(hkey, &ih);
    if (!k)
        return CE_ERROR_INVALID_HANDLE_R;
    if (!name)
        name = (const CE_WCHAR *)L"";
    ce_regval **pp = &k->values;
    while (*pp) {
        if (ce_wcscasecmp((*pp)->name, name) == 0) {
            ce_regval *dead = *pp;
            *pp = dead->next;
            free(dead->name);
            free(dead->data);
            free(dead);
            return CE_ERROR_SUCCESS;
        }
        pp = &(*pp)->next;
    }
    return CE_ERROR_FILE_NOT_FOUND_R;
}
