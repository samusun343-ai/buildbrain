/* calcmodel - bd-calc expression engine. Implements the
 * contract in calcmodel.h: a recursive-descent parser for the
 * standard/scientific evaluator, a separate uint64 evaluator
 * for programmer mode, offline unit-conversion tables (currency
 * needs a hand-entered rate - the device is offline), and the
 * count-capped paper-tape history persisted through bdstore.
 */
#define _POSIX_C_SOURCE 200809L
#include "calcmodel.h"
#include "bdstore.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

void calc_state_init(calc_state *st)
{
    st->memory = 0;
    st->deg = true;
}

const char *calc_err_str(kc_err e)
{
    switch (e) {
    case KC_OK:          return "OK";
    case KC_ERR_SYNTAX:  return "式が正しくありません";
    case KC_ERR_DIVZERO: return "0 で割ることはできません";
    case KC_ERR_DOMAIN:  return "定義域エラーです";
    case KC_ERR_OVERFLOW:return "値が大きすぎます";
    case KC_ERR_NO_RATE: return "為替レートが未設定です（オフラインのため手入力が必要です）";
    case KC_ERR_RANGE:   return "範囲外です";
    }
    return "エラー";
}

/* ================================================================ */
/* standard / scientific evaluator                                   */
/* ================================================================ */

typedef struct {
    const char *p;
    const calc_state *st;
    kc_err err;
} parser;

static void skip_ws(parser *ps) { while (*ps->p == ' ' || *ps->p == '\t') ps->p++; }

static double parse_expr(parser *ps);

/* function table: name -> id */
enum {
    F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN,
    F_SINH, F_COSH, F_TANH, F_ASINH, F_ACOSH, F_ATANH,
    F_LN, F_LOG10, F_LOG2, F_SQRT, F_CBRT, F_EXP, F_ABS
};
static const struct { const char *name; int id; } funcs[] = {
    { "asin", F_ASIN }, { "acos", F_ACOS }, { "atan", F_ATAN },
    { "asinh", F_ASINH }, { "acosh", F_ACOSH }, { "atanh", F_ATANH },
    { "sinh", F_SINH }, { "cosh", F_COSH }, { "tanh", F_TANH },
    { "sin", F_SIN }, { "cos", F_COS }, { "tan", F_TAN },
    { "ln", F_LN }, { "log10", F_LOG10 }, { "log2", F_LOG2 },
    { "log", F_LOG10 },
    { "sqrt", F_SQRT }, { "cbrt", F_CBRT }, { "exp", F_EXP },
    { "abs", F_ABS },
};

static double apply_func(parser *ps, int id, double x)
{
    const double d2r = M_PI / 180.0, r2d = 180.0 / M_PI;
    bool deg = ps->st->deg;
    switch (id) {
    case F_SIN:  return sin(deg ? x * d2r : x);
    case F_COS:  return cos(deg ? x * d2r : x);
    case F_TAN: {
        double a = deg ? x * d2r : x;
        /* tan is undefined at odd multiples of 90 deg / pi/2 */
        double c = cos(a);
        if (fabs(c) < 1e-15) { ps->err = KC_ERR_DOMAIN; return 0; }
        return sin(a) / c;
    }
    case F_ASIN:
        if (x < -1 || x > 1) { ps->err = KC_ERR_DOMAIN; return 0; }
        return deg ? asin(x) * r2d : asin(x);
    case F_ACOS:
        if (x < -1 || x > 1) { ps->err = KC_ERR_DOMAIN; return 0; }
        return deg ? acos(x) * r2d : acos(x);
    case F_ATAN: return deg ? atan(x) * r2d : atan(x);
    case F_SINH: return sinh(x);
    case F_COSH: return cosh(x);
    case F_TANH: return tanh(x);
    case F_ASINH: return asinh(x);
    case F_ACOSH:
        if (x < 1) { ps->err = KC_ERR_DOMAIN; return 0; }
        return acosh(x);
    case F_ATANH:
        if (x <= -1 || x >= 1) { ps->err = KC_ERR_DOMAIN; return 0; }
        return atanh(x);
    case F_LN:
        if (x <= 0) { ps->err = KC_ERR_DOMAIN; return 0; }
        return log(x);
    case F_LOG10:
        if (x <= 0) { ps->err = KC_ERR_DOMAIN; return 0; }
        return log10(x);
    case F_LOG2:
        if (x <= 0) { ps->err = KC_ERR_DOMAIN; return 0; }
        return log2(x);
    case F_SQRT:
        if (x < 0) { ps->err = KC_ERR_DOMAIN; return 0; }
        return sqrt(x);
    case F_CBRT: return cbrt(x);
    case F_EXP:  return exp(x);
    case F_ABS:  return fabs(x);
    }
    ps->err = KC_ERR_SYNTAX;
    return 0;
}

static double factorial(parser *ps, double x)
{
    if (x < 0 && x == floor(x)) { ps->err = KC_ERR_DOMAIN; return 0; }
    if (x > 170) { ps->err = KC_ERR_OVERFLOW; return 0; }
    double r = tgamma(x + 1);
    if (isinf(r)) { ps->err = KC_ERR_OVERFLOW; return 0; }
    if (isnan(r)) { ps->err = KC_ERR_DOMAIN; return 0; }
    return r;
}

/* primary: number | constant | M | func '(' expr ')' | '(' expr ')' */
static double parse_primary(parser *ps)
{
    skip_ws(ps);
    char c = *ps->p;
    if (c == '(') {
        ps->p++;
        double v = parse_expr(ps);
        skip_ws(ps);
        if (*ps->p != ')') { ps->err = KC_ERR_SYNTAX; return 0; }
        ps->p++;
        return v;
    }
    if (isdigit((unsigned char)c) || c == '.') {
        char *end;
        double v = strtod(ps->p, &end);
        if (end == ps->p) { ps->err = KC_ERR_SYNTAX; return 0; }
        ps->p = end;
        /* "1..2" / "1.2.3": a second '.' is malformed, not an
         * implicit multiplication by a fraction */
        if (*ps->p == '.') { ps->err = KC_ERR_SYNTAX; return 0; }
        return v;
    }
    if (isalpha((unsigned char)c)) {
        /* longest function name first (table is ordered so) */
        for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++) {
            size_t n = strlen(funcs[i].name);
            if (strncmp(ps->p, funcs[i].name, n) == 0) {
                const char *q = ps->p + n;
                while (*q == ' ') q++;
                if (*q == '(') {
                    ps->p = q + 1;
                    double arg = parse_expr(ps);
                    skip_ws(ps);
                    if (*ps->p != ')') { ps->err = KC_ERR_SYNTAX; return 0; }
                    ps->p++;
                    return apply_func(ps, funcs[i].id, arg);
                }
            }
        }
        if (strncmp(ps->p, "tau", 3) == 0) { ps->p += 3; return 2 * M_PI; }
        if (strncmp(ps->p, "pi", 2) == 0)  { ps->p += 2; return M_PI; }
        if (c == 'M') { ps->p++; return ps->st->memory; }
        if (c == 'e') { ps->p++; return M_E; }
        ps->err = KC_ERR_SYNTAX;
        return 0;
    }
    ps->err = KC_ERR_SYNTAX;
    return 0;
}

/* postfix: primary { '!' | '%' } */
static double parse_postfix(parser *ps)
{
    double v = parse_primary(ps);
    for (;;) {
        if (ps->err) return 0;
        skip_ws(ps);
        if (*ps->p == '!') { ps->p++; v = factorial(ps, v); }
        else if (*ps->p == '%') { ps->p++; v = v / 100.0; }
        else break;
    }
    return v;
}

static double parse_factor(parser *ps);

/* power: postfix [ '^' factor ]  (right-associative) */
static double parse_power(parser *ps)
{
    double base = parse_postfix(ps);
    if (ps->err) return 0;
    skip_ws(ps);
    if (*ps->p == '^') {
        ps->p++;
        double ex = parse_factor(ps);
        if (ps->err) return 0;
        double r = pow(base, ex);
        if (isnan(r)) { ps->err = KC_ERR_DOMAIN; return 0; }
        return r;
    }
    return base;
}

/* factor: '-' factor | power   (unary minus binds looser than ^,
 * so -2^2 = -4, and 2^-2 parses) */
static double parse_factor(parser *ps)
{
    skip_ws(ps);
    if (*ps->p == '-') { ps->p++; return -parse_factor(ps); }
    if (*ps->p == '+') { ps->p++; return parse_factor(ps); }
    return parse_power(ps);
}

static bool starts_primary(parser *ps)
{
    skip_ws(ps);
    char c = *ps->p;
    return isdigit((unsigned char)c) || c == '.' || c == '(' ||
           isalpha((unsigned char)c);
}

/* term: factor { ('*'|'/'|implicit) factor } */
static double parse_term(parser *ps)
{
    double v = parse_factor(ps);
    for (;;) {
        if (ps->err) return 0;
        skip_ws(ps);
        char c = *ps->p;
        if (c == '*' || c == '/') {
            ps->p++;
            double rhs = parse_factor(ps);
            if (ps->err) return 0;
            if (c == '*') v *= rhs;
            else {
                if (rhs == 0) { ps->err = KC_ERR_DIVZERO; return 0; }
                v /= rhs;
            }
        } else if (starts_primary(ps)) {
            double rhs = parse_factor(ps);   /* implicit multiply */
            if (ps->err) return 0;
            v *= rhs;
        } else break;
    }
    return v;
}

static double parse_expr(parser *ps)
{
    double v = parse_term(ps);
    for (;;) {
        if (ps->err) return 0;
        skip_ws(ps);
        char c = *ps->p;
        if (c == '+' || c == '-') {
            ps->p++;
            double rhs = parse_term(ps);
            if (ps->err) return 0;
            v = (c == '+') ? v + rhs : v - rhs;
        } else break;
    }
    return v;
}

kc_err calc_eval(const char *expr, const calc_state *st, double *out)
{
    if (!expr) return KC_ERR_RANGE;
    parser ps = { expr, st, KC_OK };
    skip_ws(&ps);
    if (*ps.p == 0) return KC_ERR_RANGE;
    double v = parse_expr(&ps);
    if (ps.err) return ps.err;
    skip_ws(&ps);
    if (*ps.p != 0) return KC_ERR_SYNTAX;
    if (isnan(v)) return KC_ERR_DOMAIN;
    if (isinf(v)) return KC_ERR_OVERFLOW;
    *out = v;
    return KC_OK;
}

void calc_mem_clear(calc_state *st) { st->memory = 0; }
void calc_mem_add(calc_state *st, double v) { st->memory += v; }
void calc_mem_sub(calc_state *st, double v) { st->memory -= v; }

void calc_format(double v, char *out, size_t cap)
{
    if (cap == 0) return;
    if (v == 0) { snprintf(out, cap, "0"); return; }  /* also -0 */
    double av = fabs(v);
    if (v == floor(v) && av < 1e15) {
        snprintf(out, cap, "%.0f", v);
        return;
    }
    if (av >= 1e15 || av < 1e-10) {
        /* exponent form: %.11e then trim mantissa zeros */
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%.11e", v);
        char *e = strchr(tmp, 'e');
        if (e) {
            char *dot = strchr(tmp, '.');
            if (dot && dot < e) {
                char *t = e - 1;
                while (t > dot && *t == '0') t--;
                if (t == dot) t--;
                memmove(t + 1, e, strlen(e) + 1);
            }
            /* exponent: drop '+', strip leading zeros */
            char *ep = strchr(tmp, 'e') + 1;
            char sign = 0;
            if (*ep == '+' || *ep == '-') { sign = (*ep == '-') ? '-' : 0; ep++; }
            while (ep[0] == '0' && ep[1]) ep++;
            char num[16];
            snprintf(num, sizeof(num), "%s%s", sign ? "-" : "", ep);
            char *epos = strchr(tmp, 'e');
            *epos = 0;
            snprintf(out, cap, "%se%s", tmp, num);
            return;
        }
        snprintf(out, cap, "%s", tmp);
        return;
    }
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.12g", v);
    snprintf(out, cap, "%s", tmp);
}

/* ================================================================ */
/* programmer evaluator (uint64 wrap-around)                         */
/* ================================================================ */

typedef struct {
    const char *p;
    int base;
    kc_err err;
} pparser;

static void p_skip(pparser *ps) { while (*ps->p == ' ' || *ps->p == '\t') ps->p++; }

static int digit_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint64_t p_expr(pparser *ps);

static uint64_t p_primary(pparser *ps)
{
    p_skip(ps);
    if (*ps->p == '(') {
        ps->p++;
        uint64_t v = p_expr(ps);
        p_skip(ps);
        if (*ps->p != ')') { ps->err = KC_ERR_SYNTAX; return 0; }
        ps->p++;
        return v;
    }
    int d = digit_val(*ps->p);
    if (d >= 0 && d < ps->base) {
        uint64_t v = 0;
        while ((d = digit_val(*ps->p)) >= 0 && d < ps->base) {
            v = v * (uint64_t)ps->base + (uint64_t)d;
            ps->p++;
        }
        return v;
    }
    ps->err = KC_ERR_SYNTAX;
    return 0;
}

static uint64_t p_unary(pparser *ps)
{
    p_skip(ps);
    if (*ps->p == '~') { ps->p++; return ~p_unary(ps); }
    if (*ps->p == '-') { ps->p++; return 0 - p_unary(ps); }
    if (strncasecmp(ps->p, "NOT", 3) == 0 &&
        !isalnum((unsigned char)ps->p[3])) {
        ps->p += 3;
        return ~p_unary(ps);
    }
    return p_primary(ps);
}

static uint64_t p_mul(pparser *ps)
{
    uint64_t v = p_unary(ps);
    for (;;) {
        if (ps->err) return 0;
        p_skip(ps);
        char c = *ps->p;
        if (c != '*' && c != '/') break;
        ps->p++;
        uint64_t r = p_unary(ps);
        if (ps->err) return 0;
        if (c == '*') v *= r;
        else {
            if (r == 0) { ps->err = KC_ERR_DIVZERO; return 0; }
            v /= r;
        }
    }
    return v;
}

static uint64_t p_add(pparser *ps)
{
    uint64_t v = p_mul(ps);
    for (;;) {
        if (ps->err) return 0;
        p_skip(ps);
        char c = *ps->p;
        if (c != '+' && c != '-') break;
        ps->p++;
        uint64_t r = p_mul(ps);
        if (ps->err) return 0;
        v = (c == '+') ? v + r : v - r;
    }
    return v;
}

static uint64_t p_shift(pparser *ps)
{
    uint64_t v = p_add(ps);
    for (;;) {
        if (ps->err) return 0;
        p_skip(ps);
        if (ps->p[0] == '<' && ps->p[1] == '<') {
            ps->p += 2;
            uint64_t r = p_add(ps);
            if (ps->err) return 0;
            v <<= (r & 63);
        } else if (ps->p[0] == '>' && ps->p[1] == '>') {
            ps->p += 2;
            uint64_t r = p_add(ps);
            if (ps->err) return 0;
            v >>= (r & 63);
        } else break;
    }
    return v;
}

/* keyword binary op at cursor: returns length or 0. `word` must be
 * followed by a non-alphanumeric boundary. */
static int kw_op(pparser *ps, const char *word)
{
    size_t n = strlen(word);
    if (strncasecmp(ps->p, word, n) == 0 &&
        !isalnum((unsigned char)ps->p[n])) {
        ps->p += n;
        return 1;
    }
    return 0;
}

static uint64_t p_and(pparser *ps)
{
    uint64_t v = p_shift(ps);
    for (;;) {
        if (ps->err) return 0;
        p_skip(ps);
        if (!kw_op(ps, "AND")) break;
        uint64_t r = p_shift(ps);
        if (ps->err) return 0;
        v &= r;
    }
    return v;
}

static uint64_t p_xor(pparser *ps)
{
    uint64_t v = p_and(ps);
    for (;;) {
        if (ps->err) return 0;
        p_skip(ps);
        if (!kw_op(ps, "XOR")) break;
        uint64_t r = p_and(ps);
        if (ps->err) return 0;
        v ^= r;
    }
    return v;
}

static uint64_t p_expr(pparser *ps)
{
    uint64_t v = p_xor(ps);
    for (;;) {
        if (ps->err) return 0;
        p_skip(ps);
        if (kw_op(ps, "NOR")) {
            uint64_t r = p_xor(ps);
            if (ps->err) return 0;
            v = ~(v | r);
        } else if (kw_op(ps, "OR")) {
            uint64_t r = p_xor(ps);
            if (ps->err) return 0;
            v |= r;
        } else break;
    }
    return v;
}

kc_err calc_prog_eval(const char *expr, int base, uint64_t *out)
{
    if (!expr || (base != 2 && base != 8 && base != 10 && base != 16))
        return KC_ERR_RANGE;
    pparser ps = { expr, base, KC_OK };
    p_skip(&ps);
    if (*ps.p == 0) return KC_ERR_RANGE;
    uint64_t v = p_expr(&ps);
    if (ps.err) return ps.err;
    p_skip(&ps);
    if (*ps.p != 0) return KC_ERR_SYNTAX;
    *out = v;
    return KC_OK;
}

void calc_prog_format(uint64_t v, int base, char *out, size_t cap)
{
    if (cap == 0) return;
    if (v == 0) { snprintf(out, cap, "0"); return; }
    char rev[65];
    int n = 0;
    while (v) {
        int d = (int)(v % (uint64_t)base);
        rev[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v /= (uint64_t)base;
    }
    size_t w = 0;
    while (n > 0 && w + 1 < cap) out[w++] = rev[--n];
    out[w] = 0;
}

void calc_prog_bits(uint64_t v, char *out, size_t cap)
{
    size_t w = 0;
    for (int i = 63; i >= 0; i--) {
        if (w + 1 >= cap) break;
        out[w++] = (v >> i) & 1 ? '1' : '0';
        if (i > 0 && i % 8 == 0 && w + 1 < cap) out[w++] = ' ';
    }
    if (cap) out[w < cap ? w : cap - 1] = 0;
}

void calc_prog_char_info(uint64_t v, char *out, size_t cap)
{
    static const char *const c0[32] = {
        "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
        "BS", "TAB", "LF", "VT", "FF", "CR", "SO", "SI",
        "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
        "CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US"
    };
    if (v >= 0x20 && v <= 0x7E)
        snprintf(out, cap, "'%c' (U+%04llX)", (char)v,
                 (unsigned long long)v);
    else if (v < 0x20)
        snprintf(out, cap, "%s (U+%04llX, 制御文字)", c0[v],
                 (unsigned long long)v);
    else if (v == 0x7F)
        snprintf(out, cap, "DEL (U+007F, 制御文字)");
    else if (v <= 0x10FFFF)
        snprintf(out, cap, "U+%04llX", (unsigned long long)v);
    else
        snprintf(out, cap, "コードポイント範囲外");
}

/* ================================================================ */
/* conversion tables                                                 */
/* ================================================================ */

typedef struct {
    const char *name;
    double to_base;      /* multiply to get the base unit */
} unit_def;

typedef struct {
    const char *name;
    const unit_def *units;
    size_t n_units;
    bool affine_temp;    /* temperature: base is Celsius */
    bool currency;
} cat_def;

static const unit_def u_length[] = {
    { "ミリメートル", 0.001 }, { "センチメートル", 0.01 },
    { "メートル", 1 }, { "キロメートル", 1000 },
    { "インチ", 0.0254 }, { "フィート", 0.3048 },
    { "ヤード", 0.9144 }, { "マイル", 1609.344 },
    { "海里", 1852 },
};
static const unit_def u_mass[] = {
    { "ミリグラム", 1e-6 }, { "グラム", 0.001 }, { "キログラム", 1 },
    { "トン", 1000 }, { "オンス", 0.028349523125 },
    { "ポンド", 0.45359237 }, { "貫", 3.75 }, { "斤", 0.6 },
};
static const unit_def u_area[] = {
    { "平方メートル", 1 }, { "平方キロメートル", 1e6 },
    { "ヘクタール", 1e4 }, { "アール", 100 },
    { "坪", 3.305785123966942 }, { "畳（江戸間）", 1.548 },
    { "エーカー", 4046.8564224 },
};
static const unit_def u_volume[] = {
    { "ミリリットル", 1e-6 }, { "リットル", 0.001 },
    { "立方メートル", 1 }, { "合", 0.00018039 },
    { "升", 0.0018039 }, { "斗", 0.018039 }, { "石", 0.18039 },
    { "米ガロン", 0.003785411784 }, { "米液量オンス", 2.95735295625e-5 },
};
static const unit_def u_temp[] = {   /* affine, handled specially */
    { "摂氏 (℃)", 1 }, { "華氏 (℉)", 0 }, { "ケルビン (K)", 0 },
};
static const unit_def u_speed[] = {
    { "m/s", 1 }, { "km/h", 1000.0 / 3600.0 }, { "マイル/h", 0.44704 },
    { "ノット", 1852.0 / 3600.0 }, { "マッハ（海面）", 340.29 },
};
static const unit_def u_energy[] = {
    { "ジュール", 1 }, { "キロジュール", 1000 },
    { "カロリー", 4.184 }, { "キロカロリー", 4184 },
    { "Wh", 3600 }, { "kWh", 3600000 }, { "BTU", 1055.05585 },
};
static const unit_def u_pressure[] = {
    { "パスカル", 1 }, { "ヘクトパスカル", 100 }, { "キロパスカル", 1000 },
    { "バール", 100000 }, { "気圧", 101325 }, { "mmHg", 133.322387415 },
};
static const unit_def u_power[] = {
    { "ワット", 1 }, { "キロワット", 1000 }, { "馬力（仏）", 735.49875 },
    { "馬力（英）", 745.699872 },
};
static const unit_def u_time[] = {
    { "ミリ秒", 0.001 }, { "秒", 1 }, { "分", 60 }, { "時間", 3600 },
    { "日", 86400 }, { "週", 604800 }, { "年（365.25日）", 31557600 },
};
static const unit_def u_currency[] = {  /* factors unused: rates rule */
    { "JPY", 1 }, { "USD", 0 }, { "EUR", 0 }, { "GBP", 0 },
    { "AUD", 0 }, { "CNY", 0 }, { "KRW", 0 },
};

#define CAT(nm, u, aff, cur) { nm, u, sizeof(u) / sizeof(u[0]), aff, cur }
static const cat_def cats[] = {
    CAT("長さ", u_length, false, false),
    CAT("質量", u_mass, false, false),
    CAT("面積", u_area, false, false),
    CAT("体積", u_volume, false, false),
    CAT("温度", u_temp, true, false),
    CAT("速度", u_speed, false, false),
    CAT("エネルギー", u_energy, false, false),
    CAT("圧力", u_pressure, false, false),
    CAT("仕事率", u_power, false, false),
    CAT("時間", u_time, false, false),
    CAT("通貨", u_currency, false, true),
};

size_t calc_conv_categories(void) { return sizeof(cats) / sizeof(cats[0]); }

const char *calc_conv_category_name(size_t cat)
{
    return cat < calc_conv_categories() ? cats[cat].name : NULL;
}

size_t calc_conv_units(size_t cat)
{
    return cat < calc_conv_categories() ? cats[cat].n_units : 0;
}

const char *calc_conv_unit_name(size_t cat, size_t unit)
{
    if (cat >= calc_conv_categories() || unit >= cats[cat].n_units)
        return NULL;
    return cats[cat].units[unit].name;
}

bool calc_conv_is_currency(size_t cat)
{
    return cat < calc_conv_categories() && cats[cat].currency;
}

static double temp_to_c(size_t unit, double v)
{
    switch (unit) {
    case 0: return v;                    /* C */
    case 1: return (v - 32) * 5.0 / 9.0; /* F */
    case 2: return v - 273.15;           /* K */
    }
    return v;
}

static double temp_from_c(size_t unit, double v)
{
    switch (unit) {
    case 0: return v;
    case 1: return v * 9.0 / 5.0 + 32;
    case 2: return v + 273.15;
    }
    return v;
}

static const calc_rate *find_rate(const calc_rate *rates, size_t n,
                                  const char *unit)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(rates[i].unit, unit) == 0) return &rates[i];
    return NULL;
}

kc_err calc_convert_ex(size_t cat, size_t from, size_t to, double v,
                       const calc_rate *rates, size_t n_rates,
                       double *out)
{
    if (cat >= calc_conv_categories()) return KC_ERR_RANGE;
    const cat_def *c = &cats[cat];
    if (from >= c->n_units || to >= c->n_units) return KC_ERR_RANGE;
    if (c->affine_temp) {
        *out = temp_from_c(to, temp_to_c(from, v));
        return KC_OK;
    }
    if (c->currency) {
        /* value -> JPY -> target, both legs need a rate (JPY = 1) */
        double in_jpy;
        if (strcmp(c->units[from].name, "JPY") == 0) in_jpy = v;
        else {
            const calc_rate *r = find_rate(rates, n_rates, c->units[from].name);
            if (!r) return KC_ERR_NO_RATE;
            in_jpy = v * r->per_jpy;
        }
        if (strcmp(c->units[to].name, "JPY") == 0) { *out = in_jpy; return KC_OK; }
        const calc_rate *r = find_rate(rates, n_rates, c->units[to].name);
        if (!r) return KC_ERR_NO_RATE;
        if (r->per_jpy == 0) return KC_ERR_NO_RATE;
        *out = in_jpy / r->per_jpy;
        return KC_OK;
    }
    *out = v * c->units[from].to_base / c->units[to].to_base;
    return KC_OK;
}

kc_err calc_convert(size_t cat, size_t from, size_t to, double v, double *out)
{
    return calc_convert_ex(cat, from, to, v, NULL, 0, out);
}

/* memory register helpers for the UI's rate table live in the UI;
 * rates themselves are plain data (calc_rate) above. */

/* ================================================================ */
/* paper-tape history                                                */
/* ================================================================ */

#define KC_HIST_MAX 200

struct kc_hist {
    char dir[1024];
    char **lines;
    size_t n, cap;
};

static void hist_path(const kc_hist *h, char *out, size_t cap)
{
    snprintf(out, cap, "%s/history.txt", h->dir);
}

static void hist_flush(kc_hist *h)
{
    /* join lines and rewrite atomically; <= 200 short lines */
    size_t total = 0;
    for (size_t i = 0; i < h->n; i++) total += strlen(h->lines[i]) + 1;
    char *buf = malloc(total + 1);
    if (!buf) return;
    size_t w = 0;
    for (size_t i = 0; i < h->n; i++) {
        size_t n = strlen(h->lines[i]);
        memcpy(buf + w, h->lines[i], n);
        w += n;
        buf[w++] = '\n';
    }
    buf[w] = 0;
    char path[1100];
    hist_path(h, path, sizeof(path));
    bd_write_atomic(path, buf, w);
    free(buf);
}

kc_hist *kc_hist_open(const char *root)
{
    kc_hist *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    snprintf(h->dir, sizeof(h->dir), "%s", root);
    bd_mkdir_p(h->dir);
    bd_sweep_tmp(h->dir);
    char path[1100];
    hist_path(h, path, sizeof(path));
    size_t len;
    char *data = bd_read_all(path, &len);
    if (data) {
        char *save = NULL;
        for (char *line = strtok_r(data, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            if (!strstr(line, " = ")) continue;  /* torn/foreign line */
            if (h->n == h->cap) {
                h->cap = h->cap ? h->cap * 2 : 32;
                h->lines = realloc(h->lines, h->cap * sizeof(char *));
            }
            h->lines[h->n++] = strdup(line);
        }
        free(data);
    }
    /* enforce the count cap on load too (older/foreign writers) */
    while (h->n > KC_HIST_MAX) {
        free(h->lines[0]);
        memmove(h->lines, h->lines + 1, (h->n - 1) * sizeof(char *));
        h->n--;
    }
    return h;
}

void kc_hist_close(kc_hist *h)
{
    if (!h) return;
    for (size_t i = 0; i < h->n; i++) free(h->lines[i]);
    free(h->lines);
    free(h);
}

size_t kc_hist_count(const kc_hist *h) { return h ? h->n : 0; }

const char *kc_hist_at(const kc_hist *h, size_t i)
{
    return (h && i < h->n) ? h->lines[i] : NULL;
}

void kc_hist_expr_at(const kc_hist *h, size_t i, char *out, size_t cap)
{
    if (cap == 0) return;
    out[0] = 0;
    const char *line = kc_hist_at(h, i);
    if (!line) return;
    const char *sep = strstr(line, " = ");
    size_t n = sep ? (size_t)(sep - line) : strlen(line);
    if (n >= cap) n = cap - 1;
    memcpy(out, line, n);
    out[n] = 0;
}

void kc_hist_add(kc_hist *h, const char *expr, const char *result)
{
    if (!h || !expr || !result) return;
    char line[1024];
    snprintf(line, sizeof(line), "%s = %s", expr, result);
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 32;
        h->lines = realloc(h->lines, h->cap * sizeof(char *));
    }
    h->lines[h->n++] = strdup(line);
    while (h->n > KC_HIST_MAX) {
        free(h->lines[0]);
        memmove(h->lines, h->lines + 1, (h->n - 1) * sizeof(char *));
        h->n--;
    }
    hist_flush(h);
}

void kc_hist_clear(kc_hist *h)
{
    if (!h) return;
    for (size_t i = 0; i < h->n; i++) free(h->lines[i]);
    h->n = 0;
    hist_flush(h);
}
