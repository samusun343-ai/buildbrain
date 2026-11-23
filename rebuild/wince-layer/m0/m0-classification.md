# M0 分類結果（アプリ直接呼び coredll、ordmap 適用後）

- 対象モジュール: 27 本（NAND 上の AppMain.exe、ファイル名は大文字小文字を区別）
- import キー総数: 874（名前 437 / 序数のみ 437）
- 序数のみキーの ordmap 解決: 437 / 437 件（未解決 0 件）
- 解決された名前は全て名前形式の集合に含まれていた。すなわち序数のみキーは新規関数ではなく、同一関数群を序数で import しているモジュール側の記録である。
- 解決後の相異なる関数数: 437 件

| 機能群 | 件数 |
|---|---|
| CRT / 文字列 | 119 |
| ウィンドウ / メッセージ | 96 |
| GDI / 描画 / フォント | 65 |
| 実行 / 同期 / モジュール | 33 |
| ファイル / マッピング | 25 |
| レジストリ | 10 |
| メモリ | 8 |
| その他（通知・電源・デバイス・時刻等） | 81 |
| 合計 | 437 |

## CRT / 文字列（119 件）

??0exception@std@@QAA@ABV01@@Z, ??0exception@std@@QAA@PBD@Z, ??0exception@std@@QAA@XZ, ??1exception@std@@UAA@XZ, ??2@YAPAXI@Z, ??3@YAXPAX@Z, ??_7type_info@@6B@, ??_L@YAXPAXIHP6AX0@Z1@Z, ??_M@YAXPAXIHP6AX0@Z@Z, ?what@exception@std@@UBAPBDXZ, CompareStringW, GetLocaleInfoW, MultiByteToWideChar, WideCharToMultiByte, _XcptFilter, __C_specific_handler, __CxxFrameHandler3, __CxxThrowException, __GetUserKData, __addd, __divd, __divs, __dtoi, __eqs, __i64tod, __i64tos, __itod, __itos, __led, __les, __lts, __muld, __muls, __nes, __report_gsfailure, __rt_sdiv, __rt_sdiv64by64, __rt_srem64by64, __rt_udiv, __rt_udiv64by64, __rt_urem64by64, __security_gen_cookie2, __stod, __stoi, __stoi64, __stou, __subd, __utod, __utos, _getstdfilex, _isctype, _itow_s, _msize, _purecall, _set_invalid_parameter_handler, _snwprintf, _stricmp, _vsnwprintf, _vsnwprintf_s, _wcsdup, _wcsicmp, _wcslwr_s, _wcsnicmp, _wcsupr_s, _wfopen, _wfopen_s, _wtol, atoi, calloc, fclose, feof, ferror, fflush, fgetc, fopen, fprintf, fputs, fread, free, fseek, ftell, fwrite, lstrcmpW, lstrcmpiW, malloc, memcmp, memcpy, memmove, memset, printf, qsort, rand, realloc, sprintf, srand, strcat, strchr, strcmp, strcpy, strcspn, strlen, strncmp, strncpy, strspn, strstr, strtoul, swprintf, swscanf, vsprintf, wcscat, wcschr, wcscmp, wcscpy, wcslen, wcsncmp, wcsncpy, wcsrchr, wcsstr, wsprintfW

## ウィンドウ / メッセージ（96 件）

AdjustWindowRectEx, BeginDeferWindowPos, BeginPaint, BringWindowToTop, CallWindowProcW, CheckMenuItem, ClientToScreen, CreateDialogIndirectParamW, CreateWindowExW, DefDlgProcW, DefWindowProcW, DeferWindowPos, DeleteMenu, DestroyMenu, DestroyWindow, DialogBoxIndirectParamW, DispatchMessageW, EnableMenuItem, EnableWindow, EndDeferWindowPos, EndDialog, EndPaint, EnumWindows, FindWindowW, GetActiveWindow, GetCapture, GetClassInfoW, GetClassNameW, GetClientRect, GetCursorPos, GetDesktopWindow, GetDlgCtrlID, GetDlgItem, GetFocus, GetForegroundWindow, GetKeyState, GetMenuItemInfoW, GetMessagePos, GetMessageW, GetNextDlgTabItem, GetParent, GetScrollInfo, GetSubMenu, GetSysColor, GetSysColorBrush, GetSystemMetrics, GetWindow, GetWindowDC, GetWindowLongW, GetWindowRect, GetWindowTextLengthW, GetWindowTextW, GetWindowThreadProcessId, InsertMenuW, InvalidateRect, InvalidateRgn, IsDialogMessageW, IsWindow, IsWindowEnabled, IsWindowVisible, KillTimer, LoadCursorW, LoadMenuW, MapDialogRect, MapWindowPoints, MessageBeep, MessageBoxW, MoveWindow, PeekMessageW, PostMessageW, ReleaseCapture, ScreenToClient, ScrollWindowEx, SendDlgItemMessageW, SendMessageW, SetActiveWindow, SetCapture, SetCursor, SetDlgItemTextW, SetFocus, SetForegroundWindow, SetMenuItemInfoW, SetScrollInfo, SetScrollPos, SetScrollRange, SetTimer, SetWindowLongW, SetWindowPos, SetWindowTextW, ShowWindow, SystemParametersInfoW, TranslateMessage, UnregisterClassW, UpdateWindow, ValidateRect, keybd_event

## GDI / 描画 / フォント（65 件）

AddFontResourceW, BitBlt, CombineRgn, CopyRect, CreateBitmap, CreateCompatibleBitmap, CreateCompatibleDC, CreateDIBSection, CreateFontIndirectW, CreatePen, CreateRectRgn, CreateRectRgnIndirect, CreateSolidBrush, DeleteDC, DeleteObject, DestroyIcon, DrawTextW, Ellipse, EnumFontFamiliesExW, EqualRect, ExtTextOutW, ExtractIconExW, FillRect, GetBkColor, GetClipBox, GetDC, GetDIBColorTable, GetDeviceCaps, GetObjectW, GetPixel, GetRgnBox, GetStockObject, GetTextColor, GetTextExtentExPointW, InflateRect, IntersectRect, IsRectEmpty, LineTo, LoadBitmapW, LoadIconW, MoveToEx, OffsetRect, PatBlt, Polygon, PtInRect, RectVisible, Rectangle, ReleaseDC, RemoveFontResourceW, RestoreDC, RoundRect, SaveDC, SelectObject, SetBitmapBits, SetBkColor, SetBkMode, SetDIBColorTable, SetPixel, SetRect, SetRectEmpty, SetStretchBltMode, SetTextColor, SetWindowRgn, StretchBlt, UnionRect

## 実行 / 同期 / モジュール（33 件）

CreateEventW, CreateMutexW, CreateProcessW, CreateThread, DeleteCriticalSection, EnterCriticalSection, EventModify, ExitThread, FreeLibrary, GetExitCodeThread, GetModuleFileNameW, GetModuleHandleW, GetProcAddressW, GetThreadPriority, InitializeCriticalSection, InterlockedCompareExchange, InterlockedDecrement, InterlockedExchange, InterlockedIncrement, LeaveCriticalSection, LoadLibraryW, OpenEventW, ReleaseMutex, ResumeThread, SetThreadPriority, Sleep, SuspendThread, TerminateProcess, TerminateThread, TlsGetValue, TlsSetValue, WaitForMultipleObjects, WaitForSingleObject

## ファイル / マッピング（25 件）

CloseHandle, CopyFileW, CreateDirectoryW, CreateFileMappingW, CreateFileW, DeleteFileW, FindClose, FindFirstFileW, FindNextFileW, FlushFileBuffers, GetDiskFreeSpaceExW, GetFileAttributesW, GetFileSize, GetFileTime, GetTempPathW, MapViewOfFile, MoveFileW, ReadFile, RemoveDirectoryW, SetEndOfFile, SetFileAttributesW, SetFilePointer, SetFileTime, UnmapViewOfFile, WriteFile

## レジストリ（10 件）

RegCloseKey, RegCreateKeyExW, RegDeleteKeyW, RegEnumKeyExW, RegOpenKeyExW, RegQueryValueExW, RegSetValueExW, RegisterClassW, RegisterHotKey, RegisterWindowMessageW

## メモリ（8 件）

GetProcessHeap, HeapAllocTrace, HeapFree, LocalAlloc, LocalFree, LocalReAlloc, VirtualAlloc, VirtualFree

## その他（通知・電源・デバイス・時刻等）（81 件）

CeGetVolumeInfoW, CompareFileTime, ConvertDefaultLocale, DeviceIoControl, FileTimeToLocalFileTime, FileTimeToSystemTime, FindResourceW, FormatMessageW, GetLastError, GetLocalTime, GetMouseMovePoints, GetOpenFileNameW, GetProcessIndexFromID, GetSaveFileNameW, GetSystemPowerStatusEx, GetSystemPowerStatusEx2, GetSystemTime, GetTickCount, GetTimeZoneInformation, GetVersionExW, GlobalDeleteAtom, GlobalMemoryStatus, ImageList_LoadImage, IsChild, IsClipboardFormatAvailable, LoadAcceleratorsW, LoadResource, LoadStringW, NKDbgPrintfW, OutputDebugStringW, PlaySoundW, PostQuitMessage, RaiseException, SHGetSpecialFolderPath, SHLoadDIBitmap, SendNotifyMessageW, SetLastError, SetRectRgn, SizeofResource, SystemTimeToFileTime, TlsCall, TranslateAcceleratorW, TransparentImage, UnregisterHotKey, fgetpos, fopen_s, memcpy_s, memmove_s, sndPlaySoundW, sprintf_s, sscanf_s, strcat_s, strcpy_s, strncpy_s, swprintf_s, swscanf_s, waveInAddBuffer, waveInClose, waveInGetPosition, waveInMessage, waveInOpen, waveInPrepareHeader, waveInReset, waveInStart, waveInStop, waveInUnprepareHeader, waveOutClose, waveOutGetPosition, waveOutGetVolume, waveOutMessage, waveOutOpen, waveOutPause, waveOutPrepareHeader, waveOutReset, waveOutRestart, waveOutSetVolume, waveOutUnprepareHeader, waveOutWrite, wcscat_s, wcscpy_s, wcsncpy_s
