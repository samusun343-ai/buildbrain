/* test_tk.c - headless unit tests for the tk toolkit core:
 * layout coordinates, click dispatch + bubbling, pointer capture,
 * focus traversal, TextField editing, ListView, ScrollView, Radio. */
#include "tk.h"
#include <stdio.h>
#include <string.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int clicks;
static void on_click(tk_widget *w, void *ud)
{
    (void)w; (void)ud; clicks++;
}

static int changes;
static void on_change(tk_widget *w, void *ud)
{
    (void)w; (void)ud; changes++;
}

static void send_click(tk_root *r, int x, int y)
{
    tk_event dn = {0}, up = {0};
    dn.type = TK_EV_POINTER_DOWN; dn.x = x; dn.y = y;
    up.type = TK_EV_POINTER_UP; up.x = x; up.y = y;
    tk_root_event(r, &dn);
    tk_root_event(r, &up);
}

static void send_key(tk_root *r, tk_key key, const char *text, uint32_t mods)
{
    tk_event ev = {0};
    ev.type = TK_EV_KEY_DOWN; ev.key = key; ev.text = text;
    ev.modifiers = mods;
    tk_root_event(r, &ev);
}

int main(void)
{
    const tk_theme *theme = tk_theme_default();

    /* ============ layout: vertical box with flex ============ */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 8);
        tk_widget *a = tk_label_new("A");
        tk_widget *b = tk_button_new("B");
        tk_widget *c = tk_label_new("C");
        tk_set_flex(c, 1);
        tk_add_child(box, a);
        tk_add_child(box, b);
        tk_add_child(box, c);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 200);
        tk_rect ra = tk_widget_rect(a);
        tk_rect rb = tk_widget_rect(b);
        tk_rect rc = tk_widget_rect(c);
        /* padding 8, spacing 4: a at y=8, height = line height (21) */
        CHECK(ra.x == 8 && ra.y == 8 && ra.w == 184);
        CHECK(ra.h == tk_line_height(theme));
        CHECK(rb.y == 8 + ra.h + 4);
        CHECK(rb.h == theme->control_h);
        /* c is flex: takes the rest */
        CHECK(rc.y == rb.y + rb.h + 4);
        CHECK(rc.y + rc.h == 200 - 8);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ layout: horizontal box, center align ============ */
    {
        tk_widget *box = tk_box_new(TK_DIR_HORIZONTAL, 0, 0);
        tk_box_set_align(box, TK_ALIGN_CENTER);
        tk_widget *a = tk_label_new("Hi");  /* 2 chars * 8 = 16 wide */
        tk_add_child(box, a);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 100, 60);
        tk_rect ra = tk_widget_rect(a);
        CHECK(ra.w == 16);
        CHECK(ra.y == (60 - ra.h) / 2);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ fixed container ============ */
    {
        tk_widget *fx = tk_fixed_new();
        tk_widget *btn = tk_button_new("X");
        tk_set_fixed_rect(btn, (tk_rect){30, 40, 90, 36});
        tk_add_child(fx, btn);
        tk_root *root = tk_root_new(fx);
        tk_root_layout(root, 300, 200);
        tk_rect rb = tk_widget_rect(btn);
        CHECK(rb.x == 30 && rb.y == 40 && rb.w == 90 && rb.h == 36);
        tk_root_free(root);
        tk_widget_free(fx);
    }

    /* ============ click dispatch: button fires once per click ==== */
    {
        clicks = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *btn = tk_button_new("Press");
        tk_button_set_on_click(btn, on_click, NULL);
        tk_add_child(box, btn);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 100);
        tk_rect rb = tk_widget_rect(btn);
        send_click(root, rb.x + rb.w / 2, rb.y + rb.h / 2);
        CHECK(clicks == 1);
        /* click outside: no fire */
        send_click(root, 199, 99);
        CHECK(clicks == 1);
        /* down inside, up outside: no fire (capture + release check) */
        tk_event dn = {0}, up = {0};
        dn.type = TK_EV_POINTER_DOWN; dn.x = rb.x + 5; dn.y = rb.y + 5;
        up.type = TK_EV_POINTER_UP; up.x = 199; up.y = 99;
        tk_root_event(root, &dn);
        tk_root_event(root, &up);
        CHECK(clicks == 1);
        /* click also focuses the button */
        send_click(root, rb.x + 5, rb.y + 5);
        CHECK(tk_focused(root) == btn);
        CHECK(clicks == 2);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ checkbox toggling + radio group ============ */
    {
        changes = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *chk = tk_checkbox_new("agree", false);
        tk_checkbox_set_on_change(chk, on_change, NULL);
        tk_widget *r1 = tk_radio_new("one", 7, 1);
        tk_widget *r2 = tk_radio_new("two", 7, 2);
        tk_widget *r3 = tk_radio_new("other-group", 9, 3);
        tk_add_child(box, chk);
        tk_add_child(box, r1);
        tk_add_child(box, r2);
        tk_add_child(box, r3);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 200);
        tk_rect rc = tk_widget_rect(chk);
        send_click(root, rc.x + 5, rc.y + rc.h / 2);
        CHECK(tk_checkbox_checked(chk));
        CHECK(changes == 1);
        tk_rect rr2 = tk_widget_rect(r2);
        send_click(root, rr2.x + 5, rr2.y + rr2.h / 2);
        CHECK(tk_radio_selected(r2));
        CHECK(!tk_radio_selected(r1));
        tk_rect rr1 = tk_widget_rect(r1);
        send_click(root, rr1.x + 5, rr1.y + rr1.h / 2);
        CHECK(tk_radio_selected(r1));
        CHECK(!tk_radio_selected(r2));
        CHECK(!tk_radio_selected(r3)); /* untouched other group */
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ slider: drag with capture ============ */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *sl = tk_slider_new(0.0f);
        tk_add_child(box, sl);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 60);
        tk_rect rs = tk_widget_rect(sl);
        /* press at 75% across */
        tk_event dn = {0}, mv = {0}, up = {0};
        dn.type = TK_EV_POINTER_DOWN;
        dn.x = rs.x + (int)(rs.w * 0.75f); dn.y = rs.y + rs.h / 2;
        tk_root_event(root, &dn);
        CHECK(tk_slider_value(sl) > 0.70f && tk_slider_value(sl) < 0.80f);
        /* drag beyond the widget edge: capture keeps delivering */
        mv.type = TK_EV_POINTER_MOVE; mv.x = rs.x + rs.w + 40; mv.y = rs.y + rs.h / 2;
        tk_root_event(root, &mv);
        CHECK(tk_slider_value(sl) == 1.0f);
        up.type = TK_EV_POINTER_UP; up.x = mv.x; up.y = mv.y;
        tk_root_event(root, &up);
        /* keyboard adjust */
        CHECK(tk_focus(root, sl));
        send_key(root, TK_KEY_LEFT, NULL, 0);
        CHECK(tk_slider_value(sl) < 1.0f);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ focus traversal (Tab / Shift+Tab, wrap) ======== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *l = tk_label_new("not focusable");
        tk_widget *b1 = tk_button_new("b1");
        tk_widget *t1 = tk_textfield_new("");
        tk_widget *b2 = tk_button_new("b2");
        tk_set_enabled(b2, false); /* disabled: skipped */
        tk_widget *c1 = tk_checkbox_new("c1", false);
        tk_add_child(box, l);
        tk_add_child(box, b1);
        tk_add_child(box, t1);
        tk_add_child(box, b2);
        tk_add_child(box, c1);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 300);
        CHECK(tk_focus_next(root));
        CHECK(tk_focused(root) == b1);
        send_key(root, TK_KEY_TAB, NULL, 0);
        CHECK(tk_focused(root) == t1);
        send_key(root, TK_KEY_TAB, NULL, 0);
        CHECK(tk_focused(root) == c1);  /* b2 skipped */
        send_key(root, TK_KEY_TAB, NULL, 0);
        CHECK(tk_focused(root) == b1);  /* wraps */
        send_key(root, TK_KEY_TAB, NULL, TK_MOD_SHIFT);
        CHECK(tk_focused(root) == c1);  /* backwards */
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ TextField editing through tk events ============ */
    {
        changes = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *tf = tk_textfield_new("");
        tk_textfield_set_on_change(tf, on_change, NULL);
        tk_add_child(box, tf);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 300, 60);
        CHECK(tk_focus(root, tf));
        /* type "hello" via TEXT_INPUT + key text */
        tk_event ti = {0};
        ti.type = TK_EV_TEXT_INPUT; ti.text = "hello";
        tk_root_event(root, &ti);
        char buf[128];
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "hello") == 0);
        CHECK(changes >= 1);
        /* backspace via key */
        send_key(root, TK_KEY_BACKSPACE, NULL, 0);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "hell") == 0);
        /* cursor left + insert */
        send_key(root, TK_KEY_LEFT, NULL, 0);
        ti.text = "X";
        tk_root_event(root, &ti);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "helXl") == 0);
        /* select all + replace */
        send_key(root, TK_KEY_NONE, "a", TK_MOD_CTRL);
        ti.text = "bye";
        tk_root_event(root, &ti);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "bye") == 0);
        /* undo via Ctrl+Z */
        send_key(root, TK_KEY_NONE, "z", TK_MOD_CTRL);
        tk_textfield_text(tf, buf, sizeof(buf));
        /* undo of replace: first removes "bye" insert, second restores
         * the deleted selection */
        send_key(root, TK_KEY_NONE, "z", TK_MOD_CTRL);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "helXl") == 0);
        /* undo restored the selection too; collapse it before typing */
        tc_move(tk_textfield_buffer(tf), TC_MOVE_DOC_END, 1, false);
        /* Japanese input via TEXT_INPUT */
        ti.text = "あい";
        tk_root_event(root, &ti);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "helXlあい") == 0);
        /* composition does not alter the buffer until commit */
        tk_event comp = {0};
        comp.type = TK_EV_COMPOSITION; comp.text = "かんじ";
        tk_root_event(root, &comp);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "helXlあい") == 0);
        char compbuf[64];
        tc_composition(tk_textfield_buffer(tf), compbuf, sizeof(compbuf));
        CHECK(strcmp(compbuf, "かんじ") == 0);
        ti.text = "漢字";
        tk_root_event(root, &ti);
        tk_textfield_text(tf, buf, sizeof(buf));
        CHECK(strcmp(buf, "helXlあい漢字") == 0);
        CHECK(tc_composition(tk_textfield_buffer(tf), compbuf, sizeof(compbuf)) == 0);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ TextField clipboard through callbacks ========= */
    {
        extern char clip_dummy_anchor; /* not used; clipboard set below */
        (void)clip_dummy_anchor;
    }
    {
        static char store[256];
        /* reuse a tiny local clipboard via textcore directly */
        tk_widget *tf = tk_textfield_new("clip test");
        tc_buffer *b = tk_textfield_buffer(tf);
        /* select "clip" and copy using a lambda-free approach:
         * set an in-memory clipboard implemented in this file */
        extern void test_set_clip(tc_buffer *);
        test_set_clip(b);
        tc_move_to(b, 0, false);
        tc_move(b, TC_MOVE_CLUSTER, 1, true);
        tc_move(b, TC_MOVE_CLUSTER, 1, true);
        tc_move(b, TC_MOVE_CLUSTER, 1, true);
        tc_move(b, TC_MOVE_CLUSTER, 1, true);
        CHECK(tc_copy(b));
        tc_move_to(b, tc_length(b), false);
        CHECK(tc_paste(b));
        char buf[128];
        tc_text(b, buf, sizeof(buf));
        CHECK(strcmp(buf, "clip testclip") == 0);
        (void)store;
        tk_widget_free(tf);
    }

    /* ============ ListView: click + keyboard + activate ========= */
    {
        changes = 0;
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *lv = tk_listview_new();
        const char *items[] = { "alpha", "beta", "gamma", "delta" };
        tk_listview_set_items(lv, items, 4);
        tk_listview_set_on_select(lv, on_change, NULL);
        tk_add_child(box, lv);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 200, 200);
        tk_rect rl = tk_widget_rect(lv);
        /* click the third row */
        send_click(root, rl.x + 10, rl.y + theme->row_h * 2 + 4);
        CHECK(tk_listview_selected(lv) == 2);
        CHECK(changes == 1);
        /* keyboard: focus then Down */
        CHECK(tk_focus(root, lv));
        send_key(root, TK_KEY_DOWN, NULL, 0);
        CHECK(tk_listview_selected(lv) == 3);
        send_key(root, TK_KEY_UP, NULL, 0);
        CHECK(tk_listview_selected(lv) == 2);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ============ ScrollView: wheel scrolling + clipping ======== */
    {
        tk_widget *sv = tk_scrollview_new();
        tk_widget *inner = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        for (int i = 0; i < 20; i++) {
            char label[16];
            snprintf(label, sizeof(label), "row %d", i);
            tk_add_child(inner, tk_label_new(label));
        }
        tk_scrollview_set_child(sv, inner);
        tk_root *root = tk_root_new(sv);
        tk_root_layout(root, 200, 105); /* ~5 rows visible of 20 */
        CHECK(tk_scrollview_offset_y(sv) == 0);
        /* wheel down 3 rows */
        tk_event wh = {0};
        wh.type = TK_EV_WHEEL; wh.x = 10; wh.y = 10; wh.wheel_dy = 3;
        tk_root_event(root, &wh);
        CHECK(tk_scrollview_offset_y(sv) == 3 * theme->row_h);
        /* first child moved up accordingly */
        tk_widget *row0 = tk_child_at(inner, 0);
        CHECK(tk_widget_rect(row0).y == -3 * theme->row_h);
        /* scroll back to top, clamped */
        wh.wheel_dy = -100;
        tk_root_event(root, &wh);
        CHECK(tk_scrollview_offset_y(sv) == 0);
        /* paint into a canvas: does not crash, bg painted */
        tk_canvas *cv = tk_canvas_new(200, 105);
        tk_root_paint(root, cv, theme);
        tk_color px = tk_canvas_pixel(cv, 2, 2);
        CHECK(px.a == 255);
        tk_canvas_free(cv);
        tk_root_free(root);
        tk_widget_free(sv);
    }

    /* ============ round rect geometry (M4 regression) ==========
     * A fully rounded rect (radius == h/2: the slider thumb and
     * the switch track/knob shapes) must be a capsule: narrow at
     * the top/bottom rows, full width at the middle rows, and
     * symmetric. The old inset math inverted the corner distance
     * and drew an hourglass (full width at the edges, pinched at
     * the center rows) - visible in quick settings, the settings
     * app and the player. ======================================= */
    {
        tk_canvas *cv = tk_canvas_new(60, 40);
        tk_color bg = {255, 255, 255, 255}, fg = {10, 20, 30, 255};
        tk_canvas_clear(cv, bg);
        tk_canvas_fill_round_rect(cv, (tk_rect){10, 10, 36, 20}, 10, fg);
        /* top row (local y=0): inset 7 - corners empty, center full */
        CHECK(tk_canvas_pixel(cv, 10, 10).r == 255);
        CHECK(tk_canvas_pixel(cv, 45, 10).r == 255);
        CHECK(tk_canvas_pixel(cv, 16, 10).r == 255);
        CHECK(tk_canvas_pixel(cv, 17, 10).r == 10);
        CHECK(tk_canvas_pixel(cv, 28, 10).r == 10);
        CHECK(tk_canvas_pixel(cv, 38, 10).r == 10);
        CHECK(tk_canvas_pixel(cv, 39, 10).r == 255);
        /* local y=5: inset 1 */
        CHECK(tk_canvas_pixel(cv, 10, 15).r == 255);
        CHECK(tk_canvas_pixel(cv, 11, 15).r == 10);
        CHECK(tk_canvas_pixel(cv, 44, 15).r == 10);
        CHECK(tk_canvas_pixel(cv, 45, 15).r == 255);
        /* middle rows: full width (the hourglass pinched HERE) */
        CHECK(tk_canvas_pixel(cv, 10, 19).r == 10);
        CHECK(tk_canvas_pixel(cv, 45, 19).r == 10);
        CHECK(tk_canvas_pixel(cv, 10, 20).r == 10);
        CHECK(tk_canvas_pixel(cv, 45, 20).r == 10);
        /* bottom row mirrors the top */
        CHECK(tk_canvas_pixel(cv, 16, 29).r == 255);
        CHECK(tk_canvas_pixel(cv, 17, 29).r == 10);
        CHECK(tk_canvas_pixel(cv, 38, 29).r == 10);
        CHECK(tk_canvas_pixel(cv, 39, 29).r == 255);
        tk_canvas_free(cv);
    }
    {
        /* widget level: slider thumb and switch track painted
         * through the real paint path. UI-REDESIGN 2.2 geometry:
         * slider thumb is a 28px-diameter circle; the switch
         * track is 52x32 with a 24px knob. */
        tk_widget *sl = tk_slider_new(0.5f);
        tk_root *root = tk_root_new(sl);
        tk_root_layout(root, 200, 40);
        tk_canvas *cv = tk_canvas_new(200, 40);
        tk_root_paint(root, cv, theme);
        /* thumb: centre (100,20), spans x 86..113, y 6..33;
         * its top row (y=6) is inset by 10, so x=87 is outside
         * and x=100 is inside; the middle row is full width */
        tk_color acc = theme->accent;
        tk_color p = tk_canvas_pixel(cv, 87, 6);
        CHECK(!(p.r == acc.r && p.g == acc.g && p.b == acc.b));
        p = tk_canvas_pixel(cv, 100, 6);
        CHECK(p.r == acc.r && p.g == acc.g && p.b == acc.b);
        p = tk_canvas_pixel(cv, 86, 20);
        CHECK(p.r == acc.r && p.g == acc.g && p.b == acc.b);
        tk_canvas_free(cv);
        tk_root_free(root);
        tk_widget_free(sl);
    }
    {
        tk_widget *sw = tk_switch_new("Wi-Fi", false);
        tk_root *root = tk_root_new(sw);
        tk_root_layout(root, 200, 40);
        tk_canvas *cv = tk_canvas_new(200, 40);
        tk_root_paint(root, cv, theme);
        tk_rect sr = tk_widget_rect(sw);
        int cy = sr.y + sr.h / 2;
        /* track spans y cy-16..cy+15; its top row (cy-16) is
         * inset by 12: the corner is background, the middle is
         * track. (Probing lower rows would hit the knob.) */
        tk_color alt = theme->surface_alt;
        tk_color p = tk_canvas_pixel(cv, sr.x + 26, cy - 16);
        CHECK(p.r == alt.r && p.g == alt.g && p.b == alt.b);
        p = tk_canvas_pixel(cv, sr.x + 1, cy - 16);
        CHECK(!(p.r == alt.r && p.g == alt.g && p.b == alt.b));
        /* track middle row at the left edge: full width */
        p = tk_canvas_pixel(cv, sr.x + 1, cy);
        CHECK(p.r == alt.r || p.r == theme->surface.r);
        tk_canvas_free(cv);
        tk_root_free(root);
        tk_widget_free(sw);
    }

    /* ============ bubbling: unhandled click reaches the parent == */
    {
        /* a label inside a box: hit is the label (not focusable),
         * focus moves to nothing, but dispatch still returns to root */
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *l = tk_label_new("plain");
        tk_add_child(box, l);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, 100, 50);
        tk_event dn = {0};
        dn.type = TK_EV_POINTER_DOWN; dn.x = 5; dn.y = 5;
        bool handled = tk_root_event(root, &dn);
        CHECK(!handled); /* nobody consumes a label click */
        CHECK(tk_hit_test(box, 5, 5) == l);
        tk_root_free(root);
        tk_widget_free(box);
    }

    printf("tk: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

/* --- test clipboard provider (used above) --- */
static char test_clip[256];
static void test_clip_set(void *ud, const char *text, size_t len)
{
    (void)ud;
    if (len >= sizeof(test_clip)) len = sizeof(test_clip) - 1;
    memcpy(test_clip, text, len);
    test_clip[len] = '\0';
}
static size_t test_clip_get(void *ud, char *out, size_t cap)
{
    (void)ud;
    size_t len = strlen(test_clip);
    if (out && cap) {
        size_t w = len < cap - 1 ? len : cap - 1;
        memcpy(out, test_clip, w);
        out[w] = '\0';
    }
    return len;
}
void test_set_clip(tc_buffer *b)
{
    tc_clipboard cb = { NULL, test_clip_set, test_clip_get };
    tc_set_clipboard(b, &cb);
}
char clip_dummy_anchor;
