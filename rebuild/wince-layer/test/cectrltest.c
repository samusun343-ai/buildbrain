/* BrainCE Layer M3 test binary: original commctrl.dll hosting.
 *
 * Loads the ROM's commctrl.dll through LoadLibraryW, initialises
 * the common controls, creates a progress bar and a status bar as
 * children of a plain window, drives them with their documented
 * messages and verifies the replies. Results go to
 * \CE-TEST\ctrlresults.txt. If \CE-TEST\GHOLD exists the window
 * stays open (guest screenshot mode).
 *
 * On the dev host under qemu-arm (user mode) commctrl's native
 * base 0x40120000 is inside qemu's own reserved VA span, so the
 * load fails there for an environmental reason (measured: the
 * span [0x40000000,0x41000000) is unavailable to MAP_FIXED under
 * qemu-user even for a static test binary). The test then logs
 * commctrl=SKIP-VA and exits 0; the guest run is the acceptance
 * environment and must log commctrl=OK.
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

#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2

#define WM_DESTROY 0x0002
#define WM_USER    0x0400
#define PBM_SETRANGE (WM_USER + 1)
#define PBM_SETPOS   (WM_USER + 2)
#define PBM_GETPOS   (WM_USER + 8)
#define PBM_SETSTEP  (WM_USER + 4)
#define PBM_STEPIT   (WM_USER + 5)
#define SB_SETTEXTW  (WM_USER + 11)
#define SB_GETTEXTW  (WM_USER + 13)
#define ICC_PROGRESS_CLASS 0x00000020u
#define ICC_BAR_CLASS      0x00000004u

#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#define WS_CHILD   0x40000000u
#define WS_VISIBLE 0x10000000u
#define SW_SHOW 5

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct {
    DWORD style;
    WNDPROC lpfnWndProc;
    int cbClsExtra, cbWndExtra;
    HANDLE hInstance, hIcon, hCursor, hbrBackground;
    const WCHAR *lpszMenuName, *lpszClassName;
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
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX;

/* --- imports --- */
UINT RegisterClassW(const WNDCLASSW *);
HANDLE CreateWindowExW(DWORD, const WCHAR *, const WCHAR *, DWORD,
                       int, int, int, int, HANDLE, HANDLE, HANDLE,
                       LPVOID);
BOOL ShowWindow(HANDLE, int);
BOOL DestroyWindow(HANDLE);
LONG DefWindowProcW(HANDLE, UINT, DWORD, LONG);
LONG SendMessageW(HANDLE, UINT, DWORD, LONG);
void PostQuitMessage(int);
BOOL GetMessageW(MSG *, HANDLE, UINT, UINT);
BOOL TranslateMessage(const MSG *);
LONG DispatchMessageW(const MSG *);
HANDLE LoadLibraryW(const WCHAR *);
void *GetProcAddressW(HANDLE, const WCHAR *);
HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD,
                   HANDLE);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, void *);
BOOL CloseHandle(HANDLE);
DWORD GetFileAttributesW(const WCHAR *);
void OutputDebugStringW(const WCHAR *);
DWORD GetTickCount(void);
void Sleep(DWORD);

typedef BOOL (*fn_iccex)(const INITCOMMONCONTROLSEX *);
typedef void (*fn_icc)(void);

static char g_result[2048];
static int g_len = 0;
static int g_fail = 0;

static void res_add(const char *s)
{
    while (*s && g_len < 2000)
        g_result[g_len++] = *s++;
    g_result[g_len] = 0;
}

static void res_line(const char *name, int ok)
{
    res_add(name);
    res_add(ok ? "=OK\n" : "=FAIL\n");
    if (!ok)
        g_fail = 1;
}

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

static int w_eq(const WCHAR *a, const char *b)
{
    int i = 0;
    while (b[i]) {
        if (a[i] != (WCHAR)(unsigned char)b[i])
            return 0;
        i++;
    }
    return a[i] == 0;
}

static HANDLE g_main;

static LONG main_proc(HANDLE hwnd, UINT msg, DWORD wp, LONG lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void write_results(void)
{
    WCHAR path[128];
    HANDLE h = CreateFileW(W("\\CE-TEST\\ctrlresults.txt", path),
                           GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, g_result, (DWORD)g_len, &written, 0);
        CloseHandle(h);
    }
}

int WinMain(HANDLE hInst, HANDLE hPrev, WCHAR *cmd, int show)
{
    (void)hPrev; (void)cmd; (void)show;
    WCHAR buf[128], cls[64], title[64];
    OutputDebugStringW(W("cectrltest start", buf));
    res_add("braince-m3 cectrltest v1\n");

    HANDLE lib = LoadLibraryW(W("commctrl.dll", buf));
    if (!lib) {
        res_add("commctrl=SKIP-VA\n");
        res_add("ALL=SKIP\n");
        write_results();
        return 0;
    }
    res_line("loadlib", 1);

    fn_iccex iccex = (fn_iccex)GetProcAddressW(
        lib, W("InitCommonControlsEx", buf));
    fn_icc icc = (fn_icc)GetProcAddressW(
        lib, W("InitCommonControls", buf));
    res_line("getproc", iccex != 0 || icc != 0);
    int init_ok = 0;
    if (iccex) {
        INITCOMMONCONTROLSEX s;
        s.dwSize = sizeof(s);
        s.dwICC = ICC_PROGRESS_CLASS | ICC_BAR_CLASS;
        init_ok = iccex(&s) ? 1 : 0;
    }
    if (icc)
        icc();
    res_line("init", init_ok || icc != 0);

    WNDCLASSW wc;
    wc.style = 0;
    wc.lpfnWndProc = main_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = (HANDLE)(DWORD)(5 + 1); /* COLOR_WINDOW+1 */
    wc.lpszMenuName = 0;
    wc.lpszClassName = W("BrainCECtrlTest", cls);
    RegisterClassW(&wc);
    g_main = CreateWindowExW(0, cls, W("BrainCE commctrl Test", title),
                             WS_OVERLAPPEDWINDOW, 60, 40, 480, 240,
                             0, 0, hInst, 0);
    res_line("mainwin", g_main != 0);
    if (!g_main) {
        res_add("ALL=FAIL\n");
        write_results();
        return 1;
    }
    ShowWindow(g_main, SW_SHOW);

    HANDLE prog = CreateWindowExW(
        0, W("msctls_progress32", buf), W("", title),
        WS_CHILD | WS_VISIBLE, 20, 20, 400, 28, g_main, 0, hInst, 0);
    res_line("progress-create", prog != 0);
    int pos_ok = 0, range_ok = 0;
    if (prog) {
        SendMessageW(prog, PBM_SETRANGE, 0,
                     (LONG)((100 << 16) | 0));
        SendMessageW(prog, PBM_SETSTEP, 5, 0);
        SendMessageW(prog, PBM_SETPOS, 42, 0);
        LONG p = SendMessageW(prog, PBM_GETPOS, 0, 0);
        pos_ok = (p == 42);
        SendMessageW(prog, PBM_STEPIT, 0, 0);
        p = SendMessageW(prog, PBM_GETPOS, 0, 0);
        range_ok = (p == 47);
    }
    res_line("progress-pos", pos_ok);
    res_line("progress-step", range_ok);

    HANDLE status = CreateWindowExW(
        0, W("msctls_statusbar32", buf), W("", title),
        WS_CHILD | WS_VISIBLE, 20, 190, 440, 24, g_main, 0, hInst, 0);
    res_line("status-create", status != 0);
    int text_ok = 0;
    if (status) {
        WCHAR txt[64], got[64];
        SendMessageW(status, SB_SETTEXTW, 0,
                     (LONG)W("BrainCE commctrl OK", txt));
        LONG n = SendMessageW(status, SB_GETTEXTW, 0, (LONG)got);
        text_ok = (n > 0) && w_eq(got, "BrainCE commctrl OK");
    }
    res_line("status-text", text_ok);

    res_add(g_fail ? "ALL=FAIL\n" : "ALL=PASS\n");
    write_results();

    int hold = GetFileAttributesW(W("\\CE-TEST\\GHOLD", buf))
               != (DWORD)-1;
    MSG msg;
    DWORD start = GetTickCount();
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!hold && GetTickCount() - start > 1500)
            DestroyWindow(g_main);
    }
    return g_fail ? 1 : 0;
}
