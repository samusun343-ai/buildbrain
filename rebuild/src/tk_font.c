/* tk_font.c - font backend for libbrainui2 (M2; shaping upgraded in M24).
 *
 * Font loading, metrics and rasterization on FreeType; text shaping
 * (ligatures, GPOS kerning, combining-mark clusters) on HarfBuzz.
 * M24 replaced the stb_truetype backend: measurement, drawing and
 * wrapping all consume HarfBuzz-shaped glyph runs instead of
 * per-codepoint glyph lookups. The public API (tk.h) is unchanged.
 *
 * Fallback policy (also documented in tk.h and DESIGN section 11):
 * fonts form a chain; a run of codepoints missing from a font is
 * shaped with the next font in the chain that covers it; missing
 * everywhere, the head font's .notdef glyph is used. With no font
 * installed at all, measurement falls back to the M1 monospace
 * approximation and draw_text paints nothing (headless-test path
 * only, not a product path).
 *
 * Shaping policy (REPORT-toolkit-m24.md):
 * - Itemization follows the fallback chain: consecutive codepoints
 *   served by the same font form one item, shaped as a single LTR
 *   run. FriBidi is deliberately NOT integrated: the bundled fonts
 *   carry no RTL-script glyphs and the UI content is Japanese +
 *   Latin, so visual-order reordering has nothing to act on. The
 *   item boundary is the seam where a future FriBidi split slots in.
 * - Rasterization loads glyphs with FT_LOAD_NO_HINTING, matching
 *   the previous backend's geometry exactly; the M24 change is
 *   shaping correctness only. Hinting is a separate evaluation.
 * - Shaped runs are never cached: shaping is a per-call scratch
 *   operation in one reused buffer. The only retained text cache
 *   is the glyph bitmap cache below (existing budget:
 *   TK_FONT_CACHE_CAP entries per font, ring eviction).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* dladdr (M23 library-relative font search);
                     * must precede every include - __GLIBC__ is
                     * not defined yet at this point */
#endif
#include "tk_internal.h"
#include <stdio.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <dlfcn.h>
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

/* ------------------------------------------------------------------ */
/* Font object + glyph cache                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int glyph;               /* glyph index within this font */
    int px;
    int adv;                 /* advance in pixels at this size */
    int w, h, xoff, yoff;
    unsigned char *bmp;      /* w*h alpha bytes, owned */
    bool used;
} glyph_entry;

/* Shaped fonts are cached per pixel size: the toolkit uses a
 * handful of text styles, so a small ring is plenty. */
#define TK_HB_FONT_SLOTS 8

typedef struct {
    int px;
    hb_font_t *font;
} hb_font_slot;

struct tk_font {
    unsigned char *data;     /* whole font file, owned; referenced
                              * (not copied) by both FT and HarfBuzz */
    FT_Face ft;
    hb_blob_t *hb_blob;
    hb_face_t *hb_face;
    hb_font_slot hb_fonts[TK_HB_FONT_SLOTS];
    int n_hb_fonts;
    unsigned hb_ring;
    tk_font *fallback;
    bool fallback_owned;     /* fallback attached by the bundled
                              * loader: freed with this font */
    glyph_entry *cache;
    size_t n_cache, cap_cache, ring;
};

#define TK_FONT_CACHE_CAP 1024

static tk_font *g_current_font;

static FT_Library g_ft_library;

static FT_Library ft_library(void)
{
    if (!g_ft_library && FT_Init_FreeType(&g_ft_library) != 0)
        return NULL;
    return g_ft_library;
}

tk_font *tk_font_load(const char *path, int face_index)
{
    if (!path) return NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz <= 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    unsigned char *data = malloc((size_t)sz);
    if (!data) { fclose(fp); return NULL; }
    if (fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
        free(data); fclose(fp); return NULL;
    }
    fclose(fp);
    FT_Library lib = ft_library();
    if (!lib) { free(data); return NULL; }
    /* Face count first (a negative index reports num_faces), so an
     * out-of-range face index clamps exactly like the pre-M24
     * backend did. */
    FT_Face probe;
    if (FT_New_Memory_Face(lib, data, sz, -1, &probe) != 0) {
        free(data); return NULL;
    }
    int nfaces = (int)probe->num_faces;
    FT_Done_Face(probe);
    if (nfaces < 1) nfaces = 1;
    if (face_index < 0) face_index = 0;
    if (face_index >= nfaces) face_index = 0;
    tk_font *f = calloc(1, sizeof(*f));
    if (!f) { free(data); return NULL; }
    f->data = data;
    if (FT_New_Memory_Face(lib, data, sz, face_index, &f->ft) != 0) {
        free(data); free(f); return NULL;
    }
    f->hb_blob = hb_blob_create((const char *)data, (unsigned)sz,
                                HB_MEMORY_MODE_READONLY, NULL, NULL);
    if (f->hb_blob) f->hb_face = hb_face_create(f->hb_blob, (unsigned)face_index);
    if (!f->hb_face) {
        if (f->hb_blob) hb_blob_destroy(f->hb_blob);
        FT_Done_Face(f->ft);
        free(data); free(f); return NULL;
    }
    f->cap_cache = TK_FONT_CACHE_CAP;
    f->cache = calloc(f->cap_cache, sizeof(glyph_entry));
    if (!f->cache) {
        hb_face_destroy(f->hb_face);
        hb_blob_destroy(f->hb_blob);
        FT_Done_Face(f->ft);
        free(data); free(f); return NULL;
    }
    return f;
}

/* Bundled subset font (M3): fonts/brainui-jp-subset.ttf, built from
 * Noto Sans CJK JP (SIL OFL 1.1) by fonts/build_subset.py and
 * installed to share/fonts/libbrainui2/. Search order (M23 -
 * the pre-M23 order led with a CWD-relative path, so a product
 * launched from an arbitrary working directory silently lost
 * its font): the BRAINUI_FONT_PATH override, locations relative
 * to the EXECUTABLE (/proc/self/exe: ../share/fonts/libbrainui2,
 * fonts/, ../fonts), locations relative to this LIBRARY
 * (dladdr, for a dlopen'd .so in a private prefix), the
 * installed locations, and only as a last resort the
 * CWD-relative fonts/ dir (development trees). */
/* Attach the bundled supplement fonts (M4) that live next to the
 * primary subset: brainui-jp-supplement.ttf (codepoints the main
 * source font lacks - OSK symbol pages, calculator super/subscripts)
 * and brainui-jp-symbols.ttf (the media symbols U+23ED/23EE/23F8
 * and friends). They are chained as OWNED fallbacks, so every
 * product that loads the bundled font gets full UI coverage with
 * no extra code; tk_font_free releases the whole chain. A missing
 * supplement file simply ends the chain earlier. */
static void bundled_attach_supplements(tk_font *primary,
                                       const char *primary_path)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", primary_path);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0;
    else dir[0] = 0; /* bare filename: current directory */
    static const char *names[] = {
        "brainui-jp-supplement.ttf",
        "brainui-jp-symbols.ttf",
        NULL
    };
    tk_font *tail = primary;
    while (tail->fallback) tail = tail->fallback;
    for (int i = 0; names[i]; i++) {
        char path[600];
        snprintf(path, sizeof(path), "%s%s", dir, names[i]);
        tk_font *s = tk_font_load(path, 0);
        if (s) {
            tail->fallback = s;
            tail->fallback_owned = true;
            tail = s;
        }
    }
}

static tk_font *bundled_try(const char *path)
{
    tk_font *f = tk_font_load(path, 0);
    if (f) bundled_attach_supplements(f, path);
    return f;
}

/* try "<dir>/<suffix>" (suffix may itself contain ../ steps) */
static tk_font *bundled_try_join(const char *dir, const char *suffix)
{
    char buf[1024];
    if (!dir || !*dir) return NULL;
    size_t n = strlen(dir), m = strlen(suffix);
    if (n + 1 + m + 1 > sizeof(buf)) return NULL;
    memcpy(buf, dir, n);
    buf[n] = '/';
    memcpy(buf + n + 1, suffix, m + 1);
    return bundled_try(buf);
}

/* the font candidates anchored at one base directory: the
 * installed layout (../share/fonts/libbrainui2) and the
 * development-tree layouts (fonts/, ../fonts) */
static tk_font *bundled_try_base(const char *base)
{
    static const char *suffixes[] = {
        "../share/fonts/libbrainui2/brainui-jp-subset.ttf",
        "fonts/brainui-jp-subset.ttf",
        "../fonts/brainui-jp-subset.ttf",
        NULL
    };
    for (int i = 0; suffixes[i]; i++) {
        tk_font *f = bundled_try_join(base, suffixes[i]);
        if (f) return f;
    }
    return NULL;
}

static void dir_of(char *buf, size_t cap, const char *path)
{
    snprintf(buf, cap, "%s", path);
    char *slash = strrchr(buf, '/');
    if (slash) *slash = 0;
    else buf[0] = 0;
}

tk_font *tk_font_load_bundled(void)
{
    const char *env = getenv("BRAINUI_FONT_PATH");
    if (env && *env) {
        tk_font *f = bundled_try(env);
        if (f) return f;
    }
    /* executable-relative (covers every statically linked
     * product wherever it is launched from) */
    char exe[1024];
    ssize_t k = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (k > 0) {
        char dir[1024];
        exe[k] = 0;
        dir_of(dir, sizeof(dir), exe);
        tk_font *f = bundled_try_base(dir);
        if (f) return f;
    }
#if defined(__GLIBC__)
    /* library-relative (a dlopen'd libbrainui2.so installed in
     * a private prefix with its fonts under ../share) */
    Dl_info info;
    if (dladdr((void *)tk_font_load_bundled, &info) &&
        info.dli_fname && strchr(info.dli_fname, '/')) {
        char dir[1024];
        dir_of(dir, sizeof(dir), info.dli_fname);
        tk_font *f = bundled_try_base(dir);
        if (f) return f;
    }
#endif
    /* installed locations */
    static const char *paths[] = {
        "/usr/local/share/fonts/libbrainui2/brainui-jp-subset.ttf",
        "/usr/share/fonts/libbrainui2/brainui-jp-subset.ttf",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        tk_font *f = bundled_try(paths[i]);
        if (f) return f;
    }
    /* last resort: the CWD-relative development tree */
    return bundled_try("fonts/brainui-jp-subset.ttf");
}

tk_font *tk_font_load_default(void)
{
    static const char *paths[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        NULL
    };
    /* env override, then the bundled subset (the on-device path),
     * then host system fonts */
    tk_font *f = tk_font_load_bundled();
    if (f) return f;
    for (int i = 0; paths[i]; i++) {
        f = tk_font_load(paths[i], 0);
        if (f) return f;
    }
    return NULL;
}

void tk_font_free(tk_font *f)
{
    if (!f) return;
    if (f->fallback && f->fallback_owned) tk_font_free(f->fallback);
    for (int i = 0; i < f->n_hb_fonts; i++) hb_font_destroy(f->hb_fonts[i].font);
    if (f->hb_face) hb_face_destroy(f->hb_face);
    if (f->hb_blob) hb_blob_destroy(f->hb_blob);
    if (f->ft) FT_Done_Face(f->ft);
    for (size_t i = 0; i < f->n_cache; i++) free(f->cache[i].bmp);
    free(f->cache);
    free(f->data);
    if (g_current_font == f) g_current_font = NULL;
    free(f);
}

void tk_font_set_fallback(tk_font *primary, tk_font *fallback)
{
    if (primary) {
        primary->fallback = fallback;
        primary->fallback_owned = false; /* caller keeps ownership */
    }
}

void tk_font_install(tk_font *f) { g_current_font = f; }
tk_font *tk_font_current(void) { return g_current_font; }

size_t tk_font_cache_count(const tk_font *f)
{
    return f ? f->n_cache : 0;
}

/* Resolve which font in the chain provides cp (NULL = none). */
static const tk_font *resolve(const tk_font *f, uint32_t cp)
{
    for (const tk_font *p = f; p; p = p->fallback)
        if (FT_Get_Char_Index(p->ft, cp) != 0) return p;
    return NULL;
}

bool tk_font_has_glyph(const tk_font *f, uint32_t cp)
{
    return f && resolve(f, cp) != NULL;
}

/* Design units -> pixels, rounded half up. The toolkit convention
 * is px = EM size, so a fullwidth glyph advances exactly px. */
static int design_to_px(const tk_font *f, long design, int px)
{
    long upem = f->ft->units_per_EM;
    if (upem <= 0) return px;
    return (int)((design * px * 2 + upem) / (2 * upem));
}

int tk_font_cp_advance(const tk_font *f, int px, uint32_t cp)
{
    if (!f || px <= 0) return 0;
    const tk_font *src = resolve(f, cp);
    uint32_t glyph;
    if (src) {
        glyph = FT_Get_Char_Index(src->ft, cp);
    } else {
        src = f;
        glyph = 0; /* .notdef */
    }
    /* Context-free single-codepoint metric (password masks, OSK
     * key labels, wrap fallback): the raw hmtx advance, exactly
     * what the pre-M24 backend reported. Shaped text goes through
     * the HarfBuzz path below instead. */
    if (FT_Load_Glyph(src->ft, glyph,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0)
        return 0;
    return design_to_px(src, src->ft->glyph->metrics.horiAdvance, px);
}

static int font_ascent_px(const tk_font *f, int px)
{
    return design_to_px(f, f->ft->ascender, px);
}

static int font_line_px(const tk_font *f, int px)
{
    /* FT height == hhea (ascender - descender + lineGap), the same
     * triple the pre-M24 backend combined by hand. */
    int lh = design_to_px(f, f->ft->height, px);
    if (lh <= 0)
        lh = design_to_px(f, f->ft->ascender - f->ft->descender, px);
    return lh > 0 ? lh : px;
}

/* ------------------------------------------------------------------ */
/* Shaping (M24)                                                       */
/* ------------------------------------------------------------------ */

/* One shaped glyph, in 26.6 fixed-point pixels. cluster is the
 * byte offset (within the string handed to shape_run) of the
 * cluster this glyph belongs to. */
typedef struct {
    uint32_t glyph;
    const tk_font *src;      /* font owning the glyph */
    int adv;                 /* x advance, 26.6 px */
    int xoff, yoff;          /* HarfBuzz offsets, 26.6 px, y up */
    uint32_t cluster;
} tk_shaped_glyph;

/* Scratch only: one reused buffer + one reused hb_buffer. The
 * toolkit is single-threaded and shaping never re-enters, so the
 * result is consumed before the next call by construction. */
static tk_shaped_glyph *g_shaped;
static size_t g_shaped_n, g_shaped_cap;
static hb_buffer_t *g_hb_buffer;

static hb_font_t *hb_font_for(const tk_font *f, int px)
{
    tk_font *m = (tk_font *)f; /* shaped-font cache is mutable state */
    for (int i = 0; i < m->n_hb_fonts; i++)
        if (m->hb_fonts[i].px == px) return m->hb_fonts[i].font;
    hb_font_t *font = hb_font_create(m->hb_face);
    if (!font) return NULL;
    hb_font_set_scale(font, px * 64, px * 64);
    int slot;
    if (m->n_hb_fonts < TK_HB_FONT_SLOTS) {
        slot = m->n_hb_fonts++;
    } else {
        slot = (int)m->hb_ring;
        m->hb_ring = (m->hb_ring + 1) % TK_HB_FONT_SLOTS;
        hb_font_destroy(m->hb_fonts[slot].font);
    }
    m->hb_fonts[slot].px = px;
    m->hb_fonts[slot].font = font;
    return font;
}

static void shape_item(const tk_font *src, int px,
                       const char *s, size_t len, uint32_t base)
{
    hb_font_t *font = hb_font_for(src, px);
    if (!font) return;
    if (!g_hb_buffer) g_hb_buffer = hb_buffer_create();
    if (!g_hb_buffer) return;
    hb_buffer_reset(g_hb_buffer);
    hb_buffer_add_utf8(g_hb_buffer, s, (int)len, 0, (int)len);
    hb_buffer_guess_segment_properties(g_hb_buffer);
    /* Single-direction items by design (see the header note on
     * bidi): guessing is kept for script/language detection, the
     * direction is pinned so stray RTL codepoints - which shape
     * to .notdef in our fonts anyway - cannot flip a run. */
    hb_buffer_set_direction(g_hb_buffer, HB_DIRECTION_LTR);
    hb_shape(font, g_hb_buffer, NULL, 0);
    unsigned n = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(g_hb_buffer, &n);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(g_hb_buffer, &n);
    if (g_shaped_n + n > g_shaped_cap) {
        size_t cap = g_shaped_cap ? g_shaped_cap : 64;
        while (cap < g_shaped_n + n) cap *= 2;
        tk_shaped_glyph *grown = realloc(g_shaped, cap * sizeof(*grown));
        if (!grown) return;
        g_shaped = grown;
        g_shaped_cap = cap;
    }
    for (unsigned i = 0; i < n; i++) {
        tk_shaped_glyph *g = &g_shaped[g_shaped_n++];
        g->glyph = info[i].codepoint;
        g->src = src;
        g->adv = pos[i].x_advance;
        g->xoff = pos[i].x_offset;
        g->yoff = pos[i].y_offset;
        g->cluster = base + info[i].cluster;
    }
}

/* Shape s[0..len) with the fallback chain headed by `head`.
 * Consecutive codepoints served by the same font form one item;
 * '\n' produces no glyphs (callers treat it as a line break) and
 * codepoints missing everywhere shape as the head font's .notdef.
 * Returns the glyph count; glyphs are in g_shaped[0..count). */
static size_t shape_run(const tk_font *head, int px,
                        const char *s, size_t len)
{
    g_shaped_n = 0;
    if (!head || px <= 0 || !s) return 0;
    size_t off = 0;
    while (off < len) {
        const tk_font *src = NULL;
        size_t scan = off;
        while (scan < len) {
            size_t cl;
            uint32_t cp = tc_utf8_decode(s + scan, len - scan, &cl);
            if (cl == 0) cl = 1;
            if (cp == '\n') break;
            const tk_font *r = resolve(head, cp);
            if (!r) r = head; /* .notdef comes from the head font */
            if (!src) src = r;
            else if (r != src) break;
            scan += cl;
        }
        if (scan == off) { off++; continue; } /* the '\n' itself */
        shape_item(src, px, s + off, scan - off, (uint32_t)off);
        off = scan;
    }
    return g_shaped_n;
}

/* Fetch (rasterizing on first use) the cached glyph `glyph` of
 * font `src` at px. bmp may be NULL for space-like glyphs. */
static glyph_entry *glyph_for_gid(const tk_font *src, int px, uint32_t glyph)
{
    tk_font *m = (tk_font *)src; /* cache is mutable state */
    for (size_t i = 0; i < m->n_cache; i++) {
        glyph_entry *e = &m->cache[i];
        if (e->used && e->glyph == (int)glyph && e->px == px) return e;
    }
    glyph_entry *e;
    if (m->n_cache < m->cap_cache) {
        e = &m->cache[m->n_cache++];
    } else {
        e = &m->cache[m->ring];
        m->ring = (m->ring + 1) % m->cap_cache;
        free(e->bmp); e->bmp = NULL;
    }
    memset(e, 0, sizeof(*e));
    e->used = true; e->glyph = (int)glyph; e->px = px;
    if (FT_Set_Pixel_Sizes(m->ft, 0, (FT_UInt)px) == 0 &&
        FT_Load_Glyph(m->ft, glyph,
                      FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT) == 0) {
        FT_GlyphSlot slot = m->ft->glyph;
        e->adv = (int)((slot->advance.x + 32) >> 6);
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) == 0) {
            FT_Bitmap *b = &slot->bitmap;
            e->w = (int)b->width;
            e->h = (int)b->rows;
            e->xoff = slot->bitmap_left;
            e->yoff = -slot->bitmap_top;
            if (e->w > 0 && e->h > 0) {
                e->bmp = malloc((size_t)e->w * (size_t)e->h);
                if (e->bmp) {
                    int pitch = b->pitch;
                    for (int y = 0; y < e->h; y++) {
                        const unsigned char *row = pitch >= 0
                            ? b->buffer + (size_t)y * (size_t)pitch
                            : b->buffer + (size_t)(e->h - 1 - y) * (size_t)(-pitch);
                        memcpy(e->bmp + (size_t)y * (size_t)e->w, row,
                               (size_t)e->w);
                    }
                }
            }
        }
    }
    return e;
}

/* ------------------------------------------------------------------ */
/* Toolkit text metrics / rendering (moved from tk_draw.c in M2)       */
/* ------------------------------------------------------------------ */

/* M1 approximation, kept only for the no-font path. */
static int approx_cp_width_px(int px, uint32_t cp)
{
    if (cp < 0x80) return px / 2;
    if (cp >= 0xFF61 && cp <= 0xFFDC) return px / 2;
    if (tc_cp_is_combining(cp) || cp == 0x200D) return 0;
    return px;
}

int tk_text_cp_width(const tk_theme *t, uint32_t cp)
{
    if (cp == '\n' || cp == '\r') return 0;
    if (g_current_font) return tk_font_cp_advance(g_current_font, t->font_px, cp);
    return approx_cp_width_px(t->font_px, cp);
}

int tk_text_width_px(const tk_theme *t, int px, const char *utf8)
{
    (void)t; /* metrics come from the installed font + px */
    if (!utf8) return 0;
    if (!g_current_font) {
        int w = 0;
        size_t len = strlen(utf8), off = 0;
        while (off < len) {
            size_t cl;
            uint32_t cp = tc_utf8_decode(utf8 + off, len - off, &cl);
            if (cl == 0) cl = 1;
            w += approx_cp_width_px(px, cp);
            off += cl;
        }
        return w;
    }
    /* M24: the shaped total, rounded once. The pre-M24 backend
     * rounded every glyph's advance to whole pixels before
     * summing, accumulating up to +0.5px of error per glyph. */
    size_t n = shape_run(g_current_font, px, utf8, strlen(utf8));
    long total = 0;
    for (size_t i = 0; i < n; i++) total += g_shaped[i].adv;
    return (int)((total + 32) >> 6);
}

int tk_text_width(const tk_theme *t, const char *utf8)
{
    return tk_text_width_px(t, t->font_px, utf8);
}

int tk_text_width_st(const tk_theme *t, tk_text_style s, const char *utf8)
{
    return tk_text_width_px(t, tk_theme_style_px(t, s), utf8);
}

int tk_line_height_px(const tk_theme *t, int px)
{
    (void)t;
    if (g_current_font) return font_line_px(g_current_font, px);
    return px + px / 3;
}

int tk_line_height(const tk_theme *t)
{
    return tk_line_height_px(t, t->font_px);
}

int tk_line_height_st(const tk_theme *t, tk_text_style s)
{
    return tk_line_height_px(t, tk_theme_style_px(t, s));
}

void tk_canvas_draw_text_px(tk_canvas *c, const tk_theme *t, int px,
                            bool bold, int x, int y,
                            const char *utf8, tk_color col)
{
    if (!c || !utf8 || !g_current_font) return; /* no-font: paints nothing */
    const tk_font *font = g_current_font;
    /* Synthetic bold (the bundled font has one weight): the same
     * glyph run is blended twice, the second pass shifted 1px
     * right. Advances are unchanged, so bold text measures
     * exactly like regular text and layout never shifts. */
    int passes = bold ? 2 : 1;
    int baseline = y + font_ascent_px(font, px);
    const char *p = utf8;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        size_t n = shape_run(font, px, p, len);
        for (int pass = 0; pass < passes; pass++) {
            /* The pen accumulates in 26.6 fixed point and each
             * glyph is placed at the rounded position, so shaped
             * kerning/ligature advances never accumulate rounding
             * drift along the line. */
            /* x may be negative (scrolled text); a left shift
             * of a negative value is UB, so scale by multiply */
            long pen = ((long)(x + pass)) * 64;
            for (size_t i = 0; i < n; i++) {
                tk_shaped_glyph *g = &g_shaped[i];
                glyph_entry *e = glyph_for_gid(g->src, px, g->glyph);
                if (e && e->bmp) {
                    /* M5: bulk blit - clip + damage once per glyph
                     * instead of per pixel (same blend arithmetic).
                     * HarfBuzz y offsets are y-up, hence the minus. */
                    int gx = (int)((pen + g->xoff + 32) >> 6) + e->xoff;
                    int gy = baseline - (int)((g->yoff + 32) >> 6) + e->yoff;
                    tk_canvas_blit_alpha(c, gx, gy, e->w, e->h,
                                         e->bmp, e->w, col);
                }
                pen += g->adv;
            }
        }
        if (!nl) break;
        p = nl + 1;
        baseline += font_line_px(font, px);
    }
    (void)t;
}

void tk_canvas_draw_text(tk_canvas *c, const tk_theme *t, int x, int y,
                         const char *utf8, tk_color col)
{
    tk_canvas_draw_text_px(c, t, t->font_px, false, x, y, utf8, col);
}

void tk_canvas_draw_text_st(tk_canvas *c, const tk_theme *t,
                            tk_text_style s, int x, int y,
                            const char *utf8, tk_color col)
{
    tk_canvas_draw_text_px(c, t, tk_theme_style_px(t, s),
                           tk_theme_style_bold(s), x, y, utf8, col);
}

/* ------------------------------------------------------------------ */
/* Kinsoku-aware wrapping                                              */
/* ------------------------------------------------------------------ */

/* Chars that must not start a line (JIS X 4051 line-start prohibitions,
 * common subset + ASCII closing punctuation). */
static bool start_prohibited(uint32_t cp)
{
    switch (cp) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0x30FB:
    case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF01: case 0x30FC:
    case 0x309D: case 0x309E: case 0x30FD: case 0x30FE:
    case 0xFF09: case 0xFF3D: case 0xFF5D: case 0xFF60:
    case 0x3009: case 0x300B: case 0x300D: case 0x300F:
    case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301B:
    case '.': case ',': case ':': case ';': case '!': case '?':
    case ')': case ']': case '}': case '%':
        return true;
    default:
        break;
    }
    /* small kana (hiragana + katakana) */
    if (cp == 0x3041 || cp == 0x3043 || cp == 0x3045 || cp == 0x3047 ||
        cp == 0x3049 || cp == 0x3063 || cp == 0x3083 || cp == 0x3085 ||
        cp == 0x3087 || cp == 0x308E || cp == 0x30A1 || cp == 0x30A3 ||
        cp == 0x30A5 || cp == 0x30A7 || cp == 0x30A9 || cp == 0x30C3 ||
        cp == 0x30E3 || cp == 0x30E5 || cp == 0x30E7 || cp == 0x30EE ||
        cp == 0x30F5 || cp == 0x30F6)
        return true;
    return false;
}

/* Chars that must not end a line (opening brackets/quotes). */
static bool end_prohibited(uint32_t cp)
{
    switch (cp) {
    case 0xFF08: case 0xFF3B: case 0xFF5B: case 0xFF62:
    case 0x3008: case 0x300A: case 0x300C: case 0x300E:
    case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301A:
    case '(': case '[': case '{':
        return true;
    default:
        return false;
    }
}

static bool is_break_space(uint32_t cp) { return cp == ' ' || cp == 0x3000; }

size_t tk_text_wrap(const tk_theme *t, const char *utf8, int max_w_px,
                    tk_wrap_cb cb, void *userdata)
{
    if (!utf8) utf8 = "";
    size_t len = strlen(utf8);
    /* decode into arrays */
    size_t cap = len + 1, n = 0;
    uint32_t *cps = malloc(cap * sizeof(uint32_t));
    size_t *offs = malloc((cap + 1) * sizeof(size_t));
    int *widths = malloc(cap * sizeof(int));
    bool *cluster_start = malloc(cap * sizeof(bool));
    if (!cps || !offs || !widths || !cluster_start) {
        free(cps); free(offs); free(widths); free(cluster_start);
        if (cb) cb(userdata, 0, len);
        return 1;
    }
    size_t off = 0;
    while (off < len) {
        size_t cl;
        uint32_t cp = tc_utf8_decode(utf8 + off, len - off, &cl);
        if (cl == 0) cl = 1;
        cps[n] = cp; offs[n] = off;
        n++; off += cl;
    }
    offs[n] = len;
    if (g_current_font) {
        /* M24: per-codepoint widths come from shaping the whole
         * string once. Each cluster's advance is charged to the
         * cluster's first codepoint; the other members measure 0
         * and are marked non-starts so the breaker below can never
         * split a ligature or a base+mark cluster. Codepoints that
         * produce no glyph (e.g. ZWJ) measure 0 and travel with
         * the following cluster. */
        for (size_t i = 0; i < n; i++) {
            widths[i] = 0;
            cluster_start[i] = false;
        }
        size_t ng = shape_run(g_current_font, t->font_px, utf8, len);
        size_t ci = 0;
        for (size_t gi = 0; gi < ng; gi++) {
            uint32_t cl = g_shaped[gi].cluster;
            while (ci < n && offs[ci] < cl) ci++;
            if (ci >= n) break;
            /* widths[] temporarily accumulates 26.6 fixed point */
            widths[ci] += g_shaped[gi].adv;
            cluster_start[ci] = true;
        }
        for (size_t i = 0; i < n; i++)
            widths[i] = (widths[i] + 32) >> 6;
    } else {
        for (size_t i = 0; i < n; i++) {
            widths[i] = approx_cp_width_px(t->font_px, cps[i]);
            cluster_start[i] = true;
        }
    }
    if (max_w_px <= 0) {
        if (cb) cb(userdata, 0, len);
        free(cps); free(offs); free(widths); free(cluster_start);
        return 1;
    }
    size_t lines = 0;
    size_t start = 0;
    while (true) {
        if (start >= n) {
            if (lines == 0) { if (cb) cb(userdata, 0, 0); lines = 1; }
            break;
        }
        int width = 0;
        long last_break = -1;    /* cp index to break before */
        size_t j = start;
        bool hard_end = false;
        while (j < n) {
            if (cps[j] == '\n') { hard_end = true; break; }
            if (width + widths[j] > max_w_px && j > start) break;
            width += widths[j];
            /* breaking before j+1: legal unless the next char is
             * start-prohibited or this char is end-prohibited; a
             * break space is always a legal break point. M24: the
             * boundary must also be a cluster start - a break may
             * not split a shaped cluster. (The degenerate fallback
             * below, cutting at j when nothing is legal, can still
             * split an over-wide cluster; such a cluster cannot
             * fit the line at all.) */
            if ((j + 1 >= n || cluster_start[j + 1]) &&
                (is_break_space(cps[j]) ||
                 ((j + 1 >= n || !start_prohibited(cps[j + 1])) &&
                  !end_prohibited(cps[j]))))
                last_break = (long)(j + 1);
            j++;
        }
        if (j >= n) {            /* rest fits on one line */
            if (cb) cb(userdata, offs[start], offs[n] - offs[start]);
            lines++;
            break;
        }
        if (hard_end) {          /* explicit newline */
            if (cb) cb(userdata, offs[start], offs[j] - offs[start]);
            lines++;
            start = j + 1;
            continue;
        }
        size_t cut = (last_break > (long)start) ? (size_t)last_break : j;
        if (cut <= start) cut = start + 1;
        size_t emit_end = cut;
        while (emit_end > start && is_break_space(cps[emit_end - 1]))
            emit_end--;
        if (emit_end == start) emit_end = cut; /* all spaces: keep them */
        if (cb) cb(userdata, offs[start], offs[emit_end] - offs[start]);
        lines++;
        start = cut;
        while (start < n && cps[start] == ' ') start++;
    }
    free(cps); free(offs); free(widths); free(cluster_start);
    return lines;
}
