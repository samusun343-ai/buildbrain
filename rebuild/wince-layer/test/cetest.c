/* BrainCE Layer M1 test binary (DESIGN-wince-layer.md section 9.1).
 *
 * Freestanding WinCE PE built with clang -target armv5te-none-eabi
 * and pe-link47.py; imports only COREDLL.dll by ordinal. Behaviour:
 * create a file, write a known byte pattern, read it back and
 * verify, exercise the rest of the M1 shim set, and record the
 * outcome to \CE-TEST\result.txt in a byte-deterministic format so
 * the host and guest runs can be diffed exactly.
 *
 * If \CE-TEST\HOLD exists, the binary loops in Sleep() after writing
 * the result so the harness can sample the loader's RSS.
 *
 * No division/modulo and no 64-bit helpers: the object must not pull
 * in __aeabi_* runtime symbols (they are not coredll imports here).
 */

typedef unsigned short WCHAR;
typedef unsigned int   DWORD;
typedef int            BOOL;
typedef int            LONG;
typedef void          *HANDLE;
typedef void          *LPVOID;
typedef const void    *LPCVOID;

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define ERROR_FILE_NOT_FOUND 2

#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_BEGIN 0
#define FILE_END 2
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define LMEM_ZEROINIT 0x40
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000

typedef struct {
    DWORD dwFileAttributes;
    DWORD ftCreationLow, ftCreationHigh;
    DWORD ftAccessLow, ftAccessHigh;
    DWORD ftWriteLow, ftWriteHigh;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD,
                   HANDLE);
BOOL ReadFile(HANDLE, LPVOID, DWORD, DWORD *, void *);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, void *);
DWORD SetFilePointer(HANDLE, LONG, LONG *, DWORD);
DWORD GetFileSize(HANDLE, DWORD *);
BOOL FlushFileBuffers(HANDLE);
BOOL CloseHandle(HANDLE);
BOOL DeleteFileW(const WCHAR *);
BOOL CreateDirectoryW(const WCHAR *, void *);
DWORD GetFileAttributesW(const WCHAR *);
HANDLE FindFirstFileW(const WCHAR *, WIN32_FIND_DATAW *);
BOOL FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
BOOL FindClose(HANDLE);
LPVOID LocalAlloc(DWORD, DWORD);
LPVOID LocalFree(LPVOID);
LPVOID VirtualAlloc(LPVOID, DWORD, DWORD, DWORD);
BOOL VirtualFree(LPVOID, DWORD, DWORD);
DWORD GetTickCount(void);
void Sleep(DWORD);
DWORD GetLastError(void);
void SetLastError(DWORD);
void OutputDebugStringW(const WCHAR *);
void *memcpy(void *, const void *, DWORD);
void *memset(void *, int, DWORD);
int memcmp(const void *, const void *, DWORD);
DWORD wcslen(const WCHAR *);
WCHAR *wcscpy(WCHAR *, const WCHAR *);

/* ---------- tiny result builder (no CRT) ---------- */
static char g_result[1024];
static int g_len;
static int g_fail;

static void res_add(const char *s)
{
    while (*s)
        g_result[g_len++] = *s++;
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

#define PATTERN_LEN 4096
static unsigned char pat_byte(int i)
{
    return (unsigned char)((i * 37 + 11) & 0xFF);
}

int WinMain(HANDLE hInst, HANDLE hPrev, WCHAR *cmd, int show)
{
    (void)hInst; (void)hPrev; (void)cmd; (void)show;
    WCHAR path[128];
    WCHAR msg[64];
    DWORD written = 0, readn = 0;

    OutputDebugStringW(W("cetest start", msg));

    res_add("braince-m1 cetest v1\n");

    /* --- CRT / string --- */
    {
        WCHAR a[32], b[32];
        W("BrainCE", a);
        wcscpy(b, a);
        int ok = (wcslen(b) == 7) && b[0] == 'B' && b[6] == 'E';
        res_line("wcs", ok);
    }

    /* --- LocalAlloc + memset/memcpy/memcmp --- */
    unsigned char *buf1 = LocalAlloc(LMEM_ZEROINIT, PATTERN_LEN);
    unsigned char *buf2 = LocalAlloc(LMEM_ZEROINIT, PATTERN_LEN);
    {
        int ok = buf1 && buf2;
        if (ok) {
            int zero_ok = 1;
            for (int i = 0; i < PATTERN_LEN; i++)
                if (buf1[i] != 0)
                    zero_ok = 0;
            for (int i = 0; i < PATTERN_LEN; i++)
                buf1[i] = pat_byte(i);
            memcpy(buf2, buf1, PATTERN_LEN);
            ok = zero_ok && memcmp(buf1, buf2, PATTERN_LEN) == 0;
            memset(buf2, 0xA5, 16);
            ok = ok && buf2[0] == 0xA5 && buf2[15] == 0xA5
                 && buf2[16] == pat_byte(16);
        }
        res_line("localalloc", ok);
    }

    /* --- VirtualAlloc --- */
    {
        unsigned char *va = VirtualAlloc(0, 8192,
                                         MEM_RESERVE | MEM_COMMIT, 0x04);
        int ok = va != 0;
        if (ok) {
            va[0] = 0x11;
            va[4096] = 0x22;
            va[8191] = 0x33;
            ok = va[0] == 0x11 && va[4096] == 0x22 && va[8191] == 0x33;
            ok = ok && VirtualFree(va, 0, MEM_RELEASE);
        }
        res_line("virtualalloc", ok);
    }

    /* --- tick / sleep --- */
    {
        DWORD t0 = GetTickCount();
        Sleep(20);
        DWORD t1 = GetTickCount();
        res_line("tick", (t1 - t0) < 5000);
    }

    /* --- file write / size / readback --- */
    HANDLE h = CreateFileW(W("\\CE-TEST\\pattern.bin", path),
                           GENERIC_READ | GENERIC_WRITE, 0, 0,
                           CREATE_ALWAYS, 0, 0);
    {
        int ok = h != INVALID_HANDLE_VALUE;
        if (ok) {
            ok = WriteFile(h, buf1, PATTERN_LEN, &written, 0)
                 && written == PATTERN_LEN && FlushFileBuffers(h);
        }
        res_line("filewrite", ok);
    }
    if (h != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(h, 0);
        DWORD end = SetFilePointer(h, 0, 0, FILE_END);
        res_line("filesize", sz == PATTERN_LEN && end == PATTERN_LEN);
        DWORD back = SetFilePointer(h, 0, 0, FILE_BEGIN);
        memset(buf2, 0, PATTERN_LEN);
        int ok = back == 0
                 && ReadFile(h, buf2, PATTERN_LEN, &readn, 0)
                 && readn == PATTERN_LEN
                 && memcmp(buf1, buf2, PATTERN_LEN) == 0;
        res_line("fileread", ok);
        CloseHandle(h);
    } else {
        res_line("filesize", 0);
        res_line("fileread", 0);
    }

    /* --- error path --- */
    {
        SetLastError(0);
        HANDLE bad = CreateFileW(W("\\CE-TEST\\no-such-file.bin", path),
                                 GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
        DWORD err = GetLastError();
        res_line("errorpath",
                 bad == INVALID_HANDLE_VALUE
                     && err == ERROR_FILE_NOT_FOUND);
    }

    /* --- directory + find --- */
    {
        int ok = CreateDirectoryW(W("\\CE-TEST\\SUB", path), 0);
        DWORD attr = GetFileAttributesW(W("\\CE-TEST\\SUB", path));
        ok = ok && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        res_line("mkdir", ok);
    }
    {
        static const char note[] = "hello braince\n";
        HANDLE hn = CreateFileW(W("\\CE-TEST\\SUB\\note.txt", path),
                                GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
        int ok = hn != INVALID_HANDLE_VALUE;
        if (ok) {
            ok = WriteFile(hn, note, sizeof(note) - 1, &written, 0)
                 && written == sizeof(note) - 1;
            CloseHandle(hn);
        }
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(W("\\CE-TEST\\SUB\\*", path), &fd);
        ok = ok && hf != INVALID_HANDLE_VALUE;
        if (hf != INVALID_HANDLE_VALUE) {
            /* first (and only) entry must be note.txt */
            ok = ok && fd.cFileName[0] == 'n' && fd.cFileName[1] == 'o'
                 && fd.cFileName[2] == 't' && fd.cFileName[3] == 'e'
                 && fd.nFileSizeLow == sizeof(note) - 1;
            ok = ok && !FindNextFileW(hf, &fd);
            FindClose(hf);
        }
        res_line("findfile", ok);
    }

    /* --- delete --- */
    {
        int ok = DeleteFileW(W("\\CE-TEST\\SUB\\note.txt", path));
        DWORD attr = GetFileAttributesW(W("\\CE-TEST\\SUB\\note.txt",
                                          path));
        res_line("delete", ok && attr == INVALID_FILE_ATTRIBUTES);
    }

    LocalFree(buf1);
    LocalFree(buf2);

    res_add(g_fail ? "ALL=FAIL\n" : "ALL=PASS\n");

    /* --- record the result --- */
    HANDLE hr = CreateFileW(W("\\CE-TEST\\result.txt", path),
                            GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (hr != INVALID_HANDLE_VALUE) {
        WriteFile(hr, g_result, (DWORD)g_len, &written, 0);
        FlushFileBuffers(hr);
        CloseHandle(hr);
    } else {
        g_fail = 1;
    }

    OutputDebugStringW(W(g_fail ? "cetest FAIL" : "cetest PASS", msg));

    /* --- optional hold for RSS sampling --- */
    if (GetFileAttributesW(W("\\CE-TEST\\HOLD", path))
        != INVALID_FILE_ATTRIBUTES) {
        for (;;)
            Sleep(1000);
    }

    return g_fail ? 1 : 0;
}
