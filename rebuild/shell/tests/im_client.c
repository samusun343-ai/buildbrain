/* im_client - WM client driven by test_session's OSK scenarios
 * (and by the screenshot harness): a toolkit window with a text
 * field and a button, speaking a stdout line protocol:
 *   READY <win-id>          after connecting
 *   FIELD <x> <y> <w> <h>   field rect in content coords
 *                           (printed at start and on resize)
 *   BTNRECT <x> <y> <w> <h> button rect, printed with FIELD
 *   TEXT <text>             the field's text after each change
 *   COMP <text>             a composition update ("" = cleared)
 *   BTN                     the button was activated
 *   RESIZED <w> <h>         the window was resized
 *   CLOSED                  the server asked us to close; exit 0
 *
 * It is a correct IM client, in the same shape as
 * clients/wm_sample_tk.c: events are translated to tk_event and
 * fed to the root, compositions become TK_EV_COMPOSITION, and
 * bd_wm_client_im_sync() runs after every event and layout so
 * the shell's on-screen keyboard tracks the field's focus.
 *
 * usage: im_client <sock> [w h]
 */
#include "wm.h"
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bd_wm_client *cl;
static tk_root *root;
static tk_widget *field;
static tk_widget *btn;
static bool dirty = true;

static void print_field(void)
{
    tk_rect r = tk_widget_rect(field);
    printf("FIELD %d %d %d %d\n", r.x, r.y, r.w, r.h);
    tk_rect b = tk_widget_rect(btn);
    printf("BTNRECT %d %d %d %d\n", b.x, b.y, b.w, b.h);
    fflush(stdout);
}

static void on_field_change(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    char buf[256];
    tk_textfield_text(field, buf, sizeof(buf));
    printf("TEXT %s\n", buf);
    fflush(stdout);
    dirty = true;
}

static void on_button(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    printf("BTN\n");
    fflush(stdout);
    dirty = true;
}

static void draw_and_commit(void)
{
    tk_canvas *cv = bd_wm_client_canvas(cl);
    if (!cv) return;
    tk_canvas_clear(cv, (tk_color){ 235, 238, 242, 255 });
    tk_root_paint(root, cv, tk_theme_default());
    tk_rect full = { 0, 0, bd_wm_client_width(cl),
                     bd_wm_client_height(cl) };
    if (bd_wm_client_commit(cl, &full, 1) == 0) dirty = false;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    int req_w = argc > 3 ? atoi(argv[2]) : 400;
    int req_h = argc > 3 ? atoi(argv[3]) : 300;

    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);

    cl = bd_wm_client_open(argv[1], "IM App", req_w, req_h);
    if (!cl) return 3;
    int w = bd_wm_client_width(cl), h = bd_wm_client_height(cl);

    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 10);
    field = tk_textfield_new("");
    tk_textfield_set_placeholder(field, "入力欄");
    tk_textfield_set_on_change(field, on_field_change, NULL);
    btn = tk_button_new("ボタン");
    tk_button_set_on_click(btn, on_button, NULL);
    tk_widget *lbl = tk_label_new("IM クライアント");
    tk_add_child(box, field);
    tk_add_child(box, btn);
    tk_add_child(box, lbl);
    root = tk_root_new(box);
    tk_root_layout(root, w, h);

    printf("READY %u\n", bd_wm_client_id(cl));
    fflush(stdout);
    print_field();
    draw_and_commit();

    for (;;) {
        if (dirty && bd_wm_client_canvas(cl)) draw_and_commit();
        bd_wm_client_event ev;
        int rc = bd_wm_client_poll(cl, &ev, 50);
        if (rc < 0) break;
        if (rc == 0) continue;
        tk_event tev = {0};
        switch (ev.type) {
        case BD_WM_CEV_CLOSE:
            printf("CLOSED\n");
            fflush(stdout);
            tk_root_free(root);
            tk_widget_free(box);
            bd_wm_client_close(cl);
            return 0;
        case BD_WM_CEV_RESIZED:
            tk_root_layout(root, ev.w, ev.h);
            (void)bd_wm_client_im_sync(cl, root);
            printf("RESIZED %d %d\n", ev.w, ev.h);
            fflush(stdout);
            print_field();
            dirty = true;
            break;
        case BD_WM_CEV_COMPOSITION:
            tev.type = TK_EV_COMPOSITION;
            tev.text = ev.comp[0] ? ev.comp : NULL;
            tk_root_event(root, &tev);
            printf("COMP %s\n", ev.comp);
            fflush(stdout);
            dirty = true;
            break;
        case BD_WM_CEV_POINTER_DOWN:
        case BD_WM_CEV_POINTER_UP:
        case BD_WM_CEV_POINTER_MOVE:
            tev.type = ev.type == BD_WM_CEV_POINTER_DOWN ? TK_EV_POINTER_DOWN :
                       ev.type == BD_WM_CEV_POINTER_UP ? TK_EV_POINTER_UP :
                       TK_EV_POINTER_MOVE;
            tev.x = ev.x; tev.y = ev.y;
            tk_root_event(root, &tev);
            dirty = true;
            break;
        case BD_WM_CEV_KEY_DOWN:
        case BD_WM_CEV_KEY_UP:
            tev.type = ev.type == BD_WM_CEV_KEY_DOWN ? TK_EV_KEY_DOWN :
                       TK_EV_KEY_UP;
            tev.key = (tk_key)ev.key;
            tev.modifiers = ev.mods;
            tev.text = ev.text[0] ? ev.text : NULL;
            tk_root_event(root, &tev);
            dirty = true;
            break;
        default:
            break;
        }
        if (ev.type == BD_WM_CEV_POINTER_DOWN ||
            ev.type == BD_WM_CEV_POINTER_UP ||
            ev.type == BD_WM_CEV_KEY_DOWN ||
            ev.type == BD_WM_CEV_FOCUS_IN || ev.type == BD_WM_CEV_FOCUS_OUT)
            (void)bd_wm_client_im_sync(cl, root);
    }
    tk_root_free(root);
    tk_widget_free(box);
    bd_wm_client_close(cl);
    return 1;
}
