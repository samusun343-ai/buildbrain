/* sdk_settings.c - "third-party" sample app for libbrainui2 (M3).
 *
 * Built ONLY against the installed public headers (tk.h, textcore.h)
 * and the shared library, located via pkg-config (see the Makefile
 * sdk-check target). It deliberately uses nothing from src/ - if
 * this file cannot express a realistic settings screen, the public
 * API is wrong, and M3 fixed what it found (recorded in DESIGN
 * section 9/12).
 *
 * The app: a settings screen (Switch, editable ComboBox, SpinButton,
 * a multi-line TextView, a button that opens a modal Dialog) driven
 * headlessly through the damage pipeline: paint_damage ->
 * tk_fb_flush_damage into a memory framebuffer, printing the byte
 * counts so the damage path is measurable end to end.
 */
#include "tk.h"
#include "textcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
                   failures++; } \
} while (0)

typedef struct {
    tk_root *root;
    tk_widget *dlg;
    int dialog_ok;
} app_ctx;

static void on_dialog_ok(tk_widget *w, void *ud)
{
    (void)w;
    app_ctx *app = ud;
    app->dialog_ok++;
    tk_dialog_close(app->root, app->dlg);
}

static void on_show_dialog(tk_widget *w, void *ud)
{
    (void)w;
    app_ctx *app = ud;
    tk_dialog_show(app->root, app->dlg);
}

static void send_ev(tk_root *r, tk_event *ev) { (void)tk_root_event(r, ev); }

static void send_click(tk_root *r, int x, int y)
{
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_DOWN; ev.x = x; ev.y = y;
    send_ev(r, &ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_POINTER_UP; ev.x = x; ev.y = y;
    send_ev(r, &ev);
}

static void send_key(tk_root *r, tk_key key)
{
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_KEY_DOWN; ev.key = key;
    send_ev(r, &ev);
}

static void send_text(tk_root *r, const char *text)
{
    tk_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT; ev.text = text;
    send_ev(r, &ev);
}

static int center_x(tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    return r.x + r.w / 2;
}
static int center_y(tk_widget *w)
{
    tk_rect r = tk_widget_rect(w);
    return r.y + r.h / 2;
}

int main(void)
{
    tk_theme_set_default(&tk_theme_v2); /* UI v2 product default */
    /* Font: the bundled subset if installed, else an explicit path
     * (staging dir / dev tree), else the default search. */
    tk_font *font = tk_font_load_bundled();
    if (!font) font = tk_font_load(
        "usr/local/share/fonts/libbrainui2/brainui-jp-subset.ttf", 0);
    if (!font) font = tk_font_load("fonts/brainui-jp-subset.ttf", 0);
    if (!font) font = tk_font_load_default();
    CHECK(font != NULL);
    if (font) tk_font_install(font);

    /* ---- build the settings screen ---- */
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 8);
    tk_widget *title = tk_label_new("設定");
    tk_widget *sw = tk_switch_new("Wi-Fi", false);
    tk_widget_set_tooltip(sw, "無線LANのオン/オフ");
    const char *langs[] = { "日本語", "English", "Deutsch" };
    tk_widget *combo = tk_combobox_new();
    tk_combobox_set_items(combo, langs, 3);
    tk_combobox_set_editable(combo, true);
    tk_combobox_select(combo, 0);
    tk_widget *spin = tk_spinbutton_new(5, 0, 100, 1);
    tk_widget *tv = tk_textview_new("");
    tk_widget *btn = tk_button_new("情報");
    tk_add_child(box, title);
    tk_add_child(box, sw);
    tk_add_child(box, combo);
    tk_add_child(box, spin);
    tk_add_child(box, tv);
    tk_add_child(box, btn);

    app_ctx app = { NULL, NULL, 0 };
    tk_widget *dlg = tk_dialog_new("情報");
    tk_widget *ok = tk_button_new("OK");
    tk_button_set_on_click(ok, on_dialog_ok, &app);
    tk_add_child(dlg, tk_label_new("Brainux 設定サンプル"));
    tk_add_child(dlg, ok);
    app.dlg = dlg;

    tk_root *root = tk_root_new(box);
    app.root = root;
    tk_button_set_on_click(btn, on_show_dialog, &app);

    /* M19: the stage height grew 272 -> 360. The M3-era
     * 272px stage no longer fits the stack at the current
     * documented row height (control_h 56px since M7's
     * 187ppi rescale): the last row landed at y=260..316,
     * outside the root, so its click missed and the dialog
     * flow failed; the switch-row damage fraction also
     * crossed the <1/5 premise (56px row + 2px inflate =
     * 21.5% of 272). At 360 the whole stack is on stage
     * and the row is ~16% of the frame again. The library
     * behaviour in both cases was the documented one
     * (damage = the toggled row only; hit testing
     * respects the root bounds) - AUDIT SS17. */
    tk_root_layout(root, 480, 360);
    tk_canvas *canvas = tk_canvas_new(480, 360);
    tk_fb *fb = tk_fb_open_mem(480, 360, 16);
    CHECK(canvas != NULL && fb != NULL);
    const tk_theme *theme = tk_theme_default();

    /* ---- first frame: full paint + full transfer ---- */
    int painted = tk_root_paint_damage(root, canvas, theme);
    size_t full_bytes = tk_fb_flush_damage(fb, canvas);
    printf("first frame: painted=%d rects, transferred=%zu bytes\n",
           painted, full_bytes);
    CHECK(full_bytes == (size_t)480 * 360 * 2);

    /* ---- toggle the switch: only its rect is repainted/shipped -- */
    send_click(root, center_x(sw), center_y(sw));
    CHECK(tk_switch_on(sw));
    painted = tk_root_paint_damage(root, canvas, theme);
    size_t sw_bytes = tk_fb_flush_damage(fb, canvas);
    printf("switch toggle: painted=%d rects, transferred=%zu bytes "
           "(%.1f%% of full)\n", painted, sw_bytes,
           100.0 * (double)sw_bytes / (double)full_bytes);
    /* the switch row spans the full layout width, so its damage is
     * one wide row: ~16% of the frame, not a full repaint */
    CHECK(sw_bytes > 0 && sw_bytes < full_bytes / 5);

    /* ---- editable combo: type a custom value, commit with Enter - */
    {
        tk_rect cr = tk_widget_rect(combo);
        send_click(root, cr.x + 10, cr.y + cr.h / 2); /* text zone */
        CHECK(tk_focused(root) == combo);
        /* the click put the caret at the start; go to the end and
         * backspace the synced item text away (the minimal editor
         * has no select-all - noted in DESIGN section 12) */
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 16; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "カスタム");
        send_key(root, TK_KEY_ENTER);
        printf("combo after typing: text=%s selected=%d\n",
               tk_combobox_text(combo), tk_combobox_selected(combo));
        CHECK(strcmp(tk_combobox_text(combo), "カスタム") == 0);
        CHECK(tk_combobox_selected(combo) == -1);
    }

    /* ---- spinbutton: replace the value by typing ---- */
    {
        tk_rect sr = tk_widget_rect(spin);
        send_click(root, sr.x + 10, sr.y + sr.h / 2); /* text zone */
        CHECK(tk_focused(root) == spin);
        send_key(root, TK_KEY_END);
        for (int i = 0; i < 8; i++) send_key(root, TK_KEY_BACKSPACE);
        send_text(root, "42");
        send_key(root, TK_KEY_ENTER);
        printf("spin after typing: value=%g\n",
               (double)tk_spinbutton_value(spin));
        CHECK(tk_spinbutton_value(spin) == 42.0f);
    }

    /* ---- textview: click in and type ---- */
    {
        tk_rect vr = tk_widget_rect(tv);
        send_click(root, vr.x + 12, vr.y + 8);
        CHECK(tk_focused(root) == tv);
        send_text(root, "hello");
        char buf[64];
        tk_textview_text(tv, buf, sizeof(buf));
        CHECK(strcmp(buf, "hello") == 0);
    }

    /* ---- tooltip: rest the pointer on the switch ---- */
    {
        tk_root_tick(root, 1000);
        tk_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TK_EV_POINTER_MOVE;
        ev.x = center_x(sw); ev.y = center_y(sw);
        send_ev(root, &ev);
        tk_root_tick(root, 1000 + TK_TOOLTIP_DELAY_MS - 1);
        CHECK(!tk_root_tooltip_visible(root));
        tk_root_tick(root, 1000 + TK_TOOLTIP_DELAY_MS);
        CHECK(tk_root_tooltip_visible(root));
        printf("tooltip shown at (%d,%d)\n",
               tk_root_tooltip_rect(root).x, tk_root_tooltip_rect(root).y);
    }

    /* ---- dialog: open via the button, close via its OK button -- */
    send_click(root, center_x(btn), center_y(btn));
    CHECK(tk_dialog_is_shown(root, dlg));
    (void)tk_root_paint_damage(root, canvas, theme);
    (void)tk_fb_flush_damage(fb, canvas);
    send_click(root, center_x(ok), center_y(ok));
    CHECK(app.dialog_ok == 1);
    CHECK(!tk_dialog_is_shown(root, dlg));

    tk_fb_close(fb);
    tk_canvas_free(canvas);
    tk_widget_free(dlg);
    tk_root_free(root);
    tk_widget_free(box);
    if (font) tk_font_free(font);

    if (failures) {
        printf("sdk_settings: %d FAILURES\n", failures);
        return 1;
    }
    printf("sdk_settings: SDK-OK (public API only)\n");
    return 0;
}
