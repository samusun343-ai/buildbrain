/* test_font.c - M2 font backend tests: real glyph measurement vs
 * the M1 monospace approximation (differences printed for the
 * record), fallback chain for CJK, glyph rasterization onto the
 * canvas, the glyph cache, and kinsoku wrapping with real metrics.
 *
 * Uses host system fonts: DejaVu Sans (primary) and Noto Sans CJK
 * (fallback). Font files are NOT bundled; this is the documented
 * load-API path (DESIGN section 11). */
#include "tk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int count_non_bg(tk_canvas *c, tk_color bg)
{
    int n = 0;
    for (int y = 0; y < tk_canvas_height(c); y++)
        for (int x = 0; x < tk_canvas_width(c); x++) {
            tk_color p = tk_canvas_pixel(c, x, y);
            if (p.r != bg.r || p.g != bg.g || p.b != bg.b) n++;
        }
    return n;
}

static void collect_start(void *ud, size_t start, size_t len)
{
    (void)len;
    void **arr = ud;
    size_t *starts = arr[0];
    int *n = arr[1];
    if (*n < 16) starts[(*n)] = start;
    (*n)++;
}

/* M23: the bundled search must not depend on the CWD (pre-M23
 * the CWD-relative fonts/ path came first, so a product
 * launched from an arbitrary directory lost its font). The
 * test binary lives in the tree root, so the executable-
 * relative candidate finds the bundled font even after a
 * chdir into an empty directory; the env override still
 * works, and a bogus env value falls through. */
static void test_bundled_search(void)
{
    char cwd[1024];
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    const char *empty = "/tmp/brainui-nofonts-m23";
    mkdir(empty, 0700); /* may already exist; it has no fonts/ */
    char fontpath[1200];
    snprintf(fontpath, sizeof(fontpath),
             "%s/fonts/brainui-jp-subset.ttf", cwd);
    CHECK(chdir(empty) == 0);
    unsetenv("BRAINUI_FONT_PATH");
    tk_font *f = tk_font_load_bundled();
    CHECK(f != NULL); /* via the executable, not the CWD */
    tk_font_free(f);
    setenv("BRAINUI_FONT_PATH", fontpath, 1);
    f = tk_font_load_bundled();
    CHECK(f != NULL); /* the env override, from an empty CWD */
    tk_font_free(f);
    setenv("BRAINUI_FONT_PATH", "/nonexistent/brainui.ttf", 1);
    f = tk_font_load_bundled();
    CHECK(f != NULL); /* a bogus env value falls through */
    tk_font_free(f);
    unsetenv("BRAINUI_FONT_PATH");
    CHECK(chdir(cwd) == 0);
    rmdir(empty);
}

int main(void)
{
    test_bundled_search();
    const tk_theme *theme = tk_theme_default();
    tk_color bg = { 255, 255, 255, 255 };
    tk_color ink = { 0, 0, 0, 255 };

    /* --- approximation baseline (no font installed) --- */
    int approx_hello = tk_text_width(theme, "Hello");
    int approx_w = tk_text_width(theme, "WWWW");
    int approx_i = tk_text_width(theme, "iiii");
    CHECK(approx_hello == 40);       /* 5 chars x 8px */
    CHECK(approx_w == approx_i);     /* approximation cannot tell */
    printf("approx: Hello=%d WWWW=%d iiii=%d line_h=%d\n",
           approx_hello, approx_w, approx_i, tk_line_height(theme));

    /* --- load real fonts --- */
    tk_font *dejavu = tk_font_load(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0);
    tk_font *cjk = tk_font_load(
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0);
    CHECK(dejavu != NULL);
    CHECK(cjk != NULL);
    if (!dejavu || !cjk) {
        printf("font: %d passed, %d failed (fonts missing)\n",
               passes, fails);
        return 1;
    }
    CHECK(tk_font_has_glyph(dejavu, 'A'));
    CHECK(!tk_font_has_glyph(dejavu, 0x6F22));   /* 漢: not in DejaVu */
    CHECK(tk_font_has_glyph(cjk, 0x6F22));
    tk_font_set_fallback(dejavu, cjk);
    CHECK(tk_font_has_glyph(dejavu, 0x6F22));    /* via the chain */

    tk_font_install(dejavu);

    /* --- real measurement differs from the approximation --- */
    int real_hello = tk_text_width(theme, "Hello");
    int real_w = tk_text_width(theme, "WWWW");
    int real_i = tk_text_width(theme, "iiii");
    int real_lh = tk_line_height(theme);
    printf("real:   Hello=%d (approx %d, diff %+d) WWWW=%d iiii=%d "
           "line_h=%d (approx %d)\n",
           real_hello, approx_hello, real_hello - approx_hello,
           real_w, real_i, real_lh, theme->font_px + theme->font_px / 3);
    /* "Hello" happens to total the same 40px in DejaVu; use M
     * (near-full em advance) as the discriminator instead */
    int real_m = tk_text_width(theme, "MMMM");
    printf("real:   MMMM=%d (approx 32)\n", real_m);
    CHECK(real_m != 32);
    CHECK(real_w > real_i);          /* proportional font: W >> i */
    int adv_kanji = tk_font_cp_advance(dejavu, theme->font_px, 0x6F22);
    printf("real:   kanji advance via fallback=%d (approx %d)\n",
           adv_kanji, theme->font_px);
    CHECK(adv_kanji > 0);
    CHECK(tk_text_width(theme, "漢字") == 2 * adv_kanji);

    /* --- glyph rasterization actually paints --- */
    tk_canvas *cv = tk_canvas_new(240, 48);
    tk_canvas_clear(cv, bg);
    tk_canvas_draw_text(cv, theme, 4, 4, "Ag", ink);
    int painted_latin = count_non_bg(cv, bg);
    CHECK(painted_latin > 20);
    tk_canvas_clear(cv, bg);
    tk_canvas_draw_text(cv, theme, 4, 4, "漢字", ink);
    int painted_cjk = count_non_bg(cv, bg);
    CHECK(painted_cjk > 20);
    printf("raster: latin glyphs painted %d px, CJK painted %d px\n",
           painted_latin, painted_cjk);
    tk_canvas_free(cv);

    /* --- glyph cache is populated and reused --- */
    size_t cache_n = tk_font_cache_count(dejavu);
    CHECK(cache_n > 0);
    printf("cache: %zu glyphs cached in primary font\n", cache_n);

    /* --- kinsoku wrapping with real metrics ---
     * Lines must not start with 、 and must not end with 「. */
    {
        const char *jp = "あいうえお、かきくけこ";
        int adv = tk_font_cp_advance(dejavu, theme->font_px, 0x3042);
        size_t starts[16];
        int n = 0;
        void *arr[2] = { starts, &n };
        size_t lines = tk_text_wrap(theme, jp, 5 * adv,
                                    collect_start, arr);
        CHECK(lines == 3);
        /* byte offset of line starts: line boundaries must fall
         * before お (byte 12), not before 、 (byte 15) */
        CHECK(starts[1] == 12);
        for (int i = 1; i < n && i < 16; i++) {
            uint32_t cp = tc_utf8_decode(jp + starts[i],
                                         strlen(jp) - starts[i], NULL);
            CHECK(cp != 0x3001 && cp != 0x3002);   /* not 、 or 。 */
        }
        const char *jp2 = "「あいうえお」かきくけこさ";
        n = 0;
        lines = tk_text_wrap(theme, jp2, 5 * adv, collect_start, arr);
        CHECK(lines >= 2);
        /* no line may END with 「: the char before each line start
         * (skipping back over the previous char) is checked by
         * verifying line 2 does not start right after a lone 「 */
        CHECK(starts[1] != 3);  /* would mean line 1 was just 「 */
        printf("kinsoku: jp lines=%zu starts=%zu,%zu,... jp2 ok\n",
               lines, starts[0], starts[1]);
    }

    tk_font_install(NULL);
    /* after uninstall the approximation is back */
    CHECK(tk_text_width(theme, "Hello") == approx_hello);

    tk_font_free(dejavu);
    tk_font_free(cjk);
    printf("font: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
