#ifndef PICALCWIN32_SYSMON_H_
#define PICALCWIN32_SYSMON_H_

#include "cpu.h"
#include "framework.h"
#include "mem.h"

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

#endif // PICALCWIN32_SYSMON_H_
