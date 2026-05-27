// System monitor controls, graphs, and monitoring state.

#include "sysmon.h"

#include "constants.h"
#include "globals.h"
#include "resource.h"
#include "strings.h"
#include "utils.h"

static HWND s_hSysmonGroup = nullptr;
static HWND s_hGraph       = nullptr;

// CPU and memory metric sub-groupboxes (children of parent/main window).
// Created before the metric controls so they are lower in Z-order and painted
// first — the standard Win32 dialog pattern for groupboxes with sibling labels.
// No WS_CLIPSIBLINGS: with it the groupbox clips out the metric controls (which
// are higher-Z siblings) and the frame becomes invisible.
static HWND s_hCpuGroup = nullptr;
static HWND s_hMemGroup = nullptr;

// CPU metric label + value statics — children of parent, siblings of sub-groupboxes.
// Order top-to-bottom: Idle, User, Kernel, Total.
static HWND s_hCpuIdleLabel   = nullptr;
static HWND s_hCpuIdleValue   = nullptr;
static HWND s_hCpuUserLabel   = nullptr;
static HWND s_hCpuUserValue   = nullptr;
static HWND s_hCpuKernelLabel = nullptr;
static HWND s_hCpuKernelValue = nullptr;
static HWND s_hCpuUsageLabel  = nullptr;
static HWND s_hCpuUsageValue  = nullptr;

// Memory metric label + value statics — children of parent, siblings of sub-groupboxes.
static HWND s_hRamLabel   = nullptr;
static HWND s_hRamValue   = nullptr;
static HWND s_hPfLabel    = nullptr;
static HWND s_hPfValue    = nullptr;
static HWND s_hVmLabel    = nullptr;
static HWND s_hVmValue    = nullptr;
static HWND s_hCacheLabel = nullptr;
static HWND s_hCacheValue = nullptr;

static const wchar_t* kGraphClassName = L"PicalcGraph";

static LRESULT CALLBACK GraphProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return TRUE;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT rc;
      GetClientRect(hWnd, &rc);
      FillRectWithColor(hdc, rc, RGB_BLACK);
      DrawEdge(hdc, &rc, EDGE_SUNKEN, BF_RECT);
      EndPaint(hWnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

static bool RegisterGraphClass(HINSTANCE hInstance) {
  WNDCLASSEXW wc   = {};
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = GraphProc;
  wc.hInstance     = hInstance;
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kGraphClassName;
  if (RegisterClassExW(&wc) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool CreateSysmonControls(HWND parent) {
  if (parent == nullptr) {
    return false;
  }
  if (!RegisterGraphClass(g_hInstance)) {
    return false;
  }

  s_hSysmonGroup = CreateWindowExW(
      0, WC_BUTTON, kSysmonGroupLabel,
      dwCHILD | BS_GROUPBOX | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SYSMON_GROUP)),
      g_hInstance, nullptr);
  if (s_hSysmonGroup == nullptr) {
    return false;
  }

  s_hGraph = CreateWindowExW(0, kGraphClassName, L"", dwCHILD, 0, 0, 0, 0, parent,
                             nullptr, g_hInstance, nullptr);
  if (s_hGraph == nullptr) {
    return false;
  }

  // Sub-groupboxes: no WS_CLIPSIBLINGS so the frame is not clipped out by the
  // higher-Z metric controls that visually sit inside it.
  s_hCpuGroup = CreateWindowExW(
      0, WC_BUTTON, kCPUGroupLabel,
      dwCHILD | BS_GROUPBOX, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CPU_GROUP)),
      g_hInstance, nullptr);
  if (s_hCpuGroup == nullptr) {
    return false;
  }

  s_hMemGroup = CreateWindowExW(
      0, WC_BUTTON, kMemGroupLabel,
      dwCHILD | BS_GROUPBOX, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_MEM_GROUP)),
      g_hInstance, nullptr);
  if (s_hMemGroup == nullptr) {
    return false;
  }

  // Metric controls are siblings of the sub-groupboxes (both children of parent).
  // They are created AFTER the sub-groupboxes so they sit higher in Z-order and
  // paint on top of the groupbox frames — the standard Win32 dialog pattern.
  // Labels use SS_RIGHT | SS_CENTERIMAGE so the colon is flush against the value.
  // Values use SS_LEFT  | SS_CENTERIMAGE and carry IDC_ IDs for later updates.
  static constexpr DWORD kLabelStyle = dwCHILD | SS_RIGHT | SS_CENTERIMAGE;
  static constexpr DWORD kValueStyle = dwCHILD | SS_LEFT  | SS_CENTERIMAGE;

  struct MetricDef {
    HWND*          label_hwnd;
    HWND*          value_hwnd;
    const wchar_t* label_text;
    UINT           value_id;
  };
  const MetricDef kMetrics[] = {
    { &s_hCpuIdleLabel,   &s_hCpuIdleValue,   kMetricCpuIdle,   IDC_CPUIDLE    },
    { &s_hCpuUserLabel,   &s_hCpuUserValue,   kMetricCpuUser,   IDC_CPUUSER    },
    { &s_hCpuKernelLabel, &s_hCpuKernelValue, kMetricCpuKernel, IDC_CPUKERNEL  },
    { &s_hCpuUsageLabel,  &s_hCpuUsageValue,  kMetricCpuUsage,  IDC_CPUTOTAL   },
    { &s_hRamLabel,       &s_hRamValue,       kMetricRam,       IDC_RAMTOTAL   },
    { &s_hPfLabel,        &s_hPfValue,        kMetricPageFile,  IDC_PFTOTAL    },
    { &s_hVmLabel,        &s_hVmValue,        kMetricVirtMem,   IDC_VMTOTAL    },
    { &s_hCacheLabel,     &s_hCacheValue,     kMetricSysCache,  IDC_CACHETOTAL },
  };
  for (const auto& m : kMetrics) {
    *(m.label_hwnd) = CreateWindowExW(0, WC_STATIC, m.label_text,
        kLabelStyle, 0, 0, 0, 0, parent, nullptr, g_hInstance, nullptr);
    if (*(m.label_hwnd) == nullptr) {
      return false;
    }
    *(m.value_hwnd) = CreateWindowExW(0, WC_STATIC, kMetricInitVal,
        kValueStyle, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(m.value_id)),
        g_hInstance, nullptr);
    if (*(m.value_hwnd) == nullptr) {
      return false;
    }
  }

  const HFONT hGuiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  const HWND kFontTargets[] = {
    s_hSysmonGroup,
    s_hCpuGroup,       s_hMemGroup,
    s_hCpuIdleLabel,   s_hCpuIdleValue,
    s_hCpuUserLabel,   s_hCpuUserValue,
    s_hCpuKernelLabel, s_hCpuKernelValue,
    s_hCpuUsageLabel,  s_hCpuUsageValue,
    s_hRamLabel,       s_hRamValue,
    s_hPfLabel,        s_hPfValue,
    s_hVmLabel,        s_hVmValue,
    s_hCacheLabel,     s_hCacheValue,
  };
  for (HWND hw : kFontTargets) {
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hGuiFont), MAKELPARAM(FALSE, 0));
  }
  return true;
}

HWND GetSysmonGroupHwnd() {
  return s_hSysmonGroup;
}

HWND GetGraphHwnd() {
  return s_hGraph;
}

HDWP LayoutSysmonMetrics(HDWP hdwp, int x, int y, int w, int h) {
  if (hdwp == nullptr) {
    return nullptr;
  }
  const int col_w = (w - kHGap) / 2;
  const int rx    = x + col_w + kHGap;

  // Sub-groupboxes in parent-window client coords.
  if (s_hCpuGroup != nullptr && hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, s_hCpuGroup, nullptr, x, y, col_w, h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (s_hMemGroup != nullptr && hdwp != nullptr) {
    hdwp = DeferWindowPos(hdwp, s_hMemGroup, nullptr, rx, y, col_w, h,
                          SWP_NOZORDER | SWP_NOACTIVATE);
  }

  // Metric controls are siblings of the sub-groupboxes; coords are in parent-window
  // client space. kGroupMargin side padding; rows are vertically centered in the
  // available inner area (frame line to inner bottom), distributing leftover height
  // equally above and below the row block.
  const int used_h  = 4 * kControlHeight;
  const int avail_h = h - kGroupMargin - kGroupInnerPad - kGroupMargin;
  const int v_off   = (avail_h > used_h) ? (avail_h - used_h) / 2 : 0;
  const int row0_y  = y + kGroupMargin + kGroupInnerPad + v_off;
  const int content_w = col_w - 2 * kGroupMargin;
  const int lbl_w     = content_w * 55 / 100;
  const int val_w     = content_w - lbl_w;
  const int lbl_x_l   = x  + kGroupMargin;
  const int lbl_x_r   = rx + kGroupMargin;
  const int stride    = kControlHeight;

  struct Entry { HWND hw; int cx, cy, cw, ch; };
  const Entry entries[] = {
    { s_hCpuIdleLabel,   lbl_x_l,        row0_y,          lbl_w, kControlHeight },
    { s_hCpuIdleValue,   lbl_x_l+lbl_w,  row0_y,          val_w, kControlHeight },
    { s_hCpuUserLabel,   lbl_x_l,        row0_y+stride,   lbl_w, kControlHeight },
    { s_hCpuUserValue,   lbl_x_l+lbl_w,  row0_y+stride,   val_w, kControlHeight },
    { s_hCpuKernelLabel, lbl_x_l,        row0_y+2*stride, lbl_w, kControlHeight },
    { s_hCpuKernelValue, lbl_x_l+lbl_w,  row0_y+2*stride, val_w, kControlHeight },
    { s_hCpuUsageLabel,  lbl_x_l,        row0_y+3*stride, lbl_w, kControlHeight },
    { s_hCpuUsageValue,  lbl_x_l+lbl_w,  row0_y+3*stride, val_w, kControlHeight },
    { s_hRamLabel,       lbl_x_r,        row0_y,          lbl_w, kControlHeight },
    { s_hRamValue,       lbl_x_r+lbl_w,  row0_y,          val_w, kControlHeight },
    { s_hPfLabel,        lbl_x_r,        row0_y+stride,   lbl_w, kControlHeight },
    { s_hPfValue,        lbl_x_r+lbl_w,  row0_y+stride,   val_w, kControlHeight },
    { s_hVmLabel,        lbl_x_r,        row0_y+2*stride, lbl_w, kControlHeight },
    { s_hVmValue,        lbl_x_r+lbl_w,  row0_y+2*stride, val_w, kControlHeight },
    { s_hCacheLabel,     lbl_x_r,        row0_y+3*stride, lbl_w, kControlHeight },
    { s_hCacheValue,     lbl_x_r+lbl_w,  row0_y+3*stride, val_w, kControlHeight },
  };
  for (const auto& e : entries) {
    if (hdwp != nullptr && e.hw != nullptr) {
      hdwp = DeferWindowPos(hdwp, e.hw, nullptr, e.cx, e.cy, e.cw, e.ch,
                            SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  return hdwp;
}
