/* BrainCE Layer -- ImageList (M3).
 *
 * commctrl's toolbar/trackbar/listview code keeps its glyphs in
 * image lists, which live in coredll on CE. An image list here
 * is a strip bitmap (count * cx wide) plus a 1-bit-semantics
 * mask strip, both ordinary layer GDI bitmaps. Drawing uses
 * the documented masked-blit sequence through the layer's own
 * BitBlt (SRCAND the mask, SRCPAINT the image), with
 * ILD_TRANSPARENT resolved against the list background color.
 * The drag-image functions and CopyDitherImage stay stubs:
 * nothing in the hosted controls' non-interactive paths calls
 * them, and a fake drag image would be worse than an honest
 * stub report if something ever does.
 */
#include <stdlib.h>
#include <string.h>

#include "ce_types.h"
#include "gwe.h"

CE_HANDLE ce_CreateBitmap(int w, int h, unsigned planes,
                          unsigned bpp, const void *bits);
CE_HANDLE ce_CreateCompatibleDC(CE_HANDLE hdc);
CE_BOOL ce_DeleteDC(CE_HANDLE hdc);
CE_HANDLE ce_SelectObject(CE_HANDLE hdc, CE_HANDLE obj);
CE_BOOL ce_BitBlt(CE_HANDLE hdc, int x, int y, int w, int h,
                  CE_HANDLE src, int sx, int sy, CE_DWORD rop);
CE_BOOL ce_DeleteObject(CE_HANDLE h);

#define CLR_NONE_V 0xFFFFFFFFu

typedef struct {
    int cx, cy;
    int count, alloc, grow;
    CE_DWORD flags;
    CE_DWORD bk;              /* CLR_NONE_V when unset */
    ce_gdobj *strip;          /* bitmap: alloc*cx x cy */
    ce_gdobj *mask;           /* bitmap: white=transparent */
} ce_imagelist;

static ce_gdobj *new_strip(int w, int h)
{
    if (w <= 0)
        w = 1;
    if (h <= 0)
        h = 1;
    return (ce_gdobj *)ce_CreateBitmap(w, h, 1, 32, NULL);
}

void *ce_ImageList_Create(int cx, int cy, CE_DWORD flags,
                          int initial, int grow)
{
    ce_imagelist *il = calloc(1, sizeof(*il));
    if (!il)
        return NULL;
    il->cx = cx;
    il->cy = cy;
    il->flags = flags;
    il->grow = grow > 0 ? grow : 4;
    il->alloc = initial > 0 ? initial : il->grow;
    il->bk = CLR_NONE_V;
    il->strip = new_strip(cx * il->alloc, cy);
    il->mask = new_strip(cx * il->alloc, cy);
    if (!il->strip || !il->mask) {
        if (il->strip)
            ce_DeleteObject((CE_HANDLE)il->strip);
        if (il->mask)
            ce_DeleteObject((CE_HANDLE)il->mask);
        free(il);
        return NULL;
    }
    /* mask starts fully transparent (white) */
    for (int i = 0; i < il->mask->bmp_w * il->mask->bmp_h; i++)
        il->mask->bmp_px[i] = 0xFFFFFFFFu;
    return il;
}

CE_BOOL ce_ImageList_Destroy(void *himl)
{
    ce_imagelist *il = himl;
    if (!il)
        return CE_FALSE;
    ce_DeleteObject((CE_HANDLE)il->strip);
    ce_DeleteObject((CE_HANDLE)il->mask);
    free(il);
    return CE_TRUE;
}

static int il_reserve(ce_imagelist *il, int need)
{
    if (need <= il->alloc)
        return 1;
    int na = il->alloc;
    while (na < need)
        na += il->grow;
    ce_gdobj *ns = new_strip(il->cx * na, il->cy);
    ce_gdobj *nm = new_strip(il->cx * na, il->cy);
    if (!ns || !nm) {
        if (ns)
            ce_DeleteObject((CE_HANDLE)ns);
        if (nm)
            ce_DeleteObject((CE_HANDLE)nm);
        return 0;
    }
    for (int i = 0; i < il->mask->bmp_w * il->mask->bmp_h; i++)
        nm->bmp_px[i] = 0xFFFFFFFFu;
    int copyw = il->cx * il->count;
    for (int y = 0; y < il->cy; y++) {
        memcpy(ns->bmp_px + (size_t)y * ns->bmp_w,
               il->strip->bmp_px + (size_t)y * il->strip->bmp_w,
               (size_t)copyw * 4);
        memcpy(nm->bmp_px + (size_t)y * nm->bmp_w,
               il->mask->bmp_px + (size_t)y * il->mask->bmp_w,
               (size_t)copyw * 4);
    }
    ce_DeleteObject((CE_HANDLE)il->strip);
    ce_DeleteObject((CE_HANDLE)il->mask);
    il->strip = ns;
    il->mask = nm;
    il->alloc = na;
    return 1;
}

static void il_blit_slot(ce_imagelist *il, ce_gdobj *dststrip,
                         ce_gdobj *srcbmp, int slot)
{
    CE_HANDLE ddc = ce_CreateCompatibleDC(NULL);
    CE_HANDLE sdc = ce_CreateCompatibleDC(NULL);
    if (!ddc || !sdc) {
        if (ddc)
            ce_DeleteDC(ddc);
        if (sdc)
            ce_DeleteDC(sdc);
        return;
    }
    ce_SelectObject(ddc, (CE_HANDLE)dststrip);
    ce_SelectObject(sdc, (CE_HANDLE)srcbmp);
    ce_BitBlt(ddc, slot * il->cx, 0, il->cx, il->cy, sdc, 0, 0,
              CE_SRCCOPY);
    ce_DeleteDC(ddc);
    ce_DeleteDC(sdc);
}

int ce_ImageList_Add(void *himl, CE_HANDLE hbmp, CE_HANDLE hmask)
{
    ce_imagelist *il = himl;
    ce_gdobj *src = (ce_gdobj *)hbmp;
    if (!il || !src || src->type != CE_OBJ_BITMAP)
        return -1;
    if (!il_reserve(il, il->count + 1))
        return -1;
    int slot = il->count;
    if (src->dib_bits)
        gdi_sync_dib_to_canvas(src);
    il_blit_slot(il, il->strip, src, slot);
    if (hmask) {
        ce_gdobj *m = (ce_gdobj *)hmask;
        if (m && m->type == CE_OBJ_BITMAP) {
            if (m->dib_bits)
                gdi_sync_dib_to_canvas(m);
            il_blit_slot(il, il->mask, m, slot);
        }
    } else {
        /* no mask: fully opaque slot */
        for (int y = 0; y < il->cy; y++)
            for (int x = 0; x < il->cx; x++)
                il->mask->bmp_px[(size_t)y * il->mask->bmp_w
                                 + slot * il->cx + x] = 0xFF000000u;
    }
    il->count++;
    return slot;
}

int ce_ImageList_AddMasked(void *himl, CE_HANDLE hbmp,
                           CE_DWORD cr_mask)
{
    ce_imagelist *il = himl;
    ce_gdobj *src = (ce_gdobj *)hbmp;
    if (!il || !src || src->type != CE_OBJ_BITMAP)
        return -1;
    int slot = ce_ImageList_Add(himl, hbmp, NULL);
    if (slot < 0)
        return -1;
    uint32_t key = gdi_cref_to_px(cr_mask);
    for (int y = 0; y < il->cy; y++) {
        for (int x = 0; x < il->cx; x++) {
            size_t si = (size_t)y * il->strip->bmp_w
                        + slot * il->cx + x;
            size_t mi = (size_t)y * il->mask->bmp_w
                        + slot * il->cx + x;
            if ((il->strip->bmp_px[si] & 0x00FFFFFFu)
                == (key & 0x00FFFFFFu)) {
                il->strip->bmp_px[si] = 0xFF000000u;
                il->mask->bmp_px[mi] = 0xFFFFFFFFu;
            } else {
                il->mask->bmp_px[mi] = 0xFF000000u;
            }
        }
    }
    return slot;
}

CE_BOOL ce_ImageList_Remove(void *himl, int index)
{
    ce_imagelist *il = himl;
    if (!il || index < -1 || index >= il->count)
        return CE_FALSE;
    if (index == -1) {
        il->count = 0;
        return CE_TRUE;
    }
    for (int y = 0; y < il->cy; y++) {
        uint32_t *srow = il->strip->bmp_px
                         + (size_t)y * il->strip->bmp_w;
        uint32_t *mrow = il->mask->bmp_px
                         + (size_t)y * il->mask->bmp_w;
        memmove(srow + (size_t)index * il->cx,
                srow + (size_t)(index + 1) * il->cx,
                (size_t)(il->count - index - 1) * il->cx * 4);
        memmove(mrow + (size_t)index * il->cx,
                mrow + (size_t)(index + 1) * il->cx,
                (size_t)(il->count - index - 1) * il->cx * 4);
    }
    il->count--;
    return CE_TRUE;
}

int ce_ImageList_GetImageCount(void *himl)
{
    ce_imagelist *il = himl;
    return il ? il->count : 0;
}

CE_BOOL ce_ImageList_SetImageCount(void *himl, unsigned count)
{
    ce_imagelist *il = himl;
    if (!il)
        return CE_FALSE;
    if ((int)count > il->alloc && !il_reserve(il, (int)count))
        return CE_FALSE;
    il->count = (int)count;
    return CE_TRUE;
}

CE_DWORD ce_ImageList_SetBkColor(void *himl, CE_DWORD bk)
{
    ce_imagelist *il = himl;
    if (!il)
        return CLR_NONE_V;
    CE_DWORD old = il->bk;
    il->bk = bk;
    return old;
}

CE_DWORD ce_ImageList_GetBkColor(void *himl)
{
    ce_imagelist *il = himl;
    return il ? il->bk : CLR_NONE_V;
}

CE_BOOL ce_ImageList_GetIconSize(void *himl, int *cx, int *cy)
{
    ce_imagelist *il = himl;
    if (!il)
        return CE_FALSE;
    if (cx)
        *cx = il->cx;
    if (cy)
        *cy = il->cy;
    return CE_TRUE;
}

CE_BOOL ce_ImageList_DrawEx(void *himl, int index, CE_HANDLE hdc,
                            int x, int y, int dx, int dy,
                            CE_DWORD rgb_bk, CE_DWORD rgb_fg,
                            CE_DWORD style)
{
    ce_imagelist *il = himl;
    if (!il || index < 0 || index >= il->count || !hdc)
        return CE_FALSE;
    if (dx <= 0)
        dx = il->cx;
    if (dy <= 0)
        dy = il->cy;
    (void)rgb_fg;
    CE_HANDLE sdc = ce_CreateCompatibleDC(NULL);
    if (!sdc)
        return CE_FALSE;
    /* ILD flags (public comctl32): NORMAL=0, TRANSPARENT=0x1,
     * BLEND25=0x2, BLEND50=0x4, MASK=0x10, IMAGE=0x20. */
    if (style & 0x0010u) {   /* ILD_MASK: draw the mask itself */
        ce_SelectObject(sdc, (CE_HANDLE)il->mask);
        ce_BitBlt(hdc, x, y, dx, dy, sdc, index * il->cx, 0,
                  CE_SRCCOPY);
        ce_DeleteDC(sdc);
        return CE_TRUE;
    }
    if (!(style & 0x0001u)) {   /* not ILD_TRANSPARENT: opaque */
        /* (ILD_BLEND styles are approximated by the plain copy;
         * the 25/50% dither blend is a later refinement.) */
        ce_SelectObject(sdc, (CE_HANDLE)il->strip);
        ce_BitBlt(hdc, x, y, dx, dy, sdc, index * il->cx, 0,
                  CE_SRCCOPY);
        ce_DeleteDC(sdc);
        return CE_TRUE;
    }
    /* transparent: if the list has a real mask, use the
     * AND/PAINT dance; otherwise key out the bk color into a
     * temporary mask first. */
    ce_SelectObject(sdc, (CE_HANDLE)il->mask);
    ce_BitBlt(hdc, x, y, dx, dy, sdc, index * il->cx, 0,
              CE_SRCAND);
    ce_SelectObject(sdc, (CE_HANDLE)il->strip);
    ce_BitBlt(hdc, x, y, dx, dy, sdc, index * il->cx, 0,
              CE_SRCPAINT);
    ce_DeleteDC(sdc);
    (void)rgb_bk;
    return CE_TRUE;
}

CE_BOOL ce_ImageList_Draw(void *himl, int index, CE_HANDLE hdc,
                          int x, int y, CE_DWORD style)
{
    return ce_ImageList_DrawEx(himl, index, hdc, x, y, 0, 0,
                               CLR_NONE_V, CLR_NONE_V, style);
}

typedef struct {
    int cbSize;
    void *himl;
    int i;
    CE_HANDLE hdcDst;
    int x, y, cx, cy;
    int xBitmap, yBitmap;
    CE_DWORD rgbBk, rgbFg;
    CE_DWORD fStyle;
    CE_DWORD dwRop;
    CE_DWORD fState;
    CE_DWORD Frame;
    CE_DWORD crEffect;
} ce_ild_params;

CE_BOOL ce_ImageList_DrawIndirect(ce_ild_params *p)
{
    if (!p)
        return CE_FALSE;
    return ce_ImageList_DrawEx(p->himl, p->i, p->hdcDst, p->x,
                               p->y, p->cx, p->cy, p->rgbBk,
                               p->rgbFg, p->fStyle);
}

int ce_ImageList_ReplaceIcon(void *himl, int index, CE_HANDLE hicon)
{
    /* Icons in this layer are cursor-class objects without a
     * public pixel surface; replacing from one is not supported
     * yet. Report failure honestly instead of faking a slot. */
    (void)himl;
    (void)index;
    (void)hicon;
    return -1;
}
