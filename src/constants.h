#ifndef PICALCWIN32_CONSTANTS_H_
#define PICALCWIN32_CONSTANTS_H_

#include "framework.h"

// Color constants
#define RGB_BLACK   RGB(0, 0, 0)
#define RGB_WHITE   RGB(255, 255, 255)
#define RGB_GREY    RGB(128, 128, 128)
#define RGB_DKGREY  RGB(64, 64, 64)
#define RGB_LTGREY  RGB(192, 192, 192) // Classic Win9x/2000 button-face grey
#define RGB_RED     RGB(255, 0, 0)
#define RGB_GREEN   RGB(0, 255, 0)
#define RGB_BLUE    RGB(0, 0, 255)
#define RGB_YELLOW  RGB(255, 255, 0)
#define RGB_CYAN    RGB(0, 255, 255)
#define RGB_MAGENTA RGB(255, 0, 255)

// Default desired outer window size at startup.
inline constexpr INT CW_WIDTH  = 600;
inline constexpr INT CW_HEIGHT = 800;

// Min window size
inline constexpr INT CW_MINWIDTH  = 320;
inline constexpr INT CW_MINHEIGHT = 480;

// Splitter bar dimensions (height in px) and the minimum height either
// pane (top controls / bottom output) is allowed to shrink to while
// being dragged.
inline constexpr INT kSplitterHeight = 5;
inline constexpr INT kMinPaneHeight  = CW_MINHEIGHT / 2;

// Top-pane control layout (in pixels, relative to the parent client area).
inline constexpr INT kPadLeft       = 14;
inline constexpr INT kPadTop        = 14;
inline constexpr INT kHGap          = 5;
inline constexpr INT kVGap          = 7;
inline constexpr INT kLabelWidth    = 100;
inline constexpr INT kControlHeight = 21;
inline constexpr INT kComboWidth    = 90;
inline constexpr INT kButtonWidth   = kComboWidth;
inline constexpr INT kButtonHeight  = 28;
// Height passed to a combobox at create time is the *dropdown list*
// height when open, not the always-visible field height (that's font-
// derived). 200px gives room for ~8 options without scrolling.
inline constexpr INT kComboDropHeight = 200;

inline constexpr UINT kMinNumDigits = 1u; // Min would be 3
inline constexpr UINT kMaxNumDigits = 1000000000; // 1 Billion max digits cap

// 10,000 max digits printable in output area, any more and it will only output
// the full result to a text file, truncating to this in the output area.
inline constexpr UINT kMaxPrintNumDigits = 10000;

inline constexpr UINT kMinNumThreads = 1u; // Need at least 1 thread
inline constexpr UINT kMaxNumThreads = 256u; // No consumer CPUs have more than this

// Child window style
inline constexpr DWORD dwCHILD = WS_CHILD | WS_VISIBLE;

// Minimum common controls version for certain functions, used for fallback codepaths
// See https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions
inline constexpr DWORD dwComCtl32TargetVer =
    _PACKVERSION(static_cast<DWORD>(5u), static_cast<DWORD>(82u));

#endif // PICALCWIN32_CONSTANTS_H_
