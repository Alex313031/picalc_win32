// Result viewer popup window.
// A modeless WS_EX_TOOLWINDOW popup that shows the contents of the result
// file. Toggled open/closed by the "Open Results" / "Close Results File"
// button. Always uses the system default black-on-white edit colors.

#include "results.h"

#include "globals.h"
#include "strings.h"
#include "utils.h"

namespace {

  const wchar_t* const kResultWindowClassName = L"PicalcResultWnd";

  HWND s_result_hwnd  = nullptr;
  HWND s_result_edit  = nullptr;
  HWND s_hidden_owner = nullptr;
  size_t s_wrap_width = kResultWrapWidth;
  std::wstring s_raw_content;

} // namespace

// Forward declaration so RegisterResultWindowClass can reference the proc
// before its full definition at the bottom of this file.
static LRESULT CALLBACK ResultWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Reads up to kMaxResultViewBytes from the result file and returns the
// content as a wide string with the UTF-16 LE BOM stripped.
// GetFileSizeEx requires XP+, so GetFileSize is used for Win2k compat.
static std::wstring LoadResultContent() {
  const std::wstring path = GetExeDir() + kResultsFile;
  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return std::wstring();
  }
  DWORD size_high      = 0;
  const DWORD size_low = GetFileSize(hFile, &size_high);
  if (size_low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
    CloseHandle(hFile);
    return std::wstring();
  }
  const ULONGLONG file_size =
      (static_cast<ULONGLONG>(size_high) << 32) | static_cast<ULONGLONG>(size_low);
  const DWORD read_bytes = (file_size > static_cast<ULONGLONG>(kMaxResultViewBytes))
                               ? kMaxResultViewBytes
                               : static_cast<DWORD>(file_size);
  if (read_bytes < sizeof(wchar_t)) {
    CloseHandle(hFile);
    return std::wstring();
  }
  std::vector<BYTE> buf(read_bytes);
  DWORD bytes_read = 0;
  if (!ReadFile(hFile, buf.data(), read_bytes, &bytes_read, nullptr) ||
      bytes_read < sizeof(wchar_t)) {
    CloseHandle(hFile);
    return std::wstring();
  }
  CloseHandle(hFile);
  // Strip the UTF-16 LE BOM (FF FE) written by OpenResultFile.
  const BYTE* data = buf.data();
  DWORD data_bytes = bytes_read;
  if (data_bytes >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
    data += 2;
    data_bytes -= 2;
  }
  if (data_bytes < sizeof(wchar_t)) {
    return std::wstring();
  }
  return std::wstring(reinterpret_cast<const wchar_t*>(data), data_bytes / sizeof(wchar_t));
}

// =========================================================================
// Public API
// =========================================================================

bool RegisterResultWindowClass(HINSTANCE hInstance) {
  WNDCLASSEXW wc   = {};
  wc.cbSize        = sizeof(wc);
  wc.style         = 0; // No CS_HREDRAW/CS_VREDRAW — edit fills the client area
  wc.lpfnWndProc   = ResultWindowProc;
  wc.hInstance     = hInstance;
  wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
  wc.lpszClassName = kResultWindowClassName;
  if (RegisterClassExW(&wc) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool IsResultWindowOpen() {
  return s_result_hwnd != nullptr;
}

HWND GetResultHwnd() {
  return s_result_hwnd;
}

// Returns the number of digit-width characters that fit in the edit control's
// usable text area. Reads the edit's own client rect — which the system has
// already clipped to exclude the WS_EX_CLIENTEDGE border and WS_VSCROLL
// scrollbar as non-client area — then subtracts the edit's internal text
// margins (EM_GETMARGINS). These margins vary by platform and font and cannot
// be reliably inferred from system metrics alone. Falls back to kResultWrapWidth
// if s_result_edit is not yet sized or any query fails.
static size_t ComputeWrapWidth() {
  if (s_result_edit == nullptr) {
    return kResultWrapWidth;
  }
  RECT editRc = {};
  if (!GetClientRect(s_result_edit, &editRc) || editRc.right <= editRc.left) {
    return kResultWrapWidth;
  }
  // LOWORD = left margin, HIWORD = right margin, both in pixels.
  const LRESULT mg = SendMessageW(s_result_edit, EM_GETMARGINS, 0, 0);
  const int usable = (editRc.right - editRc.left) - (int)LOWORD(mg) - (int)HIWORD(mg);
  if (usable <= 0) {
    return kResultWrapWidth;
  }
  HDC hdc = GetDC(s_result_edit);
  if (hdc == nullptr) {
    return kResultWrapWidth;
  }
  const HFONT hFont  = reinterpret_cast<HFONT>(GetStockObject(SYSTEM_FONT));
  const HGDIOBJ hOld = SelectObject(hdc, hFont);
  // Measure a digit directly — digits are wider than tmAveCharWidth in
  // proportional fonts, and the result content is primarily decimal digits.
  SIZE sz       = {};
  const BOOL ok = GetTextExtentPoint32W(hdc, L"0", 1, &sz);
  SelectObject(hdc, hOld);
  ReleaseDC(s_result_edit, hdc);
  if (!ok || sz.cx <= 0) {
    return kResultWrapWidth;
  }
  // Subtract one column as a safety margin: GDI character measurement and the
  // edit control's internal line-break measurement diverge by ~1 char on real
  // Win2k/XP, causing the last digit of each pre-wrapped line to overflow.
  const size_t raw_cols = static_cast<size_t>(usable / sz.cx);
  const size_t cols     = (raw_cols > 1) ? raw_cols - 1 : raw_cols;
  return (cols > 0) ? cols : kResultWrapWidth;
}

// Inserts \r\n after every `width` characters on lines that exceed that width.
// Keeps lines short so the word-wrap edit layout stays O(lines) rather than
// O(chars) for a million-digit result on one long line.
static std::wstring WrapLongLines(const std::wstring& text, size_t width) {
  std::wstring out;
  out.reserve(text.size() + (text.size() / width + 1) * 2);
  size_t col = 0;
  for (const wchar_t ch : text) {
    if (ch == L'\r' || ch == L'\n') {
      col = 0;
    } else {
      if (col == width) {
        out += L'\r';
        out += L'\n';
        col = 0;
      }
      ++col;
    }
    out += ch;
  }
  return out;
}

void ReloadResultWindow() {
  if (s_result_edit == nullptr) {
    return;
  }
  s_raw_content = LoadResultContent();
  const std::wstring display =
      s_raw_content.empty() ? std::wstring() : WrapLongLines(s_raw_content, s_wrap_width);
  // Suppress redraws while setting text so the edit control does a single
  // layout pass rather than repainting on every internal update.
  SendMessageW(s_result_edit, WM_SETREDRAW, FALSE, 0);
  SetWindowTextW(s_result_edit, display.empty() ? L"(No results yet)" : display.c_str());
  SendMessageW(s_result_edit, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(s_result_edit, nullptr, TRUE);
  // Place cursor at the beginning so the file opens at the top.
  SendMessageW(s_result_edit, EM_SETSEL, 0, 0);
  SendMessageW(s_result_edit, EM_SCROLLCARET, 0, 0);
}

void CloseResultWindow() {
  if (s_result_hwnd == nullptr) {
    return;
  }
  // WM_DESTROY clears s_result_hwnd and s_result_edit.
  DestroyWindow(s_result_hwnd);
  if (hOpenOutButton != nullptr) {
    SetWindowTextW(hOpenOutButton, kOpenResultLabel);
  }
}

void ToggleResultWindow(HWND parent) {
  if (s_result_hwnd != nullptr) {
    CloseResultWindow();
    LOG(INFO) << L"Closed results window";
    return;
  }
  static constexpr DWORD style =
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_SIZEBOX | WS_CLIPCHILDREN;
  // Use a hidden WS_EX_TOOLWINDOW popup as the owner instead of mainHwnd.
  // Owned windows have no taskbar button, but owned windows are always
  // Z-above their owner. Using a hidden dummy owner breaks that constraint
  // with mainHwnd while still keeping the result window off the taskbar.
  // ShutDownApp closes it explicitly before destroying the main window.
  if (s_hidden_owner == nullptr) {
    s_hidden_owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
                                     nullptr, nullptr, g_hInstance, nullptr);
  }
  // Place the result window to the right of the main window, clamped so it
  // stays within the virtual screen (handles multi-monitor layouts too).
  RECT mainRc = {};
  GetWindowRect(mainHwnd, &mainRc);
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  const int pos_x =
      std::max(vx, std::min(static_cast<int>(mainRc.right), vx + vw - kResultWindowWidth));
  const int pos_y =
      std::max(vy, std::min(static_cast<int>(mainRc.top), vy + vh - kResultWindowHeight));

  const std::wstring title = std::wstring(kResultPopupTitle) + L" - " + kResultsFile;
  s_result_hwnd = CreateWindowExW(WS_EX_WINDOWEDGE, kResultWindowClassName, title.c_str(), style, pos_x, pos_y,
                                  kResultWindowWidth, kResultWindowHeight, s_hidden_owner, nullptr,
                                  g_hInstance, nullptr);
  if (s_result_hwnd == nullptr) {
    return;
  }
  ShowWindow(s_result_hwnd, SW_SHOWNORMAL);
  UpdateWindow(s_result_hwnd);
  if (hOpenOutButton != nullptr) {
    SetWindowTextW(hOpenOutButton, kCloseResultLabel);
  }
}

// =========================================================================
// Window procedure
// =========================================================================

static LRESULT CALLBACK ResultWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      static constexpr DWORD dwEdit =
          dwCHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL;
      s_result_edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, WC_EDIT, L"Loading...", dwEdit, 0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
          hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(1)), g_hInstance, nullptr);
      if (s_result_edit == nullptr) {
        return -1;
      }
      // Classic old win16 style font
      static const HFONT hResultFont = reinterpret_cast<HFONT>(GetStockObject(SYSTEM_FONT));
      SendMessageW(s_result_edit, WM_SETFONT, reinterpret_cast<WPARAM>(hResultFont), FALSE);
      // The default multiline edit limit on Win2k is 32 KB; raise it to
      // match our file-load cap so large results aren't silently truncated.
      SendMessageW(s_result_edit, EM_SETLIMITTEXT, kMaxResultViewBytes / sizeof(wchar_t), 0);
      // Pre-size the edit to its final dimensions now so ComputeWrapWidth can
      // read the edit's real client rect (WS_EX_CLIENTEDGE and WS_VSCROLL
      // already excluded) and its font-derived internal margins.
      {
        RECT parentRc = {};
        GetClientRect(hWnd, &parentRc);
        MoveWindow(s_result_edit, 0, 0, parentRc.right - parentRc.left,
                   parentRc.bottom - parentRc.top, FALSE);
      }
      s_wrap_width = ComputeWrapWidth();
      // Inherit icons from the main window's class so the caption and
      // Alt+Tab switcher show the same icon as the main program.
      {
        const HICON hBig   = reinterpret_cast<HICON>(GetClassLongPtrW(mainHwnd, GCLP_HICON));
        const HICON hSmall = reinterpret_cast<HICON>(GetClassLongPtrW(mainHwnd, GCLP_HICONSM));
        if (hBig) {
          SendMessageW(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
        }
        if (hSmall) {
          SendMessageW(hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
        }
      }
      // Load content now while the parent window is not yet visible — Wine
      // skips layout/paint for children of unmapped windows, so this is
      // fast even for large files. Post-calc reloads hit ReloadResultWindow
      // directly and rely on WM_SETREDRAW to reduce layout overhead.
      ReloadResultWindow();
      SetFocus(s_result_edit);
      LOG(INFO) << L"Opened results window";
      return 0;
    }
    case WM_ERASEBKGND:
      // The edit fills the entire client area; suppress the erase pass so
      // the parent background never flashes through during resize.
      return TRUE;
    case WM_SIZE:
      if (s_result_edit != nullptr) {
        MoveWindow(s_result_edit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        // Debounce re-wrap: reset the timer on every resize event so we only
        // re-pre-wrap once, 100 ms after the user stops dragging the border.
        KillTimer(hWnd, 1);
        SetTimer(hWnd, 1, 100, nullptr);
      }
      return 0;
    case WM_TIMER:
      if (wParam == 1) {
        KillTimer(hWnd, 1);
        const size_t new_width = ComputeWrapWidth();
        if (new_width != s_wrap_width && !s_raw_content.empty()) {
          // Scale the first visible line to approximate the same position in
          // the re-wrapped text so the view doesn't jump to top or bottom.
          const int old_first    = (int)SendMessageW(s_result_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
          const size_t old_width = s_wrap_width;
          s_wrap_width           = new_width;
          const std::wstring display = WrapLongLines(s_raw_content, s_wrap_width);
          SendMessageW(s_result_edit, WM_SETREDRAW, FALSE, 0);
          SetWindowTextW(s_result_edit, display.c_str());
          SendMessageW(s_result_edit, WM_SETREDRAW, TRUE, 0);
          InvalidateRect(s_result_edit, nullptr, TRUE);
          // Proportional scroll: line N at old width ≈ line N*(old/new) at new.
          const int new_first =
              (old_width > 0)
                  ? (int)((long long)old_first * (long long)old_width / (long long)new_width)
                  : 0;
          SendMessageW(s_result_edit, EM_LINESCROLL, 0, (LPARAM)new_first);
        }
      }
      return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      SetBkColor(hdc, RGB_WHITE);
      SetTextColor(hdc, RGB_BLACK);
      return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_GETMINMAXINFO: {
      LPMINMAXINFO pMinMaxInfo      = reinterpret_cast<LPMINMAXINFO>(lParam);
      pMinMaxInfo->ptMinTrackSize.x = kResultWindowMinWidth;
      pMinMaxInfo->ptMinTrackSize.y = kResultWindowMinHeight;
      const int MAXWIDTH            = GetSystemMetrics(SM_CXMAXIMIZED);
      const int MAXHEIGHT           = GetSystemMetrics(SM_CYMAXIMIZED);
      pMinMaxInfo->ptMaxTrackSize.x = MAXWIDTH;
      pMinMaxInfo->ptMaxTrackSize.y = MAXHEIGHT;
      break;
    }
    case WM_CLOSE:
      CloseResultWindow();
      return 0;
    case WM_DESTROY:
      KillTimer(hWnd, 1);
      s_result_hwnd = nullptr;
      s_result_edit = nullptr;
      s_raw_content.clear();
      s_raw_content.shrink_to_fit();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}
