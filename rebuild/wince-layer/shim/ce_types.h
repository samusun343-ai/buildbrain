/* BrainCE Layer -- CE ABI types for the coredll shim.
 *
 * The CE side is compiled with -fshort-wchar, so CE WCHAR is 16 bit.
 * The shim is a Linux binary; to keep the ABI boundary exact we never
 * use the host wchar_t in shim prototypes -- CE strings are uint16_t
 * arrays everywhere.
 */
#ifndef BRAINCE_CE_TYPES_H
#define BRAINCE_CE_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint16_t CE_WCHAR;
typedef uint32_t CE_DWORD;
typedef int32_t  CE_BOOL;
typedef int32_t  CE_LONG;
typedef uint32_t CE_ULONG;
typedef void    *CE_HANDLE;
typedef void    *CE_HMODULE;
typedef CE_DWORD CE_LRESULT;

#define CE_TRUE  1
#define CE_FALSE 0
#define CE_INVALID_HANDLE_VALUE ((CE_HANDLE)(intptr_t)-1)
#define CE_INVALID_FILE_ATTRIBUTES ((CE_DWORD)-1)
#define CE_INVALID_SET_FILE_POINTER ((CE_DWORD)-1)

typedef struct {
    CE_DWORD dwLowDateTime;
    CE_DWORD dwHighDateTime;
} CE_FILETIME;

/* WIN32_FIND_DATAW as laid out by WinCE (cFileName is MAX_PATH=260
 * UTF-16 units, cAlternateFileName 14). 592 bytes total. */
typedef struct {
    CE_DWORD  dwFileAttributes;
    CE_FILETIME ftCreationTime;
    CE_FILETIME ftLastAccessTime;
    CE_FILETIME ftLastWriteTime;
    CE_DWORD  nFileSizeHigh;
    CE_DWORD  nFileSizeLow;
    CE_DWORD  dwReserved0;
    CE_DWORD  dwReserved1;
    CE_WCHAR  cFileName[260];
    CE_WCHAR  cAlternateFileName[14];
} CE_WIN32_FIND_DATAW;

/* CE error codes used by the M1 set (values from the public Win32
 * error table, which CE shares). */
#define CE_ERROR_SUCCESS           0
#define CE_ERROR_FILE_NOT_FOUND    2
#define CE_ERROR_PATH_NOT_FOUND    3
#define CE_ERROR_TOO_MANY_OPEN_FILES 4
#define CE_ERROR_ACCESS_DENIED     5
#define CE_ERROR_INVALID_HANDLE    6
#define CE_ERROR_NOT_ENOUGH_MEMORY 8
#define CE_ERROR_INVALID_PARAMETER 87
#define CE_ERROR_MOD_NOT_FOUND     126
#define CE_ERROR_FILE_EXISTS       80
#define CE_ERROR_ALREADY_EXISTS    183
#define CE_ERROR_DIR_NOT_EMPTY     145
#define CE_ERROR_DIRECTORY         267

/* CreateFileW constants */
#define CE_GENERIC_READ    0x80000000u
#define CE_GENERIC_WRITE   0x40000000u
#define CE_CREATE_NEW      1
#define CE_CREATE_ALWAYS   2
#define CE_OPEN_EXISTING   3
#define CE_OPEN_ALWAYS     4
#define CE_TRUNCATE_EXISTING 5

#define CE_FILE_ATTRIBUTE_READONLY  0x00000001
#define CE_FILE_ATTRIBUTE_HIDDEN    0x00000002
#define CE_FILE_ATTRIBUTE_SYSTEM    0x00000004
#define CE_FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define CE_FILE_ATTRIBUTE_ARCHIVE   0x00000020
#define CE_FILE_ATTRIBUTE_NORMAL    0x00000080

#define CE_FILE_BEGIN   0
#define CE_FILE_CURRENT 1
#define CE_FILE_END     2

/* LocalAlloc flags */
#define CE_LMEM_FIXED    0x0000
#define CE_LMEM_MOVEABLE 0x0002
#define CE_LMEM_ZEROINIT 0x0040
#define CE_LPTR          0x0040

/* VirtualAlloc flags */
#define CE_MEM_COMMIT    0x1000
#define CE_MEM_RESERVE   0x2000
#define CE_MEM_DECOMMIT  0x4000
#define CE_MEM_RELEASE   0x8000
#define CE_PAGE_EXECUTE_READWRITE 0x40

/* thread-local last error, CE semantics */
extern __thread CE_DWORD ce_tls_lasterror;
static inline void ce_set_lasterror(CE_DWORD e) { ce_tls_lasterror = e; }

/* ================= GWE (M2) types and constants =================
 * All values are the published Win32/WinCE constants (Microsoft
 * Learn, Windows CE 6.0 documentation). Struct layouts are the
 * 32-bit ARM ABI layouts of the corresponding CE structures. */

typedef struct { CE_LONG x, y; } CE_POINT;
typedef struct { CE_LONG left, top, right, bottom; } CE_RECT;
typedef struct { CE_LONG cx, cy; } CE_SIZE;

/* TEXTMETRICW (public Win32/CE layout). */
typedef struct {
    CE_LONG tmHeight, tmAscent, tmDescent, tmInternalLeading;
    CE_LONG tmExternalLeading, tmAveCharWidth, tmMaxCharWidth;
    CE_LONG tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    CE_WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    uint8_t tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily;
    uint8_t tmCharSet;
} CE_TEXTMETRICW;

typedef struct {
    CE_HANDLE hwnd;
    CE_DWORD  message;
    CE_DWORD  wParam;
    CE_LONG   lParam;
    CE_DWORD  time;
    CE_POINT  pt;
} CE_MSG; /* 28 bytes */

typedef CE_LONG (*CE_WNDPROC)(CE_HANDLE, CE_DWORD, CE_DWORD, CE_LONG);

typedef struct {
    CE_DWORD   style;
    CE_WNDPROC lpfnWndProc;
    int        cbClsExtra;
    int        cbWndExtra;
    CE_HANDLE  hInstance;
    CE_HANDLE  hIcon;
    CE_HANDLE  hCursor;
    CE_HANDLE  hbrBackground;
    const CE_WCHAR *lpszMenuName;
    const CE_WCHAR *lpszClassName;
} CE_WNDCLASSW; /* 40 bytes */

typedef struct {
    void     *lpCreateParams;
    CE_HANDLE hInstance;
    CE_HANDLE hMenu;
    CE_HANDLE hwndParent;
    int cy, cx, y, x;
    CE_LONG   style;
    const CE_WCHAR *lpszName;
    const CE_WCHAR *lpszClass;
    CE_DWORD  dwExStyle;
} CE_CREATESTRUCTW; /* 52 bytes */

typedef struct {
    CE_HANDLE hdc;
    CE_BOOL   fErase;
    CE_RECT   rcPaint;
    CE_BOOL   fRestore;
    CE_BOOL   fIncUpdate;
    uint8_t   rgbReserved[32];
} CE_PAINTSTRUCT; /* 64 bytes */

/* LOGFONTW: 92 bytes. lfFaceName is 32 UTF-16 units (LF_FACESIZE). */
typedef struct {
    CE_LONG lfHeight;
    CE_LONG lfWidth;
    CE_LONG lfEscapement;
    CE_LONG lfOrientation;
    CE_LONG lfWeight;
    uint8_t lfItalic;
    uint8_t lfUnderline;
    uint8_t lfStrikeOut;
    uint8_t lfCharSet;
    uint8_t lfOutPrecision;
    uint8_t lfClipPrecision;
    uint8_t lfQuality;
    uint8_t lfPitchAndFamily;
    CE_WCHAR lfFaceName[32];
} CE_LOGFONTW;

/* BITMAP as returned by GetObjectW on a bitmap handle: 24 bytes. */
typedef struct {
    CE_LONG bmType;
    CE_LONG bmWidth;
    CE_LONG bmHeight;
    CE_LONG bmWidthBytes;
    uint16_t bmPlanes;
    uint16_t bmBitsPixel;
    void    *bmBits;
} CE_BITMAP;

typedef struct {
    CE_DWORD biSize;
    CE_LONG  biWidth;
    CE_LONG  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    CE_DWORD biCompression;
    CE_DWORD biSizeImage;
    CE_LONG  biXPelsPerMeter;
    CE_LONG  biYPelsPerMeter;
    CE_DWORD biClrUsed;
    CE_DWORD biClrImportant;
} CE_BITMAPINFOHEADER; /* 40 bytes */

typedef struct {
    CE_DWORD lbStyle;
    CE_DWORD lbColor;
    CE_LONG  lbHatch;
} CE_LOGBRUSH; /* 12 bytes */

typedef struct {
    CE_POINT lotPt;
    CE_DWORD lotStyle;
    CE_DWORD lotColor;
} CE_LOGPEN; /* 16 bytes (POINT is 8 bytes; order per SDK: style first
              * in LOGPEN is actually { UINT lopnStyle; POINT lopnWidth;
              * COLORREF lopnColor } -- see gdi.c GetObjectW) */

/* window messages (subset used by the M2 set) */
#define CE_WM_CREATE        0x0001
#define CE_WM_DESTROY       0x0002
#define CE_WM_MOVE          0x0003
#define CE_WM_SIZE          0x0005
#define CE_WM_ACTIVATE      0x0006
#define CE_WM_SETFOCUS      0x0007
#define CE_WM_KILLFOCUS     0x0008
#define CE_WM_ENABLE        0x000A
#define CE_WM_SETREDRAW     0x000B
#define CE_WM_SETTEXT       0x000C
#define CE_WM_GETTEXT       0x000D
#define CE_WM_GETTEXTLENGTH 0x000E
#define CE_WM_PAINT         0x000F
#define CE_WM_CLOSE         0x0010
#define CE_WM_QUIT          0x0012
#define CE_WM_ERASEBKGND    0x0014
#define CE_WM_SHOWWINDOW    0x0018
#define CE_WM_SETCURSOR     0x0020
#define CE_WM_GETMINMAXINFO 0x0024
#define CE_WM_WINDOWPOSCHANGED 0x0047
#define CE_WM_NCCREATE      0x0081
#define CE_WM_NCDESTROY     0x0082
#define CE_WM_NCCALCSIZE    0x0083
#define CE_WM_NCPAINT       0x0085
#define CE_WM_KEYDOWN       0x0100
#define CE_WM_KEYUP         0x0101
#define CE_WM_CHAR          0x0102
#define CE_WM_SYSKEYDOWN    0x0104
#define CE_WM_SYSKEYUP      0x0105
#define CE_WM_SYSCHAR       0x0106
#define CE_WM_COMMAND       0x0111
#define CE_WM_SYSCOMMAND    0x0112
#define CE_WM_TIMER         0x0113
#define CE_WM_MOUSEMOVE     0x0200
#define CE_WM_LBUTTONDOWN   0x0201
#define CE_WM_LBUTTONUP     0x0202
#define CE_WM_LBUTTONDBLCLK 0x0203
#define CE_WM_RBUTTONDOWN   0x0204
#define CE_WM_RBUTTONUP     0x0205
#define CE_WM_MBUTTONDOWN   0x0207
#define CE_WM_MBUTTONUP     0x0208
#define CE_WM_CAPTURECHANGED 0x0215
#define CE_WM_USER          0x0400
#define CE_WM_APP           0x8000

/* window styles */
#define CE_WS_POPUP         0x80000000u
#define CE_WS_CHILD         0x40000000u
#define CE_WS_MINIMIZE      0x20000000u
#define CE_WS_VISIBLE       0x10000000u
#define CE_WS_DISABLED      0x08000000u
#define CE_WS_CLIPSIBLINGS  0x04000000u
#define CE_WS_CLIPCHILDREN  0x02000000u
#define CE_WS_MAXIMIZE      0x01000000u
#define CE_WS_CAPTION       0x00C00000u
#define CE_WS_BORDER        0x00800000u
#define CE_WS_DLGFRAME      0x00400000u
#define CE_WS_VSCROLL       0x00200000u
#define CE_WS_HSCROLL       0x00100000u
#define CE_WS_SYSMENU       0x00080000u
#define CE_WS_THICKFRAME    0x00040000u
#define CE_WS_GROUP         0x00020000u
#define CE_WS_TABSTOP       0x00010000u
#define CE_WS_OVERLAPPEDWINDOW 0x00CF0000u
#define CE_CW_USEDEFAULT    ((int)0x80000000)

/* ShowWindow commands */
#define CE_SW_HIDE          0
#define CE_SW_SHOWNORMAL    1
#define CE_SW_SHOWMINIMIZED 2
#define CE_SW_SHOWMAXIMIZED 3
#define CE_SW_SHOWNOACTIVATE 4
#define CE_SW_SHOW          5
#define CE_SW_MINIMIZE      6
#define CE_SW_SHOWMINNOACTIVE 7
#define CE_SW_SHOWNA        8
#define CE_SW_RESTORE       9

/* GetWindowLong / SetWindowLong indices */
#define CE_GWL_WNDPROC   (-4)
#define CE_GWL_HINSTANCE (-6)
#define CE_GWL_HWNDPARENT (-8)
#define CE_GWL_ID        (-12)
#define CE_GWL_STYLE     (-16)
#define CE_GWL_EXSTYLE   (-20)
#define CE_GWL_USERDATA  (-21)

/* GetWindow commands */
#define CE_GW_HWNDFIRST 0
#define CE_GW_HWNDLAST  1
#define CE_GW_HWNDNEXT  2
#define CE_GW_HWNDPREV  3
#define CE_GW_OWNER     4
#define CE_GW_CHILD     5

/* SetWindowPos flags / special HWNDs */
#define CE_SWP_NOSIZE     0x0001
#define CE_SWP_NOMOVE     0x0002
#define CE_SWP_NOZORDER   0x0004
#define CE_SWP_NOACTIVATE 0x0010
#define CE_SWP_SHOWWINDOW 0x0040
#define CE_SWP_HIDEWINDOW 0x0080
#define CE_SWP_FRAMECHANGED 0x0020
#define CE_HWND_TOP       ((CE_HANDLE)0)
#define CE_HWND_BOTTOM    ((CE_HANDLE)1)
#define CE_HWND_TOPMOST   ((CE_HANDLE)-1)
#define CE_HWND_NOTOPMOST ((CE_HANDLE)-2)

/* virtual keys (subset) */
#define CE_VK_LBUTTON 0x01
#define CE_VK_RBUTTON 0x02
#define CE_VK_CANCEL  0x03
#define CE_VK_MBUTTON 0x04
#define CE_VK_BACK    0x08
#define CE_VK_TAB     0x09
#define CE_VK_RETURN  0x0D
#define CE_VK_SHIFT   0x10
#define CE_VK_CONTROL 0x11
#define CE_VK_MENU    0x12
#define CE_VK_ESCAPE  0x1B
#define CE_VK_SPACE   0x20
#define CE_VK_PRIOR   0x21
#define CE_VK_NEXT    0x22
#define CE_VK_END     0x23
#define CE_VK_HOME    0x24
#define CE_VK_LEFT    0x25
#define CE_VK_UP      0x26
#define CE_VK_RIGHT   0x27
#define CE_VK_DOWN    0x28
#define CE_VK_INSERT  0x2D
#define CE_VK_DELETE  0x2E
#define CE_MK_LBUTTON 0x0001
#define CE_MK_RBUTTON 0x0002
#define CE_MK_SHIFT   0x0004
#define CE_MK_CONTROL 0x0008
#define CE_MK_MBUTTON 0x0010

/* class styles */
#define CE_CS_VREDRAW  0x0001
#define CE_CS_HREDRAW  0x0002
#define CE_CS_DBLCLKS  0x0008
#define CE_CS_GLOBALCLASS 0x4000

/* GDI constants */
#define CE_SRCCOPY     0x00CC0020u
#define CE_SRCPAINT    0x00EE0086u
#define CE_SRCAND      0x008800C6u
#define CE_SRCINVERT   0x00660046u
#define CE_SRCERASE    0x00440328u
#define CE_NOTSRCCOPY  0x00330008u
#define CE_NOTSRCERASE 0x001100A6u
#define CE_MERGEPAINT  0x00BB0226u
#define CE_MERGECOPY   0x00C000CAu
#define CE_PATINVERT   0x005A0049u
#define CE_PATCOPY     0x00F00021u
#define CE_PATPAINT    0x00FB0A09u
#define CE_DSTINVERT   0x00550009u
#define CE_BLACKNESS   0x00000042u
#define CE_WHITENESS   0x00FF0062u

#define CE_PS_SOLID 0
#define CE_PS_DASH  1
#define CE_PS_DOT   2
#define CE_PS_NULL  5
#define CE_BS_SOLID 0
#define CE_BS_NULL  1
#define CE_BS_HOLLOW CE_BS_NULL
#define CE_TRANSPARENT 1
#define CE_OPAQUE      2

/* stock objects */
#define CE_WHITE_BRUSH   0
#define CE_LTGRAY_BRUSH  1
#define CE_GRAY_BRUSH    2
#define CE_DKGRAY_BRUSH  3
#define CE_BLACK_BRUSH   4
#define CE_NULL_BRUSH    5
#define CE_WHITE_PEN     6
#define CE_BLACK_PEN     7
#define CE_NULL_PEN      8
#define CE_OEM_FIXED_FONT 10
#define CE_ANSI_FIXED_FONT 11
#define CE_ANSI_VAR_FONT 12
#define CE_SYSTEM_FONT   13
#define CE_DEVICE_DEFAULT_FONT 14
#define CE_SYSTEM_FIXED_FONT 16
#define CE_DEFAULT_GUI_FONT 17

/* DrawText flags */
#define CE_DT_LEFT       0x00000000
#define CE_DT_TOP        0x00000000
#define CE_DT_CENTER     0x00000001
#define CE_DT_RIGHT      0x00000002
#define CE_DT_VCENTER    0x00000004
#define CE_DT_BOTTOM     0x00000008
#define CE_DT_WORDBREAK  0x00000010
#define CE_DT_SINGLELINE 0x00000020
#define CE_DT_NOCLIP     0x00000100
#define CE_DT_CALCRECT   0x00000400
#define CE_DT_NOPREFIX   0x00000800

#define CE_ETO_OPAQUE  0x0002
#define CE_ETO_CLIPPED 0x0004

/* GetDeviceCaps indices */
#define CE_HORZRES    8
#define CE_VERTRES    10
#define CE_BITSPIXEL  12
#define CE_PLANES     14
#define CE_NUMCOLORS  24
#define CE_LOGPIXELSX 88
#define CE_LOGPIXELSY 90

/* SystemMetrics indices (subset) */
#define CE_SM_CXSCREEN 0
#define CE_SM_CYSCREEN 1
#define CE_SM_CXVSCROLL 2
#define CE_SM_CYHSCROLL 3
#define CE_SM_CYCAPTION 4
#define CE_SM_CXBORDER 5
#define CE_SM_CYBORDER 6
#define CE_SM_CXDLGFRAME 7
#define CE_SM_CYDLGFRAME 8
#define CE_SM_CXFULLSCREEN 16
#define CE_SM_CYFULLSCREEN 17
#define CE_SM_CXFRAME 32
#define CE_SM_CYFRAME 33

/* system colors (subset) */
#define CE_COLOR_SCROLLBAR 0
#define CE_COLOR_BACKGROUND 1
#define CE_COLOR_ACTIVECAPTION 2
#define CE_COLOR_INACTIVECAPTION 3
#define CE_COLOR_MENU 4
#define CE_COLOR_WINDOW 5
#define CE_COLOR_WINDOWFRAME 6
#define CE_COLOR_MENUTEXT 7
#define CE_COLOR_WINDOWTEXT 8
#define CE_COLOR_CAPTIONTEXT 9
#define CE_COLOR_HIGHLIGHT 13
#define CE_COLOR_HIGHLIGHTTEXT 14
#define CE_COLOR_BTNFACE 15
#define CE_COLOR_BTNSHADOW 16
#define CE_COLOR_GRAYTEXT 17
#define CE_COLOR_BTNTEXT 18
#define CE_COLOR_BTNHIGHLIGHT 20
#define CE_COLOR_3DDKSHADOW 21
#define CE_COLOR_3DLIGHT 22
#define CE_COLOR_INFOTEXT 23
#define CE_COLOR_INFOBK 24
#define CE_COLOR_DESKTOP CE_COLOR_BACKGROUND

/* region combine modes */
#define CE_RGN_AND  1
#define CE_RGN_OR   2
#define CE_RGN_XOR  3
#define CE_RGN_DIFF 4
#define CE_RGN_COPY 5
#define CE_ERROR_RGN 0
#define CE_NULLREGION 1
#define CE_SIMPLEREGION 2
#define CE_COMPLEXREGION 3

/* extra error codes */
#define CE_ERROR_CLASS_ALREADY_EXISTS 1410
#define CE_ERROR_CLASS_DOES_NOT_EXIST 1411
#define CE_ERROR_CLASS_HAS_WINDOWS    1412
#define CE_ERROR_INVALID_INDEX        1413
#define CE_ERROR_INVALID_WINDOW_HANDLE 1400

#define CE_SIZE_RESTORED 0
#define CE_SIZE_MINIMIZED 1
#define CE_SIZE_MAXIMIZED 2

#define CE_SPI_GETWORKAREA 0x0030

#endif
