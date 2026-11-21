/* tk_image.c - image decoding via stb_image (PNG/JPEG/BMP/...).
 * Separated so the large single-header implementation is compiled
 * exactly once. Returns pixels packed like the canvas (ARGB8888 in
 * a uint32_t: a<<24 | r<<16 | g<<8 | b). */
#include "tk_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

bool tk_image_load_rgba(const char *path, uint32_t **out_px, int *w, int *h)
{
    if (!path || !out_px || !w || !h) return false;
    int iw = 0, ih = 0, channels = 0;
    unsigned char *data = stbi_load(path, &iw, &ih, &channels, 4);
    if (!data) return false;
    uint32_t *px = malloc((size_t)iw * (size_t)ih * sizeof(uint32_t));
    if (!px) { stbi_image_free(data); return false; }
    for (size_t i = 0, n = (size_t)iw * (size_t)ih; i < n; i++) {
        const unsigned char *s = data + i * 4;
        px[i] = ((uint32_t)s[3] << 24) | ((uint32_t)s[0] << 16) |
                ((uint32_t)s[1] << 8) | (uint32_t)s[2];
    }
    stbi_image_free(data);
    *out_px = px; *w = iw; *h = ih;
    return true;
}

/* ---- downscaling (M5) ------------------------------------------
 * Area-average (box) downscale with alpha weighting, so edges of
 * transparent regions do not darken. Integer-only math. The fit
 * rule never upscales: a source that already fits is copied at
 * native size. */
bool tk_image_scale_to_fit(const uint32_t *px, int iw, int ih,
                           int max_w, int max_h,
                           uint32_t **out_px, int *ow, int *oh)
{
    if (!px || iw <= 0 || ih <= 0 || max_w <= 0 || max_h <= 0 ||
        !out_px || !ow || !oh)
        return false;
    int tw = iw, th = ih;
    if (iw > max_w || ih > max_h) {
        /* shrink by the larger ratio, keeping aspect */
        if (iw * max_h > ih * max_w) { tw = max_w; th = ih * max_w / iw; }
        else                         { th = max_h; tw = iw * max_h / ih; }
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    uint32_t *out = malloc((size_t)tw * (size_t)th * sizeof(uint32_t));
    if (!out) return false;
    if (tw == iw && th == ih) {
        memcpy(out, px, (size_t)iw * (size_t)ih * sizeof(uint32_t));
    } else {
        for (int y = 0; y < th; y++) {
            int sy0 = y * ih / th, sy1 = (y + 1) * ih / th;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int x = 0; x < tw; x++) {
                int sx0 = x * iw / tw, sx1 = (x + 1) * iw / tw;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                uint64_t sr = 0, sg = 0, sb = 0, sa = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++) {
                        uint32_t v = px[(size_t)sy * iw + sx];
                        uint32_t a = v >> 24;
                        sr += ((v >> 16) & 0xFF) * a;
                        sg += ((v >> 8) & 0xFF) * a;
                        sb += (v & 0xFF) * a;
                        sa += a;
                    }
                uint32_t a = sa
                    ? (uint32_t)(sa / ((uint64_t)(sx1 - sx0) *
                                       (uint64_t)(sy1 - sy0)))
                    : 0;
                uint32_t r = sa ? (uint32_t)(sr / sa) : 0;
                uint32_t g = sa ? (uint32_t)(sg / sa) : 0;
                uint32_t b = sa ? (uint32_t)(sb / sa) : 0;
                out[(size_t)y * tw + x] =
                    (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
    *out_px = out; *ow = tw; *oh = th;
    return true;
}

/* ---- thumbnails (M7: scaled-IDCT JPEG path) -------------------
 * The M6 thumbnail decoded the full image and box-scaled it:
 * a 1600x1200 photo held ~15MB of intermediates to produce a
 * 200x150 thumb - untenable next to a 128MB system budget when
 * a file grid decodes many of them. For baseline JPEGs the
 * thumbnail now decodes at the smallest IDCT scale (1/2, 1/4,
 * 1/8) that still covers the target box, then box-scales the
 * (small) remainder. tk_image_last_decode_peak() reports the
 * intermediate bytes the LAST call actually held, on either
 * path, so tests and apps can verify the budget claim. */
static size_t g_last_decode_peak;

size_t tk_image_last_decode_peak(void) { return g_last_decode_peak; }

static unsigned char *read_whole_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

/* the fit rule of tk_image_scale_to_fit, without decoding */
static void fit_dims(int iw, int ih, int max_w, int max_h,
                     int *tw, int *th)
{
    *tw = iw; *th = ih;
    if (iw > max_w || ih > max_h) {
        if (iw * max_h > ih * max_w) { *tw = max_w; *th = ih * max_w / iw; }
        else                         { *th = max_h; *tw = iw * max_h / ih; }
        if (*tw < 1) *tw = 1;
        if (*th < 1) *th = 1;
    }
}

bool tk_image_load_thumbnail(const char *path, int max_w, int max_h,
                             uint32_t **out_px, int *ow, int *oh)
{
    if (!path || max_w <= 0 || max_h <= 0 || !out_px || !ow || !oh)
        return false;
    g_last_decode_peak = 0;
    /* JPEG fast path: probe the header from the raw bytes and
     * decode scaled. Any failure falls through to stb. */
    size_t flen = 0;
    unsigned char *raw = read_whole_file(path, &flen);
    if (raw) {
        int iw = 0, ih = 0;
        if (tk_jpeg_probe(raw, flen, &iw, &ih)) {
            int tw, th;
            fit_dims(iw, ih, max_w, max_h, &tw, &th);
            static const int scales[4] = { 8, 4, 2, 1 };
            int scale = 1;
            for (int i = 0; i < 4; i++) {
                int s = scales[i];
                if ((iw + s - 1) / s >= tw &&
                    (ih + s - 1) / s >= th) { scale = s; break; }
            }
            uint32_t *spx = NULL; int sw = 0, sh = 0;
            size_t jpeak = 0;
            if (tk_jpeg_decode_scaled(raw, flen, scale, &spx,
                                       &sw, &sh, &jpeak)) {
                free(raw);
                bool ok = tk_image_scale_to_fit(spx, sw, sh,
                                                max_w, max_h,
                                                out_px, ow, oh);
                if (ok) {
                    /* scale_to_fit holds its output next to the
                     * scaled decode: add it to the peak */
                    g_last_decode_peak =
                        jpeak + (size_t)(*ow) * (size_t)(*oh) * 4;
                }
                free(spx);
                return ok;
            }
        }
        free(raw);
    }
    /* stb path (PNG/BMP/progressive JPEG/...): full decode,
     * then box-scale. Peak = stb RGBA + the ARGB copy (both
     * alive together in tk_image_load_rgba) + the output. */
    uint32_t *px = NULL; int iw = 0, ih = 0;
    if (!tk_image_load_rgba(path, &px, &iw, &ih)) return false;
    bool ok = tk_image_scale_to_fit(px, iw, ih, max_w, max_h,
                                    out_px, ow, oh);
    if (ok)
        g_last_decode_peak = (size_t)iw * (size_t)ih * 8 +
                             (size_t)(*ow) * (size_t)(*oh) * 4;
    free(px);
    return ok;
}
