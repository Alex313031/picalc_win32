#ifndef PICALCWIN32_CONSTANTS_H_
#define PICALCWIN32_CONSTANTS_H_

#include "framework.h"

// Color constants
#define RGB_BLACK   RGB(0, 0, 0)
#define RGB_WHITE   RGB(255, 255, 255)
#define RGB_GREY    RGB(128, 128, 128)
#define RGB_DKGREY  RGB(64, 64, 64)
#define RGB_RED     RGB(255, 0, 0)
#define RGB_GREEN   RGB(0, 255, 0)
#define RGB_BLUE    RGB(0, 0, 255)
#define RGB_YELLOW  RGB(255, 255, 0)
#define RGB_CYAN    RGB(0, 255, 255)
#define RGB_MAGENTA RGB(255, 0, 255)
// Custom colors
#define RGB_DARKRED     RGB(96, 0, 0)
#define RGB_DARKGREEN   RGB(0, 96, 0)
#define RGB_DARKBLUE    RGB(0, 0, 96)
#define RGB_DARKYELLOW  RGB(96, 96, 0)
#define RGB_DARKCYAN    RGB(0, 96, 96)
#define RGB_DARKMAGENTA RGB(96, 0, 96)

// Output file name, written side-by-side with the .exe.
inline constexpr wchar_t kResultsFile[] = L"picalc_results.txt";

// Default desired outer window size at startup.
inline constexpr INT CW_WIDTH  = 800;
inline constexpr INT CW_HEIGHT = 600;

// Min window size
inline constexpr INT CW_MINWIDTH  = 500;
inline constexpr INT CW_MINHEIGHT = 420;

// Result viewer popup window default/min dimensions.
inline constexpr INT kResultWindowWidth     = 600;
inline constexpr INT kResultWindowHeight    = 600;
inline constexpr INT kResultWindowMinWidth  = 200;
inline constexpr INT kResultWindowMinHeight = 100;

// Splitter bar dimensions (height in px) and the minimum height either
// pane (top controls / bottom output) is allowed to shrink to while
// being dragged.
inline constexpr INT kSplitterHeight           = 10;
inline constexpr INT kSplitterHandleWidth      = 14;
inline constexpr INT kSplitterHandleHeight     = 1;
inline constexpr COLORREF kSplitterHandleColor = RGB_GREY;

// Default to a 1/2 top, 1/2 bottom pane split on first layout.
// Computed in float so the ratio is explicit.
inline constexpr float kTopPaneFraction = 1.0f / 2.0f;

// Top-pane control layout (in pixels, relative to the parent client area).
inline constexpr INT kGroupMargin = 7; // groupbox outer margin: left, right, bottom
inline constexpr INT kGroupOuterTop =
    10; // groupbox outer margin: top (distance from client top to frame line)
inline constexpr INT kGroupInnerPad = 10; // inner padding: frame line → first control row
inline constexpr INT kPadLeft       = 14;
inline constexpr INT kPadTop        = 14;
inline constexpr INT kHGap          = 5;
inline constexpr INT kVGap          = 7;
inline constexpr INT kLabelWidth    = 100;
inline constexpr INT kControlHeight = 21;
inline constexpr INT kComboWidth    = 100;
inline constexpr INT kButtonWidth   = kComboWidth;
inline constexpr INT kButtonHeight  = 28;
// Height passed to a combobox at create time is the *dropdown list*
// height when open, not the always-visible field height (that's font-
// derived). 200px gives room for ~8 options without scrolling.
inline constexpr INT kComboDropHeight = 200;

// Y of all top-pane groupbox HWNDs from client origin (frame line lands at
// kGroupOuterTop, the HWND sits kGroupMargin above that).
inline constexpr INT kGroupHwndTop = kGroupOuterTop - kGroupMargin;

// Right edge of the controls column inside the controls groupbox, and the
// resulting HWND width of the controls groupbox itself.
inline constexpr INT kControlsRight      = kPadLeft + kLabelWidth + kHGap + kComboWidth;
inline constexpr INT kControlsGroupWidth = kControlsRight + kGroupMargin;

// Minimum top-pane height: enough to show all 6 button rows with inner and
// outer groupbox padding, derived from layout constants so it stays in sync.
//   row1_y                            = kGroupOuterTop + kGroupInnerPad
//   + 2 label/combo row gaps          = (kControlHeight + kPadTop) * 2
//   + 3 button row gaps               = (kButtonHeight + kVGap) * 3
//   + last row height                 = kButtonHeight
//   + inner bottom pad (frame gap)    = kVGap
//   + outer bottom margin             = kGroupMargin
inline constexpr INT kMinTopHeight = (kGroupOuterTop + kGroupInnerPad) +
                                     (kControlHeight + kPadTop) * 2 + (kButtonHeight + kVGap) * 3 +
                                     kButtonHeight + kVGap + kGroupMargin;

// Minimum height of the output (bottom) pane.
inline constexpr INT kMinBottomHeight = CW_MINHEIGHT / 3;

// Fallback wrap column for the result viewer used when the window width
// cannot be measured (e.g. GetClientRect fails). Dynamic value computed
// from actual window width at open time; see ComputeWrapWidth in results.cc.
inline constexpr size_t kResultWrapWidth = 80u;
inline constexpr int kSeparatorWidth = static_cast<int>(kResultWrapWidth);

inline constexpr UINT kMinNumDigits = 1u;         // Min would be 3
inline constexpr UINT kMaxNumDigits = 1000000000; // 1 Billion max digits cap

// Max digits shown in the output pane; anything beyond this is truncated and
// the full result is written to the result file only.
inline constexpr UINT kMaxPrintNumDigits = 32;

// Maximum bytes that can be loaded into an edit control
inline constexpr size_t kMaxEditLoadBytes = static_cast<size_t>(50u * 1024u * 1024u); // 50 MB

inline constexpr UINT kMinNumThreads = 1u;   // Need at least 1 thread
inline constexpr UINT kMaxNumThreads = 256u; // No consumer CPUs have more than this

// Custom WM_APP messages for cross-thread UI notifications.
// Posted (never sent) from worker threads to mainHwnd so the UI thread
// performs the actual window update, avoiding cross-thread SendMessage hangs.
inline constexpr UINT WM_PICALC_RELOAD_RESULTS = WM_APP + 1;

// Child window style
inline constexpr DWORD dwCHILD = WS_CHILD | WS_VISIBLE;

// Minimum common controls version for certain functions, used for fallback codepaths
// See https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions
inline constexpr DWORD dwComCtl32TargetVer =
    _PACKVERSION(static_cast<DWORD>(5u), static_cast<DWORD>(82u));

// Delay in milliseconds for system monitor timer.
inline constexpr unsigned long kSlowSpeed = 2000UL;
inline constexpr unsigned long kMedSpeed  = 1000UL;
inline constexpr unsigned long kHighSpeed = 500UL;

#endif // PICALCWIN32_CONSTANTS_H_
