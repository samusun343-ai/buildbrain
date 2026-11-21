/* tk_jpeg.c - baseline JPEG decoder with scaled IDCT (M7).
 *
 * Purpose: thumbnails of large JPEGs must not pay a full-size
 * decode on a 128MB device. This decoder implements the
 * standard JPEG scaled-IDCT idea (the same one libjpeg uses
 * for its 1/2, 1/4, 1/8 scale factors): only the low-frequency
 * N x N coefficients of each 8x8 block are inverse-transformed,
 * with an N-point IDCT, so a 1/8 decode touches 1/64 of the
 * full-resolution sample data and never materialises a
 * full-size image. Anything outside the supported subset
 * (progressive scans, arithmetic coding, CMYK, >8-bit samples)
 * is rejected and the caller falls back to stb_image.
 *
 * Subset: SOF0/SOF1 (sequential Huffman DCT), 8-bit samples,
 * 1 or 3 components, any sampling factors 1..4 (4:4:4, 4:2:2,
 * 4:2:0, 4:1:1, ...), 16-bit quant tables, restart markers.
 * Chroma upsampling is bilinear at the JFIF sample phase (the
 * triangle kernel libjpeg's fancy upsampling applies).
 * Peak-memory accounting: the caller learns the exact bytes
 * held for sample planes plus the output buffer.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------- marker scan / probe ---------------- */

static int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }

/* zigzag scan position -> raster index (used by the DQT
 * parser and the block decoder) */
static const uint8_t jpg_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30,
    37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
    54, 47, 55, 62, 63
};

typedef struct {
    int w, h, ncomp;
    int hid[4], vid[4], qid[4];
    int hmax, vmax;
    int quant[4][64];
    bool quant_ok[4];
    int restart_interval;
    long sos_end; /* offset just past the SOS segment */
    int dc_id[4], ac_id[4]; /* per-component table selectors */
} jpg_hdr;

/* Walk the marker segments up to SOS. On a supported header,
 * fills the geometry and returns true. */
static bool jpg_parse(const unsigned char *d, size_t len,
                      jpg_hdr *o)
{
    memset(o, 0, sizeof(*o));
    if (len < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t p = 2;
    bool have_sof = false, have_dht = false;
    while (p + 4 <= len) {
        if (d[p] != 0xFF) return false;
        int marker = d[p + 1];
        if (marker == 0xD8) { p += 2; continue; }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
            p += 2; continue; /* standalone markers */
        }
        int seglen = be16(d + p + 2);
        if (seglen < 2 || p + 2 + (size_t)seglen > len) return false;
        const unsigned char *seg = d + p + 4;
        int payload = seglen - 2;
        switch (marker) {
        case 0xC0: case 0xC1: { /* SOF0 / SOF1 */
            if (payload < 6) return false;
            if (seg[0] != 8) return false; /* precision */
            o->h = be16(seg + 1);
            o->w = be16(seg + 3);
            o->ncomp = seg[5];
            if (o->w <= 0 || o->h <= 0 ||
                (o->ncomp != 1 && o->ncomp != 3)) return false;
            if (payload < 6 + o->ncomp * 3) return false;
            o->hmax = 1; o->vmax = 1;
            for (int i = 0; i < o->ncomp; i++) {
                int hv = seg[7 + i * 3];
                o->hid[i] = hv >> 4;
                o->vid[i] = hv & 15;
                o->qid[i] = seg[8 + i * 3];
                if (o->hid[i] < 1 || o->hid[i] > 4 ||
                    o->vid[i] < 1 || o->vid[i] > 4 ||
                    o->qid[i] > 3) return false;
                if (o->hid[i] > o->hmax) o->hmax = o->hid[i];
                if (o->vid[i] > o->vmax) o->vmax = o->vid[i];
            }
            have_sof = true;
            break;
        }
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
        case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE:
        case 0xCF: case 0xCC:
            return false; /* progressive / lossless / arithmetic */
        case 0xDB: { /* DQT */
            int q = 0;
            while (q < payload) {
                int pq = seg[q] >> 4, tq = seg[q] & 15;
                if (tq > 3) return false;
                q++;
                /* DQT stores values in zigzag order; the
                 * decoder indexes quantisers by raster index,
                 * so de-zigzag on the way in */
                if (pq == 0) {
                    if (q + 64 > payload) return false;
                    for (int i = 0; i < 64; i++)
                        o->quant[tq][jpg_zigzag[i]] = seg[q + i];
                    q += 64;
                } else {
                    if (q + 128 > payload) return false;
                    for (int i = 0; i < 64; i++)
                        o->quant[tq][jpg_zigzag[i]] =
                            be16(seg + q + i * 2);
                    q += 128;
                }
                o->quant_ok[tq] = true;
            }
            break;
        }
        case 0xC4: /* DHT present (parsed again at decode) */
            have_dht = true;
            break;
        case 0xDD: /* DRI */
            if (payload >= 2) o->restart_interval = be16(seg);
            break;
        case 0xDA: { /* SOS */
            if (!have_sof || !have_dht) return false;
            if (payload < 1 + o->ncomp * 2 + 3) return false;
            int ns = seg[0];
            if (ns != o->ncomp) return false;
            for (int i = 0; i < ns; i++) {
                int id = seg[1 + i * 2];
                int tt = seg[2 + i * 2];
                /* SOS component selectors are usually the SOF
                 * component ids (often 1..n); map those onto
                 * indices, accept raw indices otherwise */
                int ci = (id >= 1 && id <= o->ncomp) ? id - 1 : id;
                if (ci < 0 || ci >= o->ncomp) return false;
                o->dc_id[ci] = tt >> 4;
                o->ac_id[ci] = tt & 15;
                if (o->dc_id[ci] > 3 || o->ac_id[ci] > 3)
                    return false;
            }
            for (int i = 0; i < o->ncomp; i++)
                if (!o->quant_ok[o->qid[i]]) return false;
            o->sos_end = (long)(p + 2 + (size_t)seglen);
            return true;
        }
        default:
            break; /* APPn / COM / ... : skipped by length */
        }
        p += 2 + (size_t)seglen;
    }
    return false;
}

bool tk_jpeg_probe(const unsigned char *data, size_t len, int *w,
                   int *h)
{
    jpg_hdr hdr;
    if (!jpg_parse(data, len, &hdr)) return false;
    if (w) *w = hdr.w;
    if (h) *h = hdr.h;
    return true;
}

/* ---------------- Huffman tables ---------------- */

typedef struct {
    uint8_t counts[16];
    uint8_t symbols[256];
    int mincode[16], maxcode[16], valptr[16];
    bool ok;
} hufftab;

static void huff_build(hufftab *t)
{
    int k = 0, code = 0;
    t->ok = true;
    for (int len = 1; len <= 16; len++) {
        int n = t->counts[len - 1];
        if (n) {
            t->valptr[len - 1] = k;
            t->mincode[len - 1] = code;
            k += n;
            code += n;
            t->maxcode[len - 1] = code - 1;
        } else {
            t->maxcode[len - 1] = -1;
        }
        code <<= 1;
    }
}

static bool parse_dht(const unsigned char *d, size_t len,
                      hufftab dc[4], hufftab ac[4])
{
    size_t p = 2;
    if (len < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    while (p + 4 <= len) {
        if (d[p] != 0xFF) return false;
        int marker = d[p + 1];
        if (marker == 0xDA) break; /* SOS: tables complete */
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
            p += 2; continue;
        }
        int seglen = be16(d + p + 2);
        if (seglen < 2 || p + 2 + (size_t)seglen > len) return false;
        if (marker == 0xC4) {
            const unsigned char *seg = d + p + 4;
            int payload = seglen - 2, q = 0;
            while (q < payload) {
                int tc = seg[q] >> 4, th = seg[q] & 15;
                if (th > 3 || tc > 1) return false;
                q++;
                if (q + 16 > payload) return false;
                hufftab *t = tc ? &ac[th] : &dc[th];
                memcpy(t->counts, seg + q, 16);
                q += 16;
                int total = 0;
                for (int i = 0; i < 16; i++) total += t->counts[i];
                if (total > 256 || q + total > payload) return false;
                memcpy(t->symbols, seg + q, (size_t)total);
                q += total;
                huff_build(t);
            }
        }
        p += 2 + (size_t)seglen;
    }
    return true;
}

/* ---------------- bit reader ---------------- */

typedef struct {
    const unsigned char *next, *end;
    uint32_t acc;
    int nbits;
    int marker; /* byte after a consumed 0xFF, or -1 */
} bitrd;

static void br_fill(bitrd *b)
{
    while (b->nbits <= 24) {
        unsigned int byte;
        if (b->marker >= 0 || b->next >= b->end) {
            byte = 0xFF; /* spec padding with 1-bits */
        } else if (b->next[0] == 0xFF) {
            if (b->next + 1 < b->end && b->next[1] == 0x00) {
                b->next += 2;
                byte = 0xFF;
            } else if (b->next + 1 < b->end) {
                b->marker = b->next[1];
                b->next += 2;
                byte = 0xFF;
            } else {
                b->next++;
                byte = 0xFF;
            }
        } else {
            byte = b->next[0];
            b->next++;
        }
        b->acc = (b->acc << 8) | byte;
        b->nbits += 8;
    }
}

static int br_bits(bitrd *b, int n)
{
    if (n == 0) return 0;
    br_fill(b);
    int v = (int)((b->acc >> (b->nbits - n)) &
                  ((1u << n) - 1u));
    b->nbits -= n;
    b->acc &= b->nbits ? ((1u << b->nbits) - 1u) : 0;
    return v;
}

static int huff_decode(bitrd *b, const hufftab *t)
{
    if (!t->ok) return -1;
    int code = br_bits(b, 1);
    for (int len = 1; len <= 16; len++) {
        if (t->maxcode[len - 1] >= 0 && code <= t->maxcode[len - 1])
            return t->symbols[t->valptr[len - 1] + code -
                              t->mincode[len - 1]];
        code = (code << 1) | br_bits(b, 1);
    }
    return -1;
}

static int receive_extend(bitrd *b, int n)
{
    if (n == 0) return 0;
    int v = br_bits(b, n);
    if (v < (1 << (n - 1))) v += 1 - (1 << n);
    return v;
}

/* ---------------- scaled IDCT ---------------- */

/* N-point 1D IDCT (N = 8, 4, 2, 1): out[x] = (1/2) sum_u
 * C(u) in[u] cos((2x+1) u pi / 2N), C(0) = 1/sqrt(2). Two
 * passes give the JPEG 1/4 normalisation, so the DC-only
 * result is F(0,0)/8 - the block average, exactly what a
 * scaled IDCT must produce. */
static void idct_1d(const float *in, float *out, int n)
{
    if (n == 1) { out[0] = in[0] * 0.5f * 0.70710678f; return; }
    for (int x = 0; x < n; x++) {
        double s = 0;
        for (int u = 0; u < n; u++) {
            double c = (u == 0) ? 0.70710678118654752 : 1.0;
            s += c * in[u] *
                 cos((2.0 * x + 1.0) * u * 3.141592653589793 /
                     (2.0 * n));
        }
        out[x] = (float)(0.5 * s);
    }
}

/* Scaled 2D IDCT: coef is the dequantised 8x8 block in raster
 * order; only the top-left n x n coefficients are read.
 * samples receives n x n bytes (level-shifted, clamped). */
static void idct2_scaled(const float *coef, uint8_t *samples,
                         int n)
{
    float stage[64];
    float line[8], out[8];
    for (int y = 0; y < n; y++) {          /* rows */
        idct_1d(coef + (size_t)y * 8, line, n);
        for (int x = 0; x < n; x++) stage[y * 8 + x] = line[x];
    }
    for (int x = 0; x < n; x++) {          /* columns */
        for (int y = 0; y < n; y++) line[y] = stage[y * 8 + x];
        idct_1d(line, out, n);
        for (int y = 0; y < n; y++) {
            int v = (int)lrintf(out[y]) + 128;
            samples[(size_t)y * n + x] =
                (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

/* ---------------- decoder ---------------- */

/* Bilinear sample of a (possibly subsampled) component plane
 * at output pixel (x, y), honouring the JFIF chroma phase:
 * component sample j is centred at full-res coordinate
 * (j + 0.5) * (max / factor) - 0.5, and output pixel x sits
 * at full-res coordinate x * scale ... expressed in output
 * units the centre of sample j is (j + 0.5) * max / factor
 * - 0.5 (the scale cancels: both axes are in output pixels).
 * When the component is at full sampling the position is
 * exactly x and this reduces to the sample itself. */
static float chroma_at(const uint8_t *plane, int pw_, int ph_,
                       int x, int y, int hf, int vf, int hmax,
                       int vmax)
{
    float u = ((float)x + 0.5f) * (float)hf / (float)hmax - 0.5f;
    float v = ((float)y + 0.5f) * (float)vf / (float)vmax - 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    float fx = u - (float)x0, fy = v - (float)y0;
    int xa = x0 < 0 ? 0 : x0 >= pw_ ? pw_ - 1 : x0;
    int xb = x0 + 1 < 0 ? 0 : x0 + 1 >= pw_ ? pw_ - 1 : x0 + 1;
    int ya = y0 < 0 ? 0 : y0 >= ph_ ? ph_ - 1 : y0;
    int yb = y0 + 1 < 0 ? 0 : y0 + 1 >= ph_ ? ph_ - 1 : y0 + 1;
    float top = plane[(size_t)ya * pw_ + xa] * (1.0f - fx) +
                plane[(size_t)ya * pw_ + xb] * fx;
    float bot = plane[(size_t)yb * pw_ + xa] * (1.0f - fx) +
                plane[(size_t)yb * pw_ + xb] * fx;
    return top * (1.0f - fy) + bot * fy;
}

bool tk_jpeg_decode_scaled(const unsigned char *data, size_t len,
                           int scale, uint32_t **out_px, int *ow,
                           int *oh, size_t *peak)
{
    if (!data || !out_px || !ow || !oh) return false;
    if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
        return false;
    jpg_hdr hdr;
    hufftab dc[4], ac[4];
    memset(dc, 0, sizeof(dc));
    memset(ac, 0, sizeof(ac));
    if (!jpg_parse(data, len, &hdr)) return false;
    if (!parse_dht(data, len, dc, ac)) return false;
    for (int i = 0; i < hdr.ncomp; i++)
        if (!dc[hdr.dc_id[i]].ok || !ac[hdr.ac_id[i]].ok)
            return false;

    int n = 8 / scale; /* IDCT size */
    int mcux = (hdr.w + hdr.hmax * 8 - 1) / (hdr.hmax * 8);
    int mcuy = (hdr.h + hdr.vmax * 8 - 1) / (hdr.vmax * 8);

    /* Per-component sample planes at scaled resolution. Each
     * plane covers the padded MCU grid, so block writes are
     * always in bounds; output reads stay inside by the
     * sampling-ratio mapping (and the max index is < the
     * padded size). */
    uint8_t *planes[4] = { NULL, NULL, NULL, NULL };
    int pw[4] = { 0, 0, 0, 0 }, ph[4] = { 0, 0, 0, 0 };
    size_t planes_bytes = 0;
    for (int i = 0; i < hdr.ncomp; i++) {
        pw[i] = mcux * hdr.hid[i] * n;
        ph[i] = mcuy * hdr.vid[i] * n;
        planes[i] = malloc((size_t)pw[i] * (size_t)ph[i]);
        if (!planes[i]) goto fail;
        planes_bytes += (size_t)pw[i] * (size_t)ph[i];
    }

    bitrd br = { data + hdr.sos_end, data + len, 0, 0, -1 };
    int dc_pred[4] = { 0, 0, 0, 0 };
    long mcu_done = 0;
    int rst_expect = 0;
    float coef[64];
    uint8_t block[64];

    for (int my = 0; my < mcuy; my++) {
        for (int mx = 0; mx < mcux; mx++) {
            if (hdr.restart_interval > 0 && mcu_done > 0 &&
                mcu_done % hdr.restart_interval == 0) {
                int want = 0xD0 + rst_expect;
                if (br.marker < 0) {
                    /* the lookahead never reached the marker:
                     * it must be the next two stream bytes
                     * (every real byte is already in the
                     * accumulator) */
                    if (br.next + 1 >= br.end ||
                        br.next[0] != 0xFF || br.next[1] != want)
                        goto fail;
                    br.next += 2;
                } else if (br.marker != want) {
                    goto fail;
                }
                br.marker = -1;
                br.acc = 0; br.nbits = 0;
                rst_expect = (rst_expect + 1) & 7;
                dc_pred[0] = dc_pred[1] = dc_pred[2] =
                    dc_pred[3] = 0;
            }
            for (int ci = 0; ci < hdr.ncomp; ci++) {
                const int *q = hdr.quant[hdr.qid[ci]];
                for (int by = 0; by < hdr.vid[ci]; by++) {
                    for (int bx = 0; bx < hdr.hid[ci]; bx++) {
                        memset(coef, 0, sizeof(coef));
                        int cat = huff_decode(&br,
                                              &dc[hdr.dc_id[ci]]);
                        if (cat < 0 || cat > 15) goto fail;
                        dc_pred[ci] += receive_extend(&br, cat);
                        coef[0] = (float)(dc_pred[ci] * q[0]);
                        int k = 1;
                        while (k < 64) {
                            int rs = huff_decode(&br,
                                                 &ac[hdr.ac_id[ci]]);
                            if (rs < 0) goto fail;
                            int run = rs >> 4, sz = rs & 15;
                            if (sz == 0) {
                                if (run != 15) break; /* EOB */
                                k += 16;
                                continue;
                            }
                            k += run;
                            if (k > 63) goto fail;
                            int v = receive_extend(&br, sz);
                            int idx = jpg_zigzag[k];
                            coef[idx] = (float)(v * q[idx]);
                            k++;
                        }
                        idct2_scaled(coef, block, n);
                        int px0 = (mx * hdr.hid[ci] + bx) * n;
                        int py0 = (my * hdr.vid[ci] + by) * n;
                        for (int y = 0; y < n; y++)
                            memcpy(planes[ci] +
                                       (size_t)(py0 + y) * pw[ci] +
                                       px0,
                                   block + (size_t)y * n,
                                   (size_t)n);
                    }
                }
            }
            mcu_done++;
        }
    }

    int W = (hdr.w + scale - 1) / scale;
    int H = (hdr.h + scale - 1) / scale;
    uint32_t *out = malloc((size_t)W * (size_t)H * 4);
    if (!out) goto fail;
    if (peak) *peak = planes_bytes + (size_t)W * (size_t)H * 4;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t px;
            if (hdr.ncomp == 1) {
                int sx = x * hdr.hid[0] / hdr.hmax;
                int sy = y * hdr.vid[0] / hdr.vmax;
                uint8_t g = planes[0][(size_t)sy * pw[0] + sx];
                px = 0xFF000000u | ((uint32_t)g << 16) |
                     ((uint32_t)g << 8) | g;
            } else {
                int sx0 = x * hdr.hid[0] / hdr.hmax;
                int sy0 = y * hdr.vid[0] / hdr.vmax;
                float Y = planes[0][(size_t)sy0 * pw[0] + sx0];
                /* Chroma is bilinearly sampled at the JFIF
                 * phase (a subsampled component's sample j is
                 * centred at (j + 0.5) * ratio - 0.5 in
                 * full-res sample coordinates). This is the
                 * same triangle kernel libjpeg's fancy
                 * upsampling applies; nearest-neighbour
                 * sampling instead leaves saturated colour
                 * edges off by a whole chroma block. */
                float Cb = chroma_at(planes[1], pw[1], ph[1],
                                     x, y, hdr.hid[1], hdr.vid[1],
                                     hdr.hmax, hdr.vmax) - 128.0f;
                float Cr = chroma_at(planes[2], pw[2], ph[2],
                                     x, y, hdr.hid[2], hdr.vid[2],
                                     hdr.hmax, hdr.vmax) - 128.0f;
                int R = (int)lrintf(Y + 1.402f * Cr);
                int G = (int)lrintf(Y - 0.344136f * Cb -
                                    0.714136f * Cr);
                int B = (int)lrintf(Y + 1.772f * Cb);
                if (R < 0) R = 0; else if (R > 255) R = 255;
                if (G < 0) G = 0; else if (G > 255) G = 255;
                if (B < 0) B = 0; else if (B > 255) B = 255;
                px = 0xFF000000u | ((uint32_t)R << 16) |
                     ((uint32_t)G << 8) | (uint32_t)B;
            }
            out[(size_t)y * W + x] = px;
        }
    }
    for (int i = 0; i < hdr.ncomp; i++) free(planes[i]);
    *out_px = out; *ow = W; *oh = H;
    return true;

fail:
    for (int i = 0; i < 4; i++) free(planes[i]);
    return false;
}
