/* BrainCE Layer -- dialog manager, built-in controls, hot keys.
 *
 * M3: the bundled apps are dialog-driven (their .rsrc carries
 * DLGTEMPLATEs), so this TU implements the CE dialog manager on
 * top of the M2 GWE: template parsing (DLGTEMPLATE and
 * DLGTEMPLATEEX), dialog-unit conversion from the dialog font,
 * the modal loop, IsDialogMessageW keyboard navigation, and the
 * predefined control classes Button / Edit / Static with their
 * standard messages. Also RegisterHotKey/UnregisterHotKey and
 * keybd_event. All constants are the published Win32/WinCE values.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <alloca.h>
#include <unistd.h>

#include "ce_types.h"
#include "fs_bridge.h"
#include "gwe.h"

static uint16_t rd16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}
static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* ---- published constants not already in ce_types.h ---- */
#define CE_WM_NOTIFY        0x004E
#define CE_WM_INITDIALOG    0x0110
#define CE_WM_SETFONT       0x0030
#define CE_WM_GETFONT       0x0031
#define CE_WM_HOTKEY        0x0312
#define CE_WM_CTLCOLORBTN   0x0135
#define CE_WM_CTLCOLORDLG   0x0136
#define CE_WM_CTLCOLOREDIT  0x0133
#define CE_WM_CTLCOLORSTATIC 0x0138
#define CE_DM_GETDEFID      (0x0400 + 0)
#define CE_DM_SETDEFID      (0x0400 + 1)
#define CE_DWL_MSGRESULT    0
#define CE_DWL_DLGPROC      4
#define CE_DWL_USER         8
#define CE_DLGWINDOWEXTRA   30
#define CE_IDOK             1
#define CE_IDCANCEL         2
#define CE_MOD_ALT          0x0001
#define CE_MOD_CONTROL      0x0002
#define CE_MOD_SHIFT        0x0004
#define CE_KEYEVENTF_KEYUP  0x0002
#define CE_VK_SHIFT         0x10
#define CE_VK_CONTROL       0x11
#define CE_VK_MENU          0x12
#define CE_VK_SPACE         0x20
#define CE_BS_TYPEMASK      0x0000000FL
#define CE_BS_PUSHBUTTON    0x00000000L
#define CE_BS_DEFPUSHBUTTON 0x00000001L
#define CE_BS_CHECKBOX      0x00000002L
#define CE_BS_AUTOCHECKBOX  0x00000003L
#define CE_BS_RADIOBUTTON   0x00000004L
#define CE_BS_3STATE        0x00000005L
#define CE_BS_AUTO3STATE    0x00000006L
#define CE_BS_GROUPBOX      0x00000007L
#define CE_BS_AUTORADIOBUTTON 0x00000009L
#define CE_BS_OWNERDRAW     0x0000000BL
#define CE_BS_LEFT          0x00000100L
#define CE_BST_UNCHECKED    0x0000
#define CE_BST_CHECKED      0x0001
#define CE_BST_INDETERMINATE 0x0002
#define CE_BM_GETCHECK      0x00F0
#define CE_BM_SETCHECK      0x00F1
#define CE_BM_GETSTATE      0x00F2
#define CE_BM_SETSTATE      0x00F3
#define CE_BM_SETSTYLE      0x00F4
#define CE_BM_CLICK         0x00F5
#define CE_BN_CLICKED       0
#define CE_BN_DOUBLECLICKED 5
#define CE_SS_LEFT          0x00000000L
#define CE_SS_CENTER        0x00000001L
#define CE_SS_RIGHT         0x00000002L
#define CE_SS_ICON          0x00000003L
#define CE_SS_BLACKFRAME    0x00000007L
#define CE_SS_SIMPLE        0x0000000BL
#define CE_SS_LEFTNOWORDWRAP 0x0000000CL
#define CE_SS_BITMAP        0x0000000EL
#define CE_SS_SUNKEN        0x00001000L
#define CE_STM_SETICON      0x0170
#define CE_STM_GETICON      0x0171
#define CE_STM_SETIMAGE     0x0172
#define CE_STM_GETIMAGE     0x0173
#define CE_IMAGE_ICON       1
#define CE_IMAGE_BITMAP     0
#define CE_ES_LEFT          0x0000L
#define CE_ES_CENTER        0x0001L
#define CE_ES_MULTILINE     0x0004L
#define CE_ES_AUTOVSCROLL   0x0040L
#define CE_ES_AUTOHSCROLL   0x0080L
#define CE_ES_READONLY      0x0800L
#define CE_ES_WANTRETURN    0x1000L
#define CE_EM_GETSEL        0x00B0
#define CE_EM_SETSEL        0x00B1
#define CE_EM_REPLACESEL    0x00C2
#define CE_EM_GETLINECOUNT  0x00BA
#define CE_EM_LINEINDEX     0x00BB
#define CE_EM_GETLINE       0x00C4
#define CE_EM_LINELENGTH    0x00C1
#define CE_EM_SETREADONLY   0x00CF
#define CE_EN_SETFOCUS      0x0100
#define CE_EN_KILLFOCUS     0x0200
#define CE_EN_CHANGE        0x0300
#define CE_EN_UPDATE        0x0400
#define CE_DLGC_WANTARROWS  0x0001
#define CE_DLGC_WANTTAB     0x0002
#define CE_DLGC_WANTALLKEYS 0x0004
#define CE_DLGC_HASSETSEL   0x0008
#define CE_DLGC_DEFPUSHBUTTON 0x0010
#define CE_DLGC_UNDEFPUSHBUTTON 0x0020
#define CE_DLGC_WANTCHARS   0x0080
#define CE_DLGC_BUTTON      0x2000
#define CE_WM_GETDLGCODE    0x0087
#define CE_DS_ABSALIGN      0x01
#define CE_DS_SYSMODAL      0x02
#define CE_DS_SETFONT       0x40
#define CE_DS_FIXEDSYS      0x08
#define CE_DS_SHELLFONT     (0x40 | 0x08)
#define CE_WS_POPUP         0x80000000u
#define CE_WS_BORDER        0x00800000u
#define CE_WS_DLGFRAME      0x00400000u
#define CE_WS_CAPTION       0x00C00000u
#define CE_WS_SYSMENU       0x00080000u
#define CE_WS_VISIBLE       0x10000000u
#define CE_WS_DISABLED      0x08000000u
#define CE_WS_CHILD         0x40000000u
#define CE_WS_CLIPSIBLINGS  0x04000000u
#define CE_DT_LEFT          0x0000
#define CE_DT_CENTER        0x0001
#define CE_DT_RIGHT         0x0002
#define CE_DT_VCENTER       0x0004
#define CE_DT_WORDBREAK     0x0010
#define CE_DT_SINGLELINE    0x0020
#define CE_DT_NOCLIP        0x0100
#define CE_DT_CALCRECT      0x0400
#define CE_TRANSPARENT_BK   1
#define CE_OPAQUE_BK        2

/* GDI / GWE entry points used here (defined in gwe.c / gdi.c). */
CE_HANDLE ce_CreateWindowExW(CE_DWORD, const CE_WCHAR *, const CE_WCHAR *,
                             CE_DWORD, int, int, int, int, CE_HANDLE,
                             CE_HANDLE, CE_HANDLE, void *);
CE_BOOL ce_DestroyWindow(CE_HANDLE);
CE_BOOL ce_ShowWindow(CE_HANDLE, int);
CE_HANDLE ce_SetFocus(CE_HANDLE);
CE_HANDLE ce_GetFocus(void);
CE_BOOL ce_EnableWindow(CE_HANDLE, CE_BOOL);
CE_LONG ce_SendMessageW(CE_HANDLE, CE_DWORD, CE_DWORD, CE_LONG);
CE_BOOL ce_PostMessageW(CE_HANDLE, CE_DWORD, CE_DWORD, CE_LONG);
CE_BOOL ce_GetMessageW(CE_MSG *, CE_HANDLE, CE_DWORD, CE_DWORD);
CE_BOOL ce_PeekMessageW(CE_MSG *, CE_HANDLE, CE_DWORD, CE_DWORD, CE_DWORD);
CE_BOOL ce_TranslateMessage(const CE_MSG *);
CE_LONG ce_DispatchMessageW(const CE_MSG *);
CE_BOOL ce_IsWindow(CE_HANDLE);
CE_BOOL ce_IsWindowVisible(CE_HANDLE);
CE_BOOL ce_IsWindowEnabled(CE_HANDLE);
CE_HANDLE ce_GetParent(CE_HANDLE);
CE_LONG ce_GetWindowLongW(CE_HANDLE, int);
CE_LONG ce_SetWindowLongW(CE_HANDLE, int, CE_LONG);
int ce_GetDlgCtrlID(CE_HANDLE);
CE_BOOL ce_GetClientRect(CE_HANDLE, CE_RECT *);
int ce_GetWindowTextW(CE_HANDLE, CE_WCHAR *, int);
int ce_GetWindowTextLengthW(CE_HANDLE);
CE_BOOL ce_SetWindowTextW(CE_HANDLE, const CE_WCHAR *);
CE_BOOL ce_InvalidateRect(CE_HANDLE, const CE_RECT *, CE_BOOL);
CE_LONG ce_DefWindowProcW(CE_HANDLE, CE_DWORD, CE_DWORD, CE_LONG);
CE_HANDLE ce_RegisterClassW(const CE_WNDCLASSW *);
CE_HANDLE ce_GetDC(CE_HANDLE);
int ce_ReleaseDC(CE_HANDLE, CE_HANDLE);
CE_HANDLE ce_BeginPaint(CE_HANDLE, void *);
CE_BOOL ce_EndPaint(CE_HANDLE, const void *);
CE_HANDLE ce_CreateFontIndirectW(const CE_LOGFONTW *);
CE_HANDLE ce_CreateSolidBrush(CE_DWORD);
CE_HANDLE ce_GetStockObject(int);
CE_HANDLE ce_GetSysColorBrush(int);
CE_HANDLE ce_SelectObject(CE_HANDLE, CE_HANDLE);
CE_BOOL ce_DeleteObject(CE_HANDLE);
int ce_FillRect(CE_HANDLE, const CE_RECT *, CE_HANDLE);
CE_BOOL ce_Rectangle(CE_HANDLE, int, int, int, int);
int ce_DrawTextW(CE_HANDLE, const CE_WCHAR *, int, CE_RECT *, CE_DWORD);
CE_BOOL ce_TextOutW(CE_HANDLE, int, int, const CE_WCHAR *, int);
CE_BOOL ce_GetTextMetricsW(CE_HANDLE, CE_TEXTMETRICW *);
CE_DWORD ce_SetTextColor(CE_HANDLE, CE_DWORD);
CE_DWORD ce_GetSysColor(int);
int ce_SetBkMode(CE_HANDLE, int);
CE_HANDLE ce_SetCapture(CE_HANDLE);
CE_BOOL ce_ReleaseCapture(void);
CE_BOOL ce_PtInRect(const CE_RECT *, CE_POINT);
CE_DWORD ce_GetTickCount(void);
CE_HANDLE ce_FindResourceW(CE_HANDLE, const CE_WCHAR *, const CE_WCHAR *);
CE_HANDLE ce_LoadResource(CE_HANDLE, CE_HANDLE);
CE_DWORD ce_GetSysColor(int);

typedef CE_LONG (*CE_DLGPROC)(CE_HANDLE, CE_DWORD, CE_DWORD, CE_LONG);

/* ================= dialog records ================= */

#define CE_MAX_DIALOGS 16
typedef struct {
    int        used;
    CE_HANDLE  hwnd;
    CE_DLGPROC proc;
    CE_HANDLE  font;        /* dialog font (owned) or NULL */
    int        base_x, base_y;
    int        ended;
    int        destroyed;
    CE_LONG    result;
    CE_HANDLE  owner;
} ce_dialog_rec;

static ce_dialog_rec g_dlgs[CE_MAX_DIALOGS];

static ce_dialog_rec *dlg_find(CE_HANDLE hwnd)
{
    for (int i = 0; i < CE_MAX_DIALOGS; i++)
        if (g_dlgs[i].used && g_dlgs[i].hwnd == hwnd)
            return &g_dlgs[i];
    return NULL;
}

static ce_dialog_rec *dlg_new(void)
{
    for (int i = 0; i < CE_MAX_DIALOGS; i++)
        if (!g_dlgs[i].used) {
            memset(&g_dlgs[i], 0, sizeof(g_dlgs[i]));
            g_dlgs[i].used = 1;
            return &g_dlgs[i];
        }
    return NULL;
}

/* The proc a dialog currently answers to: DWL_DLGPROC wins when
 * the app replaced it via SetWindowLongW. */
static CE_DLGPROC dlg_proc_of(ce_window *w, ce_dialog_rec *rec)
{
    if (w && w->extra) {
        CE_LONG v = 0;
        memcpy(&v, w->extra + CE_DWL_DLGPROC, sizeof(v));
        if (v)
            return (CE_DLGPROC)(uintptr_t)v;
    }
    return rec ? rec->proc : NULL;
}

/* ================= dialog-unit conversion ================= */

static void dlg_font_metrics(CE_HANDLE font, int *bx, int *by)
{
    *bx = 8;
    *by = 16;
    if (!font)
        return;
    CE_HANDLE dc = ce_GetDC(NULL);
    if (!dc)
        return;
    CE_HANDLE old = ce_SelectObject(dc, font);
    CE_TEXTMETRICW tm;
    if (ce_GetTextMetricsW(dc, &tm)) {
        if (tm.tmAveCharWidth > 0)
            *bx = (int)tm.tmAveCharWidth;
        if (tm.tmHeight > 0)
            *by = (int)(tm.tmHeight + tm.tmExternalLeading);
    }
    ce_SelectObject(dc, old);
    ce_ReleaseDC(NULL, dc);
}

static int dlg_x(int dlu, int bx) { return (dlu * bx + 2) / 4; }
static int dlg_y(int dlu, int by) { return (dlu * by + 4) / 8; }

CE_LONG ce_GetDialogBaseUnits(void)
{
    int bx, by;
    CE_HANDLE f = ce_GetStockObject(13 /* SYSTEM_FONT */);
    dlg_font_metrics(f, &bx, &by);
    return (CE_LONG)(((CE_DWORD)by << 16) | (CE_DWORD)bx);
}

CE_BOOL ce_MapDialogRect(CE_HANDLE hdlg, CE_RECT *r)
{
    ce_dialog_rec *rec = dlg_find(hdlg);
    int bx = 8, by = 16;
    if (rec && rec->base_x) {
        bx = rec->base_x;
        by = rec->base_y;
    } else {
        CE_LONG u = ce_GetDialogBaseUnits();
        bx = (int)(u & 0xFFFF);
        by = (int)((u >> 16) & 0xFFFF);
    }
    if (!r)
        return CE_FALSE;
    r->left = dlg_x((int)r->left, bx);
    r->right = dlg_x((int)r->right, bx);
    r->top = dlg_y((int)r->top, by);
    r->bottom = dlg_y((int)r->bottom, by);
    return CE_TRUE;
}

/* ================= template parsing ================= */

typedef struct {
    CE_DWORD style, exstyle;
    int      cdit;
    short    x, y, cx, cy;
    const CE_WCHAR *title;
    unsigned short font_size;   /* points, when DS_SETFONT */
    const CE_WCHAR *font_face;  /* when DS_SETFONT */
    const uint8_t *items;       /* first DLGITEMTEMPLATE */
    int      is_ex;
} ce_dlg_template;

static const uint8_t *align4(const uint8_t *p)
{
    return (const uint8_t *)(((uintptr_t)p + 3) & ~(uintptr_t)3);
}

static const CE_WCHAR *skip_sz_or_ord(const uint8_t *p, const uint8_t **next)
{
    const uint16_t *w = (const uint16_t *)p;
    if (*w == 0xFFFF) {
        *next = p + 4;
        return NULL;
    }
    *next = p + (ce_wcslen((const CE_WCHAR *)p) + 1) * 2;
    return (const CE_WCHAR *)p;
}

static int parse_template(const void *tmpl, ce_dlg_template *out)
{
    const uint8_t *p = tmpl;
    const uint16_t *w16 = (const uint16_t *)p;
    memset(out, 0, sizeof(*out));
    out->font_size = 0;
    if (w16[0] == 0x0001 && w16[1] == 0xFFFF) {
        /* DLGTEMPLATEEX: ver(2) sig(2) helpID(4) exStyle(4)
         * style(4) cdit(2) x y cx cy (2 each) -> menu at 26 */
        out->is_ex = 1;
        out->exstyle = rd32((const uint8_t *)tmpl + 8);
        out->style = rd32((const uint8_t *)tmpl + 12);
        out->cdit = (int)(int16_t)rd16((const uint8_t *)tmpl + 16);
        out->x = (short)rd16((const uint8_t *)tmpl + 18);
        out->y = (short)rd16((const uint8_t *)tmpl + 20);
        out->cx = (short)rd16((const uint8_t *)tmpl + 22);
        out->cy = (short)rd16((const uint8_t *)tmpl + 24);
        p = (const uint8_t *)tmpl + 26;
    } else {
        out->style = rd32(p);
        out->exstyle = rd32(p + 4);
        out->cdit = (int)(int16_t)rd16(p + 8);
        out->x = (short)rd16(p + 10);
        out->y = (short)rd16(p + 12);
        out->cx = (short)rd16(p + 14);
        out->cy = (short)rd16(p + 16);
        p += 18;
    }
    const uint8_t *next;
    skip_sz_or_ord(p, &next);           /* menu */
    p = next;
    skip_sz_or_ord(p, &next);           /* window class */
    p = next;
    out->title = (const CE_WCHAR *)p;
    p += (ce_wcslen(out->title) + 1) * 2;
    if (out->style & CE_DS_SETFONT) {
        out->font_size = (unsigned short)rd16(p);
        p += 2;
        if (out->is_ex)
            p += 4;   /* weight(2) italic(1) charset(1) */
        out->font_face = (const CE_WCHAR *)p;
        p += (ce_wcslen(out->font_face) + 1) * 2;
    }
    out->items = align4(p);
    return 0;
}

/* ================= the dialog window procedure ================= */

static CE_LONG ce_dlg_wndproc(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                              CE_LONG lp);

CE_BOOL ce_EndDialog(CE_HANDLE hwnd, CE_LONG result)
{
    ce_dialog_rec *rec = dlg_find(hwnd);
    if (!rec)
        return CE_FALSE;
    rec->result = result;
    rec->ended = 1;
    ce_DestroyWindow(hwnd);
    return CE_TRUE;
}

CE_LONG ce_DefDlgProcW(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                       CE_LONG lp)
{
    switch (msg) {
    case CE_WM_ERASEBKGND: {
        CE_HANDLE dc = (CE_HANDLE)wp;
        if (dc) {
            CE_RECT client;
            if (ce_GetClientRect(hwnd, &client))
                ce_FillRect(dc, &client,
                            ce_GetSysColorBrush(CE_COLOR_BTNFACE));
        }
        return 1;
    }
    case CE_WM_PAINT: {
        /* validate the update region; children paint themselves */
        void *ps = alloca(64);
        CE_HANDLE dc = ce_BeginPaint(hwnd, ps);
        if (dc)
            ce_EndPaint(hwnd, ps);
        return 0;
    }
    case CE_WM_CLOSE:
        ce_EndDialog(hwnd, CE_IDCANCEL);
        return 0;
    case CE_WM_CTLCOLORDLG:
    case CE_WM_CTLCOLORBTN:
    case CE_WM_CTLCOLORSTATIC:
        return (CE_LONG)ce_GetSysColorBrush(CE_COLOR_BTNFACE);
    case CE_WM_CTLCOLOREDIT:
        return (CE_LONG)ce_GetSysColorBrush(5 /* COLOR_WINDOW */);
    default:
        break;
    }
    /* The dialog manager's message-result convention. */
    if (msg != CE_WM_INITDIALOG)
        return ce_GetWindowLongW(hwnd, CE_DWL_MSGRESULT);
    return 0;
}

static CE_LONG ce_dlg_wndproc(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                              CE_LONG lp)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    ce_dialog_rec *rec = dlg_find(hwnd);
    if (msg == CE_WM_CREATE && lp) {
        const CE_CREATESTRUCTW *cs = (const CE_CREATESTRUCTW *)lp;
        ce_dialog_rec *r = (ce_dialog_rec *)cs->lpCreateParams;
        if (r) {
            r->hwnd = hwnd;
            if (w && w->extra) {
                CE_LONG v = (CE_LONG)(uintptr_t)r->proc;
                memcpy(w->extra + CE_DWL_DLGPROC, &v, sizeof(v));
            }
            rec = r;
        }
    }
    if (msg == CE_WM_NCDESTROY) {
        if (rec) {
            rec->destroyed = 1;
            if (!rec->ended) {
                rec->ended = 1;
                rec->result = CE_IDCANCEL;
            }
        }
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    }
    CE_DLGPROC proc = dlg_proc_of(w, rec);
    if (proc) {
        CE_LONG r = proc(hwnd, msg, wp, lp);
        if (r)
            return msg == CE_WM_INITDIALOG ? r : r;
        return ce_DefDlgProcW(hwnd, msg, wp, lp);
    }
    return ce_DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================= dialog construction ================= */

static const CE_WCHAR *atom_class_name(uint16_t atom, CE_WCHAR *buf)
{
    static const char *names[6] = {
        "Button", "Edit", "Static", "ListBox", "ComboBox", "ScrollBar"
    };
    if (atom >= 0x0080 && atom <= 0x0085) {
        const char *n = names[atom - 0x0080];
        int i = 0;
        while (n[i]) {
            buf[i] = (CE_WCHAR)n[i];
            i++;
        }
        buf[i] = 0;
        return buf;
    }
    return NULL;
}

static CE_HANDLE dlg_create_from_template(CE_HANDLE hinst,
                                          const void *tmpl,
                                          CE_HANDLE owner,
                                          CE_DLGPROC proc,
                                          CE_LONG init_param,
                                          ce_dialog_rec **rec_out)
{
    ce_dlg_template t;
    if (parse_template(tmpl, &t) != 0)
        return NULL;
    ce_dialog_rec *rec = dlg_new();
    if (!rec) {
        ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    rec->proc = proc;
    rec->owner = owner;
    /* dialog font */
    if (t.style & CE_DS_SETFONT) {
        int px = t.font_size
            ? (int)((t.font_size * 96 + 36) / 72) : 12;
        CE_LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -px;
        lf.lfWeight = 400;
        lf.lfCharSet = 128;   /* SHIFTJIS_CHARSET */
        if (t.font_face) {
            size_t n = 0;
            while (t.font_face[n] && n < 31) {
                lf.lfFaceName[n] = t.font_face[n];
                n++;
            }
            lf.lfFaceName[n] = 0;
        }
        rec->font = ce_CreateFontIndirectW(&lf);
    }
    dlg_font_metrics(rec->font, &rec->base_x, &rec->base_y);
    int bx = rec->base_x, by = rec->base_y;
    int dx = dlg_x(t.x, bx), dy = dlg_y(t.y, by);
    int dcx = dlg_x(t.cx, bx), dcy = dlg_y(t.cy, by);
    /* A dialog with no explicit position is centred on the screen
     * (CE centers top-level dialogs the same way in practice). */
    if (!(t.style & CE_DS_ABSALIGN)) {
        dx = (CE_SCREEN_W - dcx) / 2;
        dy = (448 - dcy) / 2;
        if (dx < 0)
            dx = 0;
        if (dy < 0)
            dy = 0;
    }
    CE_DWORD style = (t.style & ~CE_DS_SETFONT & ~CE_DS_FIXEDSYS)
                     | CE_WS_POPUP | CE_WS_CAPTION | CE_WS_SYSMENU;
    style &= ~CE_WS_VISIBLE;
    static const CE_WCHAR dlg_class[] =
        { '#', '3', '2', '7', '7', '0', 0 };
    CE_HANDLE hwnd = ce_CreateWindowExW(0, dlg_class,
                                        t.title, style, dx, dy, dcx, dcy,
                                        owner, NULL, hinst, rec);
    if (!hwnd) {
        if (rec->font)
            ce_DeleteObject(rec->font);
        rec->used = 0;
        return NULL;
    }
    rec->hwnd = hwnd;
    /* children */
    const uint8_t *p = t.items;
    CE_HANDLE first_tab = NULL;
    for (int i = 0; i < t.cdit; i++) {
        p = align4(p);
        CE_DWORD istyle, iexstyle;
        short ix, iy, icx, icy;
        CE_DWORD iid;
        const uint8_t *q;
        if (t.is_ex) {
            /* DLGITEMTEMPLATEEX: helpID(4) exStyle(4) style(4)
             * x y cx cy (2 each) id(4) -> class at 24 */
            iexstyle = rd32(p + 4);
            istyle = rd32(p + 8);
            ix = (short)rd16(p + 12);
            iy = (short)rd16(p + 14);
            icx = (short)rd16(p + 16);
            icy = (short)rd16(p + 18);
            iid = rd32(p + 20);
            q = p + 24;
        } else {
            istyle = rd32(p);
            iexstyle = rd32(p + 4);
            ix = (short)rd16(p + 8);
            iy = (short)rd16(p + 10);
            icx = (short)rd16(p + 12);
            icy = (short)rd16(p + 14);
            iid = (CE_DWORD)(int16_t)rd16(p + 16);
            q = p + 18;
        }
        /* class: atom or string */
        CE_WCHAR clsbuf[64];
        const CE_WCHAR *clsname;
        const uint8_t *next;
        const uint16_t *cw = (const uint16_t *)q;
        if (*cw == 0xFFFF) {
            clsname = atom_class_name(cw[1], clsbuf);
            next = q + 4;
            if (!clsname) {
                /* unknown predefined class: skip the item */
                clsname = (const CE_WCHAR *)clsbuf;
                clsbuf[0] = 0;
            }
        } else {
            clsname = (const CE_WCHAR *)q;
            next = q + (ce_wcslen(clsname) + 1) * 2;
        }
        q = next;
        /* title: string or resource ordinal */
        const CE_WCHAR *title = (const CE_WCHAR *)q;
        CE_WCHAR titlebuf[128];
        const uint16_t *tw = (const uint16_t *)q;
        if (*tw == 0xFFFF) {
            titlebuf[0] = 0;
            title = titlebuf;
            q += 4;
        } else {
            q += (ce_wcslen(title) + 1) * 2;
        }
        uint16_t extra_bytes;
        memcpy(&extra_bytes, q, 2);
        q += 2 + extra_bytes;
        if (clsname[0]) {
            CE_HANDLE child = ce_CreateWindowExW(
                iexstyle, clsname, title,
                istyle | CE_WS_CHILD,
                dlg_x(ix, bx), dlg_y(iy, by),
                dlg_x(icx, bx), dlg_y(icy, by),
                hwnd, (CE_HANDLE)(uintptr_t)(int)(short)iid,
                hinst, NULL);
            if (child) {
                if (rec->font)
                    ce_SendMessageW(child, CE_WM_SETFONT,
                                    (CE_DWORD)rec->font, 1);
                if (!first_tab && (istyle & CE_WS_TABSTOP)
                    && !(istyle & CE_WS_DISABLED))
                    first_tab = child;
            }
        }
        p = q;
    }
    /* WM_INITDIALOG: lParam is the app's init parameter; the
     * conventional wParam is the first tab-stop control. */
    CE_LONG init_ret = ce_SendMessageW(hwnd, CE_WM_INITDIALOG,
                                       (CE_DWORD)first_tab,
                                       init_param);
    if (init_ret && first_tab)
        ce_SetFocus(first_tab);
    if (rec_out)
        *rec_out = rec;
    return hwnd;
}

CE_LONG ce_DialogBoxIndirectParamW(CE_HANDLE hinst, const void *tmpl,
                                   CE_HANDLE owner, CE_DLGPROC proc,
                                   CE_LONG init_param)
{
    if (!tmpl || !proc) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return -1;
    }
    ce_dialog_rec *rec = NULL;
    CE_HANDLE hwnd = dlg_create_from_template(hinst, tmpl, owner,
                                              proc, init_param, &rec);
    if (!hwnd)
        return -1;
    if (owner && ce_IsWindow(owner))
        ce_EnableWindow(owner, CE_FALSE);
    ce_ShowWindow(hwnd, 5 /* SW_SHOW */);
    /* modal loop */
    CE_MSG msg;
    while (rec && !rec->ended) {
        CE_BOOL r = ce_GetMessageW(&msg, NULL, 0, 0);
        if (r == CE_FALSE || r == (CE_BOOL)-1)
            break;
        if (!ce_IsDialogMessageW(hwnd, &msg)) {
            ce_TranslateMessage(&msg);
            ce_DispatchMessageW(&msg);
        }
    }
    CE_LONG result = rec ? rec->result : CE_IDCANCEL;
    if (owner && ce_IsWindow(owner))
        ce_EnableWindow(owner, CE_TRUE);
    if (rec) {
        if (rec->font)
            ce_DeleteObject(rec->font);
        rec->used = 0;
    }
    return result;
}

CE_HANDLE ce_CreateDialogIndirectParamW(CE_HANDLE hinst,
                                        const void *tmpl,
                                        CE_HANDLE owner,
                                        CE_DLGPROC proc,
                                        CE_LONG init_param)
{
    if (!tmpl || !proc) {
        ce_set_lasterror(CE_ERROR_INVALID_PARAMETER);
        return NULL;
    }
    ce_dialog_rec *rec = NULL;
    CE_HANDLE hwnd = dlg_create_from_template(hinst, tmpl, owner,
                                              proc, init_param, &rec);
    if (hwnd)
        ce_ShowWindow(hwnd, 5 /* SW_SHOW */);
    return hwnd;
}

/* ================= dialog helpers ================= */

CE_HANDLE ce_GetDlgItem(CE_HANDLE hdlg, int id)
{
    ce_window *w = gwe_win_from_handle(hdlg);
    if (!w)
        return NULL;
    for (ce_window *c = w->first_child; c; c = c->next_sibling)
        if ((int)(intptr_t)c->menu_or_id == id)
            return (CE_HANDLE)c;
    return NULL;
}

CE_BOOL ce_SetDlgItemTextW(CE_HANDLE hdlg, int id, const CE_WCHAR *text)
{
    CE_HANDLE c = ce_GetDlgItem(hdlg, id);
    if (!c)
        return CE_FALSE;
    return ce_SetWindowTextW(c, text);
}

CE_DWORD ce_GetDlgItemTextW(CE_HANDLE hdlg, int id, CE_WCHAR *buf,
                            int max)
{
    CE_HANDLE c = ce_GetDlgItem(hdlg, id);
    if (!c || !buf || max <= 0)
        return 0;
    return (CE_DWORD)ce_GetWindowTextW(c, buf, max);
}

CE_LONG ce_SendDlgItemMessageW(CE_HANDLE hdlg, int id, CE_DWORD msg,
                               CE_DWORD wp, CE_LONG lp)
{
    CE_HANDLE c = ce_GetDlgItem(hdlg, id);
    if (!c)
        return 0;
    return ce_SendMessageW(c, msg, wp, lp);
}

/* Tab order: creation order is sibling list order reversed (the
 * list is topmost-first); collect children into an array in
 * creation order (template order). */
static int dlg_collect(ce_window *dlg, ce_window **out, int max)
{
    int n = 0;
    for (ce_window *c = dlg->first_child; c; c = c->next_sibling)
        n++;
    int i = n;
    for (ce_window *c = dlg->first_child; c; c = c->next_sibling) {
        if (--i < max)
            out[i] = c;
    }
    return n < max ? n : max;
}

static int dlg_is_tabstop(ce_window *c)
{
    return c && c->visible && c->enabled
           && (c->style & CE_WS_TABSTOP);
}

CE_BOOL ce_IsDialogMessageW(CE_HANDLE hdlg, CE_MSG *msg)
{
    ce_window *dlg = gwe_win_from_handle(hdlg);
    if (!dlg || !msg || !msg->hwnd)
        return CE_FALSE;
    /* the message must target the dialog or one of its children */
    ce_window *target = gwe_win_from_handle(msg->hwnd);
    if (!target)
        return CE_FALSE;
    ce_window *top = target;
    int in_dlg = 0;
    for (ce_window *p = target; p; p = p->parent)
        if (p == dlg) {
            in_dlg = 1;
            break;
        }
    if (!in_dlg)
        return CE_FALSE;
    (void)top;
    if (msg->message != CE_WM_KEYDOWN && msg->message != CE_WM_KEYUP
        && msg->message != CE_WM_CHAR
        && msg->message != CE_WM_SYSKEYDOWN
        && msg->message != CE_WM_SYSKEYUP)
        return CE_FALSE;
    if (msg->message != CE_WM_KEYDOWN)
        return CE_FALSE;
    CE_DWORD vk = msg->wParam;
    ce_window *kids[64];
    int nk = dlg_collect(dlg, kids, 64);
    if (vk == CE_VK_TAB) {
        /* move focus among tab stops (Shift reverses; the shift
         * state is not carried by our queue entries, so only the
         * forward direction is distinguishable here) */
        CE_HANDLE cur = ce_GetFocus();
        int idx = -1;
        for (int i = 0; i < nk; i++)
            if ((CE_HANDLE)kids[i] == cur)
                idx = i;
        for (int step = 1; step <= nk; step++) {
            ce_window *cand = kids[(idx + step) % nk];
            if (dlg_is_tabstop(cand)) {
                /* let an edit control keep Tab when it wants it */
                CE_LONG code = gwe_call_proc(cand, CE_WM_GETDLGCODE,
                                             0, (CE_LONG)msg);
                if (code & CE_DLGC_WANTTAB)
                    return CE_FALSE;
                ce_SetFocus((CE_HANDLE)cand);
                return CE_TRUE;
            }
        }
        return CE_TRUE;
    }
    if (vk == CE_VK_RETURN) {
        CE_HANDLE focus = ce_GetFocus();
        ce_window *fw = gwe_win_from_handle(focus);
        CE_LONG code = fw ? gwe_call_proc(fw, CE_WM_GETDLGCODE, 0,
                                          (CE_LONG)msg) : 0;
        if (code & CE_DLGC_WANTALLKEYS)
            return CE_FALSE;
        if (fw && (code & CE_DLGC_BUTTON)) {
            ce_SendMessageW(focus, CE_BM_CLICK, 0, 0);
            return CE_TRUE;
        }
        CE_LONG defid = ce_SendMessageW(hdlg, CE_DM_GETDEFID, 0, 0);
        int id = (int)(defid & 0xFFFF);
        if (id) {
            ce_SendMessageW(hdlg, CE_WM_COMMAND,
                            (CE_DWORD)((CE_BN_CLICKED << 16)
                                       | (CE_DWORD)(uint16_t)id),
                            (CE_LONG)ce_GetDlgItem(hdlg, id));
            return CE_TRUE;
        }
        return CE_FALSE;
    }
    if (vk == CE_VK_ESCAPE) {
        ce_SendMessageW(hdlg, CE_WM_COMMAND,
                        (CE_DWORD)((CE_BN_CLICKED << 16) | CE_IDCANCEL),
                        0);
        return CE_TRUE;
    }
    if (vk == CE_VK_UP || vk == CE_VK_DOWN || vk == CE_VK_LEFT
        || vk == CE_VK_RIGHT) {
        /* radio-button group navigation */
        CE_HANDLE cur = ce_GetFocus();
        ce_window *cw = gwe_win_from_handle(cur);
        if (cw && cw->parent == dlg) {
            CE_LONG code = gwe_call_proc(cw, CE_WM_GETDLGCODE, 0,
                                         (CE_LONG)msg);
            if (!(code & CE_DLGC_WANTARROWS)) {
                int dir = (vk == CE_VK_DOWN || vk == CE_VK_RIGHT)
                          ? 1 : -1;
                int idx = -1;
                for (int i = 0; i < nk; i++)
                    if (kids[i] == cw)
                        idx = i;
                for (int step = 1; step <= nk; step++) {
                    int j = ((idx + dir * step) % nk + nk) % nk;
                    ce_window *cand = kids[j];
                    if (cand->style & CE_WS_GROUP)
                        break;
                    if (dlg_is_tabstop(cand)) {
                        ce_SetFocus((CE_HANDLE)cand);
                        if ((cand->style & CE_BS_TYPEMASK)
                            == CE_BS_AUTORADIOBUTTON)
                            ce_SendMessageW((CE_HANDLE)cand,
                                            CE_BM_SETCHECK,
                                            CE_BST_CHECKED, 0);
                        return CE_TRUE;
                    }
                }
                return CE_TRUE;
            }
        }
        return CE_FALSE;
    }
    return CE_FALSE;
}

/* ================= control state via window extra =================
 * Built-in classes reserve cbWndExtra bytes: [0..3] font handle,
 * [4..7] state pointer. Apps cannot collide: these classes are
 * ours, and GWLP_USERDATA lives elsewhere. */

static CE_HANDLE ctl_font(ce_window *w)
{
    if (w && w->extra) {
        CE_LONG v = 0;
        memcpy(&v, w->extra, sizeof(v));
        if (v)
            return (CE_HANDLE)(uintptr_t)v;
    }
    return ce_GetStockObject(13 /* SYSTEM_FONT */);
}

static void ctl_set_font(ce_window *w, CE_HANDLE f)
{
    if (w && w->extra) {
        CE_LONG v = (CE_LONG)(uintptr_t)f;
        memcpy(w->extra, &v, sizeof(v));
    }
}

static void *ctl_state(ce_window *w)
{
    if (w && w->extra) {
        CE_LONG v = 0;
        memcpy(&v, w->extra + 4, sizeof(v));
        return (void *)(uintptr_t)v;
    }
    return NULL;
}

static void ctl_set_state(ce_window *w, void *s)
{
    if (w && w->extra) {
        CE_LONG v = (CE_LONG)(uintptr_t)s;
        memcpy(w->extra + 4, &v, sizeof(v));
    }
}

static void ctl_paint_bg(ce_window *w, CE_HANDLE dc, CE_DWORD color_idx)
{
    CE_RECT r = { 0, 0, gwe_win_w(w), gwe_win_h(w) };
    ce_FillRect(dc, &r, ce_GetSysColorBrush((int)color_idx));
}

static void ctl_draw_text(ce_window *w, CE_HANDLE dc, const CE_RECT *r,
                          CE_DWORD flags)
{
    if (!w->text)
        return;
    CE_HANDLE f = ctl_font(w);
    CE_HANDLE old = ce_SelectObject(dc, f);
    ce_SetBkMode(dc, CE_TRANSPARENT_BK);
    CE_RECT rr = *r;
    ce_DrawTextW(dc, w->text, -1, &rr, flags);
    ce_SelectObject(dc, old);
}

/* ================= Button ================= */

typedef struct {
    int check;      /* BST_* */
    int pushed;
    int tracking;
} ce_btn_state;

static void btn_notify(ce_window *w, int code)
{
    if (w->parent)
        gwe_enqueue((CE_HANDLE)w->parent, CE_WM_COMMAND,
                    (CE_DWORD)(((CE_DWORD)code << 16)
                               | (CE_DWORD)(uint16_t)
                                 (int)(intptr_t)w->menu_or_id),
                    (CE_LONG)(CE_HANDLE)w);
}

static void btn_set_check(ce_window *w, ce_btn_state *st, int v)
{
    CE_DWORD type = w->style & CE_BS_TYPEMASK;
    if (type == CE_BS_AUTORADIOBUTTON && v == CE_BST_CHECKED
        && w->parent) {
        /* uncheck the rest of the group */
        for (ce_window *s = w->parent->first_child; s;
             s = s->next_sibling) {
            if (s != w
                && (s->style & CE_BS_TYPEMASK) == CE_BS_AUTORADIOBUTTON) {
                ce_btn_state *ss = ctl_state(s);
                if (ss && ss->check != CE_BST_UNCHECKED) {
                    ss->check = CE_BST_UNCHECKED;
                    gwe_invalidate(s, NULL, 1);
                }
            }
        }
    }
    st->check = v;
    gwe_invalidate(w, NULL, 1);
}

static void btn_click(ce_window *w, ce_btn_state *st)
{
    CE_DWORD type = w->style & CE_BS_TYPEMASK;
    switch (type) {
    case CE_BS_AUTOCHECKBOX:
        btn_set_check(w, st,
                      st->check == CE_BST_CHECKED
                          ? CE_BST_UNCHECKED : CE_BST_CHECKED);
        break;
    case CE_BS_AUTO3STATE:
        btn_set_check(w, st, (st->check + 1) % 3);
        break;
    case CE_BS_AUTORADIOBUTTON:
        btn_set_check(w, st, CE_BST_CHECKED);
        break;
    default:
        break;
    }
    btn_notify(w, CE_BN_CLICKED);
}

static CE_LONG ce_button_proc(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                              CE_LONG lp)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return 0;
    ce_btn_state *st = ctl_state(w);
    switch (msg) {
    case CE_WM_NCCREATE:
        return 1;
    case CE_WM_CREATE: {
        ce_btn_state *s = calloc(1, sizeof(*s));
        ctl_set_state(w, s);
        return 0;
    }
    case CE_WM_NCDESTROY: {
        void *s = ctl_state(w);
        free(s);
        ctl_set_state(w, NULL);
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    }
    case CE_WM_SETFONT:
        ctl_set_font(w, (CE_HANDLE)wp);
        if (lp)
            gwe_invalidate(w, NULL, 1);
        return 0;
    case CE_WM_GETFONT:
        return (CE_LONG)ctl_font(w);
    case CE_BM_GETCHECK:
        return st ? st->check : 0;
    case CE_BM_SETCHECK:
        if (st)
            btn_set_check(w, st, (int)wp);
        return 0;
    case CE_BM_GETSTATE:
        return st && st->pushed ? 4 /* BST_PUSHED */ : 0;
    case CE_BM_SETSTATE:
        if (st) {
            st->pushed = wp ? 1 : 0;
            gwe_invalidate(w, NULL, 1);
        }
        return 0;
    case CE_BM_CLICK:
        if (st)
            btn_click(w, st);
        return 0;
    case CE_WM_GETDLGCODE: {
        CE_DWORD type = w->style & CE_BS_TYPEMASK;
        CE_LONG r = CE_DLGC_BUTTON;
        if (type == CE_BS_DEFPUSHBUTTON)
            r |= CE_DLGC_DEFPUSHBUTTON;
        else if (type == CE_BS_PUSHBUTTON)
            r |= CE_DLGC_UNDEFPUSHBUTTON;
        if (type == CE_BS_AUTORADIOBUTTON
            || type == CE_BS_RADIOBUTTON)
            r |= CE_DLGC_WANTARROWS;
        return r;
    }
    case CE_WM_LBUTTONDOWN:
        if (w->enabled) {
            ce_SetFocus(hwnd);
            if (st) {
                st->pushed = 1;
                st->tracking = 1;
            }
            ce_SetCapture(hwnd);
            gwe_invalidate(w, NULL, 1);
        }
        return 0;
    case CE_WM_LBUTTONUP:
        if (st && st->tracking) {
            st->tracking = 0;
            st->pushed = 0;
            ce_ReleaseCapture();
            CE_POINT pt = { (CE_LONG)(short)(lp & 0xFFFF),
                            (CE_LONG)(short)((lp >> 16) & 0xFFFF) };
            CE_RECT cr = { 0, 0, gwe_win_w(w), gwe_win_h(w) };
            gwe_invalidate(w, NULL, 1);
            if (ce_PtInRect(&cr, pt))
                btn_click(w, st);
        }
        return 0;
    case CE_WM_KEYDOWN:
        if (wp == CE_VK_SPACE && st) {
            st->pushed = 1;
            gwe_invalidate(w, NULL, 1);
        }
        return 0;
    case CE_WM_KEYUP:
        if (wp == CE_VK_SPACE && st && st->pushed) {
            st->pushed = 0;
            gwe_invalidate(w, NULL, 1);
            btn_click(w, st);
        }
        return 0;
    case CE_WM_SETTEXT:
        ce_DefWindowProcW(hwnd, msg, wp, lp);
        gwe_invalidate(w, NULL, 1);
        return 0;
    case CE_WM_PAINT: {
        uint8_t psbuf[64];
        CE_HANDLE dc = ce_BeginPaint(hwnd, psbuf);
        if (!dc)
            return 0;
        CE_DWORD type = w->style & CE_BS_TYPEMASK;
        int W = gwe_win_w(w), H = gwe_win_h(w);
        if (type == CE_BS_GROUPBOX) {
            ctl_paint_bg(w, dc, CE_COLOR_BTNFACE);
            CE_HANDLE pen = ce_CreatePen(0, 1,
                                         ce_GetSysColor(16 /* BTNSHADOW */));
            CE_HANDLE oldp = ce_SelectObject(dc, pen);
            ce_Rectangle(dc, 0, 6, W, H);
            ce_SelectObject(dc, oldp);
            ce_DeleteObject(pen);
            if (w->text) {
                CE_RECT tr = { 8, 0, W - 8, 14 };
                ce_SetTextColor(dc, ce_GetSysColor(18 /* BTNTEXT */));
                ctl_draw_text(w, dc, &tr,
                              CE_DT_LEFT | CE_DT_SINGLELINE);
            }
            ce_EndPaint(hwnd, psbuf);
            return 0;
        }
        int check_type = (type == CE_BS_CHECKBOX
                          || type == CE_BS_AUTOCHECKBOX
                          || type == CE_BS_RADIOBUTTON
                          || type == CE_BS_AUTORADIOBUTTON
                          || type == CE_BS_3STATE
                          || type == CE_BS_AUTO3STATE);
        ctl_paint_bg(w, dc, CE_COLOR_BTNFACE);
        CE_DWORD face = ce_GetSysColor(CE_COLOR_BTNFACE);
        CE_DWORD shadow = ce_GetSysColor(16);
        CE_DWORD hilite = ce_GetSysColor(20 /* BTNHIGHLIGHT */);
        CE_DWORD dark = ce_GetSysColor(21 /* 3DDKSHADOW */);
        int x0 = 0;
        if (check_type) {
            int box = 13;
            int bx = 1, by = (H - box) / 2;
            if (by < 0)
                by = 0;
            CE_RECT br = { bx, by, bx + box, by + box };
            ce_FillRect(dc, &br,
                        ce_GetSysColorBrush(5 /* COLOR_WINDOW */));
            CE_HANDLE pen = ce_CreatePen(0, 1, shadow);
            CE_HANDLE oldp = ce_SelectObject(dc, pen);
            ce_Rectangle(dc, bx, by, bx + box, by + box);
            ce_SelectObject(dc, oldp);
            ce_DeleteObject(pen);
            int radio = (type == CE_BS_RADIOBUTTON
                         || type == CE_BS_AUTORADIOBUTTON);
            if (st && st->check == CE_BST_CHECKED) {
                CE_RECT mr = radio
                    ? (CE_RECT){ bx + 4, by + 4, bx + box - 4,
                                 by + box - 4 }
                    : (CE_RECT){ bx + 2, by + 2, bx + box - 2,
                                 by + box - 2 };
                CE_HANDLE mb = ce_CreateSolidBrush(
                    ce_GetSysColor(18 /* BTNTEXT */));
                ce_FillRect(dc, &mr, mb);
                ce_DeleteObject(mb);
            } else if (st && st->check == CE_BST_INDETERMINATE) {
                CE_RECT mr = { bx + 2, by + 2, bx + box - 2,
                               by + box - 2 };
                CE_HANDLE mb = ce_CreateSolidBrush(shadow);
                ce_FillRect(dc, &mr, mb);
                ce_DeleteObject(mb);
            }
            x0 = box + 5;
        } else {
            /* push button face + border */
            CE_RECT fr = { 0, 0, W, H };
            CE_HANDLE fb = ce_CreateSolidBrush(face);
            ce_FillRect(dc, &fr, fb);
            ce_DeleteObject(fb);
            CE_DWORD tl = (st && st->pushed) ? dark : hilite;
            CE_DWORD brc = (st && st->pushed) ? hilite : shadow;
            CE_HANDLE p1 = ce_CreatePen(0, 1, tl);
            CE_HANDLE oldp = ce_SelectObject(dc, p1);
            ce_Rectangle(dc, 0, 0, W, H);
            ce_SelectObject(dc, oldp);
            ce_DeleteObject(p1);
            CE_HANDLE p2 = ce_CreatePen(0, 1, brc);
            oldp = ce_SelectObject(dc, p2);
            ce_Rectangle(dc, 1, 1, W - 1, H - 1);
            ce_SelectObject(dc, oldp);
            ce_DeleteObject(p2);
            if (type == CE_BS_DEFPUSHBUTTON) {
                CE_HANDLE p3 = ce_CreatePen(0, 1,
                                             ce_GetSysColor(18));
                oldp = ce_SelectObject(dc, p3);
                ce_Rectangle(dc, 0, 0, W, H);
                ce_SelectObject(dc, oldp);
                ce_DeleteObject(p3);
            }
        }
        if (w->text) {
            CE_RECT tr = { x0 + 2, 0, W - 2, H };
            if (!check_type && st && st->pushed) {
                tr.left += 1;
                tr.top += 1;
                tr.right += 1;
                tr.bottom += 1;
            }
            ce_SetTextColor(dc, ce_GetSysColor(
                w->enabled ? 18 : 17 /* BTNTEXT / GRAYTEXT */));
            ctl_draw_text(w, dc, &tr,
                          (check_type ? CE_DT_LEFT : CE_DT_CENTER)
                              | CE_DT_VCENTER | CE_DT_SINGLELINE);
        }
        if (ce_GetFocus() == hwnd && !check_type) {
            CE_RECT fr = { 3, 3, W - 3, H - 3 };
            CE_HANDLE pen = ce_CreatePen(2 /* PS_DOT */, 1,
                                         ce_GetSysColor(18));
            CE_HANDLE oldp = ce_SelectObject(dc, pen);
            ce_Rectangle(dc, fr.left, fr.top, fr.right, fr.bottom);
            ce_SelectObject(dc, oldp);
            ce_DeleteObject(pen);
        }
        ce_EndPaint(hwnd, psbuf);
        return 0;
    }
    default:
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    }
}

/* ================= Edit ================= */

typedef struct {
    int caret;          /* offset into text */
    int sel_anchor;     /* selection anchor offset; == caret: none */
    int goal_x;
} ce_edit_state;

static void edit_notify(ce_window *w, int code)
{
    if (w->parent)
        gwe_enqueue((CE_HANDLE)w->parent, CE_WM_COMMAND,
                    (CE_DWORD)(((CE_DWORD)code << 16)
                               | (CE_DWORD)(uint16_t)
                                 (int)(intptr_t)w->menu_or_id),
                    (CE_LONG)(CE_HANDLE)w);
}

static int edit_text_len(ce_window *w)
{
    return w->text ? (int)ce_wcslen(w->text) : 0;
}

static void edit_replace_sel(ce_window *w, ce_edit_state *st,
                             const CE_WCHAR *ins, int ins_len)
{
    int len = edit_text_len(w);
    int a = st->sel_anchor < st->caret ? st->sel_anchor : st->caret;
    int b = st->sel_anchor < st->caret ? st->caret : st->sel_anchor;
    if (a < 0)
        a = 0;
    if (b > len)
        b = len;
    int nlen = len - (b - a) + ins_len;
    CE_WCHAR *nt = malloc((size_t)(nlen + 1) * sizeof(CE_WCHAR));
    if (!nt)
        return;
    if (a)
        memcpy(nt, w->text, (size_t)a * sizeof(CE_WCHAR));
    if (ins_len)
        memcpy(nt + a, ins, (size_t)ins_len * sizeof(CE_WCHAR));
    if (len - b)
        memcpy(nt + a + ins_len, w->text + b,
               (size_t)(len - b) * sizeof(CE_WCHAR));
    nt[nlen] = 0;
    edit_notify(w, CE_EN_UPDATE);
    ce_SetWindowTextW((CE_HANDLE)w, nt);
    free(nt);
    st->caret = a + ins_len;
    st->sel_anchor = st->caret;
    edit_notify(w, CE_EN_CHANGE);
    gwe_invalidate(w, NULL, 1);
}

static CE_LONG ce_edit_proc(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                            CE_LONG lp)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return 0;
    ce_edit_state *st = ctl_state(w);
    switch (msg) {
    case CE_WM_NCCREATE:
        return 1;
    case CE_WM_CREATE: {
        ce_edit_state *s = calloc(1, sizeof(*s));
        ctl_set_state(w, s);
        return 0;
    }
    case CE_WM_NCDESTROY:
        free(ctl_state(w));
        ctl_set_state(w, NULL);
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    case CE_WM_SETFONT:
        ctl_set_font(w, (CE_HANDLE)wp);
        if (lp)
            gwe_invalidate(w, NULL, 1);
        return 0;
    case CE_WM_GETFONT:
        return (CE_LONG)ctl_font(w);
    case CE_WM_SETFOCUS:
        edit_notify(w, CE_EN_SETFOCUS);
        gwe_invalidate(w, NULL, 1);
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    case CE_WM_KILLFOCUS:
        edit_notify(w, CE_EN_KILLFOCUS);
        gwe_invalidate(w, NULL, 1);
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    case CE_WM_GETDLGCODE:
        return CE_DLGC_WANTARROWS | CE_DLGC_WANTCHARS
               | CE_DLGC_HASSETSEL;
    case CE_EM_GETSEL: {
        if (!st)
            return 0;
        int a = st->sel_anchor < st->caret ? st->sel_anchor : st->caret;
        int b = st->sel_anchor < st->caret ? st->caret : st->sel_anchor;
        if (wp)
            *(CE_DWORD *)wp = (CE_DWORD)a;
        if (lp)
            *(CE_DWORD *)lp = (CE_DWORD)b;
        return (CE_LONG)(((CE_DWORD)(uint16_t)b << 16)
                          | (CE_DWORD)(uint16_t)a);
    }
    case CE_EM_SETSEL: {
        if (!st)
            return 0;
        int len = edit_text_len(w);
        int a = (int)(CE_LONG)lp == -1 && (int)wp == -1 ? len
                : (int)wp;
        int b = (int)(CE_LONG)lp;
        if ((int)wp == -1) {
            st->sel_anchor = len;
            st->caret = len;
        } else {
            if (a < 0) a = 0;
            if (a > len) a = len;
            if (b < 0) b = len;
            if (b > len) b = len;
            st->sel_anchor = a;
            st->caret = b;
        }
        (void)b;
        gwe_invalidate(w, NULL, 1);
        return 0;
    }
    case CE_EM_REPLACESEL:
        if (st && !(w->style & CE_ES_READONLY))
            edit_replace_sel(w, st, (const CE_WCHAR *)lp,
                             (int)ce_wcslen((const CE_WCHAR *)lp));
        return 0;
    case CE_EM_GETLINECOUNT: {
        int n = 1;
        if (w->text)
            for (const CE_WCHAR *p = w->text; *p; p++)
                if (*p == '\n')
                    n++;
        return n;
    }
    case CE_EM_LINEINDEX: {
        int line = (int)wp;
        if (line < 0) {
            /* line containing the caret */
            if (!st)
                return 0;
            line = 0;
            if (w->text)
                for (int i = 0; i < st->caret && w->text[i]; i++)
                    if (w->text[i] == '\n')
                        line++;
        }
        int cur = 0, idx = 0;
        if (w->text) {
            while (w->text[idx] && cur < line) {
                if (w->text[idx] == '\n')
                    cur++;
                idx++;
            }
        }
        return cur == line ? idx : -1;
    }
    case CE_EM_LINELENGTH: {
        int idx = (int)wp;
        if (!w->text)
            return 0;
        int n = 0;
        while (w->text[idx + n] && w->text[idx + n] != '\n'
               && w->text[idx + n] != '\r')
            n++;
        return n;
    }
    case CE_EM_GETLINE: {
        if (!lp || !w->text)
            return 0;
        CE_WCHAR *buf = (CE_WCHAR *)lp;
        int maxw = buf[0];
        int line = (int)wp;
        int idx = (int)ce_SendMessageW(hwnd, CE_EM_LINEINDEX,
                                       (CE_DWORD)line, 0);
        if (idx < 0)
            return 0;
        int n = 0;
        while (w->text[idx + n] && w->text[idx + n] != '\n'
               && n < maxw) {
            buf[n] = w->text[idx + n];
            n++;
        }
        return n;
    }
    case CE_EM_SETREADONLY:
        if (wp)
            w->style |= CE_ES_READONLY;
        else
            w->style &= ~CE_ES_READONLY;
        gwe_invalidate(w, NULL, 1);
        return 1;
    case CE_WM_CHAR: {
        if (!st || (w->style & CE_ES_READONLY))
            return 0;
        CE_WCHAR ch = (CE_WCHAR)wp;
        if (ch == '\b') {
            if (st->sel_anchor != st->caret) {
                edit_replace_sel(w, st, NULL, 0);
            } else if (st->caret > 0) {
                st->sel_anchor = st->caret - 1;
                edit_replace_sel(w, st, NULL, 0);
            }
            return 0;
        }
        if (ch == '\r') {
            if (w->style & CE_ES_MULTILINE) {
                CE_WCHAR nl = '\n';
                edit_replace_sel(w, st, &nl, 1);
            }
            return 0;
        }
        if (ch == '\t' || ch == 27)
            return 0;
        if (ch >= 0x20 || ch > 0x7E)
            edit_replace_sel(w, st, &ch, 1);
        return 0;
    }
    case CE_WM_KEYDOWN: {
        if (!st)
            return 0;
        int len = edit_text_len(w);
        int shift = 0;   /* shift state is not in the queue entry */
        int moved = 1;
        switch (wp) {
        case 0x25: /* VK_LEFT */
            if (st->caret > 0)
                st->caret--;
            break;
        case 0x27: /* VK_RIGHT */
            if (st->caret < len)
                st->caret++;
            break;
        case 0x24: /* VK_HOME */
            st->caret = 0;
            break;
        case 0x23: /* VK_END */
            st->caret = len;
            break;
        case 0x2E: /* VK_DELETE */
            if (!(w->style & CE_ES_READONLY)) {
                if (st->sel_anchor != st->caret) {
                    edit_replace_sel(w, st, NULL, 0);
                } else if (st->caret < len) {
                    st->sel_anchor = st->caret + 1;
                    edit_replace_sel(w, st, NULL, 0);
                }
            }
            break;
        default:
            moved = 0;
            break;
        }
        if (moved) {
            if (!shift)
                st->sel_anchor = st->caret;
            gwe_invalidate(w, NULL, 1);
            return 0;
        }
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    }
    case CE_WM_LBUTTONDOWN: {
        if (!st)
            return 0;
        ce_SetFocus(hwnd);
        /* caret from x: measure with the control font */
        CE_HANDLE dc = ce_GetDC(hwnd);
        int cx = 0;
        if (dc) {
            CE_HANDLE old = ce_SelectObject(dc, ctl_font(w));
            CE_TEXTMETRICW tm;
            int avg = 8;
            if (ce_GetTextMetricsW(dc, &tm) && tm.tmAveCharWidth > 0)
                avg = (int)tm.tmAveCharWidth;
            int x = (short)(lp & 0xFFFF);
            cx = avg ? (x + avg / 2) / avg : 0;
            ce_SelectObject(dc, old);
            ce_ReleaseDC(hwnd, dc);
        }
        int len = edit_text_len(w);
        if (cx > len)
            cx = len;
        st->caret = cx;
        st->sel_anchor = cx;
        gwe_invalidate(w, NULL, 1);
        return 0;
    }
    case CE_WM_SETTEXT: {
        CE_LONG r = ce_DefWindowProcW(hwnd, msg, wp, lp);
        if (st) {
            st->caret = edit_text_len(w);
            st->sel_anchor = st->caret;
        }
        gwe_invalidate(w, NULL, 1);
        return r;
    }
    case CE_WM_PAINT: {
        uint8_t psbuf[64];
        CE_HANDLE dc = ce_BeginPaint(hwnd, psbuf);
        if (!dc)
            return 0;
        int W = gwe_win_w(w), H = gwe_win_h(w);
        int readonly = (w->style & CE_ES_READONLY) || !w->enabled;
        ctl_paint_bg(w, dc,
                     readonly ? CE_COLOR_BTNFACE : 5 /* COLOR_WINDOW */);
        CE_HANDLE pen = ce_CreatePen(0, 1, ce_GetSysColor(16));
        CE_HANDLE oldp = ce_SelectObject(dc, pen);
        ce_Rectangle(dc, 0, 0, W, H);
        ce_SelectObject(dc, oldp);
        ce_DeleteObject(pen);
        if (w->text && st) {
            CE_HANDLE f = ctl_font(w);
            CE_HANDLE old = ce_SelectObject(dc, f);
            CE_TEXTMETRICW tm;
            int lh = 16, avg = 8;
            if (ce_GetTextMetricsW(dc, &tm)) {
                lh = (int)(tm.tmHeight + tm.tmExternalLeading);
                if (tm.tmAveCharWidth > 0)
                    avg = (int)tm.tmAveCharWidth;
            }
            ce_SetBkMode(dc, CE_TRANSPARENT_BK);
            ce_SetTextColor(dc, ce_GetSysColor(8 /* WINDOWTEXT */));
            int multiline = (w->style & CE_ES_MULTILINE) != 0;
            /* draw line by line; caret position tracked in chars */
            int pos = 0, line = 0;
            const CE_WCHAR *t = w->text;
            int caret_x = 2, caret_y = 1;
            int sel_a = st->sel_anchor < st->caret
                        ? st->sel_anchor : st->caret;
            int sel_b = st->sel_anchor < st->caret
                        ? st->caret : st->sel_anchor;
            while (1) {
                int eol = pos;
                while (t[eol] && t[eol] != '\n')
                    eol++;
                int linelen = eol - pos;
                if (sel_b > sel_a && sel_a >= pos && sel_a <= eol) {
                    int sx = 2 + (sel_a - pos) * avg;
                    int ex = 2 + ((sel_b < eol ? sel_b : eol) - pos)
                             * avg;
                    CE_RECT sr = { sx, 1 + line * lh, ex,
                                   1 + (line + 1) * lh };
                    ce_FillRect(dc, &sr, ce_GetSysColorBrush(
                        13 /* COLOR_HIGHLIGHT */));
                }
                if (linelen) {
                    CE_WCHAR lbuf[512];
                    int cpl = linelen > 511 ? 511 : linelen;
                    memcpy(lbuf, t + pos,
                           (size_t)cpl * sizeof(CE_WCHAR));
                    lbuf[cpl] = 0;
                    ce_TextOutW(dc, 2, 1 + line * lh, lbuf, cpl);
                }
                if (st->caret >= pos && st->caret <= eol) {
                    caret_x = 2 + (st->caret - pos) * avg;
                    caret_y = 1 + line * lh;
                }
                if (!t[eol])
                    break;
                pos = eol + 1;
                line++;
                if (!multiline)
                    break;
            }
            (void)caret_y;
            if (ce_GetFocus() == hwnd) {
                CE_HANDLE cp = ce_CreatePen(
                    0, 1, ce_GetSysColor(8));
                CE_HANDLE op2 = ce_SelectObject(dc, cp);
                ce_Rectangle(dc, caret_x, caret_y, caret_x + 1,
                             caret_y + lh);
                ce_SelectObject(dc, op2);
                ce_DeleteObject(cp);
            }
            ce_SelectObject(dc, old);
        }
        ce_EndPaint(hwnd, psbuf);
        return 0;
    }
    default:
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    }
}

/* ================= Static ================= */

typedef struct {
    CE_HANDLE image;    /* icon/bitmap handle from STM_SETIMAGE */
    int       image_type;
} ce_static_state;

static CE_LONG ce_static_proc(CE_HANDLE hwnd, CE_DWORD msg, CE_DWORD wp,
                              CE_LONG lp)
{
    ce_window *w = gwe_win_from_handle(hwnd);
    if (!w)
        return 0;
    ce_static_state *st = ctl_state(w);
    switch (msg) {
    case CE_WM_NCCREATE:
        return 1;
    case CE_WM_CREATE:
        ctl_set_state(w, calloc(1, sizeof(ce_static_state)));
        return 0;
    case CE_WM_NCDESTROY:
        free(ctl_state(w));
        ctl_set_state(w, NULL);
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    case CE_WM_SETFONT:
        ctl_set_font(w, (CE_HANDLE)wp);
        if (lp)
            gwe_invalidate(w, NULL, 1);
        return 0;
    case CE_WM_GETFONT:
        return (CE_LONG)ctl_font(w);
    case CE_STM_SETIMAGE:
        if (st) {
            CE_HANDLE old = st->image;
            st->image = (CE_HANDLE)lp;
            st->image_type = (int)wp;
            gwe_invalidate(w, NULL, 1);
            return (CE_LONG)old;
        }
        return 0;
    case CE_STM_GETIMAGE:
        return st ? (CE_LONG)st->image : 0;
    case CE_STM_SETICON:
        if (st) {
            CE_HANDLE old = st->image;
            st->image = (CE_HANDLE)wp;
            st->image_type = CE_IMAGE_ICON;
            gwe_invalidate(w, NULL, 1);
            return (CE_LONG)old;
        }
        return 0;
    case CE_STM_GETICON:
        return st ? (CE_LONG)st->image : 0;
    case CE_WM_SETTEXT:
        ce_DefWindowProcW(hwnd, msg, wp, lp);
        gwe_invalidate(w, NULL, 1);
        return 0;
    case CE_WM_PAINT: {
        uint8_t psbuf[64];
        CE_HANDLE dc = ce_BeginPaint(hwnd, psbuf);
        if (!dc)
            return 0;
        int W = gwe_win_w(w), H = gwe_win_h(w);
        CE_DWORD style = w->style & 0x1F;
        ctl_paint_bg(w, dc, CE_COLOR_BTNFACE);
        if (style == CE_SS_BLACKFRAME || (w->style & CE_SS_SUNKEN)) {
            CE_HANDLE pen = ce_CreatePen(0, 1,
                                         ce_GetSysColor(18));
            CE_HANDLE oldp = ce_SelectObject(dc, pen);
            ce_Rectangle(dc, 0, 0, W, H);
            ce_SelectObject(dc, oldp);
            ce_DeleteObject(pen);
        }
        if (w->text && style != CE_SS_ICON && style != CE_SS_BITMAP) {
            CE_RECT tr = { 0, 0, W, H };
            CE_DWORD fl = CE_DT_WORDBREAK;
            if (style == CE_SS_CENTER)
                fl |= CE_DT_CENTER;
            else if (style == CE_SS_RIGHT)
                fl |= CE_DT_RIGHT;
            if (style == CE_SS_LEFTNOWORDWRAP
                || style == CE_SS_SIMPLE)
                fl = CE_DT_SINGLELINE | CE_DT_LEFT;
            ce_SetTextColor(dc, ce_GetSysColor(
                w->enabled ? 18 : 17));
            ctl_draw_text(w, dc, &tr, fl);
        }
        ce_EndPaint(hwnd, psbuf);
        return 0;
    }
    default:
        return ce_DefWindowProcW(hwnd, msg, wp, lp);
    }
}

/* ================= class registration ================= */

static int g_ctl_ready = 0;

static void reg_class(const char *name, CE_WNDPROC proc, int extra,
                      CE_HANDLE bg)
{
    CE_WCHAR wn[32];
    int i = 0;
    while (name[i] && i < 31) {
        wn[i] = (CE_WCHAR)name[i];
        i++;
    }
    wn[i] = 0;
    CE_WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.cbWndExtra = extra;
    wc.hbrBackground = bg;
    wc.lpszClassName = wn;
    ce_RegisterClassW(&wc);
}

void ce_dialog_register_classes(void)
{
    if (g_ctl_ready)
        return;
    g_ctl_ready = 1;
    reg_class("Button", ce_button_proc, 8, NULL);
    reg_class("Edit", ce_edit_proc, 8,
              ce_GetSysColorBrush(5 /* COLOR_WINDOW */));
    reg_class("Static", ce_static_proc, 8, NULL);
    reg_class("#32770", ce_dlg_wndproc, CE_DLGWINDOWEXTRA,
              ce_GetSysColorBrush(CE_COLOR_BTNFACE));
}

/* ================= hot keys + keybd_event ================= */

#define CE_MAX_HOTKEYS 32
typedef struct {
    int       used;
    CE_HANDLE hwnd;
    int       id;
    CE_DWORD  mods;
    CE_DWORD  vk;
} ce_hotkey_rec;
static ce_hotkey_rec g_hotkeys[CE_MAX_HOTKEYS];
static CE_DWORD g_hk_mods_now = 0;

CE_BOOL ce_RegisterHotKey(CE_HANDLE hwnd, int id, CE_DWORD mods,
                          CE_DWORD vk)
{
    for (int i = 0; i < CE_MAX_HOTKEYS; i++)
        if (g_hotkeys[i].used && g_hotkeys[i].hwnd == hwnd
            && g_hotkeys[i].id == id) {
            g_hotkeys[i].mods = mods;
            g_hotkeys[i].vk = vk;
            return CE_TRUE;
        }
    for (int i = 0; i < CE_MAX_HOTKEYS; i++)
        if (!g_hotkeys[i].used) {
            g_hotkeys[i].used = 1;
            g_hotkeys[i].hwnd = hwnd;
            g_hotkeys[i].id = id;
            g_hotkeys[i].mods = mods;
            g_hotkeys[i].vk = vk;
            return CE_TRUE;
        }
    ce_set_lasterror(CE_ERROR_NOT_ENOUGH_MEMORY);
    return CE_FALSE;
}

CE_BOOL ce_UnregisterHotKey(CE_HANDLE hwnd, int id)
{
    for (int i = 0; i < CE_MAX_HOTKEYS; i++)
        if (g_hotkeys[i].used && g_hotkeys[i].hwnd == hwnd
            && g_hotkeys[i].id == id) {
            g_hotkeys[i].used = 0;
            return CE_TRUE;
        }
    return CE_FALSE;
}

/* Key state for GetKeyState/GetAsyncKeyState: every key event
 * in the layer passes through ce_hotkey_notify, so the table
 * here is the layer's true key state. The latch bit is the
 * "pressed since the last GetAsyncKeyState" flag. */
static uint8_t g_key_down[256];
static uint8_t g_key_latched[256];

int ce_key_down_state(int vk)
{
    if (vk < 0 || vk > 255)
        return 0;
    return g_key_down[vk] ? 1 : 0;
}

int ce_key_pressed_latch(int vk)
{
    if (vk < 0 || vk > 255)
        return 0;
    int v = g_key_latched[vk] ? 1 : 0;
    g_key_latched[vk] = 0;
    return v;
}

/* Called from gwe.c's WM key input path with every key event.
 * Returns 1 when the key was consumed as a hot key. */
int ce_hotkey_notify(CE_DWORD vk, int is_down)
{
    if (vk < 256) {
        if (is_down && !g_key_down[vk])
            g_key_latched[vk] = 1;
        g_key_down[vk] = is_down ? 1 : 0;
    }
    CE_DWORD bit = 0;
    if (vk == CE_VK_CONTROL)
        bit = CE_MOD_CONTROL;
    else if (vk == CE_VK_MENU)
        bit = CE_MOD_ALT;
    else if (vk == CE_VK_SHIFT)
        bit = CE_MOD_SHIFT;
    if (bit) {
        if (is_down)
            g_hk_mods_now |= bit;
        else
            g_hk_mods_now &= ~bit;
        return 0;
    }
    if (!is_down)
        return 0;
    for (int i = 0; i < CE_MAX_HOTKEYS; i++) {
        if (g_hotkeys[i].used && g_hotkeys[i].vk == vk
            && (g_hotkeys[i].mods & (CE_MOD_ALT | CE_MOD_CONTROL
                                     | CE_MOD_SHIFT))
               == (g_hk_mods_now & (CE_MOD_ALT | CE_MOD_CONTROL
                                    | CE_MOD_SHIFT))) {
            gwe_enqueue(g_hotkeys[i].hwnd, CE_WM_HOTKEY,
                        (CE_DWORD)g_hotkeys[i].id,
                        (CE_LONG)((g_hk_mods_now & 0xFFFF)
                                  | (vk << 16)));
            return 1;
        }
    }
    return 0;
}

void ce_keybd_event(uint8_t vk, uint8_t scan, CE_DWORD flags,
                    CE_DWORD extra)
{
    (void)scan;
    (void)extra;
    CE_HANDLE target = g_focus_win;
    if (!target)
        return;
    int up = (flags & CE_KEYEVENTF_KEYUP) != 0;
    CE_DWORD msg = up ? CE_WM_KEYUP : CE_WM_KEYDOWN;
    CE_LONG lp = 1;
    if (up)
        lp |= (CE_LONG)(3u << 30);
    gwe_enqueue(target, msg, vk, lp);
    if (!up) {
        /* printable keys also produce WM_CHAR, mirroring what
         * TranslateMessage would do with the queued key */
        CE_WCHAR ch = 0;
        if (vk >= 'A' && vk <= 'Z')
            ch = (CE_WCHAR)(vk - 'A' + 'a');
        else if (vk >= '0' && vk <= '9')
            ch = (CE_WCHAR)vk;
        else if (vk == CE_VK_SPACE)
            ch = ' ';
        else if (vk == 0x0D)
            ch = '\r';
        if (ch)
            gwe_enqueue(target, CE_WM_CHAR, ch, lp);
    }
}

/* ---- GetNextDlgTabItem (M3) ---- */

CE_HANDLE ce_GetNextDlgTabItem(CE_HANDLE dlg, CE_HANDLE cur,
                               CE_BOOL previous)
{
    ce_window *d = gwe_win_from_handle(dlg);
    if (!d)
        return NULL;
    /* collect children in list order */
    ce_window *kids[128];
    int n = 0;
    for (ce_window *c = d->first_child; c && n < 128;
         c = c->next_sibling)
        kids[n++] = c;
    if (n == 0)
        return NULL;
    int start = -1;
    if (cur) {
        ce_window *cw = gwe_win_from_handle(cur);
        for (int i = 0; i < n; i++)
            if (kids[i] == cw) {
                start = i;
                break;
            }
    }
    for (int step = 1; step <= n; step++) {
        int ix;
        if (start < 0)
            ix = previous ? n - step : step - 1;
        else
            ix = previous ? (start - step % n + n * 2) % n
                          : (start + step) % n;
        ce_window *c = kids[ix];
        if ((c->style & CE_WS_TABSTOP) && c->visible && c->enabled)
            return (CE_HANDLE)c;
    }
    return NULL;
}
