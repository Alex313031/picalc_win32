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
// first - the standard Win32 dialog pattern for groupboxes with sibling labels.
// No WS_CLIPSIBLINGS: with it the groupbox clips out the metric controls (which
// are higher-Z siblings) and the frame becomes invisible.
static HWND s_hCpuGroup = nullptr;
static HWND s_hMemGroup = nullptr;

// CPU metric label + value statics - children of parent, siblings of sub-groupboxes.
// Order top-to-bottom: Idle, User, Kernel, Total.
static HWND s_hCpuIdleLabel   = nullptr;
static HWND s_hCpuIdleValue   = nullptr;
static HWND s_hCpuUserLabel   = nullptr;
static HWND s_hCpuUserValue   = nullptr;
static HWND s_hCpuKernelLabel = nullptr;
static HWND s_hCpuKernelValue = nullptr;
static HWND s_hCpuUsageLabel  = nullptr;
static HWND s_hCpuUsageValue  = nullptr;

// Memory metric label + value statics - children of parent, siblings of sub-groupboxes.
static HWND s_hRamLabel   = nullptr;
static HWND s_hRamValue   = nullptr;
static HWND s_hPfLabel    = nullptr;
static HWND s_hPfValue    = nullptr;
static HWND s_hVmLabel    = nullptr;
static HWND s_hVmValue    = nullptr;
static HWND s_hCacheLabel = nullptr;
static HWND s_hCacheValue = nullptr;

static const wchar_t* kGraphClassName = L"PicalcGraph";

// Cumulative pixel scroll distance; advances by kGraphScrollStep each tick.
// Read in GraphProc to compute the vertical-line phase offset.
static ULONGLONG s_graph_scroll_x = 0;

// ---------------------------------------------------------------------------
// Graph sample ring buffer: zero-initialised so the graph shows a flat 0%
// line before the first real tick and ramps up naturally as data arrives.
// ---------------------------------------------------------------------------
static constexpr int kGraphMaxSamples = 2048;

struct GraphSample {
  float total_pct;
  float kernel_pct;
};

static GraphSample s_graph_samples[kGraphMaxSamples] = {};
static int s_graph_head                              = 0; // index of next write slot (circular)

// ---------------------------------------------------------------------------
// Cached GDI object: created once on first paint, freed in WM_DESTROY.
// ---------------------------------------------------------------------------
static HPEN s_hGridPen           = nullptr;
static HPEN s_hTotalLinePen      = nullptr;
static HPEN s_hKernelLinePen     = nullptr;
static HBRUSH s_hTotalFillBrush  = nullptr;
static HBRUSH s_hKernelFillBrush = nullptr;

// ---------------------------------------------------------------------------
// Double-buffer backbuffer: recreated on WM_SIZE, blitted to screen in WM_PAINT.
// ---------------------------------------------------------------------------
static HDC s_hMemDC         = nullptr;
static HBITMAP s_hMemBmp    = nullptr;
static HBITMAP s_hOldMemBmp = nullptr; // default 1x1 bmp, restored before DeleteDC
static int s_mem_cx         = 0;
static int s_mem_cy         = 0;

// Renders the graph (background, edge, grid, fills + lines) into `dc` using
// `rc` as the target rect. Lazy-inits cached pens/brushes on first call;
// restores pen/brush selection state before returning.
static void PaintGraph(HDC dc, const RECT& rc) {
  FillRectWithColor(dc, rc, RGB_BLACK);
  RECT edge_rc = rc;
  DrawEdge(dc, &edge_rc, EDGE_SUNKEN, BF_RECT);

  // Inner rect: everything (grid, fills, lines) is mapped onto this so the
  // graph stays strictly inside the 2px sunken bevel, matching Task Manager's
  // graph layout. The clip is a safety net for the polygon fill, whose
  // bottom-left baseline corner can sit just to the left of inner.left so the
  // oldest visible sample connects smoothly to the baseline.
  const RECT inner  = {rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2};
  const int inner_w = inner.right - inner.left;
  const int inner_h = inner.bottom - inner.top;
  if (inner_w <= 0 || inner_h <= 0) {
    return;
  }
  const int saved_dc = SaveDC(dc);
  IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);

  // Lazy-init all cached GDI objects on first paint.
  if (s_hGridPen == nullptr) {
    s_hGridPen = CreatePen(PS_SOLID, 1, kGraphGridColor);
  }
  if (s_hTotalLinePen == nullptr) {
    s_hTotalLinePen = CreatePen(PS_SOLID, 1, kCpuLineColor);
  }
  if (s_hKernelLinePen == nullptr) {
    s_hKernelLinePen = CreatePen(PS_SOLID, 1, kKernelLineColor);
  }
  if (s_hTotalFillBrush == nullptr) {
    s_hTotalFillBrush = CreateSolidBrush(kCpuFillColor);
  }
  if (s_hKernelFillBrush == nullptr) {
    s_hKernelFillBrush = CreateSolidBrush(kKernelFillColor);
  }

  HPEN hOldPen     = static_cast<HPEN>(SelectObject(dc, s_hGridPen));
  HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));

  // --- Pass 0: grid ---
  for (int i = 1; i <= 9; ++i) {
    const int y = inner.top + i * inner_h / 10;
    MoveToEx(dc, inner.left, y, nullptr);
    LineTo(dc, inner.right, y);
  }
  const int col_w = inner_w / 10;
  if (col_w > 0) {
    const int phase = static_cast<int>(s_graph_scroll_x % static_cast<ULONGLONG>(col_w));
    for (int x = inner.left + col_w - phase; x < inner.right; x += col_w) {
      MoveToEx(dc, x, inner.top, nullptr);
      LineTo(dc, x, inner.bottom);
    }
  }

  // --- Passes 1-4: filled CPU lines (painter's algorithm) ---
  {
    // How many samples fit across the inner width; capped to buffer size.
    const int num_visible = inner_w / kGraphScrollStep + 2;
    const int actual_pts  = (num_visible < kGraphMaxSamples) ? num_visible : kGraphMaxSamples;
    const LONG x_newest   = static_cast<LONG>(inner.right) - 1L;
    const LONG y_base     = static_cast<LONG>(inner.bottom); // one row below the inner area

    // pct -> y: 0% = inner.bottom-1 (bottom row inside bevel),
    //         100% = inner.top    (top row inside bevel).
    // Returns LONG so POINT brace-initializers get {LONG, LONG} with no
    // narrowing conversion.
    auto pct_to_y = [&](float pct) -> LONG {
      if (pct < 0.0f) {
        pct = 0.0f;
      }
      if (pct > 100.0f) {
        pct = 100.0f;
      }
      return static_cast<LONG>(inner.bottom) - 1L -
             static_cast<LONG>(pct * static_cast<float>(inner_h - 1) / 100.0f + 0.5f);
    };

    // Build polygon point arrays (left-baseline, data pts, right-baseline).
    // poly[0]             = left baseline corner
    // poly[1..actual_pts] = sample data, left (oldest) to right (newest)
    // poly[actual_pts+1]  = right baseline corner
    // Polyline for the line uses poly+1 with count actual_pts.
    POINT total_poly[kGraphMaxSamples + 4];
    POINT kernel_poly[kGraphMaxSamples + 4];

    total_poly[0] = {
        x_newest - static_cast<LONG>(actual_pts - 1) * static_cast<LONG>(kGraphScrollStep), y_base};
    kernel_poly[0] = total_poly[0];

    for (int i = 0; i < actual_pts; ++i) {
      // i=0 -> oldest visible; i=actual_pts-1 -> newest.
      const int sample_age = actual_pts - 1 - i;
      const int idx = ((s_graph_head - 1 - sample_age) % kGraphMaxSamples + kGraphMaxSamples) %
                      kGraphMaxSamples;
      const LONG x = x_newest - static_cast<LONG>(sample_age) * static_cast<LONG>(kGraphScrollStep);
      total_poly[i + 1]  = {x, pct_to_y(s_graph_samples[idx].total_pct)};
      kernel_poly[i + 1] = {x, pct_to_y(s_graph_samples[idx].kernel_pct)};
    }

    total_poly[actual_pts + 1]  = {x_newest, y_base};
    kernel_poly[actual_pts + 1] = {x_newest, y_base};
    const int poly_count        = actual_pts + 2;

    // Pass 1: total fill (null pen so Polygon draws no outline).
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, s_hTotalFillBrush);
    Polygon(dc, total_poly, poly_count);

    // Pass 2: total line - before any kernel painting so kernel is always on top.
    SelectObject(dc, s_hTotalLinePen);
    Polyline(dc, total_poly + 1, actual_pts);

    // Pass 3: kernel fill (on top of total fill and total line).
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, s_hKernelFillBrush);
    Polygon(dc, kernel_poly, poly_count);

    // Pass 4: kernel line (topmost).
    SelectObject(dc, s_hKernelLinePen);
    Polyline(dc, kernel_poly + 1, actual_pts);
  }

  SelectObject(dc, hOldBrush);
  SelectObject(dc, hOldPen);
  RestoreDC(dc, saved_dc);
}

static LRESULT CALLBACK GraphProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return TRUE;

    case WM_SIZE: {
      // Drop the backbuffer on resize; WM_PAINT recreates it at the new size.
      if (s_hMemBmp != nullptr) {
        SelectObject(s_hMemDC, s_hOldMemBmp);
        DeleteObject(s_hMemBmp);
        s_hMemBmp    = nullptr;
        s_hOldMemBmp = nullptr;
      }
      if (s_hMemDC != nullptr) {
        DeleteDC(s_hMemDC);
        s_hMemDC = nullptr;
      }
      s_mem_cx = s_mem_cy = 0;
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT rc;
      GetClientRect(hWnd, &rc);
      const int cx = rc.right;
      const int cy = rc.bottom;

      // Ensure the backbuffer exists and matches the current window size.
      if (cx > 0 && cy > 0) {
        if (s_hMemDC == nullptr) {
          s_hMemDC = CreateCompatibleDC(hdc);
        }
        if (s_hMemBmp == nullptr || s_mem_cx != cx || s_mem_cy != cy) {
          if (s_hMemBmp != nullptr) {
            SelectObject(s_hMemDC, s_hOldMemBmp);
            DeleteObject(s_hMemBmp);
          }
          s_hMemBmp    = CreateCompatibleBitmap(hdc, cx, cy);
          s_hOldMemBmp = static_cast<HBITMAP>(SelectObject(s_hMemDC, s_hMemBmp));
          s_mem_cx     = cx;
          s_mem_cy     = cy;
        }
      }

      // All drawing goes into the backbuffer DC; falls back to hdc if creation failed.
      HDC dc = (s_hMemDC != nullptr) ? s_hMemDC : hdc;
      PaintGraph(dc, rc);

      // Blit the fully-composed backbuffer to screen in one atomic operation.
      if (s_hMemDC != nullptr && cx > 0 && cy > 0) {
        BitBlt(hdc, 0, 0, cx, cy, s_hMemDC, 0, 0, SRCCOPY);
      }

      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_DESTROY: {
      if (s_hMemBmp != nullptr) {
        SelectObject(s_hMemDC, s_hOldMemBmp);
        DeleteObject(s_hMemBmp);
        s_hMemBmp    = nullptr;
        s_hOldMemBmp = nullptr;
      }
      if (s_hMemDC != nullptr) {
        DeleteDC(s_hMemDC);
        s_hMemDC = nullptr;
      }
      if (s_hGridPen != nullptr) {
        DeleteObject(s_hGridPen);
        s_hGridPen = nullptr;
      }
      if (s_hTotalLinePen != nullptr) {
        DeleteObject(s_hTotalLinePen);
        s_hTotalLinePen = nullptr;
      }
      if (s_hKernelLinePen != nullptr) {
        DeleteObject(s_hKernelLinePen);
        s_hKernelLinePen = nullptr;
      }
      if (s_hTotalFillBrush != nullptr) {
        DeleteObject(s_hTotalFillBrush);
        s_hTotalFillBrush = nullptr;
      }
      if (s_hKernelFillBrush != nullptr) {
        DeleteObject(s_hKernelFillBrush);
        s_hKernelFillBrush = nullptr;
      }
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
      0, WC_BUTTON, kSysmonGroupLabel, dwCHILD | BS_GROUPBOX | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SYSMON_GROUP)), g_hInstance, nullptr);
  if (s_hSysmonGroup == nullptr) {
    return false;
  }

  s_hGraph = CreateWindowExW(0, kGraphClassName, L"", dwCHILD, 0, 0, 0, 0, parent, nullptr,
                             g_hInstance, nullptr);
  if (s_hGraph == nullptr) {
    return false;
  }

  // Sub-groupboxes: no WS_CLIPSIBLINGS so the frame is not clipped out by the
  // higher-Z metric controls that visually sit inside it.
  s_hCpuGroup = CreateWindowExW(
      0, WC_BUTTON, kCPUGroupLabel, dwCHILD | BS_GROUPBOX, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_CPU_GROUP)), g_hInstance, nullptr);
  if (s_hCpuGroup == nullptr) {
    return false;
  }

  s_hMemGroup = CreateWindowExW(
      0, WC_BUTTON, kMemGroupLabel, dwCHILD | BS_GROUPBOX, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_MEM_GROUP)), g_hInstance, nullptr);
  if (s_hMemGroup == nullptr) {
    return false;
  }

  // Metric controls are siblings of the sub-groupboxes (both children of parent).
  // They are created AFTER the sub-groupboxes so they sit higher in Z-order and
  // paint on top of the groupbox frames - the standard Win32 dialog pattern.
  // SS_NOTIFY makes the static return HTCLIENT from WM_NCHITTEST instead of
  // the default HTTRANSPARENT, so WM_MOUSEMOVE actually reaches the control -
  // required for TTF_SUBCLASS-based hover tooltips to fire.
  static constexpr DWORD kLabelStyle = dwCHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY;
  static constexpr DWORD kValueStyle = dwCHILD | SS_RIGHT | SS_CENTERIMAGE | SS_NOTIFY;

  struct MetricDef {
    HWND* label_hwnd;
    HWND* value_hwnd;
    const wchar_t* label_text;
    UINT value_id;
  };
  const MetricDef kMetrics[] = {
      {&s_hCpuIdleLabel, &s_hCpuIdleValue, kMetricCpuIdle, IDC_CPUIDLE},
      {&s_hCpuUserLabel, &s_hCpuUserValue, kMetricCpuUser, IDC_CPUUSER},
      {&s_hCpuKernelLabel, &s_hCpuKernelValue, kMetricCpuKernel, IDC_CPUKERNEL},
      {&s_hCpuUsageLabel, &s_hCpuUsageValue, kMetricCpuUsage, IDC_CPUTOTAL},
      {&s_hRamLabel, &s_hRamValue, kMetricRam, IDC_RAMTOTAL},
      {&s_hPfLabel, &s_hPfValue, kMetricPageFile, IDC_PFTOTAL},
      {&s_hVmLabel, &s_hVmValue, kMetricVirtMem, IDC_VMTOTAL},
      {&s_hCacheLabel, &s_hCacheValue, kMetricSysCache, IDC_CACHETOTAL},
  };
  for (const auto& m : kMetrics) {
    *(m.label_hwnd) = CreateWindowExW(0, WC_STATIC, m.label_text, kLabelStyle, 0, 0, 0, 0, parent,
                                      nullptr, g_hInstance, nullptr);
    if (*(m.label_hwnd) == nullptr) {
      return false;
    }
    *(m.value_hwnd) = CreateWindowExW(0, WC_STATIC, kMetricInitVal, kValueStyle, 0, 0, 0, 0, parent,
                                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(m.value_id)),
                                      g_hInstance, nullptr);
    if (*(m.value_hwnd) == nullptr) {
      return false;
    }
  }

  HFONT hMonFont = GetFont(0);
  if (hMonFont == nullptr) {
    hMonFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
  const HWND kFontTargets[] = {
      s_hSysmonGroup,   s_hCpuGroup,     s_hMemGroup,       s_hCpuIdleLabel,   s_hCpuIdleValue,
      s_hCpuUserLabel,  s_hCpuUserValue, s_hCpuKernelLabel, s_hCpuKernelValue, s_hCpuUsageLabel,
      s_hCpuUsageValue, s_hRamLabel,     s_hRamValue,       s_hPfLabel,        s_hPfValue,
      s_hVmLabel,       s_hVmValue,      s_hCacheLabel,     s_hCacheValue,
  };
  for (HWND hw : kFontTargets) {
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hMonFont), MAKELPARAM(FALSE, 0));
  }

  // Hover tooltips: each metric row (label + value) shares one string so a
  // hover anywhere on the row surfaces the same hint. The graph gets its own.
  struct TooltipBinding {
    HWND hCtrl;
    const wchar_t* text;
  };
  const TooltipBinding kTooltipBindings[] = {
      {s_hGraph, kTooltipGraph},
      {s_hCpuIdleLabel, kTooltipCpuIdle},
      {s_hCpuIdleValue, kTooltipCpuIdle},
      {s_hCpuUserLabel, kTooltipCpuUser},
      {s_hCpuUserValue, kTooltipCpuUser},
      {s_hCpuKernelLabel, kTooltipCpuKernel},
      {s_hCpuKernelValue, kTooltipCpuKernel},
      {s_hCpuUsageLabel, kTooltipCpuTotal},
      {s_hCpuUsageValue, kTooltipCpuTotal},
      {s_hRamLabel, kTooltipRam},
      {s_hRamValue, kTooltipRam},
      {s_hPfLabel, kTooltipPageFile},
      {s_hPfValue, kTooltipPageFile},
      {s_hVmLabel, kTooltipVirtMem},
      {s_hVmValue, kTooltipVirtMem},
      {s_hCacheLabel, kTooltipSysCache},
      {s_hCacheValue, kTooltipSysCache},
  };
  for (const auto& tb : kTooltipBindings) {
    AddTooltip(parent, tb.hCtrl, g_hInstance, tb.text);
  }
  return true;
}

HWND GetSysmonGroupHwnd() {
  return s_hSysmonGroup;
}

HWND GetGraphHwnd() {
  return s_hGraph;
}

// =========================================================================
// Monitoring state
// =========================================================================

static CpuStats s_cpu_stats = {};
static MemStats s_mem_stats = {};

// =========================================================================
// =========================================================================
// Timer + tick
// =========================================================================

bool StartSysmon(HWND hWnd, UINT interval_ms) {
  if (hWnd == nullptr) {
    return false;
  }
  // Seed the first CPU snapshot so the next tick has a delta immediately.
  CpuStats dummy = {};
  UpdateCpuStats(&dummy);
  KillTimer(hWnd, IDT_MONTIMER);
  return SetTimer(hWnd, IDT_MONTIMER, interval_ms, nullptr) != 0;
}

void StopSysmon(HWND hWnd) {
  if (hWnd != nullptr) {
    KillTimer(hWnd, IDT_MONTIMER);
  }
}

void OnSysmonTick(HWND hWnd) {
  // Advance the graph scroll and trigger a repaint.
  s_graph_scroll_x += static_cast<ULONGLONG>(kGraphScrollStep);
  if (s_hGraph != nullptr) {
    InvalidateRect(s_hGraph, nullptr, FALSE);
  }

  // Update CPU stats.
  CpuStats cpu = {};
  if (UpdateCpuStats(&cpu)) {
    s_cpu_stats = cpu;
    wchar_t buf[32];
    auto set_val = [&](UINT id, float pct) {
      swprintf(buf, ARRAYSIZE(buf), L"%.2f%%", static_cast<double>(pct));
      HWND hw = GetDlgItem(hWnd, static_cast<int>(id));
      if (hw == nullptr) {
        return;
      }
      wchar_t cur[32] = {};
      GetWindowTextW(hw, cur, ARRAYSIZE(cur));
      if (wcscmp(cur, buf) != 0) {
        SetWindowTextW(hw, buf);
      }
    };
    set_val(IDC_CPUIDLE, s_cpu_stats.idle_pct);
    set_val(IDC_CPUUSER, s_cpu_stats.user_pct);
    set_val(IDC_CPUKERNEL, s_cpu_stats.kernel_pct);
    set_val(IDC_CPUTOTAL, s_cpu_stats.total_pct);
  }

  // Record graph sample. Uses current s_cpu_stats: zeroed until the first
  // successful UpdateCpuStats call, which gives the "ramps up from 0" effect.
  s_graph_samples[s_graph_head] = {s_cpu_stats.total_pct, s_cpu_stats.kernel_pct};
  s_graph_head                  = (s_graph_head + 1) % kGraphMaxSamples;

  // Update memory stats.
  MemStats mem = {};
  if (UpdateMemStats(&mem)) {
    s_mem_stats = mem;
    wchar_t buf[48];
    auto set_str = [&](UINT id, const wchar_t* str) {
      HWND hw = GetDlgItem(hWnd, static_cast<int>(id));
      if (hw == nullptr) {
        return;
      }
      wchar_t cur[48] = {};
      GetWindowTextW(hw, cur, ARRAYSIZE(cur));
      if (wcscmp(cur, str) != 0) {
        SetWindowTextW(hw, str);
      }
    };
    FormatBytesPair(buf, ARRAYSIZE(buf), s_mem_stats.ram_used, s_mem_stats.ram_total);
    set_str(IDC_RAMTOTAL, buf);
    if (s_mem_stats.pf_limit > 0ULL) {
      FormatBytesPair(buf, ARRAYSIZE(buf), s_mem_stats.pf_used, s_mem_stats.pf_limit);
    } else {
      swprintf(buf, ARRAYSIZE(buf), L"NaN");
    }
    set_str(IDC_PFTOTAL, buf);
    FormatBytesPair(buf, ARRAYSIZE(buf), s_mem_stats.vm_used, s_mem_stats.vm_limit);
    set_str(IDC_VMTOTAL, buf);
    // (SIZE_T)-1 is the sentinel Windows uses for "system-managed / no fixed
    // cap". When there is a finite cap, show "used / cap"; when uncapped, fall
    // back to "used / vm_limit" so the row keeps the same "x / total" shape as
    // every other metric.
    static const ULONGLONG kCacheLimitUnlimited = static_cast<ULONGLONG>(static_cast<SIZE_T>(-1));
    if (s_mem_stats.cache_bytes == 0ULL) {
      swprintf(buf, ARRAYSIZE(buf), L"NaN");
    } else if (s_mem_stats.cache_limit != kCacheLimitUnlimited && s_mem_stats.cache_limit > 0ULL) {
      FormatBytesPair(buf, ARRAYSIZE(buf), static_cast<ULONGLONG>(s_mem_stats.cache_bytes),
                      s_mem_stats.cache_limit);
    } else {
      FormatBytesPair(buf, ARRAYSIZE(buf), static_cast<ULONGLONG>(s_mem_stats.cache_bytes),
                      s_mem_stats.vm_limit);
    }
    set_str(IDC_CACHETOTAL, buf);
  }
}

const CpuStats& GetLatestCpuStats() {
  return s_cpu_stats;
}

const MemStats& GetLatestMemStats() {
  return s_mem_stats;
}

// =========================================================================

HDWP LayoutSysmonMetrics(HDWP hdwp, int x, int y, int w, int h) {
  if (hdwp == nullptr) {
    return nullptr;
  }
  const int col_w = (w - kHGap) / 2;
  const int rx    = x + col_w + kHGap;

  // Sub-groupboxes in parent-window client coords.
  if (s_hCpuGroup != nullptr && hdwp != nullptr) {
    hdwp =
        DeferWindowPos(hdwp, s_hCpuGroup, nullptr, x, y, col_w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (s_hMemGroup != nullptr && hdwp != nullptr) {
    hdwp =
        DeferWindowPos(hdwp, s_hMemGroup, nullptr, rx, y, col_w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  }

  // Metric controls are siblings of the sub-groupboxes; coords are in parent-window
  // client space. kGroupMargin side padding; rows are vertically centered in the
  // available inner area (frame line to inner bottom), distributing leftover height
  // equally above and below the row block.
  // Pack rows tighter than kControlHeight (the per-row height). Labels have
  // built-in vertical padding, so a sub-height stride just trims dead space.
  // Sides + bottom use kGroupInnerPad (10) so that left-aligned label text
  // and right-aligned value text aren't clipped at the groupbox frame.
  const int stride    = kControlHeight - 4;
  const int used_h    = 3 * stride + kControlHeight;
  const int avail_h   = h - kGroupMargin - kGroupInnerPad - kGroupInnerPad;
  const int v_off     = (avail_h > used_h) ? (avail_h - used_h) / 2 : 0;
  const int row0_y    = y + kGroupMargin + kGroupInnerPad + v_off;
  const int content_w = col_w - 2 * kGroupInnerPad;
  const int lbl_w     = content_w * 55 / kLabelWidth;
  const int val_w     = content_w - lbl_w;
  const int lbl_x_l   = x + kGroupInnerPad;
  const int lbl_x_r   = rx + kGroupInnerPad;

  struct Entry {
    HWND hw;
    int cx, cy, cw, ch;
  };
  const Entry entries[] = {
      {s_hCpuIdleLabel, lbl_x_l, row0_y, lbl_w, kControlHeight},
      {s_hCpuIdleValue, lbl_x_l + lbl_w, row0_y, val_w, kControlHeight},
      {s_hCpuUserLabel, lbl_x_l, row0_y + stride, lbl_w, kControlHeight},
      {s_hCpuUserValue, lbl_x_l + lbl_w, row0_y + stride, val_w, kControlHeight},
      {s_hCpuKernelLabel, lbl_x_l, row0_y + 2 * stride, lbl_w, kControlHeight},
      {s_hCpuKernelValue, lbl_x_l + lbl_w, row0_y + 2 * stride, val_w, kControlHeight},
      {s_hCpuUsageLabel, lbl_x_l, row0_y + 3 * stride, lbl_w, kControlHeight},
      {s_hCpuUsageValue, lbl_x_l + lbl_w, row0_y + 3 * stride, val_w, kControlHeight},
      {s_hRamLabel, lbl_x_r, row0_y, lbl_w, kControlHeight},
      {s_hRamValue, lbl_x_r + lbl_w, row0_y, val_w, kControlHeight},
      {s_hPfLabel, lbl_x_r, row0_y + stride, lbl_w, kControlHeight},
      {s_hPfValue, lbl_x_r + lbl_w, row0_y + stride, val_w, kControlHeight},
      {s_hCacheLabel, lbl_x_r, row0_y + 2 * stride, lbl_w, kControlHeight},
      {s_hCacheValue, lbl_x_r + lbl_w, row0_y + 2 * stride, val_w, kControlHeight},
      {s_hVmLabel, lbl_x_r, row0_y + 3 * stride, lbl_w, kControlHeight},
      {s_hVmValue, lbl_x_r + lbl_w, row0_y + 3 * stride, val_w, kControlHeight},
  };
  for (const auto& e : entries) {
    if (hdwp != nullptr && e.hw != nullptr) {
      hdwp = DeferWindowPos(hdwp, e.hw, nullptr, e.cx, e.cy, e.cw, e.ch,
                            SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  return hdwp;
}
