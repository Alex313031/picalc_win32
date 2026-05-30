/*------------------------------------------
   PiCalc Win32
   Copyright (c) 2026 Alex313031
  ------------------------------------------*/

#include "main.h"

#include <timeapi.h>

#include "controls.h"
#include "globals.h"
#include "resource.h"
#include "results.h"
#include "strings.h"
#include "sysmon.h"

// =========================================================================
// Statics
// =========================================================================

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

// Sysmon polling interval, latched from the .rc's CHECKED speed item in
// ApplyMenuDefaults. Defaults to kMedSpeed if no item is checked.
static UINT s_sysmon_speed = kMedSpeed;

// Double-buffer for the main window's top pane. Created lazily in WM_PAINT,
// resized on WM_SIZE, freed in WM_DESTROY. Eliminates the grey-flash flicker
// caused by the parent painting under children before they repaint.
static HDC s_main_memDC         = nullptr;
static HBITMAP s_main_memBmp    = nullptr;
static HBITMAP s_main_oldMemBmp = nullptr;
static int s_main_mem_cx        = 0;
static int s_main_mem_cy        = 0;

static void DestroyMainBackbuffer() {
  if (s_main_memBmp != nullptr) {
    SelectObject(s_main_memDC, s_main_oldMemBmp);
    DeleteObject(s_main_memBmp);
    s_main_memBmp    = nullptr;
    s_main_oldMemBmp = nullptr;
  }
  if (s_main_memDC != nullptr) {
    DeleteDC(s_main_memDC);
    s_main_memDC = nullptr;
  }
  s_main_mem_cx = s_main_mem_cy = 0;
}

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

COLORREF g_bkg_color = GetSysColor(COLOR_3DFACE); // Standard grey background

bool is_on_wine            = false; // Whether we are on wine
static std::string winever = "";

// =========================================================================
// Forward declarations
// =========================================================================

static bool ParseCommandLine(int argc, LPWSTR argv[]);
static int ShowVersionAndExit();
static int ShowHelpAndExit();
static void UpdateConsoleToggleMenu(HWND hWnd);
static void ApplyMenuDefaults(HWND hWnd);
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
  // We handle erase + paint ourselves: WM_ERASEBKGND returns TRUE
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
  std::wcout << L"\n " << GetAppName() << L" Version " << GetVersionString() << L"\n " << std::endl;
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

  // Raise the process priority class so the calculation worker threads and
  // the sysmon timer tick win scheduler quanta against other normal-priority
  // user processes. Threads inherit the elevated base priority, so we don't
  // have to touch each one individually. ABOVE_NORMAL keeps the UI input
  // queue responsive even when the calc is pinning every core - HIGH starves
  // foreground GUI threads of other apps and REALTIME starves the system
  // (audio glitches, mouse lag). No special privilege is required for
  // ABOVE_NORMAL, so this should always succeed; ignore the return.
  SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

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
  timeBeginPeriod(
      kTimerResolution); // raise system timer resolution to 1ms for accurate sysmon ticks
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
  is_on_wine = IsRunningOnWine(&winever);
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

  // Post the startup-complete marker last so it lands at the tail of the
  // queue. By the time WindowProc pulls it, every synchronous init message
  // (WM_CREATE / WM_SIZE / WM_PAINT from CreateWindow / ShowWindow /
  // UpdateWindow) has drained and the pump is in steady state.
  PostMessageW(mainHwnd, WM_PICALC_STARTUP_COMPLETE, 0, 0);

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
      if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE || msg.wParam == 'R')) {
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
      // Enter on a closed combobox starts the calculation. IsDialogMessageW
      // can swallow VK_RETURN on CBS_DROPDOWNLIST combos without routing it
      // to the default button, so intercept it here first.
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
        const HWND focused = GetFocus();
        if ((focused == hDigitsCombo || focused == hThreadsCombo) &&
            !SendMessageW(focused, CB_GETDROPPEDSTATE, 0, 0)) {
          SendMessageW(mainHwnd, WM_COMMAND, MAKEWPARAM(IDC_START_BUTTON, BN_CLICKED),
                       reinterpret_cast<LPARAM>(hStartButton));
          continue;
        }
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
  timeEndPeriod(kTimerResolution); // restore default timer resolution on exit
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
  // Sysmon speed: whichever of the three speed items the .rc marks CHECKED.
  // Fall through to kMedSpeed if none are explicitly checked.
  UINT speed_id = IDM_MED;
  if (IsMenuChecked(menu, IDM_SLOW)) {
    s_sysmon_speed = kSlowSpeed;
    speed_id       = IDM_SLOW;
  } else if (IsMenuChecked(menu, IDM_FAST)) {
    s_sysmon_speed = kHighSpeed;
    speed_id       = IDM_FAST;
  } else {
    s_sysmon_speed = kMedSpeed;
    speed_id       = IDM_MED;
  }
  // Replace the .rc's plain CHECKED mark with a radio check so the
  // three speed items behave as a mutually exclusive group.
  CheckMenuRadioItem(menu, IDM_SLOW, IDM_FAST, speed_id, MF_BYCOMMAND);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
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
      InitApp(hWnd);
      SetFocus(hStartButton);
    } break;
    case WM_TIMER: {
      if (wParam == IDT_MONTIMER) {
        OnSysmonTick(hWnd);
      }
      break;
    }
    case WM_ERASEBKGND: {
      // Two distinct callers reach here:
      //   1. The OS, as part of our own paint cycle. WindowFromDC(hdc) == hWnd.
      //      We defer to WM_PAINT, which composites into a backbuffer - return
      //      TRUE to suppress the default erase (and the grey flash that comes
      //      with it).
      //   2. DrawThemeParentBackground from a child (typically the themed
      //      groupbox), forwarding WM_ERASEBKGND with the child's own DC.
      //      WindowFromDC returns the child HWND. We paint the parent bg into
      //      that DC so the child sees the right backdrop.
      HDC hdc = reinterpret_cast<HDC>(wParam);
      if (WindowFromDC(hdc) == hWnd) {
        return TRUE;
      }
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
      // Double-buffered composition of the top pane:
      //   1. Fill bg into the backbuffer.
      //   2. Send WM_PRINTCLIENT to each direct child that lives in the top
      //      pane (skipping the splitter and the output edit, which paint
      //      themselves directly to screen below the splitter).
      //   3. BitBlt the composed backbuffer to the screen in one operation.
      // The OS still sends WM_PAINT to children after this; their direct
      // repaints draw the same pixels we just BitBlt'd, so there is no
      // visible flicker from the parent's paint cycle.
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT client_rc;
      GetClientRect(hWnd, &client_rc);
      const int spl   = GetClampedSplitterY(cyClient);
      const int top_h = (spl > 0) ? spl : client_rc.bottom;
      const int cx    = client_rc.right;

      if (cx > 0 && top_h > 0) {
        if (s_main_memDC == nullptr) {
          s_main_memDC = CreateCompatibleDC(hdc);
        }
        if (s_main_memBmp == nullptr || s_main_mem_cx != cx || s_main_mem_cy != top_h) {
          if (s_main_memBmp != nullptr) {
            SelectObject(s_main_memDC, s_main_oldMemBmp);
            DeleteObject(s_main_memBmp);
          }
          s_main_memBmp    = CreateCompatibleBitmap(hdc, cx, top_h);
          s_main_oldMemBmp = static_cast<HBITMAP>(SelectObject(s_main_memDC, s_main_memBmp));
          s_main_mem_cx    = cx;
          s_main_mem_cy    = top_h;
        }

        RECT mem_rc = {0, 0, cx, top_h};
        FillRectWithColor(s_main_memDC, mem_rc, g_bkg_color);

        for (HWND child = GetWindow(hWnd, GW_CHILD); child != nullptr;
             child      = GetWindow(child, GW_HWNDNEXT)) {
          if (child == hSplitter || child == hOutputEdit) {
            continue;
          }
          if (!IsWindowVisible(child)) {
            continue;
          }
          RECT cr;
          GetWindowRect(child, &cr);
          MapWindowPoints(nullptr, hWnd, reinterpret_cast<LPPOINT>(&cr), 2);
          if (cr.top >= top_h) {
            continue;
          }

          const int saved = SaveDC(s_main_memDC);
          OffsetViewportOrgEx(s_main_memDC, cr.left, cr.top, nullptr);
          SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(s_main_memDC),
                       PRF_CLIENT | PRF_CHILDREN | PRF_NONCLIENT);
          RestoreDC(s_main_memDC, saved);
        }

        BitBlt(hdc, 0, 0, cx, top_h, s_main_memDC, 0, 0, SRCCOPY);
      }

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
      // Drop the backbuffer; WM_PAINT recreates it at the new size.
      DestroyMainBackbuffer();
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
          StartCalculation(digits, threads);
          break;
        }
        case IDC_DIGITS_COMBO:
        case IDC_THREADS_COMBO:
          HandleComboBoxes(hWnd, wParam, lParam);
          break;
        case IDC_CONSOLE_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDM_CONSOLE, 0), 0);
          break;
        case IDC_EXIT_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDM_CEXIT, 0), 0);
          break;
        case IDC_RESULT:
          ToggleResultWindow(hWnd);
          break;
        case IDC_CLEARRESULT_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDC_CLEARRESULTS, 0), 0);
          break;
        case IDC_CLEAROUTPUT_BUTTON:
          SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDC_CLEAROUTPUT, 0), 0);
          break;
        case IDC_STOP_BUTTON: {
          // StopCalculation does the atomic g_running check internally and
          // returns true iff it actually requested a stop. Branching on its
          // return (instead of re-reading g_running here first) collapses
          // the two reads into one and removes the race where a worker
          // could finish naturally between the check and the call.
          const bool stopped = StopCalculation();
          if (!stopped) {
            EmitLine(L"No Pi threads running.", false);
          } else if (g_sound_on) {
            PlayWav(IDR_OHNO_WAV);
          }
        } break;
        case IDM_HELP:
          LaunchHelp(hWnd);
          break;
        case IDC_CLEARRESULTS:
          if (ConfirmClearResults(hWnd)) {
            if (!ClearResultFile()) {
              ErrorBox(hWnd, L"Results File Error", L"Failed to clear results file.");
            } else {
              ReloadResultWindow();
            }
          }
          break;
        case IDC_CLEAROUTPUT:
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
          LOG(INFO) << L"Toggled sound " << (g_sound_on ? L"on" : L"off");
          break;
        }
        case IDM_COLOREDOUTPUT: {
          g_colored_output = ToggleMenuCheck(hWnd, IDM_COLOREDOUTPUT);
          // Force the output edit to repaint immediately.
          if (hOutputEdit != nullptr) {
            InvalidateRect(hOutputEdit, nullptr, TRUE);
            LOG(INFO) << L"Colored status pane turned " << (g_colored_output ? L"on" : L"off");
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
              LOG(INFO) << L"Showed console.";
            } else {
              LOG(ERROR) << L"Failed to show console!";
            }
          } else {
            if (logging::HideConsole()) {
              s_console_hidden = true;
              LOG(INFO) << L"Hid console.";
            } else {
              LOG(ERROR) << L"Failed to hide console!";
            }
          }
          UpdateConsoleToggleMenu(hWnd);
          break;
        }
        case IDM_SLOW:
          s_sysmon_speed = kSlowSpeed;
          CheckMenuRadioItem(GetMenu(hWnd), IDM_SLOW, IDM_FAST, IDM_SLOW, MF_BYCOMMAND);
          StartSysmon(hWnd, s_sysmon_speed);
          break;
        case IDM_MED:
          s_sysmon_speed = kMedSpeed;
          CheckMenuRadioItem(GetMenu(hWnd), IDM_SLOW, IDM_FAST, IDM_MED, MF_BYCOMMAND);
          StartSysmon(hWnd, s_sysmon_speed);
          break;
        case IDM_FAST:
          s_sysmon_speed = kHighSpeed;
          CheckMenuRadioItem(GetMenu(hWnd), IDM_SLOW, IDM_FAST, IDM_FAST, MF_BYCOMMAND);
          StartSysmon(hWnd, s_sysmon_speed);
          break;
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
      DestroyMainBackbuffer();
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
    case WM_PICALC_STARTUP_COMPLETE:
      // Posted once at the tail of the startup message queue by wWinMain.
      // By the time we get here every synchronous init message has been
      // dispatched, so this is the earliest "fully running" moment.
      LOG(INFO) << L"Startup complete in " << MsSinceProcessStart() << L"ms";
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
  SendOutputMessage(GetWelcomeMessage());
  if (is_on_wine) {
    EmitLine(L"Running on Wine " + ToWide(winever) + L" (NT Version " + GetNTVerString() + L")",
             false);
  } else {
    EmitLine(L"Windows NT Version " + GetNTVerString(), false);
  }
  // Pull defaults from the menu's CHECKED state first,
  // so they need the final values by the time they run.
  ApplyMenuDefaults(hWnd);
  StartSysmon(hWnd, s_sysmon_speed);
  return true;
}

void ShutDownApp() {
  // mainHwnd is cleared in WM_NCDESTROY; guard so a duplicate exit
  // path (e.g. WM_CLOSE arriving after WM_DESTROY's tear-down began)
  // doesn't pass NULL to DestroyWindow, which is undefined per MSDN.
  if (mainHwnd != nullptr) {
    StopSysmon(mainHwnd);
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
