/* wm_sample_tk - sample WM client built on toolkit v2: a label, a
 * button that counts presses, a checkbox and a text field, laid
 * out in a vertical box and painted straight into the WM
 * shared-memory buffer through tk_canvas_wrap (zero copy).
 * Events from the WM are translated into tk_event and fed to the
 * toolkit root.
 *
 * The text field makes this the reference client for the
 * input-method path: after every event (and after a resize) the
 * client calls bd_wm_client_im_sync(), so the shell raises its
 * on-screen keyboard while the field has focus, and compositions
 * arriving as BD_WM_CEV_COMPOSITION are fed to the root as
 * TK_EV_COMPOSITION.
 *
 * usage: wm_sample_tk <sock> <title> <w> <h>
 */
#include "wm.h"
#include "tk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int presses;
static tk_widget *count_label;

static void on_press(tk_widget *w, void *ud)
{
    (void)w; (void)ud;
    presses++;
    char buf[64];
    snprintf(buf, sizeof(buf), "押した回数: %d", presses);
    tk_label_set_text(count_label, buf);
}

int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    const char *sock = argv[1], *title = argv[2];
    int w = atoi(argv[3]), h = atoi(argv[4]);

    bd_wm_client *cl = bd_wm_client_open(sock, title, w, h);
    if (!cl) return 1;
    w = bd_wm_client_width(cl);
    h = bd_wm_client_height(cl);

    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 10);
    tk_widget *title_lbl = tk_label_new(title);
    count_label = tk_label_new("押した回数: 0");
    tk_widget *btn = tk_button_new("押す");
    tk_button_set_on_click(btn, on_press, NULL);
    tk_widget *chk = tk_checkbox_new("有効", true);
    tk_widget *tf = tk_textfield_new("");
    tk_textfield_set_placeholder(tf, "入力欄（OSK 対象）");
    tk_add_child(box, title_lbl);
    tk_add_child(box, count_label);
    tk_add_child(box, btn);
    tk_add_child(box, chk);
    tk_add_child(box, tf);
    tk_root *root = tk_root_new(box);
    tk_root_layout(root, w, h);

    bool dirty = true;
    for (;;) {
        if (dirty) {
            tk_canvas *cv = bd_wm_client_canvas(cl);
            if (cv) {
                tk_canvas_clear(cv, (tk_color){ 235, 238, 242, 255 });
                tk_root_paint(root, cv, tk_theme_default());
                tk_rect full = { 0, 0, bd_wm_client_width(cl),
                                 bd_wm_client_height(cl) };
                if (bd_wm_client_commit(cl, &full, 1) == 0) dirty = false;
            }
        }
        bd_wm_client_event ev;
        int rc = bd_wm_client_poll(cl, &ev, 50);
        if (rc < 0) break;
        if (rc == 0) continue;
        tk_event tev = {0};
        switch (ev.type) {
        case BD_WM_CEV_CLOSE:
            tk_root_free(root);
            tk_widget_free(box);
            bd_wm_client_close(cl);
            return 0;
        case BD_WM_CEV_RESIZED:
            tk_root_layout(root, ev.w, ev.h);
            (void)bd_wm_client_im_sync(cl, root);
            dirty = true;
            break;
        case BD_WM_CEV_COMPOSITION:
            tev.type = TK_EV_COMPOSITION;
            tev.text = ev.comp[0] ? ev.comp : NULL;
            tk_root_event(root, &tev);
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
        /* focus may have moved (pointer/key): keep the shell's
         * idea of our text-input state current */
        if (ev.type == BD_WM_CEV_POINTER_DOWN ||
            ev.type == BD_WM_CEV_POINTER_UP ||
            ev.type == BD_WM_CEV_KEY_DOWN || ev.type == BD_WM_CEV_FOCUS_IN ||
            ev.type == BD_WM_CEV_FOCUS_OUT)
            (void)bd_wm_client_im_sync(cl, root);
    }
    tk_root_free(root);
    tk_widget_free(box);
    bd_wm_client_close(cl);
    return 0;
}
