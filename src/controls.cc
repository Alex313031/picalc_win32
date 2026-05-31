// Controls creation/management and layout/logic

#include "controls.h"

#include "constants.h"
#include "cpu.h"
#include "main.h"
#include "resource.h"
#include "strings.h"
#include "sysmon.h"
#include "utils.h"

// =========================================================================
// Splitter window class
// =========================================================================

static const wchar_t* kSplitterClassName = L"PicalcSplitter";

// -1 = uninitialised, gets centred on the first LayoutChildren call.
static int s_splitter_y = -1;
static bool s_dragging  = false;

// Tracks the last-confirmed selection in each combo so HandleComboBoxes can
// revert when the user cancels the custom-input dialog. Digits defaults to
// index 6 ("1M"); threads is set by CreateChildControls based on the actual
// processor count, possibly injecting a custom value.
static int s_prev_digits_sel          = 6;
static int s_prev_threads_sel         = 0;
static bool s_digits_custom_injected  = false;
static bool s_threads_custom_injected = false;

// Params passed via DialogBoxParamW lParam for the custom-input dialog.
struct CustomInputParams {
  const wchar_t* title;
  const wchar_t* prompt;
  UINT min_val;
  UINT max_val;
  UINT edit_limit; // max characters the edit control accepts
  UINT result;     // written on IDOK
  HWND to_focus;   // HWND of control to focus when dialog done.
};

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
      POINT mouse_pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      HWND parent    = GetParent(hWnd);
      if (parent == nullptr) {
        return 0;
      }
      ClientToScreen(hWnd, &mouse_pt);
      ScreenToClient(parent, &mouse_pt);
      s_splitter_y = mouse_pt.y;
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
      RECT splitter_rc;
      GetClientRect(hWnd, &splitter_rc);
      // Classic button-face fill with a 1px raised top + bottom edge so
      // the bar is visible against the surrounding grey without looking
      // like a heavyweight 3D bevel.
      FillRectWithColor(hdc, splitter_rc, g_bkg_color);
      DrawEdge(hdc, &splitter_rc, EDGE_RAISED, BF_TOP | BF_BOTTOM);
      // Grab handle: 1px tall, 6px wide, centered in the bar.
      const int splitter_w = splitter_rc.right - splitter_rc.left;
      const int splitter_h = splitter_rc.bottom - splitter_rc.top;
      const int hndl_x     = splitter_rc.left + (splitter_w - kSplitterHandleWidth) / 2;
      const int hndl_y     = splitter_rc.top + (splitter_h - kSplitterHandleHeight) / 2;
      const RECT handle    = {hndl_x, hndl_y, hndl_x + kSplitterHandleWidth,
                              hndl_y + kSplitterHandleHeight};
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
  WNDCLASSEXW wnd_class   = {};
  wnd_class.cbSize        = sizeof(wnd_class);
  wnd_class.style         = CS_HREDRAW | CS_VREDRAW;
  wnd_class.lpfnWndProc   = SplitterProc;
  wnd_class.hInstance     = hInstance;
  wnd_class.hCursor       = LoadCursorW(nullptr, IDC_SIZENS);
  wnd_class.hbrBackground = nullptr; // We paint in WM_PAINT
  wnd_class.lpszClassName = kSplitterClassName;
  if (RegisterClassExW(&wnd_class) != 0) {
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
  hOutputEdit = CreateWindowExW(WS_EX_WINDOWEDGE, WC_EDIT, L"",
                                dwOutput | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OUTPUT_EDIT)),
                                g_hInstance, nullptr);
  if (hOutputEdit == nullptr) {
    return false;
  }

  hSplitter = CreateWindowExW(0, kSplitterClassName, L"", dwCHILD, 0, 0, 0, 0, parent,
                              reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SPLITTER)),
                              g_hInstance, nullptr);
  if (hSplitter == nullptr) {
    return false;
  }

  // Group box: created before the inner controls so it sits behind them
  // in Z-order. Position is 0,0,0,0 here; LayoutChildren sizes it to
  // fill the top pane with a 7px margin on all sides.
  s_hGroupBox = CreateWindowExW(
      0, WC_BUTTON, kCntrlsGroupLabel, dwCHILD | BS_GROUPBOX | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CONTROLS_GROUP)), g_hInstance, nullptr);
  if (s_hGroupBox == nullptr) {
    return false;
  }

  // Sysmon groupbox: sits to the right of the controls groupbox, occupying
  // the remaining top-pane width. Position set by LayoutChildren.
  if (!CreateSysmonControls(parent)) {
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

  // SS_NOTIFY makes the static return HTCLIENT from WM_NCHITTEST instead of
  // the default HTTRANSPARENT, so WM_MOUSEMOVE reaches the control - required
  // for TTF_SUBCLASS-based hover tooltips to fire when hovering the label.
  hDigitsLabel = CreateWindowExW(
      0, WC_STATIC, kNumDigitsLabel, dwCHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, kRowLeft,
      row1_y, kLabelWidth, kControlHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DIGITS_LABEL)), g_hInstance, nullptr);
  if (hDigitsLabel == nullptr) {
    return false;
  }

  hDigitsCombo = CreateWindowExW(
      0, WC_COMBOBOX, L"", dwCHILD | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, kRowCol2, row1_y,
      kComboWidth, kComboDropHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DIGITS_COMBO)), g_hInstance, nullptr);
  if (hDigitsCombo == nullptr) {
    return false;
  }

  hThreadsLabel = CreateWindowExW(
      0, WC_STATIC, kNumThreadsLabel, dwCHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, kRowLeft,
      row2_y, kLabelWidth, kControlHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_THREADS_LABEL)), g_hInstance, nullptr);
  if (hThreadsLabel == nullptr) {
    return false;
  }

  hThreadsCombo = CreateWindowExW(
      0, WC_COMBOBOX, L"", dwCHILD | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, kRowCol2, row2_y,
      kComboWidth, kComboDropHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_THREADS_COMBO)), g_hInstance, nullptr);
  if (hThreadsCombo == nullptr) {
    return false;
  }

  // Row 3: Calculate + Stop buttons. BS_CENTER | BS_VCENTER spell out
  // the centering even though BS_PUSHBUTTON already centres by default.
  hStartButton = CreateWindowExW(
      0, WC_BUTTON, kStartButtonLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER | BS_DEFPUSHBUTTON, kRowLeft,
      row3_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_START_BUTTON)), g_hInstance, nullptr);
  if (hStartButton == nullptr) {
    return false;
  }

  hStopButton = CreateWindowExW(
      0, WC_BUTTON, kStopButtonLabel, dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowCol2, row3_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_STOP_BUTTON)), g_hInstance, nullptr);
  if (hStopButton == nullptr) {
    return false;
  }

  // Row 4: Open Out File + Clear Results buttons.
  hOpenOutButton = CreateWindowExW(
      0, WC_BUTTON, kOpenResultLabel, dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowLeft, row4_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_RESULT)), g_hInstance, nullptr);
  if (hOpenOutButton == nullptr) {
    return false;
  }

  hClearResultButton = CreateWindowExW(
      0, WC_BUTTON, kClearResultLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER, kRowCol2, row4_y, kButtonWidth,
      kButtonHeight, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CLEARRESULT_BUTTON)),
      g_hInstance, nullptr);
  if (hClearResultButton == nullptr) {
    return false;
  }

  // Row 5: Clear Output + Console toggle buttons.
  hClearOutputButton = CreateWindowExW(
      0, WC_BUTTON, kClearOutputLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER, kRowLeft, row5_y, kButtonWidth,
      kButtonHeight, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CLEAROUTPUT_BUTTON)),
      g_hInstance, nullptr);
  if (hClearOutputButton == nullptr) {
    return false;
  }

  hConsoleButton = CreateWindowExW(
      0, WC_BUTTON, kShowConsoleLabel,
      dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER, kRowCol2, row5_y, kButtonWidth,
      kButtonHeight, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CONSOLE_BUTTON)),
      g_hInstance, nullptr);
  if (hConsoleButton == nullptr) {
    return false;
  }

  // Row 6: About + Exit buttons.
  hAboutButton = CreateWindowExW(0, WC_BUTTON, kAboutButtonLabel,
                                 dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
                                 kRowLeft, row6_y, kButtonWidth, kButtonHeight, parent,
                                 reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_ABOUT_BUTTON)),
                                 g_hInstance, nullptr);
  if (hAboutButton == nullptr) {
    return false;
  }

  hExitButton = CreateWindowExW(
      0, WC_BUTTON, kExitButtonLabel, dwCHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
      kRowCol2, row6_y, kButtonWidth, kButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_EXIT_BUTTON)), g_hInstance, nullptr);
  if (hExitButton == nullptr) {
    return false;
  }

  // Apply Tahoma explicitly so the face is consistent across all systems.
  // Fall back to DEFAULT_GUI_FONT if Tahoma is unavailable.
  HFONT hGuiFont = GetFont(0);
  if (hGuiFont == nullptr) {
    hGuiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
  const HWND kFontTargets[] = {
      s_hGroupBox,        hDigitsLabel, hDigitsCombo,   hThreadsLabel,  hThreadsCombo,
      hStartButton,       hStopButton,  hOpenOutButton, hConsoleButton, hClearResultButton,
      hClearOutputButton, hAboutButton, hExitButton};
  for (HWND hCtrl : kFontTargets) {
    SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(hGuiFont), MAKELPARAM(FALSE, 0));
  }
  // Output pane uses old style font
  static const HFONT hOutputFont = reinterpret_cast<HFONT>(GetStockObject(SYSTEM_FONT));
  SendMessageW(hOutputEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hOutputFont),
               MAKELPARAM(FALSE, 0));

  // Hover tooltips - paired label+combo controls share one string so a hover
  // anywhere on the row surfaces the same hint. Tooltips on a CBS_DROPDOWNLIST
  // combo attach to the combo's HWND.
  struct TooltipBinding {
    HWND hCtrl;
    const wchar_t* text;
  };
  const TooltipBinding kTooltipBindings[] = {
      {hDigitsLabel, kTooltipDigits},          {hDigitsCombo, kTooltipDigits},
      {hThreadsLabel, kTooltipThreads},        {hThreadsCombo, kTooltipThreads},
      {hStartButton, kTooltipStart},           {hStopButton, kTooltipStop},
      {hOpenOutButton, kTooltipOpenPiTxt},     {hClearResultButton, kTooltipClrResult},
      {hClearOutputButton, kTooltipClrOutput}, {hConsoleButton, kTooltipConsole},
      {hAboutButton, kTooltipAbout},           {hExitButton, kTooltipExit},
      {hOutputEdit, kTooltipOutput},
  };
  for (const auto& tb : kTooltipBindings) {
    AddTooltip(parent, tb.hCtrl, g_hInstance, tb.text);
  }

  for (const wchar_t* opt : kDigitOptions) {
    SendMessageW(hDigitsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(opt));
  }
  // Effective thread ceiling - GetEffectiveThreadMax bakes in the policy:
  // hard cap from kMaxNumThreads, system cap from logical CPU count, plus
  // the single-CPU special case that allows 2 threads (see cpu.h).
  const DWORD cpu_count = GetEffectiveThreadMax();

  // Only add thread-count options that fit under cpu_count; "Custom" is always
  // added (the custom-input dialog clamps to the same ceiling).
  for (const wchar_t* opt : kThreadsOptions) {
    const bool is_custom = (wcscmp(opt, L"Custom") == 0);
    if (is_custom || wcstoul(opt, nullptr, 10) <= cpu_count) {
      SendMessageW(hThreadsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(opt));
    }
  }
  // Default digit selection: 1M digits (index 6).
  SendMessageW(hDigitsCombo, CB_SETCURSEL, 6, 0);

  // Default thread selection: exact logical CPU count. CB_FINDSTRINGEXACT
  // matches against what was actually added to the combobox (filter-aware),
  // so we don't have to track combo-vs-array indices separately.
  wchar_t cpu_str[16];
  swprintf(cpu_str, ARRAYSIZE(cpu_str), L"%u", cpu_count);
  const int found = static_cast<int>(
      SendMessageW(hThreadsCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                   reinterpret_cast<LPARAM>(cpu_str)));
  if (found != CB_ERR) {
    SendMessageW(hThreadsCombo, CB_SETCURSEL, found, 0);
    s_prev_threads_sel        = found;
    s_threads_custom_injected = false;
  } else {
    // CPU count isn't a standard option - inject it before "Custom".
    const int insert_at = static_cast<int>(SendMessageW(hThreadsCombo, CB_GETCOUNT, 0, 0)) - 1;
    SendMessageW(hThreadsCombo, CB_INSERTSTRING, insert_at, reinterpret_cast<LPARAM>(cpu_str));
    SendMessageW(hThreadsCombo, CB_SETCURSEL, insert_at, 0);
    s_prev_threads_sel        = insert_at;
    s_threads_custom_injected = true;
  }

  return true;
}

static INT_PTR CALLBACK CustomInputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_INITDIALOG: {
      auto* params = reinterpret_cast<CustomInputParams*>(lParam);
      SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(params));
      SetWindowTextW(hDlg, params->title);
      SetDlgItemTextW(hDlg, IDC_CUSTOM_PROMPT, params->prompt);
      SendDlgItemMessageW(hDlg, IDC_CUSTOM_EDIT, EM_SETLIMITTEXT, params->edit_limit, 0);
      // Position the dialog just below the mouse cursor, clamped to screen.
      POINT cursor_pt;
      GetCursorPos(&cursor_pt);
      RECT dlg_rect;
      GetWindowRect(hDlg, &dlg_rect);
      const int dlg_w    = dlg_rect.right - dlg_rect.left;
      const int dlg_h    = dlg_rect.bottom - dlg_rect.top;
      const int screen_w = GetSystemMetrics(SM_CXSCREEN);
      const int screen_h = GetSystemMetrics(SM_CYSCREEN);
      int dlg_x          = cursor_pt.x;
      int dlg_y          = cursor_pt.y + 4; // slight offset so cursor doesn't overlap the title bar
      if (dlg_x + dlg_w > screen_w) {
        dlg_x = screen_w - dlg_w;
      }
      if (dlg_y + dlg_h > screen_h) {
        dlg_y = screen_h - dlg_h;
      }
      if (dlg_x < 0) {
        dlg_x = 0;
      }
      if (dlg_y < 0) {
        dlg_y = 0;
      }
      SetWindowPos(hDlg, nullptr, dlg_x, dlg_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
      SetFocus(GetDlgItem(hDlg, IDC_CUSTOM_EDIT));
      return FALSE; // FALSE because we set focus manually
    }
    case WM_COMMAND: {
      const int ctrl = LOWORD(wParam);
      if (ctrl == IDOK) {
        auto* params    = reinterpret_cast<CustomInputParams*>(GetWindowLongPtrW(hDlg, DWLP_USER));
        wchar_t buf[32] = {};
        GetDlgItemTextW(hDlg, IDC_CUSTOM_EDIT, buf, ARRAYSIZE(buf));
        wchar_t* end            = nullptr;
        const unsigned long val = wcstoul(buf, &end, 10);
        if (end == buf || *end != L'\0' || val < params->min_val || val > params->max_val) {
          wchar_t errmsg[128];
          swprintf(errmsg, ARRAYSIZE(errmsg), L"Enter a whole number between %u and %u.",
                   params->min_val, params->max_val);
          WarnBox(hDlg, params->title, errmsg);
          SetFocus(GetDlgItem(hDlg, IDC_CUSTOM_EDIT));
          SendDlgItemMessageW(hDlg, IDC_CUSTOM_EDIT, EM_SETSEL, 0, -1);
          return TRUE;
        }
        params->result = static_cast<UINT>(val);
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      if (ctrl == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      return FALSE;
    }
    case WM_CLOSE:
      EndDialog(hDlg, IDCANCEL);
      return TRUE;
    default:
      return FALSE;
  }
}

void HandleComboBoxes(HWND hWnd, WPARAM wParam, LPARAM lParam) {
  if (HIWORD(wParam) != CBN_SELCHANGE) {
    return;
  }
  const int command    = LOWORD(wParam);
  HWND hCombo          = reinterpret_cast<HWND>(lParam);
  const bool is_digits = (command == IDC_DIGITS_COMBO);
  const int count      = static_cast<int>(SendMessageW(hCombo, CB_GETCOUNT, 0, 0));
  const int sel        = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
  if (sel == CB_ERR) {
    return;
  }
  if (sel != count - 1) {
    if (is_digits) {
      s_prev_digits_sel = sel;
    } else {
      s_prev_threads_sel = sel;
    }
    return;
  }
  // "Custom" was selected - show the input dialog.
  CustomInputParams params = {};
  if (is_digits) {
    params.title      = kDigitsDlgTitle;
    params.prompt     = kDigitsDlgPrompt;
    params.min_val    = kMinNumDigits;
    params.max_val    = kMaxNumDigits;
    params.edit_limit = 10; // 1,000,000,000 = 10 digits
    params.to_focus   = hDigitsCombo;
  } else {
    params.title      = kThreadsDlgTitle;
    params.prompt     = kThreadsDlgPrompt;
    params.min_val    = kMinNumThreads;
    // Mirror the combobox filter: respect the same effective ceiling.
    params.max_val    = GetEffectiveThreadMax();
    params.edit_limit = 3; // 256 = 3 digits
    params.to_focus   = hThreadsCombo;
  }
  const INT_PTR res = DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_CUSTOM_INPUT), hWnd,
                                      CustomInputDlgProc, reinterpret_cast<LPARAM>(&params));
  // Restore focus to the originating combo box now that the dialog is fully
  // destroyed - calling SetFocus from inside the dialog proc after EndDialog
  // would race with the system's own focus restoration during teardown.
  SetFocus(params.to_focus);
  if (res == IDOK) {
    // Format the validated value and inject it just before "Custom".
    wchar_t val_str[32];
    swprintf(val_str, ARRAYSIZE(val_str), L"%u", params.result);
    // Remove any previously injected custom item (always at count-2
    // when injected == true, because "Custom" stays last).
    bool& injected = is_digits ? s_digits_custom_injected : s_threads_custom_injected;
    if (injected) {
      const int cur_count = static_cast<int>(SendMessageW(hCombo, CB_GETCOUNT, 0, 0));
      SendMessageW(hCombo, CB_DELETESTRING, cur_count - 2, 0);
    }
    const int insert_at = static_cast<int>(SendMessageW(hCombo, CB_GETCOUNT, 0, 0)) - 1;
    SendMessageW(hCombo, CB_INSERTSTRING, insert_at, reinterpret_cast<LPARAM>(val_str));
    SendMessageW(hCombo, CB_SETCURSEL, insert_at, 0);
    injected = true;
    if (is_digits) {
      s_prev_digits_sel = insert_at;
    } else {
      s_prev_threads_sel = insert_at;
    }
  } else {
    // Revert to the last known good selection.
    const int prev = is_digits ? s_prev_digits_sel : s_prev_threads_sel;
    if (prev >= 0) {
      SendMessageW(hCombo, CB_SETCURSEL, prev, 0);
    }
  }
}

int GetSplitterY() {
  return s_splitter_y;
}

int GetClampedSplitterY(int client_h) {
  if (s_splitter_y < 0) {
    return static_cast<int>(static_cast<float>(client_h) * kTopPaneFraction);
  }
  const int min_y = kMinTopHeight;
  const int max_y = client_h - kMinBottomHeight - kSplitterHeight;
  if (max_y < min_y) {
    return min_y;
  }
  if (s_splitter_y < min_y) {
    return min_y;
  }
  if (s_splitter_y > max_y) {
    return max_y;
  }
  return s_splitter_y;
}

void SetSplitterY(int new_y) {
  s_splitter_y = new_y;
}

void LayoutChildren(HWND parent) {
  if (parent == nullptr || hSplitter == nullptr || hOutputEdit == nullptr ||
      s_hGroupBox == nullptr) {
    return;
  }
  RECT client_rc;
  GetClientRect(parent, &client_rc);
  const int client_w = client_rc.right - client_rc.left;
  const int client_h = client_rc.bottom - client_rc.top;
  if (client_w <= 0 || client_h <= 0) {
    return;
  }
  // kTopPaneFraction is 0.5 truncated to int at the end since
  // the splitter Y is a pixel value.
  if (s_splitter_y < 0) {
    s_splitter_y = static_cast<int>(static_cast<float>(client_h) * kTopPaneFraction);
  }
  // Clamp into a local for layout; don't write back to s_splitter_y.
  // Otherwise shrinking the window past the user's splitter Y would
  // overwrite their preference, so growing the window again wouldn't
  // restore the original position.
  const int min_y  = kMinTopHeight;
  const int max_y  = client_h - kMinBottomHeight - kSplitterHeight;
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
  const int bottom_h   = (client_h > bottom_top) ? (client_h - bottom_top) : 0;

  // Controls groupbox: kGroupMargin on left/right/bottom; frame line at
  // kGroupOuterTop, so the HWND top is kGroupHwndTop = kGroupOuterTop - kGroupMargin.
  const int group_h = splitter_top - kGroupMargin - kGroupHwndTop;
  // Sysmon groupbox: same top/height, starts kGroupMargin past the controls
  // group's right edge and extends to kGroupMargin from the client right edge.
  const int sysmon_x = kGroupMargin + kControlsGroupWidth + kGroupMargin;
  const int sysmon_w = client_w - sysmon_x - kGroupMargin;

  // Batch all moves atomically via DeferWindowPos so the children
  // reposition in a single pass - no intermediate state is painted,
  // eliminating the flicker that sequential MoveWindow calls produce
  // during live resize.
  // Graph area: 7px (kGroupMargin) side/bottom padding, kGroupInnerPad from the
  // top frame line (matches the digits combo top). Fills the top half of the
  // inner content area; the bottom half is reserved for metrics labels.
  const int graph_x = sysmon_x + kGroupMargin;
  const int graph_y = kGroupOuterTop + kGroupInnerPad;
  const int graph_w = sysmon_w - 2 * kGroupMargin;
  const int inner_h = group_h - 2 * kGroupMargin - kGroupInnerPad;
  const int graph_h = inner_h / 2;

  const HWND hSysmonGroup = GetSysmonGroupHwnd();
  const HWND hGraph       = GetGraphHwnd();
  // 5 structural + 2 sub-groupboxes + 16 metric label/value statics = 23; use 25 as hint.
  HDWP hdwp = BeginDeferWindowPos(25);
  if (hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, s_hGroupBox, nullptr, kGroupMargin, kGroupHwndTop,
                          kControlsGroupWidth, group_h, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr && hSysmonGroup != nullptr) {
    hdwp = DeferWindowPos(hdwp, hSysmonGroup, nullptr, sysmon_x, kGroupHwndTop, sysmon_w, group_h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr && hGraph != nullptr) {
    hdwp = DeferWindowPos(hdwp, hGraph, nullptr, graph_x, graph_y, graph_w, graph_h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  // Sub-groupbox HWND top sits kGroupMargin above the frame line (same relationship
  // as kGroupHwndTop = kGroupOuterTop - kGroupMargin for the outer groupboxes).
  // Height extends from that point to the inner bottom of the sysmon groupbox.
  const int metrics_y   = graph_y + graph_h + kVGap;
  const int sub_grp_top = metrics_y - kGroupMargin;
  const int sub_grp_h   = kGroupHwndTop + group_h - metrics_y;
  if (hdwp != nullptr) {
    hdwp = LayoutSysmonMetrics(hdwp, graph_x, sub_grp_top, graph_w, sub_grp_h);
  }
  if (hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, hSplitter, nullptr, 0, splitter_top, client_w, kSplitterHeight,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, hOutputEdit, nullptr, 0, bottom_top, client_w, bottom_h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hdwp != nullptr) {
    EndDeferWindowPos(hdwp);
  }
  // Splitter drags only call LayoutChildren: no WM_SIZE fires, so the
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
  for (size_t char_idx = 0; char_idx < msg.size(); ++char_idx) {
    const wchar_t ch = msg[char_idx];
    if (ch == L'\r') {
      out += L"\r\n";
      // Eat a paired \n so \r\n doesn't become \r\n\r\n.
      if (char_idx + 1 < msg.size() && msg[char_idx + 1] == L'\n') {
        ++char_idx;
      }
    } else if (ch == L'\n') {
      out += L"\r\n";
    } else {
      out += ch;
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
  SendMessageW(hOutputEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
  // EM_REPLACESEL on an ES_AUTOVSCROLL edit usually scrolls the new
  // text into view, but EM_SCROLLCARET makes the guarantee explicit
  // when the user has scrolled up to read earlier output.
  SendMessageW(hOutputEdit, EM_SCROLLCARET, 0, 0);
}

void PrintOutputSeparator() {
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
  LOG(INFO) << L"Cleared status pane";
}

// "10", "1K", "10M", "1,000" -> integer. Commas are skipped; a
// trailing K / M suffix multiplies by 1e3 / 1e6 respectively.
// "Custom" -> -1 (caller should treat as "user input not yet wired").
static int ParseCountSuffixed(const std::wstring& s) {
  if (s == L"Custom") {
    return -1;
  }
  long val    = 0;
  wchar_t suf = 0;
  for (wchar_t ch : s) {
    if (ch >= L'0' && ch <= L'9') {
      val = val * 10 + (ch - L'0');
    } else if (ch == L',') {
      // skip thousands separators
    } else {
      suf = ch;
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
