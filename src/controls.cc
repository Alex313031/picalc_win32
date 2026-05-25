// Controls creation/management and layout/logic

#include "controls.h"

#include "constants.h"
#include "main.h"
#include "resource.h"
#include "strings.h"
#include "utils.h"

// =========================================================================
// Splitter window class
// =========================================================================

static const wchar_t* kSplitterClassName = L"PicalcSplitter";

// -1 = uninitialised, gets centred on the first LayoutChildren call.
static int s_splitter_y = -1;
static bool s_dragging  = false;

// Group box that frames the top-pane controls. Sized in LayoutChildren
// since its bottom edge tracks the splitter position.
static HWND s_hGroupBox = nullptr;

static LRESULT CALLBACK SplitterProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_LBUTTONDOWN:
      SetCapture(hWnd);
      s_dragging = true;
      return 0;
    case WM_LBUTTONUP:
      if (s_dragging) {
        ReleaseCapture();
        s_dragging = false;
      }
      return 0;
    case WM_MOUSEMOVE: {
      if (!s_dragging) {
        return 0;
      }
      // lParam is in splitter-local client coords; translate to parent
      // client coords so we can store the absolute splitter top edge.
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      HWND parent = GetParent(hWnd);
      if (parent == nullptr) {
        return 0;
      }
      ClientToScreen(hWnd, &pt);
      ScreenToClient(parent, &pt);
      s_splitter_y = pt.y;
      LayoutChildren(parent);
      return 0;
    }
    case WM_SETCURSOR:
      // Always show the vertical-resize cursor while the pointer is over
      // the splitter, even between drags.
      SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
      return TRUE;
    case WM_ERASEBKGND:
      return TRUE;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT rc;
      GetClientRect(hWnd, &rc);
      // Classic button-face fill with a 1px raised top + bottom edge so
      // the bar is visible against the surrounding grey without looking
      // like a heavyweight 3D bevel.
      FillRectWithColor(hdc, rc, g_bkg_color);
      DrawEdge(hdc, &rc, EDGE_RAISED, BF_TOP | BF_BOTTOM);
      // Grab handle: 1px tall, 6px wide, centered in the bar.
      const int cx      = rc.right - rc.left;
      const int cy      = rc.bottom - rc.top;
      const int hndl_x  = rc.left + (cx - kSplitterHandleWidth) / 2;
      const int hndl_y  = rc.top  + (cy - kSplitterHandleHeight) / 2;
      const RECT handle = {hndl_x, hndl_y,
                           hndl_x + kSplitterHandleWidth, hndl_y + kSplitterHandleHeight};
      FillRectWithColor(hdc, handle, kSplitterHandleColor);
      EndPaint(hWnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

bool RegisterSplitterClass(HINSTANCE hInstance) {
  // RegisterClassExW returns 0 if the class is already registered
  // *and* the second registration disagrees with the first - getting
  // back the original ATOM for a duplicate-but-identical registration
  // would be 0 with GetLastError = ERROR_CLASS_ALREADY_EXISTS. Treat
  // that as success so this function stays idempotent.
  WNDCLASSEXW wc = {};
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = SplitterProc;
  wc.hInstance     = hInstance;
  wc.hCursor       = LoadCursorW(nullptr, IDC_SIZENS);
  wc.hbrBackground = nullptr; // We paint in WM_PAINT
  wc.lpszClassName = kSplitterClassName;
  if (RegisterClassExW(&wc) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool CreateChildControls(HWND parent) {
  if (parent == nullptr) {
    return false;
  }
  // Bottom pane: multi-line, read-only edit. Starts hidden-size (0x0);
  // LayoutChildren places it correctly on the first WM_SIZE.
  static constexpr DWORD dwOutput =
      dwCHILD | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE | ES_READONLY;
  hOutputEdit = CreateWindowExW(
      WS_EX_WINDOWEDGE, WC_EDIT, L"",
      dwOutput | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
      0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OUTPUT_EDIT)),
      g_hInstance, nullptr);
  if (hOutputEdit == nullptr) {
    return false;
  }

  hSplitter = CreateWindowExW(
      0, kSplitterClassName, L"", dwCHILD, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SPLITTER)), g_hInstance, nullptr);
  if (hSplitter == nullptr) {
    return false;
  }

  // Group box: created before the inner controls so it sits behind them
  // in Z-order. Position is 0,0,0,0 here; LayoutChildren sizes it to
  // fill the top pane with a 7px margin on all sides.
  s_hGroupBox = CreateWindowExW(
      0, WC_BUTTON, kCntrlsGroupLabel, dwCHILD | BS_GROUPBOX | WS_CLIPSIBLINGS,
      0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CONTROLS_GROUP)), g_hInstance, nullptr);
  if (s_hGroupBox == nullptr) {
    return false;
  }

  // Top pane: stacked rows of "label + combobox" pickers. SS_CENTERIMAGE
  // vertically centres the label text so it aligns with the combo's
  // text baseline.
  const int row1_y = kGroupOuterTop + kGroupInnerPad;
  const int row2_y = row1_y + kControlHeight + kPadTop;
  const int row3_y = row2_y + kControlHeight + kPadTop;
  const int row4_y = row3_y + kButtonHeight + kVGap;
  const int row5_y = row4_y + kButtonHeight + kVGap;
  const int row6_y = row5_y + kButtonHeight + kVGap;
  // All rows (label+combo and button pairs) have the same width, so a single
  // centering formula applies to every row. The controls area of the groupbox
  // has kGroupMargin padding on both sides; centre within that region.
  constexpr int kRowWidth    = kLabelWidth + kHGap + kComboWidth;
  constexpr int kAreaCenterX = kGroupMargin + (kRowWidth + 2 * kGroupMargin) / 2;
  constexpr int kRowLeft     = kAreaCenterX - kRowWidth / 2;
  constexpr int kRowCol2     = kRowLeft + kLabelWidth + kHGap;

  hDigitsLabel = CreateWindowExW(
      0, WC_STATIC, kNumDigitsLabel, dwCHILD | SS_LEFT | SS_CENTERIMAGE,
      kRowLeft, row1_y, kLabelWidth, kControlHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DIGITS_LABEL)), g_hInstance, nullptr);
  if (hDigitsLabel == nullptr) {
    return false;
  }

  hDigitsCombo = CreateWindowExW(
      0, WC_COMBOBOX, L"", dwCHILD | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST,
      kRowCol2, row1_y, kComboWidth, kComboDropHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DIGITS_COMBO)), g_hInstance, nullptr);
  if (hDigitsCombo == nullptr) {
    return false;
  }

  hThreadsLabel = CreateWindowExW(
      0, WC_STATIC, kNumThreadsLabel, dwCHILD | SS_LEFT | SS_CENTERIMAGE,
      kRowLeft, row2_y, kLabelWidth, kControlHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_THREADS_LABEL)), g_hInstance, nullptr);
  if (hThreadsLabel == nullptr) {
    return false;
  }

  hThreadsCombo = CreateWindowExW(
      0, WC_COMBOBOX, L"", dwCHILD | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST,
      kRowCol2, row2_y, kComboWidth, kComboDropHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_THREADS_COMBO)), g_hInstance, nullptr);
  if (hThreadsCombo == nullptr) {
    return false;
  }

  // Row 3: Calculate + Stop buttons. BS_CENTER | BS_VCENTER spell out
  // the centering even though BS_PUSHBUTTON already centres by default.
  hStartButton = CreateWindowExW(
      0, WC_BUTTON, kStartButtonLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER | BS_DEFPUSHBUTTON,
      kRowLeft, row3_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_START_BUTTON)), g_hInstance, nullptr);
  if (hStartButton == nullptr) {
    return false;
  }

  hStopButton = CreateWindowExW(
      0, WC_BUTTON, kStopButtonLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowCol2, row3_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_STOP_BUTTON)), g_hInstance, nullptr);
  if (hStopButton == nullptr) {
    return false;
  }

  // Row 4: Open Out File + Console toggle buttons.
  hOpenOutButton = CreateWindowExW(
      0, WC_BUTTON, kOpenResultLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowLeft, row4_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OPENOUT_BUTTON)), g_hInstance, nullptr);
  if (hOpenOutButton == nullptr) {
    return false;
  }

  hConsoleButton = CreateWindowExW(
      0, WC_BUTTON, kShowConsoleLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowCol2, row4_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CONSOLE_BUTTON)), g_hInstance, nullptr);
  if (hConsoleButton == nullptr) {
    return false;
  }

  // Row 5: Clear Results + Clear Output buttons.
  hClearResultButton = CreateWindowExW(
      0, WC_BUTTON, kClearResultLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowLeft, row5_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CLEARRESULT_BUTTON)), g_hInstance, nullptr);
  if (hClearResultButton == nullptr) {
    return false;
  }

  hClearOutputButton = CreateWindowExW(
      0, WC_BUTTON, kClearOutputLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowCol2, row5_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CLEAROUTPUT_BUTTON)), g_hInstance, nullptr);
  if (hClearOutputButton == nullptr) {
    return false;
  }

  // Row 6: About + Exit buttons.
  hAboutButton = CreateWindowExW(
      0, WC_BUTTON, kAboutButtonLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowLeft, row6_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ABOUT_BUTTON)), g_hInstance, nullptr);
  if (hAboutButton == nullptr) {
    return false;
  }

  hExitButton = CreateWindowExW(
      0, WC_BUTTON, kExitButtonLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowCol2, row6_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_EXIT_BUTTON)), g_hInstance, nullptr);
  if (hExitButton == nullptr) {
    return false;
  }

  // Apply the standard GUI font to every control. Without this they
  // render in the ancient SYSTEM_FONT (raster "System" face) on Win2k.
  // DEFAULT_GUI_FONT is a stock object - no cleanup needed.
  HFONT hGuiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  const HWND kFontTargets[] = {s_hGroupBox,        hDigitsLabel,       hDigitsCombo,
                               hThreadsLabel,      hThreadsCombo,      hStartButton,
                               hStopButton,        hOpenOutButton,     hConsoleButton,
                               hClearResultButton, hClearOutputButton, hAboutButton,
                               hExitButton};
  for (HWND hCtrl : kFontTargets) {
    SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(hGuiFont), MAKELPARAM(FALSE, 0));
  }

  for (const wchar_t* opt : kDigitOptions) {
    SendMessageW(hDigitsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(opt));
  }
  for (const wchar_t* opt : kThreadsOptions) {
    SendMessageW(hThreadsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(opt));
  }
  // Default selections: 1M digits (index 5), 2 threads (index 1).
  SendMessageW(hDigitsCombo,  CB_SETCURSEL, 5, 0);
  SendMessageW(hThreadsCombo, CB_SETCURSEL, 1, 0);

  return true;
}

int GetSplitterY() {
  return s_splitter_y;
}

int GetClampedSplitterY(int cy) {
  if (s_splitter_y < 0) {
    return static_cast<int>(static_cast<float>(cy) * kTopPaneFraction);
  }
  const int min_y = kMinTopHeight;
  const int max_y = cy - kMinBottomHeight - kSplitterHeight;
  if (max_y < min_y) return min_y;
  if (s_splitter_y < min_y) return min_y;
  if (s_splitter_y > max_y) return max_y;
  return s_splitter_y;
}

void SetSplitterY(int y) {
  s_splitter_y = y;
}

void LayoutChildren(HWND parent) {
  if (parent == nullptr || hSplitter == nullptr || hOutputEdit == nullptr ||
      s_hGroupBox == nullptr) {
    return;
  }
  RECT rc;
  GetClientRect(parent, &rc);
  const int cx = rc.right - rc.left;
  const int cy = rc.bottom - rc.top;
  if (cx <= 0 || cy <= 0) {
    return;
  }
  // kTopPaneFraction is 0.5 truncated to int at the end since
  // the splitter Y is a pixel value.
  if (s_splitter_y < 0) {
    s_splitter_y = static_cast<int>(static_cast<float>(cy) * kTopPaneFraction);
  }
  // Clamp into a local for layout; don't write back to s_splitter_y.
  // Otherwise shrinking the window past the user's splitter Y would
  // overwrite their preference, so growing the window again wouldn't
  // restore the original position.
  const int min_y = kMinTopHeight;
  const int max_y = cy - kMinBottomHeight - kSplitterHeight;
  int splitter_top = s_splitter_y;
  if (max_y < min_y) {
    // Window is too small for both panes + splitter at minimum
    // sizes; collapse the top pane and accept a too-short bottom.
    splitter_top = min_y;
  } else if (splitter_top < min_y) {
    splitter_top = min_y;
  } else if (splitter_top > max_y) {
    splitter_top = max_y;
  }

  const int bottom_top = splitter_top + kSplitterHeight;
  const int bottom_h   = (cy > bottom_top) ? (cy - bottom_top) : 0;

  // Group box: kGroupMargin (7px) on left/right/bottom; frame line lands at
  // kGroupOuterTop (10px) from client top, so the HWND sits 3px below the
  // client top (kGroupOuterTop - kGroupMargin). Right edge is kGroupMargin
  // past the widest controls so the right side stays free for future widgets.
  constexpr int kControlsRight = kPadLeft + kLabelWidth + kHGap + kComboWidth;
  constexpr int kGroupHwndTop  = kGroupOuterTop - kGroupMargin; // = 3
  const int group_w = kControlsRight + kGroupMargin;
  const int group_h = splitter_top - kGroupMargin - kGroupHwndTop;

  // Batch all moves atomically via DeferWindowPos so the children
  // reposition in a single pass - no intermediate state is painted,
  // eliminating the flicker that sequential MoveWindow calls produce
  // during live resize.
  HDWP hdwp = BeginDeferWindowPos(3);
  if (hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, s_hGroupBox, nullptr,
                          kGroupMargin, kGroupHwndTop, group_w, group_h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, hSplitter, nullptr,
                          0, splitter_top, cx, kSplitterHeight,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, hOutputEdit, nullptr,
                          0, bottom_top, cx, bottom_h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr) {
    EndDeferWindowPos(hdwp);
  }
  // Splitter drags only call LayoutChildren — no WM_SIZE fires, so the
  // system won't trigger a parent repaint automatically. Invalidate here
  // so the parent erases the old groupbox border before the groupbox
  // repaints at its new size. Without WS_CLIPCHILDREN the parent can
  // now reach the groupbox area, making this effective.
  InvalidateRect(parent, nullptr, TRUE);
}

// Pre-Vista Edit controls only treat \r\n as a line break - bare \n
// renders as a "tofu" control glyph on Win2k/XP (Win7+ and Wine are
// forgiving). Normalize any mix of \r, \n, \r\n in the caller's
// string to canonical \r\n before we append.
static std::wstring NormalizeNewlines(const std::wstring& msg) {
  std::wstring out;
  out.reserve(msg.size() + 8);
  for (size_t i = 0; i < msg.size(); ++i) {
    const wchar_t c = msg[i];
    if (c == L'\r') {
      out += L"\r\n";
      // Eat a paired \n so \r\n doesn't become \r\n\r\n.
      if (i + 1 < msg.size() && msg[i + 1] == L'\n') {
        ++i;
      }
    } else if (c == L'\n') {
      out += L"\r\n";
    } else {
      out += c;
    }
  }
  return out;
}

void SendOutputMessage(const std::wstring& msg) {
  if (hOutputEdit == nullptr) {
    return;
  }
  // Collapse selection to a caret at the very end, then splice the new
  // line in via EM_REPLACESEL. wParam=FALSE keeps the append out of
  // the undo buffer (the output pane is a log, not user input).
  const int len = GetWindowTextLengthW(hOutputEdit);
  SendMessageW(hOutputEdit, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
  const std::wstring line = NormalizeNewlines(msg) + L"\r\n";
  SendMessageW(hOutputEdit, EM_REPLACESEL, FALSE,
               reinterpret_cast<LPARAM>(line.c_str()));
  // EM_REPLACESEL on an ES_AUTOVSCROLL edit usually scrolls the new
  // text into view, but EM_SCROLLCARET makes the guarantee explicit
  // when the user has scrolled up to read earlier output.
  SendMessageW(hOutputEdit, EM_SCROLLCARET, 0, 0);
}

void PrintOutputSeparator() {
  static constexpr int kSeparatorWidth = 90;
  SendOutputMessage(std::wstring(kSeparatorWidth, L'*'));
}

void EmitLine(const std::wstring& msg, bool is_error) {
  // LOG sink is silent in release; SendOutputMessage always shows
  // (when the edit exists). Both are thread-safe: LOG has its own
  // mutex, SendMessageW marshals across thread boundaries.
  //
  // Two literal LOG() calls instead of one taking a runtime level -
  // LOG(level) token-pastes `LOG_##level`, so it only accepts literal
  // identifiers (INFO / ERROR / ...), not a variable.
  if (is_error) {
    LOG(ERROR) << msg;
  } else {
    LOG(INFO) << msg;
  }
  SendOutputMessage(msg);
}

void ClearOutput() {
  if (hOutputEdit == nullptr) {
    return;
  }
  // WM_SETTEXT with an empty string clears the entire buffer in one
  // shot - cheaper than EM_SETSEL+EM_REPLACESEL for a wipe. Reprint
  // the welcome banner so the edit lands in the same state as it
  // was at startup (WM_CREATE).
  SetWindowTextW(hOutputEdit, L"");
  SendOutputMessage(GetWelcomeMessage());
}

// "10", "1K", "10M", "1,000" -> integer. Commas are skipped; a
// trailing K / M suffix multiplies by 1e3 / 1e6 respectively.
// "Custom" -> -1 (caller should treat as "user input not yet wired").
static int ParseCountSuffixed(const std::wstring& s) {
  if (s == L"Custom") {
    return -1;
  }
  long val = 0;
  wchar_t suf = 0;
  for (wchar_t c : s) {
    if (c >= L'0' && c <= L'9') {
      val = val * 10 + (c - L'0');
    } else if (c == L',') {
      // skip thousands separators
    } else {
      suf = c;
      break;
    }
  }
  if (suf == L'K' || suf == L'k') {
    val *= 1000;
  } else if (suf == L'M' || suf == L'm') {
    val *= 1000000;
  }
  return static_cast<int>(val);
}

static int GetSelectedFromCombo(HWND hCombo) {
  if (hCombo == nullptr) {
    return -1;
  }
  const LRESULT idx = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
  if (idx == CB_ERR) {
    return -1;
  }
  wchar_t buf[64] = {0};
  if (SendMessageW(hCombo, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buf)) == CB_ERR) {
    return -1;
  }
  return ParseCountSuffixed(buf);
}

int GetSelectedDigits() {
  return GetSelectedFromCombo(hDigitsCombo);
}

int GetSelectedThreads() {
  return GetSelectedFromCombo(hThreadsCombo);
}
