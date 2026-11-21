/* tk_draw.c - memory canvas (RGBA8888), clipping, primitives.
 * Text metrics, glyph rasterization and wrapping live in tk_font.c
 * since M2 (real stb_truetype backend; see DESIGN section 11). */
#include "tk.h"
#include "tk_internal.h"
#include "textcore.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct tk_canvas {
    int w, h;
    uint32_t *px;
    bool own_px; /* false for tk_canvas_wrap(): caller owns pixels */
    tk_rect *clips;
    size_t n_clips, cap_clips;
    /* M3 damage accumulation: areas written since the last flush */
    tk_rect *dmg;
    size_t n_dmg, cap_dmg;
    /* M5 scroll blits: regions whose pixels were shifted in place
     * by tk_canvas_shift_region(). A backend that presents this
     * canvas from a separate buffer (the fbdev flush) must apply
     * the same shifts to its own memory before flushing damage;
     * consumers that present the canvas itself need do nothing.
     * Capped: on overflow the shifts are dropped and the affected
     * area is damaged in full, which is always correct. */
    tk_blit *blits;
    size_t n_blits, cap_blits;
};

#define TK_CANVAS_DMG_MAX 64

static tk_rect rect_union(tk_rect a, tk_rect b)
{
    int x = a.x < b.x ? a.x : b.x;
    int y = a.y < b.y ? a.y : b.y;
    int x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tk_rect){x, y, x2 - x, y2 - y};
}

/* Overlap or edge-abut (abutting rects merge: they flush as one). */
static bool rect_touch(tk_rect a, tk_rect b)
{
    return a.x <= b.x + b.w && b.x <= a.x + a.w &&
           a.y <= b.y + b.h && b.y <= a.y + a.h;
}

static void damage_add(tk_canvas *c, tk_rect r)
{
    int x0 = r.x > 0 ? r.x : 0;
    int y0 = r.y > 0 ? r.y : 0;
    int x1 = r.x + r.w < c->w ? r.x + r.w : c->w;
    int y1 = r.y + r.h < c->h ? r.y + r.h : c->h;
    if (x1 <= x0 || y1 <= y0) return;
    r = (tk_rect){x0, y0, x1 - x0, y1 - y0};
    /* fast path: contained in the most recent rect (glyph runs) */
    if (c->n_dmg) {
        tk_rect *l = &c->dmg[c->n_dmg - 1];
        if (r.x >= l->x && r.y >= l->y &&
            r.x + r.w <= l->x + l->w && r.y + r.h <= l->y + l->h)
            return;
    }
    /* merge with any touching rects, repeatedly */
    for (;;) {
        bool merged = false;
        for (size_t i = 0; i < c->n_dmg; i++) {
            if (rect_touch(r, c->dmg[i])) {
                r = rect_union(r, c->dmg[i]);
                memmove(c->dmg + i, c->dmg + i + 1,
                        (c->n_dmg - i - 1) * sizeof(tk_rect));
                c->n_dmg--;
                merged = true;
                break;
            }
        }
        if (!merged) break;
    }
    if (c->n_dmg >= TK_CANVAS_DMG_MAX) {
        /* collapse everything into the bounding box */
        for (size_t i = 0; i < c->n_dmg; i++)
            r = rect_union(r, c->dmg[i]);
        c->dmg[0] = r;
        c->n_dmg = 1;
        return;
    }
    if (c->n_dmg >= c->cap_dmg) {
        size_t nc = c->cap_dmg ? c->cap_dmg * 2 : 8;
        tk_rect *na = realloc(c->dmg, nc * sizeof(*na));
        if (!na) return;
        c->dmg = na; c->cap_dmg = nc;
    }
    c->dmg[c->n_dmg++] = r;
}

void tk_canvas_invalidate(tk_canvas *c, tk_rect r)
{
    if (c) damage_add(c, r);
}

size_t tk_canvas_damage_count(const tk_canvas *c)
{
    return c ? c->n_dmg : 0;
}

tk_rect tk_canvas_damage_rect(const tk_canvas *c, size_t i)
{
    if (!c || i >= c->n_dmg) return (tk_rect){0, 0, 0, 0};
    return c->dmg[i];
}

tk_rect tk_canvas_damage_bounds(const tk_canvas *c)
{
    if (!c || !c->n_dmg) return (tk_rect){0, 0, 0, 0};
    tk_rect r = c->dmg[0];
    for (size_t i = 1; i < c->n_dmg; i++) r = rect_union(r, c->dmg[i]);
    return r;
}

void tk_canvas_damage_clear(tk_canvas *c)
{
    if (c) c->n_dmg = 0;
}

static uint32_t pack(tk_color c)
{
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

tk_canvas *tk_canvas_new(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    tk_canvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->w = w; c->h = h;
    c->px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!c->px) { free(c); return NULL; }
    c->own_px = true;
    damage_add(c, (tk_rect){0, 0, w, h}); /* starts fully damaged */
    return c;
}

tk_canvas *tk_canvas_wrap(int w, int h, uint32_t *px)
{
    if (w <= 0 || h <= 0 || !px) return NULL;
    tk_canvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->w = w; c->h = h;
    c->px = px;
    c->own_px = false;
    damage_add(c, (tk_rect){0, 0, w, h}); /* starts fully damaged */
    return c;
}

void tk_canvas_free(tk_canvas *c)
{
    if (!c) return;
    free(c->clips);
    free(c->dmg);
    free(c->blits);
    if (c->own_px) free(c->px);
    free(c);
}

int tk_canvas_width(const tk_canvas *c) { return c->w; }
int tk_canvas_height(const tk_canvas *c) { return c->h; }
uint32_t *tk_canvas_pixels(tk_canvas *c) { return c->px; }

static tk_rect clip_of(const tk_canvas *c)
{
    if (c->n_clips) return c->clips[c->n_clips - 1];
    return (tk_rect){0, 0, c->w, c->h};
}

tk_rect tk_canvas_clip_bounds(const tk_canvas *c)
{
    if (!c) return (tk_rect){0, 0, 0, 0};
    return clip_of(c);
}

void tk_canvas_push_clip(tk_canvas *c, tk_rect r)
{
    tk_rect cur = clip_of(c);
    tk_rect n = { r.x > cur.x ? r.x : cur.x, r.y > cur.y ? r.y : cur.y,
                  0, 0 };
    int x2 = r.x + r.w < cur.x + cur.w ? r.x + r.w : cur.x + cur.w;
    int y2 = r.y + r.h < cur.y + cur.h ? r.y + r.h : cur.y + cur.h;
    n.w = x2 > n.x ? x2 - n.x : 0;
    n.h = y2 > n.y ? y2 - n.y : 0;
    if (c->n_clips >= c->cap_clips) {
        size_t nc = c->cap_clips ? c->cap_clips * 2 : 8;
        tk_rect *na = realloc(c->clips, nc * sizeof(*na));
        if (!na) return;
        c->clips = na; c->cap_clips = nc;
    }
    c->clips[c->n_clips++] = n;
}

void tk_canvas_pop_clip(tk_canvas *c)
{
    if (c->n_clips) c->n_clips--;
}

void tk_canvas_clear(tk_canvas *c, tk_color col)
{
    uint32_t v = pack(col);
    for (size_t i = 0, n = (size_t)c->w * c->h; i < n; i++) c->px[i] = v;
    c->n_dmg = 0;
    c->n_blits = 0; /* wholesale rewrite: pending shifts are moot */
    damage_add(c, (tk_rect){0, 0, c->w, c->h});
}

/* ---- scroll shifts (M5) ---------------------------------------- */

size_t tk_canvas_blit_count(const tk_canvas *c)
{
    return c ? c->n_blits : 0;
}

tk_blit tk_canvas_blit_at(const tk_canvas *c, size_t i)
{
    if (!c || i >= c->n_blits) return (tk_blit){{0, 0, 0, 0}, 0};
    return c->blits[i];
}

void tk_canvas_blits_clear(tk_canvas *c)
{
    if (c) c->n_blits = 0;
}

#define TK_CANVAS_BLIT_MAX 16

tk_rect tk_canvas_shift_region(tk_canvas *c, tk_rect r, int dy)
{
    tk_rect none = {0, 0, 0, 0};
    if (!c || dy == 0) return none;
    /* clip to the canvas (the caller guarantees the region is
     * inside the current clip; the shift must not invent pixels
     * from outside the region) */
    int x0 = r.x > 0 ? r.x : 0;
    int y0 = r.y > 0 ? r.y : 0;
    int x1 = r.x + r.w < c->w ? r.x + r.w : c->w;
    int y1 = r.y + r.h < c->h ? r.y + r.h : c->h;
    if (x1 <= x0 || y1 <= y0) return none;
    int w = x1 - x0, h = y1 - y0;
    if (dy >= h || dy <= -h) return none; /* nothing retained */
    /* move the retained rows (row segments are contiguous) */
    if (dy > 0) {          /* content moves down: band at the top */
        for (int y = h - 1; y >= dy; y--)
            memmove(&c->px[(size_t)(y0 + y) * c->w + x0],
                    &c->px[(size_t)(y0 + y - dy) * c->w + x0],
                    (size_t)w * sizeof(uint32_t));
    } else {               /* content moves up: band at the bottom */
        for (int y = 0; y < h + dy; y++)
            memmove(&c->px[(size_t)(y0 + y) * c->w + x0],
                    &c->px[(size_t)(y0 + y - dy) * c->w + x0],
                    (size_t)w * sizeof(uint32_t));
    }
    tk_rect band = dy > 0 ? (tk_rect){x0, y0, w, dy}
                          : (tk_rect){x0, y1 + dy, w, -dy};
    if (c->n_blits >= TK_CANVAS_BLIT_MAX) {
        /* Too many shifts to replay on the backend: drop them all
         * and damage every shifted area in full (always correct,
         * just slower). */
        for (size_t i = 0; i < c->n_blits; i++)
            damage_add(c, c->blits[i].rect);
        c->n_blits = 0;
        damage_add(c, (tk_rect){x0, y0, w, h});
        return band;
    }
    if (c->n_blits >= c->cap_blits) {
        size_t nc = c->cap_blits ? c->cap_blits * 2 : 4;
        tk_blit *na = realloc(c->blits, nc * sizeof(*na));
        if (!na) { damage_add(c, (tk_rect){x0, y0, w, h}); return band; }
        c->blits = na; c->cap_blits = nc;
    }
    c->blits[c->n_blits++] = (tk_blit){{x0, y0, w, h}, dy};
    /* only the exposed band is damaged: the retained area already
     * holds the right pixels in this canvas, and the backend
     * replays the shift in its own memory */
    damage_add(c, band);
    return band;
}

void tk_canvas_fill_rect(tk_canvas *c, tk_rect r, tk_color col)
{
    tk_rect cl = clip_of(c);
    int x0 = r.x > cl.x ? r.x : cl.x;
    int y0 = r.y > cl.y ? r.y : cl.y;
    int x1 = r.x + r.w < cl.x + cl.w ? r.x + r.w : cl.x + cl.w;
    int y1 = r.y + r.h < cl.y + cl.h ? r.y + r.h : cl.y + cl.h;
    if (x1 <= x0 || y1 <= y0) return;
    damage_add(c, (tk_rect){x0, y0, x1 - x0, y1 - y0});
    uint32_t v = pack(col);
    if (col.a == 255) {
        for (int y = y0; y < y1; y++) {
            uint32_t *row = &c->px[(size_t)y * c->w + x0];
            for (int x = x0; x < x1; x++) *row++ = v;
        }
    } else {
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                uint32_t *p = &c->px[(size_t)y * c->w + x];
                uint32_t d = *p;
                int a = col.a, ia = 255 - a;
                uint8_t rr = (uint8_t)((col.r * a + ((d >> 16) & 0xFF) * ia) / 255);
                uint8_t gg = (uint8_t)((col.g * a + ((d >> 8) & 0xFF) * ia) / 255);
                uint8_t bb = (uint8_t)((col.b * a + (d & 0xFF) * ia) / 255);
                *p = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
        }
    }
}

void tk_canvas_stroke_rect(tk_canvas *c, tk_rect r, int width, tk_color col)
{
    if (width <= 0) return;
    tk_canvas_fill_rect(c, (tk_rect){r.x, r.y, r.w, width}, col);
    tk_canvas_fill_rect(c, (tk_rect){r.x, r.y + r.h - width, r.w, width}, col);
    tk_canvas_fill_rect(c, (tk_rect){r.x, r.y, width, r.h}, col);
    tk_canvas_fill_rect(c, (tk_rect){r.x + r.w - width, r.y, width, r.h}, col);
}

static int tk_isqrt(int v)
{
    int s = 0;
    while ((s + 1) * (s + 1) <= v) s++;
    return s;
}

/* M13: one row's inset for a corner of the given radius, in
 * the pixel-center geometry of tk_canvas_fill_round_rect. */
static int corner_row_inset(int h, int radius, int ry)
{
    if (radius <= 0) return 0;
    int dyc;
    if (ry < radius) dyc = 2 * (radius - ry) - 1;
    else if (ry >= h - radius) dyc = 2 * (ry - (h - radius)) + 1;
    else return 0;
    int half = (tk_isqrt(4 * radius * radius - dyc * dyc) + 1) / 2;
    int inset = radius - half;
    return inset < 0 ? 0 : inset;
}

static void fill_round_rect_gen(tk_canvas *c, tk_rect r,
                                int rtl, int rtr, int rbr, int rbl,
                                tk_color col)
{
    if (rtl > r.w / 2) rtl = r.w / 2;
    if (rtr > r.w / 2) rtr = r.w / 2;
    if (rbr > r.w / 2) rbr = r.w / 2;
    if (rbl > r.w / 2) rbl = r.w / 2;
    if (rtl > r.h / 2) rtl = r.h / 2;
    if (rtr > r.h / 2) rtr = r.h / 2;
    if (rbr > r.h / 2) rbr = r.h / 2;
    if (rbl > r.h / 2) rbl = r.h / 2;
    tk_rect cl = clip_of(c);
    int cy0 = r.y > cl.y ? r.y : cl.y;
    int cy1 = r.y + r.h < cl.y + cl.h ? r.y + r.h : cl.y + cl.h;
    if (cy1 <= cy0) return;
    int bx0 = r.x > cl.x ? r.x : cl.x;
    int bx1 = r.x + r.w < cl.x + cl.w ? r.x + r.w : cl.x + cl.w;
    if (bx1 <= bx0) return;
    damage_add(c, (tk_rect){bx0, cy0, bx1 - bx0, cy1 - cy0});
    uint32_t v = pack(col);
    for (int y = cy0; y < cy1; y++) {
        int ry = y - r.y;
        int li = corner_row_inset(r.h, ry < r.h / 2 ? rtl : rbl, ry);
        int ri = corner_row_inset(r.h, ry < r.h / 2 ? rtr : rbr, ry);
        int x0 = r.x + li > bx0 ? r.x + li : bx0;
        int x1 = r.x + r.w - ri < bx1 ? r.x + r.w - ri : bx1;
        if (x1 <= x0) continue;
        uint32_t *row = &c->px[(size_t)y * c->w];
        if (col.a == 255) {
            for (int x = x0; x < x1; x++) row[x] = v;
        } else {
            int a = col.a, ia = 255 - a;
            for (int x = x0; x < x1; x++) {
                uint32_t d = row[x];
                uint8_t rr = (uint8_t)((col.r * a + ((d >> 16) & 0xFF) * ia) / 255);
                uint8_t gg = (uint8_t)((col.g * a + ((d >> 8) & 0xFF) * ia) / 255);
                uint8_t bb = (uint8_t)((col.b * a + (d & 0xFF) * ia) / 255);
                row[x] = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
        }
    }
}

void tk_canvas_fill_round_rect4(tk_canvas *c, tk_rect r,
                                int rtl, int rtr, int rbr, int rbl,
                                tk_color col)
{
    if (rtl <= 0 && rtr <= 0 && rbr <= 0 && rbl <= 0) {
        tk_canvas_fill_rect(c, r, col);
        return;
    }
    fill_round_rect_gen(c, r, rtl, rtr, rbr, rbl, col);
}

void tk_canvas_fill_round_rect(tk_canvas *c, tk_rect r, int radius, tk_color col)
{
    if (radius <= 0) { tk_canvas_fill_rect(c, r, col); return; }
    if (radius > r.w / 2) radius = r.w / 2;
    if (radius > r.h / 2) radius = r.h / 2;
    if (radius <= 0) { tk_canvas_fill_rect(c, r, col); return; }
    /* Row-by-row with pixel-center geometry: the corner circle has
     * its center at (radius, radius) (and mirror images); a row
     * whose center is dyc/2 px from the center row is filled from
     * x = radius - sqrt(radius^2 - (dyc/2)^2). The previous code
     * fed the circle test an inverted distance, which made the
     * inset GROW toward the corner-center rows and vanish at the
     * edges - a fully rounded rect (radius == h/2: slider thumbs,
     * switch tracks/knobs) came out as an hourglass.
     * M5: the rows are written directly (one clip computation and
     * one damage record for the whole shape - the per-row
     * fill_rect calls merged into the same bounding damage anyway)
     * because the v2 style paints rounded shapes everywhere. The
     * blend arithmetic for a < 255 is tk_canvas_fill_rect's. */
    tk_rect cl = clip_of(c);
    int cy0 = r.y > cl.y ? r.y : cl.y;
    int cy1 = r.y + r.h < cl.y + cl.h ? r.y + r.h : cl.y + cl.h;
    if (cy1 <= cy0) return;
    int bx0 = r.x > cl.x ? r.x : cl.x;
    int bx1 = r.x + r.w < cl.x + cl.w ? r.x + r.w : cl.x + cl.w;
    if (bx1 <= bx0) return;
    damage_add(c, (tk_rect){bx0, cy0, bx1 - bx0, cy1 - cy0});
    uint32_t v = pack(col);
    for (int y = cy0; y < cy1; y++) {
        int ry = y - r.y;
        int dyc; /* 2x distance from this row's center to the
                  * corner-center row; 0 in the straight band */
        if (ry < radius) dyc = 2 * (radius - ry) - 1;
        else if (ry >= r.h - radius) dyc = 2 * (ry - (r.h - radius)) + 1;
        else dyc = 0;
        int inset = 0;
        if (dyc > 0) {
            /* (2*half)^2 + dyc^2 <= (2*radius)^2 */
            int half = (tk_isqrt(4 * radius * radius - dyc * dyc) + 1) / 2;
            inset = radius - half;
            if (inset < 0) inset = 0;
        }
        int x0 = r.x + inset > bx0 ? r.x + inset : bx0;
        int x1 = r.x + r.w - inset < bx1 ? r.x + r.w - inset : bx1;
        if (x1 <= x0) continue;
        uint32_t *row = &c->px[(size_t)y * c->w];
        if (col.a == 255) {
            for (int x = x0; x < x1; x++) row[x] = v;
        } else {
            int a = col.a, ia = 255 - a;
            for (int x = x0; x < x1; x++) {
                uint32_t d = row[x];
                uint8_t rr = (uint8_t)((col.r * a + ((d >> 16) & 0xFF) * ia) / 255);
                uint8_t gg = (uint8_t)((col.g * a + ((d >> 8) & 0xFF) * ia) / 255);
                uint8_t bb = (uint8_t)((col.b * a + (d & 0xFF) * ia) / 255);
                row[x] = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
        }
    }
}

/* M17: fill a circle (centre cx,cy, radius; fractional values
 * allowed) clipped to a rounded rect with per-corner radii -
 * the M3 ripple disc inside a component's shape (the split
 * button's asymmetric corners included). Row spans are the
 * intersection of the circle's row span with the rounded
 * rect's row span (the same pixel-center geometry as
 * tk_canvas_fill_round_rect / fill_round_rect_gen), written
 * directly like the other row painters: one clip computation,
 * one damage record. */
void tk_canvas_fill_circle_rrect4(tk_canvas *c, tk_rect r,
                                  int rtl, int rtr, int rbr,
                                  int rbl, float cx, float cy,
                                  float radius, tk_color col)
{
    if (!c || radius <= 0.0f || col.a == 0) return;
    if (rtl > r.w / 2) rtl = r.w / 2;
    if (rtr > r.w / 2) rtr = r.w / 2;
    if (rbr > r.w / 2) rbr = r.w / 2;
    if (rbl > r.w / 2) rbl = r.w / 2;
    if (rtl > r.h / 2) rtl = r.h / 2;
    if (rtr > r.h / 2) rtr = r.h / 2;
    if (rbr > r.h / 2) rbr = r.h / 2;
    if (rbl > r.h / 2) rbl = r.h / 2;
    if (rtl < 0) rtl = 0;
    if (rtr < 0) rtr = 0;
    if (rbr < 0) rbr = 0;
    if (rbl < 0) rbl = 0;
    tk_rect cl = clip_of(c);
    int cy0 = (int)ceilf(cy - radius);
    if (cy0 < r.y) cy0 = r.y;
    if (cy0 < cl.y) cy0 = cl.y;
    int cy1 = (int)ceilf(cy + radius);
    if (cy1 > r.y + r.h) cy1 = r.y + r.h;
    if (cy1 > cl.y + cl.h) cy1 = cl.y + cl.h;
    if (cy1 <= cy0) return;
    int bx0 = r.x > cl.x ? r.x : cl.x;
    int bx1 = r.x + r.w < cl.x + cl.w ? r.x + r.w : cl.x + cl.w;
    if (bx1 <= bx0) return;
    damage_add(c, (tk_rect){bx0, cy0, bx1 - bx0, cy1 - cy0});
    uint32_t v = pack(col);
    for (int y = cy0; y < cy1; y++) {
        float dy = (float)y + 0.5f - cy;
        float rem = radius * radius - dy * dy;
        if (rem <= 0.0f) continue;
        float hw = sqrtf(rem);
        int x0 = (int)ceilf(cx - hw);
        int x1 = (int)ceilf(cx + hw);
        int ry = y - r.y;
        int li = corner_row_inset(r.h, ry < r.h / 2 ? rtl : rbl,
                                  ry);
        int ri = corner_row_inset(r.h, ry < r.h / 2 ? rtr : rbr,
                                  ry);
        if (x0 < r.x + li) x0 = r.x + li;
        if (x1 > r.x + r.w - ri) x1 = r.x + r.w - ri;
        if (x0 < bx0) x0 = bx0;
        if (x1 > bx1) x1 = bx1;
        if (x1 <= x0) continue;
        uint32_t *row = &c->px[(size_t)y * c->w];
        if (col.a == 255) {
            for (int x = x0; x < x1; x++) row[x] = v;
        } else {
            int a = col.a, ia = 255 - a;
            for (int x = x0; x < x1; x++) {
                uint32_t d = row[x];
                uint8_t rr = (uint8_t)((col.r * a + ((d >> 16) & 0xFF) * ia) / 255);
                uint8_t gg = (uint8_t)((col.g * a + ((d >> 8) & 0xFF) * ia) / 255);
                uint8_t bb = (uint8_t)((col.b * a + (d & 0xFF) * ia) / 255);
                row[x] = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
        }
    }
}

/* Horizontal inset of a rounded corner for row y of a rect of
 * height h, using the same pixel-center geometry as
 * tk_canvas_fill_round_rect(). */
static int row_corner_inset(int h, int radius, int y)
{
    if (radius <= 0) return 0;
    int dyc;
    if (y < radius) dyc = 2 * (radius - y) - 1;
    else if (y >= h - radius) dyc = 2 * (y - (h - radius)) + 1;
    else return 0;
    int half = (tk_isqrt(4 * radius * radius - dyc * dyc) + 1) / 2;
    int inset = radius - half;
    return inset < 0 ? 0 : inset;
}

void tk_canvas_stroke_round_rect(tk_canvas *c, tk_rect r, int radius,
                                 int width, tk_color col)
{
    if (width <= 0) return;
    if (radius <= 0) { tk_canvas_stroke_rect(c, r, width, col); return; }
    if (radius > r.w / 2) radius = r.w / 2;
    if (radius > r.h / 2) radius = r.h / 2;
    if (radius <= 0) { tk_canvas_stroke_rect(c, r, width, col); return; }
    tk_rect in = { r.x + width, r.y + width,
                   r.w - 2 * width, r.h - 2 * width };
    int irad = radius - width;
    if (irad < 0) irad = 0;
    for (int y = 0; y < r.h; y++) {
        int io = row_corner_inset(r.h, radius, y);
        int x0 = r.x + io, x1 = r.x + r.w - io;
        if (y < width || y >= r.h - width || in.w <= 0 || in.h <= 0) {
            tk_canvas_fill_rect(c, (tk_rect){x0, r.y + y, x1 - x0, 1},
                                col);
        } else {
            int ii = row_corner_inset(in.h, irad, y - width);
            int ix0 = in.x + ii, ix1 = in.x + in.w - ii;
            if (ix0 > x0)
                tk_canvas_fill_rect(c, (tk_rect){x0, r.y + y, ix0 - x0, 1},
                                    col);
            if (ix1 < x1)
                tk_canvas_fill_rect(c, (tk_rect){ix1, r.y + y, x1 - ix1, 1},
                                    col);
        }
    }
}

tk_color tk_canvas_pixel(const tk_canvas *c, int x, int y)
{
    tk_color out = {0, 0, 0, 0};
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return out;
    uint32_t v = c->px[(size_t)y * c->w + x];
    out.a = (uint8_t)(v >> 24); out.r = (uint8_t)(v >> 16);
    out.g = (uint8_t)(v >> 8); out.b = (uint8_t)v;
    return out;
}

/* --- single-pixel access honoring the clip stack (tk_font/tk_widgets
 *     use these for glyph blending and image blitting) ------------- */

void tk_canvas_blend_pixel(tk_canvas *c, int x, int y, tk_color col)
{
    tk_rect cl = clip_of(c);
    if (x < cl.x || y < cl.y || x >= cl.x + cl.w || y >= cl.y + cl.h) return;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    if (col.a == 0) return;
    damage_add(c, (tk_rect){x, y, 1, 1});
    uint32_t *p = &c->px[(size_t)y * c->w + x];
    if (col.a == 255) { *p = pack(col); return; }
    if (col.a == 0) return;
    uint32_t d = *p;
    int a = col.a, ia = 255 - a;
    uint8_t rr = (uint8_t)((col.r * a + ((d >> 16) & 0xFF) * ia) / 255);
    uint8_t gg = (uint8_t)((col.g * a + ((d >> 8) & 0xFF) * ia) / 255);
    uint8_t bb = (uint8_t)((col.b * a + (d & 0xFF) * ia) / 255);
    *p = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
}

void tk_canvas_put_pixel(tk_canvas *c, int x, int y, tk_color col)
{
    tk_rect cl = clip_of(c);
    if (x < cl.x || y < cl.y || x >= cl.x + cl.w || y >= cl.y + cl.h) return;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    damage_add(c, (tk_rect){x, y, 1, 1});
    c->px[(size_t)y * c->w + x] = pack(col);
}

/* Rotated image blit (M8, see tk.h). The scan walks the rotated
 * bounding box (at most (w+h)^2 pixels for the square icons this
 * serves) and inverse-maps each destination pixel centre into
 * source space; nothing is allocated and no intermediate image
 * exists. Per-pixel store/blend semantics are blit_image's. */
void tk_canvas_draw_image_rot(tk_canvas *c, const uint32_t *px,
                              int iw, int ih, tk_rect dst,
                              float angle_deg)
{
    if (!c || !px || iw <= 0 || ih <= 0 || dst.w <= 0 || dst.h <= 0)
        return;
    float rad = angle_deg * 0.0174532925f;
    float cs = cosf(rad), sn = sinf(rad);
    float cx = dst.x + dst.w * 0.5f, cy = dst.y + dst.h * 0.5f;
    float hw = (fabsf(dst.w * cs) + fabsf(dst.h * sn)) * 0.5f;
    float hh = (fabsf(dst.w * sn) + fabsf(dst.h * cs)) * 0.5f;
    int x0 = (int)floorf(cx - hw), x1 = (int)ceilf(cx + hw);
    int y0 = (int)floorf(cy - hh), y1 = (int)ceilf(cy + hh);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float sx = dx * cs + dy * sn;
            float sy = -dx * sn + dy * cs;
            float u = sx + dst.w * 0.5f, v = sy + dst.h * 0.5f;
            if (u < 0 || v < 0 || u >= dst.w || v >= dst.h) continue;
            int ix = (int)(u * (float)iw / (float)dst.w);
            int iy = (int)(v * (float)ih / (float)dst.h);
            uint32_t val = px[(size_t)iy * (size_t)iw + (size_t)ix];
            tk_color col = { (uint8_t)(val >> 16), (uint8_t)(val >> 8),
                             (uint8_t)val, (uint8_t)(val >> 24) };
            if (col.a == 255) tk_canvas_put_pixel(c, x, y, col);
            else if (col.a) tk_canvas_blend_pixel(c, x, y, col);
        }
    }
}

/* Scaled blit (M20, see tk.h). Bilinear with pixel-centre
 * mapping: destination pixel (x, y) samples the source at
 * ((x + 0.5) * src_w / dst_w - 0.5) within src_rect, clamped
 * to the rect's edge pixels. The arithmetic is per-channel
 * linear interpolation with round-to-nearest, the same
 * rounding the blend paths use. */
void tk_canvas_blit_scaled(tk_canvas *c, const tk_canvas *src,
                           tk_rect src_rect, tk_rect dst_rect)
{
    if (!c || !src || src == c) return;
    if (src_rect.w <= 0 || src_rect.h <= 0 ||
        dst_rect.w <= 0 || dst_rect.h <= 0) return;
    if (src_rect.x < 0 || src_rect.y < 0 ||
        src_rect.x + src_rect.w > src->w ||
        src_rect.y + src_rect.h > src->h) return;
    tk_rect cl = clip_of(c);
    int x0 = dst_rect.x > cl.x ? dst_rect.x : cl.x;
    int y0 = dst_rect.y > cl.y ? dst_rect.y : cl.y;
    int x1 = dst_rect.x + dst_rect.w < cl.x + cl.w
                 ? dst_rect.x + dst_rect.w : cl.x + cl.w;
    int y1 = dst_rect.y + dst_rect.h < cl.y + cl.h
                 ? dst_rect.y + dst_rect.h : cl.y + cl.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    if (x1 <= x0 || y1 <= y0) return;
    damage_add(c, (tk_rect){x0, y0, x1 - x0, y1 - y0});
    float kx = (float)src_rect.w / (float)dst_rect.w;
    float ky = (float)src_rect.h / (float)dst_rect.h;
    int sx_max = src_rect.x + src_rect.w - 1;
    int sy_max = src_rect.y + src_rect.h - 1;
    for (int y = y0; y < y1; y++) {
        float v = src_rect.y + (y - dst_rect.y + 0.5f) * ky - 0.5f;
        if (v < src_rect.y) v = (float)src_rect.y;
        if (v > sy_max) v = (float)sy_max;
        int iy0 = (int)v;
        float fy = v - (float)iy0;
        int iy1 = iy0 + 1 > sy_max ? sy_max : iy0 + 1;
        const uint32_t *row0 = &src->px[(size_t)iy0 * src->w];
        const uint32_t *row1 = &src->px[(size_t)iy1 * src->w];
        uint32_t *drow = &c->px[(size_t)y * c->w];
        for (int x = x0; x < x1; x++) {
            float u = src_rect.x + (x - dst_rect.x + 0.5f) * kx - 0.5f;
            if (u < src_rect.x) u = (float)src_rect.x;
            if (u > sx_max) u = (float)sx_max;
            int ix0 = (int)u;
            float fx = u - (float)ix0;
            int ix1 = ix0 + 1 > sx_max ? sx_max : ix0 + 1;
            uint32_t p00 = row0[ix0], p10 = row0[ix1];
            uint32_t p01 = row1[ix0], p11 = row1[ix1];
            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;
            uint32_t out = 0;
            for (int sh = 0; sh <= 24; sh += 8) {
                float val = ((p00 >> sh) & 0xFF) * w00 +
                            ((p10 >> sh) & 0xFF) * w10 +
                            ((p01 >> sh) & 0xFF) * w01 +
                            ((p11 >> sh) & 0xFF) * w11;
                out |= (uint32_t)(val + 0.5f) << sh;
            }
            drow[x] = out;
        }
    }
}

/* Bulk alpha blit (M5): blend a w*h 8-bit coverage bitmap (row
 * stride `stride`) tinted with `col` into the canvas. This is the
 * glyph painter's hot path: the clip intersection and the damage
 * record are computed ONCE for the whole glyph instead of per
 * pixel (the old path ran clip test + damage merge per pixel).
 * The arithmetic is tk_canvas_blend_pixel's, unchanged: effective
 * alpha a' = col.a * cov / 255, then (s*a' + d*(255-a')) / 255 per
 * channel with an opaque result; a' == 255 stores the colour. */
void tk_canvas_blit_alpha(tk_canvas *c, int x, int y, int w, int h,
                          const uint8_t *alpha, int stride, tk_color col)
{
    if (!c || !alpha || w <= 0 || h <= 0 || col.a == 0) return;
    tk_rect cl = clip_of(c);
    int x0 = x > cl.x ? x : cl.x;
    int y0 = y > cl.y ? y : cl.y;
    int x1 = x + w < cl.x + cl.w ? x + w : cl.x + cl.w;
    int y1 = y + h < cl.y + cl.h ? y + h : cl.y + cl.h;
    if (x1 <= x0 || y1 <= y0) return;
    damage_add(c, (tk_rect){x0, y0, x1 - x0, y1 - y0});
    uint32_t solid = pack((tk_color){col.r, col.g, col.b, 255});
    for (int yy = y0; yy < y1; yy++) {
        const uint8_t *arow = alpha + (size_t)(yy - y) * (size_t)stride;
        uint32_t *row = &c->px[(size_t)yy * c->w];
        for (int xx = x0; xx < x1; xx++) {
            int a = (int)col.a * arow[xx - x] / 255;
            if (!a) continue;
            if (a == 255) { row[xx] = solid; continue; }
            uint32_t d = row[xx];
            int ia = 255 - a;
            uint8_t rr = (uint8_t)((col.r * a + ((d >> 16) & 0xFF) * ia) / 255);
            uint8_t gg = (uint8_t)((col.g * a + ((d >> 8) & 0xFF) * ia) / 255);
            uint8_t bb = (uint8_t)((col.b * a + (d & 0xFF) * ia) / 255);
            row[xx] = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
        }
    }
}
