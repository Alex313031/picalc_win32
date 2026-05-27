/*------------------------------------------
   PiCalc Win32
   Copyright (c) 2026 Alex313031
  ------------------------------------------*/

#include "main.h"

#include "controls.h"
#include "globals.h"
#include "resource.h"
#include "results.h"
#include "strings.h"

// =========================================================================
// Statics
// =========================================================================

// Previous non-"Custom" selection for each combo, used to revert when the
// user cancels the custom-input dialog. Digits defaults to index 5 ("1M");
// threads is seeded from GetInitialThreadsSel() after CreateChildControls.
static int s_prev_digits_sel          = 5;
static int s_prev_threads_sel         = 0;
static bool s_digits_custom_injected  = false;
static bool s_threads_custom_injected = false;

// Latched at startup from the .rc's initial GRAYED flag on IDM_CONSOLE.
// When true, UpdateConsoleToggleMenu leaves the item alone (no enable,
// no label swap) and the WM_COMMAND handler refuses to toggle - the
// .rc gets the final say on whether the feature is available at all.
static bool s_console_menu_user_disabled = false;

// Our own view of whether the user has toggled the console hidden.
// We can't use IsWindowVisible(GetConsoleWindow()) for this because on
// Win10/11 with Windows Terminal as the default conhost, GetConsoleWindow
// returns a permanently-hidden pseudo-window owned by conhost.exe - the
// visible Terminal UI is a separate process. So IsWindowVisible says
// "hidden" even though the user can see the console fine. Track intent
// instead: wWinMain hides the console immediately at startup unless
// --debug / --version / --help asked for it visible, and the flag flips
// on every successful Show/HideConsole call after that.
static bool s_console_hidden = false;

// Whether window was minimized or not
static bool s_was_minimized = false;

// =========================================================================
// Globals
// =========================================================================

HWND mainHwnd           = nullptr;
HWND hOutputEdit        = nullptr;
HWND hSplitter          = nullptr;
HWND hDigitsLabel       = nullptr;
HWND hDigitsCombo       = nullptr;
HWND hThreadsLabel      = nullptr;
HWND hThreadsCombo      = nullptr;
HWND hStartButton       = nullptr;
HWND hStopButton        = nullptr;
HWND hOpenOutButton     = nullptr;
HWND hClearResultButton = nullptr;
HWND hClearOutputButton = nullptr;
HWND hAboutButton       = nullptr;
HWND hConsoleButton     = nullptr;
HWND hExitButton        = nullptr;

HINSTANCE g_hInstance = nullptr;

int cxClient = 0;
int cyClient = 0;

// Whether Pi Calculation is currently running
std::atomic<bool> g_running(false);

// Mirrors the Settings -> Sound? menu check state. Polled by picalc.cc
// before playing the completion chime. Initial value matches the .rc's
// CHECKED flag (true); ApplyMenuDefaults latches the .rc state into it
// at startup and the IDM_SOUND handler keeps it in sync on toggle.
bool g_sound_on       = true;
bool g_colored_output = false;

bool g_debug_mode = is_debug;
// CLI flags. Set by ParseCommandLine before InitLogging runs so the log
// sink picks up --debug, and so --version / --help can short-circuit
// wWinMain before the window is created.
bool g_show_version = false;
bool g_show_help    = false;

// Store handles to main icon since commonly used
HICON kMainIcon  = nullptr;
HICON kSmallIcon = nullptr;

// Whether we have commctl32 5.82 (XP/I.E 6.0)
bool can_use_582_controls = false;

COLORREF g_bkg_color = GetSysColor(COLOR_3DFACE);

// =========================================================================
// Forward declarations
// =========================================================================

static bool ParseCommandLine(int argc, LPWSTR argv[]);
static int ShowVersionAndExit();
static int ShowHelpAndExit();
static void UpdateConsoleToggleMenu(HWND hWnd);
static void ApplyMenuDefaults(HWND hWnd);

// Params passed via DialogBoxParamW lParam for the custom-input dialog.
struct CustomInputParams {
  const wchar_t* title;
  const wchar_t* prompt;
  UINT min_val;
  UINT max_val;
  UINT edit_limit; // max characters the edit control accepts
  UINT result;     // written on IDOK
};

// =========================================================================
// Functions
// =========================================================================

bool RegisterWndClass(HINSTANCE hInstance, LPCWSTR className) {
  if (kMainIcon == nullptr || kSmallIcon == nullptr) {
    return false;
  }
  WNDCLASSEXW wndclass;
  wndclass.cbSize      = sizeof(WNDCLASSEX);
  wndclass.style       = CS_HREDRAW | CS_VREDRAW;
  wndclass.lpfnWndProc = WindowProc;
  wndclass.cbClsExtra  = 0;
  wndclass.cbWndExtra  = 0;
  wndclass.hInstance   = hInstance;
  wndclass.hIcon       = kMainIcon;
  wndclass.hCursor     = LoadCursorW(nullptr, IDC_ARROW);
  // We handle erase + paint ourselves - WM_ERASEBKGND returns TRUE
  // and WM_PAINT fills with g_bkg_color. Use the system pseudo-brush
  // so the OS updates it automatically on WM_SYSCOLORCHANGE.
  wndclass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
  wndclass.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAIN);
  wndclass.lpszClassName = className;
  wndclass.hIconSm       = kSmallIcon;

  // RegisterClassEx returns an ATOM (typedef unsigned short - really a short
  // pointer left over from Win16 days), 0 on failure. The double cast spells
  // out "this is an ATOM-shaped zero" rather than relying on the implicit
  // promotion from int 0.
  if (RegisterClassExW(&wndclass) == static_cast<ATOM>(static_cast<unsigned short>(0))) {
    return false;
  }
  return true;
}

bool InitWindow(HINSTANCE hInstance, LPCWSTR className, LPCWSTR title, int iCmdShow) {
  static constexpr DWORD exStyle = WS_EX_OVERLAPPEDWINDOW;
  // WS_CLIPCHILDREN keeps the parent's WM_PAINT from drawing through
  // child windows (output edit + splitter), eliminating the grey
  // flash on resize.
  static constexpr DWORD style =
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;

  // Create main window
  mainHwnd = CreateWindowExW(exStyle, className, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_WIDTH, CW_HEIGHT, nullptr, nullptr, hInstance, nullptr);

  if (mainHwnd == nullptr) {
    return false;
  }
  ShowWindow(mainHwnd, iCmdShow);
  if (!UpdateWindow(mainHwnd)) {
    return false;
  }
  return true;
}

// Walks the wchar_t argv produced by CommandLineToArgvW and flips any of
// g_debug_mode / g_show_version / g_show_help that the user passed. Each
// flag accepts the common Win32 / Unix variants (--foo, -foo, -f, /f) so
// we work the same way from PowerShell, cmd.exe, and a Unix shell under
// Wine. Returns false only when argv itself is null (i.e. the system
// failed to split the command line), so wWinMain can give up cleanly
// before we depend on log output.
static bool ParseCommandLine(int argc, LPWSTR argv[]) {
  if (argv == nullptr) {
    return false;
  }
  bool is_debug_mode   = false;
  bool is_version_mode = false;
  bool is_help_mode    = false;
  // argv[0] is the .exe path (CommandLineToArgvW convention); skip it so
  // a path containing characters that happen to match a flag literal
  // can't false-trigger one of the wcscmp checks below.
  for (int arg_idx = 1; arg_idx < argc; ++arg_idx) {
    wchar_t* arg = argv[arg_idx];
    is_debug_mode |= (wcscmp(arg, L"--debug") == 0) || (wcscmp(arg, L"-d") == 0) ||
                     (wcscmp(arg, L"-debug") == 0) || (wcscmp(arg, L"/d") == 0) ||
                     (wcscmp(arg, L"/D") == 0);
    is_version_mode |= (wcscmp(arg, L"--version") == 0) || (wcscmp(arg, L"-v") == 0) ||
                       (wcscmp(arg, L"-ver") == 0) || (wcscmp(arg, L"/v") == 0) ||
                       (wcscmp(arg, L"/V") == 0);
    is_help_mode |= (wcscmp(arg, L"--help") == 0) || (wcscmp(arg, L"-h") == 0) ||
                    (wcscmp(arg, L"-?") == 0) || (wcscmp(arg, L"/h") == 0) ||
                    (wcscmp(arg, L"/H") == 0) || (wcscmp(arg, L"/?") == 0);
  }
  if (is_version_mode && !is_help_mode) {
    g_show_version = true;
  }
  if (is_help_mode) {
    g_show_help = true;
  }
  if (is_debug_mode) {
    g_debug_mode = true;
  }
  return true;
}

// Prints the app name + semver to the attached console (InitLogging has
// already done the AttachConsole/AllocConsole dance) and returns wWinMain's
// exit code. `system("pause")` keeps the window open when launched from
// Explorer so the user can actually read the line.
static int ShowVersionAndExit() {
  std::wcout << GetAppName() << GetVersionString() << std::endl;
  system("pause");
  return 0;
}

// Same as above, but for --help. Lists the recognised flags exactly as
// ParseCommandLine spells them out so the two stay in sync.
static int ShowHelpAndExit() {
  std::wcout << L"\n " << ORIG_FILENAME << L" Usage: \n" << std::flush;
  std::wostringstream wostr;
  wostr << L"   /d | -d | --debug   : Enable debug logging\n"
        << L"   /v | -v | --version : Show version info \n"
        << L"   /? | -h | --help    : Show this Help \n"
        << std::flush;
  static const std::wstring kHelpMsg = wostr.str();
  std::wcout << kHelpMsg.c_str() << std::endl;
  system("pause");
  return 0;
}

// Syncs the Dev -> Hide/Show Console menu entry to the current console
// state. Greyed when no console is attached (i.e. we launched without
// --debug from Explorer); otherwise the label flips between "Hide
// Console" and "Show Console" to mirror the current visibility.
static void UpdateConsoleToggleMenu(HWND hWnd) {
  HMENU menu = GetMenu(hWnd);
  if (menu == nullptr) {
    return;
  }
  // Respect the .rc's GRAYED flag: if the build disabled this item,
  // never re-enable it or touch its label.
  if (s_console_menu_user_disabled) {
    if (hConsoleButton != nullptr) {
      EnableWindow(hConsoleButton, FALSE);
    }
    return;
  }
  if (!logging::GetIsConsoleAttached()) {
    EnableMenuItem(menu, IDM_CONSOLE, MF_BYCOMMAND | MF_GRAYED);
    if (hConsoleButton != nullptr) {
      EnableWindow(hConsoleButton, FALSE);
    }
    return;
  }
  EnableMenuItem(menu, IDM_CONSOLE, MF_BYCOMMAND | MF_ENABLED);
  // Label off our own intent bool (see s_console_hidden comment for why
  // we don't query IsWindowVisible here).
  // SetMenuItemInfoW with MIIM_STRING copies the string, so passing a
  // string-literal pointer through const_cast is safe.
  const LPCWSTR label = s_console_hidden ? kShowConsoleLabel : kHideConsoleLabel;
  MENUITEMINFOW mii   = {};
  mii.cbSize          = sizeof(mii);
  mii.fMask           = MIIM_STRING;
  mii.dwTypeData      = const_cast<LPWSTR>(label);
  SetMenuItemInfoW(menu, IDM_CONSOLE, FALSE, &mii);
  if (hConsoleButton != nullptr) {
    EnableWindow(hConsoleButton, TRUE);
    SetWindowTextW(hConsoleButton, label);
  }
}

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine,
                      int iCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  g_hInstance = hInstance;
  // Initialize common controls
  INITCOMMONCONTROLSEX icex;
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC  = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
  InitCommonControlsEx(&icex);
  // Now that comctl32 is initialized, probe its version once for callers
  // that need to gate v5.82+ behavior (notably the status-bar tooltip
  // TOOLINFO size that Win2000's v5.81 doesn't accept).
  can_use_582_controls = IsCommCtrlAtLeast(dwComCtl32TargetVer);

  static const std::wstring name   = GetAppName();
  static const LPCWSTR appTitle    = name.c_str();
  static const LPCWSTR szClassName = MAIN_WNDCLASS;

  // Parse the command line into a real argv via CommandLineToArgvW
  // (lpCmdLine is the post-exe-path tail only; we want the full thing
  // so argv[0] is the exe path that ParseCommandLine's loop skips).
  // Failure path is "no flags set" - we can't LOG(ERROR) here because
  // logging isn't initialized yet.
  int argc     = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!ParseCommandLine(argc, argv)) {
    std::wcerr << L"Failed to parse command line, aborting!" << std::endl;
    return 2;
  }
  if (argv != nullptr) {
    LocalFree(argv);
  }
  // Open a conhost window when we have anything text-y to show. Without
  // any of these flags, the log sink defaults to LOG_NONE - LOG() calls
  // become near-no-ops, useful in release.
  static const bool console_only      = g_show_version || g_show_help;
  logging::LogDest kLogSink           = console_only ? logging::LOG_TO_STDERR : logging::LOG_TO_ALL;
  static const std::wstring file_name = std::wstring(INTERNAL_NAME);
  const std::wstring kLogFile         = file_name + L".log";
  logging::LogInitSettings LoggingSettings;
  LoggingSettings.log_sink          = kLogSink;
  LoggingSettings.logfile_name      = kLogFile;
  LoggingSettings.app_name          = appTitle;
  LoggingSettings.show_func_sigs    = false;
  LoggingSettings.show_line_numbers = false;
  LoggingSettings.show_time         = false;
  LoggingSettings.full_prefix_level = LOG_ERROR;
  if (!logging::InitLogging(g_hInstance, LoggingSettings)) {
    ErrorBox(nullptr, L"Logging Initialization Failure", L"InitLogging failed!");
    return 3;
  }
  logging::SetIsDCheck(is_dcheck);
  if (g_show_version) {
    return ShowVersionAndExit();
  }
  if (g_show_help) {
    return ShowHelpAndExit();
  }
  LOG(INFO) << GetWelcomeMessage();

  kMainIcon  = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
  kSmallIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SMALL));

  // Register our window class.
  if (!RegisterWndClass(g_hInstance, szClassName)) {
    ErrorBox(nullptr, L"RegisterClassEx Error", L"This program requires Windows NT!");
    return 1;
  }

  // We always attach a console (so the Dev -> Console menu is useful
  // even in release builds), but unless we're in a mode that actually
  // wants it visible right now - --debug for live logging, --version
  // or --help for one-shot output before exit - hide it immediately
  // so a normal launch doesn't pop a console window the user didn't
  // ask for. Logs still flow to the hidden stream; "Show Console"
  // reveals it any time.
  if (!g_debug_mode && !g_show_version && !g_show_help) {
    if (logging::HideConsole()) {
      s_console_hidden = true;
    }
  }

  // Open our window now
  if (!InitWindow(g_hInstance, szClassName, appTitle, iCmdShow)) {
    return 4;
  }
  // Sync the Dev -> Console menu label to the current state. Console
  // is always attached now, so the only thing this does in practice
  // is flip the label between "Show Console" and "Hide Console"
  // based on s_console_hidden (set by the startup auto-hide above).
  UpdateConsoleToggleMenu(mainHwnd);

  HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_MAIN));
  if (hAccel == nullptr) {
    return 5;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    // Check whether this message is destined for the result viewer or one
    // of its children (the edit control). TranslateAcceleratorW processes
    // accelerators for ALL thread messages regardless of focus, so without
    // this guard Escape would reach the main window's confirm-exit path
    // even when the user intends to close only the viewer.
    const HWND result_hwnd = GetResultHwnd();
    const bool for_result =
        (result_hwnd != nullptr) && (msg.hwnd == result_hwnd || IsChild(result_hwnd, msg.hwnd));
    if (for_result) {
      // Escape closes the viewer; all other keys dispatch normally.
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
        CloseResultWindow();
        continue;
      }
    } else {
      // Accelerator first so menu shortcuts (Ctrl+Q, '?', etc.) win
      // over any TAB/arrow handling. Then IsDialogMessageW gives us
      // dialog-style navigation in the top pane (TAB / Shift+TAB
      // between WS_TABSTOP controls, arrows inside combos, Enter for
      // the default button) which a plain WM_KEYDOWN dispatch on a
      // non-dialog window wouldn't.
      if (TranslateAcceleratorW(mainHwnd, hAccel, &msg)) {
        continue;
      }
      if (IsDialogMessageW(mainHwnd, &msg)) {
        continue;
      }
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (hAccel != nullptr) {
    DestroyAcceleratorTable(hAccel);
  }
  return static_cast<int>(msg.wParam);
}

// Reads every menu item whose CHECKED state is wired to a runtime setting
// and applies it before the first frame. Add new ID->setter pairs here as
// settings come online.
static void ApplyMenuDefaults(HWND hWnd) {
  HMENU menu = GetMenu(hWnd);
  if (menu == nullptr) {
    return;
  }
  // Latch the .rc's GRAYED flag for IDM_CONSOLE. Must happen before
  // UpdateConsoleToggleMenu runs (called from wWinMain right after
  // InitWindow returns) so the latch is in place before the first
  // enable / label-swap attempt.
  s_console_menu_user_disabled = IsMenuGrayed(menu, IDM_CONSOLE);
  // Initial sound on/off from the .rc's CHECKED flag on IDM_SOUND.
  g_sound_on = IsMenuChecked(menu, IDM_SOUND);
  // Initial colored-output state from the .rc's CHECKED flag on IDM_COLOREDOUTPUT.
  g_colored_output = IsMenuChecked(menu, IDM_COLOREDOUTPUT);
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
        GetDlgItemTextW(hDlg, IDC_CUSTOM_EDIT, buf, 32);
        wchar_t* end            = nullptr;
        const unsigned long val = wcstoul(buf, &end, 10);
        if (end == buf || *end != L'\0' || val < params->min_val || val > params->max_val) {
          wchar_t errmsg[128];
          swprintf(errmsg, 128, L"Enter a whole number between %u and %u.", params->min_val,
                   params->max_val);
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

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      if (mainHwnd == nullptr) {
        mainHwnd = hWnd; // Prevent race condition in InitApp
      }
      CenterWindowOnScreen(hWnd, /*multimon=*/true);
      if (!RegisterSplitterClass(g_hInstance)) {
        LOG(ERROR) << L"Failed to register splitter class!";
        return -1;
      }
      if (!RegisterResultWindowClass(g_hInstance)) {
        LOG(ERROR) << L"Failed to register result window class!";
        return -1;
      }
      if (!CreateChildControls(hWnd)) {
        LOG(ERROR) << L"Failed to create child controls!";
        return -1;
      }
      s_prev_threads_sel         = GetInitialThreadsSel();
      s_threads_custom_injected  = IsInitialThreadsCustomInjected();
      InitApp(hWnd);
      SendOutputMessage(GetWelcomeMessage());
      break;
    case WM_TIMER: {
      break;
    }
    case WM_ERASEBKGND: {
      // Fill only the top pane (above the splitter) so the parent never
      // paints over the output edit below. Without WS_CLIPCHILDREN the
      // parent DC is not clipped, so we restrict manually.
      // This also satisfies DrawThemeParentBackground calls from the
      // groupbox: it forwards WM_ERASEBKGND with the child's own HDC,
      // which is already clipped to the groupbox rect, so FillRect lands
      // exactly where the groupbox background needs it.
      HDC hdc = reinterpret_cast<HDC>(wParam);
      RECT client_rc;
      GetClientRect(hWnd, &client_rc);
      const int spl = GetClampedSplitterY(cyClient);
      if (spl > 0) {
        client_rc.bottom = spl;
      }
      FillRectWithColor(hdc, client_rc, g_bkg_color);
      return TRUE;
    }
    case WM_GETMINMAXINFO: {
      LPMINMAXINFO pMinMaxInfo      = reinterpret_cast<LPMINMAXINFO>(lParam);
      pMinMaxInfo->ptMinTrackSize.x = CW_MINWIDTH;
      pMinMaxInfo->ptMinTrackSize.y = CW_MINHEIGHT;
      const int MAXWIDTH            = GetSystemMetrics(SM_CXMAXIMIZED);
      const int MAXHEIGHT           = GetSystemMetrics(SM_CYMAXIMIZED);
      pMinMaxInfo->ptMaxTrackSize.x = MAXWIDTH;
      pMinMaxInfo->ptMaxTrackSize.y = MAXHEIGHT;
      break;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT client_rc;
      GetClientRect(hWnd, &client_rc);
      const int spl = GetClampedSplitterY(cyClient);
      if (spl > 0) {
        client_rc.bottom = spl;
      }
      FillRectWithColor(hdc, client_rc, g_bkg_color);
      EndPaint(hWnd, &ps);
      break;
    }
    case WM_CTLCOLORSTATIC: {
      // ES_READONLY edits get coloured via WM_CTLCOLORSTATIC (not
      // WM_CTLCOLOREDIT), and the default response is a COLOR_BTNFACE
      // brush - which is grey on classic Win2k. Use white-on-black
      // normally, or black-on-green (CRT style) when g_colored_output.
      HWND hCtrl = reinterpret_cast<HWND>(lParam);
      HDC hdc    = reinterpret_cast<HDC>(wParam);
      if (hCtrl == hOutputEdit) {
        if (g_colored_output) {
          static HBRUSH s_crt_brush = CreateSolidBrush(RGB_BLACK);
          SetBkColor(hdc, RGB_BLACK);
          SetTextColor(hdc, RGB_GREEN);
          return reinterpret_cast<LRESULT>(s_crt_brush);
        }
        SetBkColor(hdc, RGB_WHITE);
        SetTextColor(hdc, RGB_BLACK);
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
      }
      // Everything else (plain STATIC labels, disabled edits, etc.) -
      // paint with the parent's g_bkg_color so the control blends into
      // the top pane instead of standing out at the system's
      // COLOR_BTNFACE shade. Brush is cached the first time we need it
      // and leaked at process exit; the OS reclaims GDI objects on
      // termination.
      static HBRUSH s_bkg_brush = nullptr;
      if (s_bkg_brush == nullptr) {
        s_bkg_brush = CreateSolidBrush(g_bkg_color);
      }
      SetBkColor(hdc, g_bkg_color);
      return reinterpret_cast<LRESULT>(s_bkg_brush);
    }
    case WM_SIZE: {
      // cxClient / cyClient mirror the main window's client area in pixels.
      cxClient = LOWORD(lParam);
      cyClient = HIWORD(lParam);
      if (wParam == SIZE_MINIMIZED) {
        s_was_minimized = true;
        break;
      }
      if (cyClient < 0) {
        cyClient = 0;
      }
      LayoutChildren(hWnd);
      break;
    }
    case WM_COMMAND: {
      const int command = LOWORD(wParam);
      switch (command) {
        case IDM_CEXIT:
          if (ConfirmExit(hWnd)) {
            ShutDownApp();
          }
          break;
        case IDM_EXIT:
          ShutDownApp();
          break;
        case IDM_ABOUT:
        case IDC_ABOUT_BUTTON:
          DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUTDLG), hWnd, AboutDlgProc);
          break;
        case IDC_START_BUTTON: {
          const int digits  = GetSelectedDigits();
          const int threads = GetSelectedThreads();
          if (digits < 0 || threads < 0) {
            WarnBox(hWnd, L"Invalid Selection",
                    L"Please select a digit and thread count before calculating.");
            break;
          }
          if (!StartCalculation(digits, threads)) {
            SendOutputMessage(L"Could not start (already running?).");
          }
          break;
        }
        case IDC_DIGITS_COMBO:
        case IDC_THREADS_COMBO: {
          if (HIWORD(wParam) != CBN_SELCHANGE) {
            break;
          }
          HWND hCombo          = reinterpret_cast<HWND>(lParam);
          const bool is_digits = (command == IDC_DIGITS_COMBO);
          const int count      = static_cast<int>(SendMessageW(hCombo, CB_GETCOUNT, 0, 0));
          const int sel        = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
          if (sel == CB_ERR) {
            break;
          }
          // "Custom" is always the last item; anything else just updates the
          // previous-selection tracker so we know what to revert to on Cancel.
          if (sel != count - 1) {
            if (is_digits) {
              s_prev_digits_sel = sel;
            } else {
              s_prev_threads_sel = sel;
            }
            break;
          }
          // "Custom" was selected — show the input dialog.
          CustomInputParams params = {};
          if (is_digits) {
            params.title      = L"Custom Digit Count";
            params.prompt     = L"Enter number of digits:";
            params.min_val    = kMinNumDigits;
            params.max_val    = kMaxNumDigits;
            params.edit_limit = 10; // 1,000,000,000 = 10 digits
          } else {
            params.title      = L"Custom Thread Count";
            params.prompt     = L"Enter number of threads:";
            params.min_val    = kMinNumThreads;
            params.max_val    = kMaxNumThreads;
            params.edit_limit = 3; // 256 = 3 digits
          }
          const INT_PTR res =
              DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_CUSTOM_INPUT), hWnd,
                              CustomInputDlgProc, reinterpret_cast<LPARAM>(&params));
          if (res == IDOK) {
            // Format the validated value and inject it just before "Custom".
            wchar_t val_str[32];
            swprintf(val_str, 32, L"%u", params.result);
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
          break;
        }
        case IDC_CONSOLE_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDM_CONSOLE, 0), 0);
          break;
        case IDC_EXIT_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDM_CEXIT, 0), 0);
          break;
        case IDC_OPENOUT_BUTTON:
          ToggleResultWindow(hWnd);
          break;
        case IDC_CLEARRESULT_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDM_CLEARRESULTS, 0), 0);
          break;
        case IDC_CLEAROUTPUT_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDM_CLEAROUTPUT, 0), 0);
          break;
        case IDC_STOP_BUTTON:
          if (!g_running.load()) {
            EmitLine(L"No Pi threads running", false);
          }
          StopCalculation();
          PlayWav(IDR_OHNO_WAV);
          break;
        case IDM_HELP:
          LaunchHelp(hWnd);
          break;
        case IDM_CLEARRESULTS:
          if (ConfirmClearResults(hWnd)) {
            if (!ClearResultFile()) {
              ErrorBox(hWnd, L"Results File Error", L"Failed to clear results file.");
            } else {
              ReloadResultWindow();
              LOG(INFO) << L"Cleared result file " << kResultsFile;
            }
          }
          break;
        case IDM_CLEAROUTPUT:
          ClearOutput();
          break;
        case IDM_CLEARLOG:
          if (!logging::ClearFileContents()) {
            ErrorBox(hWnd, L"Log File Error", L"Failed to clear log file.");
          } else {
            CLOG(INFO) << L"Cleared log file";
          }
          break;
        case IDM_OPENLOG: {
          const std::wstring logpath = logging::GetLogFilePath();
          if (!logpath.empty()) {
            ShellExecuteW(hWnd, L"open", logpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          } else {
            CLOG(INFO) << L"Opened log file";
          }
          break;
        }
        case IDM_SAVEAS: {
          std::wstring savepath;
          if (!SaveClientBitmap(hWnd, &savepath)) {
            // SaveClientBitmap also returns false when the user just
            // cancels the Save dialog. CommDlgExtendedError() is 0 in
            // that case (no actual error), non-zero on a real common-
            // dialog failure. Only show the box on a real failure.
            if (CommDlgExtendedError() != 0) {
              ErrorBox(hWnd, L"Save Screenshot Error", L"Failed to save screenshot!");
            } else {
              EmitLine(std::wstring(L"Saved screenshot to ") + savepath, false);
            }
          }
          break;
        }
        case IDM_SOUND: {
          // CHECKED == Sounds on. Push into the global so picalc's
          // completion-chime check sees the new state.
          g_sound_on = ToggleMenuCheck(hWnd, IDM_SOUND);
          // LOG(INFO) << L"Toggled sound " << g_sound_on ? L"on" : L"off";
          break;
        }
        case IDM_COLOREDOUTPUT: {
          g_colored_output = ToggleMenuCheck(hWnd, IDM_COLOREDOUTPUT);
          // Force the output edit to repaint immediately.
          if (hOutputEdit != nullptr) {
            InvalidateRect(hOutputEdit, nullptr, TRUE);
            // LOG(INFO) << L"Colored status pane " << g_colored_output ? L"on" : L"off";
          } else {
            LOG(ERROR) << L"No edit control to color!";
          }
          break;
        }
        case IDM_CONSOLE: {
          // Flip the console window's visibility. If the .rc disabled
          // this feature outright, or if no console is attached
          // (released build, launched from Explorer without --debug),
          // the menu item is greyed out and we shouldn't get here - but
          // double-check anyway since accelerators / WM_COMMAND can
          // arrive even for a greyed item.
          if (s_console_menu_user_disabled) {
            break;
          }
          if (!logging::GetIsConsoleAttached()) {
            LOG(ERROR) << L"No console attached to window";
            break;
          }
          // Drive the flip off our intent bool, not IsWindowVisible -
          // see s_console_hidden's comment for the Win11 Terminal
          // pseudo-window quirk. Only flip the bool if the actual
          // Show/Hide call succeeded so a failed SW_HIDE (which can
          // happen on Win11 Terminal) doesn't leave the label lying.
          if (s_console_hidden) {
            if (logging::ShowConsole(false)) { // false = don't steal focus
              s_console_hidden = false;
              LOG(DEBUG) << L"Showed console.";
            } else {
              LOG(ERROR) << L"Failed to show console!";
            }
          } else {
            if (logging::HideConsole()) {
              s_console_hidden = true;
              LOG(DEBUG) << L"Hid console.";
            } else {
              LOG(ERROR) << L"Failed to hide console!";
            }
          }
          UpdateConsoleToggleMenu(hWnd);
          break;
        }
        case IDM_RUN:
          OpenRunDialog(hWnd);
          break;
        default:
          return DefWindowProcW(hWnd, message, wParam, lParam);
      }
    } break;
    case WM_HELP:
      LaunchHelp(hWnd);
      break;
    case WM_CLOSE:
      ShutDownApp();
      break;
    case WM_QUERYENDSESSION:
      LOG(DEBUG) << L"Window station is going down now!";
      return TRUE;
    case WM_DESTROY:
      // Close the result file
      CloseResultFile();
      PostQuitMessage(0); // WM_QUIT
      break;
    case WM_NCDESTROY:
      mainHwnd = nullptr;
      // Last message this window will receive. Close log file, and console
      // cleanly here, before the message loop sees the WM_QUIT that
      // WM_DESTROY's PostQuitMessage queued.
      logging::DeInitLogging(g_hInstance);
      break;
    case WM_PICALC_RELOAD_RESULTS:
      // Posted by the calc worker thread on completion, and by
      // ToggleResultWindow on open. Kicks off the async file load.
      ReloadResultWindow();
      return 0;
    default:
      return DefWindowProcW(hWnd, message, wParam, lParam);
  }
  return 0;
}

bool InitApp(HWND hWnd) {
  if (hWnd == nullptr) {
    return false;
  }
  // Pull defaults from the menu's CHECKED state first,
  // so they need the final values by the time they run.
  ApplyMenuDefaults(hWnd);
  return true;
}

void ShutDownApp() {
  // mainHwnd is cleared in WM_NCDESTROY; guard so a duplicate exit
  // path (e.g. WM_CLOSE arriving after WM_DESTROY's tear-down began)
  // doesn't pass NULL to DestroyWindow, which is undefined per MSDN.
  if (mainHwnd != nullptr) {
    // Result window has no owner, so it won't be auto-destroyed with the
    // main window. Close it first so it doesn't linger after app exit.
    CloseResultWindow();
    DestroyWindow(mainHwnd);
  }
}

bool LaunchHelp(HWND hWnd) {
  bool success = false;
  if (InfoBox(hWnd, L"Help32", L"No help yet...")) {
    success = true;
  }
  return success;
}

INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  UNREFERENCED_PARAMETER(lParam);
  switch (message) {
    case WM_INITDIALOG:
      // Set icon in titlebar of about dialog
      static const HICON kAboutIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_ABOUT));
      SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)kAboutIcon);
      SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)kAboutIcon);
      if (g_sound_on) {
        PlayWav(IDR_NOTIFY_WAV);
      }
      LOG(INFO) << L"Showed About dialog";
      return TRUE;
    case WM_CLOSE:
      EndDialog(hDlg, TRUE);
      return TRUE;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, LOWORD(wParam));
        return TRUE;
      }
      break;
    default:
      break;
  }
  return FALSE;
}
