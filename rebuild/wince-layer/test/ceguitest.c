/* BrainCE Layer M2 test binary (DESIGN-wince-layer.md section 9.2).
 *
 * Freestanding WinCE PE built with clang -target armv5te-none-eabi
 * and pe-link47.py; imports only COREDLL.dll by ordinal (~30 GWE +
 * file functions, P47App-class). Behaviour:
 *
 *  - registers a window class, creates a 400x300 top-level window,
 *    shows it, posts itself a WM_USER message and sends itself
 *    another (both dispatch paths), and runs a real message loop;
 *  - WM_PAINT draws a deterministic scene (white background, blue
 *    band, red block, green rectangle with pen border, black text
 *    "BrainCE M2", one magenta SetPixel, a yellow 50x50 block
 *    BitBlt-ed from a memory DC) and self-verifies three pixels with
 *    GetPixel; a GetDC/SetPixel path adds one cyan pixel;
 *  - a 100 ms timer fires 8 times, then the app writes its message
 *    log to \CE-TEST\guilog.txt through the M1 file API and destroys
 *    the window; WM_DESTROY writes the final log and quits.
 *
 * If \CE-TEST\GHOLD exists, the log is written at timer 8 but the
 * window stays open and the loop keeps pumping, so the harness can
 * sample the loader's RSS with a live GUI app.
 *
 * No division/modulo and no 64-bit helpers: the object must not pull
 * in __aeabi_* runtime symbols (they are not coredll imports here).
 */

typedef unsigned short WCHAR;
typedef unsigned int   DWORD;
typedef unsigned int   UINT;
typedef int            BOOL;
typedef int            LONG;
typedef void          *HANDLE;
typedef void          *LPVOID;
typedef const void    *LPCVOID;
typedef LONG (*WNDPROC)(HANDLE, UINT, DWORD, LONG);

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2

#define WM_CREATE  0x0001
#define WM_DESTROY 0x0002
#define WM_PAINT   0x000F
#define WM_TIMER   0x0113
#define WM_USER    0x0400

#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#define SW_SHOW 5
#define SRCCOPY 0x00CC0020u
#define COLOR_WINDOW 5

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct {
    DWORD style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HANDLE hInstance;
    HANDLE hIcon;
    HANDLE hCursor;
    HANDLE hbrBackground;
    const WCHAR *lpszMenuName;
    const WCHAR *lpszClassName;
} WNDCLASSW;
typedef struct {
    HANDLE hwnd;
    UINT message;
    DWORD wParam;
    LONG lParam;
    DWORD time;
    POINT pt;
} MSG;
typedef struct {
    HANDLE hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncremental;
    unsigned char rgbReserved[32];
} PAINTSTRUCT;

/* --- GWE imports --- */
UINT RegisterClassW(const WNDCLASSW *);
HANDLE CreateWindowExW(DWORD, const WCHAR *, const WCHAR *, DWORD,
                       int, int, int, int, HANDLE, HANDLE, HANDLE,
                       LPVOID);
BOOL ShowWindow(HANDLE, int);
BOOL UpdateWindow(HANDLE);
BOOL DestroyWindow(HANDLE);
LONG DefWindowProcW(HANDLE, UINT, DWORD, LONG);
BOOL PostMessageW(HANDLE, UINT, DWORD, LONG);
LONG SendMessageW(HANDLE, UINT, DWORD, LONG);
void PostQuitMessage(int);
BOOL GetMessageW(MSG *, HANDLE, UINT, UINT);
BOOL TranslateMessage(const MSG *);
LONG DispatchMessageW(const MSG *);
UINT SetTimer(HANDLE, UINT, UINT, void *);
BOOL KillTimer(HANDLE, UINT);
HANDLE GetDC(HANDLE);
int ReleaseDC(HANDLE, HANDLE);
HANDLE BeginPaint(HANDLE, PAINTSTRUCT *);
BOOL EndPaint(HANDLE, const PAINTSTRUCT *);
int FillRect(HANDLE, const RECT *, HANDLE);
HANDLE CreateSolidBrush(DWORD);
HANDLE CreatePen(int, int, DWORD);
HANDLE SelectObject(HANDLE, HANDLE);
BOOL DeleteObject(HANDLE);
BOOL Rectangle(HANDLE, int, int, int, int);
DWORD SetPixel(HANDLE, int, int, DWORD);
DWORD GetPixel(HANDLE, int, int);
HANDLE CreateCompatibleDC(HANDLE);
HANDLE CreateCompatibleBitmap(HANDLE, int, int);
BOOL DeleteDC(HANDLE);
BOOL BitBlt(HANDLE, int, int, int, int, HANDLE, int, int, DWORD);
int DrawTextW(HANDLE, const WCHAR *, int, RECT *, UINT);

/* --- file imports (M1 set) --- */
HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD,
                   HANDLE);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, void *);
BOOL CloseHandle(HANDLE);
DWORD GetFileAttributesW(const WCHAR *);
void OutputDebugStringW(const WCHAR *);

/* COLORREF is 0x00BBGGRR. */
#define CREF(r, g, b) \
    ((DWORD)(unsigned char)(r) | ((DWORD)(unsigned char)(g) << 8) \
     | ((DWORD)(unsigned char)(b) << 16))
#define COL_WHITE   CREF(255, 255, 255)
#define COL_BLUE    CREF(0, 0, 255)
#define COL_RED     CREF(255, 0, 0)
#define COL_GREEN   CREF(0, 200, 0)
#define COL_YELLOW  CREF(255, 255, 0)
#define COL_MAGENTA CREF(255, 0, 255)
#define COL_CYAN    CREF(0, 255, 255)
#define COL_BLACK   CREF(0, 0, 0)

/* ---------- counters recorded into the log ---------- */
static int g_create, g_user_post, g_user_send, g_paint;
static int g_timer, g_bitblt_ok, g_getpixel_ok, g_destroy;
static int g_send_ret_ok;
static int g_hold;

static const WCHAR *W(const char *ascii, WCHAR *buf)
{
    int i = 0;
    while (ascii[i]) {
        buf[i] = (WCHAR)(unsigned char)ascii[i];
        i++;
    }
    buf[i] = 0;
    return buf;
}

/* ---------- tiny log builder (no CRT) ---------- */
static char g_log[512];
static int g_log_len;

static void log_add(const char *s)
{
    while (*s)
        g_log[g_log_len++] = *s++;
}

static void log_kv(const char *name, int v)
{
    /* decimal output by subtraction over a fixed power table:
     * no division anywhere (no __aeabi_idiv in a freestanding PE) */
    static const int pows[6] = { 100000, 10000, 1000, 100, 10, 1 };
    log_add(name);
    log_add("=");
    if (v < 0) {
        log_add("-\n");
        return;
    }
    int started = 0;
    for (int i = 0; i < 6; i++) {
        int d = 0;
        while (v >= pows[i]) {
            v -= pows[i];
            d++;
        }
        if (d || started || pows[i] == 1) {
            g_log[g_log_len++] = (char)('0' + d);
            started = 1;
        }
    }
    log_add("\n");
}

static void log_yn(const char *name, int ok)
{
    log_add(name);
    log_add(ok ? "=OK\n" : "=FAIL\n");
}

static void write_log(void)
{
    WCHAR path[64];
    g_log_len = 0;
    log_add("braince-m2 ceguitest v1\n");
    log_kv("CREATE", g_create);
    log_kv("USER_POST", g_user_post);
    log_kv("USER_SEND", g_user_send);
    log_yn("SENDRET", g_send_ret_ok);
    log_yn("PAINT", g_paint > 0);
    log_kv("TIMER", g_timer);
    log_yn("BITBLT", g_bitblt_ok);
    log_yn("GETPIXEL", g_getpixel_ok);
    log_kv("DESTROY", g_destroy);
    int pass = g_create == 1 && g_user_post == 1 && g_user_send == 1
        && g_send_ret_ok && g_paint > 0 && g_timer >= 8
        && g_bitblt_ok && g_getpixel_ok;
    if (!g_hold)
        pass = pass && g_destroy == 1;
    log_add(pass ? "GUILOG=PASS\n" : "GUILOG=FAIL\n");

    HANDLE h = CreateFileW(W("\\CE-TEST\\guilog.txt", path),
                           GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, g_log, (DWORD)g_log_len, &written, 0);
        CloseHandle(h);
    }
}

/* ---------- the deterministic paint scene ---------- */
static void paint_scene(HANDLE hwnd, HANDLE hdc)
{
    RECT rc;
    HANDLE brush;

    /* white background */
    brush = CreateSolidBrush(COL_WHITE);
    rc.left = 0; rc.top = 0; rc.right = 400; rc.bottom = 300;
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    /* blue band across the top */
    brush = CreateSolidBrush(COL_BLUE);
    rc.left = 0; rc.top = 0; rc.right = 400; rc.bottom = 40;
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    /* red block */
    brush = CreateSolidBrush(COL_RED);
    rc.left = 10; rc.top = 50; rc.right = 110; rc.bottom = 150;
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    /* green rectangle with a black pen border */
    brush = CreateSolidBrush(COL_GREEN);
    HANDLE pen = CreatePen(0, 1, COL_BLACK);
    HANDLE oldb = SelectObject(hdc, brush);
    HANDLE oldp = SelectObject(hdc, pen);
    Rectangle(hdc, 20, 160, 180, 220);
    SelectObject(hdc, oldb);
    SelectObject(hdc, oldp);
    DeleteObject(brush);
    DeleteObject(pen);

    /* black text */
    WCHAR text[32];
    rc.left = 10; rc.top = 230; rc.right = 300; rc.bottom = 260;
    DrawTextW(hdc, W("BrainCE M2", text), -1, &rc, 0);

    /* one magenta pixel */
    SetPixel(hdc, 390, 290, COL_MAGENTA);

    /* yellow 50x50 via memory DC + BitBlt */
    HANDLE memdc = CreateCompatibleDC(hdc);
    HANDLE bmp = CreateCompatibleBitmap(hdc, 50, 50);
    if (memdc && bmp) {
        HANDLE oldbmp = SelectObject(memdc, bmp);
        brush = CreateSolidBrush(COL_YELLOW);
        rc.left = 0; rc.top = 0; rc.right = 50; rc.bottom = 50;
        FillRect(memdc, &rc, brush);
        DeleteObject(brush);
        BitBlt(hdc, 300, 50, 50, 50, memdc, 0, 0, SRCCOPY);
        SelectObject(memdc, oldbmp);
    }
    if (bmp)
        DeleteObject(bmp);
    if (memdc)
        DeleteDC(memdc);

    /* self-verification through GetPixel */
    DWORD px_blue = GetPixel(hdc, 200, 20);
    DWORD px_red = GetPixel(hdc, 60, 100);
    DWORD px_yellow = GetPixel(hdc, 325, 75);
    g_bitblt_ok = (px_yellow == COL_YELLOW);
    g_getpixel_ok = (px_blue == COL_BLUE) && (px_red == COL_RED);

    /* cyan pixel through the GetDC path */
    HANDLE dc2 = GetDC(hwnd);
    if (dc2) {
        SetPixel(dc2, 395, 295, COL_CYAN);
        ReleaseDC(hwnd, dc2);
    }
}

static LONG wnd_proc(HANDLE hwnd, UINT msg, DWORD wp, LONG lp)
{
    (void)lp;
    switch (msg) {
    case WM_CREATE:
        g_create++;
        return 0;
    case WM_USER + 2:
        if (wp == 0x1234)
            g_user_post = 1;
        return 0;
    case WM_USER + 3:
        if (wp == 0x5678)
            g_user_send = 1;
        return 0xBEEF;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HANDLE hdc = BeginPaint(hwnd, &ps);
        if (hdc) {
            paint_scene(hwnd, hdc);
            EndPaint(hwnd, &ps);
        }
        g_paint++;
        return 0;
    }
    case WM_TIMER:
        g_timer++;
        if (g_timer == 8) {
            write_log();
            if (!g_hold) {
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_DESTROY:
        g_destroy = 1;
        write_log();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int WinMain(HANDLE hInst, HANDLE hPrev, WCHAR *cmd, int show)
{
    (void)hPrev; (void)cmd; (void)show;
    WCHAR cls[32], title[32], holdpath[32], dbg[32];
    OutputDebugStringW(W("ceguitest start", dbg));

    g_hold = GetFileAttributesW(W("\\CE-TEST\\GHOLD", holdpath))
             != INVALID_FILE_ATTRIBUTES;

    WNDCLASSW wc;
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = (HANDLE)(DWORD)(COLOR_WINDOW + 1);
    wc.lpszMenuName = 0;
    wc.lpszClassName = W("CEGUITEST", cls);
    if (!RegisterClassW(&wc))
        return 3;

    HANDLE hwnd = CreateWindowExW(0, wc.lpszClassName,
                                  W("BrainCE M2 Test", title),
                                  WS_OVERLAPPEDWINDOW,
                                  60, 40, 400, 300, 0, 0, hInst, 0);
    if (!hwnd)
        return 4;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* both in-process message paths, exercised before the loop */
    PostMessageW(hwnd, WM_USER + 2, 0x1234, 0);
    LONG sret = SendMessageW(hwnd, WM_USER + 3, 0x5678, 0);
    g_send_ret_ok = (sret == 0xBEEF);

    SetTimer(hwnd, 1, 100, 0);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
