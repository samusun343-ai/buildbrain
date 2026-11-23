#!/usr/bin/env python3
"""M0: classify the ordinal-only coredll calls of the Brain app exes.

Inputs (QEMU-side project's published static census; read-only):
  exports-census.json  -- per-DLL export census incl. coredll `ordmap`
                          (ordinal -> name, from the export directory)
  callers.json         -- "dll!name" / "dll!#ordinal" -> [caller modules]

Reproduction of DESIGN-wince-layer.md section 6.2's population:
  callers whose path is NAND:.../AppMain.exe (case-sensitive file name)
  = 27 modules; their coredll.dll import keys = 874, split 437 named /
  437 ordinal-only.  Applying the census ordmap resolves all 437
  ordinal-only keys, and every resolved name is already present in the
  named set: the two forms are the same 437 functions imported by name
  from some modules and by ordinal from others.

Output: m0-classification.json + m0-classification.md (group table).

Classification rules (applied in order, first match wins; the group
names follow DESIGN section 6.2):
  1. Reg*                                            -> registry
  2. explicit file/mapping name sets                 -> file/mapping
  3. Local*/Global*/Virtual*/Heap* (alloc family)    -> memory
  4. thread/process/sync/module name sets + prefix   -> exec/sync/module
  5. window/message name sets + prefixes             -> window/message
  6. GDI/drawing/font name sets + prefixes           -> gdi/drawing/font
  7. CRT & string: leading '_', mangled C++ ('?'/'$'),
     known C library names, lstr* / MultiByteToWideChar /
     WideCharToMultiByte                             -> crt/string
  8. everything else                                 -> other
"""
import json, os, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
CENSUS = os.path.expanduser(
    "~/workspace/goals/sharp-brain-full-virtualization/hidden_files/"
    "p45work/static")

GROUPS = ["crt/string", "window/message", "gdi/drawing/font",
          "exec/sync/module", "file/mapping", "registry", "memory",
          "other"]
GROUP_JA = {
    "crt/string": "CRT / 文字列",
    "window/message": "ウィンドウ / メッセージ",
    "gdi/drawing/font": "GDI / 描画 / フォント",
    "exec/sync/module": "実行 / 同期 / モジュール",
    "file/mapping": "ファイル / マッピング",
    "registry": "レジストリ",
    "memory": "メモリ",
    "other": "その他（通知・電源・デバイス・時刻等）",
}

FILE_SET = {
    "CreateFileW", "CreateFileA", "ReadFile", "WriteFile",
    "SetFilePointer", "SetFilePointerEx", "GetFileSize", "GetFileSizeEx",
    "SetEndOfFile", "FlushFileBuffers", "GetFileAttributesW",
    "GetFileAttributesA", "GetFileAttributesExW", "SetFileAttributesW",
    "GetFileTime", "SetFileTime", "DeleteFileW", "DeleteFileA",
    "CopyFileW", "CopyFileA", "MoveFileW", "MoveFileA",
    "CreateDirectoryW", "CreateDirectoryA", "RemoveDirectoryW",
    "RemoveDirectoryA", "FindFirstFileW", "FindFirstFileA",
    "FindNextFileW", "FindNextFileA", "FindClose",
    "GetFileInformationByHandle", "CreateFileMappingW",
    "CreateFileMappingA", "CreateFileForMappingW", "OpenFileMappingW",
    "OpenFileMappingA", "MapViewOfFile", "UnmapViewOfFile",
    "FlushViewOfFile", "GetCurrentDirectoryW", "SetCurrentDirectoryW",
    "GetTempFileNameW", "GetTempPathW", "LockFile", "LockFileEx",
    "UnlockFile", "UnlockFileEx", "ReadFileEx", "WriteFileEx",
    "GetFullPathNameW", "SearchPathW", "GetDiskFreeSpaceExW",
    "GetDriveTypeW", "FindFirstFileExW", "FindFirstFileExA",
    "CloseHandle",
}
EXEC_SET = {
    "CreateThread", "ExitThread", "TerminateThread", "SuspendThread",
    "ResumeThread", "GetCurrentThread", "GetCurrentThreadId",
    "GetThreadPriority", "SetThreadPriority", "GetThreadContext",
    "SetThreadContext", "CreateProcessW", "CreateProcessA",
    "TerminateProcess", "ExitProcess", "GetCurrentProcess",
    "GetCurrentProcessId", "GetExitCodeThread", "GetExitCodeProcess",
    "WaitForSingleObject", "WaitForMultipleObjects", "Sleep",
    "CreateEventW", "CreateEventA", "OpenEventW", "OpenEventA",
    "SetEvent", "ResetEvent", "PulseEvent", "EventModify",
    "CreateMutexW", "CreateMutexA", "OpenMutexW", "OpenMutexA",
    "ReleaseMutex", "CreateSemaphoreW", "CreateSemaphoreA",
    "OpenSemaphoreW", "OpenSemaphoreA", "ReleaseSemaphore",
    "InitializeCriticalSection", "DeleteCriticalSection",
    "EnterCriticalSection", "LeaveCriticalSection",
    "TryEnterCriticalSection", "TlsAlloc", "TlsFree", "TlsGetValue",
    "TlsSetValue", "LoadLibraryW", "LoadLibraryA", "LoadLibraryExW",
    "LoadLibraryExA", "FreeLibrary", "GetProcAddress",
    "GetProcAddressA", "GetProcAddressW", "GetModuleHandleW",
    "GetModuleHandleA", "GetModuleFileNameW", "GetModuleFileNameA",
    "DisableThreadLibraryCalls", "QueueUserAPC", "SleepEx",
    "CreateFiber", "SwitchToFiber", "DeleteFiber", "ConvertThreadToFiber",
    "IsProcessorFeaturePresent", "GetThreadTimes", "OpenThread",
    "OpenProcess", "GetProcessVersion", "FreeLibraryAndExitThread",
    "GetStartupInfoW", "GetStartupInfoA", "GetCommandLineW",
    "GetCommandLineA",
}
EXEC_PREFIX = ("Interlocked",)
WIN_SET = {
    "CreateWindowExW", "CreateWindowExA", "RegisterClassW",
    "RegisterClassA", "RegisterClassExW", "UnregisterClassW",
    "UnregisterClassA", "GetClassInfoW", "GetClassInfoExW",
    "DefWindowProcW", "DefWindowProcA", "CallWindowProcW",
    "CallWindowProcA", "GetMessageW", "GetMessageA", "PeekMessageW",
    "PeekMessageA", "PostMessageW", "PostMessageA", "SendMessageW",
    "SendMessageA", "SendMessageTimeoutW", "PostThreadMessageW",
    "PostThreadMessageA", "DispatchMessageW", "DispatchMessageA",
    "TranslateMessage", "GetQueueStatus", "WaitMessage",
    "RegisterWindowMessageW", "RegisterWindowMessageA",
    "ShowWindow", "UpdateWindow", "DestroyWindow", "IsWindow",
    "IsWindowVisible", "IsWindowEnabled", "EnableWindow",
    "SetFocus", "GetFocus", "SetForegroundWindow",
    "GetForegroundWindow", "GetActiveWindow", "SetActiveWindow",
    "GetCapture", "SetCapture", "ReleaseCapture", "FindWindowW",
    "FindWindowA", "FindWindowExW", "EnumWindows", "EnumChildWindows",
    "GetParent", "SetParent", "GetWindow", "GetWindowLongW",
    "GetWindowLongA", "SetWindowLongW", "SetWindowLongA",
    "GetWindowTextW", "GetWindowTextA", "SetWindowTextW",
    "SetWindowTextA", "GetWindowTextLengthW", "GetWindowTextLengthA",
    "GetWindowRect", "GetClientRect", "MoveWindow", "SetWindowPos",
    "GetWindowPlacement", "SetWindowPlacement", "BringWindowToTop",
    "IsIconic", "IsZoomed", "ShowWindowAsync", "CloseWindow",
    "InvalidateRect", "InvalidateRgn", "ValidateRect", "ValidateRgn",
    "BeginPaint", "EndPaint", "GetUpdateRect", "GetUpdateRgn",
    "RedrawWindow", "UpdateLayeredWindow", "GetWindowRgn",
    "ScrollWindow", "ScrollWindowEx", "ScrollDC", "SetTimer",
    "KillTimer", "MessageBoxW", "MessageBoxA", "MessageBeep",
    "CreateDialogParamW", "CreateDialogParamA", "DialogBoxParamW",
    "DialogBoxParamA", "EndDialog", "IsDialogMessageW",
    "IsDialogMessageA", "GetDlgItem", "SetDlgItemTextW",
    "SetDlgItemTextA", "GetDlgItemTextW", "GetDlgItemTextA",
    "SetDlgItemInt", "GetDlgItemInt", "SendDlgItemMessageW",
    "SendDlgItemMessageA", "GetNextDlgTabItem", "GetNextDlgGroupItem",
    "MapDialogRect", "GetDialogBaseUnits", "GetWindowThreadProcessId",
    "GetDesktopWindow", "GetShellWindow", "LockWindowUpdate",
    "SetScrollInfo", "GetScrollInfo", "SetScrollPos", "GetScrollPos",
    "SetScrollRange", "GetScrollRange", "ShowScrollBar",
    "EnableScrollBar", "AdjustWindowRectEx", "ClientToScreen",
    "ScreenToClient", "MapWindowPoints", "WindowFromPoint",
    "ChildWindowFromPoint", "GetCursorPos", "SetCursorPos",
    "ClipCursor", "GetClipCursor", "ShowCursor", "SetCursor",
    "GetCursor", "LoadCursorW", "LoadCursorA", "DestroyCursor",
    "CopyIcon", "GetIconInfo", "DrawIconEx", "GetSysColor",
    "SetSysColors", "GetSysColorBrush", "SystemParametersInfoW",
    "SystemParametersInfoA", "GetSystemMetrics", "SetPropW",
    "GetPropW", "RemovePropW", "EnumPropsExW", "GetClassLongW",
    "GetClassLongA", "SetClassLongW", "SetClassLongA",
    "GetClassNameW", "GetClassNameA", "FlashWindow", "FlashWindowEx",
    "AnimateWindow", "GetGUIThreadInfo", "GetTitleBarInfo",
    "GetWindowInfo", "PrintWindow", "SetLayeredWindowAttributes",
    "GetLayeredWindowAttributes", "TrackMouseEvent", "DragDetect",
    "GetKeyState", "GetAsyncKeyState", "GetKeyboardState",
    "SetKeyboardState", "keybd_event", "mouse_event", "VkKeyScanW",
    "VkKeyScanA", "MapVirtualKeyW", "MapVirtualKeyA", "ToAscii",
    "ToUnicode", "GetKeyNameTextW", "GetKeyNameTextA",
    "OemKeyScan", "GetTabbedTextExtentW", "DrawFocusRect",
    "GetDCEx",  # window DC acquisition lives with window/message here
    "GetWindowDC", "BeginDeferWindowPos", "DeferWindowPos",
    "EndDeferWindowPos", "CheckMenuItem", "DeleteMenu", "DestroyMenu",
    "EnableMenuItem", "GetMenuItemInfoW", "GetMenuItemInfoA",
    "SetMenuItemInfoW", "GetSubMenu", "GetMenu", "SetMenu",
    "CreateMenu", "CreatePopupMenu", "AppendMenuW", "AppendMenuA",
    "InsertMenuW", "InsertMenuA", "ModifyMenuW", "ModifyMenuA",
    "RemoveMenu", "GetMenuItemCount", "GetMenuItemID",
    "GetMenuState", "TrackPopupMenu", "TrackPopupMenuEx",
    "DrawMenuBar", "HiliteMenuItem", "CreateDialogIndirectParamW",
    "CreateDialogIndirectParamA", "DialogBoxIndirectParamW",
    "DialogBoxIndirectParamA", "DefDlgProcW", "DefDlgProcA",
    "GetDlgCtrlID", "GetMessagePos", "GetMessageTime",
    "SetMenuDefaultItem", "GetMenuDefaultItem", "LoadMenuW",
    "LoadMenuA", "LoadMenuIndirectW", "IsMenu", "GetSystemMenu",
    "CheckMenuRadioItem", "SetMenuItemBitmaps", "GetMenuCheckMarkDimensions",
}
GDI_SET = {
    "CreateDCW", "CreateDCA", "CreateCompatibleDC", "DeleteDC",
    "GetDC", "ReleaseDC", "SaveDC", "RestoreDC", "GetDeviceCaps",
    "BitBlt", "StretchBlt", "PatBlt", "MaskBlt", "PlgBlt",
    "StretchDIBits", "SetDIBitsToDevice", "GetDIBits", "AlphaBlend",
    "TransparentBlt", "GradientFill", "CreateBitmap",
    "CreateBitmapIndirect", "CreateCompatibleBitmap",
    "CreateDIBSection", "CreateDIBitmap", "GetBitmapBits",
    "SetBitmapBits", "GetDIBColorTable", "SetDIBColorTable",
    "SelectObject", "DeleteObject", "GetObjectW", "GetObjectA",
    "GetCurrentObject", "GetStockObject", "CreatePen",
    "CreatePenIndirect", "CreateSolidBrush", "CreatePatternBrush",
    "CreateBrushIndirect", "CreateHatchBrush", "CreateFontW",
    "CreateFontA", "CreateFontIndirectW", "CreateFontIndirectA",
    "AddFontResourceW", "AddFontResourceA", "RemoveFontResourceW",
    "RemoveFontResourceA", "EnumFontFamiliesExW", "EnumFontFamiliesW",
    "GetTextMetricsW", "GetTextMetricsA", "GetTextExtentPoint32W",
    "GetTextExtentPoint32A", "GetTextExtentExPointW",
    "GetTextExtentExPointA", "DrawTextW", "DrawTextA", "ExtTextOutW",
    "ExtTextOutA", "TextOutW", "TextOutA", "SetTextColor",
    "GetTextColor", "SetBkColor", "GetBkColor", "SetBkMode",
    "GetBkMode", "GetTextFaceW", "GetTextFaceA", "GetCharWidth32W",
    "GetCharWidth32A", "GetCharABCWidthsW", "SetTextAlign",
    "GetTextAlign", "FillRect", "FrameRect", "InvertRect",
    "Rectangle", "RoundRect", "Ellipse", "Arc", "ArcTo", "Chord",
    "Pie", "Polygon", "PolyPolygon", "Polyline", "PolylineTo",
    "LineTo", "MoveToEx", "SetPixel", "SetPixelV", "GetPixel",
    "CreateRectRgn", "CreateRectRgnIndirect", "CreateRoundRectRgn",
    "CreateEllipticRgn", "CreatePolygonRgn", "CombineRgn",
    "OffsetRgn", "PtInRegion", "RectInRegion", "SetWindowRgn",
    "GetRgnBox", "EqualRgn", "CreatePalette", "SelectPalette",
    "RealizePalette", "GetSystemPaletteEntries", "LoadIconW",
    "LoadIconA", "LoadBitmapW", "LoadBitmapA", "LoadImageW",
    "LoadImageA", "DestroyIcon", "DrawIcon", "SetROP2", "GetROP2",
    "SetStretchBltMode", "GetStretchBltMode", "SetMapMode",
    "GetMapMode", "SetViewportOrgEx", "GetViewportOrgEx",
    "SetWindowOrgEx", "GetWindowOrgEx", "OffsetViewportOrgEx",
    "OffsetWindowOrgEx", "SetViewportExtEx", "GetViewportExtEx",
    "SetWindowExtEx", "GetWindowExtEx", "ScaleViewportExtEx",
    "ScaleWindowExtEx", "LPtoDP", "DPtoLP", "IntersectClipRect",
    "ExcludeClipRect", "OffsetClipRgn", "SelectClipRgn",
    "GetClipBox", "GetClipRgn", "PtVisible", "RectVisible",
    "ExtSelectClipRgn", "BeginPath", "EndPath", "FillPath",
    "StrokePath", "StrokeAndFillPath", "FlattenPath", "WidenPath",
    "AbortPath", "CloseFigure", "SelectClipPath", "GetPath",
    "SetMiterLimit", "GetMiterLimit", "SetArcDirection",
    "GetArcDirection", "PolyBezier", "PolyBezierTo", "PolyDraw",
    "SetColorAdjustment", "GetColorAdjustment", "CreateColorSpaceW",
    "EnumFontsW", "GetFontData", "GetGlyphOutlineW", "GetKerningPairsW",
    "GetOutlineTextMetricsW", "GetRasterizerCaps", "GetTextCharset",
    "TranslateCharsetInfo", "CreateScalableFontResourceW",
    "RemoveFontResourceExW", "AddFontResourceExW", "GetPixelFormat",
    "DescribePixelFormat", "SetPixelFormat", "ChoosePixelFormat",
    "SwapBuffers", "ExtCreatePen", "GetCharWidthFloatW",
    "TabbedTextOutW", "TabbedTextOutA", "GrayStringW", "DrawStateW",
    "DrawEdge", "DrawFrameControl", "DrawCaption", "GetBrushOrgEx",
    "SetBrushOrgEx", "GetWorldTransform", "SetWorldTransform",
    "ModifyWorldTransform", "CombineTransform", "SetGraphicsMode",
    "GetGraphicsMode", "PlayEnhMetaFile", "GetEnhMetaFileW",
    "CloseEnhMetaFile", "DeleteEnhMetaFile", "CreateEnhMetaFileW",
    "SetWinMetaFileBits", "GdiComment", "CopyRect", "EqualRect",
    "IntersectRect", "UnionRect", "OffsetRect", "PtInRect",
    "SetRect", "SetRectEmpty", "InflateRect", "IsRectEmpty",
    "ExtractIconExW", "ExtractIconExA",
    "GetTextExtentPointW",
    "GetTextExtentPointA", "AngleArc", "PolyPolyline", "SetPolyFillMode",
    "GetPolyFillMode", "FloodFill", "ExtFloodFill", "GetBoundsRect",
    "SetBoundsRect", "GetNearestColor", "GetNearestPaletteIndex",
    "ResizePalette", "UpdateColors", "SetICMMode", "GetICMProfileW",
    "EnumICMProfilesW", "ColorMatchToTarget", "CreateDiscardableBitmap",
    "GetObjectType", "SetTextJustification", "GetTextJustification",
    "GetFontLanguageInfo", "GetCharacterPlacementW", "AbortDoc",
    "StartDocW", "StartPage", "EndPage", "EndDoc", "Escape",
    "SetAbortProc", "GetViewportOrgEx", "InGdi", "GdiFlush",
    "GdiSetBatchLimit", "GdiGetBatchLimit", "GetTextColor",
}
CRT_EXACT = {
    "malloc", "free", "calloc", "realloc", "memcpy", "memmove",
    "memset", "memcmp", "memchr", "strlen", "wcslen", "strcpy",
    "wcscpy", "strncpy", "wcsncpy", "strcat", "wcscat", "strncat",
    "wcsncat", "strcmp", "wcscmp", "strncmp", "wcsncmp", "stricmp",
    "wcsicmp", "_stricmp", "_wcsicmp", "_strnicmp", "_wcsnicmp",
    "strchr", "wcschr", "strrchr", "wcsrchr", "strstr", "wcsstr",
    "strtok", "wcstok", "strspn", "wcsspn", "strcspn", "wcscspn",
    "strpbrk", "wcspbrk", "sprintf", "swprintf", "wsprintfW",
    "wsprintfA", "vsprintf", "vswprintf", "_snprintf", "_snwprintf",
    "_vsnprintf", "_vsnwprintf", "printf", "wprintf", "fprintf",
    "fwprintf", "sscanf", "swscanf", "scanf", "wscanf", "atoi",
    "atol", "atof", "wcstol", "wcstoul", "wcstod", "_wtoi", "_wtol",
    "_wtof", "itoa", "_itoa", "_itow", "_ltow", "_ultow", "strtol",
    "strtoul", "strtod", "toupper", "tolower", "isalpha", "isdigit",
    "isspace", "isupper", "islower", "isalnum", "isprint", "iscntrl",
    "ispunct", "isxdigit", "towupper", "towlower", "iswalpha",
    "iswdigit", "iswspace", "iswupper", "iswlower", "iswalnum",
    "iswprint", "qsort", "bsearch", "abs", "labs", "div", "ldiv",
    "rand", "srand", "fopen", "fclose", "fread", "fwrite", "fseek",
    "ftell", "fgetc", "fputc", "fgets", "fputs", "fprintf",
    "fflush", "feof", "ferror", "clearerr", "rewind", "setbuf",
    "setvbuf", "ungetc", "getc", "putc", "getchar", "putchar",
    "gets", "puts", "perror", "remove", "rename", "tmpnam",
    "clock", "time", "mktime", "localtime", "gmtime", "strftime",
    "wcsftime", "difftime", "asctime", "ctime", "sin", "cos", "tan",
    "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "exp",
    "log", "log10", "pow", "sqrt", "ceil", "floor", "fabs", "fmod",
    "frexp", "ldexp", "modf", "abort", "exit", "_exit", "atexit",
    "getenv", "system", "raise", "signal", "longjmp", "setjmp",
    "_setjmp", "assert", "_assert", "MultiByteToWideChar",
    "WideCharToMultiByte", "lstrlenW", "lstrlenA", "lstrcpyW",
    "lstrcpyA", "lstrcpynW", "lstrcpynA", "lstrcatW", "lstrcatA",
    "lstrcmpW", "lstrcmpA", "lstrcmpiW", "lstrcmpiA", "CharUpperW",
    "CharUpperA", "CharLowerW", "CharLowerA", "CharNextW",
    "CharNextA", "CharPrevW", "CharPrevA", "IsCharAlphaW",
    "IsCharAlphaA", "IsCharAlphaNumericW", "IsCharUpperW",
    "IsCharLowerW", "CompareStringW", "CompareStringA",
    "GetStringTypeW", "GetStringTypeA", "GetStringTypeExW",
    "LCMapStringW", "LCMapStringA", "FoldStringW", "FoldStringA",
    "EnumCalendarInfoW", "GetLocaleInfoW", "GetLocaleInfoA",
    "SetLocaleInfoW", "GetCPInfo", "GetACP", "GetOEMCP",
    "IsValidCodePage", "GetTimeFormatW", "GetDateFormatW",
    "GetNumberFormatW", "GetCurrencyFormatW", "EnumDateFormatsW",
    "EnumTimeFormatsW", "GetUserDefaultLCID", "GetSystemDefaultLCID",
    "GetUserDefaultLangID", "GetSystemDefaultLangID",
    "GetThreadLocale", "SetThreadLocale", "IsValidLocale",
    "EnumSystemLocalesW", "GetLocaleInfoEx", "VerLanguageNameW",
}


MEMORY_SET = {"GetProcessHeap"}
MEMORY_EXCLUDE = {  # prefix matches that are not allocators
    "GlobalDeleteAtom", "GlobalAddAtom", "GlobalFindAtom",
    "GlobalGetAtomNameW", "GlobalGetAtomNameA", "GlobalMemoryStatus",
    "GlobalMemoryStatusEx",
}


def classify(name):
    if name.startswith("Reg"):
        return "registry"
    if name in FILE_SET:
        return "file/mapping"
    if name in MEMORY_SET:
        return "memory"
    if (name.startswith(("Local", "Global", "Virtual", "Heap"))
            and "Window" not in name and name not in MEMORY_EXCLUDE):
        return "memory"
    if name in EXEC_SET or name.startswith(EXEC_PREFIX):
        return "exec/sync/module"
    if name in WIN_SET:
        return "window/message"
    if name in GDI_SET:
        return "gdi/drawing/font"
    if (name.startswith(("_", "?", "$")) or name in CRT_EXACT):
        return "crt/string"
    return "other"


def main():
    ec = json.load(open(os.path.join(CENSUS, "exports-census.json")))
    cj = json.load(open(os.path.join(CENSUS, "callers.json")))
    ordmap = {int(k): v for k, v in ec["coredll.dll"]["ordmap"].items()}

    apps = set()
    for _k, v in cj.items():
        for p in v:
            if p.startswith("NAND:") and p.split("/")[-1] == "AppMain.exe":
                apps.add(p)

    named, ordinal_only = {}, {}
    for k, v in cj.items():
        if not k.startswith("coredll.dll!"):
            continue
        callers = sorted(p for p in v if p in apps)
        if not callers:
            continue
        sym = k.split("!", 1)[1]
        if sym.startswith("#"):
            ordinal_only[int(sym[1:])] = callers
        else:
            named[sym] = callers

    resolved = {}
    unresolved = []
    for o, callers in sorted(ordinal_only.items()):
        nm = ordmap.get(o)
        if nm is None:
            unresolved.append(o)
        else:
            resolved[o] = nm

    # union of distinct functions (name-keyed), merging both forms
    funcs = {}
    for nm, callers in named.items():
        e = funcs.setdefault(nm, {"name": nm, "ordinal": None,
                                  "forms": [], "callers": set()})
        e["forms"].append("name")
        e["callers"].update(callers)
    name2ord = {v: k for k, v in ordmap.items()}
    for o, nm in resolved.items():
        e = funcs.setdefault(nm, {"name": nm, "ordinal": None,
                                  "forms": [], "callers": set()})
        e["ordinal"] = o
        e["forms"].append("ordinal")
        e["callers"].update(ordinal_only[o])
    for nm, e in funcs.items():
        if e["ordinal"] is None:
            e["ordinal"] = name2ord.get(nm)
        e["callers"] = sorted(e["callers"])
        e["group"] = classify(nm)

    by_group = collections.defaultdict(list)
    for nm in sorted(funcs):
        by_group[funcs[nm]["group"]].append(nm)

    out = {
        "population": {
            "modules": sorted(apps),
            "n_modules": len(apps),
            "import_keys_total": len(named) + len(ordinal_only),
            "import_keys_named": len(named),
            "import_keys_ordinal_only": len(ordinal_only),
            "ordinal_keys_resolved": len(resolved),
            "ordinal_keys_unresolved": unresolved,
            "distinct_functions_after_resolution": len(funcs),
            "resolved_names_subset_of_named_set":
                set(resolved.values()) <= set(named),
        },
        "groups": {g: {"count": len(by_group[g]),
                       "functions": by_group[g]} for g in GROUPS},
        "functions": [funcs[nm] for nm in sorted(funcs)],
    }
    with open(os.path.join(HERE, "m0-classification.json"), "w") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)

    lines = []
    lines.append("# M0 分類結果（アプリ直接呼び coredll、ordmap 適用後）")
    lines.append("")
    lines.append(f"- 対象モジュール: {len(apps)} 本（NAND 上の AppMain.exe、"
                 "ファイル名は大文字小文字を区別）")
    lines.append(f"- import キー総数: {len(named) + len(ordinal_only)}"
                 f"（名前 {len(named)} / 序数のみ {len(ordinal_only)}）")
    lines.append(f"- 序数のみキーの ordmap 解決: {len(resolved)} / "
                 f"{len(ordinal_only)} 件（未解決 {len(unresolved)} 件）")
    lines.append("- 解決された名前は全て名前形式の集合に含まれていた。"
                 "すなわち序数のみキーは新規関数ではなく、同一関数群を"
                 "序数で import しているモジュール側の記録である。")
    lines.append(f"- 解決後の相異なる関数数: {len(funcs)} 件")
    lines.append("")
    lines.append("| 機能群 | 件数 |")
    lines.append("|---|---|")
    for g in GROUPS:
        lines.append(f"| {GROUP_JA[g]} | {len(by_group[g])} |")
    lines.append(f"| 合計 | {len(funcs)} |")
    lines.append("")
    for g in GROUPS:
        lines.append(f"## {GROUP_JA[g]}（{len(by_group[g])} 件）")
        lines.append("")
        lines.append(", ".join(by_group[g]))
        lines.append("")
    with open(os.path.join(HERE, "m0-classification.md"), "w") as f:
        f.write("\n".join(lines))

    print(f"modules={len(apps)} keys={len(named)+len(ordinal_only)} "
          f"named={len(named)} ord={len(ordinal_only)} "
          f"resolved={len(resolved)} unresolved={len(unresolved)} "
          f"distinct={len(funcs)}")
    for g in GROUPS:
        print(f"  {GROUP_JA[g]}: {len(by_group[g])}")


if __name__ == "__main__":
    sys.exit(main())
