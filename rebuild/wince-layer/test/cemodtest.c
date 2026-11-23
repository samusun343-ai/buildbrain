/* BrainCE Layer M3 test binary: module system, CE volumes,
 * threads/sync, dialog manager.
 *
 * Freestanding WinCE PE (clang + pe-link47), coredll imports only.
 * Writes \CE-TEST\modresults.txt with one name=OK/FAIL line per
 * check and ALL=PASS at the end when everything passed. Runs
 * headless (the GWE shadow canvas needs no WM server) and also in
 * the guest. No division/modulo, no 64-bit helpers.
 *
 * Checks:
 *  volread   - read \Nand\ToolsApp\Calc\AppMain.exe via the S0
 *              read path: MZ header + exact size 848896.
 *  voljp     - read the TextMemoTool AppMain.exe under its
 *              Japanese directory name (UTF-16 exact): size 5632.
 *  volfind   - enumerate \Nand\ToolsApp\* : finds the Calc dir.
 *  overlay   - write \Nand\memo-test.txt (lands in the SD overlay),
 *              read it back through the volume path.
 *  loadlib   - LoadLibraryW("edwgsp.dll") (a ROM module), resolve
 *              its three exports by name, reject a bogus name,
 *              GetModuleHandleW / GetModuleFileNameW agree.
 *  thread    - CreateThread + critical section + interlocked
 *              counter; WaitForSingleObject + GetExitCodeThread.
 *  process   - CreateProcessW(\CE-TEST\child.exe) (a cetest copy
 *              placed by the runner), wait, exit code 0.
 *  dialog    - DialogBoxIndirectParamW with an in-memory template:
 *              SetDlgItemTextW/GetDlgItemTextW roundtrip in
 *              WM_INITDIALOG, programmatic IDOK, EndDialog code.
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
typedef LONG (*DLGPROC)(HANDLE, UINT, DWORD, LONG);

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)-1)

#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define CREATE_NEW    1
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define WAIT_OBJECT_0 0u
#define INFINITE      0xFFFFFFFFu
#define STILL_ACTIVE  259u

#define WM_INITDIALOG 0x0110
#define WM_COMMAND    0x0111
#define BN_CLICKED    0
#define IDOK          1
#define EM_SETSEL     0x00B1

typedef struct {
    DWORD dwFileAttributes;
    DWORD ftCreationTimeLow, ftCreationTimeHigh;
    DWORD ftLastAccessTimeLow, ftLastAccessTimeHigh;
    DWORD ftLastWriteTimeLow, ftLastWriteTimeHigh;
    DWORD nFileSizeHigh, nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct {
    HANDLE hProcess, hThread;
    DWORD dwProcessId, dwThreadId;
} PROCESS_INFORMATION;

/* --- imports --- */
HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD,
                   HANDLE);
BOOL ReadFile(HANDLE, LPVOID, DWORD, DWORD *, void *);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, void *);
BOOL CloseHandle(HANDLE);
DWORD GetFileSize(HANDLE, DWORD *);
DWORD GetFileAttributesW(const WCHAR *);
HANDLE FindFirstFileW(const WCHAR *, WIN32_FIND_DATAW *);
BOOL FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
BOOL FindClose(HANDLE);
HANDLE LoadLibraryW(const WCHAR *);
HANDLE GetModuleHandleW(const WCHAR *);
DWORD GetModuleFileNameW(HANDLE, WCHAR *, DWORD);
void *GetProcAddressW(HANDLE, const WCHAR *);
HANDLE CreateThread(void *, DWORD, DWORD (*)(LPVOID), LPVOID, DWORD,
                    DWORD *);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL GetExitCodeThread(HANDLE, DWORD *);
BOOL CreateProcessW(const WCHAR *, const WCHAR *, void *, void *,
                    BOOL, DWORD, void *, const WCHAR *, void *,
                    PROCESS_INFORMATION *);
BOOL GetExitCodeProcess(HANDLE, DWORD *);
void InitializeCriticalSection(void *);
void EnterCriticalSection(void *);
void LeaveCriticalSection(void *);
void DeleteCriticalSection(void *);
LONG InterlockedIncrement(volatile LONG *);
LONG InterlockedExchange(volatile LONG *, LONG);
LONG InterlockedCompareExchange(volatile LONG *, LONG, LONG);
LONG DialogBoxIndirectParamW(HANDLE, const void *, HANDLE, DLGPROC,
                             LONG);
BOOL EndDialog(HANDLE, LONG);
HANDLE GetDlgItem(HANDLE, int);
BOOL SetDlgItemTextW(HANDLE, int, const WCHAR *);
DWORD GetDlgItemTextW(HANDLE, int, WCHAR *, int);
LONG SendDlgItemMessageW(HANDLE, int, DWORD, DWORD, LONG);
BOOL PostMessageW(HANDLE, UINT, DWORD, LONG);
void OutputDebugStringW(const WCHAR *);

/* --- result log --- */
static char g_result[4096];
static int g_len = 0;
static int g_fail = 0;

static void res_add(const char *s)
{
    while (*s && g_len < 4000)
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

static int w_contains(const WCHAR *a, const char *b)
{
    int i, j;
    for (i = 0; a[i]; i++) {
        for (j = 0; b[j]; j++)
            if (a[i + j] != (WCHAR)(unsigned char)b[j])
                break;
        if (!b[j])
            return 1;
    }
    return 0;
}

/* --- individual checks --- */

static int check_volread(void)
{
    WCHAR path[128];
    HANDLE h = CreateFileW(W("\\Nand\\ToolsApp\\Calc\\AppMain.exe",
                             path),
                           GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    unsigned char hdr[2] = { 0, 0 };
    DWORD got = 0;
    BOOL ok = ReadFile(h, hdr, 2, &got, 0);
    DWORD size = GetFileSize(h, 0);
    CloseHandle(h);
    return ok && got == 2 && hdr[0] == 'M' && hdr[1] == 'Z'
           && size == 848896;
}

static int check_voljp(void)
{
    /* \Nand\<U+30A2 U+30D7 U+30EA>\TextMemoTool\AppMain.exe */
    WCHAR path[128];
    int i = 0;
    const char *pre = "\\Nand\\";
    while (pre[i]) {
        path[i] = (WCHAR)pre[i];
        i++;
    }
    path[i++] = 0x30A2;
    path[i++] = 0x30D7;
    path[i++] = 0x30EA;
    const char *post = "\\TextMemoTool\\AppMain.exe";
    int j = 0;
    while (post[j]) {
        path[i++] = (WCHAR)post[j];
        j++;
    }
    path[i] = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, 0, 0, OPEN_EXISTING, 0,
                           0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    unsigned char hdr[2] = { 0, 0 };
    DWORD got = 0;
    BOOL ok = ReadFile(h, hdr, 2, &got, 0);
    DWORD size = GetFileSize(h, 0);
    CloseHandle(h);
    return ok && got == 2 && hdr[0] == 'M' && hdr[1] == 'Z'
           && size == 5632;
}

static int check_volfind(void)
{
    WCHAR pat[128];
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W("\\Nand\\ToolsApp\\*", pat), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int count = 0, found_calc = 0;
    for (;;) {
        count++;
        if (w_eq(fd.cFileName, "Calc")
            && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            found_calc = 1;
        if (!FindNextFileW(h, &fd))
            break;
        if (count > 500)
            break;
    }
    FindClose(h);
    return found_calc && count >= 3;
}

static int check_overlay(void)
{
    WCHAR path[128];
    static const char payload[] = "overlay-write-check";
    HANDLE h = CreateFileW(W("\\Nand\\memo-test.txt", path),
                           GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD put = 0;
    BOOL ok = WriteFile(h, payload, sizeof(payload) - 1, &put, 0);
    CloseHandle(h);
    if (!ok || put != sizeof(payload) - 1)
        return 0;
    HANDLE r = CreateFileW(W("\\Nand\\memo-test.txt", path),
                           GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (r == INVALID_HANDLE_VALUE)
        return 0;
    char buf[32];
    DWORD got = 0;
    ok = ReadFile(r, buf, sizeof(payload) - 1, &got, 0);
    CloseHandle(r);
    if (!ok || got != sizeof(payload) - 1)
        return 0;
    for (DWORD i = 0; i < got; i++)
        if (buf[i] != payload[i])
            return 0;
    return 1;
}

static int check_loadlib(void)
{
    WCHAR name[64], en[64], modname[260];
    HANDLE h = LoadLibraryW(W("edwgsp.dll", name));
    if (!h)
        return 0;
    void *p1 = GetProcAddressW(h, W("e_rdb_UOCGeom_GetDistance", en));
    void *p2 = GetProcAddressW(h, W("e_rdb_UOCGeom_GetRectangle", en));
    void *p3 = GetProcAddressW(h, W("e_rdb_UOCGeom_SetPoint", en));
    void *bad = GetProcAddressW(h, W("NoSuchExportXYZ", en));
    HANDLE h2 = GetModuleHandleW(W("edwgsp.dll", name));
    DWORD n = GetModuleFileNameW(h, modname, 260);
    return p1 && p2 && p3 && !bad && h2 == h && n > 0
           && w_contains(modname, "edwgsp");
}

/* thread check */
static volatile LONG g_counter = 0;
static LONG g_critsec[8];   /* app-owned CRITICAL_SECTION storage */

static DWORD worker(LPVOID param)
{
    (void)param;
    int i;
    for (i = 0; i < 1000; i++) {
        EnterCriticalSection(g_critsec);
        InterlockedIncrement(&g_counter);
        LeaveCriticalSection(g_critsec);
    }
    return 42;
}

static int check_thread(void)
{
    DWORD tid = 0;
    InitializeCriticalSection(g_critsec);
    HANDLE h = CreateThread(0, 0, worker, 0, 0, &tid);
    if (!h)
        return 0;
    DWORD w = WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    BOOL g = GetExitCodeThread(h, &code);
    DeleteCriticalSection(g_critsec);
    CloseHandle(h);
    volatile LONG v = 0;
    LONG old = InterlockedExchange(&v, 7);
    LONG cur = InterlockedCompareExchange(&v, 9, 7);
    LONG wrong = InterlockedCompareExchange(&v, 5, 7);
    return w == WAIT_OBJECT_0 && g && code == 42 && g_counter == 1000
           && tid != 0 && old == 0 && cur == 7 && v == 9 && wrong == 9;
}

static int check_process(void)
{
    WCHAR path[128];
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(W("\\CE-TEST\\child.exe", path), 0, 0, 0,
                             FALSE, 0, 0, 0, 0, &pi);
    if (!ok)
        return 0;
    DWORD w = WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0xFFFF;
    BOOL g = GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return w == WAIT_OBJECT_0 && g && code == 0;
}

/* dialog check: the template is assembled at runtime into a
 * WORD buffer (the on-disk format, built field by field). */
static WCHAR g_tmpl[1024];
static int g_tp;

static void t_w(WCHAR v) { g_tmpl[g_tp++] = v; }
static void t_dw(DWORD v)
{
    g_tmpl[g_tp++] = (WCHAR)(v & 0xFFFF);
    g_tmpl[g_tp++] = (WCHAR)((v >> 16) & 0xFFFF);
}
static void t_str(const char *s)
{
    while (*s) {
        g_tmpl[g_tp++] = (WCHAR)(unsigned char)*s;
        s++;
    }
    g_tmpl[g_tp++] = 0;
}
static void t_align(void)
{
    while (g_tp & 1)
        g_tmpl[g_tp++] = 0;
}
static void t_item(DWORD style, short x, short y, short cx, short cy,
                   short id, unsigned short atom, const char *title)
{
    t_align();
    t_dw(style);
    t_dw(0);                    /* exStyle */
    t_w((WCHAR)x);
    t_w((WCHAR)y);
    t_w((WCHAR)cx);
    t_w((WCHAR)cy);
    t_w((WCHAR)id);
    t_w(0xFFFF);
    t_w((WCHAR)atom);
    t_str(title);
    t_w(0);                     /* creation data length */
}

static int g_dlg_stage = 0;

static LONG test_dlgproc(HANDLE hwnd, UINT msg, DWORD wp, LONG lp)
{
    WCHAR buf[64], want[16];
    switch (msg) {
    case WM_INITDIALOG: {
        g_dlg_stage = 1;
        if (!SetDlgItemTextW(hwnd, 101, W("hello", want)))
            return TRUE;
        DWORD n = GetDlgItemTextW(hwnd, 101, buf, 64);
        if (n == 5 && w_eq(buf, "hello"))
            g_dlg_stage = 2;
        SendDlgItemMessageW(hwnd, 101, EM_SETSEL, 0, -1);
        /* programmatic click of the default button */
        PostMessageW(hwnd, WM_COMMAND,
                     (DWORD)((BN_CLICKED << 16) | IDOK),
                     (LONG)GetDlgItem(hwnd, IDOK));
        return TRUE;
    }
    case WM_COMMAND:
        if ((wp & 0xFFFF) == IDOK) {
            if (g_dlg_stage == 2)
                g_dlg_stage = 3;
            EndDialog(hwnd, 1234);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

static int check_dialog(void)
{
    g_tp = 0;
    /* DLGTEMPLATE: DS_SETFONT|DS_MODALFRAME|WS_POPUP|WS_CAPTION|
     * WS_SYSMENU, 3 items, 10,10 120x60 */
    t_dw(0x80C800C0u);
    t_dw(0);
    t_w(3);
    t_w(10); t_w(10); t_w(120); t_w(60);
    t_w(0);                     /* menu: none */
    t_w(0);                     /* class: default */
    t_str("ModTest");
    t_w(9);                     /* font size */
    t_str("Tahoma");
    /* Static "Name:" id=100 */
    t_item(0x50000000u, 4, 4, 60, 10, 100, 0x0082, "Name:");
    /* Edit id=101 */
    t_item(0x50010080u, 4, 16, 100, 12, 101, 0x0081, "");
    /* Button OK id=1 (IDOK), BS_DEFPUSHBUTTON */
    t_item(0x50010001u, 70, 36, 34, 14, 1, 0x0080, "OK");
    LONG r = DialogBoxIndirectParamW(0, g_tmpl, 0, test_dlgproc, 0);
    return r == 1234 && g_dlg_stage == 3;
}

int WinMain(HANDLE hInst, HANDLE hPrev, WCHAR *cmd, int show)
{
    (void)hInst; (void)hPrev; (void)cmd; (void)show;
    WCHAR msg[64];
    OutputDebugStringW(W("cemodtest start", msg));

    res_add("braince-m3 cemodtest v1\n");
    res_line("volread", check_volread());
    res_line("voljp", check_voljp());
    res_line("volfind", check_volfind());
    res_line("overlay", check_overlay());
    res_line("loadlib", check_loadlib());
    res_line("thread", check_thread());
    res_line("process", check_process());
    res_line("dialog", check_dialog());
    res_add(g_fail ? "ALL=FAIL\n" : "ALL=PASS\n");

    WCHAR path[128];
    HANDLE h = CreateFileW(W("\\CE-TEST\\modresults.txt", path),
                           GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, g_result, (DWORD)g_len, &written, 0);
        CloseHandle(h);
    }
    return g_fail ? 1 : 0;
}
