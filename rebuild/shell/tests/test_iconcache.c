/* test_iconcache.c - icon cache tests with an injected fake
 * decoder (counts decodes, returns a known 8x8 pattern). */
#include "iconcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int decode_calls;
static bool fake_decode(void *ud, const char *path,
                        uint32_t **out_px, int *out_w, int *out_h)
{
    (void)ud;
    decode_calls++;
    if (strstr(path, "bad")) return false;
    int w = 8, h = 8;
    uint32_t *px = malloc((size_t)w * h * sizeof(uint32_t));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = 0xFF000000u | ((uint32_t)(x * 32) << 16) |
                            ((uint32_t)(y * 32) << 8);
    *out_px = px; *out_w = w; *out_h = h;
    return true;
}

static uint32_t px_at(const uint32_t *px, int w, int x, int y)
{
    return px[y * w + x];
}

int main(void)
{
    size_t hits, misses, evict;

    /* --- basic hit/miss + scaling correctness --- */
    ic_cache *c = ic_cache_new(1024 * 1024);
    ic_cache_set_decoder(c, fake_decode, NULL);
    decode_calls = 0;
    int w = 0, h = 0;
    const uint32_t *img = ic_get(c, "a.png", 48, 48, &w, &h);
    CHECK(img != NULL && w == 48 && h == 48);
    CHECK(decode_calls == 1);
    /* nearest neighbour: dst(47,47) <- src(7,7), dst(0,0) <- src(0,0) */
    CHECK(px_at(img, 48, 47, 47) == (0xFF000000u | (224u << 16) | (224u << 8)));
    CHECK(px_at(img, 48, 0, 0) == 0xFF000000u);
    img = ic_get(c, "a.png", 48, 48, &w, &h);
    CHECK(img != NULL);
    CHECK(decode_calls == 1); /* second get is a cache hit */
    ic_cache_stats(c, &hits, &misses, &evict);
    CHECK(hits == 1 && misses == 1 && evict == 0);
    /* different size = different cache entry */
    img = ic_get(c, "a.png", 24, 24, &w, &h);
    CHECK(img != NULL && decode_calls == 2);
    CHECK(px_at(img, 24, 23, 23) == (0xFF000000u | (224u << 16) | (224u << 8)));
    /* decode failure propagates, and is not cached */
    CHECK(ic_get(c, "bad.png", 48, 48, &w, &h) == NULL);
    CHECK(ic_get(c, "bad.png", 48, 48, &w, &h) == NULL);
    CHECK(decode_calls == 4);
    /* invalidate forces a re-decode */
    ic_cache_invalidate(c, "a.png");
    img = ic_get(c, "a.png", 48, 48, &w, &h);
    CHECK(img != NULL && decode_calls == 5);
    ic_cache_free(c);

    /* --- LRU eviction: budget holds exactly two 48x48 icons --- */
    c = ic_cache_new(48 * 48 * 4 * 2);
    ic_cache_set_decoder(c, fake_decode, NULL);
    decode_calls = 0;
    ic_get(c, "a.png", 48, 48, &w, &h);
    ic_get(c, "b.png", 48, 48, &w, &h);
    CHECK(ic_cache_bytes(c) == (size_t)(48 * 48 * 4 * 2));
    ic_get(c, "a.png", 48, 48, &w, &h); /* a becomes MRU */
    CHECK(decode_calls == 2);
    ic_get(c, "c.png", 48, 48, &w, &h); /* evicts b (LRU), keeps a */
    ic_cache_stats(c, &hits, &misses, &evict);
    CHECK(evict == 1);
    ic_get(c, "a.png", 48, 48, &w, &h);
    CHECK(decode_calls == 3); /* a still cached */
    ic_get(c, "b.png", 48, 48, &w, &h);
    CHECK(decode_calls == 4); /* b was evicted */
    ic_cache_free(c);

    /* --- oversized item: returned but never cached --- */
    c = ic_cache_new(1024);
    ic_cache_set_decoder(c, fake_decode, NULL);
    decode_calls = 0;
    img = ic_get(c, "big.png", 48, 48, &w, &h);
    CHECK(img != NULL);
    CHECK(ic_cache_bytes(c) == 0);
    ic_get(c, "big.png", 48, 48, &w, &h);
    CHECK(decode_calls == 2); /* re-decoded every time */
    ic_cache_free(c);

    /* --- icon name resolution (M4) --- */
    {
        char out[512];
        /* the bundled Papirus-derived set in the dev tree
         * (../assets/app relative to shell/) resolves both by
         * its own stem and by freedesktop alias */
        CHECK(ic_resolve_icon_path("clock", out, sizeof(out)));
        CHECK(strstr(out, "clock.png") != NULL);
        CHECK(ic_resolve_icon_path("accessories-calculator",
                                   out, sizeof(out)));
        CHECK(strstr(out, "calc.png") != NULL);
        CHECK(ic_resolve_icon_path("bd-files", out, sizeof(out)));
        CHECK(strstr(out, "files.png") != NULL);
        /* a real file path passes through; a missing one fails */
        CHECK(ic_resolve_icon_path("../assets/app/notes.png",
                                   out, sizeof(out)));
        CHECK(!ic_resolve_icon_path("/nonexistent/icon.png",
                                    out, sizeof(out)));
        CHECK(!ic_resolve_icon_path("no-such-icon-name-m4",
                                    out, sizeof(out)));
        CHECK(!ic_resolve_icon_path(NULL, out, sizeof(out)));
        /* BD_ICON_DIR takes precedence and is really consulted */
        char tmpl[] = "/tmp/bdiconXXXXXX";
        CHECK(mkdtemp(tmpl) != NULL);
        char p[512];
        snprintf(p, sizeof(p), "%s/custom.png", tmpl);
        FILE *f = fopen(p, "w");
        CHECK(f != NULL);
        if (f) { fputs("x", f); fclose(f); }
        setenv("BD_ICON_DIR", tmpl, 1);
        CHECK(ic_resolve_icon_path("custom", out, sizeof(out)));
        CHECK(strcmp(out, p) == 0);
        unsetenv("BD_ICON_DIR");
        unlink(p);
        rmdir(tmpl);
    }

    printf("test_iconcache: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
