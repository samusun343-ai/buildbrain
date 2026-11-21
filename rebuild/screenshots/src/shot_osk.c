/* shot_osk - screenshots of OSK v2 (toolkit widget + libbrainime).
 *
 * Uses the same fixture shape as osk/tests/test_osk.c: a fixed
 * container with a TextField on top and the OSK below, the real
 * libbrainime engine (data/brainux3.bid3) attached through the
 * product osk_brainime backend. Romaji is fed through
 * tk_osk_input_romaji() - the same entry point the QWERTY keys
 * use - so the composition in the field and the candidate bar are
 * genuine conversion output.
 *   06 QWERTY + IME conversion in progress (800x480)
 *   07 symbol page (800x480)
 *   08 QWERTY at the small panel size (480x272)
 */
#define _POSIX_C_SOURCE 200809L
#include "tk.h"
#include "osk.h"
#include "brainime.h"
#include "osk_brainime.h"
#include "shot_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    tk_widget *top, *tf, *osk;
    tk_root *root;
} ui;

static ui ui_new(bi_engine *eng, int w, int h, int tf_h)
{
    ui u;
    u.top = tk_fixed_new();
    u.tf = tk_textfield_new("");
    tk_set_fixed_rect(u.tf, (tk_rect){ 0, 0, w, tf_h });
    u.osk = tk_osk_new();
    tk_set_fixed_rect(u.osk, (tk_rect){ 0, tf_h, w, h - tf_h });
    tk_add_child(u.top, u.tf);
    tk_add_child(u.top, u.osk);
    tk_osk_attach(u.osk, u.tf);
    if (eng) tk_osk_set_backend(u.osk, osk_brainime_backend(), eng);
    u.root = tk_root_new(u.top);
    tk_root_layout(u.root, w, h);
    tk_focus(u.root, u.tf);
    return u;
}

static void ui_free(ui *u)
{
    tk_root_free(u->root);
    tk_widget_free(u->top);
}

int main(void)
{
    shot_install_font();

    bi_engine *eng = bi_engine_open("libbrainime/data/brainux3.bid3");
    if (!eng) fprintf(stderr, "shot_osk: bid3 engine failed to open\n");

    /* 06: QWERTY with a live conversion (candidates in the bar) */
    {
        ui u = ui_new(eng, 800, 480, 52);
        tk_osk_input_romaji(u.osk, "kyouhayoitenkidesu");
        printf("osk conversion: %s | candidates: %d\n",
               tk_osk_conversion(u.osk) ? tk_osk_conversion(u.osk) : "(null)",
               tk_osk_candidate_count(u.osk));
        shot_root("screenshots/raw/06_osk_qwerty_ime.raw", u.root, 800, 480);
        ui_free(&u);
    }

    /* 07: symbol page (the physical keyboard's superset) */
    {
        ui u = ui_new(eng, 800, 480, 52);
        tk_osk_set_mode(u.osk, OSK_MODE_SYMBOL);
        tk_root_layout(u.root, 800, 480);
        shot_root("screenshots/raw/07_osk_symbols.raw", u.root, 800, 480);
        ui_free(&u);
    }

    /* 08: QWERTY at 480x272 (10-column rows are width-limited) */
    {
        ui u = ui_new(eng, 480, 272, 40);
        tk_osk_input_romaji(u.osk, "tegami");
        shot_root("screenshots/raw/08_osk_qwerty_480x272.raw", u.root,
                  480, 272);
        ui_free(&u);
    }

    if (eng) bi_engine_close(eng);
    return 0;
}
