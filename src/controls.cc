// Controls layout/logic

#include "controls.h"

#include "constants.h"
#include "main.h"
#include "resource.h"
#include "utils.h"

// =========================================================================
// Splitter window class
// =========================================================================

static const wchar_t* kSplitterClassName = L"PicalcSplitter";

// -1 = uninitialised, gets centred on the first LayoutChildren call.
static int s_splitter_y = -1;
static bool s_dragging  = false;

static LRESULT CALLBACK SplitterProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_LBUTTONDOWN:
      SetCapture(hWnd);
      s_dragging = true;
      return 0;
    case WM_LBUTTONUP:
      if (s_dragging) {
        ReleaseCapture();
        s_dragging = false;
      }
      return 0;
    case WM_MOUSEMOVE: {
      if (!s_dragging) {
        return 0;
      }
      // lParam is in splitter-local client coords; translate to parent
      // client coords so we can store the absolute splitter top edge.
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      HWND parent = GetParent(hWnd);
      if (parent == nullptr) {
        return 0;
      }
      ClientToScreen(hWnd, &pt);
      ScreenToClient(parent, &pt);
      s_splitter_y = pt.y;
      LayoutChildren(parent);
      return 0;
    }
    case WM_SETCURSOR:
      // Always show the vertical-resize cursor while the pointer is over
      // the splitter, even between drags.
      SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
      return TRUE;
    case WM_ERASEBKGND:
      return TRUE;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT rc;
      GetClientRect(hWnd, &rc);
      // Classic button-face fill with a 1px raised top + bottom edge so
      // the bar is visible against the surrounding grey without looking
      // like a heavyweight 3D bevel.
      FillRectWithColor(hdc, rc, RGB_LTGREY);
      DrawEdge(hdc, &rc, EDGE_RAISED, BF_TOP | BF_BOTTOM);
      EndPaint(hWnd, &ps);
      return 0;
    }
    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

bool RegisterSplitterClass(HINSTANCE hInstance) {
  // RegisterClassExW returns 0 if the class is already registered
  // *and* the second registration disagrees with the first - getting
  // back the original ATOM for a duplicate-but-identical registration
  // would be 0 with GetLastError = ERROR_CLASS_ALREADY_EXISTS. Treat
  // that as success so this function stays idempotent.
  WNDCLASSEXW wc = {};
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = SplitterProc;
  wc.hInstance     = hInstance;
  wc.hCursor       = LoadCursorW(nullptr, IDC_SIZENS);
  wc.hbrBackground = nullptr; // We paint in WM_PAINT
  wc.lpszClassName = kSplitterClassName;
  if (RegisterClassExW(&wc) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool CreateChildControls(HWND parent) {
  if (parent == nullptr) {
    return false;
  }
  // Bottom pane: multi-line, read-only edit. Starts hidden-size (0x0);
  // LayoutChildren places it correctly on the first WM_SIZE.
  hOutputEdit = CreateWindowExW(
      WS_EX_WINDOWEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
      0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_OUTPUT_EDIT)),
      g_hInstance, nullptr);
  if (hOutputEdit == nullptr) {
    return false;
  }

  hSplitter = CreateWindowExW(
      0, kSplitterClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SPLITTER)), g_hInstance, nullptr);
  if (hSplitter == nullptr) {
    return false;
  }
  return true;
}

int GetSplitterY() {
  return s_splitter_y;
}

void SetSplitterY(int y) {
  s_splitter_y = y;
}

void LayoutChildren(HWND parent) {
  if (parent == nullptr || hSplitter == nullptr || hOutputEdit == nullptr) {
    return;
  }
  RECT rc;
  GetClientRect(parent, &rc);
  const int cx = rc.right - rc.left;
  const int cy = rc.bottom - rc.top;
  if (cx <= 0 || cy <= 0) {
    return;
  }
  // Default to a 50/50 split on first layout.
  if (s_splitter_y < 0) {
    s_splitter_y = cy / 2;
  }
  // Clamp into a local for layout; don't write back to s_splitter_y.
  // Otherwise shrinking the window past the user's splitter Y would
  // overwrite their preference, so growing the window again wouldn't
  // restore the original position.
  const int min_y = kMinPaneHeight;
  const int max_y = cy - kMinPaneHeight - kSplitterHeight;
  int splitter_top = s_splitter_y;
  if (max_y < min_y) {
    // Window is too small for both panes + splitter at minimum
    // sizes; collapse the top pane and accept a too-short bottom.
    splitter_top = min_y;
  } else if (splitter_top < min_y) {
    splitter_top = min_y;
  } else if (splitter_top > max_y) {
    splitter_top = max_y;
  }

  const int bottom_top = splitter_top + kSplitterHeight;
  const int bottom_h   = (cy > bottom_top) ? (cy - bottom_top) : 0;

  // Top pane is empty for now - the parent's WM_PAINT fills the
  // strip above the splitter with g_bkg_color.
  MoveWindow(hSplitter, 0, splitter_top, cx, kSplitterHeight, TRUE);
  MoveWindow(hOutputEdit, 0, bottom_top, cx, bottom_h, TRUE);
}

// Pre-Vista Edit controls only treat \r\n as a line break - bare \n
// renders as a "tofu" control glyph on Win2k/XP (Win7+ and Wine are
// forgiving). Normalize any mix of \r, \n, \r\n in the caller's
// string to canonical \r\n before we append.
static std::wstring NormalizeNewlines(const std::wstring& msg) {
  std::wstring out;
  out.reserve(msg.size() + 8);
  for (size_t i = 0; i < msg.size(); ++i) {
    const wchar_t c = msg[i];
    if (c == L'\r') {
      out += L"\r\n";
      // Eat a paired \n so \r\n doesn't become \r\n\r\n.
      if (i + 1 < msg.size() && msg[i + 1] == L'\n') {
        ++i;
      }
    } else if (c == L'\n') {
      out += L"\r\n";
    } else {
      out += c;
    }
  }
  return out;
}

void SendOutputMessage(const std::wstring& msg) {
  if (hOutputEdit == nullptr) {
    return;
  }
  // Collapse selection to a caret at the very end, then splice the new
  // line in via EM_REPLACESEL. wParam=FALSE keeps the append out of
  // the undo buffer (the output pane is a log, not user input).
  const int len = GetWindowTextLengthW(hOutputEdit);
  SendMessageW(hOutputEdit, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
  const std::wstring line = NormalizeNewlines(msg) + L"\r\n";
  SendMessageW(hOutputEdit, EM_REPLACESEL, FALSE,
               reinterpret_cast<LPARAM>(line.c_str()));
  // EM_REPLACESEL on an ES_AUTOVSCROLL edit usually scrolls the new
  // text into view, but EM_SCROLLCARET makes the guarantee explicit
  // when the user has scrolled up to read earlier output.
  SendMessageW(hOutputEdit, EM_SCROLLCARET, 0, 0);
}
