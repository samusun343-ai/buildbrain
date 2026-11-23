/* BrainCE Layer -- GWE shim internals (M2).
 *
 * Shared state between gwe.c (windows / message queue / WM client
 * glue) and gdi.c (device contexts / GDI objects / drawing).
 * Windows and DCs are pool-allocated and identified by their pool
 * address as the CE handle; handles are never reused while a window
 * is merely hidden, and a destroyed window stays in the pool marked
 * dead so stale handles fail IsWindow instead of aliasing.
 *
 * Coordinate model: the braindesk WM content area of a top-level
 * window IS the CE window's client area (the WM server draws the
 * chrome itself, outside the content). A top-level window therefore
 * has window rect == client rect at a shim-side virtual position
 * (programmatic moves update the virtual position and the size we
 * track, but the WM frame itself is server-driven; see the M2
 * report for the exact limitation). Child windows live in their
 * parent's client area; all painting lands in the top-level
 * window's shadow canvas and is flushed to the WM shared buffer at
 * EndPaint / ReleaseDC / UpdateWindow with damage-limited copies.
 */
#ifndef BRAINCE_GWE_H
#define BRAINCE_GWE_H

#include "ce_types.h"
#include "tk.h"
#include "wm.h"

#define CE_MAX_WINDOWS 256
#define CE_MAX_CLASSES 64
#define CE_MAX_DCS     128
#define CE_MAX_OBJECTS 512
#define CE_MAX_TIMERS  64
#define CE_MAX_QUEUE   512
#define CE_MAX_RECTS   16
#define CE_SCREEN_W    854
#define CE_SCREEN_H    480

typedef struct ce_window ce_window;
typedef struct ce_dc ce_dc;
typedef struct ce_gdobj ce_gdobj;

struct ce_window {
    int used;
    int dead;
    int is_desktop;
    struct ce_wclass *cls;
    CE_WNDPROC proc;
    CE_HANDLE hinstance;
    ce_window *parent;
    ce_window *first_child;   /* topmost first */
    ce_window *next_sibling;
    CE_RECT rect;             /* child: parent-client coords.
                               * top-level: (vx, vy, vx+w, vy+h) */
    CE_DWORD style, exstyle;
    CE_WCHAR *text;           /* owned UTF-16 copy, may be NULL */
    CE_HANDLE menu_or_id;
    void *userdata;
    void *create_param;
    uint8_t *extra;           /* cbWndExtra bytes */
    int visible;
    int enabled;
    /* update region in client coords */
    CE_RECT upd[CE_MAX_RECTS];
    int nupd;
    int upd_erase;
    /* top-level only: WM connection + shadow surface */
    bd_wm_client *wm;
    uint32_t *shadow_px;
    tk_canvas *shadow;
    int surf_w, surf_h;       /* shadow size == client size */
    CE_RECT dmg[CE_MAX_RECTS];/* drawn since last flush, client coords */
    int ndmg;
    ce_gdobj *win_rgn;        /* SetWindowRgn (stored; not shaped) */
};

typedef struct ce_wclass {
    int used;
    CE_WCHAR *name;           /* owned */
    CE_DWORD style;
    CE_WNDPROC proc;
    int cls_extra, wnd_extra;
    CE_HANDLE hinstance;
    CE_HANDLE hicon, hcursor, hbr_background;
    const CE_WCHAR *menu_name; /* borrowed (not owned) */
    int win_count;
} ce_wclass;

enum ce_obj_type {
    CE_OBJ_PEN = 1, CE_OBJ_BRUSH, CE_OBJ_FONT, CE_OBJ_BITMAP,
    CE_OBJ_RGN, CE_OBJ_CURSOR
};

struct ce_gdobj {
    int used;
    int type;
    int stock;                /* stock objects are never deleted */
    /* pen */
    CE_DWORD pen_style, pen_width, pen_color;
    /* brush */
    CE_DWORD brush_style, brush_color;
    /* font */
    int font_px, font_bold, font_italic;
    CE_WCHAR font_face[32];
    /* bitmap: pixels are RGBA8888 words (0xAARRGGBB), row-major */
    int bmp_w, bmp_h;
    uint32_t *bmp_px;
    tk_canvas *bmp_canvas;
    /* DIBSection raw view (bpp 32/24/16): raw bits in the format
     * the app asked for; conversions happen at sync points. */
    int dib_bpp;
    int dib_topdown;
    uint8_t *dib_bits;        /* owned when the section is ours */
    int dib_stride;           /* bytes per row (positive) */
    /* region: rect list */
    CE_RECT rgn[32];
    int nrgn;
    /* cursor */
    int cursor_id;
};

struct ce_dc {
    int used;
    int is_memory;
    int is_paint;             /* handed out by BeginPaint */
    ce_window *win;           /* window DC target (NULL: memory) */
    ce_window *top;           /* top-level ancestor owning surface */
    CE_POINT origin;          /* DC coords -> top client coords */
    tk_canvas *canvas;        /* shadow canvas or bitmap canvas */
    ce_gdobj *bitmap;         /* memory DC: selected bitmap */
    ce_gdobj *pen, *brush, *font;
    CE_DWORD text_color, bk_color;
    int bk_mode;
    CE_POINT pos;             /* current pen position, DC coords */
    CE_RECT clip[CE_MAX_RECTS]; /* DC coords; nclip==0: unclipped */
    int nclip;
    CE_RECT dmg;              /* damage bbox, top client coords */
    int has_dmg;
    /* SaveDC stack */
    struct {
        ce_gdobj *pen, *brush, *font;
        CE_DWORD text_color, bk_color;
        int bk_mode;
        CE_POINT pos;
        CE_RECT clip[CE_MAX_RECTS];
        int nclip;
    } saved[8];
    int nsaved;
};

/* ---- shared state (gwe.c owns) ---- */
extern ce_window g_windows[CE_MAX_WINDOWS];
extern ce_window g_desktop;
extern ce_dc g_dcs[CE_MAX_DCS];
extern ce_gdobj g_objects[CE_MAX_OBJECTS];
extern CE_HANDLE g_focus_win;    /* ce_window* or NULL */
extern CE_HANDLE g_active_win;
extern CE_HANDLE g_capture_win;
extern CE_POINT g_cursor_pt;     /* last pointer pos, screen coords */
extern int g_button_down;

ce_window *gwe_win_from_handle(CE_HANDLE h);
ce_window *gwe_top_of(ce_window *w);
CE_POINT gwe_client_origin(ce_window *w); /* in top client coords */
int gwe_win_w(ce_window *w);
int gwe_win_h(ce_window *w);
CE_LONG gwe_call_proc(ce_window *w, CE_DWORD msg, CE_DWORD wp, CE_LONG lp);
CE_LONG gwe_send_internal(ce_window *w, CE_DWORD msg, CE_DWORD wp, CE_LONG lp);
void gwe_invalidate(ce_window *w, const CE_RECT *r, int erase);
void gwe_validate(ce_window *w, const CE_RECT *r);
void gwe_flush(ce_window *top);
void gwe_add_damage(ce_window *top, CE_RECT r);
void gwe_pump_wm(int timeout_ms);
void gwe_set_focus(ce_window *w);
int  gwe_enqueue(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp, CE_LONG lp);
void gwe_font_ensure(void);
CE_DWORD gwe_sys_color(int idx);

/* ---- gdi.c ---- */
ce_dc *gdi_dc_from_handle(CE_HANDLE h);
ce_dc *gdi_new_dc(void);
void gdi_free_dc(ce_dc *dc);
ce_gdobj *gdi_obj_from_handle(CE_HANDLE h);
ce_gdobj *gdi_new_obj(int type);
void gdi_fill_rect_px(ce_dc *dc, CE_RECT r, uint32_t px);
void gdi_sync_dib_to_canvas(ce_gdobj *b);
void gdi_sync_canvas_to_dib(ce_gdobj *b);
uint32_t gdi_cref_to_px(CE_DWORD cref);
CE_DWORD gdi_px_to_cref(uint32_t px);

/* rect helpers shared by both files */
int  ce_rect_empty(const CE_RECT *r);
CE_RECT ce_rect_intersect(CE_RECT a, CE_RECT b);
CE_RECT ce_rect_union(CE_RECT a, CE_RECT b);
void ce_rect_add(CE_RECT *list, int *n, CE_RECT r); /* merge/bbox cap */

#endif
