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

// Cumulative pixel scroll distance; advances by kGraphScrollStep each tick.
// Read in GraphProc to compute the vertical-line phase offset.
static ULONGLONG s_graph_scroll_x = 0;

// ---------------------------------------------------------------------------
// Graph sample ring buffer — zero-initialised so the graph shows a flat 0%
// line before the first real tick and ramps up naturally as data arrives.
// ---------------------------------------------------------------------------
static constexpr int kGraphMaxSamples = 2048;

struct GraphSample {
  float total_pct;
  float kernel_pct;
};

static GraphSample s_graph_samples[kGraphMaxSamples] = {};
static int s_graph_head = 0; // index of next write slot (circular)

// ---------------------------------------------------------------------------
// Cached GDI objects — created once on first paint, process-lifetime.
// ---------------------------------------------------------------------------
static HPEN   s_hGridPen         = nullptr;
static HPEN   s_hTotalLinePen    = nullptr;
static HPEN   s_hKernelLinePen   = nullptr;
static HBRUSH s_hTotalFillBrush  = nullptr;
static HBRUSH s_hKernelFillBrush = nullptr;

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

      // Lazy-init all cached GDI objects on first paint.
      if (s_hGridPen        == nullptr) s_hGridPen        = CreatePen(PS_SOLID, 1, kGraphGridColor);
      if (s_hTotalLinePen   == nullptr) s_hTotalLinePen   = CreatePen(PS_SOLID, 1, kCpuLineColor);
      if (s_hKernelLinePen  == nullptr) s_hKernelLinePen  = CreatePen(PS_SOLID, 1, kKernelLineColor);
      if (s_hTotalFillBrush  == nullptr) s_hTotalFillBrush  = CreateSolidBrush(kCpuFillColor);
      if (s_hKernelFillBrush == nullptr) s_hKernelFillBrush = CreateSolidBrush(kKernelFillColor);

      HPEN   hOldPen   = static_cast<HPEN>  (SelectObject(hdc, s_hGridPen));
      HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));

      // --- Pass 0: grid ---
      for (int i = 1; i <= 9; ++i) {
        const int y = i * rc.bottom / 10;
        MoveToEx(hdc, rc.left, y, nullptr);
        LineTo(hdc, rc.right, y);
      }
      if (rc.right > 0) {
        const int col_w = rc.right / 10;
        if (col_w > 0) {
          const int phase = static_cast<int>(
              s_graph_scroll_x % static_cast<ULONGLONG>(col_w));
          for (int x = col_w - phase; x < rc.right; x += col_w) {
            MoveToEx(hdc, x, rc.top, nullptr);
            LineTo(hdc, x, rc.bottom);
          }
        }
      }

      // --- Passes 1–4: filled CPU lines (painter's algorithm) ---
      if (rc.right > 0 && rc.bottom > 0) {
        // How many samples fit across the width; capped to buffer size.
        const int  num_visible = rc.right / kGraphScrollStep + 2;
        const int  actual_pts  = (num_visible < kGraphMaxSamples) ? num_visible : kGraphMaxSamples;
        const LONG x_newest    = static_cast<LONG>(rc.right) - 1L;
        const LONG y_base      = static_cast<LONG>(rc.bottom); // one row below visible

        // pct → y: 0% = rc.bottom-1 (bottom), 100% = 0 (top). Returns LONG so
        // POINT brace-initializers get {LONG, LONG} with no narrowing conversion.
        auto pct_to_y = [&](float pct) -> LONG {
          if (pct < 0.0f)   pct = 0.0f;
          if (pct > 100.0f) pct = 100.0f;
          return static_cast<LONG>(rc.bottom) - 1L
               - static_cast<LONG>(pct * static_cast<float>(rc.bottom - 1) / 100.0f + 0.5f);
        };

        // Build polygon point arrays (left-baseline, data pts, right-baseline).
        // poly[0]             = left baseline corner
        // poly[1..actual_pts] = sample data, left (oldest) to right (newest)
        // poly[actual_pts+1]  = right baseline corner
        // Polyline for the line uses poly+1 with count actual_pts.
        POINT total_poly [kGraphMaxSamples + 4];
        POINT kernel_poly[kGraphMaxSamples + 4];

        total_poly [0] = { x_newest - static_cast<LONG>(actual_pts - 1) * static_cast<LONG>(kGraphScrollStep), y_base };
        kernel_poly[0] = total_poly[0];

        for (int i = 0; i < actual_pts; ++i) {
          // i=0 → oldest visible; i=actual_pts-1 → newest.
          const int  sample_age = actual_pts - 1 - i;
          const int  idx = ((s_graph_head - 1 - sample_age) % kGraphMaxSamples
                           + kGraphMaxSamples) % kGraphMaxSamples;
          const LONG x   = x_newest - static_cast<LONG>(sample_age) * static_cast<LONG>(kGraphScrollStep);
          total_poly [i + 1] = { x, pct_to_y(s_graph_samples[idx].total_pct)  };
          kernel_poly[i + 1] = { x, pct_to_y(s_graph_samples[idx].kernel_pct) };
        }

        total_poly [actual_pts + 1] = { x_newest, y_base };
        kernel_poly[actual_pts + 1] = { x_newest, y_base };
        const int poly_count = actual_pts + 2;

        // Pass 1: total fill (null pen so Polygon draws no outline).
        SelectObject(hdc, GetStockObject(NULL_PEN));
        SelectObject(hdc, s_hTotalFillBrush);
        Polygon(hdc, total_poly, poly_count);

        // Pass 2: total line — before any kernel painting so kernel is always on top.
        SelectObject(hdc, s_hTotalLinePen);
        Polyline(hdc, total_poly + 1, actual_pts);

        // Pass 3: kernel fill (on top of total fill and total line).
        SelectObject(hdc, GetStockObject(NULL_PEN));
        SelectObject(hdc, s_hKernelFillBrush);
        Polygon(hdc, kernel_poly, poly_count);

        // Pass 4: kernel line (topmost).
        SelectObject(hdc, s_hKernelLinePen);
        Polyline(hdc, kernel_poly + 1, actual_pts);
      }

      SelectObject(hdc, hOldBrush);
      SelectObject(hdc, hOldPen);
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

  HFONT hMonFont = GetFont(0);
  if (hMonFont == nullptr) {
    hMonFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
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
    SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hMonFont), MAKELPARAM(FALSE, 0));
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
// Value formatting helpers (local to this translation unit)
// =========================================================================

// Formats a byte count into the most readable unit: GB, MB, or KB.
static void FormatBytes(wchar_t* buf, size_t cnt, ULONGLONG bytes) {
  if (bytes >= kGB) {
    swprintf(buf, cnt, L"%.2f GB", static_cast<double>(bytes) / static_cast<double>(kGB));
  } else if (bytes >= kMB) {
    swprintf(buf, cnt, L"%.2f MB", static_cast<double>(bytes) / static_cast<double>(kMB));
  } else {
    swprintf(buf, cnt, L"%I64u KB", bytes / 1024ULL);
  }
}

// Formats a used/limit pair with a shared unit suffix: "1.23 / 16.00 GB".
static void FormatBytesPair(wchar_t* buf, size_t cnt, ULONGLONG used, ULONGLONG limit) {
  if (limit >= kGB) {
    swprintf(buf, cnt, L"%.2f / %.2f GB",
             static_cast<double>(used)  / static_cast<double>(kGB),
             static_cast<double>(limit) / static_cast<double>(kGB));
  } else {
    swprintf(buf, cnt, L"%I64u / %I64u MB", used / kMB, limit / kMB);
  }
}

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
  KillTimer(hWnd, WM_MONTIMER);
  return SetTimer(hWnd, WM_MONTIMER, interval_ms, nullptr) != 0;
}

void StopSysmon(HWND hWnd) {
  if (hWnd != nullptr) {
    KillTimer(hWnd, WM_MONTIMER);
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
      swprintf(buf, 32, L"%.2f%%", static_cast<double>(pct));
      HWND hw = GetDlgItem(hWnd, static_cast<int>(id));
      if (hw != nullptr) {
        SetWindowTextW(hw, buf);
      }
    };
    set_val(IDC_CPUIDLE,   s_cpu_stats.idle_pct);
    set_val(IDC_CPUUSER,   s_cpu_stats.user_pct);
    set_val(IDC_CPUKERNEL, s_cpu_stats.kernel_pct);
    set_val(IDC_CPUTOTAL,  s_cpu_stats.total_pct);
  }

  // Record graph sample. Uses current s_cpu_stats — zeroed until the first
  // successful UpdateCpuStats call, which gives the "ramps up from 0" effect.
  s_graph_samples[s_graph_head] = { s_cpu_stats.total_pct, s_cpu_stats.kernel_pct };
  s_graph_head = (s_graph_head + 1) % kGraphMaxSamples;

  // Update memory stats.
  MemStats mem = {};
  if (UpdateMemStats(&mem)) {
    s_mem_stats = mem;
    wchar_t buf[48];
    auto set_str = [&](UINT id, const wchar_t* str) {
      HWND hw = GetDlgItem(hWnd, static_cast<int>(id));
      if (hw != nullptr) {
        SetWindowTextW(hw, str);
      }
    };
    FormatBytesPair(buf, 48, s_mem_stats.ram_used, s_mem_stats.ram_total);
    set_str(IDC_RAMTOTAL, buf);
    FormatBytesPair(buf, 48, s_mem_stats.pf_used, s_mem_stats.pf_limit);
    set_str(IDC_PFTOTAL, buf);
    FormatBytesPair(buf, 48, s_mem_stats.vm_used, s_mem_stats.vm_limit);
    set_str(IDC_VMTOTAL, buf);
    // Show "used / limit" only when the OS has a finite cap configured.
    // (SIZE_T)-1 is the sentinel Windows uses for "system-managed / no fixed limit".
    static const ULONGLONG kCacheLimitUnlimited =
        static_cast<ULONGLONG>(static_cast<SIZE_T>(-1));
    if (s_mem_stats.cache_limit != kCacheLimitUnlimited && s_mem_stats.cache_limit > 0) {
      FormatBytesPair(buf, 48,
                      static_cast<ULONGLONG>(s_mem_stats.cache_bytes),
                      s_mem_stats.cache_limit);
    } else {
      FormatBytes(buf, 48, static_cast<ULONGLONG>(s_mem_stats.cache_bytes));
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
  const int lbl_w     = content_w * 55 / kLabelWidth;
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
    { s_hCacheLabel,     lbl_x_r,        row0_y+2*stride, lbl_w, kControlHeight },
    { s_hCacheValue,     lbl_x_r+lbl_w,  row0_y+2*stride, val_w, kControlHeight },
    { s_hVmLabel,        lbl_x_r,        row0_y+3*stride, lbl_w, kControlHeight },
    { s_hVmValue,        lbl_x_r+lbl_w,  row0_y+3*stride, val_w, kControlHeight },
  };
  for (const auto& e : entries) {
    if (hdwp != nullptr && e.hw != nullptr) {
      hdwp = DeferWindowPos(hdwp, e.hw, nullptr, e.cx, e.cy, e.cw, e.ch,
                            SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  return hdwp;
}
