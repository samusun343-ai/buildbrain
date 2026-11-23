/* BrainCE Layer -- GDI shim (M2).
 *
 * Device contexts and GDI objects for the CE GWE surface
 * (DESIGN-wince-layer.md section 5.4). Rendering goes through
 * libbrainui2's canvas/font foundation (tk_canvas_wrap over the
 * window's shadow surface, tk_font for text), as the design
 * requires; the WM shared buffer is only touched by the
 * damage-limited flush in gwe.c.
 *
 * Internal pixel format is RGBA8888 words (0xAARRGGBB), the
 * toolkit's canvas format. DIBSections keep the app-visible raw
 * bits in the requested format (32/24/16 bpp BI_RGB) and convert
 * at sync points: raw -> canvas before the DC surface is read
 * (BitBlt source, GetPixel), canvas -> raw after any drawing op
 * into the DC (the app may read its bits at any time, so the raw
 * view is refreshed eagerly per op; the cost is bounded by the
 * bitmap size and only paid by DIBSection users).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ce_types.h"
#include "fs_bridge.h"
#include "gwe.h"

extern CE_DWORD ce_GetTickCount(void);

/* ---------------- color conversion ---------------- */

uint32_t gdi_cref_to_px(CE_DWORD cref)
{
    return 0xFF000000u | ((cref & 0xFF) << 16)
         | (cref & 0xFF00) | ((cref >> 16) & 0xFF);
}

CE_DWORD gdi_px_to_cref(uint32_t px)
{
    return ((px >> 16) & 0xFF) | (px & 0xFF00) | ((px & 0xFF) << 16);
}

/* ---------------- objects ---------------- */

ce_gdobj *gdi_obj_from_handle(CE_HANDLE h)
{
    if (!h)
        return NULL;
    ce_gdobj *o = (ce_gdobj *)h;
    if (o < &g_objects[0] || o >= &g_objects[CE_MAX_OBJECTS])
        return NULL;
    return o->used ? o : NULL;
}

ce_gdobj *gdi_new_obj(int type)
{
    for (int i = 0; i < CE_MAX_OBJECTS; i++)
        if (!g_objects[i].used) {
            memset(&g_objects[i], 0, sizeof(ce_gdobj));
            g_objects[i].used = 1;
            g_objects[i].type = type;
            return &g_objects[i];
        }
    return NULL;
}

static ce_gdobj *stock_brush(CE_DWORD color, CE_DWORD style)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_BRUSH);
    if (o) {
        o->stock = 1;
        o->brush_color = color;
        o->brush_style = style;
    }
    return o;
}

static ce_gdobj *stock_pen(CE_DWORD color, CE_DWORD style)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_PEN);
    if (o) {
        o->stock = 1;
        o->pen_color = color;
        o->pen_style = style;
        o->pen_width = 1;
    }
    return o;
}

static ce_gdobj *stock_font(int px)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_FONT);
    if (o) {
        o->stock = 1;
        o->font_px = px;
    }
    return o;
}

static ce_gdobj *g_stock[18];
static int g_stock_ready;

static void stock_init(void)
{
    if (g_stock_ready)
        return;
    g_stock_ready = 1;
    g_stock[CE_WHITE_BRUSH] = stock_brush(0x00FFFFFF, CE_BS_SOLID);
    g_stock[CE_LTGRAY_BRUSH] = stock_brush(0x00C0C0C0, CE_BS_SOLID);
    g_stock[CE_GRAY_BRUSH] = stock_brush(0x00808080, CE_BS_SOLID);
    g_stock[CE_DKGRAY_BRUSH] = stock_brush(0x00404040, CE_BS_SOLID);
    g_stock[CE_BLACK_BRUSH] = stock_brush(0x00000000, CE_BS_SOLID);
    g_stock[CE_NULL_BRUSH] = stock_brush(0, CE_BS_NULL);
    g_stock[CE_WHITE_PEN] = stock_pen(0x00FFFFFF, CE_PS_SOLID);
    g_stock[CE_BLACK_PEN] = stock_pen(0x00000000, CE_PS_SOLID);
    g_stock[CE_NULL_PEN] = stock_pen(0, CE_PS_NULL);
    g_stock[CE_OEM_FIXED_FONT] = stock_font(13);
    g_stock[CE_ANSI_FIXED_FONT] = stock_font(13);
    g_stock[CE_ANSI_VAR_FONT] = stock_font(13);
    g_stock[CE_SYSTEM_FONT] = stock_font(13);
    g_stock[CE_DEVICE_DEFAULT_FONT] = stock_font(13);
    g_stock[CE_SYSTEM_FIXED_FONT] = stock_font(13);
    g_stock[CE_DEFAULT_GUI_FONT] = stock_font(13);
}

CE_HANDLE ce_GetStockObject(int id)
{
    stock_init();
    if (id < 0 || id >= 18 || !g_stock[id]) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return (CE_HANDLE)g_stock[id];
}

/* brush for a COLOR_* index, as a cached stock-like object */
static ce_gdobj *g_sys_brush[32];
ce_gdobj *gdi_sys_brush_obj(int idx)
{
    if (idx < 0 || idx >= 32)
        return NULL;
    if (!g_sys_brush[idx]) {
        ce_gdobj *o = gdi_new_obj(CE_OBJ_BRUSH);
        if (!o)
            return NULL;
        o->stock = 1;
        o->brush_style = CE_BS_SOLID;
        o->brush_color = gwe_sys_color(idx);
        g_sys_brush[idx] = o;
    }
    return g_sys_brush[idx];
}

CE_HANDLE ce_GetSysColorBrush(int idx)
{
    ce_gdobj *o = gdi_sys_brush_obj(idx);
    return o ? (CE_HANDLE)o : NULL;
}

/* ---------------- font ---------------- */

static int g_font_ready;
static int g_font_ok;

void gwe_font_ensure(void)
{
    if (g_font_ready)
        return;
    g_font_ready = 1;
    tk_font *f = tk_font_load_bundled();
    if (f) {
        tk_font_install(f);
        g_font_ok = 1;
    }
    /* Without a font the toolkit falls back to monospace metrics
     * and paints no glyphs (its documented headless behaviour);
     * the guest always has the bundled font installed. */
}

/* ---------------- DC pool ---------------- */

ce_dc *gdi_dc_from_handle(CE_HANDLE h)
{
    if (!h)
        return NULL;
    ce_dc *dc = (ce_dc *)h;
    if (dc < &g_dcs[0] || dc >= &g_dcs[CE_MAX_DCS])
        return NULL;
    return dc->used ? dc : NULL;
}

ce_dc *gdi_new_dc(void)
{
    stock_init();
    for (int i = 0; i < CE_MAX_DCS; i++)
        if (!g_dcs[i].used) {
            ce_dc *dc = &g_dcs[i];
            memset(dc, 0, sizeof(*dc));
            dc->used = 1;
            dc->pen = g_stock[CE_BLACK_PEN];
            dc->brush = g_stock[CE_WHITE_BRUSH];
            dc->font = g_stock[CE_DEFAULT_GUI_FONT];
            dc->text_color = 0x00000000;
            dc->bk_color = 0x00FFFFFF;
            dc->bk_mode = CE_OPAQUE;
            return dc;
        }
    return NULL;
}

void gdi_free_dc(ce_dc *dc)
{
    if (dc)
        dc->used = 0;
}

static ce_gdobj *new_bitmap_obj(int w, int h)
{
    if (w <= 0 || h <= 0)
        return NULL;
    ce_gdobj *o = gdi_new_obj(CE_OBJ_BITMAP);
    if (!o)
        return NULL;
    o->bmp_w = w;
    o->bmp_h = h;
    o->bmp_px = malloc((size_t)w * h * sizeof(uint32_t));
    if (!o->bmp_px) {
        o->used = 0;
        return NULL;
    }
    for (int i = 0; i < w * h; i++)
        o->bmp_px[i] = 0xFF000000u; /* opaque black */
    o->bmp_canvas = tk_canvas_wrap(w, h, o->bmp_px);
    return o;
}

/* ---------------- DIBSection sync ---------------- */

static uint32_t dib_read_px(const ce_gdobj *b, int x, int y)
{
    int row = b->dib_topdown ? y : (b->bmp_h - 1 - y);
    const uint8_t *p = b->dib_bits + (size_t)row * b->dib_stride;
    switch (b->dib_bpp) {
    case 32: {
        uint32_t v;
        memcpy(&v, p + (size_t)x * 4, 4);
        return v | 0xFF000000u;
    }
    case 24:
        p += (size_t)x * 3;
        return 0xFF000000u | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8)
               | p[0];
    case 16: {
        uint16_t v;
        memcpy(&v, p + (size_t)x * 2, 2);
        uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, bl = v & 0x1F;
        return 0xFF000000u | ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8)
               | (bl * 255 / 31);
    }
    default:
        return 0xFF000000u;
    }
}

static void dib_write_px(ce_gdobj *b, int x, int y, uint32_t px)
{
    int row = b->dib_topdown ? y : (b->bmp_h - 1 - y);
    uint8_t *p = b->dib_bits + (size_t)row * b->dib_stride;
    switch (b->dib_bpp) {
    case 32:
        memcpy(p + (size_t)x * 4, &px, 4);
        break;
    case 24:
        p += (size_t)x * 3;
        p[0] = px & 0xFF;
        p[1] = (px >> 8) & 0xFF;
        p[2] = (px >> 16) & 0xFF;
        break;
    case 16: {
        uint16_t v = (uint16_t)((((px >> 16) & 0xFF) * 31 / 255) << 11
                     | (((px >> 8) & 0xFF) * 63 / 255) << 5
                     | ((px & 0xFF) * 31 / 255));
        memcpy(p + (size_t)x * 2, &v, 2);
        break;
    }
    default:
        break;
    }
}

void gdi_sync_dib_to_canvas(ce_gdobj *b)
{
    if (!b || !b->dib_bits || !b->bmp_px)
        return;
    for (int y = 0; y < b->bmp_h; y++)
        for (int x = 0; x < b->bmp_w; x++)
            b->bmp_px[(size_t)y * b->bmp_w + x] = dib_read_px(b, x, y);
}

void gdi_sync_canvas_to_dib(ce_gdobj *b)
{
    if (!b || !b->dib_bits || !b->bmp_px)
        return;
    for (int y = 0; y < b->bmp_h; y++)
        for (int x = 0; x < b->bmp_w; x++)
            dib_write_px(b, x, y, b->bmp_px[(size_t)y * b->bmp_w + x]);
}

/* ---------------- drawing primitives ---------------- */

/* Effective clip rects of the DC in canvas coordinates. Returns
 * count; rects[] receives canvas-space rects. */
static int dc_clip_rects(ce_dc *dc, CE_RECT *rects, int max)
{
    int cw = tk_canvas_width(dc->canvas);
    int ch = tk_canvas_height(dc->canvas);
    CE_RECT bounds = { 0, 0, cw, ch };
    if (dc->nclip == 0) {
        rects[0] = bounds;
        return 1;
    }
    int n = 0;
    for (int i = 0; i < dc->nclip && n < max; i++) {
        CE_RECT r = dc->clip[i];
        r.left += dc->origin.x;
        r.top += dc->origin.y;
        r.right += dc->origin.x;
        r.bottom += dc->origin.y;
        r = ce_rect_intersect(r, bounds);
        if (!ce_rect_empty(&r))
            rects[n++] = r;
    }
    return n;
}

static void dc_damage(ce_dc *dc, CE_RECT r_dc_coords)
{
    if (!dc->top || dc->is_memory)
        return;
    CE_RECT r = r_dc_coords;
    r.left += dc->origin.x;
    r.top += dc->origin.y;
    r.right += dc->origin.x;
    r.bottom += dc->origin.y;
    gwe_add_damage(dc->top, r);
    if (dc->bitmap && dc->bitmap->dib_bits)
        gdi_sync_canvas_to_dib(dc->bitmap);
}

static tk_color px_to_tk(uint32_t px)
{
    tk_color c = { (px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF,
                   (px >> 24) & 0xFF };
    return c;
}

/* Fill r (DC coords) with a raw pixel value, honouring clip. */
void gdi_fill_rect_px(ce_dc *dc, CE_RECT r, uint32_t px)
{
    if (!dc->canvas)
        return;
    CE_RECT clips[CE_MAX_RECTS];
    int n = dc_clip_rects(dc, clips, CE_MAX_RECTS);
    tk_rect tr = { r.left + dc->origin.x, r.top + dc->origin.y,
                   r.right - r.left, r.bottom - r.top };
    for (int i = 0; i < n; i++) {
        tk_rect cr = { clips[i].left, clips[i].top,
                       clips[i].right - clips[i].left,
                       clips[i].bottom - clips[i].top };
        tk_canvas_push_clip(dc->canvas, cr);
        tk_canvas_fill_rect(dc->canvas, tr, px_to_tk(px));
        tk_canvas_pop_clip(dc->canvas);
    }
    dc_damage(dc, r);
}

static void plot_px(ce_dc *dc, int x, int y, uint32_t px)
{
    if (!dc->canvas)
        return;
    int cx = x + dc->origin.x, cy = y + dc->origin.y;
    int cw = tk_canvas_width(dc->canvas);
    int ch = tk_canvas_height(dc->canvas);
    if (cx < 0 || cy < 0 || cx >= cw || cy >= ch)
        return;
    if (dc->nclip) {
        int inside = 0;
        for (int i = 0; i < dc->nclip; i++) {
            const CE_RECT *c = &dc->clip[i];
            if (x >= c->left && x < c->right && y >= c->top && y < c->bottom) {
                inside = 1;
                break;
            }
        }
        if (!inside)
            return;
    }
    uint32_t *pix = tk_canvas_pixels(dc->canvas);
    pix[(size_t)cy * cw + cx] = px;
    CE_RECT r = { x, y, x + 1, y + 1 };
    dc_damage(dc, r);
}

static uint32_t read_px(ce_dc *dc, int x, int y)
{
    if (!dc->canvas)
        return 0;
    int cx = x + dc->origin.x, cy = y + dc->origin.y;
    int cw = tk_canvas_width(dc->canvas);
    int ch = tk_canvas_height(dc->canvas);
    if (cx < 0 || cy < 0 || cx >= cw || cy >= ch)
        return 0;
    return tk_canvas_pixels(dc->canvas)[(size_t)cy * cw + cx];
}

/* ---------------- DC acquisition ---------------- */

static ce_dc *window_dc(ce_window *w, int is_paint)
{
    ce_dc *dc = gdi_new_dc();
    if (!dc)
        return NULL;
    ce_window *top = gwe_top_of(w);
    dc->win = w;
    dc->top = top;
    dc->is_paint = is_paint;
    if (top && !top->is_desktop) {
        if (!top->shadow)
            gwe_flush(top); /* no-op; surface ensured at create */
        dc->canvas = top->shadow;
        dc->origin = gwe_client_origin(w);
    }
    return dc;
}

CE_HANDLE ce_GetDC(CE_HANDLE hwnd)
{
    if (!hwnd)
        hwnd = (CE_HANDLE)&g_desktop;
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    ce_dc *dc = window_dc(w, 0);
    return dc ? (CE_HANDLE)dc : NULL;
}

CE_HANDLE ce_GetWindowDC(CE_HANDLE hwnd)
{
    /* The layer has no client/nonclient split (the WM server owns
     * the frame), so a window DC is the client DC. */
    return ce_GetDC(hwnd);
}

int ce_ReleaseDC(CE_HANDLE hwnd, CE_HANDLE hdc)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc)
        return 0;
    (void)hwnd;
    if (dc->top)
        gwe_flush(dc->top);
    gdi_free_dc(dc);
    return 1;
}

CE_HANDLE ce_CreateCompatibleDC(CE_HANDLE ref)
{
    (void)ref;
    ce_dc *dc = gdi_new_dc();
    if (!dc)
        return NULL;
    dc->is_memory = 1;
    ce_gdobj *b = new_bitmap_obj(1, 1);
    if (!b) {
        gdi_free_dc(dc);
        return NULL;
    }
    dc->bitmap = b;
    dc->canvas = b->bmp_canvas;
    return (CE_HANDLE)dc;
}

CE_BOOL ce_DeleteDC(CE_HANDLE hdc)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc)
        return CE_FALSE;
    if (dc->top)
        gwe_flush(dc->top);
    gdi_free_dc(dc);
    return CE_TRUE;
}

/* ---------------- paint DC ---------------- */

CE_HANDLE ce_BeginPaint(CE_HANDLE hwnd, CE_PAINTSTRUCT *ps)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !ps) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    ce_dc *dc = window_dc(w, 1);
    if (!dc)
        return NULL;
    memset(ps, 0, sizeof(*ps));
    ps->hdc = (CE_HANDLE)dc;
    if (w->nupd > 0) {
        CE_RECT bb = w->upd[0];
        for (int i = 1; i < w->nupd; i++)
            bb = ce_rect_union(bb, w->upd[i]);
        ps->rcPaint = bb;
        ps->fErase = w->upd_erase ? CE_TRUE : CE_FALSE;
        dc->nclip = w->nupd;
        for (int i = 0; i < w->nupd; i++)
            dc->clip[i] = w->upd[i];
        if (ps->fErase) {
            /* CE sends WM_ERASEBKGND from inside BeginPaint */
            gwe_call_proc(w, CE_WM_ERASEBKGND, (CE_DWORD)dc, 0);
        }
    } else {
        ps->rcPaint = (CE_RECT){ 0, 0, 0, 0 };
        ps->fErase = CE_FALSE;
    }
    return (CE_HANDLE)dc;
}

CE_BOOL ce_EndPaint(CE_HANDLE hwnd, const CE_PAINTSTRUCT *ps)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !ps)
        return CE_FALSE;
    ce_dc *dc = gdi_dc_from_handle(ps->hdc);
    gwe_validate(w, NULL);
    if (w)
        gwe_flush(gwe_top_of(w));
    if (dc)
        gdi_free_dc(dc);
    return CE_TRUE;
}

/* ---------------- object creation ---------------- */

CE_HANDLE ce_CreatePen(int style, int width, CE_DWORD color)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_PEN);
    if (!o)
        return NULL;
    o->pen_style = (CE_DWORD)style;
    o->pen_width = width > 0 ? (CE_DWORD)width : 1;
    o->pen_color = color;
    return (CE_HANDLE)o;
}

CE_HANDLE ce_CreateSolidBrush(CE_DWORD color)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_BRUSH);
    if (!o)
        return NULL;
    o->brush_style = CE_BS_SOLID;
    o->brush_color = color;
    return (CE_HANDLE)o;
}

CE_HANDLE ce_CreateFontIndirectW(const CE_LOGFONTW *lf)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_FONT);
    if (!o)
        return NULL;
    if (lf) {
        int px = lf->lfHeight < 0 ? -lf->lfHeight : lf->lfHeight;
        if (px == 0)
            px = 13;
        o->font_px = px;
        o->font_bold = lf->lfWeight >= 600;
        o->font_italic = lf->lfItalic != 0;
        memcpy(o->font_face, lf->lfFaceName, sizeof(o->font_face));
    } else {
        o->font_px = 13;
    }
    return (CE_HANDLE)o;
}

CE_HANDLE ce_SelectObject(CE_HANDLE hdc, CE_HANDLE obj_h)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    ce_gdobj *o = gdi_obj_from_handle(obj_h);
    if (!dc || !o) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    CE_HANDLE old = NULL;
    switch (o->type) {
    case CE_OBJ_PEN:
        old = (CE_HANDLE)dc->pen;
        dc->pen = o;
        break;
    case CE_OBJ_BRUSH:
        old = (CE_HANDLE)dc->brush;
        dc->brush = o;
        break;
    case CE_OBJ_FONT:
        old = (CE_HANDLE)dc->font;
        dc->font = o;
        break;
    case CE_OBJ_BITMAP:
        if (!dc->is_memory) {
            ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
            return NULL;
        }
        old = (CE_HANDLE)dc->bitmap;
        dc->bitmap = o;
        dc->canvas = o->bmp_canvas;
        if (o->dib_bits)
            gdi_sync_dib_to_canvas(o);
        break;
    default:
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return old;
}

static void free_obj(ce_gdobj *o)
{
    if (o->type == CE_OBJ_BITMAP) {
        if (o->bmp_canvas)
            tk_canvas_free(o->bmp_canvas);
        free(o->bmp_px);
        free(o->dib_bits);
    }
    memset(o, 0, sizeof(*o));
}

CE_BOOL ce_DeleteObject(CE_HANDLE obj_h)
{
    ce_gdobj *o = gdi_obj_from_handle(obj_h);
    if (!o)
        return CE_FALSE;
    if (o->stock)
        return CE_FALSE; /* stock objects cannot be deleted */
    /* deselect from every live DC first (desktop semantics make
     * deleting a selected object undefined; we resolve it to the
     * stock default so no DC is left dangling) */
    stock_init();
    for (int i = 0; i < CE_MAX_DCS; i++) {
        ce_dc *dc = &g_dcs[i];
        if (!dc->used)
            continue;
        if (dc->pen == o) dc->pen = g_stock[CE_BLACK_PEN];
        if (dc->brush == o) dc->brush = g_stock[CE_WHITE_BRUSH];
        if (dc->font == o) dc->font = g_stock[CE_DEFAULT_GUI_FONT];
        if (dc->bitmap == o) {
            dc->bitmap = NULL;
            dc->canvas = NULL;
        }
    }
    free_obj(o);
    return CE_TRUE;
}

int ce_GetObjectW(CE_HANDLE obj_h, int size, void *buf)
{
    ce_gdobj *o = gdi_obj_from_handle(obj_h);
    if (!o)
        return 0;
    union {
        CE_BITMAP bmp;
        CE_LOGFONTW lf;
        CE_LOGBRUSH lb;
        struct { CE_DWORD style; CE_POINT width; CE_DWORD color; } lp;
    } tmp;
    int need = 0;
    switch (o->type) {
    case CE_OBJ_BITMAP:
        memset(&tmp, 0, sizeof(tmp));
        tmp.bmp.bmType = 0;
        tmp.bmp.bmWidth = o->bmp_w;
        tmp.bmp.bmHeight = o->bmp_h;
        tmp.bmp.bmWidthBytes = o->bmp_w * (o->dib_bits ? o->dib_bpp / 8 : 4);
        tmp.bmp.bmPlanes = 1;
        tmp.bmp.bmBitsPixel = (uint16_t)(o->dib_bits ? o->dib_bpp : 32);
        tmp.bmp.bmBits = o->dib_bits;
        need = sizeof(CE_BITMAP);
        break;
    case CE_OBJ_FONT:
        memset(&tmp, 0, sizeof(tmp));
        tmp.lf.lfHeight = -o->font_px;
        tmp.lf.lfWeight = o->font_bold ? 700 : 400;
        tmp.lf.lfItalic = o->font_italic ? 1 : 0;
        memcpy(tmp.lf.lfFaceName, o->font_face, sizeof(tmp.lf.lfFaceName));
        need = sizeof(CE_LOGFONTW);
        break;
    case CE_OBJ_BRUSH:
        tmp.lb.lbStyle = o->brush_style;
        tmp.lb.lbColor = o->brush_color;
        tmp.lb.lbHatch = 0;
        need = sizeof(CE_LOGBRUSH);
        break;
    case CE_OBJ_PEN:
        tmp.lp.style = o->pen_style;
        tmp.lp.width.x = (CE_LONG)o->pen_width;
        tmp.lp.width.y = 0;
        tmp.lp.color = o->pen_color;
        need = 16;
        break;
    default:
        return 0;
    }
    if (!buf)
        return need;
    int n = size < need ? size : need;
    memcpy(buf, &tmp, (size_t)n);
    return n;
}

/* ---------------- DC state ---------------- */

CE_DWORD ce_SetTextColor(CE_HANDLE hdc, CE_DWORD color)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc)
        return (CE_DWORD)-1;
    CE_DWORD old = dc->text_color;
    dc->text_color = color;
    return old;
}

CE_DWORD ce_GetTextColor(CE_HANDLE hdc)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    return dc ? dc->text_color : (CE_DWORD)-1;
}

CE_DWORD ce_SetBkColor(CE_HANDLE hdc, CE_DWORD color)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc)
        return (CE_DWORD)-1;
    CE_DWORD old = dc->bk_color;
    dc->bk_color = color;
    return old;
}

CE_DWORD ce_GetBkColor(CE_HANDLE hdc)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    return dc ? dc->bk_color : (CE_DWORD)-1;
}

int ce_SetBkMode(CE_HANDLE hdc, int mode)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc)
        return 0;
    int old = dc->bk_mode;
    dc->bk_mode = mode;
    return old;
}

int ce_SaveDC(CE_HANDLE hdc)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || dc->nsaved >= 8)
        return 0;
    int lvl = dc->nsaved++;
    dc->saved[lvl].pen = dc->pen;
    dc->saved[lvl].brush = dc->brush;
    dc->saved[lvl].font = dc->font;
    dc->saved[lvl].text_color = dc->text_color;
    dc->saved[lvl].bk_color = dc->bk_color;
    dc->saved[lvl].bk_mode = dc->bk_mode;
    dc->saved[lvl].pos = dc->pos;
    dc->saved[lvl].nclip = dc->nclip;
    memcpy(dc->saved[lvl].clip, dc->clip, sizeof(dc->clip));
    return lvl + 1;
}

CE_BOOL ce_RestoreDC(CE_HANDLE hdc, int level)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || dc->nsaved == 0)
        return CE_FALSE;
    int lvl;
    if (level == -1)
        lvl = dc->nsaved - 1;
    else if (level >= 1 && level <= dc->nsaved)
        lvl = level - 1;
    else
        return CE_FALSE;
    dc->pen = dc->saved[lvl].pen;
    dc->brush = dc->saved[lvl].brush;
    dc->font = dc->saved[lvl].font;
    dc->text_color = dc->saved[lvl].text_color;
    dc->bk_color = dc->saved[lvl].bk_color;
    dc->bk_mode = dc->saved[lvl].bk_mode;
    dc->pos = dc->saved[lvl].pos;
    dc->nclip = dc->saved[lvl].nclip;
    memcpy(dc->clip, dc->saved[lvl].clip, sizeof(dc->clip));
    dc->nsaved = lvl;
    return CE_TRUE;
}

int ce_GetDeviceCaps(CE_HANDLE hdc, int idx)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    int w = CE_SCREEN_W, h = CE_SCREEN_H;
    if (dc) {
        if (dc->is_memory && dc->bitmap) {
            w = dc->bitmap->bmp_w;
            h = dc->bitmap->bmp_h;
        } else if (dc->win) {
            w = gwe_win_w(dc->win);
            h = gwe_win_h(dc->win);
        }
    }
    switch (idx) {
    case CE_HORZRES: return w;
    case CE_VERTRES: return h;
    case CE_BITSPIXEL: return 32;
    case CE_PLANES: return 1;
    case CE_NUMCOLORS: return -1;
    case CE_LOGPIXELSX: return 96;
    case CE_LOGPIXELSY: return 96;
    default: return 0;
    }
}

/* ---------------- rect helper APIs ---------------- */

CE_BOOL ce_SetRect(CE_RECT *r, int l, int t, int rr, int b)
{
    if (!r) return CE_FALSE;
    r->left = l; r->top = t; r->right = rr; r->bottom = b;
    return CE_TRUE;
}

CE_BOOL ce_SetRectEmpty(CE_RECT *r)
{
    if (!r) return CE_FALSE;
    r->left = r->top = r->right = r->bottom = 0;
    return CE_TRUE;
}

CE_BOOL ce_CopyRect(CE_RECT *dst, const CE_RECT *src)
{
    if (!dst || !src) return CE_FALSE;
    *dst = *src;
    return CE_TRUE;
}

CE_BOOL ce_InflateRect(CE_RECT *r, int dx, int dy)
{
    if (!r) return CE_FALSE;
    r->left -= dx; r->right += dx;
    r->top -= dy; r->bottom += dy;
    return CE_TRUE;
}

CE_BOOL ce_OffsetRect(CE_RECT *r, int dx, int dy)
{
    if (!r) return CE_FALSE;
    r->left += dx; r->right += dx;
    r->top += dy; r->bottom += dy;
    return CE_TRUE;
}

CE_BOOL ce_IsRectEmpty(const CE_RECT *r)
{
    return ce_rect_empty(r) ? CE_TRUE : CE_FALSE;
}

CE_BOOL ce_EqualRect(const CE_RECT *a, const CE_RECT *b)
{
    if (!a || !b) return CE_FALSE;
    return a->left == b->left && a->top == b->top
        && a->right == b->right && a->bottom == b->bottom;
}

CE_BOOL ce_PtInRect(const CE_RECT *r, CE_POINT p)
{
    if (!r) return CE_FALSE;
    return p.x >= r->left && p.x < r->right
        && p.y >= r->top && p.y < r->bottom;
}

CE_BOOL ce_IntersectRect(CE_RECT *dst, const CE_RECT *a, const CE_RECT *b)
{
    if (!dst || !a || !b) return CE_FALSE;
    *dst = ce_rect_intersect(*a, *b);
    return ce_rect_empty(dst) ? CE_FALSE : CE_TRUE;
}

CE_BOOL ce_UnionRect(CE_RECT *dst, const CE_RECT *a, const CE_RECT *b)
{
    if (!dst || !a || !b) return CE_FALSE;
    *dst = ce_rect_union(*a, *b);
    return ce_rect_empty(dst) ? CE_FALSE : CE_TRUE;
}

/* ---------------- fills and shapes ---------------- */

/* Fill with a brush handle, which may also be an encoded system
 * color handle (HBRUSH)(COLOR_x + 1). Returns 1 on success. */
int gdi_fill_rect_handle(CE_HANDLE hdc, const CE_RECT *r, CE_HANDLE hbrush)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !r)
        return 0;
    uint32_t px;
    if ((uintptr_t)hbrush < 0x1000 && hbrush != NULL) {
        px = gdi_cref_to_px(gwe_sys_color((int)(uintptr_t)hbrush - 1));
    } else {
        ce_gdobj *b = gdi_obj_from_handle(hbrush);
        if (!b || b->type != CE_OBJ_BRUSH)
            return 0;
        if (b->brush_style == CE_BS_NULL)
            return 1; /* hollow: nothing to paint */
        px = gdi_cref_to_px(b->brush_color);
    }
    gdi_fill_rect_px(dc, *r, px);
    return 1;
}

int ce_FillRect(CE_HANDLE hdc, const CE_RECT *r, CE_HANDLE hbrush)
{
    return gdi_fill_rect_handle(hdc, r, hbrush);
}

static void fill_with_brush(ce_dc *dc, CE_RECT r)
{
    if (dc->brush && dc->brush->brush_style != CE_BS_NULL)
        gdi_fill_rect_px(dc, r, gdi_cref_to_px(dc->brush->brush_color));
}

static void stroke_rect_pen(ce_dc *dc, CE_RECT r)
{
    if (!dc->pen || dc->pen->pen_style == CE_PS_NULL)
        return;
    uint32_t px = gdi_cref_to_px(dc->pen->pen_color);
    int wdt = (int)dc->pen->pen_width;
    if (wdt < 1) wdt = 1;
    CE_RECT t = { r.left, r.top, r.right, r.top + wdt };
    CE_RECT b = { r.left, r.bottom - wdt, r.right, r.bottom };
    CE_RECT l = { r.left, r.top, r.left + wdt, r.bottom };
    CE_RECT rr = { r.right - wdt, r.top, r.right, r.bottom };
    gdi_fill_rect_px(dc, t, px);
    gdi_fill_rect_px(dc, b, px);
    gdi_fill_rect_px(dc, l, px);
    gdi_fill_rect_px(dc, rr, px);
}

CE_BOOL ce_Rectangle(CE_HANDLE hdc, int l, int t, int r, int b)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc) return CE_FALSE;
    CE_RECT rc = { l, t, r, b };
    fill_with_brush(dc, rc);
    stroke_rect_pen(dc, rc);
    return CE_TRUE;
}

CE_BOOL ce_RoundRect(CE_HANDLE hdc, int l, int t, int r, int b,
                     int ew, int eh)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !dc->canvas) return CE_FALSE;
    int rad = (ew < eh ? ew : eh) / 2;
    if (rad < 0) rad = 0;
    CE_RECT clips[CE_MAX_RECTS];
    int n = dc_clip_rects(dc, clips, CE_MAX_RECTS);
    tk_rect tr = { l + dc->origin.x, t + dc->origin.y, r - l, b - t };
    for (int i = 0; i < n; i++) {
        tk_rect cr = { clips[i].left, clips[i].top,
                       clips[i].right - clips[i].left,
                       clips[i].bottom - clips[i].top };
        tk_canvas_push_clip(dc->canvas, cr);
        if (dc->brush && dc->brush->brush_style != CE_BS_NULL)
            tk_canvas_fill_round_rect(dc->canvas, tr, rad,
                                      px_to_tk(gdi_cref_to_px(dc->brush->brush_color)));
        if (dc->pen && dc->pen->pen_style != CE_PS_NULL)
            tk_canvas_stroke_round_rect(dc->canvas, tr, rad,
                                        (int)dc->pen->pen_width,
                                        px_to_tk(gdi_cref_to_px(dc->pen->pen_color)));
        tk_canvas_pop_clip(dc->canvas);
    }
    CE_RECT rc = { l, t, r, b };
    dc_damage(dc, rc);
    return CE_TRUE;
}

CE_BOOL ce_Ellipse(CE_HANDLE hdc, int l, int t, int r, int b)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !dc->canvas) return CE_FALSE;
    double cx = (l + r - 1) / 2.0, cy = (t + b - 1) / 2.0;
    double rx = (r - l) / 2.0, ry = (b - t) / 2.0;
    if (rx < 0.5 || ry < 0.5) return CE_FALSE;
    uint32_t fillpx = dc->brush ? gdi_cref_to_px(dc->brush->brush_color) : 0;
    int dofill = dc->brush && dc->brush->brush_style != CE_BS_NULL;
    uint32_t penpx = dc->pen ? gdi_cref_to_px(dc->pen->pen_color) : 0;
    int dopen = dc->pen && dc->pen->pen_style != CE_PS_NULL;
    for (int y = t; y < b; y++) {
        double dy = (y - cy) / ry;
        double dx = rx * sqrt(1.0 - dy * dy);
        int x0 = (int)ceil(cx - dx), x1 = (int)floor(cx + dx);
        if (dofill) {
            CE_RECT row = { x0, y, x1 + 1, y + 1 };
            gdi_fill_rect_px(dc, row, fillpx);
        }
        if (dopen) {
            plot_px(dc, x0, y, penpx);
            plot_px(dc, x1, y, penpx);
        }
    }
    for (int x = l; x < r; x++) {
        double dx = (x - cx) / rx;
        double dy = ry * sqrt(1.0 - dx * dx);
        if (dopen) {
            plot_px(dc, x, (int)ceil(cy - dy), penpx);
            plot_px(dc, x, (int)floor(cy + dy), penpx);
        }
    }
    CE_RECT rc = { l, t, r, b };
    dc_damage(dc, rc);
    return CE_TRUE;
}

CE_BOOL ce_MoveToEx(CE_HANDLE hdc, int x, int y, CE_POINT *old)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc) return CE_FALSE;
    if (old)
        *old = dc->pos;
    dc->pos.x = x;
    dc->pos.y = y;
    return CE_TRUE;
}

CE_BOOL ce_LineTo(CE_HANDLE hdc, int x, int y)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !dc->pen || dc->pen->pen_style == CE_PS_NULL)
        return CE_FALSE;
    uint32_t px = gdi_cref_to_px(dc->pen->pen_color);
    int wdt = (int)dc->pen->pen_width;
    if (wdt < 1) wdt = 1;
    int x0 = dc->pos.x, y0 = dc->pos.y;
    int dx = abs(x - x0), dy = -abs(y - y0);
    int sx = x0 < x ? 1 : -1, sy = y0 < y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int ox = 0; ox < wdt; ox++)
            for (int oy = 0; oy < wdt; oy++)
                plot_px(dc, x0 + ox, y0 + oy, px);
        if (x0 == x && y0 == y)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    dc->pos.x = x;
    dc->pos.y = y;
    return CE_TRUE;
}

CE_DWORD ce_SetPixel(CE_HANDLE hdc, int x, int y, CE_DWORD color)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc) return (CE_DWORD)-1;
    if (dc->is_memory && dc->bitmap && dc->bitmap->dib_bits)
        gdi_sync_dib_to_canvas(dc->bitmap);
    plot_px(dc, x, y, gdi_cref_to_px(color));
    if (dc->is_memory && dc->bitmap && dc->bitmap->dib_bits)
        gdi_sync_canvas_to_dib(dc->bitmap);
    return color;
}

CE_DWORD ce_GetPixel(CE_HANDLE hdc, int x, int y)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc) return (CE_DWORD)-1;
    if (dc->is_memory && dc->bitmap && dc->bitmap->dib_bits)
        gdi_sync_dib_to_canvas(dc->bitmap);
    return gdi_px_to_cref(read_px(dc, x, y));
}

/* ---------------- blits ---------------- */

static uint32_t rop_apply(CE_DWORD rop, uint32_t s, uint32_t d, uint32_t p)
{
    switch (rop) {
    case CE_SRCCOPY: return s;
    case CE_NOTSRCCOPY: return ~s;
    case CE_SRCINVERT: return s ^ d;
    case CE_SRCAND: return s & d;
    case CE_SRCPAINT: return s | d;
    case CE_SRCERASE: return s & ~d;
    case CE_NOTSRCERASE: return ~s & ~d;
    case CE_MERGEPAINT: return ~s | d;
    case CE_MERGECOPY: return s & p;
    case CE_PATINVERT: return p ^ d;
    case CE_PATPAINT: return ~d | p | s;
    case CE_DSTINVERT: return ~d;
    case CE_BLACKNESS: return 0xFF000000u;
    case CE_WHITENESS: return 0xFFFFFFFFu;
    default: return s;
    }
}

CE_BOOL ce_PatBlt(CE_HANDLE hdc, int x, int y, int w, int h, CE_DWORD rop)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !dc->canvas) return CE_FALSE;
    uint32_t pat = dc->brush ? gdi_cref_to_px(dc->brush->brush_color)
                             : 0xFF000000u;
    if (rop == CE_PATCOPY) {
        CE_RECT r = { x, y, x + w, y + h };
        gdi_fill_rect_px(dc, r, pat);
        return CE_TRUE;
    }
    if (rop == CE_BLACKNESS || rop == CE_WHITENESS) {
        CE_RECT r = { x, y, x + w, y + h };
        gdi_fill_rect_px(dc, r,
                         rop == CE_BLACKNESS ? 0xFF000000u : 0xFFFFFFFFu);
        return CE_TRUE;
    }
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            uint32_t d = read_px(dc, x + xx, y + yy);
            plot_px(dc, x + xx, y + yy, rop_apply(rop, 0, d, pat));
        }
    return CE_TRUE;
}

CE_BOOL ce_BitBlt(CE_HANDLE hdc, int x, int y, int w, int h,
                  CE_HANDLE src_h, int sx, int sy, CE_DWORD rop)
{
    ce_dc *dst = gdi_dc_from_handle(hdc);
    if (!dst || !dst->canvas) return CE_FALSE;
    if (rop == CE_PATCOPY || rop == CE_BLACKNESS || rop == CE_WHITENESS)
        return ce_PatBlt(hdc, x, y, w, h, rop);
    ce_dc *src = gdi_dc_from_handle(src_h);
    if (!src || !src->canvas) return CE_FALSE;
    if (src->is_memory && src->bitmap && src->bitmap->dib_bits)
        gdi_sync_dib_to_canvas(src->bitmap);
    uint32_t pat = dst->brush ? gdi_cref_to_px(dst->brush->brush_color)
                              : 0xFF000000u;
    int from_dib = src->is_memory && src->bitmap && src->bitmap->dib_bits;
    if (rop == CE_SRCCOPY && dst->canvas == src->canvas) {
        /* same-surface copy: go through a temp to be overlap-safe */
    }
    if (rop == CE_SRCCOPY) {
        /* row-wise copy honouring the destination clip */
        CE_RECT clips[CE_MAX_RECTS];
        int n = dc_clip_rects(dst, clips, CE_MAX_RECTS);
        int scw = tk_canvas_width(src->canvas);
        int sch = tk_canvas_height(src->canvas);
        int dcw = tk_canvas_width(dst->canvas);
        uint32_t *spx = tk_canvas_pixels(src->canvas);
        uint32_t *dpx = tk_canvas_pixels(dst->canvas);
        /* snapshot the source region when surfaces alias */
        uint32_t *tmp = NULL;
        if (dst->canvas == src->canvas) {
            tmp = malloc((size_t)(w > 0 ? w : 1) * (h > 0 ? h : 1) * 4);
            if (tmp) {
                for (int yy = 0; yy < h; yy++) {
                    int syy = sy + src->origin.y + yy;
                    for (int xx = 0; xx < w; xx++) {
                        int sxx = sx + src->origin.x + xx;
                        tmp[(size_t)yy * w + xx] =
                            (sxx >= 0 && syy >= 0 && sxx < scw && syy < sch)
                                ? spx[(size_t)syy * scw + sxx]
                                : 0xFF000000u;
                    }
                }
            }
        }
        for (int yy = 0; yy < h; yy++) {
            int dyy = y + dst->origin.y + yy;
            if (dyy < 0 || dyy >= tk_canvas_height(dst->canvas))
                continue;
            for (int xx = 0; xx < w; xx++) {
                int dxx = x + dst->origin.x + xx;
                if (dxx < 0 || dxx >= dcw)
                    continue;
                /* clip test (canvas coords) */
                int inside = 0;
                for (int ci = 0; ci < n; ci++)
                    if (dxx >= clips[ci].left && dxx < clips[ci].right
                        && dyy >= clips[ci].top && dyy < clips[ci].bottom) {
                        inside = 1;
                        break;
                    }
                if (!inside)
                    continue;
                uint32_t v;
                if (tmp) {
                    v = tmp[(size_t)yy * w + xx];
                } else {
                    int sxx = sx + src->origin.x + xx;
                    int syy = sy + src->origin.y + yy;
                    if (sxx < 0 || syy < 0 || sxx >= scw || syy >= sch)
                        continue;
                    v = spx[(size_t)syy * scw + sxx];
                }
                if (from_dib)
                    v |= 0xFF000000u;
                dpx[(size_t)dyy * dcw + dxx] = v;
            }
        }
        free(tmp);
    } else {
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) {
                uint32_t s = read_px(src, sx + xx, sy + yy);
                if (from_dib)
                    s |= 0xFF000000u;
                uint32_t d = read_px(dst, x + xx, y + yy);
                plot_px(dst, x + xx, y + yy, rop_apply(rop, s, d, pat));
            }
    }
    CE_RECT r = { x, y, x + w, y + h };
    dc_damage(dst, r);
    if (dst->is_memory && dst->bitmap && dst->bitmap->dib_bits)
        gdi_sync_canvas_to_dib(dst->bitmap);
    return CE_TRUE;
}

CE_BOOL ce_StretchBlt(CE_HANDLE hdc, int x, int y, int w, int h,
                      CE_HANDLE src_h, int sx, int sy, int sw, int sh,
                      CE_DWORD rop)
{
    ce_dc *dst = gdi_dc_from_handle(hdc);
    ce_dc *src = gdi_dc_from_handle(src_h);
    if (!dst || !src || !dst->canvas || !src->canvas) return CE_FALSE;
    if (src->is_memory && src->bitmap && src->bitmap->dib_bits)
        gdi_sync_dib_to_canvas(src->bitmap);
    uint32_t pat = dst->brush ? gdi_cref_to_px(dst->brush->brush_color)
                              : 0xFF000000u;
    int from_dib = src->is_memory && src->bitmap && src->bitmap->dib_bits;
    if (w == 0 || h == 0 || sw == 0 || sh == 0) return CE_FALSE;
    for (int yy = 0; yy < h; yy++) {
        int syy = sy + (int)((long)yy * sh / h);
        for (int xx = 0; xx < w; xx++) {
            int sxx = sx + (int)((long)xx * sw / w);
            uint32_t s = read_px(src, sxx, syy);
            if (from_dib)
                s |= 0xFF000000u;
            if (rop == CE_SRCCOPY) {
                plot_px(dst, x + xx, y + yy, s);
            } else {
                uint32_t d = read_px(dst, x + xx, y + yy);
                plot_px(dst, x + xx, y + yy, rop_apply(rop, s, d, pat));
            }
        }
    }
    CE_RECT r = { x, y, x + w, y + h };
    dc_damage(dst, r);
    if (dst->is_memory && dst->bitmap && dst->bitmap->dib_bits)
        gdi_sync_canvas_to_dib(dst->bitmap);
    return CE_TRUE;
}

/* ---------------- bitmap creation ---------------- */

CE_HANDLE ce_CreateCompatibleBitmap(CE_HANDLE ref, int w, int h)
{
    (void)ref;
    ce_gdobj *o = new_bitmap_obj(w, h);
    return o ? (CE_HANDLE)o : NULL;
}

CE_HANDLE ce_CreateBitmap(int w, int h, unsigned planes, unsigned bpp,
                          const void *bits)
{
    (void)planes;
    ce_gdobj *o = new_bitmap_obj(w, h);
    if (!o)
        return NULL;
    if (bits) {
        const uint8_t *src = bits;
        if (bpp == 32) {
            for (int y = 0; y < h; y++) {
                int row = h - 1 - y; /* bottom-up */
                const uint32_t *srow = (const uint32_t *)(src + (size_t)row * w * 4);
                for (int x = 0; x < w; x++)
                    o->bmp_px[(size_t)y * w + x] = srow[x] | 0xFF000000u;
            }
        } else if (bpp == 24) {
            int stride = ((w * 3) + 3) & ~3;
            for (int y = 0; y < h; y++) {
                int row = h - 1 - y;
                const uint8_t *srow = src + (size_t)row * stride;
                for (int x = 0; x < w; x++)
                    o->bmp_px[(size_t)y * w + x] = 0xFF000000u
                        | ((uint32_t)srow[x * 3 + 2] << 16)
                        | ((uint32_t)srow[x * 3 + 1] << 8)
                        | srow[x * 3];
            }
        } else if (bpp == 16) {
            for (int y = 0; y < h; y++) {
                int row = h - 1 - y;
                const uint16_t *srow = (const uint16_t *)(src + (size_t)row * w * 2);
                for (int x = 0; x < w; x++) {
                    uint16_t v = srow[x];
                    uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F,
                             bl = v & 0x1F;
                    o->bmp_px[(size_t)y * w + x] = 0xFF000000u
                        | ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8)
                        | (bl * 255 / 31);
                }
            }
        } else if (bpp == 1) {
            int stride = ((w + 31) / 32) * 4;
            for (int y = 0; y < h; y++) {
                int row = h - 1 - y;
                const uint8_t *srow = src + (size_t)row * stride;
                for (int x = 0; x < w; x++) {
                    int bit = (srow[x / 8] >> (7 - (x % 8))) & 1;
                    o->bmp_px[(size_t)y * w + x] =
                        bit ? 0xFFFFFFFFu : 0xFF000000u;
                }
            }
        }
    }
    return (CE_HANDLE)o;
}

CE_HANDLE ce_CreateDIBSection(CE_HANDLE ref, const void *bmi,
                              unsigned usage, void **bits_out,
                              CE_HANDLE section, CE_DWORD offset)
{
    (void)ref; (void)usage; (void)section; (void)offset;
    if (!bmi || !bits_out)
        return NULL;
    const CE_BITMAPINFOHEADER *h = bmi;
    if (h->biSize < sizeof(CE_BITMAPINFOHEADER) || h->biPlanes != 1
        || h->biCompression != 0
        || (h->biBitCount != 32 && h->biBitCount != 24
            && h->biBitCount != 16)) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    int w = h->biWidth;
    int hh = h->biHeight;
    int topdown = hh < 0;
    if (hh < 0) hh = -hh;
    if (w <= 0 || hh <= 0)
        return NULL;
    ce_gdobj *o = new_bitmap_obj(w, hh);
    if (!o)
        return NULL;
    o->dib_bpp = h->biBitCount;
    o->dib_topdown = topdown;
    o->dib_stride = ((w * (h->biBitCount / 8)) + 3) & ~3;
    o->dib_bits = calloc((size_t)o->dib_stride * hh, 1);
    if (!o->dib_bits) {
        free_obj(o);
        return NULL;
    }
    gdi_sync_dib_to_canvas(o); /* bits start zeroed -> opaque black */
    *bits_out = o->dib_bits;
    return (CE_HANDLE)o;
}

CE_BOOL ce_SetBitmapBits(CE_HANDLE bmp, CE_DWORD count, const void *bits)
{
    ce_gdobj *o = gdi_obj_from_handle(bmp);
    if (!o || o->type != CE_OBJ_BITMAP || !bits)
        return CE_FALSE;
    if (o->dib_bits) {
        size_t avail = (size_t)o->dib_stride * o->bmp_h;
        size_t n = count < avail ? count : avail;
        memcpy(o->dib_bits, bits, n);
        gdi_sync_dib_to_canvas(o);
        return CE_TRUE;
    }
    /* compatible bitmaps are 32bpp; source is bottom-up rows */
    size_t need = (size_t)o->bmp_w * o->bmp_h * 4;
    if (count < need)
        return CE_FALSE;
    const uint32_t *src = bits;
    for (int y = 0; y < o->bmp_h; y++) {
        int row = o->bmp_h - 1 - y;
        for (int x = 0; x < o->bmp_w; x++)
            o->bmp_px[(size_t)y * o->bmp_w + x] =
                src[(size_t)row * o->bmp_w + x] | 0xFF000000u;
    }
    return CE_TRUE;
}

int ce_GetDIBColorTable(CE_HANDLE hdc, unsigned start, unsigned entries,
                        void *rgb)
{
    (void)hdc; (void)start; (void)entries; (void)rgb;
    return 0; /* direct-color surfaces have no color table */
}

int ce_SetDIBColorTable(CE_HANDLE hdc, unsigned start, unsigned entries,
                        const void *rgb)
{
    (void)hdc; (void)start; (void)entries; (void)rgb;
    return 0;
}

/* ---------------- regions ---------------- */

static int rgn_complexity(const ce_gdobj *o)
{
    if (!o || o->nrgn == 0) return CE_NULLREGION;
    return o->nrgn == 1 ? CE_SIMPLEREGION : CE_COMPLEXREGION;
}

/* subtract rect s from list (in place via temp), returns new count */
static int rect_list_subtract(CE_RECT *list, int n, CE_RECT s)
{
    CE_RECT out[64];
    int nout = 0;
    for (int i = 0; i < n; i++) {
        CE_RECT a = list[i];
        CE_RECT in = ce_rect_intersect(a, s);
        if (ce_rect_empty(&in)) {
            if (nout < 64) out[nout++] = a;
            continue;
        }
        CE_RECT parts[4];
        int np = 0;
        if (in.top > a.top) parts[np++] = (CE_RECT){ a.left, a.top, a.right, in.top };
        if (in.bottom < a.bottom) parts[np++] = (CE_RECT){ a.left, in.bottom, a.right, a.bottom };
        if (in.left > a.left) parts[np++] = (CE_RECT){ a.left, in.top, in.left, in.bottom };
        if (in.right < a.right) parts[np++] = (CE_RECT){ in.right, in.top, a.right, in.bottom };
        for (int k = 0; k < np && nout < 64; k++)
            out[nout++] = parts[k];
    }
    memcpy(list, out, (size_t)nout * sizeof(CE_RECT));
    return nout;
}

CE_HANDLE ce_CreateRectRgn(int l, int t, int r, int b)
{
    ce_gdobj *o = gdi_new_obj(CE_OBJ_RGN);
    if (!o) return NULL;
    CE_RECT rc = { l, t, r, b };
    if (!ce_rect_empty(&rc))
        o->rgn[o->nrgn++] = rc;
    return (CE_HANDLE)o;
}

CE_HANDLE ce_CreateRectRgnIndirect(const CE_RECT *r)
{
    if (!r) return NULL;
    return ce_CreateRectRgn(r->left, r->top, r->right, r->bottom);
}

int ce_CombineRgn(CE_HANDLE dst_h, CE_HANDLE a_h, CE_HANDLE b_h, int mode)
{
    ce_gdobj *d = gdi_obj_from_handle(dst_h);
    ce_gdobj *a = gdi_obj_from_handle(a_h);
    ce_gdobj *b = gdi_obj_from_handle(b_h);
    if (!d || !a || d->type != CE_OBJ_RGN || a->type != CE_OBJ_RGN)
        return CE_ERROR_RGN;
    CE_RECT tmp[32];
    int n = 0;
    switch (mode) {
    case CE_RGN_COPY:
        n = a->nrgn;
        memcpy(tmp, a->rgn, (size_t)n * sizeof(CE_RECT));
        break;
    case CE_RGN_OR: {
        n = a->nrgn;
        memcpy(tmp, a->rgn, (size_t)n * sizeof(CE_RECT));
        if (b && b->type == CE_OBJ_RGN)
            for (int i = 0; i < b->nrgn && n < 32; i++)
                tmp[n++] = b->rgn[i];
        break;
    }
    case CE_RGN_AND:
        if (b && b->type == CE_OBJ_RGN)
            for (int i = 0; i < a->nrgn; i++)
                for (int k = 0; k < b->nrgn && n < 32; k++) {
                    CE_RECT in = ce_rect_intersect(a->rgn[i], b->rgn[k]);
                    if (!ce_rect_empty(&in))
                        tmp[n++] = in;
                }
        break;
    case CE_RGN_DIFF:
        n = a->nrgn;
        memcpy(tmp, a->rgn, (size_t)n * sizeof(CE_RECT));
        if (b && b->type == CE_OBJ_RGN)
            for (int i = 0; i < b->nrgn; i++)
                n = rect_list_subtract(tmp, n, b->rgn[i]);
        break;
    case CE_RGN_XOR: {
        /* (a or b) minus (a and b) */
        CE_RECT uni[32];
        int nu = a->nrgn;
        memcpy(uni, a->rgn, (size_t)nu * sizeof(CE_RECT));
        if (b && b->type == CE_OBJ_RGN) {
            for (int i = 0; i < b->nrgn && nu < 32; i++)
                uni[nu++] = b->rgn[i];
            for (int i = 0; i < a->nrgn; i++)
                for (int k = 0; k < b->nrgn; k++) {
                    CE_RECT in = ce_rect_intersect(a->rgn[i], b->rgn[k]);
                    if (!ce_rect_empty(&in))
                        nu = rect_list_subtract(uni, nu, in);
                }
        }
        n = nu;
        memcpy(tmp, uni, (size_t)n * sizeof(CE_RECT));
        break;
    }
    default:
        return CE_ERROR_RGN;
    }
    d->nrgn = n;
    memcpy(d->rgn, tmp, (size_t)n * sizeof(CE_RECT));
    return rgn_complexity(d);
}

int ce_GetRgnBox(CE_HANDLE hrgn, CE_RECT *out)
{
    ce_gdobj *o = gdi_obj_from_handle(hrgn);
    if (!o || o->type != CE_OBJ_RGN || !out)
        return CE_ERROR_RGN;
    if (o->nrgn == 0) {
        *out = (CE_RECT){ 0, 0, 0, 0 };
        return CE_NULLREGION;
    }
    CE_RECT bb = o->rgn[0];
    for (int i = 1; i < o->nrgn; i++)
        bb = ce_rect_union(bb, o->rgn[i]);
    *out = bb;
    return rgn_complexity(o);
}

int ce_OffsetRgn(CE_HANDLE hrgn, int dx, int dy)
{
    ce_gdobj *o = gdi_obj_from_handle(hrgn);
    if (!o || o->type != CE_OBJ_RGN)
        return CE_ERROR_RGN;
    for (int i = 0; i < o->nrgn; i++) {
        o->rgn[i].left += dx;
        o->rgn[i].right += dx;
        o->rgn[i].top += dy;
        o->rgn[i].bottom += dy;
    }
    return rgn_complexity(o);
}

int ce_SelectClipRgn(CE_HANDLE hdc, CE_HANDLE hrgn)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc)
        return CE_ERROR_RGN;
    int prev = dc->nclip == 0 ? CE_NULLREGION
               : dc->nclip == 1 ? CE_SIMPLEREGION : CE_COMPLEXREGION;
    if (!hrgn) {
        dc->nclip = 0;
        return prev;
    }
    ce_gdobj *o = gdi_obj_from_handle(hrgn);
    if (!o || o->type != CE_OBJ_RGN)
        return CE_ERROR_RGN;
    dc->nclip = o->nrgn < CE_MAX_RECTS ? o->nrgn : CE_MAX_RECTS;
    for (int i = 0; i < dc->nclip; i++)
        dc->clip[i] = o->rgn[i];
    return prev;
}

int ce_GetClipBox(CE_HANDLE hdc, CE_RECT *out)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !out)
        return CE_ERROR_RGN;
    if (dc->nclip == 0) {
        if (dc->canvas) {
            out->left = -dc->origin.x;
            out->top = -dc->origin.y;
            out->right = out->left + tk_canvas_width(dc->canvas);
            out->bottom = out->top + tk_canvas_height(dc->canvas);
        } else {
            *out = (CE_RECT){ 0, 0, 0, 0 };
        }
        return CE_SIMPLEREGION;
    }
    CE_RECT bb = dc->clip[0];
    for (int i = 1; i < dc->nclip; i++)
        bb = ce_rect_union(bb, dc->clip[i]);
    *out = bb;
    return dc->nclip == 1 ? CE_SIMPLEREGION : CE_COMPLEXREGION;
}

CE_BOOL ce_RectVisible(CE_HANDLE hdc, const CE_RECT *r)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !r)
        return CE_FALSE;
    if (dc->nclip == 0)
        return CE_TRUE;
    for (int i = 0; i < dc->nclip; i++) {
        CE_RECT in = ce_rect_intersect(dc->clip[i], *r);
        if (!ce_rect_empty(&in))
            return CE_TRUE;
    }
    return CE_FALSE;
}

int ce_SetWindowRgn(CE_HANDLE hwnd, CE_HANDLE hrgn, CE_BOOL redraw)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return 0;
    ce_gdobj *o = hrgn ? gdi_obj_from_handle(hrgn) : NULL;
    if (w->win_rgn && w->win_rgn != o)
        ce_DeleteObject((CE_HANDLE)w->win_rgn);
    w->win_rgn = o; /* ownership transfers, as on CE */
    if (redraw)
        gwe_invalidate(w, NULL, 1);
    return 1;
}

/* ---------------- text ---------------- */

/* Convert a CE (UTF-16) string to UTF-8. len < 0 means NUL-terminated.
 * Returns the number of source characters consumed. Output is always
 * NUL-terminated when outsz > 0. */
static int ce16_to_utf8(const CE_WCHAR *s, int len, char *out, size_t outsz)
{
    if (!s) {
        if (outsz) out[0] = 0;
        return 0;
    }
    int n = 0;
    size_t o = 0;
    while (len < 0 ? s[n] != 0 : n < len) {
        uint32_t cp = s[n++];
        if (cp >= 0xD800 && cp <= 0xDBFF
            && (len < 0 ? s[n] != 0 : n < len)) {
            uint32_t lo = s[n];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                n++;
            }
        }
        char tmp[4];
        int tl;
        if (cp < 0x80) {
            tmp[0] = (char)cp; tl = 1;
        } else if (cp < 0x800) {
            tmp[0] = (char)(0xC0 | (cp >> 6));
            tmp[1] = (char)(0x80 | (cp & 0x3F)); tl = 2;
        } else if (cp < 0x10000) {
            tmp[0] = (char)(0xE0 | (cp >> 12));
            tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            tmp[2] = (char)(0x80 | (cp & 0x3F)); tl = 3;
        } else {
            tmp[0] = (char)(0xF0 | (cp >> 18));
            tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            tmp[3] = (char)(0x80 | (cp & 0x3F)); tl = 4;
        }
        if (o + (size_t)tl + 1 > outsz)
            break;
        memcpy(out + o, tmp, (size_t)tl);
        o += (size_t)tl;
    }
    if (outsz)
        out[o] = 0;
    return n;
}

static int dc_font_px(ce_dc *dc)
{
    int px = dc->font ? dc->font->font_px : 13;
    return px > 0 ? px : 13;
}

/* Draw one line of UTF-8 at logical (x, y) honouring the DC clip. */
static void draw_line_utf8(ce_dc *dc, int x, int y, const char *utf8)
{
    if (!dc->canvas || !utf8 || !utf8[0])
        return;
    gwe_font_ensure();
    const tk_theme *th = tk_theme_default();
    int px = dc_font_px(dc);
    int bold = dc->font ? dc->font->font_bold : 0;
    tk_color col = px_to_tk(gdi_cref_to_px(dc->text_color));
    CE_RECT clips[CE_MAX_RECTS];
    int n = dc_clip_rects(dc, clips, CE_MAX_RECTS);
    for (int i = 0; i < n; i++) {
        tk_rect cr = { clips[i].left, clips[i].top,
                       clips[i].right - clips[i].left,
                       clips[i].bottom - clips[i].top };
        tk_canvas_push_clip(dc->canvas, cr);
        tk_canvas_draw_text_px(dc->canvas, th, px, bold != 0,
                               x + dc->origin.x, y + dc->origin.y,
                               utf8, col);
        tk_canvas_pop_clip(dc->canvas);
    }
}

static int text_width_logical(ce_dc *dc, const char *utf8)
{
    gwe_font_ensure();
    return tk_text_width_px(tk_theme_default(), dc_font_px(dc), utf8);
}

CE_BOOL ce_TextOutW(CE_HANDLE hdc, int x, int y, const CE_WCHAR *str,
                    int len)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !dc->canvas || !str)
        return CE_FALSE;
    char buf[1024];
    ce16_to_utf8(str, len, buf, sizeof(buf));
    int px = dc_font_px(dc);
    int w = text_width_logical(dc, buf);
    int lh = tk_line_height_px(tk_theme_default(), px);
    if (dc->bk_mode == CE_OPAQUE) {
        CE_RECT bg = { x, y, x + w, y + lh };
        gdi_fill_rect_px(dc, bg, gdi_cref_to_px(dc->bk_color));
    }
    draw_line_utf8(dc, x, y, buf);
    CE_RECT dmg = { x, y, x + w, y + lh };
    dc_damage(dc, dmg);
    return CE_TRUE;
}

CE_BOOL ce_ExtTextOutW(CE_HANDLE hdc, int x, int y, unsigned options,
                       const CE_RECT *clip, const CE_WCHAR *str, int len,
                       const int *dx)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !dc->canvas)
        return CE_FALSE;
    if ((options & CE_ETO_OPAQUE) && clip) {
        CE_RECT bg = *clip;
        gdi_fill_rect_px(dc, bg, gdi_cref_to_px(dc->bk_color));
        dc_damage(dc, bg);
    }
    if (!str || len == 0)
        return CE_TRUE;
    /* Save the caller clip so ETO_CLIPPED can narrow it for this
     * call only; the DC clip is restored before returning. */
    CE_RECT saved[CE_MAX_RECTS];
    int nsaved = dc->nclip;
    memcpy(saved, dc->clip, sizeof(saved));
    if ((options & CE_ETO_CLIPPED) && clip) {
        if (dc->nclip == 0) {
            dc->clip[0] = *clip;
            dc->nclip = 1;
        } else {
            CE_RECT narrowed[CE_MAX_RECTS];
            int nn = 0;
            for (int i = 0; i < dc->nclip; i++) {
                CE_RECT in = ce_rect_intersect(dc->clip[i], *clip);
                if (!ce_rect_empty(&in))
                    narrowed[nn++] = in;
            }
            memcpy(dc->clip, narrowed, sizeof(narrowed));
            dc->nclip = nn;
        }
    }
    char buf[1024];
    if (dx) {
        /* per-character placement from the dx advance array */
        int cx = x;
        for (int i = 0; i < len; i++) {
            char one[8];
            ce16_to_utf8(str + i, 1, one, sizeof(one));
            draw_line_utf8(dc, cx, y, one);
            cx += dx[i];
        }
        CE_RECT dmg = { x, y, cx, y + tk_line_height_px(tk_theme_default(),
                                                        dc_font_px(dc)) };
        dc_damage(dc, dmg);
    } else {
        ce16_to_utf8(str, len, buf, sizeof(buf));
        int w = text_width_logical(dc, buf);
        draw_line_utf8(dc, x, y, buf);
        CE_RECT dmg = { x, y, x + w,
                        y + tk_line_height_px(tk_theme_default(),
                                              dc_font_px(dc)) };
        dc_damage(dc, dmg);
    }
    /* restore the DC clip saved at entry */
    memcpy(dc->clip, saved, sizeof(saved));
    dc->nclip = nsaved;
    return CE_TRUE;
}

/* Measure one line (no wrapping). */
static int drawtext_line(ce_dc *dc, const char *utf8, CE_RECT *rc,
                         unsigned fmt, int y, int lh, int calc_only)
{
    int w = text_width_logical(dc, utf8);
    int x = rc->left;
    if (fmt & CE_DT_CENTER)
        x = rc->left + ((rc->right - rc->left) - w) / 2;
    else if (fmt & CE_DT_RIGHT)
        x = rc->right - w;
    if (!calc_only)
        draw_line_utf8(dc, x, y, utf8);
    return w;
}

int ce_DrawTextW(CE_HANDLE hdc, const CE_WCHAR *str, int len,
                 CE_RECT *rc, unsigned fmt)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !rc || !str)
        return 0;
    gwe_font_ensure();
    int px = dc_font_px(dc);
    int lh = tk_line_height_px(tk_theme_default(), px);
    /* Work on a CE_WCHAR copy with '&' prefixes stripped (unless
     * NOPREFIX) and explicit length resolved. */
    CE_WCHAR tmp[2048];
    int n = 0;
    int total = len < 0 ? ce_wcslen(str) : len;
    if (total > 2040) total = 2040;
    for (int i = 0; i < total; i++) {
        CE_WCHAR c = str[i];
        if (c == '&' && !(fmt & CE_DT_NOPREFIX)) {
            if (i + 1 < total && str[i + 1] == '&')
                tmp[n++] = str[++i];
            continue; /* prefix marker: dropped (no underline drawn) */
        }
        tmp[n++] = c;
    }
    int calc = (fmt & CE_DT_CALCRECT) != 0;
    int single = (fmt & CE_DT_SINGLELINE) != 0;
    int wrap = (fmt & CE_DT_WORDBREAK) != 0 && !single;
    int maxw = 0;
    int lines = 0;
    int y = rc->top;
    if (single && (fmt & CE_DT_VCENTER))
        y = rc->top + ((rc->bottom - rc->top) - lh) / 2;
    else if (single && (fmt & CE_DT_BOTTOM))
        y = rc->bottom - lh;

    int pos = 0;
    while (pos <= n) {
        /* find the end of this visual line */
        int line_end = pos;
        int hard_break = 0;
        if (wrap) {
            /* greedy word wrap within rc width */
            int best = -1;
            int i = pos;
            while (i < n && tmp[i] != '\n') {
                if (tmp[i] == ' ')
                    best = i;
                char seg[1024];
                CE_WCHAR wtmp[600];
                int wl = i - pos + 1;
                if (wl > 590) break;
                memcpy(wtmp, tmp + pos, (size_t)wl * sizeof(CE_WCHAR));
                wtmp[wl] = 0;
                ce16_to_utf8(wtmp, wl, seg, sizeof(seg));
                if (text_width_logical(dc, seg) > rc->right - rc->left
                    && i > pos)
                    break;
                i++;
            }
            if (i >= n || tmp[i] == '\n') {
                line_end = i;
                hard_break = i < n;
            } else if (best >= pos) {
                line_end = best; /* break at last space */
                hard_break = 0;
            } else {
                line_end = i; /* break mid-word */
            }
        } else {
            int i = pos;
            while (i < n && tmp[i] != '\n')
                i++;
            line_end = i;
            hard_break = i < n;
            if (single)
                hard_break = 0;
        }
        int ll = line_end - pos;
        CE_WCHAR ltmp[1024];
        if (ll > 1020) ll = 1020;
        memcpy(ltmp, tmp + pos, (size_t)ll * sizeof(CE_WCHAR));
        ltmp[ll] = 0;
        char lbuf[1024];
        ce16_to_utf8(ltmp, ll, lbuf, sizeof(lbuf));
        int w = drawtext_line(dc, lbuf, rc, fmt, y, lh, calc);
        if (w > maxw) maxw = w;
        lines++;
        y += lh;
        if (!hard_break && line_end >= n)
            break;
        if (single)
            break;
        pos = line_end + 1; /* skip the break char (space or \n) */
        if (pos > n)
            break;
        if (!calc && y >= rc->bottom)
            break;
    }
    int height = lines * lh;
    if (calc) {
        if (single || !wrap) {
            rc->right = rc->left + maxw;
        }
        rc->bottom = rc->top + height;
    } else {
        CE_RECT dmg = *rc;
        dc_damage(dc, dmg);
    }
    return height;
}

CE_BOOL ce_GetTextExtentExPointW(CE_HANDLE hdc, const CE_WCHAR *str,
                                 int len, int max_extent, int *fit,
                                 int *dx, CE_SIZE *size)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !str)
        return CE_FALSE;
    gwe_font_ensure();
    int total = len < 0 ? ce_wcslen(str) : len;
    char buf[2048];
    ce16_to_utf8(str, total, buf, sizeof(buf));
    int w = text_width_logical(dc, buf);
    if (size) {
        size->cx = w;
        size->cy = tk_line_height_px(tk_theme_default(), dc_font_px(dc));
    }
    if (fit || dx) {
        /* per-character advances by measuring each glyph alone */
        int acc = 0;
        int nfit = 0;
        for (int i = 0; i < total; i++) {
            char one[8];
            ce16_to_utf8(str + i, 1, one, sizeof(one));
            int cw = text_width_logical(dc, one);
            acc += cw;
            if (dx)
                dx[i] = acc;
            if (max_extent <= 0 || acc <= max_extent)
                nfit = i + 1;
        }
        if (fit)
            *fit = nfit;
    }
    return CE_TRUE;
}

CE_BOOL ce_GetTextMetricsW(CE_HANDLE hdc, CE_TEXTMETRICW *tm)
{
    ce_dc *dc = gdi_dc_from_handle(hdc);
    if (!dc || !tm)
        return CE_FALSE;
    gwe_font_ensure();
    int px = dc_font_px(dc);
    int lh = tk_line_height_px(tk_theme_default(), px);
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = lh;
    tm->tmAscent = lh * 4 / 5;
    tm->tmDescent = lh - tm->tmAscent;
    tm->tmInternalLeading = lh > px ? lh - px : 0;
    tm->tmExternalLeading = 0;
    char mbuf[2] = { 'M', 0 };
    char xbuf[2] = { 'x', 0 };
    tm->tmAveCharWidth = text_width_logical(dc, xbuf);
    tm->tmMaxCharWidth = text_width_logical(dc, mbuf);
    tm->tmWeight = dc->font && dc->font->font_bold ? 700 : 400;
    tm->tmDigitizedAspectX = 96;
    tm->tmDigitizedAspectY = 96;
    tm->tmFirstChar = 32;
    tm->tmLastChar = 0xFFFD;
    tm->tmDefaultChar = '?';
    tm->tmBreakChar = ' ';
    tm->tmItalic = dc->font ? (uint8_t)dc->font->font_italic : 0;
    tm->tmPitchAndFamily = 0x22; /* VARIABLE_PITCH | FF_SWISS approx */
    tm->tmCharSet = 0;           /* ANSI_CHARSET */
    return CE_TRUE;
}
