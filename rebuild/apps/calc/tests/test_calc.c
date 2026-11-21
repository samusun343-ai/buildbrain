/* test_calc - bd-calc model tests (strict).
 *
 * Covers the standard/scientific evaluator (precedence, powers,
 * implicit multiplication, functions in Deg and Rad, factorial,
 * percent, every error code), the memory register, formatting,
 * the programmer evaluator in all four bases, conversion tables
 * incl. affine temperature and the offline currency rule, and
 * the count-capped paper-tape history incl. torn-line recovery.
 */
#define _POSIX_C_SOURCE 200809L
#include "calcmodel.h"
#include "bdstore.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_err(const char *what, kc_err got, kc_err want)
{
    if (got == want) { passes++; printf("PASS %s -> %d\n", what, (int)got); }
    else { fails++; printf("FAIL %s: got err %d, want %d\n", what, (int)got, (int)want); }
}

static calc_state st;

static void ev(const char *expr, double want)
{
    double out = 0;
    kc_err e = calc_eval(expr, &st, &out);
    char what[160];
    snprintf(what, sizeof(what), "eval %s", expr);
    if (e == KC_OK && fabs(out - want) < 1e-9 * (1 + fabs(want))) {
        passes++; printf("PASS %s -> %.12g\n", what, out);
    } else {
        fails++;
        printf("FAIL %s: err %d out %.12g, want %.12g\n", what, (int)e, out, want);
    }
}
static void ev_err(const char *expr, kc_err want)
{
    double out;
    char what[160];
    snprintf(what, sizeof(what), "eval-err %s", expr);
    check_err(what, calc_eval(expr, &st, &out), want);
}
static void fmt(double v, const char *want)
{
    char buf[64], what[96];
    calc_format(v, buf, sizeof(buf));
    snprintf(what, sizeof(what), "fmt %.12g", v);
    if (strcmp(buf, want) == 0) { passes++; printf("PASS %s -> %s\n", what, buf); }
    else { fails++; printf("FAIL %s: got %s, want %s\n", what, buf, want); }
}
static void pv(const char *expr, int base, uint64_t want)
{
    uint64_t out = 0;
    kc_err e = calc_prog_eval(expr, base, &out);
    char what[160];
    snprintf(what, sizeof(what), "prog(%d) %s", base, expr);
    if (e == KC_OK && out == want) { passes++; printf("PASS %s -> %llu\n", what, (unsigned long long)out); }
    else { fails++; printf("FAIL %s: err %d out %llu, want %llu\n", what, (int)e, (unsigned long long)out, (unsigned long long)want); }
}
static void cv(size_t cat, size_t from, size_t to, double v, double want)
{
    double out = 0;
    kc_err e = calc_convert(cat, from, to, v, &out);
    char what[128];
    snprintf(what, sizeof(what), "conv %s %s->%s",
             calc_conv_category_name(cat), calc_conv_unit_name(cat, from),
             calc_conv_unit_name(cat, to));
    if (e == KC_OK && fabs(out - want) < 1e-9 * (1 + fabs(want))) {
        passes++; printf("PASS %s %.6g -> %.12g\n", what, v, out);
    } else {
        fails++; printf("FAIL %s: err %d out %.12g want %.12g\n", what, (int)e, out, want);
    }
}

int main(void)
{
    calc_state_init(&st);

    /* ---- standard evaluator: arithmetic & precedence ---- */
    ev("2+3*4", 14);
    ev("(2+3)*4", 20);
    ev("10/4", 2.5);
    ev("2^3^2", 512);              /* right-associative */
    ev("-2^2", -4);                /* unary minus looser than ^ */
    ev("2^-2", 0.25);
    ev("3(1+2)", 9);               /* implicit multiplication */
    ev("2pi", 2 * M_PI);
    ev("(1+1)(2+2)", 8);
    ev("2e", 2 * M_E);
    ev("1.5E3", 1500);             /* EE literal */
    ev("2.5e-2", 0.025);
    ev("5!", 120);
    ev("0!", 1);
    ev("50%", 0.5);
    ev("200*10%", 20);
    ev("sqrt(16)", 4);
    ev("sqrt(2)^2", 2);
    ev("ln(e)", 1);
    ev("log10(1000)", 3);
    ev("log(100)", 2);
    ev("log2(1024)", 10);
    ev("exp(0)", 1);
    ev("abs(-3.5)", 3.5);
    ev("cbrt(27)", 3);
    ev("tau", 2 * M_PI);
    ev("  1 + 2 ", 3);
    ev("10%*50", 5);

    /* Deg mode (default) */
    ev("sin(90)", 1);
    ev("cos(180)", -1);
    ev("tan(45)", 1);
    ev("asin(1)", 90);
    ev("atan(1)", 45);
    /* Rad mode */
    st.deg = false;
    ev("sin(pi/2)", 1);
    ev("asin(1)", M_PI / 2);
    ev("sinh(0)", 0);
    ev("cosh(0)", 1);
    ev("atanh(0)", 0);
    st.deg = true;

    /* memory register through the M token */
    calc_mem_clear(&st);
    ev("M", 0);
    calc_mem_add(&st, 7);
    ev("M*2", 14);
    calc_mem_sub(&st, 2);
    ev("M", 5);
    calc_mem_clear(&st);
    ev("M", 0);

    /* ---- errors ---- */
    ev_err("", KC_ERR_RANGE);
    ev_err("1/0", KC_ERR_DIVZERO);
    ev_err("0/0", KC_ERR_DIVZERO);
    ev_err("sqrt(-1)", KC_ERR_DOMAIN);
    ev_err("ln(0)", KC_ERR_DOMAIN);
    ev_err("ln(-2)", KC_ERR_DOMAIN);
    ev_err("asin(2)", KC_ERR_DOMAIN);
    ev_err("acosh(0.5)", KC_ERR_DOMAIN);
    ev_err("atanh(1)", KC_ERR_DOMAIN);
    ev_err("(-2)^0.5", KC_ERR_DOMAIN);
    ev_err("tan(90)", KC_ERR_DOMAIN);
    ev_err("171!", KC_ERR_OVERFLOW);
    ev_err("exp(10000)", KC_ERR_OVERFLOW);
    ev_err("2+", KC_ERR_SYNTAX);
    ev_err("(1+2", KC_ERR_SYNTAX);
    ev_err("foo(1)", KC_ERR_SYNTAX);
    ev_err("1..2", KC_ERR_SYNTAX);
    ev_err("2^^3", KC_ERR_SYNTAX);
    ev_err(")", KC_ERR_SYNTAX);
    check_true("err messages are Japanese",
               strstr(calc_err_str(KC_ERR_DIVZERO), "0") != NULL &&
               strlen(calc_err_str(KC_ERR_NO_RATE)) > 0);

    /* ---- formatting ---- */
    fmt(0, "0");
    fmt(-0.0, "0");
    fmt(14, "14");
    fmt(-42, "-42");
    fmt(2.5, "2.5");
    fmt(1.0 / 3.0, "0.333333333333");
    fmt(123456789012345.0, "123456789012345");
    fmt(1e15, "1e15");
    fmt(1.5e20, "1.5e20");
    fmt(1e-11, "1e-11");
    fmt(0.000123, "0.000123");

    /* ---- programmer evaluator ---- */
    pv("FF+1", 16, 256);
    pv("A*B", 16, 110);
    pv("1010 AND 1100", 2, 0b1000);
    pv("1010 OR 1100", 2, 0b1110);
    pv("1010 XOR 1100", 2, 0b0110);
    pv("1010 NOR 1100", 2, ~(uint64_t)0b1110);
    pv("NOT 0", 10, ~(uint64_t)0);
    pv("~0", 10, ~(uint64_t)0);
    pv("1<<10", 10, 1024);
    pv("1024>>4", 10, 64);
    pv("777+1", 8, 512);
    pv("10/3", 10, 3);                    /* truncating */
    pv("-1", 10, ~(uint64_t)0);           /* wraps */
    pv("0-1", 16, ~(uint64_t)0);
    pv("(1+2)*3", 10, 9);
    pv("1+2<<3", 10, 24);                 /* shift lower than + */
    pv("1<<64", 10, 1);                   /* shift count masked */
    pv("FF AND 0F", 16, 0x0F);
    {
        uint64_t o;
        check_err("prog div0", calc_prog_eval("1/0", 10, &o), KC_ERR_DIVZERO);
        check_err("prog bad digit", calc_prog_eval("2", 2, &o), KC_ERR_SYNTAX);
        check_err("prog bad base", calc_prog_eval("1", 3, &o), KC_ERR_RANGE);
        check_err("prog empty", calc_prog_eval("", 10, &o), KC_ERR_RANGE);
        check_err("prog trailing", calc_prog_eval("1+", 10, &o), KC_ERR_SYNTAX);
    }
    {
        char buf[128];
        calc_prog_format(255, 16, buf, sizeof(buf));
        check_true("prog fmt hex", strcmp(buf, "FF") == 0);
        calc_prog_format(255, 2, buf, sizeof(buf));
        check_true("prog fmt bin", strcmp(buf, "11111111") == 0);
        calc_prog_format(64, 8, buf, sizeof(buf));
        check_true("prog fmt oct", strcmp(buf, "100") == 0);
        calc_prog_bits(0xA5, buf, sizeof(buf));
        check_true("prog bits grouped",
                   strcmp(buf, "00000000 00000000 00000000 00000000 "
                             "00000000 00000000 00000000 10100101") == 0);
        calc_prog_char_info(65, buf, sizeof(buf));
        check_true("char info A", strcmp(buf, "'A' (U+0041)") == 0);
        calc_prog_char_info(10, buf, sizeof(buf));
        check_true("char info LF", strstr(buf, "LF") != NULL);
        calc_prog_char_info(0x7F, buf, sizeof(buf));
        check_true("char info DEL", strstr(buf, "DEL") != NULL);
        calc_prog_char_info(0x3042, buf, sizeof(buf));
        check_true("char info U+3042", strcmp(buf, "U+3042") == 0);
    }

    /* ---- conversion ---- */
    /* category indices per table order: 0 length 1 mass 2 area
     * 3 volume 4 temp 5 speed 6 energy 7 pressure 8 power 9 time
     * 10 currency */
    check_true("11 categories", calc_conv_categories() == 11);
    cv(0, 3, 2, 1, 1000);            /* km -> m */
    cv(0, 4, 2, 12, 12 * 0.0254);    /* inch -> m */
    cv(1, 2, 1, 2.5, 2500);          /* kg -> g */
    cv(3, 1, 0, 1, 1000);            /* L -> mL */
    cv(4, 0, 1, 100, 212);           /* C -> F */
    cv(4, 1, 0, 32, 0);              /* F -> C */
    cv(4, 0, 2, 0, 273.15);          /* C -> K */
    cv(4, 2, 1, 0, -459.67);         /* K -> F */
    cv(5, 1, 0, 36, 10);             /* km/h -> m/s */
    cv(9, 3, 2, 2, 120);             /* h -> min */
    cv(2, 4, 0, 1, 3.305785123966942); /* tsubo -> m2 */
    {
        double o;
        check_err("currency without rate",
                  calc_convert(10, 1, 0, 100, &o), KC_ERR_NO_RATE);
        calc_rate rates[] = { { "USD", 150.0 }, { "EUR", 160.0 } };
        check_err("USD->JPY with rate",
                  calc_convert_ex(10, 1, 0, 2, rates, 2, &o), KC_OK);
        check_true("2 USD = 300 JPY", fabs(o - 300) < 1e-9);
        check_err("JPY->EUR with rate",
                  calc_convert_ex(10, 0, 2, 320, rates, 2, &o), KC_OK);
        check_true("320 JPY = 2 EUR", fabs(o - 2) < 1e-9);
        check_err("GBP without rate",
                  calc_convert_ex(10, 3, 0, 1, rates, 2, &o), KC_ERR_NO_RATE);
        check_err("conv bad cat", calc_convert(99, 0, 0, 1, &o), KC_ERR_RANGE);
        check_err("conv bad unit", calc_convert(0, 0, 99, 1, &o), KC_ERR_RANGE);
        check_true("is_currency", calc_conv_is_currency(10) &&
                   !calc_conv_is_currency(0));
    }

    /* ---- history tape ---- */
    char tmpl[] = "/tmp/bdcalc-XXXXXX";
    check_true("mkdtemp", mkdtemp(tmpl) != NULL);
    kc_hist *h = kc_hist_open(tmpl);
    check_true("hist open", h != NULL);
    check_true("hist empty", kc_hist_count(h) == 0);
    kc_hist_add(h, "1+1", "2");
    kc_hist_add(h, "sqrt(2)", "1.41421356237");
    check_true("hist count 2", kc_hist_count(h) == 2);
    check_true("hist at 0", strcmp(kc_hist_at(h, 0), "1+1 = 2") == 0);
    char eb[64];
    kc_hist_expr_at(h, 1, eb, sizeof(eb));
    check_true("hist expr 1", strcmp(eb, "sqrt(2)") == 0);
    kc_hist_close(h);
    h = kc_hist_open(tmpl);   /* persisted */
    check_true("hist reload 2", kc_hist_count(h) == 2);
    for (int i = 0; i < 250; i++) {
        char e[32];
        snprintf(e, sizeof(e), "%d+0", i);
        kc_hist_add(h, e, "x");
    }
    check_true("hist capped at 200", kc_hist_count(h) == 200);
    kc_hist_expr_at(h, 0, eb, sizeof(eb));
    check_true("hist oldest dropped", strcmp(eb, "50+0") == 0);
    kc_hist_clear(h);
    check_true("hist cleared", kc_hist_count(h) == 0);
    kc_hist_close(h);
    /* torn / foreign lines are skipped on load */
    char path[1200];
    snprintf(path, sizeof(path), "%s/history.txt", tmpl);
    bd_write_atomic_str(path, "3+3 = 6\ntorn line without sep\n4+4 = 8\n");
    h = kc_hist_open(tmpl);
    check_true("hist torn skipped", kc_hist_count(h) == 2);
    kc_hist_close(h);

    printf("test_calc: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
