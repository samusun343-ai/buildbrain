/* BrainCE Layer -- CRT/runtime batch for hosted ROM DLLs (M3).
 *
 * The original DLLs (commctrl, edwgsp, ...) import the CE CRT and
 * the ARM software-runtime helpers from coredll. These are the
 * documented/observable semantics of those exports:
 *  - heap functions on the process heap == the C allocator;
 *  - C++ operator new/delete == malloc/free;
 *  - __addd/__subd/__muld/__divd and __rt_sdiv/__rt_udiv are the
 *    ARM software floating-point / division helpers: plain C
 *    operators under this target's soft-float ABI (arguments and
 *    results travel in the same registers);
 *  - sqrt from libm;
 *  - MulDiv with a 64-bit intermediate (published semantics);
 *  - wide-string CRT pieces the DLLs use.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <wctype.h>
#include <time.h>
#include <sys/time.h>

#include "ce_types.h"
#include "fs_bridge.h"

CE_BOOL ce_DisableThreadLibraryCalls(CE_HANDLE hmod)
{
    (void)hmod;   /* this layer makes no thread attach/detach calls */
    return CE_TRUE;
}

/* ---- heap (process heap == C allocator) ---- */

CE_HANDLE ce_GetProcessHeap(void)
{
    return (CE_HANDLE)(uintptr_t)1;   /* opaque non-NULL token */
}

void *ce_HeapAlloc(CE_HANDLE heap, CE_DWORD flags, size_t size)
{
    (void)heap;
    void *p = malloc(size ? size : 1);
    if (p && (flags & 0x00000008u))   /* HEAP_ZERO_MEMORY */
        memset(p, 0, size);
    return p;
}

CE_BOOL ce_HeapFree(CE_HANDLE heap, CE_DWORD flags, void *p)
{
    (void)heap;
    (void)flags;
    free(p);
    return CE_TRUE;
}

CE_DWORD ce_LocalSize(CE_HANDLE h)
{
    return (CE_DWORD)malloc_usable_size(h);
}

void *ce_malloc(size_t n) { return malloc(n); }
void *ce_realloc(void *p, size_t n) { return realloc(p, n); }
CE_DWORD ce__msize(void *p) { return (CE_DWORD)malloc_usable_size(p); }

/* C++ operators, resolved by ordinal in ce_resolve_import (their
 * decorated names are not valid C identifiers):
 *   ??2@YAPAXI@Z  operator new(size_t)  -> malloc
 *   ??3@YAXPAX@Z  operator delete(void*) -> free            */
void *ce_op_new(size_t n) { return malloc(n ? n : 1); }
void ce_op_delete(void *p) { free(p); }

void ce___report_gsfailure(void)
{
    /* The stack-cookie check failed in the hosted binary. There
     * is no recovery path in CE either: the process dies. */
    fprintf(stderr, "[braince] __report_gsfailure: stack cookie "
            "check failed in hosted code; aborting\n");
    abort();
}

/* ---- ARM software runtime helpers ---- */

double ce___addd(double a, double b) { return a + b; }
double ce___subd(double a, double b) { return a - b; }
double ce___muld(double a, double b) { return a * b; }
double ce___divd(double a, double b) { return a / b; }
int ce___rt_sdiv(int a, int b) { return b ? a / b : 0; }
unsigned ce___rt_udiv(unsigned a, unsigned b) { return b ? a / b : 0; }
double ce_sqrt(double x) { return sqrt(x); }

int ce_MulDiv(int a, int b, int c)
{
    if (c == 0)
        return -1;
    long long r = ((long long)a * (long long)b) / (long long)c;
    if (r > 2147483647LL || r < -2147483647LL - 1)
        return -1;
    return (int)r;
}

/* ---- wide-string CRT ---- */

CE_WCHAR *ce_wcsncpy(CE_WCHAR *d, const CE_WCHAR *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

int ce_wcsncmp(const CE_WCHAR *a, const CE_WCHAR *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}

CE_WCHAR *ce_wcschr(const CE_WCHAR *s, CE_WCHAR c)
{
    for (;; s++) {
        if (*s == c)
            return (CE_WCHAR *)s;
        if (!*s)
            return NULL;
    }
}

static CE_WCHAR fold_up(CE_WCHAR c)
{
    if (c >= 'a' && c <= 'z')
        return (CE_WCHAR)(c - 'a' + 'A');
    return (CE_WCHAR)towupper((wint_t)c);
}

int ce__wcsicmp(const CE_WCHAR *a, const CE_WCHAR *b)
{
    for (;; a++, b++) {
        CE_WCHAR ca = fold_up(*a), cb = fold_up(*b);
        if (ca != cb)
            return (int)ca - (int)cb;
        if (!ca)
            return 0;
    }
}

int ce__wcsnicmp(const CE_WCHAR *a, const CE_WCHAR *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        CE_WCHAR ca = fold_up(a[i]), cb = fold_up(b[i]);
        if (ca != cb)
            return (int)ca - (int)cb;
        if (!ca)
            return 0;
    }
    return 0;
}

CE_LONG ce__wtol(const CE_WCHAR *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    int neg = 0;
    if (*s == '-' || *s == '+') {
        neg = (*s == '-');
        s++;
    }
    long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return (CE_LONG)(neg ? -v : v);
}

int ce_iswctype(CE_WCHAR c, CE_DWORD type)
{
    /* wctype_t values on CE mirror the CRT _ctype flags; map the
     * common classes through the host libc. */
    switch (type) {
    case 0x01: return iswupper((wint_t)c) != 0;   /* _UPPER */
    case 0x02: return iswlower((wint_t)c) != 0;   /* _LOWER */
    case 0x04: return iswdigit((wint_t)c) != 0;   /* _DIGIT */
    case 0x08: return iswspace((wint_t)c) != 0;   /* _SPACE */
    case 0x10: return iswpunct((wint_t)c) != 0;   /* _PUNCT */
    case 0x20: return iswcntrl((wint_t)c) != 0;   /* _CONTROL */
    case 0x40: return iswspace((wint_t)c) != 0;   /* _BLANK approx */
    case 0x80: return iswxdigit((wint_t)c) != 0;  /* _HEX */
    default:
        if (type & 0x100)
            return iswalpha((wint_t)c) != 0;
        return 0;
    }
}

/* ---- character helpers ---- */

CE_WCHAR *ce_CharNextW(const CE_WCHAR *s)
{
    return (CE_WCHAR *)(s + (s && *s ? 1 : 0));
}

CE_DWORD ce_CharUpperW(CE_WCHAR *s)
{
    if ((uintptr_t)s <= 0xFFFF) {
        return (CE_DWORD)(uintptr_t)fold_up((CE_WCHAR)(uintptr_t)s);
    }
    for (CE_WCHAR *p = s; *p; p++)
        *p = fold_up(*p);
    return (CE_DWORD)(uintptr_t)s;
}

/* ---- process version / time ---- */

CE_DWORD ce_GetProcessVersion(CE_DWORD pid)
{
    (void)pid;
    /* The hosted images are CE 6.0 subsystem binaries. */
    return (6u << 16) | 0u;
}

typedef unsigned short ce_word;
typedef struct {
    ce_word wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute,
            wSecond, wMilliseconds;
} ce_systemtime;

void ce_GetLocalTime(ce_systemtime *st)
{
    if (!st)
        return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tmv;
    localtime_r(&tv.tv_sec, &tmv);
    st->wYear = (ce_word)(tmv.tm_year + 1900);
    st->wMonth = (ce_word)(tmv.tm_mon + 1);
    st->wDayOfWeek = (ce_word)tmv.tm_wday;
    st->wDay = (ce_word)tmv.tm_mday;
    st->wHour = (ce_word)tmv.tm_hour;
    st->wMinute = (ce_word)tmv.tm_min;
    st->wSecond = (ce_word)tmv.tm_sec;
    st->wMilliseconds = (ce_word)(tv.tv_usec / 1000);
}

CE_BOOL ce_SystemTimeToFileTime(const ce_systemtime *st,
                                CE_FILETIME *ft)
{
    if (!st || !ft)
        return CE_FALSE;
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = st->wYear - 1900;
    tmv.tm_mon = st->wMonth - 1;
    tmv.tm_mday = st->wDay;
    tmv.tm_hour = st->wHour;
    tmv.tm_min = st->wMinute;
    tmv.tm_sec = st->wSecond;
    tmv.tm_isdst = -1;
    time_t t = mktime(&tmv);
    if (t == (time_t)-1)
        return CE_FALSE;
    uint64_t v = ((uint64_t)t + 11644473600ULL) * 10000000ULL
                 + (uint64_t)st->wMilliseconds * 10000ULL;
    ft->dwLowDateTime = (CE_DWORD)(v & 0xFFFFFFFFu);
    ft->dwHighDateTime = (CE_DWORD)(v >> 32);
    return CE_TRUE;
}
