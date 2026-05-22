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

// Child window style
inline constexpr DWORD dwCHILD = WS_CHILD | WS_VISIBLE;

// Minimum common controls version for certain functions, used for fallback codepaths
// See https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions
inline constexpr DWORD dwComCtl32TargetVer =
    _PACKVERSION(static_cast<DWORD>(5u), static_cast<DWORD>(82u));

#endif // PICALCWIN32_CONSTANTS_H_
