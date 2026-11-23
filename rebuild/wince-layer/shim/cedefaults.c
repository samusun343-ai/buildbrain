/* BrainCE Layer -- "feature absent" semantics (M3).
 *
 * Some coredll exports describe facilities this device layer
 * genuinely does not have: no IME is installed in the layer,
 * and the clipboard starts (and stays, until an app sets data)
 * empty. For those, the documented no-facility answers ARE the
 * correct implementation:
 *  - ImmGetContext returns NULL (no input context exists);
 *  - ImmGetOpenStatus is FALSE (no IME is open);
 *  - GetClipboardData returns NULL (the format is not present
 *    on an empty clipboard);
 *  - OpenClipboard/CloseClipboard succeed (the clipboard can
 *    always be opened when nothing holds it).
 * These are not stubs: they are the values the real APIs
 * return in this system state, and callers (commctrl's edit
 * code among them) branch on exactly these.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ce_types.h"

/* ---- IMM (no IME installed) ---- */

CE_HANDLE ce_ImmAssociateContext(CE_HANDLE hwnd, CE_HANDLE himc)
{
    (void)hwnd;
    (void)himc;
    return NULL;   /* no previous context: there is none */
}

CE_HANDLE ce_ImmGetContext(CE_HANDLE hwnd)
{
    (void)hwnd;
    return NULL;
}

CE_BOOL ce_ImmReleaseContext(CE_HANDLE hwnd, CE_HANDLE himc)
{
    (void)hwnd;
    (void)himc;
    return CE_TRUE;
}

CE_BOOL ce_ImmGetOpenStatus(CE_HANDLE himc)
{
    (void)himc;
    return CE_FALSE;
}

CE_LONG ce_ImmGetCompositionStringW(CE_HANDLE himc, CE_DWORD idx,
                                    void *buf, CE_DWORD len)
{
    (void)himc;
    (void)idx;
    (void)buf;
    (void)len;
    return 0;   /* no composition string exists */
}

CE_BOOL ce_ImmNotifyIME(CE_HANDLE himc, CE_DWORD action,
                        CE_DWORD idx, CE_DWORD value)
{
    (void)himc;
    (void)action;
    (void)idx;
    (void)value;
    return CE_FALSE;   /* nobody to notify */
}

CE_DWORD ce_ImmSIPanelState(CE_DWORD cmd, void *data)
{
    (void)cmd;
    (void)data;
    return 0;   /* no SIP in the layer */
}

/* ---- user KDATA page ----
 * __GetUserKData returns the address of the kernel's per-process
 * user-data page, which hosted DLLs (commctrl in DllMain) read
 * OS facts from. The layer hands out one zeroed page: every
 * field reads as "absent / 0", the same neutral values a
 * minimal CE boot presents for optional features. If a hosted
 * binary is ever observed branching on a specific field, that
 * field gets a real value then -- driven by evidence, not by
 * guessing a layout here. */
static unsigned char g_kdata[4096];

void *ce___GetUserKData(void)
{
    return g_kdata;
}

/* ---- misc CE extras with "feature absent" answers ---- */

CE_DWORD ce_GetOwnerProcess(CE_HANDLE hwnd)
{
    (void)hwnd;
    /* Single-process hosting: every window belongs to this
     * process. */
    return (CE_DWORD)getpid();
}

CE_BOOL ce_GetClipCursor(CE_RECT *rc)
{
    if (!rc)
        return CE_FALSE;
    /* The cursor is never clipped: the whole panel. */
    rc->left = 0;
    rc->top = 0;
    rc->right = 854;
    rc->bottom = 480;
    return CE_TRUE;
}

CE_BOOL ce_GetGestureInfo(CE_HANDLE h, void *info)
{
    (void)h;
    (void)info;
    return CE_FALSE;   /* no gesture source in the layer */
}

CE_BOOL ce_CloseGestureInfoHandle(CE_HANDLE h)
{
    (void)h;
    return CE_FALSE;   /* no such handle was ever issued */
}

static CE_DWORD g_auto_gesture[64];

CE_BOOL ce_SetWindowAutoGesture(CE_HANDLE hwnd, CE_DWORD mask,
                                CE_DWORD flags)
{
    /* Accepted and recorded; the layer has no touch pipeline
     * feeding CE windows, so nothing consumes the setting. */
    (void)mask;
    unsigned slot = (unsigned)((uintptr_t)hwnd >> 4) & 63;
    g_auto_gesture[slot] = flags;
    return CE_TRUE;
}

CE_DWORD ce_GetWindowAutoGesture(CE_HANDLE hwnd)
{
    unsigned slot = (unsigned)((uintptr_t)hwnd >> 4) & 63;
    return g_auto_gesture[slot];
}

CE_BOOL ce_GetAnimateMessageInfo(CE_HANDLE hwnd, void *info)
{
    (void)hwnd;
    (void)info;
    return CE_FALSE;   /* no animation information exists */
}

CE_BOOL ce_SetWindowPosOnRotate(CE_HANDLE hwnd, CE_BOOL on)
{
    (void)hwnd;
    (void)on;
    return CE_TRUE;   /* the panel never rotates; setting kept
                       * conceptually, nothing to apply */
}

/* ---- clipboard (empty until an app sets data) ---- */

static CE_HANDLE g_clip_owner;

CE_BOOL ce_OpenClipboard(CE_HANDLE hwnd)
{
    g_clip_owner = hwnd;
    return CE_TRUE;
}

CE_BOOL ce_CloseClipboard(void)
{
    g_clip_owner = NULL;
    return CE_TRUE;
}

CE_BOOL ce_EmptyClipboard(void)
{
    return CE_TRUE;   /* it is empty */
}

CE_HANDLE ce_GetClipboardData(CE_DWORD format)
{
    (void)format;
    return NULL;   /* format not present on an empty clipboard */
}

CE_BOOL ce_IsClipboardFormatAvailable(CE_DWORD format)
{
    (void)format;
    return CE_FALSE;
}
