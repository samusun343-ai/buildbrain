/* test_shape.c - M24 shaping tests: HarfBuzz-shaped text through
 * the public API. Ligatures and GPOS kerning (DejaVu Sans has
 * both), combining-mark clusters, mixed-script itemization across
 * the fallback chain, and cluster-atomic line wrapping.
 *
 * All expectations are relations computed at runtime from the
 * same fonts (no hard-coded pixel values), in the style of
 * test_font.c. Uses host system fonts + the bundled subset. */
#include "tk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int max_painted_x(tk_canvas *c, tk_color bg)
{
    int m = -1;
    for (int y = 0; y < tk_canvas_height(c); y++)
        for (int x = 0; x < tk_canvas_width(c); x++) {
            tk_color p = tk_canvas_pixel(c, x, y);
            if ((p.r != bg.r || p.g != bg.g || p.b != bg.b) && x > m) m = x;
        }
    return m;
}

struct lines { size_t start[16], len[16]; int n; };
static void collect_line(void *ud, size_t start, size_t len)
{
    struct lines *L = ud;
    if (L->n < 16) { L->start[L->n] = start; L->len[L->n] = len; }
    L->n++;
}

int main(void)
{
    const tk_theme *theme = tk_theme_default();
    tk_color bg = { 255, 255, 255, 255 };
    tk_color ink = { 0, 0, 0, 255 };

    tk_font *dejavu = tk_font_load(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0);
    tk_font *cjk = tk_font_load(
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0);
    CHECK(dejavu != NULL);
    CHECK(cjk != NULL);
    if (!dejavu || !cjk) {
        printf("shape: %d passed, %d failed (fonts missing)\n",
               passes, fails);
        return 1;
    }
    tk_font_set_fallback(dejavu, cjk);
    tk_font_install(dejavu);

    /* --- ligatures: "fi" shapes to ONE glyph (the pre-M24
     * backend always measured/drew the two glyphs independently).
     * At 16px the integer widths coincide (ligature 10.08px vs
     * rounded parts 6+4), so the width evidence is "ffi" - whose
     * single glyph is narrower than the rounded sum of parts -
     * and the drawn evidence is a pixel comparison: "fi" in one
     * call must NOT look like "f" and "i" drawn side by side. --- */
    int w_f = tk_text_width(theme, "f");
    int w_i = tk_text_width(theme, "i");
    int w_fi = tk_text_width(theme, "fi");
    int w_ffi = tk_text_width(theme, "ffi");
    printf("ligature: f=%d i=%d fi=%d (sum %d) ffi=%d\n",
           w_f, w_i, w_fi, w_f + w_i, w_ffi);
    CHECK(w_fi > w_f);
    CHECK(w_ffi < 2 * w_f + w_i);
    CHECK(w_ffi < w_fi + w_f);
    {
        tk_canvas *c1 = tk_canvas_new(120, 40);
        tk_canvas *c2 = tk_canvas_new(120, 40);
        tk_canvas_clear(c1, bg);
        tk_canvas_clear(c2, bg);
        tk_canvas_draw_text(c1, theme, 4, 4, "fi", ink);
        tk_canvas_draw_text(c2, theme, 4, 4, "f", ink);
        tk_canvas_draw_text(c2, theme,
                            4 + tk_text_cp_width(theme, 'f'), 4, "i", ink);
        int diff = 0;
        for (int y = 0; y < 40; y++)
            for (int x = 0; x < 120; x++) {
                tk_color p = tk_canvas_pixel(c1, x, y);
                tk_color q = tk_canvas_pixel(c2, x, y);
                if (p.r != q.r || p.g != q.g || p.b != q.b) diff++;
            }
        printf("ligature draw: one-call vs two-call differ in %d px\n", diff);
        /* observed 32px with host DejaVu; per-codepoint drawing
         * (the old backend) would differ by exactly 0 */
        CHECK(diff > 10);
        tk_canvas_free(c1);
        tk_canvas_free(c2);
    }

    /* --- GPOS kerning: "AV" is tighter than A + V --- */
    int w_a = tk_text_width(theme, "A");
    int w_v = tk_text_width(theme, "V");
    int w_av = tk_text_width(theme, "AV");
    printf("kern: A=%d V=%d AV=%d (sum %d)\n", w_a, w_v, w_av, w_a + w_v);
    CHECK(w_av < w_a + w_v);

    /* --- combining sequence composes: e + U+0301 measures like
     * the precomposed U+00E9 and paints its accent --- */
    int w_comb = tk_text_width(theme, "e\xcc\x81");
    int w_pre = tk_text_width(theme, "\xc3\xa9");
    printf("combining: e+U+0301=%d U+00E9=%d\n", w_comb, w_pre);
    CHECK(w_comb == w_pre);
    {
        tk_canvas *cv = tk_canvas_new(120, 40);
        tk_canvas_clear(cv, bg);
        tk_canvas_draw_text(cv, theme, 4, 4, "e", ink);
        int plain = count_non_bg(cv, bg);
        tk_canvas_clear(cv, bg);
        tk_canvas_draw_text(cv, theme, 4, 4, "e\xcc\x81", ink);
        int composed = count_non_bg(cv, bg);
        printf("combining draw: e=%d px, e+mark=%d px\n", plain, composed);
        CHECK(composed > plain);   /* the accent is really painted */
        tk_canvas_free(cv);
    }

    /* --- the ligature actually paints as one run --- */
    {
        tk_canvas *cv = tk_canvas_new(120, 40);
        tk_canvas_clear(cv, bg);
        tk_canvas_draw_text(cv, theme, 4, 4, "fi", ink);
        CHECK(count_non_bg(cv, bg) > 20);
        tk_canvas_free(cv);
    }

    /* --- mixed script across the fallback chain: Japanese is
     * itemized to the CJK font, Latin/digits stay in DejaVu, and
     * the whole measures like the sum of its parts --- */
    const char *mixed = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" "ABC123";
    int w_mixed = tk_text_width(theme, mixed);
    int w_parts = tk_text_width(theme, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e")
                + tk_text_width(theme, "ABC123");
    printf("mixed: whole=%d parts=%d\n", w_mixed, w_parts);
    CHECK(w_mixed > 0);
    CHECK(abs(w_mixed - w_parts) <= 1);
    {
        tk_canvas *cv = tk_canvas_new(300, 40);
        tk_canvas_clear(cv, bg);
        tk_canvas_draw_text(cv, theme, 4, 4, "ABC123", ink);
        int latin = count_non_bg(cv, bg);
        tk_canvas_clear(cv, bg);
        tk_canvas_draw_text(cv, theme, 4, 4, mixed, ink);
        int both = count_non_bg(cv, bg);
        printf("mixed draw: latin=%d px, mixed=%d px\n", latin, both);
        CHECK(both > latin);   /* the CJK glyphs painted too */
        tk_canvas_free(cv);
    }

    /* --- wrapping never splits a cluster: "fifififi" at exactly
     * two ligatures per line must break between ligatures, so
     * every line is a whole number of "fi" byte pairs --- */
    {
        struct lines L = { { 0 }, { 0 }, 0 };
        size_t lines = tk_text_wrap(theme, "fifififi", 2 * w_fi,
                                    collect_line, &L);
        printf("wrap clusters: lines=%zu starts=%zu,%zu lens=%zu,%zu\n",
               lines, L.start[0], L.start[1], L.len[0], L.len[1]);
        CHECK(lines == 2);
        CHECK(L.n == 2);
        CHECK(L.start[0] == 0 && L.len[0] == 4);
        CHECK(L.start[1] == 4 && L.len[1] == 4);
    }

    /* --- bundled subset (the on-device font): combining kana
     * forms one cluster with the base glyph's advance, and CJK
     * text measures exactly like the sum of its advances --- */
    tk_font *bundled = tk_font_load_bundled();
    CHECK(bundled != NULL);
    if (bundled) {
        tk_font_install(bundled);
        int w_ka = tk_text_width(theme, "\xe3\x81\x8b");
        /* か + U+3099 (combining dakuten) */
        int w_kad = tk_text_width(theme, "\xe3\x81\x8b\xe3\x82\x99");
        printf("bundled cluster: ka=%d ka+mark=%d\n", w_ka, w_kad);
        CHECK(w_kad == w_ka);
        int adv_kanji = tk_font_cp_advance(bundled, theme->font_px, 0x6F22);
        CHECK(tk_text_width(theme, "\xe6\xbc\xa2\xe5\xad\x97") == 2 * adv_kanji);
        /* the mark must overlay the base glyph, not advance past
         * it: all ink stays within the base glyph's advance */
        tk_canvas *cv = tk_canvas_new(120, 40);
        tk_canvas_clear(cv, bg);
        tk_canvas_draw_text(cv, theme, 4, 4, "\xe3\x81\x8b\xe3\x82\x99", ink);
        int mx = max_painted_x(cv, bg);
        printf("bundled mark draw: max ink x=%d (base advance ends %d)\n",
               mx, 4 + w_ka);
        CHECK(mx >= 4);
        CHECK(mx < 4 + w_ka + 2);
        tk_canvas_free(cv);
        tk_font_free(bundled);
    }

    tk_font_install(NULL);
    tk_font_free(dejavu);
    tk_font_free(cjk);
    printf("shape: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
