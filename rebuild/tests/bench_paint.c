/* bench_paint.c - M5 quantitative evaluation of the paint/flush
 * hot paths at the real panel geometry (800x480): full repaint
 * time of representative screens, scroll frame cost (repaint +
 * transfer), glyph-heavy text, rounded-shape-heavy surfaces and
 * the raw framebuffer conversion. Prints numbers, not pass/fail.
 * Run with `make bench-paint`. */
#include "tk.h"
#include "textcore.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define W 800
#define H 480

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void send_ptr(tk_root *r, tk_event_type ty, int x, int y)
{
    tk_event ev; memset(&ev, 0, sizeof(ev));
    ev.type = ty; ev.x = x; ev.y = y;
    (void)tk_root_event(r, &ev);
}

/* Build the "file list" screen: header bar + ScrollView of rows
 * (two-line ListView-style content via labels in boxes is NOT what
 * apps do; use a real ListView inside a ScrollView-free layout and
 * a label list inside a ScrollView, matching bench_damage). */
static tk_widget *build_list_screen(tk_widget **sv_out, tk_widget **list_out)
{
    tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 0, 0);
    tk_widget *header = tk_label_new("ファイル一覧");
    tk_widget *sv = tk_scrollview_new();
    tk_widget *list = tk_box_new(TK_DIR_VERTICAL, 4, 4);
    for (int i = 0; i < 60; i++) {
        char b[64];
        snprintf(b, sizeof b, "ドキュメント %02d - 会議メモと写真データ.txt", i);
        tk_add_child(list, tk_label_new(b));
    }
    tk_scrollview_set_child(sv, list);
    tk_add_child(box, header);
    tk_add_child(box, sv);
    tk_set_flex(sv, 1); /* standard app pattern: SV takes the rest */
    *sv_out = sv;
    *list_out = list;
    return box;
}

int main(void)
{
    tk_font *font = tk_font_load_default();
    if (font) tk_font_install(font);
    const tk_theme *t = tk_theme_default();
    printf("panel %dx%d, font %s\n", W, H, font ? "installed" : "NONE");

    /* ===== full repaint: list screen ============================ */
    {
        tk_widget *sv, *list;
        tk_widget *top = build_list_screen(&sv, &list);
        tk_root *root = tk_root_new(top);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_root_paint(root, c, t); /* warm caches */
        int N = 30;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) tk_root_paint(root, c, t);
        double dt = (now_ms() - t0) / N;
        printf("full paint list screen (60 rows): %8.3f ms/frame\n", dt);
        tk_canvas_free(c); tk_root_free(root); tk_widget_free(top);
    }

    /* ===== scroll frame: repaint damage + flush ================= */
    for (int blit = 0; blit <= 1; blit++) {
        tk_widget *sv, *list;
        tk_widget *top = build_list_screen(&sv, &list);
        tk_root *root = tk_root_new(top);
#ifdef TK_BENCH_BLIT
        tk_root_set_scroll_blit(root, blit != 0);
#endif
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_fb *fb = tk_fb_open_mem(W, H, 16);
        tk_root_paint_damage(root, c, t);
        (void)tk_fb_flush_damage(fb, c);
        int N = 30;
        size_t bytes = 0;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) {
            tk_scrollview_scroll_to(sv, 40 + i * 13);
            tk_widget_invalidate(sv);
            tk_root_paint_damage(root, c, t);
            bytes += tk_fb_flush_damage(fb, c);
        }
        double dt = (now_ms() - t0) / N;
        printf("scroll frame (blit %s): %8.3f ms, %7zu B shipped/frame\n",
               blit ? "on " : "off", dt, bytes / (size_t)N);
        tk_fb_close(fb); tk_canvas_free(c);
        tk_root_free(root); tk_widget_free(top);
    }

    /* ===== glyph-heavy: a page of Japanese text ================= */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 4, 8);
        const char *para =
            "吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"
            "何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。";
        for (int i = 0; i < 8; i++)
            tk_add_child(box, tk_label_new(para));
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_root_paint(root, c, t);
        int N = 30;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) tk_root_paint(root, c, t);
        printf("full paint text page (8 paragraphs): %8.3f ms/frame\n",
               (now_ms() - t0) / N);
        tk_canvas_free(c); tk_root_free(root); tk_widget_free(box);
    }

    /* ===== shape-heavy: QS-style tiles (round rects + sliders) == */
    {
        tk_widget *box = tk_box_new(TK_DIR_VERTICAL, 8, 16);
        for (int i = 0; i < 4; i++) {
            tk_widget *tile = tk_box_new(TK_DIR_VERTICAL, 4, 12);
            tk_widget_set_backdrop(tile, true, 24);
            tk_add_child(tile, tk_label_new("Wi-Fi"));
            tk_add_child(tile, tk_slider_new(0.4f + 0.1f * i));
            tk_add_child(box, tile);
        }
        tk_root *root = tk_root_new(box);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_root_paint(root, c, t);
        int N = 30;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) tk_root_paint(root, c, t);
        printf("full paint QS tiles (4 cards): %8.3f ms/frame\n",
               (now_ms() - t0) / N);
        tk_canvas_free(c); tk_root_free(root); tk_widget_free(box);
    }

    /* ===== raw flush: full frame conversion ===================== */
    {
        tk_canvas *c = tk_canvas_new(W, H);
        tk_fb *fb = tk_fb_open_mem(W, H, 16);
        tk_canvas_clear(c, (tk_color){200, 200, 210, 255});
        int N = 30;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) (void)tk_fb_flush(fb, c);
        printf("fb flush full frame 16bpp: %8.3f ms\n", (now_ms() - t0) / N);
        tk_fb_close(fb); tk_canvas_free(c);
    }

    /* ===== damage paint of one small widget (culling check) ==== */
    {
        tk_widget *sv, *list;
        tk_widget *top = build_list_screen(&sv, &list);
        tk_root *root = tk_root_new(top);
        tk_root_layout(root, W, H);
        tk_canvas *c = tk_canvas_new(W, H);
        tk_root_paint_damage(root, c, t);
        /* invalidate one label deep in the (off-screen part of the)
         * list: repaint should touch only its rect */
        tk_widget *lbl = tk_child_at(list, 3);
        int N = 60;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) {
            tk_widget_invalidate(lbl);
            tk_root_paint_damage(root, c, t);
        }
        printf("damage paint 1 label in 60-row tree: %8.3f ms\n",
               (now_ms() - t0) / N);
        (void)send_ptr;
        tk_canvas_free(c); tk_root_free(root); tk_widget_free(top);
    }
    return 0;
}
