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

#endif
