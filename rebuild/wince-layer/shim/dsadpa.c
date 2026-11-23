/* BrainCE Layer -- DSA/DPA dynamic arrays (M3).
 *
 * commctrl.dll (and other hosted DLLs) build their internal
 * state on coredll's DSA (fixed-size item array) and DPA
 * (pointer array) exports. These are the publicly documented
 * comctl32-family semantics:
 *  - items are stored contiguously, the array grows by a
 *    create-time increment;
 *  - SetItem past the end grows the array, zero-filling the gap;
 *  - InsertItem at DA_LAST/DPA_APPEND appends;
 *  - Search without DPAS_SORTED is a linear scan through the
 *    caller's compare callback (0 == match);
 *  - Clone produces an independent array with the same items
 *    (the CE implementation shares storage copy-on-write; a
 *    deep copy is observably equivalent for callers).
 * Handles are the structs themselves, as on CE.
 */
#include <stdlib.h>
#include <string.h>

#include "ce_types.h"

#define DA_LAST_I 0x7FFFFFFF

typedef int (*ce_pfn_dacompare)(void *, void *, CE_LRESULT);
typedef int (*ce_pfn_enumcb)(void *, void *);

/* ---------------- DSA ---------------- */

typedef struct {
    int item_size;
    int count;
    int alloc;
    int grow;
    unsigned char *data;
} ce_dsa;

static int dsa_reserve(ce_dsa *d, int need)
{
    if (need <= d->alloc)
        return 1;
    int na = d->alloc ? d->alloc : 4;
    while (na < need)
        na += d->grow > 0 ? d->grow : 4;
    unsigned char *nd = realloc(d->data,
                                (size_t)na * (size_t)d->item_size);
    if (!nd)
        return 0;
    d->data = nd;
    d->alloc = na;
    return 1;
}

void *ce_DSA_Create(int item_size, int grow)
{
    if (item_size <= 0)
        return NULL;
    ce_dsa *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->item_size = item_size;
    d->grow = grow;
    return d;
}

CE_BOOL ce_DSA_Destroy(void *h)
{
    ce_dsa *d = h;
    if (!d)
        return CE_FALSE;
    free(d->data);
    free(d);
    return CE_TRUE;
}

void *ce_DSA_GetItemPtr(void *h, int i)
{
    ce_dsa *d = h;
    if (!d || i < 0 || i >= d->count)
        return NULL;
    return d->data + (size_t)i * (size_t)d->item_size;
}

CE_BOOL ce_DSA_GetItem(void *h, int i, void *out)
{
    void *p = ce_DSA_GetItemPtr(h, i);
    if (!p || !out)
        return CE_FALSE;
    memcpy(out, p, (size_t)((ce_dsa *)h)->item_size);
    return CE_TRUE;
}

CE_BOOL ce_DSA_SetItem(void *h, int i, const void *item)
{
    ce_dsa *d = h;
    if (!d || i < 0)
        return CE_FALSE;
    if (i >= d->count) {
        if (!dsa_reserve(d, i + 1))
            return CE_FALSE;
        memset(d->data + (size_t)d->count * (size_t)d->item_size,
               0, (size_t)(i + 1 - d->count) * (size_t)d->item_size);
        d->count = i + 1;
    }
    if (item)
        memcpy(d->data + (size_t)i * (size_t)d->item_size, item,
               (size_t)d->item_size);
    return CE_TRUE;
}

int ce_DSA_InsertItem(void *h, int i, const void *item)
{
    ce_dsa *d = h;
    if (!d)
        return -1;
    if (i < 0 || i > d->count || i == DA_LAST_I)
        i = d->count;
    if (!dsa_reserve(d, d->count + 1))
        return -1;
    memmove(d->data + (size_t)(i + 1) * (size_t)d->item_size,
            d->data + (size_t)i * (size_t)d->item_size,
            (size_t)(d->count - i) * (size_t)d->item_size);
    if (item)
        memcpy(d->data + (size_t)i * (size_t)d->item_size, item,
               (size_t)d->item_size);
    else
        memset(d->data + (size_t)i * (size_t)d->item_size, 0,
               (size_t)d->item_size);
    d->count++;
    return i;
}

CE_BOOL ce_DSA_DeleteItem(void *h, int i)
{
    ce_dsa *d = h;
    if (!d || i < 0 || i >= d->count)
        return CE_FALSE;
    memmove(d->data + (size_t)i * (size_t)d->item_size,
            d->data + (size_t)(i + 1) * (size_t)d->item_size,
            (size_t)(d->count - i - 1) * (size_t)d->item_size);
    d->count--;
    return CE_TRUE;
}

CE_BOOL ce_DSA_DeleteAllItems(void *h)
{
    ce_dsa *d = h;
    if (!d)
        return CE_FALSE;
    d->count = 0;
    return CE_TRUE;
}

void *ce_DSA_Clone(void *h)
{
    ce_dsa *d = h;
    if (!d)
        return NULL;
    ce_dsa *n = ce_DSA_Create(d->item_size, d->grow);
    if (!n)
        return NULL;
    if (d->count > 0) {
        if (!dsa_reserve(n, d->count)) {
            ce_DSA_Destroy(n);
            return NULL;
        }
        memcpy(n->data, d->data,
               (size_t)d->count * (size_t)d->item_size);
        n->count = d->count;
    }
    return n;
}

int ce_DSA_Search(void *h, const void *find, int start,
                  ce_pfn_dacompare pfn, CE_LRESULT lp, CE_DWORD opts)
{
    ce_dsa *d = h;
    if (!d || !pfn)
        return -1;
    (void)opts;   /* linear scan covers the sorted contract for
                   * the small arrays controls keep */
    if (start < 0)
        start = 0;
    for (int i = start; i < d->count; i++) {
        void *it = d->data + (size_t)i * (size_t)d->item_size;
        if (pfn(it, (void *)find, lp) == 0)
            return i;
    }
    return -1;
}

void ce_DSA_EnumCallback(void *h, ce_pfn_enumcb pfn, void *data)
{
    ce_dsa *d = h;
    if (!d || !pfn)
        return;
    for (int i = 0; i < d->count; i++) {
        void *it = d->data + (size_t)i * (size_t)d->item_size;
        if (pfn(it, data) == 0)
            break;
    }
}

CE_BOOL ce_DSA_DestroyCallback(void *h, ce_pfn_enumcb pfn, void *data)
{
    ce_DSA_EnumCallback(h, pfn, data);
    return ce_DSA_Destroy(h);
}

/* ---------------- DPA ---------------- */

typedef struct {
    void **items;
    int count;
    int alloc;
    int grow;
} ce_dpa;

static int dpa_reserve(ce_dpa *d, int need)
{
    if (need <= d->alloc)
        return 1;
    int na = d->alloc ? d->alloc : 4;
    while (na < need)
        na += d->grow > 0 ? d->grow : 4;
    void **nd = realloc(d->items, (size_t)na * sizeof(void *));
    if (!nd)
        return 0;
    d->items = nd;
    d->alloc = na;
    return 1;
}

void *ce_DPA_Create(int grow)
{
    ce_dpa *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->grow = grow;
    return d;
}

CE_BOOL ce_DPA_Destroy(void *h)
{
    ce_dpa *d = h;
    if (!d)
        return CE_FALSE;
    free(d->items);
    free(d);
    return CE_TRUE;
}

CE_BOOL ce_DPA_DeleteAllPtrs(void *h)
{
    ce_dpa *d = h;
    if (!d)
        return CE_FALSE;
    d->count = 0;
    return CE_TRUE;
}

void *ce_DPA_GetPtr(void *h, int i)
{
    ce_dpa *d = h;
    if (!d || i < 0 || i >= d->count)
        return NULL;
    return d->items[i];
}

CE_BOOL ce_DPA_SetPtr(void *h, int i, void *p)
{
    ce_dpa *d = h;
    if (!d || i < 0)
        return CE_FALSE;
    if (i >= d->count) {
        if (!dpa_reserve(d, i + 1))
            return CE_FALSE;
        for (int k = d->count; k <= i; k++)
            d->items[k] = NULL;
        d->count = i + 1;
    }
    d->items[i] = p;
    return CE_TRUE;
}

int ce_DPA_InsertPtr(void *h, int i, void *p)
{
    ce_dpa *d = h;
    if (!d)
        return -1;
    if (i < 0 || i > d->count || i == DA_LAST_I)
        i = d->count;
    if (!dpa_reserve(d, d->count + 1))
        return -1;
    memmove(&d->items[i + 1], &d->items[i],
            (size_t)(d->count - i) * sizeof(void *));
    d->items[i] = p;
    d->count++;
    return i;
}

CE_BOOL ce_DPA_Grow(void *h, int count)
{
    ce_dpa *d = h;
    if (!d)
        return CE_FALSE;
    return dpa_reserve(d, count) ? CE_TRUE : CE_FALSE;
}

static ce_pfn_dacompare g_sort_pfn;
static CE_LRESULT g_sort_lp;

static int dpa_qsort_cmp(const void *a, const void *b)
{
    return g_sort_pfn(*(void *const *)a, *(void *const *)b,
                      g_sort_lp);
}

CE_BOOL ce_DPA_Sort(void *h, ce_pfn_dacompare pfn, CE_LRESULT lp)
{
    ce_dpa *d = h;
    if (!d || !pfn)
        return CE_FALSE;
    g_sort_pfn = pfn;
    g_sort_lp = lp;
    if (d->count > 1)
        qsort(d->items, (size_t)d->count, sizeof(void *),
              dpa_qsort_cmp);
    return CE_TRUE;
}

int ce_DPA_Search(void *h, const void *find, int start,
                  ce_pfn_dacompare pfn, CE_LRESULT lp, CE_DWORD opts)
{
    ce_dpa *d = h;
    if (!d || !pfn)
        return -1;
    (void)opts;
    if (start < 0)
        start = 0;
    for (int i = start; i < d->count; i++)
        if (pfn(d->items[i], (void *)find, lp) == 0)
            return i;
    return -1;
}

void ce_DPA_EnumCallback(void *h, ce_pfn_enumcb pfn, void *data)
{
    ce_dpa *d = h;
    if (!d || !pfn)
        return;
    for (int i = 0; i < d->count; i++)
        if (pfn(d->items[i], data) == 0)
            break;
}

CE_BOOL ce_DPA_DestroyCallback(void *h, ce_pfn_enumcb pfn, void *data)
{
    ce_DPA_EnumCallback(h, pfn, data);
    return ce_DPA_Destroy(h);
}
