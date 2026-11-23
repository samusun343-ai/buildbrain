/* BrainCE Layer -- locale / NLS batch (M3).
 *
 * Hosted DLLs (commctrl's number/date controls in particular)
 * query coredll's NLS exports. The device is a Japanese PW-SH6,
 * so the defaults are ja-JP (LCID 0x0411, codepage 932) -- the
 * same answers the CE image itself gives. Coverage:
 *  - GetLocaleInfoW: a table for the common LCTYPEs;
 *  - GetNumberFormatW / GetDateFormatW / GetTimeFormatW: real
 *    formatting for the documented picture tokens;
 *  - LCMapStringW: upper/lower case and width mapping;
 *  - GetStringTypeExW: CT_CTYPE1 classes via the host libc;
 *  - WideCharToMultiByte / MultiByteToWideChar: UTF-8 natively,
 *    CP932 (the CE ANSI codepage here) through iconv.
 * Unknown LCTYPEs fail with ERROR_INVALID_PARAMETER rather than
 * inventing data.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <iconv.h>
#include <errno.h>

#include "ce_types.h"
#include "fs_bridge.h"

#define LCID_JP 0x0411u

CE_DWORD ce_GetUserDefaultLCID(void) { return LCID_JP; }
CE_DWORD ce_GetSystemDefaultLCID(void) { return LCID_JP; }

typedef unsigned short lw;
lw ce_GetSystemDefaultLangID(void) { return (lw)LCID_JP; }

/* ---------------- GetLocaleInfoW ---------------- */

struct lcent {
    CE_DWORD type;
    const char *val;
};

static const struct lcent g_lctab[] = {
    { 0x0001, "0411" },            /* LOCALE_ILANGUAGE */
    { 0x0002, "Japanese" },        /* LOCALE_SLANGUAGE */
    { 0x0003, "JPN" },             /* LOCALE_SABBREVLANGNAME */
    { 0x0005, "81" },              /* LOCALE_ICOUNTRY */
    { 0x0006, "Japan" },           /* LOCALE_SCOUNTRY */
    { 0x0007, "JPN" },             /* LOCALE_SABBREVCTRYNAME */
    { 0x0009, "0411" },            /* LOCALE_IDEFAULTLANGUAGE */
    { 0x000A, "81" },              /* LOCALE_IDEFAULTCOUNTRY */
    { 0x000B, "932" },             /* LOCALE_IDEFAULTCODEPAGE */
    { 0x000C, "932" },             /* LOCALE_IDEFAULTANSICODEPAGE */
    { 0x000D, "0" },               /* LOCALE_IMEASURE (metric) */
    { 0x000E, "." },               /* LOCALE_SDECIMAL */
    { 0x000F, "," },               /* LOCALE_STHOUSAND */
    { 0x0010, "3;0" },             /* LOCALE_SGROUPING */
    { 0x0011, "2" },               /* LOCALE_IDIGITS */
    { 0x0012, "1" },               /* LOCALE_ILZERO */
    { 0x0013, "0123456789" },      /* LOCALE_SNATIVEDIGITS */
    { 0x0014, "1" },               /* LOCALE_INEGNUMBER */
    { 0x0015, "\xC2\xA5" },        /* LOCALE_SCURRENCY (yen) */
    { 0x0018, "0" },               /* LOCALE_ICURRENCY */
    { 0x0019, "0" },               /* LOCALE_ICURRDIGITS (JPY) */
    { 0x001A, "." },               /* LOCALE_SMONDECIMALSEP */
    { 0x001B, "," },               /* LOCALE_SMONTHOUSANDSEP */
    { 0x001C, "1" },               /* LOCALE_INEGCURR */
    { 0x001D, "/" },               /* LOCALE_SDATE */
    { 0x001E, ":" },               /* LOCALE_STIME */
    { 0x001F, "yyyy/MM/dd" },      /* LOCALE_SSHORTDATE */
    { 0x0020, "yyyy/MM/dd" },      /* LOCALE_SLONGDATE */
    { 0x0021, "2" },               /* LOCALE_IDATE (ymd) */
    { 0x0022, "2" },               /* LOCALE_ILDATE */
    { 0x0023, "0" },               /* LOCALE_ITIME (24h) */
    { 0x0024, "1" },               /* LOCALE_ICENTURY */
    { 0x0025, "1" },               /* LOCALE_ITLZERO */
    { 0x0026, "0" },               /* LOCALE_IDAYLZERO */
    { 0x0027, "0" },               /* LOCALE_IMONLZERO */
    { 0x0028, "" },                /* LOCALE_S1159 */
    { 0x0029, "" },                /* LOCALE_S2359 */
    { 0x0050, "" },                /* LOCALE_SPOSITIVESIGN */
    { 0x0051, "-" },               /* LOCALE_SNEGATIVESIGN */
    { 0x1003, "H:mm:ss" },         /* LOCALE_STIMEFORMAT */
    { 0x1009, "1" },               /* LOCALE_ICALENDARTYPE */
    { 0x100C, "6" },               /* LOCALE_IFIRSTDAYOFWEEK */
    { 0x100D, "0" },               /* LOCALE_IFIRSTWEEKOFYEAR */
    { 0, NULL }
};

int ce_GetLocaleInfoW(CE_DWORD lcid, CE_DWORD type, CE_WCHAR *buf,
                      int cch)
{
    (void)lcid;
    CE_DWORD t = type & 0xFFFFu;
    const char *val = NULL;
    /* Note: 0x000C is both LOCALE_SLIST and
     * LOCALE_IDEFAULTANSICODEPAGE in different header
     * generations; this table answers the codepage (932),
     * which is what hosted DLLs branch on. */
    for (int i = 0; g_lctab[i].val; i++)
        if (g_lctab[i].type == t) {
            val = g_lctab[i].val;
            break;
        }
    if (!val) {
        ce_set_lasterror(87);   /* ERROR_INVALID_PARAMETER */
        return 0;
    }
    CE_WCHAR w[64];
    int need = ce_u82w(val, w, 64) + 1;
    if (!buf || cch == 0)
        return need;
    if (cch < need) {
        ce_set_lasterror(122);  /* ERROR_INSUFFICIENT_BUFFER */
        return 0;
    }
    memcpy(buf, w, (size_t)need * sizeof(CE_WCHAR));
    return need;
}

/* ---------------- number / date / time formatting --------- */

typedef struct {
    CE_DWORD NumDigits;
    CE_DWORD LeadingZero;
    CE_DWORD Grouping;
    CE_WCHAR *lpDecimalSep;
    CE_WCHAR *lpThousandSep;
    CE_DWORD NegativeOrder;
    CE_DWORD PositiveOrder;
} ce_numberfmt;

int ce_GetNumberFormatW(CE_DWORD lcid, CE_DWORD flags,
                        const CE_WCHAR *value, ce_numberfmt *fmt,
                        CE_WCHAR *out, int cch)
{
    (void)lcid;
    (void)flags;
    if (!value)
        return 0;
    char u8[128];
    ce_w2u8(value, u8, sizeof(u8));
    double d = strtod(u8, NULL);
    int ndig = fmt ? (int)fmt->NumDigits : 2;
    if (ndig < 0)
        ndig = 0;
    if (ndig > 12)
        ndig = 12;
    char num[160];
    snprintf(num, sizeof(num), "%.*f", ndig, d);
    /* group the integer part in threes (Grouping 3;0 default) */
    char grouped[200];
    char *dot = strchr(num, '.');
    int intlen = dot ? (int)(dot - num) : (int)strlen(num);
    int neg = (num[0] == '-');
    int first = neg ? 1 : 0;
    int digits = intlen - first;
    int gi = 0;
    if (neg)
        grouped[gi++] = '-';
    for (int i = 0; i < digits; i++) {
        if (i > 0 && ((digits - i) % 3) == 0)
            grouped[gi++] = ',';
        grouped[gi++] = num[first + i];
    }
    if (dot) {
        grouped[gi++] = '.';
        strcpy(grouped + gi, dot + 1);
        gi += (int)strlen(dot + 1);
    }
    grouped[gi] = 0;
    /* honour caller separators (single-char ones) */
    if (fmt && fmt->lpThousandSep && fmt->lpThousandSep[0]) {
        char ts[8];
        ce_w2u8(fmt->lpThousandSep, ts, sizeof(ts));
        if (ts[0] && !ts[1])
            for (int i = 0; grouped[i]; i++)
                if (grouped[i] == ',')
                    grouped[i] = ts[0];
    }
    if (fmt && fmt->lpDecimalSep && fmt->lpDecimalSep[0]) {
        char ds[8];
        ce_w2u8(fmt->lpDecimalSep, ds, sizeof(ds));
        if (ds[0] && !ds[1])
            for (int i = 0; grouped[i]; i++)
                if (grouped[i] == '.')
                    grouped[i] = ds[0];
    }
    CE_WCHAR w[220];
    int need = ce_u82w(grouped, w, 220) + 1;
    if (!out || cch == 0)
        return need;
    if (cch < need)
        return 0;
    memcpy(out, w, (size_t)need * sizeof(CE_WCHAR));
    return need - 1;
}

typedef struct {
    lw wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond,
       wMilliseconds;
} ce_st;

static int fmt_picture(const ce_st *st, const char *pic,
                       CE_WCHAR *out, int cch)
{
    char buf[256];
    int bi = 0;
    for (int i = 0; pic[i] && bi < 240; i++) {
        if (pic[i] == '\'') {
            i++;
            while (pic[i] && pic[i] != '\'')
                buf[bi++] = pic[i++];
            if (!pic[i])
                break;
            continue;
        }
        int run = 1;
        while (pic[i + run] == pic[i])
            run++;
        char tok[8];
        if (run > 4)
            run = 4;
        memcpy(tok, pic + i, (size_t)run);
        tok[run] = 0;
        char num[16];
        const char *rep = NULL;
        if (!strcmp(tok, "yyyy")) {
            snprintf(num, sizeof(num), "%04d", st->wYear);
            rep = num;
        } else if (!strcmp(tok, "yy")) {
            snprintf(num, sizeof(num), "%02d", st->wYear % 100);
            rep = num;
        } else if (!strcmp(tok, "MM") || !strcmp(tok, "M")) {
            snprintf(num, sizeof(num),
                     tok[1] ? "%02d" : "%d", st->wMonth);
            rep = num;
        } else if (!strcmp(tok, "dd") || !strcmp(tok, "d")) {
            snprintf(num, sizeof(num),
                     tok[1] ? "%02d" : "%d", st->wDay);
            rep = num;
        } else if (!strcmp(tok, "HH") || !strcmp(tok, "H")) {
            snprintf(num, sizeof(num),
                     tok[1] ? "%02d" : "%d", st->wHour);
            rep = num;
        } else if (!strcmp(tok, "hh") || !strcmp(tok, "h")) {
            int h12 = st->wHour % 12;
            if (h12 == 0)
                h12 = 12;
            snprintf(num, sizeof(num), tok[1] ? "%02d" : "%d", h12);
            rep = num;
        } else if (!strcmp(tok, "mm") || !strcmp(tok, "m")) {
            snprintf(num, sizeof(num),
                     tok[1] ? "%02d" : "%d", st->wMinute);
            rep = num;
        } else if (!strcmp(tok, "ss") || !strcmp(tok, "s")) {
            snprintf(num, sizeof(num),
                     tok[1] ? "%02d" : "%d", st->wSecond);
            rep = num;
        } else if (!strcmp(tok, "tt")) {
            rep = st->wHour < 12 ? "AM" : "PM";
        }
        if (rep) {
            int rl = (int)strlen(rep);
            memcpy(buf + bi, rep, (size_t)rl);
            bi += rl;
            i += run - 1;
        } else {
            buf[bi++] = pic[i];
        }
    }
    buf[bi] = 0;
    CE_WCHAR w[260];
    int need = ce_u82w(buf, w, 260) + 1;
    if (!out || cch == 0)
        return need;
    if (cch < need)
        return 0;
    memcpy(out, w, (size_t)need * sizeof(CE_WCHAR));
    return need - 1;
}

int ce_GetDateFormatW(CE_DWORD lcid, CE_DWORD flags,
                      const ce_st *st, const CE_WCHAR *fmt,
                      CE_WCHAR *out, int cch)
{
    (void)lcid;
    (void)flags;
    if (!st)
        return 0;
    char pic[128];
    if (fmt)
        ce_w2u8(fmt, pic, sizeof(pic));
    else
        strcpy(pic, "yyyy/MM/dd");
    return fmt_picture(st, pic, out, cch);
}

int ce_GetTimeFormatW(CE_DWORD lcid, CE_DWORD flags,
                      const ce_st *st, const CE_WCHAR *fmt,
                      CE_WCHAR *out, int cch)
{
    (void)lcid;
    (void)flags;
    if (!st)
        return 0;
    char pic[128];
    if (fmt)
        ce_w2u8(fmt, pic, sizeof(pic));
    else
        strcpy(pic, "H:mm:ss");
    return fmt_picture(st, pic, out, cch);
}

/* ---------------- LCMapStringW / GetStringTypeExW --------- */

int ce_LCMapStringW(CE_DWORD lcid, CE_DWORD flags,
                    const CE_WCHAR *src, int cch_src,
                    CE_WCHAR *dst, int cch_dst)
{
    (void)lcid;
    if (!src)
        return 0;
    int n = cch_src < 0 ? (int)ce_wcslen(src) + 1 : cch_src;
    int term = cch_src < 0;
    CE_WCHAR tmp[1024];
    if (n > 1024)
        return 0;
    for (int i = 0; i < n; i++) {
        CE_WCHAR c = src[i];
        if (flags & 0x00000100u)        /* LCMAP_LOWERCASE */
            c = (CE_WCHAR)towlower((wint_t)c);
        else if (flags & 0x00000200u)   /* LCMAP_UPPERCASE */
            c = (CE_WCHAR)towupper((wint_t)c);
        if (flags & 0x00000400u) {      /* LCMAP_FULLWIDTH */
            if (c >= 0x21 && c <= 0x7E)
                c = (CE_WCHAR)(c - 0x20 + 0xFF00);
            else if (c == 0x20)
                c = 0x3000;
        }
        if (flags & 0x00000800u) {      /* LCMAP_HALFWIDTH */
            if (c >= 0xFF01 && c <= 0xFF5E)
                c = (CE_WCHAR)(c - 0xFF00 + 0x20);
            else if (c == 0x3000)
                c = 0x20;
        }
        tmp[i] = c;
    }
    int out_n = term ? n - 1 : n;
    if (!dst || cch_dst == 0)
        return term ? n : out_n;
    if (cch_dst < (term ? n : out_n))
        return 0;
    memcpy(dst, tmp, (size_t)(term ? n : out_n) * sizeof(CE_WCHAR));
    return out_n;
}

CE_BOOL ce_GetStringTypeExW(CE_DWORD lcid, CE_DWORD infotype,
                            const CE_WCHAR *src, int cch,
                            lw *out)
{
    (void)lcid;
    if (!src || !out || infotype != 1)   /* CT_CTYPE1 only */
        return CE_FALSE;
    int n = cch < 0 ? (int)ce_wcslen(src) : cch;
    for (int i = 0; i < n; i++) {
        wint_t c = src[i];
        lw v = 0;
        if (iswupper(c)) v |= 0x0001;
        if (iswlower(c)) v |= 0x0002;
        if (iswdigit(c)) v |= 0x0004;
        if (iswspace(c)) v |= 0x0008;
        if (iswpunct(c)) v |= 0x0010;
        if (iswcntrl(c)) v |= 0x0020;
        if (c == ' ' || c == '\t') v |= 0x0040;
        if (iswxdigit(c)) v |= 0x0080;
        if (iswalpha(c)) v |= 0x0100;
        out[i] = v;
    }
    return CE_TRUE;
}

CE_BOOL ce_EnumCalendarInfoW(void *proc, CE_DWORD lcid,
                             CE_DWORD calid, CE_DWORD type)
{
    /* One installed calendar: Gregorian (id "1"). */
    (void)lcid;
    (void)type;
    if (!proc)
        return CE_FALSE;
    if (calid != 0xFFFFFFFFu && calid != 1)
        return CE_TRUE;
    typedef int (*enumproc)(CE_WCHAR *);
    CE_WCHAR one[2] = { '1', 0 };
    ((enumproc)proc)(one);
    return CE_TRUE;
}

/* ---------------- codepage conversion --------------------- */

int ce_WideCharToMultiByte(CE_DWORD cp, CE_DWORD flags,
                           const CE_WCHAR *wide, int cch_wide,
                           char *mb, int cb_mb, const char *def,
                           int *used_def)
{
    (void)flags;
    (void)def;
    if (used_def)
        *used_def = 0;
    if (!wide)
        return 0;
    int n = cch_wide < 0 ? (int)ce_wcslen(wide) + 1 : cch_wide;
    if (cp == 65001 || cp == 1200) {
        /* UTF-8 (1200 UTF-16 is not a valid target for this API
         * on CE; treat as UTF-8 fallback is wrong -- handle
         * 1200 as a plain copy below) */
        if (cp == 1200) {
            int need = n * 2;
            if (!mb || cb_mb == 0)
                return need;
            if (cb_mb < need)
                return 0;
            memcpy(mb, wide, (size_t)need);
            return need;
        }
        char tmp[8192];
        int tn = 0;
        for (int i = 0; i < n; i++) {
            uint32_t c = wide[i];
            if (c == 0 && cch_wide < 0) {
                if (tn < (int)sizeof(tmp))
                    tmp[tn++] = 0;
                break;
            }
            if (c < 0x80) {
                if (tn < (int)sizeof(tmp)) tmp[tn++] = (char)c;
            } else if (c < 0x800) {
                if (tn + 2 <= (int)sizeof(tmp)) {
                    tmp[tn++] = (char)(0xC0 | (c >> 6));
                    tmp[tn++] = (char)(0x80 | (c & 0x3F));
                }
            } else {
                if (tn + 3 <= (int)sizeof(tmp)) {
                    tmp[tn++] = (char)(0xE0 | (c >> 12));
                    tmp[tn++] = (char)(0x80 | ((c >> 6) & 0x3F));
                    tmp[tn++] = (char)(0x80 | (c & 0x3F));
                }
            }
        }
        if (!mb || cb_mb == 0)
            return tn;
        if (cb_mb < tn)
            return 0;
        memcpy(mb, tmp, (size_t)tn);
        return tn;
    }
    /* ANSI/OEM on this device is CP932 */
    const char *enc = "CP932";
    if (cp != 0 && cp != 1 && cp != 932) {
        char name[16];
        snprintf(name, sizeof(name), "CP%u", cp);
        enc = name;
        iconv_t probe = iconv_open(enc, "UTF-16LE");
        if (probe == (iconv_t)-1)
            return 0;
        iconv_close(probe);
        /* use a static copy since `name` is stack-local */
        static char encbuf[16];
        strcpy(encbuf, name);
        enc = encbuf;
    }
    iconv_t ic = iconv_open(enc, "UTF-16LE");
    if (ic == (iconv_t)-1)
        return 0;
    char tmp[8192];
    char *inp = (char *)wide;
    size_t inleft = (size_t)n * 2;
    char *outp = tmp;
    size_t outleft = sizeof(tmp);
    size_t r = iconv(ic, &inp, &inleft, &outp, &outleft);
    iconv_close(ic);
    if (r == (size_t)-1 && errno != E2BIG) {
        /* still return what converted */
    }
    int tn = (int)(sizeof(tmp) - outleft);
    if (!mb || cb_mb == 0)
        return tn;
    if (cb_mb < tn)
        return 0;
    memcpy(mb, tmp, (size_t)tn);
    return tn;
}

int ce_MultiByteToWideChar(CE_DWORD cp, CE_DWORD flags,
                           const char *mb, int cb_mb,
                           CE_WCHAR *wide, int cch_wide)
{
    (void)flags;
    if (!mb)
        return 0;
    int n = cb_mb < 0 ? (int)strlen(mb) + 1 : cb_mb;
    if (cp == 65001) {
        CE_WCHAR tmp[4096];
        int tn = 0;
        int i = 0;
        while (i < n && tn < 4095) {
            unsigned char c = (unsigned char)mb[i];
            if (c == 0 && cb_mb < 0) {
                tmp[tn++] = 0;
                break;
            }
            uint32_t v;
            int len;
            if (c < 0x80) { v = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { v = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { v = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { v = c & 0x07; len = 4; }
            else { v = 0xFFFD; len = 1; }
            if (len > 1) {
                if (i + len > n) { v = 0xFFFD; len = 1; }
                else {
                    for (int k = 1; k < len; k++)
                        v = (v << 6) | ((unsigned char)mb[i + k]
                                        & 0x3F);
                    if (v > 0xFFFF)
                        v = 0xFFFD;
                }
            }
            tmp[tn++] = (CE_WCHAR)v;
            i += len;
        }
        if (!wide || cch_wide == 0)
            return tn;
        if (cch_wide < tn)
            return 0;
        memcpy(wide, tmp, (size_t)tn * sizeof(CE_WCHAR));
        return (cb_mb < 0) ? tn - 1 : tn;
    }
    const char *enc = "CP932";
    char encbuf[16];
    if (cp != 0 && cp != 1 && cp != 932 && cp != 1200) {
        snprintf(encbuf, sizeof(encbuf), "CP%u", cp);
        enc = encbuf;
    }
    if (cp == 1200) {
        int need = n / 2;
        if (!wide || cch_wide == 0)
            return need;
        if (cch_wide < need)
            return 0;
        memcpy(wide, mb, (size_t)need * 2);
        return need;
    }
    iconv_t ic = iconv_open("UTF-16LE", enc);
    if (ic == (iconv_t)-1)
        return 0;
    CE_WCHAR tmp[4096];
    char *inp = (char *)mb;
    size_t inleft = (size_t)n;
    char *outp = (char *)tmp;
    size_t outleft = sizeof(tmp);
    iconv(ic, &inp, &inleft, &outp, &outleft);
    iconv_close(ic);
    int tn = (int)((sizeof(tmp) - outleft) / 2);
    if (!wide || cch_wide == 0)
        return tn;
    if (cch_wide < tn)
        return 0;
    memcpy(wide, tmp, (size_t)tn * sizeof(CE_WCHAR));
    return (cb_mb < 0 && tn > 0) ? tn - 1 : tn;
}
