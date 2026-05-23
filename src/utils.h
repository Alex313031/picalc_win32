#ifndef PICALCWIN32_UTILS_H_
#define PICALCWIN32_UTILS_H_

#include "constants.h"
#include "framework.h"

// clang-format off
#include <logging.h>
// clang-format on

// Typedef for accessing undocumented RtlGetNtVersionNumbers in ntdll.dll
typedef void(WINAPI* RtlGetNtVersionNumbers_t)(DWORD* pNtMajorVersion,
                                               DWORD* pNtMinorVersion,
                                               DWORD* pNtBuildNumber);

extern bool g_debug_mode;

// Save client area as a .BMP photo, capturing moment menu was clicked. On
// success, if outSavedPath is non-null, the chosen path is written there so
// the caller can surface it (e.g. via UserMessage); pass nullptr to skip.
bool SaveClientBitmap(HWND hWnd, std::wstring* outSavedPath);

// Gets the desired font at the specified size (in pixels). Face name
// defaults to Tahoma. Caller owns the returned HFONT and must
// DeleteObject it when done. Returns nullptr on failure.
HFONT GetFont(int size, std::wstring font = L"Tahoma", bool italic = false);

// Fills a rect with a solid color. Wraps the CreateSolidBrush + FillRect
// + DeleteObject trio so call sites don't have to repeat all three (and
// can't forget the DeleteObject and leak a GDI brush).
bool FillRectWithColor(HDC hdc, const RECT& rc, COLORREF color);

// Fills a convex polygon with a solid color. Pen and brush share the color
// so the rasterized outline doesn't leave a 1px halo around the fill.
[[maybe_unused]] void FillPolygon(HDC hdc, const POINT* pts, int count, COLORREF color);

// Fills a rect with a vertical gradient: rc.top maps to topColor and
// rc.bottom maps to bottomColor, linearly interpolated per scan line.
// One row per pixel - simple and avoids needing GdiGradientFill /
// msimg32. Slightly wasteful (one brush per row) but trivial at the
// scales we draw at.
void FillRectWithGradient(HDC hdc, const RECT& rc, COLORREF topColor, COLORREF bottomColor);

// Gets the current side by side directory, regardless of where .exe is started from
const std::wstring GetExeDir();

// Helper functions for MessageBoxW
bool InfoBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

bool WarnBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

bool ErrorBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

// Gets version as human readable wstring.
const std::wstring GetVersionString();

// Returns APP_NAME as wstring, for easier usage.
const std::wstring GetAppName();

// Returns true on Windows XP (5.1) or later, false on Windows 2000 (5.0)
// or earlier. Used to gate styles / APIs that exist only on WinXP.
bool IsWindowsXpOrLater();

// For checking system's commctl32.dll
bool IsCommCtrlAtLeast(const DWORD to_compare);

// Gets if a given menu has an item CHECKED or not.
bool IsMenuChecked(HMENU menu, UINT id);

// Gets if a given menu has an item GRAYED or not.
bool IsMenuGrayed(HMENU menu, UINT id);

// Toggles a given menu IDs CHECKED state.
bool ToggleMenuCheck(HWND hWnd, UINT id);

// Confirmation dialog for exit
bool ConfirmExit(HWND hWnd);

// Centers `hWnd` on screen. When `multimon` is true, uses
// MonitorFromWindow + GetMonitorInfo so the window centres inside the
// work area (minus the taskbar) of whichever monitor it currently sits
// on - DPI / multi-display friendly. When false, falls back to the
// primary display's full desktop rect via GetDesktopWindow, the
// classic single-monitor approach that ignores the taskbar. Returns
// false on failure (null hWnd, GetMonitorInfo failure, etc.).
bool CenterWindowOnScreen(HWND hWnd, bool multimon);

// Welcome message to be displayed in console
const std::wstring GetWelcomeMessage();

// Plays a given .wav resource ID.
bool PlayWav(UINT resid);

// =========================================================================
// Result file (GetExeDir() + kResultsFile, UTF-16 LE)
// =========================================================================

// Opens the result file at GetExeDir()+kResultsFile in append mode.
// Creates the file with a UTF-16 LE BOM if it does not yet exist;
// seeks to the end if it does, preserving prior results.
// Must be called before any AppendToResultFile calls.
bool OpenResultFile();

// Appends `char_count` wide characters from `data` directly to the file.
// No newline is added - the caller controls formatting. Returns false if
// the file is not open or the write fails. FILE_ATTRIBUTE_NORMAL (no
// write-through) is used so the OS can buffer large sequential writes;
// call CloseResultFile() (or FlushResultFile()) to force a flush to disk.
bool AppendToResultFile(const wchar_t* data, size_t char_count);

// Convenience overload for std::wstring.
bool AppendToResultFile(const std::wstring& text);

// Truncates the file back to just the UTF-16 LE BOM.
bool ClearResultFile();

// Flushes any buffered writes to disk without closing.
bool FlushResultFile();

// Flushes and closes the result file handle. Safe to call if already closed.
void CloseResultFile();

// Returns true if the result file handle is currently open.
bool IsResultFileOpen();

// Opens the result file in the system default viewer via ShellExecuteW.
bool ShellOpenResultFile(HWND hWnd);

// Appends `line` + CRLF to the result file.
bool WriteLineToResultFile(const std::wstring& line);

// Appends the 90-asterisk separator + CRLF to the result file,
// matching the width used by PrintOutputSeparator().
bool WriteSeparatorToResultFile();

#endif // PICALCWIN32_UTILS_H_
