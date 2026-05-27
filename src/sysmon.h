#ifndef PICALCWIN32_SYSMON_H_
#define PICALCWIN32_SYSMON_H_

#include "constants.h"
#include "cpu.h"
#include "framework.h"
#include "mem.h"

// Graph grid line color. Change at compile time to alter all grid lines.
inline constexpr COLORREF kGraphGridColor  = RGB_BLUE;
inline constexpr COLORREF kCpuLineColor    = RGB_GREEN;
inline constexpr COLORREF kCpuFillColor    = RGB_DARKGREEN;
inline constexpr COLORREF kKernelLineColor = RGB_RED;
inline constexpr COLORREF kKernelFillColor = RGB_DARKRED;

// Pixels the graph scrolls left per sysmon timer tick.
inline constexpr unsigned int kGraphScrollStep = 5u;

inline constexpr ULONGLONG kGB = 1024ULL * 1024ULL * 1024ULL;
inline constexpr ULONGLONG kMB = 1024ULL * 1024ULL;
inline constexpr ULONGLONG kKB = 1024ULL;

// Creates the sysmon groupbox and its child controls as children of `parent`.
// Must be called once from CreateChildControls.
bool CreateSysmonControls(HWND parent);

// Returns the sysmon groupbox HWND for use in LayoutChildren, or nullptr
// if CreateSysmonControls has not been called yet.
HWND GetSysmonGroupHwnd();

// Returns the CPU graph HWND for use in LayoutChildren, or nullptr if not yet created.
HWND GetGraphHwnd();

// Adds DeferWindowPos entries for the two metric sub-groupboxes and all their
// label/value statics.
// x, y: HWND origin of the sub-groupboxes in parent client coords.
// w: total width (split into two equal columns with kHGap between them).
// h: sub-groupbox HWND height.
// Returns the updated hdwp (may differ if the internal buffer was reallocated).
HDWP LayoutSysmonMetrics(HDWP hdwp, int x, int y, int w, int h);

// Starts (or restarts) the system monitor timer on hWnd with the given
// interval in milliseconds. Uses WM_MONTIMER as the nIDEvent so WM_TIMER
// handlers can identify sysmon ticks by wParam == WM_MONTIMER.
// Also takes the first CPU/mem snapshot so the first tick has a valid delta.
bool StartSysmon(HWND hWnd, UINT interval_ms);

// Stops the sysmon timer. Safe to call even if the timer was never started.
void StopSysmon(HWND hWnd);

// Must be called from the WM_TIMER handler when wParam == WM_MONTIMER.
// Queries CPU/mem stats and updates all metric value label controls.
void OnSysmonTick(HWND hWnd);

// Read-only access to the most recent sampled stats (zeroed until first tick).
const CpuStats& GetLatestCpuStats();
const MemStats& GetLatestMemStats();

#endif // PICALCWIN32_SYSMON_H_
