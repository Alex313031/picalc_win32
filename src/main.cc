/*------------------------------------------
   PiCalc Win32
   Copyright (c) 2026 Alex313031
  ------------------------------------------*/

#include "main.h"

#include "controls.h"
#include "globals.h"
#include "resource.h"
#include "strings.h"

// =========================================================================
// Globals
// =========================================================================

HWND mainHwnd      = nullptr;
HWND hOutputEdit   = nullptr;
HWND hSplitter     = nullptr;
HWND hDigitsLabel  = nullptr;
HWND hDigitsCombo  = nullptr;
HWND hThreadsLabel = nullptr;
HWND hThreadsCombo = nullptr;
HWND hStartButton   = nullptr;
HWND hStopButton    = nullptr;
HWND hOpenOutButton = nullptr;
HWND hAboutButton   = nullptr;

HINSTANCE g_hInstance = nullptr;

int cxClient = 0;
int cyClient = 0;

// Whether Pi Calculation is currently running
volatile bool g_running = false;

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

// Latched at startup from the .rc's initial GRAYED flag on IDM_CONSOLE.
// When true, UpdateConsoleToggleMenu leaves the item alone (no enable,
// no label swap) and the WM_COMMAND handler refuses to toggle - the
// .rc gets the final say on whether the feature is available at all.
static bool s_console_menu_user_disabled = false;

// Whether window was minimized or not
static bool s_was_minimized = false;

COLORREF g_bkg_color = RGB_LTGREY;

// =========================================================================
// Static forward declarations
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
  // We handle erase + paint ourselves - WM_ERASEBKGND returns
  // TRUE and WM_PAINT fills with g_bkg_color. Set to black default as fallback.
  wndclass.hbrBackground = CreateSolidBrush(g_bkg_color);
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
  static constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                 WS_MAXIMIZEBOX | WS_SIZEBOX | WS_CLIPCHILDREN;

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
  for (int i = 1; i < argc; ++i) {
    wchar_t* arg = argv[i];
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
    return;
  }
  if (!logging::GetIsConsoleAttached()) {
    EnableMenuItem(menu, IDM_CONSOLE, MF_BYCOMMAND | MF_GRAYED);
    return;
  }
  EnableMenuItem(menu, IDM_CONSOLE, MF_BYCOMMAND | MF_ENABLED);
  const HWND console = logging::GetCurrentConsole();
  const bool visible = (console != nullptr) && (IsWindowVisible(console) != 0);
  // SetMenuItemInfoW with MIIM_STRING copies the string, so passing a
  // string-literal pointer through const_cast is safe.
  MENUITEMINFOW mii = {};
  mii.cbSize        = sizeof(mii);
  mii.fMask         = MIIM_STRING;
  mii.dwTypeData    = const_cast<LPWSTR>(visible ? kHideConsoleLabel : kShowConsoleLabel);
  SetMenuItemInfoW(menu, IDM_CONSOLE, FALSE, &mii);
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
  const bool open_console = g_debug_mode || g_show_version || g_show_help;
  const logging::LogDest kLogSink =
      open_console ? g_debug_mode ? logging::LOG_TO_ALL : logging::LOG_TO_STDERR
                   : logging::LOG_NONE;
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

  // Open our window now
  if (!InitWindow(g_hInstance, szClassName, appTitle, iCmdShow)) {
    return 4;
  }
  // Grey the Dev -> Toggle Console item when InitLogging left us without
  // a console (i.e. no --debug). When a console is attached the item is
  // enabled and clicking it flips visibility.
  UpdateConsoleToggleMenu(mainHwnd);

  HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_MAIN));
  if (hAccel == nullptr) {
    return 5;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(mainHwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
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
      if (!CreateChildControls(hWnd)) {
        LOG(ERROR) << L"Failed to create child controls!";
        return -1;
      }
      InitApp(hWnd);
      SendOutputMessage(GetWelcomeMessage());
      break;
    case WM_TIMER: {
      break;
    }
    case WM_ERASEBKGND:
      // Returning TRUE tells Windows we have handled background erasing
      // ourselves, suppressing the default white fill. We do our own filling
      // in WM_PAINT so the two operations don't race or double-paint.
      return TRUE;
    case WM_GETMINMAXINFO: {
      LPMINMAXINFO pMinMaxInfo = reinterpret_cast<LPMINMAXINFO>(lParam);
      ;
      pMinMaxInfo->ptMinTrackSize.x = CW_MINWIDTH;
      pMinMaxInfo->ptMinTrackSize.y = CW_MINHEIGHT;
      const int MAXWIDTH            = GetSystemMetrics(SM_CXMAXIMIZED);
      const int MAXHEIGHT           = GetSystemMetrics(SM_CYMAXIMIZED);
      pMinMaxInfo->ptMaxTrackSize.x = MAXWIDTH;
      pMinMaxInfo->ptMaxTrackSize.y = MAXHEIGHT;
      break;
    }
    case WM_PAINT: {
      // WM_ERASEBKGND returned TRUE so Windows skipped its bg fill; we own
      // the entire client rect and paint it ourselves in one solid color.
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT client;
      GetClientRect(hWnd, &client);
      FillRectWithColor(hdc, client, g_bkg_color);
      EndPaint(hWnd, &ps);
      break;
    }
    case WM_CTLCOLORSTATIC: {
      // ES_READONLY edits get coloured via WM_CTLCOLORSTATIC (not
      // WM_CTLCOLOREDIT), and the default response is a COLOR_BTNFACE
      // brush - which is grey on classic Win2k. Force notepad-style
      // white-on-black for our output pane.
      HWND hCtrl = reinterpret_cast<HWND>(lParam);
      HDC hdc    = reinterpret_cast<HDC>(wParam);
      if (hCtrl == hOutputEdit) {
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
          PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC);
          DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUTDLG), hWnd, AboutDlgProc);
          break;
        case IDM_HELP:
          LaunchHelp(hWnd);
          break;
        case IDM_SAVEAS: {
          std::wstring savepath;
          if (!SaveClientBitmap(hWnd, &savepath)) {
            ErrorBox(hWnd, L"Save Screenshot Error", L"Failed to save screenshot!");
          }
          break;
        }
        case IDM_SOUND: {
          // CHECKED == Souds on
          const bool now_on = ToggleMenuCheck(hWnd, IDM_SOUND);
          //SetSoundOn(now_on);
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
          const HWND console = logging::GetCurrentConsole();
          if (console == nullptr) {
            break;
          }
          if (IsWindowVisible(console)) {
            logging::HideConsole();
          } else {
            logging::ShowConsole(false); // false = don't steal focus
          }
          UpdateConsoleToggleMenu(hWnd);
          break;
        }
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
      return TRUE;
    case WM_DESTROY:
      PostQuitMessage(0);
      break;
    case WM_NCDESTROY:
      mainHwnd = nullptr;
      // Last message this window will receive. Close the log file /
      // console cleanly here, before the message loop sees the WM_QUIT
      // that WM_DESTROY's PostQuitMessage queued.
      logging::DeInitLogging(g_hInstance);
      break;
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
