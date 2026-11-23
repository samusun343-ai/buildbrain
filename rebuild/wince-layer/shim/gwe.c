/* BrainCE Layer -- GWE shim, windows and messages (M2).
 *
 * Implements the window/message half of the CE GWE surface on top
 * of the braindesk WM surface protocol (DESIGN-wince-layer.md
 * section 5.4, integration stage 1 of section 8.2: the layer is a
 * separate component; each top-level CE window is a client of the
 * existing WM server and appears as a normal Brainux window).
 *
 * Model notes (see gwe.h for the coordinate model):
 *  - One message queue per process. CE queues are per-thread; the
 *    M2 target binaries are single-GUI-thread, and the queue is
 *    guarded by a mutex so worker threads may PostMessage safely.
 *  - WM_PAINT and WM_TIMER are synthesized by GetMessage/PeekMessage
 *    exactly where CE synthesizes them (posted queue first, then
 *    paint, then timers), never enqueued eagerly.
 *  - Input arrives as bd_wm_client events. Pointer events are
 *    hit-tested against the child tree; key events go to the focus
 *    window. This is the only input source: the layer never claims
 *    input it did not receive through the WM protocol.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "ce_types.h"
#include "fs_bridge.h"
#include "gwe.h"

/* defined in coredll_shim.c / gdi.c */
extern CE_DWORD ce_GetTickCount(void);
extern CE_HANDLE ce_BeginPaint(CE_HANDLE hwnd, CE_PAINTSTRUCT *ps);
extern CE_BOOL ce_EndPaint(CE_HANDLE hwnd, const CE_PAINTSTRUCT *ps);

/* forward declarations within this file */
CE_BOOL ce_DestroyWindow(CE_HANDLE hwnd);
CE_LONG ce_DefWindowProcW(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                          CE_LONG lp);
int gwe_enqueue_ch(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp, CE_LONG lp,
                   uint16_t pend_char);

ce_window g_windows[CE_MAX_WINDOWS];
ce_window g_desktop;
ce_dc g_dcs[CE_MAX_DCS];
ce_gdobj g_objects[CE_MAX_OBJECTS];
CE_HANDLE g_focus_win;
CE_HANDLE g_active_win;
CE_HANDLE g_capture_win;
CE_POINT g_cursor_pt;
int g_button_down;

static ce_wclass g_classes[CE_MAX_CLASSES];
static int g_gwe_ready;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------------- small helpers ---------------- */

static CE_WCHAR *wcs_dup(const CE_WCHAR *s)
{
    if (!s)
        return NULL;
    size_t n = ce_wcslen(s);
    CE_WCHAR *p = malloc((n + 1) * sizeof(CE_WCHAR));
    if (p)
        memcpy(p, s, (n + 1) * sizeof(CE_WCHAR));
    return p;
}

static int wcs_eq(const CE_WCHAR *a, const CE_WCHAR *b)
{
    if (!a || !b)
        return a == b;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int ce_rect_empty(const CE_RECT *r)
{
    return !r || r->right <= r->left || r->bottom <= r->top;
}

CE_RECT ce_rect_intersect(CE_RECT a, CE_RECT b)
{
    CE_RECT r;
    r.left = a.left > b.left ? a.left : b.left;
    r.top = a.top > b.top ? a.top : b.top;
    r.right = a.right < b.right ? a.right : b.right;
    r.bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

CE_RECT ce_rect_union(CE_RECT a, CE_RECT b)
{
    if (ce_rect_empty(&a)) return b;
    if (ce_rect_empty(&b)) return a;
    CE_RECT r;
    r.left = a.left < b.left ? a.left : b.left;
    r.top = a.top < b.top ? a.top : b.top;
    r.right = a.right > b.right ? a.right : b.right;
    r.bottom = a.bottom > b.bottom ? a.bottom : b.bottom;
    return r;
}

/* Append r to a damage/update rect list, merging overlaps and
 * collapsing to a single bounding box at the cap. */
void ce_rect_add(CE_RECT *list, int *n, CE_RECT r)
{
    if (ce_rect_empty(&r))
        return;
    for (int i = 0; i < *n; i++) {
        CE_RECT u = ce_rect_union(list[i], r);
        CE_RECT in = ce_rect_intersect(list[i], r);
        if (!ce_rect_empty(&in) || u.right - u.left <=
                (list[i].right - list[i].left) + (r.right - r.left)) {
            list[i] = u;
            return;
        }
    }
    if (*n < CE_MAX_RECTS) {
        list[(*n)++] = r;
        return;
    }
    CE_RECT bb = list[0];
    for (int i = 1; i < *n; i++)
        bb = ce_rect_union(bb, list[i]);
    bb = ce_rect_union(bb, r);
    list[0] = bb;
    *n = 1;
}

ce_window *gwe_win_from_handle(CE_HANDLE h)
{
    if (!h)
        return NULL;
    if (h == (CE_HANDLE)&g_desktop)
        return &g_desktop;
    ce_window *w = (ce_window *)h;
    if (w < &g_windows[0] || w >= &g_windows[CE_MAX_WINDOWS])
        return NULL;
    if (!w->used || w->dead)
        return NULL;
    return w;
}

static ce_window *win_alloc(void)
{
    for (int i = 0; i < CE_MAX_WINDOWS; i++)
        if (!g_windows[i].used) {
            memset(&g_windows[i], 0, sizeof(ce_window));
            g_windows[i].used = 1;
            g_windows[i].enabled = 1;
            return &g_windows[i];
        }
    return NULL;
}

int gwe_win_w(ce_window *w) { return w->rect.right - w->rect.left; }
int gwe_win_h(ce_window *w) { return w->rect.bottom - w->rect.top; }

ce_window *gwe_top_of(ce_window *w)
{
    if (!w)
        return NULL;
    while (w->parent && !w->parent->is_desktop)
        w = w->parent;
    return w;
}

/* Client-area origin of w in its top-level window's client coords. */
CE_POINT gwe_client_origin(ce_window *w)
{
    CE_POINT p = { 0, 0 };
    while (w && w->parent && !w->parent->is_desktop) {
        p.x += w->rect.left;
        p.y += w->rect.top;
        w = w->parent;
    }
    return p;
}

CE_LONG gwe_call_proc(ce_window *w, CE_DWORD msg, CE_DWORD wp, CE_LONG lp)
{
    if (!w || !w->proc)
        return 0;
    return w->proc((CE_HANDLE)w, msg, wp, lp);
}

CE_LONG gwe_send_internal(ce_window *w, CE_DWORD msg, CE_DWORD wp, CE_LONG lp)
{
    return gwe_call_proc(w, msg, wp, lp);
}

/* ---------------- init / shutdown ---------------- */

void ce_gwe_init(void)
{
    if (g_gwe_ready)
        return;
    g_gwe_ready = 1;
    memset(&g_desktop, 0, sizeof(g_desktop));
    g_desktop.used = 1;
    g_desktop.is_desktop = 1;
    g_desktop.enabled = 1;
    g_desktop.visible = 1;
    g_desktop.rect.left = 0;
    g_desktop.rect.top = 0;
    g_desktop.rect.right = CE_SCREEN_W;
    g_desktop.rect.bottom = CE_SCREEN_H;
    g_desktop.proc = NULL; /* set below to DefWindowProc */
    extern CE_LONG ce_DefWindowProcW(CE_HANDLE, CE_DWORD, CE_DWORD, CE_LONG);
    g_desktop.proc = ce_DefWindowProcW;
}

void ce_gwe_shutdown(void)
{
    for (int i = 0; i < CE_MAX_WINDOWS; i++) {
        ce_window *w = &g_windows[i];
        if (w->used && !w->dead && !w->parent) {
            if (w->wm)
                bd_wm_client_close(w->wm);
            w->wm = NULL;
        }
    }
}

/* ---------------- window classes ---------------- */

static ce_wclass *find_class(const CE_WCHAR *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < CE_MAX_CLASSES; i++)
        if (g_classes[i].used && wcs_eq(g_classes[i].name, name))
            return &g_classes[i];
    return NULL;
}

uint16_t ce_RegisterClassW(const CE_WNDCLASSW *wc)
{
    if (!wc || !wc->lpszClassName || !wc->lpfnWndProc) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (find_class(wc->lpszClassName)) {
        ce_set_lasterror(CE_ERROR_CLASS_ALREADY_EXISTS);
        return 0;
    }
    for (int i = 0; i < CE_MAX_CLASSES; i++) {
        if (!g_classes[i].used) {
            ce_wclass *c = &g_classes[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            c->name = wcs_dup(wc->lpszClassName);
            c->style = wc->style;
            c->proc = wc->lpfnWndProc;
            c->cls_extra = wc->cbClsExtra;
            c->wnd_extra = wc->cbWndExtra;
            c->hinstance = wc->hInstance;
            c->hicon = wc->hIcon;
            c->hcursor = wc->hCursor;
            c->hbr_background = wc->hbrBackground;
            c->menu_name = wc->lpszMenuName;
            return (uint16_t)(i + 1); /* atom: pool index + 1 */
        }
    }
    ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
    return 0;
}

CE_BOOL ce_UnregisterClassW(const CE_WCHAR *name, CE_HANDLE hinst)
{
    (void)hinst;
    ce_wclass *c = find_class(name);
    if (!c) {
        ce_set_lasterror(CE_ERROR_CLASS_DOES_NOT_EXIST);
        return CE_FALSE;
    }
    if (c->win_count > 0) {
        ce_set_lasterror(CE_ERROR_CLASS_HAS_WINDOWS);
        return CE_FALSE;
    }
    free(c->name);
    memset(c, 0, sizeof(*c));
    return CE_TRUE;
}

CE_BOOL ce_GetClassInfoW(CE_HANDLE hinst, const CE_WCHAR *name,
                         CE_WNDCLASSW *out)
{
    (void)hinst;
    ce_wclass *c = find_class(name);
    if (!c || !out) {
        ce_set_lasterror(CE_ERROR_CLASS_DOES_NOT_EXIST);
        return CE_FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->style = c->style;
    out->lpfnWndProc = c->proc;
    out->cbClsExtra = c->cls_extra;
    out->cbWndExtra = c->wnd_extra;
    out->hInstance = c->hinstance;
    out->hIcon = c->hicon;
    out->hCursor = c->hcursor;
    out->hbrBackground = (CE_HANDLE)c->hbr_background;
    out->lpszMenuName = c->menu_name;
    out->lpszClassName = c->name;
    return CE_TRUE;
}

int ce_GetClassNameW(CE_HANDLE hwnd, CE_WCHAR *buf, int max)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !buf || max <= 0) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    static const CE_WCHAR empty_name[] = { 0 };
    const CE_WCHAR *name = w->is_desktop ? empty_name : w->cls->name;
    if (!name)
        name = empty_name;
    int n = 0;
    while (name[n] && n < max - 1) {
        buf[n] = name[n];
        n++;
    }
    buf[n] = 0;
    return n;
}

/* ---------------- WM connection / surface ---------------- */

static void win_surface_ensure(ce_window *w)
{
    int width = gwe_win_w(w), height = gwe_win_h(w);
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;
    if (w->shadow && w->surf_w == width && w->surf_h == height)
        return;
    uint32_t *px = calloc((size_t)width * height, sizeof(uint32_t));
    tk_canvas *cv = px ? tk_canvas_wrap(width, height, px) : NULL;
    if (w->shadow)
        tk_canvas_free(w->shadow);
    free(w->shadow_px);
    w->shadow_px = px;
    w->shadow = cv;
    w->surf_w = width;
    w->surf_h = height;
    /* new surface starts fully damaged (contents undefined) */
    w->ndmg = 0;
    CE_RECT all = { 0, 0, width, height };
    ce_rect_add(w->dmg, &w->ndmg, all);
}

/* wmclient.c calls bd_wm_default_path(), which lives in the WM
 * server TU (shell/src/wm.c). The layer links only the client, so
 * the identical path resolution is provided here: $BD_WM_SOCK,
 * else $XDG_RUNTIME_DIR/bdwm.sock, else /tmp/bdwm-<uid>.sock. */
const char *bd_wm_default_path(void)
{
    static char buf[108];
    const char *e = getenv("BD_WM_SOCK");
    if (e && e[0])
        return e;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && rt[0]) {
        snprintf(buf, sizeof(buf), "%s/bdwm.sock", rt);
        return buf;
    }
    snprintf(buf, sizeof(buf), "/tmp/bdwm-%u.sock", (unsigned)getuid());
    return buf;
}

static void win_wm_connect(ce_window *w)
{
    if (w->wm || w->is_desktop)
        return;
    char title[128] = "";
    if (w->text)
        ce_w2u8(w->text, title, sizeof(title));
    int width = gwe_win_w(w), height = gwe_win_h(w);
    w->wm = bd_wm_client_open(NULL, title[0] ? title : "BrainCE",
                              width, height);
    if (w->wm) {
        /* The server may grant a different size than requested. */
        int gw = bd_wm_client_width(w->wm);
        int gh = bd_wm_client_height(w->wm);
        if (gw > 0 && gh > 0 && (gw != width || gh != height)) {
            w->rect.right = w->rect.left + gw;
            w->rect.bottom = w->rect.top + gh;
        }
    }
    win_surface_ensure(w);
}

/* Flush the top-level window's accumulated damage to the WM shared
 * buffer. Damage-limited row copies shadow -> back buffer; the
 * buffer is committed with the same rect list. When both buffers
 * are with the server we keep the damage and retry at the next
 * flush point. */
void gwe_flush(ce_window *top)
{
    if (!top || top->is_desktop || !top->shadow)
        return;
    if (top->ndmg == 0)
        return;
    if (!top->wm) {
        top->ndmg = 0; /* headless: drawing went to the shadow only */
        return;
    }
    int stride = 0;
    uint32_t *dst = bd_wm_client_pixels(top->wm, &stride);
    if (!dst) {
        /* wait briefly for a RELEASE, consuming internal events */
        bd_wm_client_event ev;
        (void)bd_wm_client_poll(top->wm, &ev, 50);
        dst = bd_wm_client_pixels(top->wm, &stride);
        if (!dst)
            return; /* keep damage for the next flush */
    }
    int bw = bd_wm_client_width(top->wm);
    int bh = bd_wm_client_height(top->wm);
    tk_rect rects[CE_MAX_RECTS];
    int n = 0;
    for (int i = 0; i < top->ndmg; i++) {
        CE_RECT r = top->dmg[i];
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.right > top->surf_w) r.right = top->surf_w;
        if (r.bottom > top->surf_h) r.bottom = top->surf_h;
        if (r.right > bw) r.right = bw;
        if (r.bottom > bh) r.bottom = bh;
        if (ce_rect_empty(&r))
            continue;
        for (int y = r.top; y < r.bottom; y++)
            memcpy(dst + (size_t)y * stride + r.left,
                   top->shadow_px + (size_t)y * top->surf_w + r.left,
                   (size_t)(r.right - r.left) * sizeof(uint32_t));
        rects[n].x = r.left;
        rects[n].y = r.top;
        rects[n].w = r.right - r.left;
        rects[n].h = r.bottom - r.top;
        n++;
    }
    top->ndmg = 0;
    if (n > 0)
        (void)bd_wm_client_commit(top->wm, rects, n);
}

void gwe_add_damage(ce_window *top, CE_RECT r)
{
    if (!top || top->is_desktop)
        return;
    CE_RECT bounds = { 0, 0, top->surf_w, top->surf_h };
    r = ce_rect_intersect(r, bounds);
    ce_rect_add(top->dmg, &top->ndmg, r);
}

/* ---------------- invalidation ---------------- */

void gwe_invalidate(ce_window *w, const CE_RECT *r, int erase)
{
    CE_RECT client = { 0, 0, gwe_win_w(w), gwe_win_h(w) };
    CE_RECT add = r ? ce_rect_intersect(*r, client) : client;
    if (ce_rect_empty(&add))
        return;
    ce_rect_add(w->upd, &w->nupd, add);
    if (erase)
        w->upd_erase = 1;
}

void gwe_validate(ce_window *w, const CE_RECT *r)
{
    if (!r) {
        w->nupd = 0;
        w->upd_erase = 0;
        return;
    }
    /* subtract r from each update rect (rect subtraction, <=4 out) */
    CE_RECT out[CE_MAX_RECTS * 2];
    int nout = 0;
    for (int i = 0; i < w->nupd; i++) {
        CE_RECT a = w->upd[i];
        CE_RECT in = ce_rect_intersect(a, *r);
        if (ce_rect_empty(&in)) {
            if (nout < (int)(sizeof(out) / sizeof(out[0])))
                out[nout++] = a;
            continue;
        }
        CE_RECT parts[4];
        int np = 0;
        if (in.top > a.top)    parts[np++] = (CE_RECT){ a.left, a.top, a.right, in.top };
        if (in.bottom < a.bottom) parts[np++] = (CE_RECT){ a.left, in.bottom, a.right, a.bottom };
        if (in.left > a.left)  parts[np++] = (CE_RECT){ a.left, in.top, in.left, in.bottom };
        if (in.right < a.right) parts[np++] = (CE_RECT){ in.right, in.top, a.right, in.bottom };
        for (int k = 0; k < np; k++)
            if (nout < (int)(sizeof(out) / sizeof(out[0])))
                out[nout++] = parts[k];
    }
    w->nupd = 0;
    for (int i = 0; i < nout && i < CE_MAX_RECTS; i++)
        w->upd[w->nupd++] = out[i];
    if (w->nupd == 0)
        w->upd_erase = 0;
}

CE_BOOL ce_InvalidateRect(CE_HANDLE hwnd, const CE_RECT *r, CE_BOOL erase)
{
    if (!hwnd) { /* whole screen: all top-level windows */
        for (int i = 0; i < CE_MAX_WINDOWS; i++)
            if (g_windows[i].used && !g_windows[i].dead
                && !g_windows[i].parent)
                gwe_invalidate(&g_windows[i], r, erase);
        return CE_TRUE;
    }
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    gwe_invalidate(w, r, erase);
    return CE_TRUE;
}

CE_BOOL ce_InvalidateRgn(CE_HANDLE hwnd, CE_HANDLE hrgn, CE_BOOL erase)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    ce_gdobj *rgn = gdi_obj_from_handle(hrgn);
    if (!rgn || rgn->type != CE_OBJ_RGN) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    for (int i = 0; i < rgn->nrgn; i++)
        gwe_invalidate(w, &rgn->rgn[i], erase);
    return CE_TRUE;
}

CE_BOOL ce_ValidateRect(CE_HANDLE hwnd, const CE_RECT *r)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    gwe_validate(w, r);
    return CE_TRUE;
}

CE_BOOL ce_UpdateWindow(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    if (w->nupd > 0)
        gwe_call_proc(w, CE_WM_PAINT, 0, 0);
    return CE_TRUE;
}

/* ---------------- window creation / destruction ---------------- */

static void sibling_remove(ce_window *w)
{
    ce_window *p = w->parent;
    if (!p)
        return;
    ce_window **pp = &p->first_child;
    while (*pp && *pp != w)
        pp = &(*pp)->next_sibling;
    if (*pp)
        *pp = w->next_sibling;
    w->next_sibling = NULL;
}

static void sibling_push_top(ce_window *w)
{
    ce_window *p = w->parent;
    if (!p)
        return;
    sibling_remove(w);
    w->next_sibling = p->first_child;
    p->first_child = w;
}

CE_HANDLE ce_CreateWindowExW(CE_DWORD exstyle, const CE_WCHAR *classname,
                             const CE_WCHAR *title, CE_DWORD style,
                             int x, int y, int width, int height,
                             CE_HANDLE parent_h, CE_HANDLE menu,
                             CE_HANDLE hinstance, void *param)
{
    ce_wclass *cls = find_class(classname);
    if (!cls) {
        ce_set_lasterror(CE_ERROR_CLASS_DOES_NOT_EXIST);
        return NULL;
    }
    ce_window *parent = NULL;
    if (style & CE_WS_CHILD) {
        parent = gwe_win_from_handle(parent_h);
        if (!parent) {
            ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
            return NULL;
        }
    }
    ce_window *w = win_alloc();
    if (!w) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    w->cls = cls;
    w->proc = cls->proc;
    w->hinstance = hinstance;
    w->style = style;
    w->exstyle = exstyle;
    w->text = wcs_dup(title);
    w->menu_or_id = menu;
    w->create_param = param;
    w->enabled = !(style & CE_WS_DISABLED);
    if (cls->wnd_extra > 0)
        w->extra = calloc(1, (size_t)cls->wnd_extra);
    cls->win_count++;

    if (x == CE_CW_USEDEFAULT) x = 0;
    if (y == CE_CW_USEDEFAULT) y = 0;
    if (width == CE_CW_USEDEFAULT || width <= 0) width = 320;
    if (height == CE_CW_USEDEFAULT || height <= 0) height = 240;
    w->rect.left = x;
    w->rect.top = y;
    w->rect.right = x + width;
    w->rect.bottom = y + height;
    w->parent = parent;
    if (parent)
        sibling_push_top(w);

    if (!parent)
        win_wm_connect(w);
    else
        win_surface_ensure(gwe_top_of(w));

    /* creation messages, desktop order */
    CE_CREATESTRUCTW cs;
    memset(&cs, 0, sizeof(cs));
    cs.lpCreateParams = param;
    cs.hInstance = hinstance;
    cs.hMenu = menu;
    cs.hwndParent = parent_h;
    cs.cy = height;
    cs.cx = width;
    cs.y = y;
    cs.x = x;
    cs.style = (CE_LONG)style;
    cs.lpszName = title;
    cs.lpszClass = classname;
    cs.dwExStyle = exstyle;
    if (gwe_call_proc(w, CE_WM_NCCREATE, 0, (CE_LONG)&cs) == 0) {
        sibling_remove(w);
        cls->win_count--;
        free(w->text);
        free(w->extra);
        memset(w, 0, sizeof(*w));
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (gwe_call_proc(w, CE_WM_CREATE, 0, (CE_LONG)&cs) == -1) {
        /* creation aborted by the window procedure */
        gwe_call_proc(w, CE_WM_NCDESTROY, 0, 0);
        sibling_remove(w);
        cls->win_count--;
        free(w->text);
        free(w->extra);
        memset(w, 0, sizeof(*w));
        return NULL;
    }
    if (style & CE_WS_VISIBLE) {
        w->visible = 1;
        gwe_invalidate(w, NULL, 1);
    }
    if (!parent && !g_active_win) {
        g_active_win = (CE_HANDLE)w;
        if (!g_focus_win)
            g_focus_win = (CE_HANDLE)w;
    }
    return (CE_HANDLE)w;
}

static void destroy_children(ce_window *w)
{
    while (w->first_child) {
        ce_window *c = w->first_child;
        ce_DestroyWindow((CE_HANDLE)c);
    }
}

CE_BOOL ce_DestroyWindow(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || w->is_desktop) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    gwe_call_proc(w, CE_WM_DESTROY, 0, 0);
    destroy_children(w);
    gwe_call_proc(w, CE_WM_NCDESTROY, 0, 0);
    /* timers of this window die with it */
    extern void gwe_kill_timers_of(ce_window *);
    gwe_kill_timers_of(w);
    if (g_focus_win == hwnd) g_focus_win = NULL;
    if (g_active_win == hwnd) g_active_win = NULL;
    if (g_capture_win == hwnd) g_capture_win = NULL;
    sibling_remove(w);
    if (!w->parent && w->wm) {
        bd_wm_client_close(w->wm);
        w->wm = NULL;
    }
    if (!w->parent) {
        if (w->shadow)
            tk_canvas_free(w->shadow);
        free(w->shadow_px);
        w->shadow = NULL;
        w->shadow_px = NULL;
    }
    if (w->cls)
        w->cls->win_count--;
    free(w->text);
    free(w->extra);
    w->text = NULL;
    w->extra = NULL;
    w->dead = 1; /* stays in the pool: stale handles fail cleanly */
    return CE_TRUE;
}

CE_BOOL ce_IsWindow(CE_HANDLE hwnd)
{
    if (hwnd == (CE_HANDLE)&g_desktop)
        return CE_TRUE;
    return gwe_win_from_handle(hwnd) ? CE_TRUE : CE_FALSE;
}

CE_BOOL ce_IsWindowVisible(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return CE_FALSE;
    for (; w && !w->is_desktop; w = w->parent)
        if (!w->visible)
            return CE_FALSE;
    return CE_TRUE;
}

CE_BOOL ce_IsWindowEnabled(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    return w ? (w->enabled ? CE_TRUE : CE_FALSE) : CE_FALSE;
}

CE_BOOL ce_EnableWindow(CE_HANDLE hwnd, CE_BOOL enable)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    CE_BOOL was = w->enabled ? CE_FALSE : CE_TRUE; /* returns TRUE if previously disabled */
    if (w->enabled != (enable != 0)) {
        w->enabled = enable != 0;
        gwe_call_proc(w, CE_WM_ENABLE, enable ? 1 : 0, 0);
    }
    return was;
}

CE_HANDLE ce_GetParent(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !w->parent || w->parent->is_desktop)
        return NULL;
    return (CE_HANDLE)w->parent;
}

CE_HANDLE ce_GetDesktopWindow(void)
{
    return (CE_HANDLE)&g_desktop;
}

CE_BOOL ce_GetClientRect(CE_HANDLE hwnd, CE_RECT *r)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !r) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    r->left = 0;
    r->top = 0;
    r->right = gwe_win_w(w);
    r->bottom = gwe_win_h(w);
    return CE_TRUE;
}

CE_BOOL ce_GetWindowRect(CE_HANDLE hwnd, CE_RECT *r)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !r) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    if (!w->parent || w->parent->is_desktop) {
        *r = w->rect;
        return CE_TRUE;
    }
    CE_POINT o = gwe_client_origin(w);
    ce_window *top = gwe_top_of(w);
    r->left = top->rect.left + o.x;
    r->top = top->rect.top + o.y;
    r->right = r->left + gwe_win_w(w);
    r->bottom = r->top + gwe_win_h(w);
    return CE_TRUE;
}

CE_BOOL ce_ShowWindow(CE_HANDLE hwnd, int cmd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    CE_BOOL was = w->visible ? CE_TRUE : CE_FALSE;
    int show;
    switch (cmd) {
    case CE_SW_HIDE: show = 0; break;
    case CE_SW_SHOW: case CE_SW_SHOWNORMAL: case CE_SW_RESTORE:
    case CE_SW_SHOWNA: case CE_SW_SHOWNOACTIVATE:
        show = 1; break;
    case CE_SW_MINIMIZE: case CE_SW_SHOWMINIMIZED:
    case CE_SW_SHOWMINNOACTIVE:
        show = 0; break; /* no client-side minimize in the protocol */
    case CE_SW_SHOWMAXIMIZED:
        show = 1; break;
    default: show = w->visible; break;
    }
    if (show != w->visible) {
        w->visible = show;
        gwe_call_proc(w, CE_WM_SHOWWINDOW, show ? 1 : 0, 0);
        if (show) {
            if (!w->parent)
                win_wm_connect(w);
            gwe_invalidate(w, NULL, 1);
        }
    }
    return was;
}

CE_BOOL ce_MoveWindow(CE_HANDLE hwnd, int x, int y, int width, int height,
                      CE_BOOL repaint)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    int oldw = gwe_win_w(w), oldh = gwe_win_h(w);
    int oldx = w->rect.left, oldy = w->rect.top;
    w->rect.left = x;
    w->rect.top = y;
    w->rect.right = x + width;
    w->rect.bottom = y + height;
    if (!w->parent)
        win_surface_ensure(w);
    /* WM_SIZE exactly once when the size changed, WM_MOVE exactly
     * once when the position changed (CE notification contract). */
    if (width != oldw || height != oldh)
        gwe_call_proc(w, CE_WM_SIZE, 0,
                      (CE_LONG)((height << 16) | (width & 0xFFFF)));
    if (x != oldx || y != oldy)
        gwe_call_proc(w, CE_WM_MOVE, 0,
                      (CE_LONG)((y << 16) | (x & 0xFFFF)));
    if (repaint)
        gwe_invalidate(w, NULL, 0);
    return CE_TRUE;
}

CE_BOOL ce_SetWindowPos(CE_HANDLE hwnd, CE_HANDLE after, int x, int y,
                        int cx, int cy, CE_DWORD flags)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    if (!(flags & CE_SWP_NOMOVE) || !(flags & CE_SWP_NOSIZE)) {
        int nx = (flags & CE_SWP_NOMOVE) ? w->rect.left : x;
        int ny = (flags & CE_SWP_NOMOVE) ? w->rect.top : y;
        int nw = (flags & CE_SWP_NOSIZE) ? gwe_win_w(w) : cx;
        int nh = (flags & CE_SWP_NOSIZE) ? gwe_win_h(w) : cy;
        ce_MoveWindow(hwnd, nx, ny, nw, nh, CE_TRUE);
    }
    if (!(flags & CE_SWP_NOZORDER) && w->parent) {
        if (after == CE_HWND_TOP)
            sibling_push_top(w);
        else if (after == CE_HWND_BOTTOM) {
            sibling_remove(w);
            ce_window **pp = &w->parent->first_child;
            while (*pp)
                pp = &(*pp)->next_sibling;
            *pp = w;
            w->next_sibling = NULL;
        } else {
            ce_window *a = gwe_win_from_handle(after);
            if (a && a->parent == w->parent && a != w) {
                sibling_remove(w);
                w->next_sibling = a->next_sibling;
                a->next_sibling = w;
            }
        }
    }
    if (flags & CE_SWP_SHOWWINDOW)
        ce_ShowWindow(hwnd, CE_SW_SHOW);
    if (flags & CE_SWP_HIDEWINDOW)
        ce_ShowWindow(hwnd, CE_SW_HIDE);
    if (!(flags & CE_SWP_NOACTIVATE) && !w->parent)
        g_active_win = hwnd;
    return CE_TRUE;
}

CE_BOOL ce_BringWindowToTop(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    if (w->parent)
        sibling_push_top(w);
    else
        g_active_win = hwnd;
    return CE_TRUE;
}

CE_HANDLE ce_GetWindow(CE_HANDLE hwnd, CE_DWORD cmd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return NULL;
    switch (cmd) {
    case CE_GW_CHILD:
        return w->first_child ? (CE_HANDLE)w->first_child : NULL;
    case CE_GW_HWNDNEXT:
        return w->next_sibling ? (CE_HANDLE)w->next_sibling : NULL;
    case CE_GW_HWNDPREV: {
        if (!w->parent)
            return NULL;
        ce_window *p = w->parent->first_child, *prev = NULL;
        while (p && p != w) {
            prev = p;
            p = p->next_sibling;
        }
        return prev ? (CE_HANDLE)prev : NULL;
    }
    case CE_GW_HWNDFIRST:
        if (!w->parent)
            return NULL;
        return (CE_HANDLE)w->parent->first_child;
    case CE_GW_HWNDLAST: {
        if (!w->parent)
            return NULL;
        ce_window *p = w->parent->first_child;
        if (!p)
            return NULL;
        while (p->next_sibling)
            p = p->next_sibling;
        return (CE_HANDLE)p;
    }
    default:
        return NULL; /* GW_OWNER: no owned windows in M2 */
    }
}

typedef CE_BOOL (*ce_enum_proc)(CE_HANDLE, CE_LONG);
CE_BOOL ce_EnumWindows(ce_enum_proc cb, CE_LONG param)
{
    if (!cb)
        return CE_FALSE;
    for (int i = 0; i < CE_MAX_WINDOWS; i++) {
        ce_window *w = &g_windows[i];
        if (w->used && !w->dead && !w->parent)
            if (!cb((CE_HANDLE)w, param))
                break;
    }
    return CE_TRUE;
}

CE_HANDLE ce_FindWindowW(const CE_WCHAR *classname, const CE_WCHAR *title)
{
    for (int i = 0; i < CE_MAX_WINDOWS; i++) {
        ce_window *w = &g_windows[i];
        if (!w->used || w->dead || w->parent)
            continue;
        if (classname && !wcs_eq(w->cls->name, classname))
            continue;
        if (title && !wcs_eq(w->text, title))
            continue;
        return (CE_HANDLE)w;
    }
    return NULL;
}

/* ---------------- window text / longs ---------------- */

CE_BOOL ce_SetWindowTextW(CE_HANDLE hwnd, const CE_WCHAR *text)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    gwe_call_proc(w, CE_WM_SETTEXT, 0, (CE_LONG)text);
    return CE_TRUE;
}

int ce_GetWindowTextW(CE_HANDLE hwnd, CE_WCHAR *buf, int max)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !buf || max <= 0)
        return 0;
    return (int)gwe_call_proc(w, CE_WM_GETTEXT, (CE_DWORD)max, (CE_LONG)buf);
}

int ce_GetWindowTextLengthW(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return 0;
    return (int)gwe_call_proc(w, CE_WM_GETTEXTLENGTH, 0, 0);
}

CE_LONG ce_GetWindowLongW(CE_HANDLE hwnd, int idx)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    switch (idx) {
    case CE_GWL_WNDPROC:   return (CE_LONG)w->proc;
    case CE_GWL_HINSTANCE: return (CE_LONG)w->hinstance;
    case CE_GWL_HWNDPARENT:
        return (CE_LONG)(w->parent && !w->parent->is_desktop
                         ? (CE_HANDLE)w->parent : NULL);
    case CE_GWL_ID:        return (CE_LONG)w->menu_or_id;
    case CE_GWL_STYLE:     return (CE_LONG)w->style;
    case CE_GWL_EXSTYLE:   return (CE_LONG)w->exstyle;
    case CE_GWL_USERDATA:  return (CE_LONG)w->userdata;
    default:
        if (idx >= 0 && w->extra && w->cls
            && idx + 4 <= w->cls->wnd_extra) {
            CE_LONG v;
            memcpy(&v, w->extra + idx, sizeof(v));
            return v;
        }
        ce_set_lasterror(CE_ERROR_INVALID_INDEX);
        return 0;
    }
}

CE_LONG ce_SetWindowLongW(CE_HANDLE hwnd, int idx, CE_LONG value)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    CE_LONG old;
    switch (idx) {
    case CE_GWL_WNDPROC:
        old = (CE_LONG)w->proc;
        w->proc = (CE_WNDPROC)value;
        return old;
    case CE_GWL_HINSTANCE:
        old = (CE_LONG)w->hinstance;
        w->hinstance = (CE_HANDLE)value;
        return old;
    case CE_GWL_ID:
        old = (CE_LONG)w->menu_or_id;
        w->menu_or_id = (CE_HANDLE)value;
        return old;
    case CE_GWL_STYLE:
        old = (CE_LONG)w->style;
        w->style = (CE_DWORD)value;
        w->enabled = !(w->style & CE_WS_DISABLED);
        w->visible = (w->style & CE_WS_VISIBLE) != 0;
        return old;
    case CE_GWL_EXSTYLE:
        old = (CE_LONG)w->exstyle;
        w->exstyle = (CE_DWORD)value;
        return old;
    case CE_GWL_USERDATA:
        old = (CE_LONG)w->userdata;
        w->userdata = (void *)value;
        return old;
    default:
        if (idx >= 0 && w->extra && w->cls
            && idx + 4 <= w->cls->wnd_extra) {
            memcpy(&old, w->extra + idx, sizeof(old));
            memcpy(w->extra + idx, &value, sizeof(value));
            return old;
        }
        ce_set_lasterror(CE_ERROR_INVALID_INDEX);
        return 0;
    }
}

int ce_GetDlgCtrlID(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    return w ? (int)(intptr_t)w->menu_or_id : 0;
}

/* ---------------- focus / active / capture ---------------- */

void gwe_set_focus(ce_window *w)
{
    CE_HANDLE old_h = g_focus_win;
    if (old_h == (CE_HANDLE)w)
        return;
    ce_window *old = gwe_win_from_handle(old_h);
    g_focus_win = w ? (CE_HANDLE)w : NULL;
    if (old)
        gwe_call_proc(old, CE_WM_KILLFOCUS, (CE_DWORD)g_focus_win, 0);
    if (w) {
        ce_window *top = gwe_top_of(w);
        if (top)
            g_active_win = (CE_HANDLE)top;
        gwe_call_proc(w, CE_WM_SETFOCUS, (CE_DWORD)old_h, 0);
    }
}

CE_HANDLE ce_SetFocus(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w && hwnd) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    CE_HANDLE old = g_focus_win;
    gwe_set_focus(w);
    return old;
}

CE_HANDLE ce_GetFocus(void) { return g_focus_win; }

CE_HANDLE ce_SetActiveWindow(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    CE_HANDLE old = g_active_win;
    g_active_win = (CE_HANDLE)gwe_top_of(w);
    return old;
}

CE_HANDLE ce_GetActiveWindow(void) { return g_active_win; }

CE_BOOL ce_SetForegroundWindow(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    g_active_win = (CE_HANDLE)gwe_top_of(w);
    return CE_TRUE;
}

CE_HANDLE ce_GetForegroundWindow(void) { return g_active_win; }

CE_HANDLE ce_SetCapture(CE_HANDLE hwnd)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    CE_HANDLE old = g_capture_win;
    g_capture_win = hwnd;
    return old;
}

CE_BOOL ce_ReleaseCapture(void)
{
    ce_window *old = gwe_win_from_handle(g_capture_win);
    g_capture_win = NULL;
    if (old)
        gwe_call_proc(old, CE_WM_CAPTURECHANGED, 0, 0);
    return CE_TRUE;
}

CE_HANDLE ce_GetCapture(void) { return g_capture_win; }

CE_BOOL ce_GetCursorPos(CE_POINT *p)
{
    if (!p)
        return CE_FALSE;
    *p = g_cursor_pt;
    return CE_TRUE;
}

/* ---------------- coordinate mapping ---------------- */

CE_BOOL ce_ClientToScreen(CE_HANDLE hwnd, CE_POINT *p)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !p) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    CE_POINT o = gwe_client_origin(w);
    ce_window *top = gwe_top_of(w);
    p->x += top->rect.left + o.x;
    p->y += top->rect.top + o.y;
    return CE_TRUE;
}

CE_BOOL ce_ScreenToClient(CE_HANDLE hwnd, CE_POINT *p)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w || !p) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    CE_POINT o = gwe_client_origin(w);
    ce_window *top = gwe_top_of(w);
    p->x -= top->rect.left + o.x;
    p->y -= top->rect.top + o.y;
    return CE_TRUE;
}

int ce_MapWindowPoints(CE_HANDLE from_h, CE_HANDLE to_h, CE_POINT *pts,
                       unsigned n)
{
    CE_POINT fo = { 0, 0 }, to = { 0, 0 };
    ce_window *f = gwe_win_from_handle(from_h);
    ce_window *t = gwe_win_from_handle(to_h);
    if (f && !f->is_desktop) {
        CE_POINT o = gwe_client_origin(f);
        ce_window *top = gwe_top_of(f);
        fo.x = top->rect.left + o.x;
        fo.y = top->rect.top + o.y;
    }
    if (t && !t->is_desktop) {
        CE_POINT o = gwe_client_origin(t);
        ce_window *top = gwe_top_of(t);
        to.x = top->rect.left + o.x;
        to.y = top->rect.top + o.y;
    }
    int dx = fo.x - to.x, dy = fo.y - to.y;
    for (unsigned i = 0; i < n; i++) {
        pts[i].x += dx;
        pts[i].y += dy;
    }
    return (dy << 16) | (dx & 0xFFFF);
}

CE_BOOL ce_AdjustWindowRectEx(CE_RECT *r, CE_DWORD style, CE_BOOL menu,
                              CE_DWORD exstyle)
{
    /* In this layer the WM server owns all chrome outside the
     * content area, and the CE window rect IS the content rect, so
     * no adjustment applies. */
    (void)r; (void)style; (void)menu; (void)exstyle;
    return CE_TRUE;
}

/* ---------------- metrics / colors ---------------- */

int ce_GetSystemMetrics(int idx)
{
    switch (idx) {
    case CE_SM_CXSCREEN: return CE_SCREEN_W;
    case CE_SM_CYSCREEN: return CE_SCREEN_H;
    case CE_SM_CXVSCROLL: return 16;
    case CE_SM_CYHSCROLL: return 16;
    case CE_SM_CYCAPTION: return 19;
    case CE_SM_CXBORDER: return 1;
    case CE_SM_CYBORDER: return 1;
    case CE_SM_CXDLGFRAME: return 3;
    case CE_SM_CYDLGFRAME: return 3;
    case CE_SM_CXFULLSCREEN: return CE_SCREEN_W;
    case CE_SM_CYFULLSCREEN: return 448;
    case CE_SM_CXFRAME: return 3;
    case CE_SM_CYFRAME: return 3;
    default: return 0;
    }
}

CE_DWORD gwe_sys_color(int idx)
{
    /* COLORREF values approximating the CE/Windows classic scheme */
    switch (idx) {
    case CE_COLOR_SCROLLBAR: return 0x00D4D0C8;
    case CE_COLOR_BACKGROUND: return 0x00004080;
    case CE_COLOR_ACTIVECAPTION: return 0x00A6640A;
    case CE_COLOR_INACTIVECAPTION: return 0x00CFC7B0;
    case CE_COLOR_MENU: return 0x00F0F0F0;
    case CE_COLOR_WINDOW: return 0x00FFFFFF;
    case CE_COLOR_WINDOWFRAME: return 0x00000000;
    case CE_COLOR_MENUTEXT: return 0x00000000;
    case CE_COLOR_WINDOWTEXT: return 0x00000000;
    case CE_COLOR_CAPTIONTEXT: return 0x00FFFFFF;
    case CE_COLOR_HIGHLIGHT: return 0x00C56A31;
    case CE_COLOR_HIGHLIGHTTEXT: return 0x00FFFFFF;
    case CE_COLOR_BTNFACE: return 0x00F0F0F0;
    case CE_COLOR_BTNSHADOW: return 0x00A09C98;
    case CE_COLOR_GRAYTEXT: return 0x006C6C6C;
    case CE_COLOR_BTNTEXT: return 0x00000000;
    case CE_COLOR_BTNHIGHLIGHT: return 0x00FFFFFF;
    case CE_COLOR_3DDKSHADOW: return 0x00646464;
    case CE_COLOR_3DLIGHT: return 0x00E3E3E3;
    case CE_COLOR_INFOTEXT: return 0x00000000;
    case CE_COLOR_INFOBK: return 0x00E1FFFF;
    default: return 0;
    }
}

CE_DWORD ce_GetSysColor(int idx) { return gwe_sys_color(idx); }

CE_BOOL ce_SystemParametersInfoW(CE_DWORD action, CE_DWORD param,
                                 void *out, CE_DWORD winini)
{
    (void)param; (void)winini;
    if (action == CE_SPI_GETWORKAREA && out) {
        CE_RECT *r = out;
        r->left = 0;
        r->top = 0;
        r->right = CE_SCREEN_W;
        r->bottom = 448;
        return CE_TRUE;
    }
    ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
    return CE_FALSE;
}

CE_BOOL ce_MessageBeep(CE_DWORD type)
{
    (void)type; /* no audio side channel in M2; call succeeds */
    return CE_TRUE;
}

/* ---------------- message queue ---------------- */

typedef struct {
    CE_MSG msg;
    uint16_t pend_char; /* WM_CHAR code TranslateMessage should post */
} ce_qent;

static ce_qent g_queue[CE_MAX_QUEUE];
static int g_qhead, g_qtail;
static int g_quit_posted;
static int g_quit_code;
static __thread uint16_t g_last_pend_char;
static __thread CE_HANDLE g_last_pend_hwnd;
static CE_POINT g_last_ptr_pt;

static int queue_len(void) { return g_qtail - g_qhead; }

int gwe_enqueue(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp, CE_LONG lp)
{
    return gwe_enqueue_ch(hwnd, msg, wp, lp, 0);
}

int gwe_enqueue_ch(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp, CE_LONG lp,
                   uint16_t pend_char)
{
    pthread_mutex_lock(&g_lock);
    if (queue_len() >= CE_MAX_QUEUE) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    ce_qent *e = &g_queue[g_qtail % CE_MAX_QUEUE];
    e->msg.hwnd = hwnd;
    e->msg.message = msg;
    e->msg.wParam = wp;
    e->msg.lParam = lp;
    e->msg.time = ce_GetTickCount();
    e->msg.pt = g_cursor_pt;
    e->pend_char = pend_char;
    g_qtail++;
    pthread_mutex_unlock(&g_lock);
    return 1;
}

static int msg_matches(const CE_MSG *m, CE_HANDLE filter, CE_DWORD mn,
                       CE_DWORD mx)
{
    if (filter && m->hwnd != filter)
        return 0;
    if ((mn || mx) && (m->message < mn || m->message > mx))
        return 0;
    return 1;
}

/* Pop the first matching queued message. 1 = popped. */
static int queue_pop_match(CE_MSG *out, uint16_t *pend, CE_HANDLE filter,
                           CE_DWORD mn, CE_DWORD mx)
{
    pthread_mutex_lock(&g_lock);
    for (int i = g_qhead; i < g_qtail; i++) {
        ce_qent *e = &g_queue[i % CE_MAX_QUEUE];
        if (!msg_matches(&e->msg, filter, mn, mx))
            continue;
        *out = e->msg;
        *pend = e->pend_char;
        for (int k = i; k < g_qtail - 1; k++)
            g_queue[k % CE_MAX_QUEUE] = g_queue[(k + 1) % CE_MAX_QUEUE];
        g_qtail--;
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

CE_BOOL ce_PostMessageW(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp, CE_LONG lp)
{
    if (hwnd && !gwe_win_from_handle(hwnd) && hwnd != (CE_HANDLE)&g_desktop) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return CE_FALSE;
    }
    return gwe_enqueue(hwnd, msg, wp, lp) ? CE_TRUE : CE_FALSE;
}

void ce_PostQuitMessage(int code)
{
    pthread_mutex_lock(&g_lock);
    g_quit_posted = 1;
    g_quit_code = code;
    pthread_mutex_unlock(&g_lock);
}

CE_LONG ce_SendMessageW(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp, CE_LONG lp)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    return gwe_call_proc(w, msg, wp, lp);
}

CE_LONG ce_CallWindowProcW(CE_WNDPROC prev, CE_HANDLE hwnd, CE_DWORD msg,
                           CE_DWORD wp, CE_LONG lp)
{
    if (!prev) {
        ce_window *w = gwe_win_from_handle(hwnd);
        if (w)
            return ce_DefWindowProcW(hwnd, msg, wp, lp);
        return 0;
    }
    return prev(hwnd, msg, wp, lp);
}

CE_DWORD ce_GetMessagePos(void)
{
    return (CE_DWORD)((g_last_ptr_pt.y << 16) | (g_last_ptr_pt.x & 0xFFFF));
}

/* ---------------- timers ---------------- */

typedef void (*CE_TIMERPROC)(CE_HANDLE, CE_DWORD, CE_DWORD, CE_DWORD);
typedef struct {
    int used;
    ce_window *win;
    CE_DWORD id;
    CE_DWORD elapse;
    uint64_t due; /* tick ms */
    CE_TIMERPROC proc;
} ce_timer;

static ce_timer g_timers[CE_MAX_TIMERS];

void gwe_kill_timers_of(ce_window *w)
{
    for (int i = 0; i < CE_MAX_TIMERS; i++)
        if (g_timers[i].used && g_timers[i].win == w)
            g_timers[i].used = 0;
}

CE_DWORD ce_SetTimer(CE_HANDLE hwnd, CE_DWORD id, CE_DWORD elapse,
                     void *proc)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w) {
        ce_set_lasterror(CE_ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    if (elapse < 1)
        elapse = 1;
    for (int i = 0; i < CE_MAX_TIMERS; i++)
        if (g_timers[i].used && g_timers[i].win == w
            && g_timers[i].id == id) {
            g_timers[i].elapse = elapse;
            g_timers[i].due = (uint64_t)ce_GetTickCount() + elapse;
            g_timers[i].proc = (CE_TIMERPROC)proc;
            return id;
        }
    for (int i = 0; i < CE_MAX_TIMERS; i++)
        if (!g_timers[i].used) {
            g_timers[i].used = 1;
            g_timers[i].win = w;
            g_timers[i].id = id;
            g_timers[i].elapse = elapse;
            g_timers[i].due = (uint64_t)ce_GetTickCount() + elapse;
            g_timers[i].proc = (CE_TIMERPROC)proc;
            return id;
        }
    return 0;
}

CE_BOOL ce_KillTimer(CE_HANDLE hwnd, CE_DWORD id)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return CE_FALSE;
    for (int i = 0; i < CE_MAX_TIMERS; i++)
        if (g_timers[i].used && g_timers[i].win == w
            && g_timers[i].id == id) {
            g_timers[i].used = 0;
            return CE_TRUE;
        }
    return CE_FALSE;
}

static uint64_t next_timer_due(void)
{
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < CE_MAX_TIMERS; i++)
        if (g_timers[i].used && g_timers[i].due < best)
            best = g_timers[i].due;
    return best;
}

/* ---------------- WM event pump ---------------- */

static int vk_from_tk(int key)
{
    switch (key) {
    case TK_KEY_TAB: return CE_VK_TAB;
    case TK_KEY_ENTER: return CE_VK_RETURN;
    case TK_KEY_ESCAPE: return CE_VK_ESCAPE;
    case TK_KEY_BACKSPACE: return CE_VK_BACK;
    case TK_KEY_DELETE: return CE_VK_DELETE;
    case TK_KEY_LEFT: return CE_VK_LEFT;
    case TK_KEY_RIGHT: return CE_VK_RIGHT;
    case TK_KEY_UP: return CE_VK_UP;
    case TK_KEY_DOWN: return CE_VK_DOWN;
    case TK_KEY_HOME: return CE_VK_HOME;
    case TK_KEY_END: return CE_VK_END;
    case TK_KEY_PAGE_UP: return CE_VK_PRIOR;
    case TK_KEY_PAGE_DOWN: return CE_VK_NEXT;
    case TK_KEY_SPACE: return 0x20;
    case TK_KEY_INSERT: return CE_VK_INSERT;
    case TK_KEY_BACK: return 0xA6; /* VK_BROWSER_BACK, Brain back key */
    default: return 0;
    }
}

/* first codepoint of a UTF-8 string as UTF-16 (BMP only) */
static uint16_t utf8_first_cp16(const char *s)
{
    if (!s || !*s)
        return 0;
    const unsigned char *u = (const unsigned char *)s;
    uint32_t cp;
    if (u[0] < 0x80) cp = u[0];
    else if ((u[0] & 0xE0) == 0xC0) cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    else if ((u[0] & 0xF0) == 0xE0)
        cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    else cp = 0xFFFD;
    if (cp > 0xFFFF)
        cp = 0xFFFD;
    return (uint16_t)cp;
}

static ce_window *hit_test(ce_window *top, int x, int y, CE_POINT *local)
{
    /* children first (topmost first), recursively */
    for (ce_window *c = top->first_child; c; c = c->next_sibling) {
        if (!c->visible || !c->enabled)
            continue;
        if (x >= c->rect.left && x < c->rect.right
            && y >= c->rect.top && y < c->rect.bottom) {
            CE_POINT sub = { x - c->rect.left, y - c->rect.top };
            ce_window *deep = hit_test(c, sub.x, sub.y, &sub);
            *local = sub;
            return deep;
        }
    }
    local->x = x;
    local->y = y;
    return top;
}

static void pump_one_client(ce_window *top, bd_wm_client_event *ev)
{
    ce_window *target;
    CE_POINT local;
    CE_LONG lp;
    switch (ev->type) {
    case BD_WM_CEV_RESIZED: {
        int nw = ev->w, nh = ev->h;
        if (nw > 0 && nh > 0 && (nw != gwe_win_w(top) || nh != gwe_win_h(top))) {
            top->rect.right = top->rect.left + nw;
            top->rect.bottom = top->rect.top + nh;
            win_surface_ensure(top);
            gwe_invalidate(top, NULL, 1);
            gwe_enqueue((CE_HANDLE)top, CE_WM_SIZE, 0,
                        (CE_LONG)((nh << 16) | (nw & 0xFFFF)));
        }
        break;
    }
    case BD_WM_CEV_CLOSE:
        gwe_enqueue((CE_HANDLE)top, CE_WM_CLOSE, 0, 0);
        break;
    case BD_WM_CEV_FOCUS_IN:
        if (!g_focus_win)
            gwe_set_focus(top);
        break;
    case BD_WM_CEV_FOCUS_OUT:
        break; /* focus is tracked by clicks/keys in this layer */
    case BD_WM_CEV_POINTER_DOWN:
    case BD_WM_CEV_POINTER_UP:
    case BD_WM_CEV_POINTER_MOVE: {
        g_cursor_pt.x = top->rect.left + ev->x;
        g_cursor_pt.y = top->rect.top + ev->y;
        g_last_ptr_pt = g_cursor_pt;
        if (g_capture_win) {
            target = gwe_win_from_handle(g_capture_win);
            if (!target)
                target = top;
            CE_POINT o = gwe_client_origin(target);
            local.x = ev->x - o.x;
            local.y = ev->y - o.y;
        } else {
            target = hit_test(top, ev->x, ev->y, &local);
        }
        lp = (CE_LONG)((local.y << 16) | (local.x & 0xFFFF));
        if (ev->type == BD_WM_CEV_POINTER_DOWN) {
            g_button_down = 1;
            if (gwe_win_from_handle(g_focus_win) != target)
                gwe_set_focus(target);
            g_capture_win = (CE_HANDLE)target;
            gwe_enqueue((CE_HANDLE)target, CE_WM_LBUTTONDOWN,
                        CE_MK_LBUTTON, lp);
        } else if (ev->type == BD_WM_CEV_POINTER_UP) {
            g_button_down = 0;
            gwe_enqueue((CE_HANDLE)target, CE_WM_LBUTTONUP, 0, lp);
            if (g_capture_win == (CE_HANDLE)target)
                g_capture_win = NULL;
        } else {
            gwe_enqueue((CE_HANDLE)target, CE_WM_MOUSEMOVE,
                        g_button_down ? CE_MK_LBUTTON : 0, lp);
        }
        break;
    }
    case BD_WM_CEV_KEY_DOWN:
    case BD_WM_CEV_KEY_UP: {
        target = gwe_win_from_handle(g_focus_win);
        if (!target)
            target = top;
        int vk = vk_from_tk(ev->key);
        uint16_t ch = 0;
        if (ev->type == BD_WM_CEV_KEY_DOWN) {
            if (ev->text[0])
                ch = utf8_first_cp16(ev->text);
            if (!vk && ch)
                vk = (ch >= 'a' && ch <= 'z') ? (ch - 'a' + 'A')
                     : (ch >= 'A' && ch <= 'Z') ? ch
                     : (ch >= '0' && ch <= '9') ? ch : 0;
        }
        CE_DWORD msg = (ev->type == BD_WM_CEV_KEY_DOWN)
                       ? CE_WM_KEYDOWN : CE_WM_KEYUP;
        CE_LONG keylp = 1; /* repeat count 1, scan code 0 */
        if (ev->type == BD_WM_CEV_KEY_UP)
            keylp |= (CE_LONG)(3u << 30); /* previous state + transition */
        gwe_enqueue_ch((CE_HANDLE)target, msg, (CE_DWORD)vk, keylp, ch);
        break;
    }
    default:
        break; /* COMPOSITION: IME preedit is not mapped in M2 */
    }
}

/* Drain WM client events, waiting up to timeout_ms in total. */
void gwe_pump_wm(int timeout_ms)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        int progress = 0;
        for (int i = 0; i < CE_MAX_WINDOWS; i++) {
            ce_window *w = &g_windows[i];
            if (!w->used || w->dead || w->parent || !w->wm)
                continue;
            if (bd_wm_client_dead(w->wm))
                continue;
            bd_wm_client_event ev;
            int r = bd_wm_client_poll(w->wm, &ev, 0);
            if (r == 1) {
                pump_one_client(w, &ev);
                progress = 1;
            } else if (r == 0) {
                progress = 1; /* internal traffic (RELEASE etc.) */
            }
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000
                       + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= timeout_ms)
            return;
        if (!progress) {
            /* block briefly on the first live client to avoid a
             * busy loop; events on other clients are picked up in
             * the next slice (<= remaining time) */
            int waited = 0;
            for (int i = 0; i < CE_MAX_WINDOWS && !waited; i++) {
                ce_window *w = &g_windows[i];
                if (!w->used || w->dead || w->parent || !w->wm)
                    continue;
                bd_wm_client_event ev;
                long remain = timeout_ms - elapsed;
                int r = bd_wm_client_poll(w->wm, &ev,
                                          remain > 20 ? 20 : (int)remain);
                if (r == 1)
                    pump_one_client(w, &ev);
                waited = 1;
            }
            if (!waited) {
                struct timespec ts = { 0, 5 * 1000 * 1000 };
                nanosleep(&ts, NULL);
            }
        }
    }
}

/* ---------------- GetMessage family ---------------- */

/* Try to produce one message without waiting. 1 = produced,
 * 0 = none available right now, -1 = WM_QUIT produced. */
static int try_produce(CE_MSG *out, CE_HANDLE filter, CE_DWORD mn,
                       CE_DWORD mx)
{
    uint16_t pend = 0;
    if (queue_pop_match(out, &pend, filter, mn, mx)) {
        g_last_pend_char = pend;
        g_last_pend_hwnd = out->hwnd;
        return 1;
    }
    pthread_mutex_lock(&g_lock);
    int quit = g_quit_posted;
    int code = g_quit_code;
    if (quit)
        g_quit_posted = 0;
    pthread_mutex_unlock(&g_lock);
    if (quit && queue_len() == 0) {
        memset(out, 0, sizeof(*out));
        out->message = CE_WM_QUIT;
        out->wParam = (CE_DWORD)code;
        return -1;
    }
    /* paint synthesis */
    for (int i = 0; i < CE_MAX_WINDOWS; i++) {
        ce_window *w = &g_windows[i];
        if (!w->used || w->dead || w->nupd == 0)
            continue;
        if (!msg_matches(&(CE_MSG){ (CE_HANDLE)w, CE_WM_PAINT, 0, 0, 0, {0,0} },
                         filter, mn, mx))
            continue;
        memset(out, 0, sizeof(*out));
        out->hwnd = (CE_HANDLE)w;
        out->message = CE_WM_PAINT;
        out->time = ce_GetTickCount();
        return 1;
    }
    /* timer synthesis */
    uint64_t now = ce_GetTickCount();
    for (int i = 0; i < CE_MAX_TIMERS; i++) {
        ce_timer *t = &g_timers[i];
        if (!t->used || t->due > now)
            continue;
        if (!msg_matches(&(CE_MSG){ (CE_HANDLE)t->win, CE_WM_TIMER, 0, 0, 0, {0,0} },
                         filter, mn, mx))
            continue;
        t->due = now + t->elapse;
        memset(out, 0, sizeof(*out));
        out->hwnd = (CE_HANDLE)t->win;
        out->message = CE_WM_TIMER;
        out->wParam = t->id;
        out->lParam = (CE_LONG)t->proc;
        out->time = (CE_DWORD)now;
        return 1;
    }
    return 0;
}

CE_BOOL ce_GetMessageW(CE_MSG *out, CE_HANDLE filter, CE_DWORD mn, CE_DWORD mx)
{
    if (!out) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return (CE_BOOL)-1;
    }
    for (;;) {
        int r = try_produce(out, filter, mn, mx);
        if (r == 1)
            return CE_TRUE;
        if (r == -1)
            return CE_FALSE;
        uint64_t due = next_timer_due();
        int wait = 50;
        if (due != UINT64_MAX) {
            uint64_t now = ce_GetTickCount();
            long d = (long)(due - now);
            if (d < 0) d = 0;
            if (d < wait) wait = (int)d;
        }
        gwe_pump_wm(wait);
    }
}

CE_BOOL ce_PeekMessageW(CE_MSG *out, CE_HANDLE filter, CE_DWORD mn,
                        CE_DWORD mx, CE_DWORD remove_)
{
    if (!out) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return CE_FALSE;
    }
    gwe_pump_wm(0);
    if (!remove_) {
        /* peek without removing: only the posted queue can be
         * peeked non-destructively in this implementation */
        pthread_mutex_lock(&g_lock);
        for (int i = g_qhead; i < g_qtail; i++) {
            ce_qent *e = &g_queue[i % CE_MAX_QUEUE];
            if (msg_matches(&e->msg, filter, mn, mx)) {
                *out = e->msg;
                pthread_mutex_unlock(&g_lock);
                return CE_TRUE;
            }
        }
        pthread_mutex_unlock(&g_lock);
        return CE_FALSE;
    }
    int r = try_produce(out, filter, mn, mx);
    if (r == 1)
        return CE_TRUE;
    if (r == -1) {
        /* WM_QUIT consumed by a peek: re-post so GetMessage sees it */
        ce_PostQuitMessage((int)out->wParam);
        return CE_TRUE;
    }
    return CE_FALSE;
}

CE_BOOL ce_TranslateMessage(const CE_MSG *m)
{
    if (!m)
        return CE_FALSE;
    if ((m->message == CE_WM_KEYDOWN || m->message == CE_WM_SYSKEYDOWN)
        && g_last_pend_char && m->hwnd == g_last_pend_hwnd) {
        CE_DWORD cm = (m->message == CE_WM_KEYDOWN) ? CE_WM_CHAR : CE_WM_SYSCHAR;
        gwe_enqueue(m->hwnd, cm, g_last_pend_char, m->lParam);
        g_last_pend_char = 0;
        return CE_TRUE;
    }
    return CE_FALSE;
}

CE_LONG ce_DispatchMessageW(const CE_MSG *m)
{
    if (!m)
        return 0;
    if (m->message == CE_WM_TIMER && m->lParam) {
        CE_TIMERPROC proc = (CE_TIMERPROC)m->lParam;
        proc(m->hwnd, CE_WM_TIMER, m->wParam, ce_GetTickCount());
        return 0;
    }
    ce_window *w = gwe_win_from_handle(m->hwnd);
    if (!w)
        return 0;
    return gwe_call_proc(w, m->message, m->wParam, m->lParam);
}

/* ---------------- registered messages / cursors ---------------- */

static struct { CE_WCHAR *name; } g_regmsgs[64];
static int g_nregmsgs;

CE_DWORD ce_RegisterWindowMessageW(const CE_WCHAR *name)
{
    if (!name) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return 0;
    }
    for (int i = 0; i < g_nregmsgs; i++)
        if (wcs_eq(g_regmsgs[i].name, name))
            return 0xC000 + (CE_DWORD)i;
    if (g_nregmsgs >= 64)
        return 0;
    g_regmsgs[g_nregmsgs].name = wcs_dup(name);
    return 0xC000 + (CE_DWORD)(g_nregmsgs++);
}

static CE_HANDLE g_current_cursor;

CE_HANDLE ce_LoadCursorW(CE_HANDLE hinst, const CE_WCHAR *name)
{
    (void)hinst;
    ce_gdobj *o = gdi_new_obj(CE_OBJ_CURSOR);
    if (!o)
        return NULL;
    /* system cursor ids arrive as MAKEINTRESOURCE values */
    o->cursor_id = (int)(uintptr_t)name;
    return (CE_HANDLE)o;
}

CE_HANDLE ce_SetCursor(CE_HANDLE h)
{
    CE_HANDLE old = g_current_cursor;
    g_current_cursor = h;
    return old;
}

/* ---------------- DefWindowProc ---------------- */

CE_LONG ce_DefWindowProcW(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                          CE_LONG lp)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return 0;
    switch (msg) {
    case CE_WM_NCCREATE:
        return 1;
    case CE_WM_CREATE:
        return 0;
    case CE_WM_CLOSE:
        ce_DestroyWindow(hwnd);
        return 0;
    case CE_WM_PAINT: {
        CE_PAINTSTRUCT ps;
        CE_HANDLE dc = ce_BeginPaint(hwnd, &ps);
        if (dc)
            ce_EndPaint(hwnd, &ps);
        return 0;
    }
    case CE_WM_ERASEBKGND: {
        CE_HANDLE dc = (CE_HANDLE)wp;
        if (dc && w->cls) {
            CE_RECT client = { 0, 0, gwe_win_w(w), gwe_win_h(w) };
            extern int gdi_fill_rect_handle(CE_HANDLE, const CE_RECT *,
                                            CE_HANDLE);
            CE_HANDLE brush = (CE_HANDLE)w->cls->hbr_background;
            if (brush)
                gdi_fill_rect_handle(dc, &client, brush);
        }
        return 1;
    }
    case CE_WM_SETTEXT: {
        const CE_WCHAR *t = (const CE_WCHAR *)lp;
        free(w->text);
        w->text = wcs_dup(t);
        if (!w->parent && w->wm && w->text) {
            char u8[128];
            if (ce_w2u8(w->text, u8, sizeof(u8)) >= 0)
                bd_wm_client_set_title(w->wm, u8);
        }
        return 1;
    }
    case CE_WM_GETTEXT: {
        CE_WCHAR *buf = (CE_WCHAR *)lp;
        int max = (int)wp;
        if (!buf || max <= 0)
            return 0;
        const CE_WCHAR *t = w->text;
        int n = 0;
        if (t)
            while (t[n] && n < max - 1) {
                buf[n] = t[n];
                n++;
            }
        buf[n] = 0;
        return n;
    }
    case CE_WM_GETTEXTLENGTH:
        return w->text ? (CE_LONG)ce_wcslen(w->text) : 0;
    case CE_WM_SETFOCUS:
    case CE_WM_KILLFOCUS:
    case CE_WM_DESTROY:
    case CE_WM_NCDESTROY:
    case CE_WM_NCPAINT:
    case CE_WM_NCCALCSIZE:
    case CE_WM_SIZE:
    case CE_WM_MOVE:
    case CE_WM_ENABLE:
    case CE_WM_SHOWWINDOW:
    case CE_WM_ACTIVATE:
    case CE_WM_SYSCOMMAND:
    case CE_WM_SETCURSOR:
    default:
        return 0;
    }
}
