/* test_calc_ui - bd-calc UI drive tests (headless).
 *
 * Drives the real UI through calc_app_press() (real pointer
 * events at the buttons' rects): basic arithmetic, chaining,
 * AC/C two-stage behaviour, scientific functions, 2nd face swap,
 * Deg/Rad checkbox, memory keys, programmer mode in hex,
 * conversion incl. the offline currency rule, the history tape,
 * and the damage discipline (pressing '=' must produce damage
 * and change pixels).
 */
#define _POSIX_C_SOURCE 200809L
#include "bd_calc.h"
#include "bdstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_str(const char *what, const char *got, const char *want)
{
    if (got && want && strcmp(got, want) == 0) {
        passes++; printf("PASS %s -> %s\n", what, got);
    } else {
        fails++;
        printf("FAIL %s: got %s, want %s\n", what,
               got ? got : "(null)", want ? want : "(null)");
    }
}
static void press(calc_app *app, const char *label)
{
    char what[64];
    snprintf(what, sizeof(what), "press %s", label);
    check_true(what, calc_app_press(app, label) == 0);
}
static void expr_is(calc_app *app, const char *want)
{
    char buf[1024];
    calc_app_expr(app, buf, sizeof(buf));
    check_str("expr", buf, want);
}
static void result_is(calc_app *app, const char *want)
{
    char buf[256];
    calc_app_result(app, buf, sizeof(buf));
    check_str("result", buf, want);
}

int main(void)
{
    char tmpl[] = "/tmp/bdcalc-ui-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    calc_app *app = calc_app_new(tmpl, 480, 800);
    check_true("U build", app != NULL);
    check_true("U default tab standard", calc_app_tab(app) == 0);

    /* ---- basic: 2+3x4 = 14 via real key presses ---- */
    press(app, "2"); press(app, "+"); press(app, "3");
    press(app, "×"); press(app, "4");
    expr_is(app, "2+3*4");
    press(app, "=");
    result_is(app, "= 14");
    expr_is(app, "14");                    /* chaining */
    check_true("U history 1 row", calc_app_history_rows(app) == 1);
    /* continue from result: +6 = 20 */
    press(app, "+"); press(app, "6"); press(app, "=");
    result_is(app, "= 20");
    /* digit after '=' starts fresh */
    press(app, "5");
    expr_is(app, "5");
    /* AC/C: first press deletes a token, second clears */
    press(app, "+"); press(app, "3");
    expr_is(app, "5+3");
    press(app, "AC/C");
    expr_is(app, "5+");
    press(app, "AC/C");
    expr_is(app, "5");
    press(app, "AC/C");
    expr_is(app, "0");   /* the display floors at the idle "0",
                          * never blank (F-16) */
    /* division by zero shows the Japanese error, no history */
    size_t h_before = calc_app_history_rows(app);
    press(app, "1"); press(app, "÷"); press(app, "0"); press(app, "=");
    char rbuf[256];
    calc_app_result(app, rbuf, sizeof(rbuf));
    check_true("U div0 error shown", strstr(rbuf, "0") != NULL &&
               calc_app_history_rows(app) == h_before);
    press(app, "AC/C");

    /* ---- damage discipline: '=' repaints ---- */
    calc_app_set_expr(app, "6*7");
    tk_canvas *cv = tk_canvas_new(480, 800);
    const tk_theme *th = tk_theme_default();
    tk_root_paint(calc_app_root(app), cv, th);
    uint32_t before[480 * 800];
    memcpy(before, tk_canvas_pixels(cv), sizeof(before));
    press(app, "=");
    int dmg = tk_root_paint_damage(calc_app_root(app), cv, th);
    check_true("U '=' produced damage", dmg > 0);
    check_true("U '=' changed pixels",
               memcmp(before, tk_canvas_pixels(cv), sizeof(before)) != 0);
    result_is(app, "= 42");
    tk_canvas_free(cv);

    /* ---- scientific ---- */
    calc_app_set_tab(app, 1);
    check_true("U tab sci", calc_app_tab(app) == 1);
    calc_app_set_expr(app, "");
    press(app, "sin"); press(app, "9"); press(app, "0"); press(app, ")");
    expr_is(app, "sin(90)");
    press(app, "=");
    result_is(app, "= 1");
    /* Deg default; memory: mc, 5 m+, AC, mr */
    calc_app_set_expr(app, "");
    press(app, "mc");
    check_true("U mem cleared", calc_app_memory(app) == 0);
    press(app, "5"); press(app, "m+");
    check_true("U mem 5", calc_app_memory(app) == 5);
    press(app, "AC/C");
    press(app, "mr");
    expr_is(app, "5");
    press(app, "AC/C");
    /* 2nd face: sin becomes sin-1 */
    check_true("U not second", !calc_app_second(app));
    press(app, "2nd");
    check_true("U second on", calc_app_second(app));
    press(app, "sin⁻¹"); press(app, "1"); press(app, ")");
    expr_is(app, "asin(1)");
    press(app, "=");
    result_is(app, "= 90");                /* Deg */
    press(app, "2nd");
    check_true("U second off", !calc_app_second(app));
    /* factorial + percent through keys */
    calc_app_set_expr(app, "");
    press(app, "5"); press(app, "x!");
    expr_is(app, "5!");
    press(app, "=");
    result_is(app, "= 120");

    /* ---- programmer (hex) ---- */
    calc_app_set_tab(app, 2);
    check_true("U prog base 16", calc_app_prog_base(app) == 16);
    press(app, "F"); press(app, "F"); press(app, "+");
    press(app, "1"); press(app, "=");
    char dbuf[128];
    calc_app_prog_display(app, dbuf, sizeof(dbuf));
    check_str("prog FF+1", dbuf, "100");
    char bbuf[128];
    calc_app_prog_bits(app, bbuf, sizeof(bbuf));
    check_true("prog bits end 1 00000000",
               strcmp(bbuf + strlen(bbuf) - 17, "00000001 00000000") == 0);
    press(app, "AC");
    calc_app_prog_display(app, dbuf, sizeof(dbuf));
    check_str("prog AC", dbuf, "0");
    calc_app_prog_set_base(app, 10);
    check_true("prog base 10", calc_app_prog_base(app) == 10);

    /* ---- conversion ---- */
    calc_app_set_tab(app, 3);
    calc_app_conv_set_category(app, 0);           /* length */
    calc_app_conv_set_units(app, 3, 2);            /* km -> m */
    calc_app_conv_set_value(app, "1.5");
    check_true("conv km->m ok", calc_app_convert_now(app) == KC_OK);
    char cbuf[256];
    calc_app_conv_result(app, cbuf, sizeof(cbuf));
    check_true("conv result 1500", strstr(cbuf, "1500") != NULL);
    calc_app_conv_set_category(app, 4);            /* temperature */
    calc_app_conv_set_units(app, 0, 1);            /* C -> F */
    calc_app_conv_set_value(app, "100");
    check_true("conv C->F ok", calc_app_convert_now(app) == KC_OK);
    calc_app_conv_result(app, cbuf, sizeof(cbuf));
    check_true("conv result 212", strstr(cbuf, "212") != NULL);
    /* currency: no rate -> NO_RATE error surfaced */
    calc_app_conv_set_category(app, 10);
    calc_app_conv_set_units(app, 1, 0);            /* USD -> JPY */
    calc_app_conv_set_value(app, "3");
    check_true("conv currency no rate",
               calc_app_convert_now(app) == KC_ERR_NO_RATE);
    check_true("rate set", calc_app_set_rate(app, "USD", 150) == 0);
    check_true("conv currency with rate",
               calc_app_convert_now(app) == KC_OK);
    calc_app_conv_result(app, cbuf, sizeof(cbuf));
    check_true("conv result 450", strstr(cbuf, "450") != NULL);

    /* history load: row 0 is the newest entry */
    calc_app_set_tab(app, 0);
    check_true("hist load", calc_app_history_load(app, 0) == 0);
    char ebuf[1024];
    calc_app_expr(app, ebuf, sizeof(ebuf));
    check_true("hist expr loaded", strlen(ebuf) > 0);

    calc_app_free(app);

    /* rates persist across reopen */
    app = calc_app_new(tmpl, 480, 800);
    calc_app_set_tab(app, 3);
    calc_app_conv_set_category(app, 10);
    calc_app_conv_set_units(app, 1, 0);
    calc_app_conv_set_value(app, "1");
    check_true("rate persisted", calc_app_convert_now(app) == KC_OK);
    calc_app_free(app);

    /* hardware keyboard drives the engine (F-16: the v8
     * display booted blank - no idle "0" - and typed digits
     * went nowhere because nothing held focus). Fresh app:
     * the display reads "0", keys 7/8 land, Enter evaluates,
     * Escape returns to the idle "0". */
    app = calc_app_new(tmpl, 850, 368);
    check_true("kbd app", app != NULL);
    expr_is(app, "0");
    {
        tk_root *rt = calc_app_root(app);
        tk_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_KEY_DOWN;
        ev.text = "7";
        tk_root_event(rt, &ev);
        ev.text = "8";
        tk_root_event(rt, &ev);
        expr_is(app, "78");
        ev.text = NULL;
        ev.key = TK_KEY_ENTER;
        tk_root_event(rt, &ev);
        result_is(app, "= 78");
        ev.key = TK_KEY_ESCAPE;
        tk_root_event(rt, &ev);
        expr_is(app, "0");
        result_is(app, "");
    }
    calc_app_free(app);

    printf("test_calc_ui: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
