// Common utility functions

#include "utils.h"

#include <shlwapi.h>

#include "globals.h"
#include "resource.h"
#include "strings.h"

// For Run file dialog
#define RFD_NOBROWSE        0x00000001
#define RFD_NODEFFILE       0x00000002
#define RFD_USEFULLPATHDIR  0x00000004
#define RFD_NOSHOWOPEN      0x00000008
#define RFD_WOW_APP         0x00000010
#define RFD_NOSEPMEMORY_BOX 0x00000020

static RUN_FILE_DLG_ pfnRunFileDlg = nullptr;

static DWORD GetCommCtrlVersion();

// =========================================================================
// Functions
// =========================================================================

// Opens a system Save As dialog and writes a snapshot of the window's
// client area to a 32-bit BMP file at the path the user chose. On
// success, if outSavedPath is non-null, the chosen path is written there
// so the caller can surface it. Pass nullptr to skip.
//
// We snapshot the client area into a memory bitmap *before* opening
// the Save dialog so the saved frame matches what the user saw when
// they clicked the menu - a live calc keeps pushing output lines while
// the dialog is up, but only the on-disk clone is frozen.
//
// Source is the screen DC at the window's client-area origin (mapped
// via ClientToScreen). BitBlt-from-window-DC would miss child controls
// because the parent uses WS_CLIPCHILDREN; the screen already has
// everything composited so we just blit from there. PrintWindow would
// be cleaner but it's XP+ and we want to keep Win2k support.
//
// BMP layout (no palette for 32-bit):
//   BITMAPFILEHEADER  (14 bytes) - magic 'BM', file size, pixel data offset
//   BITMAPINFOHEADER  (40 bytes) - dimensions, bit depth, compression
//   Pixel data        (w * h * 4 bytes) - 32-bit BGRA, bottom-up row order
bool SaveClientBitmap(HWND hWnd, std::wstring* outSavedPath) {
  if (hWnd == nullptr) {
    return false;
  }

  // ---- Snapshot the client area immediately. ----
  RECT client = {};
  if (!GetClientRect(hWnd, &client)) {
    return false;
  }
  const int width  = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) {
    return false;
  }

  // Translate the client (0, 0) to screen coords so we know where on
  // the screen DC to blit from.
  POINT origin = {0, 0};
  ClientToScreen(hWnd, &origin);

  HDC hdcScreen = GetDC(nullptr);
  if (hdcScreen == nullptr) {
    return false;
  }
  HBITMAP hbm_snap = CreateCompatibleBitmap(hdcScreen, width, height);
  if (hbm_snap == nullptr) {
    ReleaseDC(nullptr, hdcScreen);
    return false;
  }
  HDC hdc_snap = CreateCompatibleDC(hdcScreen);
  if (hdc_snap == nullptr) {
    ReleaseDC(nullptr, hdcScreen);
    DeleteObject(hbm_snap);
    return false;
  }
  HBITMAP hbm_default = static_cast<HBITMAP>(SelectObject(hdc_snap, hbm_snap));
  BitBlt(hdc_snap, 0, 0, width, height, hdcScreen, origin.x, origin.y, SRCCOPY);
  ReleaseDC(nullptr, hdcScreen);

  // ---- Save dialog. The snapshot above is frozen; live updates to
  // ---- the window keep happening underneath. ----
  wchar_t szFile[MAX_PATH] = {};
  OPENFILENAMEW ofn        = {};
  ofn.lStructSize          = sizeof(OPENFILENAMEW);
  ofn.hwndOwner            = hWnd;
  ofn.lpstrFile            = szFile;
  ofn.nMaxFile             = MAX_PATH;
  ofn.lpstrFilter          = L"Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
  ofn.nFilterIndex         = 1;
  ofn.lpstrDefExt          = L"bmp";
  ofn.lpstrTitle           = L"Save Screenshot As";
  ofn.Flags                = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
  const bool got_path      = GetSaveFileNameW(&ofn);
  if (!got_path) {
    SelectObject(hdc_snap, hbm_default);
    DeleteDC(hdc_snap);
    DeleteObject(hbm_snap);
    return false;
  }

  // ---- Read out the snapshot pixels in BMP-friendly format. ----
  BITMAPINFOHEADER bmp_info = {};
  bmp_info.biSize           = sizeof(BITMAPINFOHEADER);
  bmp_info.biWidth          = width;
  bmp_info.biHeight         = height;
  bmp_info.biPlanes         = 1;
  bmp_info.biBitCount       = 32;
  bmp_info.biCompression    = BI_RGB;
  bmp_info.biSizeImage      = static_cast<DWORD>(width * height * 4);
  std::vector<BYTE> pixels(bmp_info.biSizeImage);
  const int scan_lines = GetDIBits(hdc_snap, hbm_snap, 0, static_cast<UINT>(height), pixels.data(),
                                   reinterpret_cast<BITMAPINFO*>(&bmp_info), DIB_RGB_COLORS);

  // Drop the snapshot's GDI handles before file I/O so we don't hold them
  // across a slow CreateFile / WriteFile.
  SelectObject(hdc_snap, hbm_default);
  DeleteDC(hdc_snap);
  DeleteObject(hbm_snap);

  if (scan_lines == 0) {
    return false;
  }

  // ---- Write the BMP file. ----
  const DWORD pixelDataOffset   = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  BITMAPFILEHEADER bmp_file_hdr = {};
  bmp_file_hdr.bfType           = 0x4D42; // 'BM' signature
  bmp_file_hdr.bfSize           = pixelDataOffset + bmp_info.biSizeImage;
  bmp_file_hdr.bfOffBits        = pixelDataOffset;
  HANDLE hFile =
      CreateFileW(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written      = 0;
  const bool success = WriteFile(hFile, &bmp_file_hdr, sizeof(bmp_file_hdr), &written, nullptr) &&
                       WriteFile(hFile, &bmp_info, sizeof(bmp_info), &written, nullptr) &&
                       WriteFile(hFile, pixels.data(), bmp_info.biSizeImage, &written, nullptr);
  CloseHandle(hFile);
  if (success && outSavedPath != nullptr) {
    *outSavedPath = szFile;
  }
  return success;
}

HFONT GetFont(int size, std::wstring font, bool italic) {
  HDC hdc = GetDC(nullptr);
  if (!hdc) {
    return nullptr;
  }
  if (font.empty()) {
    LOG(ERROR) << L"Empty font supplied!";
  }
  // Negative height = "character height" in logical units (the cap
  // box), so passing -size yields ~size-pixel-tall glyphs on a
  // standard MM_TEXT DC. ANTIALIASED_QUALITY keeps big text from
  // looking jagged - the rest of the app embraces a retro aliased
  // look but 72-px text without smoothing is unreadable.
  int height;
  if (size <= 0) {
    height = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
  } else {
    height = -size;
  }
  ReleaseDC(nullptr, hdc);
  HFONT hGetFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, italic ? TRUE : FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font.c_str());
  if (hGetFont == nullptr) {
    LOG(ERROR) << L"failed to get " << font << L" font!";
    return nullptr;
  }
  return hGetFont;
}

bool FillRectWithColor(HDC hdc, const RECT& rc, COLORREF color) {
  bool success = true;
  if (hdc == nullptr) {
    return false;
  }
  HBRUSH hBrush = CreateSolidBrush(color);
  if (hBrush == nullptr) {
    return false;
  }
  if (!FillRect(hdc, &rc, hBrush)) {
    success = false;
  }
  DeleteObject(hBrush);
  return success;
}

void FillRectWithGradient(HDC hdc, const RECT& rc, COLORREF topColor, COLORREF bottomColor) {
  if (hdc == nullptr) {
    return;
  }
  const int height = rc.bottom - rc.top;
  if (height <= 0 || rc.right <= rc.left) {
    return;
  }
  const int r1 = GetRValue(topColor);
  const int g1 = GetGValue(topColor);
  const int b1 = GetBValue(topColor);
  const int r2 = GetRValue(bottomColor);
  const int g2 = GetGValue(bottomColor);
  const int b2 = GetBValue(bottomColor);
  // One filled row per scan line. Denominator is (height - 1) so the
  // very last row lands exactly on bottomColor instead of one step
  // shy of it.
  const double inv_span = (height > 1) ? 1.0 / (height - 1) : 0.0;
  for (int row_y = rc.top; row_y < rc.bottom; ++row_y) {
    const double frac = (row_y - rc.top) * inv_span;
    const int red     = static_cast<int>(std::lround(r1 + (r2 - r1) * frac));
    const int green   = static_cast<int>(std::lround(g1 + (g2 - g1) * frac));
    const int blue    = static_cast<int>(std::lround(b1 + (b2 - b1) * frac));
    HBRUSH hBr        = CreateSolidBrush(RGB(red, green, blue));
    if (hBr == nullptr) {
      continue;
    }
    RECT row = {rc.left, row_y, rc.right, row_y + 1};
    FillRect(hdc, &row, hBr);
    DeleteObject(hBr);
  }
}

const std::wstring GetExeDir() {
  wchar_t exe_path[MAX_PATH];
  HMODULE this_app = GetModuleHandleW(nullptr);
  if (!this_app) {
    return std::wstring();
  }
  DWORD got_path = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (got_path == 0 || got_path >= MAX_PATH) {
    return std::wstring();
  }

  // Find the last backslash to get the directory
  std::wstring fullPath(exe_path);
  size_t lastSlash = fullPath.find_last_of(L"\\/");
  std::wstring retval;
  if (lastSlash != std::wstring::npos) {
    retval = fullPath.substr(0, lastSlash + 1); // Include trailing slash
  } else {
    retval = fullPath;
  }
  return retval;
}

// MessageBoxW with MB_OK can be dismissed several ways the user considers
// equivalent: clicking OK (IDOK), clicking the X close button (IDCANCEL),
// or pressing Esc (IDCANCEL). All of those mean "the box showed and the
// user dismissed it" - which is what these helpers want to report as
// success. Only a 0 return means the box failed to display in the first
// place (bad hWnd, OOM, no desktop access, etc.); that's the real false.
// `hWnd ? hWnd : mainHwnd` falls back to the main window when the caller
// passed null - useful from helpers that don't have an hWnd of their own.
bool InfoBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONINFORMATION) != 0;
}

bool WarnBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONWARNING) != 0;
}

bool ErrorBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONERROR) != 0;
}

const std::wstring GetVersionString() {
  // VERSION_STRING is a narrow C string literal built by stringize macros,
  // so we can't feed it straight to std::wstring. Build the wide form
  // directly from the same integer macros (single source of truth in
  // version.h) - std::to_wstring keeps it standards-clean across MinGW
  // and MSVC alike.
  return std::to_wstring(MAJOR_VERSION) + L"." + std::to_wstring(MINOR_VERSION) + L"." +
         std::to_wstring(BUILD_VERSION);
}

const std::wstring GetAppName() {
  const std::wstring app_name = std::wstring(APP_NAME);
  return app_name;
}

bool GetRawNtVersion(UINT* major, UINT* minor, UINT* build) {
  HMODULE hNtDll = GetModuleHandleW(kNtDll);
  if (hNtDll == nullptr) {
    return false;
  }

  // Primary: RtlGetNtVersionNumbers (XP+, undocumented). Packs a build-type
  // flag into the top 4 bits of buildVer - mask them off so callers see the
  // plain build number (e.g. 2600 for XP SP3, 19045 for Win10 22H2).
  const RtlGetNtVersionNumbers_t pfnRtlGetNtVersionNumbers =
      reinterpret_cast<RtlGetNtVersionNumbers_t>(GetProcAddress(hNtDll, "RtlGetNtVersionNumbers"));
  if (pfnRtlGetNtVersionNumbers != nullptr) {
    DWORD majorVer = 0, minorVer = 0, buildVer = 0;
    pfnRtlGetNtVersionNumbers(&majorVer, &minorVer, &buildVer);
    if (majorVer != 0) {
      if (major != nullptr) {
        *major = static_cast<UINT>(majorVer);
      }
      if (minor != nullptr) {
        *minor = static_cast<UINT>(minorVer);
      }
      if (build != nullptr) {
        *build = static_cast<UINT>(buildVer & 0x0FFFFFFFu);
      }
      return true;
    }
  }

  // Fallback: RtlGetVersion (Win2K+, documented NT-native, not shimmed by
  // application-compatibility manifests unlike GetVersionExW on Win8.1+).
  typedef LONG(WINAPI * RtlGetVersion_t)(OSVERSIONINFOW*);
  const RtlGetVersion_t pfnRtlGetVersion =
      reinterpret_cast<RtlGetVersion_t>(GetProcAddress(hNtDll, "RtlGetVersion"));
  if (pfnRtlGetVersion != nullptr) {
    OSVERSIONINFOW vi      = {};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (pfnRtlGetVersion(&vi) == 0 && vi.dwMajorVersion != 0) {
      if (major != nullptr) {
        *major = static_cast<UINT>(vi.dwMajorVersion);
      }
      if (minor != nullptr) {
        *minor = static_cast<UINT>(vi.dwMinorVersion);
      }
      if (build != nullptr) {
        *build = static_cast<UINT>(vi.dwBuildNumber);
      }
      return true;
    }
  }

  return false;
}

bool IsWindowsXpOrLater() {
  UINT major = 0;
  UINT minor = 0;
  // Use the raw NT version: can't be spoofed by the manifest-driven shim that
  // GetVersionExW / RtlGetVersion go through, anything higher than 5.0 returns true.
  if (GetRawNtVersion(&major, &minor, nullptr)) {
    return major > 5u || (major == 5u && minor >= 1u);
  }
  return false; // Safe fallback, assume Win 2K
}

const std::wstring GetNTVerString() {
  UINT major = 0;
  UINT minor = 0;
  UINT build = 0;
  std::wstring retval;
  std::wostringstream wostr;
  if (GetRawNtVersion(&major, &minor, &build)) {
    wostr << major << L"." << minor << L"." << build;
    retval = wostr.str();
  } else {
    LOG(ERROR) << L"Unable to get NT version!";
    retval = L"Unknown";
  }
  return retval;
}

static DWORD GetCommCtrlVersion() {
  // Resolve the system comctl32.dll path explicitly. GetSystemDirectoryW
  // returns 0 on failure, or >= MAX_PATH if our buffer was too small (in
  // which case it reports the required size). Either is fatal for us -
  // bail rather than fall through with an empty path that would let
  // LoadLibraryW search the standard DLL order and silently bypass the
  // "explicitly use the system one" intent.
  wchar_t systemDir[MAX_PATH];
  const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return 0x0;
  }
  const std::wstring comctl32_path = std::wstring(systemDir) + L"\\" + kComCtl32Dll;

  HMODULE hComCtl32Dll = LoadLibraryW(comctl32_path.c_str());
  if (hComCtl32Dll == nullptr) {
    return 0x0;
  }

  DWORD dwVersion = 0x0;
  DLLGETVERSIONPROC pDllGetVersion =
      reinterpret_cast<DLLGETVERSIONPROC>(GetProcAddress(hComCtl32Dll, "DllGetVersion"));
  if (pDllGetVersion == nullptr) {
    return 0x0;
  } else {
    DLLVERSIONINFO dvi    = {sizeof(dvi)};
    const HRESULT hresult = pDllGetVersion(&dvi);
    if (hresult == S_OK) {
      dwVersion = _PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion);
    }
  }
  FreeLibrary(hComCtl32Dll);
  return dwVersion;
}

bool IsCommCtrlAtLeast(const DWORD to_compare) {
  const DWORD kCommCtrlVer = GetCommCtrlVersion();
  return kCommCtrlVer >= to_compare;
}

HWND AddTooltip(HWND hWndParent, HWND hWndControl, HINSTANCE hInst, const wchar_t* tooltipText) {
  if (hWndParent == nullptr || hWndControl == nullptr || tooltipText == nullptr) {
    return nullptr;
  }

  HWND hTooltip = CreateWindowExW(
      WS_EX_NOACTIVATE, TOOLTIPS_CLASS, nullptr, TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndParent, nullptr, hInst, nullptr);
  if (hTooltip == nullptr) {
    return nullptr;
  }

  TOOLINFOW ti = {};
  // Windows 2000 even with I.E. 6 reports false, since system comctl32.dll is not updated.
  static const bool can_use_582_controls = IsCommCtrlAtLeast(dwComCtl32TargetVer);
  if (can_use_582_controls) {
    ti.cbSize = sizeof(ti);
  } else {
    // MinGW's TOOLINFOW always includes lpReserved (V3 layout). Windows 2000's
    // comctl32 v5.81 only supports up to V2 (through lParam) - passing
    // sizeof(ti) makes TTM_ADDTOOLW reject the struct on Win2k. Fall back to
    // the V2 size on pre-XP systems.
    ti.cbSize = TTTOOLINFOW_V2_SIZE;
  }
  ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
  ti.hwnd     = hWndParent;
  ti.uId      = reinterpret_cast<UINT_PTR>(hWndControl);
  ti.lpszText = const_cast<wchar_t*>(tooltipText);

  SendMessageW(hTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
  return hTooltip;
}

// Menu-state helpers. The .rc's CHECKED flags double as default-setting
// storage - ApplyMenuDefaults reads each item's initial state at startup
// and pushes it into the engine, so adjusting the defaults is just a
// matter of toggling CHECKED in the .rc.
bool IsMenuChecked(HMENU menu, UINT id) {
  if (menu == nullptr) {
    return false;
  }
  const UINT state = GetMenuState(menu, id, MF_BYCOMMAND);
  // GetMenuState returns 0xFFFFFFFF when the item isn't found.
  if (state == static_cast<UINT>(-1)) {
    return false;
  }
  return (state & MF_CHECKED) != 0;
}

// Reads MF_GRAYED off a menu item so the .rc's GRAYED flag can act as a
// "this feature is disabled at build time" switch the same way CHECKED
// acts as a default-on / default-off toggle. Returns false if the item
// isn't found - missing items aren't "greyed", they're just absent.
bool IsMenuGrayed(HMENU menu, UINT id) {
  if (menu == nullptr) {
    return false;
  }
  const UINT state = GetMenuState(menu, id, MF_BYCOMMAND);
  if (state == static_cast<UINT>(-1)) {
    return false;
  }
  return (state & MF_GRAYED) != 0;
}

// Flips a checkable menu item and returns the new state. Used by the
// WM_COMMAND handlers so a single line covers "toggle + push into engine".
bool ToggleMenuCheck(HWND hWnd, UINT id) {
  HMENU menu = GetMenu(hWnd);
  if (menu == nullptr) {
    return false;
  }
  const bool now_checked = !IsMenuChecked(menu, id);
  CheckMenuItem(menu, id, MF_BYCOMMAND | (now_checked ? MF_CHECKED : MF_UNCHECKED));
  return now_checked;
}

// Confirmation dialog for exit
bool ConfirmExit(HWND hWnd) {
  const int exit_dialog = MessageBoxW(hWnd, L"Are you sure you want to Exit?", L"Confirm Exit",
                                      MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
  return exit_dialog == IDYES;
}

bool ConfirmClearResults(HWND hWnd) {
  const int clear_dialog =
      MessageBoxW(hWnd, L"Are you sure you want to clear the results file?",
                  L"Confirm Clear Results", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
  return clear_dialog == IDYES;
}

bool CenterWindowOnScreen(HWND hWnd, bool multimon) {
  if (hWnd == nullptr) {
    return false;
  }
  RECT window_rect;
  if (!GetWindowRect(hWnd, &window_rect)) {
    return false;
  }
  const int window_w = window_rect.right - window_rect.left;
  const int window_h = window_rect.bottom - window_rect.top;

  RECT screen_rect;
  if (multimon) {
    // Pick the monitor `hWnd` currently sits on; NEARESTONOTNULL guarantees
    // a valid HMONITOR even for off-screen windows. rcWork excludes the
    // taskbar / docked appbars so the centred window doesn't end up half
    // under them.
    HMONITOR hMon            = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {};
    monitor_info.cbSize      = sizeof(monitor_info);
    if (hMon == nullptr || !GetMonitorInfoW(hMon, &monitor_info)) {
      return false;
    }
    screen_rect = monitor_info.rcWork;
  } else {
    // Classic single-monitor path: GetDesktopWindow is the whole primary
    // screen rect, taskbar and all. Predates the multimon APIs and keeps
    // working on Win2k for callers that don't care about per-monitor
    // placement.
    if (!GetWindowRect(GetDesktopWindow(), &screen_rect)) {
      return false;
    }
  }
  const int screen_w = screen_rect.right - screen_rect.left;
  const int screen_h = screen_rect.bottom - screen_rect.top;
  const int new_x    = (screen_rect.left + (screen_w - (window_w * 2))) / 2;
  const int new_y    = (screen_rect.top + (screen_h - window_h)) / 2;
  // SWP_NOSIZE / NOZORDER / NOACTIVATE: pure reposition, don't disturb
  // size, stacking order, or focus.
  return SetWindowPos(hWnd, nullptr, new_x, new_y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

// Log name and version
const std::wstring GetWelcomeMessage() {
  std::wostringstream wostr;
  wostr << L"---- Welcome to " << GetAppName() << L" ----" << L"\n"
        << L"       Version: " << GetVersionString() << (is_debug ? L" DEBUG" : L"");
  const std::wstring welcome = wostr.str();
  return welcome;
}

// =========================================================================
// Result file
// =========================================================================

static HANDLE g_result_file = INVALID_HANDLE_VALUE;

bool OpenResultFile() {
  if (g_result_file != INVALID_HANDLE_VALUE) {
    CloseResultFile();
  }
  const std::wstring path = GetExeDir() + kResultsFile;
  if (path.length() >= MAX_PATH) {
    return false;
  }
  // CREATE_ALWAYS: each run starts with a fresh file - one result at a time.
  // FILE_ATTRIBUTE_NORMAL (no FILE_FLAG_WRITE_THROUGH) lets the OS buffer
  // writes in the page cache; critical for sequential million-digit output
  // where per-write kernel flushes would dominate wall time.
  // FILE_SHARE_READ | FILE_SHARE_WRITE: the result viewer opens a second
  // read handle concurrently. Both sides must declare the same share set or
  // Wine's CreateFileW blocks instead of returning a sharing-violation error.
  g_result_file =
      CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_result_file == INVALID_HANDLE_VALUE) {
    return false;
  }
  // Write UTF-16 LE BOM (FF FE) so readers know the encoding.
  DWORD written = 0;
  if (!WriteFile(g_result_file, &kUTF16LEBOM, sizeof(kUTF16LEBOM), &written, nullptr)) {
    CloseResultFile();
    return false;
  }
  return true;
}

bool AppendToResultFile(const wchar_t* data, size_t char_count) {
  if (g_result_file == INVALID_HANDLE_VALUE || data == nullptr || char_count == 0) {
    return false;
  }
  const DWORD byte_count = static_cast<DWORD>(char_count * sizeof(wchar_t));
  DWORD written          = 0;
  return WriteFile(g_result_file, data, byte_count, &written, nullptr) && written == byte_count;
}

bool AppendToResultFile(const std::wstring& text) {
  return AppendToResultFile(text.c_str(), text.size());
}

bool ClearResultFile() {
  if (g_result_file == INVALID_HANDLE_VALUE) {
    if (!OpenResultFile()) {
      return false;
    }
  }
  FlushFileBuffers(g_result_file);
  if (SetFilePointer(g_result_file, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
      GetLastError() != NO_ERROR) {
    return false;
  }
  if (!SetEndOfFile(g_result_file)) {
    return false;
  }
  // Re-write BOM after truncation so the file stays valid UTF-16.
  DWORD written = 0;
  const bool cleared =
      WriteFile(g_result_file, &kUTF16LEBOM, sizeof(kUTF16LEBOM), &written, nullptr);
  if (cleared) {
    LOG(INFO) << L"Cleared result file " << kResultsFile;
  }
  return cleared;
}

bool FlushResultFile() {
  if (g_result_file == INVALID_HANDLE_VALUE) {
    return false;
  }
  return FlushFileBuffers(g_result_file) != FALSE;
}

void CloseResultFile() {
  if (g_result_file == INVALID_HANDLE_VALUE) {
    return;
  }
  FlushFileBuffers(g_result_file);
  CloseHandle(g_result_file);
  g_result_file = INVALID_HANDLE_VALUE;
}

bool IsResultFileOpen() {
  return g_result_file != INVALID_HANDLE_VALUE;
}

// SetFilePointerEx is XP+; use SetFilePointer with LONG high for Win2k compat.
LONGLONG GetResultFilePosition() {
  if (g_result_file == INVALID_HANDLE_VALUE) {
    return -1;
  }
  LONG high       = 0;
  const DWORD low = SetFilePointer(g_result_file, 0, &high, FILE_CURRENT);
  if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
    return -1;
  }
  return (static_cast<LONGLONG>(high) << 32) | static_cast<LONGLONG>(low);
}

bool TruncateResultFileTo(LONGLONG position) {
  if (g_result_file == INVALID_HANDLE_VALUE || position < 0) {
    return false;
  }
  LONG high       = static_cast<LONG>(position >> 32);
  const LONG low  = static_cast<LONG>(position & 0xFFFFFFFF);
  const DWORD ret = SetFilePointer(g_result_file, low, &high, FILE_BEGIN);
  if (ret == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
    return false;
  }
  return SetEndOfFile(g_result_file) != FALSE;
}

bool WriteLineToResultFile(const std::wstring& line) {
  return AppendToResultFile(line + L"\r\n");
}

// One shot play of embedded .wav file
bool PlayWav(UINT resid) {
  if (resid < IDR_MAIN) {
    LOG(ERROR) << L"Resource ID too low";
    return false;
  }
  static const DWORD playflags = SND_RESOURCE | SND_ASYNC | SND_NODEFAULT;
  const bool played            = PlaySoundW(MAKEINTRESOURCEW(resid), g_hInstance, playflags);
  if (!played) {
    LOG(ERROR) << L"Failed to play embedded .wav file " << resid;
  }
  return played;
}

// Opens the "Run" shell dialog from shell32.dll
void OpenRunDialog(HWND hWnd) {
  if (g_hInstance) {
    static const HICON kRunDlgIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_WINFLAG));
    if (kRunDlgIcon != nullptr) {
      wchar_t szCurDir[MAX_PATH];
      GetCurrentDirectoryW(MAX_PATH, szCurDir);
      // Open "Run"
      HMODULE hShell32Dll = GetModuleHandleW(L"shell32.dll");
      if (hShell32Dll) {
        pfnRunFileDlg = reinterpret_cast<RUN_FILE_DLG_>(GetProcAddress(hShell32Dll, (LPCSTR)(61)));
        if (pfnRunFileDlg) {
          LOG(INFO) << L"Opening RunFileDlg";
          pfnRunFileDlg(hWnd, kRunDlgIcon, (LPWSTR)szCurDir, kRunTitle, kRunPrompt,
                        RFD_USEFULLPATHDIR | RFD_WOW_APP);
        } else {
          LOG(ERROR) << L"Failed to open run dialog.";
        }
      } else {
        LOG(ERROR) << L"Failed to get shell32.dll handle.";
      }
      DestroyIcon(kRunDlgIcon); // Cleanup icon
    }
    LOG(DEBUG) << L"Opened Run dialog";
  }
}

bool IsRunningOnWine(std::string* outWineVer) {
  HMODULE ntdll = GetModuleHandleW(kNtDll);
  if (ntdll == nullptr) {
    return false;
  }
  // Cleaner one-liner via a typedef than splitting the function-pointer
  // declaration and the assignment across two lines.
  typedef const char*(CDECL * WineGetVersion_t)(void);
  const WineGetVersion_t pwine_get_version =
      reinterpret_cast<WineGetVersion_t>(GetProcAddress(ntdll, "wine_get_version"));
  if (pwine_get_version == nullptr) {
    return false;
  }
  // Wine's implementation always returns a valid string in practice, but
  // std::string(nullptr) is undefined behavior - guard it.
  const char* wineVer = pwine_get_version();
  if (wineVer == nullptr) {
    return false;
  }
  // outWineVer is optional: callers that only care about the bool can pass
  // nullptr. Without this null-check we'd crash on the dereference below.
  if (outWineVer != nullptr) {
    *outWineVer = wineVer;
  }
  return true;
}
