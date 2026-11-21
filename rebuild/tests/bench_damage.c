/* bench_damage.c - M3 quantitative evaluation of damage-based
 * presentation: bytes shipped by tk_fb_flush_damage() vs the full
 * frame, for representative app-loop scenarios on the Brain panel
 * geometry (480x272, 16 bpp memory framebuffer - the same conversion
 * path as the real fbdev). Not a pass/fail test: it prints numbers.
 * Run with `make bench`. */
#include "tk.h"
#include "textcore.h"
#include <stdio.h>
#include <string.h>

#define W 480
#define H 272
#define FULL ((size_t)W * H * 2)

static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = ty; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}
static void send_text(tk_root *r, const char *s)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = TK_EV_TEXT_INPUT; ev.text = s;
    (void)tk_root_event(r, &ev);
}

/* one app-loop frame: repaint damage, ship damage, return bytes */
static size_t frame(tk_root *r, tk_canvas *c, tk_fb *fb, const tk_theme *t)
{
    (void)tk_root_paint_damage(r, c, t);
    return tk_fb_flush_damage(fb, c);
}

static void report(const char *name, size_t bytes)
{
    printf("%-34s %8zu B  (%5.1f%% of full frame, %5.1f%% saved)\n",
           name, bytes, 100.0 * (double)bytes / (double)FULL,
           100.0 * (1.0 - (double)bytes / (double)FULL));
}

int main(void)
{
    const tk_theme *t = tk_theme_default();
    printf("panel %dx%d 16bpp, full frame = %zu B\n", W, H, FULL);

    /* ===== clock: one label's text changes once per second ===== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 8);
        tk_widget *title = tk_label_new("Brainux 設定");
        tk_widget *clock = tk_label_new("12:34:56");
        tk_widget *btn = tk_button_new("保存");
        tk_add_child(box, title);
        tk_add_child(box, clock);
        tk_add_child(box, btn);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_fb *fb = tk_fb_open_mem(W, H, 16);
        report("initial frame (full)", frame(root, c, fb, t));
        char buf[16];
        size_t total = 0;
        for (int s = 57; s <= 59; s++) {
            snprintf(buf, sizeof buf, "12:34:%02d", s);
            tk_label_set_text(clock, buf);
            /* documented contract: programmatic changes need an
             * explicit tk_widget_invalidate() (setters do not
             * invalidate on their own - see REPORT-toolkit-m3.md) */
            tk_widget_invalidate(clock);
            total += frame(root, c, fb, t);
        }
        /* minute rollover changes two digits */
        tk_label_set_text(clock, "12:35:00");
        tk_widget_invalidate(clock);
        size_t roll = frame(root, c, fb, t);
        report("clock second update (avg of 3)", total / 3);
        report("clock minute rollover", roll);
        tk_fb_close(fb);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== text input: one character into a focused TextField ==== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 8);
        tk_widget *label = tk_label_new("名前");
        tk_widget *tf = tk_textfield_new("");
        tk_add_child(box, label);
        tk_add_child(box, tf);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_fb *fb = tk_fb_open_mem(W, H, 16);
        (void)frame(root, c, fb, t);
        tk_rect fr = tk_widget_rect(tf);
        send_ptr(root, TK_EV_POINTER_DOWN, fr.x + 10, fr.y + fr.h / 2);
        send_ptr(root, TK_EV_POINTER_UP, fr.x + 10, fr.y + fr.h / 2);
        (void)frame(root, c, fb, t); /* focus change frame */
        send_text(root, "a");
        report("text input: 1 ASCII char", frame(root, c, fb, t));
        send_text(root, "あ");
        report("text input: 1 kana char", frame(root, c, fb, t));
        tk_fb_close(fb);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== scroll: list under a fixed header bar ================= */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
        tk_widget *header = tk_label_new("ファイル一覧");
        tk_widget *sv = tk_scrollview_new();
        tk_widget *list = tk_box_new(TK_DIR_VERTICAL, 4, 4);
        for (int i = 0; i < 30; i++) {
            char b[32];
            snprintf(b, sizeof b, "ファイル %02d.txt", i);
            tk_add_child(list, tk_label_new(b));
        }
        tk_scrollview_set_child(sv, list);
        tk_add_child(box, header);
        tk_add_child(box, sv);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_fb *fb = tk_fb_open_mem(W, H, 16);
        (void)frame(root, c, fb, t);
        tk_scrollview_scroll_to(sv, 40);
        tk_widget_invalidate(sv); /* same explicit-invalidate contract */
        report("scroll +40px (header fixed)", frame(root, c, fb, t));
        tk_scrollview_scroll_to(sv, 40 + H);
        tk_widget_invalidate(sv);
        report("scroll +1 page (header fixed)", frame(root, c, fb, t));
        tk_fb_close(fb);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }

    /* ===== toggle: a switch flips (reference, cf. test_tk3) ====== */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 6, 8);
        tk_widget *sw = tk_switch_new("Wi-Fi", false);
        tk_add_child(box, sw);
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_fb *fb = tk_fb_open_mem(W, H, 16);
        (void)frame(root, c, fb, t);
        tk_rect sr = tk_widget_rect(sw);
        send_ptr(root, TK_EV_POINTER_DOWN, sr.x + sr.w / 2, sr.y + sr.h / 2);
        send_ptr(root, TK_EV_POINTER_UP, sr.x + sr.w / 2, sr.y + sr.h / 2);
        report("switch toggle", frame(root, c, fb, t));
        tk_fb_close(fb);
        tk_canvas_free(c);
        tk_root_free(root);
        tk_widget_free(box);
    }
    return 0;
}
