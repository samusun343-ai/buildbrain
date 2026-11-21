/* calcmodel - bd-calc expression engine (no toolkit dependency).
 *
 * Three evaluators over one parser family:
 *   - standard/scientific: full expression strings with + - * /
 *     ^ (right-associative), unary minus, parentheses, postfix
 *     ! (factorial) and % (percent, /100), functions, constants
 *     pi/e/tau, EE exponents in literals, implicit multiplication
 *     before '(' or a constant/function (2pi, 3(1+2)). Trig
 *     honours the state's Deg/Rad mode; a "2nd" flag swaps each
 *     trig/hyperbolic/log/power function for its inverse when
 *     the UI builds tokens - the parser itself always sees the
 *     final function name.
 *   - programmer: uint64 wrap-around arithmetic in base 2/8/10/
 *     16 with AND OR XOR NOR NOT << >> and the arithmetic ops.
 *   - conversion: offline unit tables (length, mass, area,
 *     volume, temperature, speed, energy, pressure, power,
 *     time). Currency is present as a category but has NO
 *     built-in rates: the device is offline by default, so a
 *     rate must be entered by hand (calc_set_rate) before a
 *     currency conversion succeeds; without one the call fails
 *     with KC_ERR_NO_RATE instead of inventing numbers.
 *
 * Errors are reported as codes + a Japanese message; the
 * evaluator never returns NaN silently.
 *
 * History (the "paper tape") is persisted with bdstore atomic
 * writes, capacity-capped by COUNT (200 entries, oldest dropped
 * first) - never by age: the device clock is untrusted.
 */
#ifndef CALCMODEL_H
#define CALCMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    KC_OK = 0,
    KC_ERR_SYNTAX,     /* malformed expression */
    KC_ERR_DIVZERO,
    KC_ERR_DOMAIN,     /* sqrt(-1), ln(0), asin(2), ... */
    KC_ERR_OVERFLOW,
    KC_ERR_NO_RATE,    /* currency rate not set (offline) */
    KC_ERR_RANGE       /* bad category/unit index, empty expr */
} kc_err;

typedef struct {
    double memory;         /* mc/m+/m-/mr register */
    bool deg;              /* true: degrees, false: radians */
} calc_state;

void calc_state_init(calc_state *st);

/* Evaluate a standard/scientific expression. On KC_OK, *out
 * holds the result. `st` supplies Deg/Rad and the memory
 * register (the token "M" reads it). */
kc_err calc_eval(const char *expr, const calc_state *st, double *out);
/* Japanese message for an error code (static storage). */
const char *calc_err_str(kc_err e);

/* Format a result the way the reference calculator does:
 * integers print without a fraction, other values with up to
 * 12 significant digits, very large/small magnitudes in
 * exponent form, -0 prints as 0. */
void calc_format(double v, char *out, size_t cap);

/* Memory keys (operate on the state register). */
void calc_mem_clear(calc_state *st);
void calc_mem_add(calc_state *st, double v);
void calc_mem_sub(calc_state *st, double v);

/* ---------------- programmer mode ---------------- */

/* Evaluate a programmer expression. Literals are read in
 * `base` (2/8/10/16; hex digits A-F accepted when base is 16).
 * Ops: + - * / (truncating), AND OR XOR NOR, unary NOT and ~,
 * << >>, parentheses. Division by zero -> KC_ERR_DIVZERO. */
kc_err calc_prog_eval(const char *expr, int base, uint64_t *out);
/* Render a value in a base (2/8/10/16), no prefix. */
void calc_prog_format(uint64_t v, int base, char *out, size_t cap);
/* 64-bit binary string grouped by 8 with spaces (for the bit
 * display; the UI lets the user flip bits by position). */
void calc_prog_bits(uint64_t v, char *out, size_t cap);
/* ASCII/Unicode readout for a value: "'A' (U+0041)" style, or
 * a description for control codes. Static buffer NOT used;
 * writes into out. */
void calc_prog_char_info(uint64_t v, char *out, size_t cap);

/* ---------------- conversion ---------------- */

size_t calc_conv_categories(void);
const char *calc_conv_category_name(size_t cat);
size_t calc_conv_units(size_t cat);
const char *calc_conv_unit_name(size_t cat, size_t unit);
bool calc_conv_is_currency(size_t cat);
/* Set a manual currency rate: 1 unit of `unit_name` (e.g.
 * "USD") equals `per_jpy` JPY. Rates live in the state struct
 * the caller passes to calc_convert_ex; the plain calc_convert
 * has no rates and returns KC_ERR_NO_RATE for currency. */
typedef struct {
    char unit[8];
    double per_jpy;
} calc_rate;
kc_err calc_convert(size_t cat, size_t from, size_t to, double v,
                    double *out);
kc_err calc_convert_ex(size_t cat, size_t from, size_t to, double v,
                       const calc_rate *rates, size_t n_rates,
                       double *out);

/* ---------------- history tape ---------------- */

typedef struct kc_hist kc_hist;
/* Store dir holds history.txt ("expr = result" lines). */
kc_hist *kc_hist_open(const char *root);
void kc_hist_close(kc_hist *h);
size_t kc_hist_count(const kc_hist *h);
/* Entry i, oldest first: "expr = result" (store-owned). */
const char *kc_hist_at(const kc_hist *h, size_t i);
/* The expression part of entry i (before " = "). */
void kc_hist_expr_at(const kc_hist *h, size_t i, char *out, size_t cap);
void kc_hist_add(kc_hist *h, const char *expr, const char *result);
void kc_hist_clear(kc_hist *h);

#endif /* CALCMODEL_H */
