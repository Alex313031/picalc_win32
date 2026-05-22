#ifndef PICALCWIN32_CONTROLS_H_
#define PICALCWIN32_CONTROLS_H_

#include "framework.h"
#include "globals.h"

// Registers the custom window class used by the draggable horizontal
// splitter. Must be called once (before CreateChildControls) and is
// idempotent across calls within the same process.
bool RegisterSplitterClass(HINSTANCE hInstance);

// Creates the splitter bar and the bottom output edit as children of
// `parent`. RegisterSplitterClass must have run first. Stores the
// HWNDs in the hSplitter / hOutputEdit globals.
bool CreateChildControls(HWND parent);

// Repositions the splitter and the output edit based on the current
// splitter Y. Call from the parent's WM_SIZE handler. On the first
// call (or after a SetSplitterY(-1) reset) the splitter is centred
// vertically so the panes default to half / half.
void LayoutChildren(HWND parent);

// Current splitter top-edge Y in parent client coordinates. Returns
// -1 before the first layout, after which LayoutChildren keeps the
// value clamped to [kMinPaneHeight, cy - kMinPaneHeight - kSplitterHeight].
int GetSplitterY();
void SetSplitterY(int y);

// Appends `msg` as a single line to the output edit, followed by a
// CRLF so the next call starts on a fresh line. Text is passed
// through as UTF-16 (std::wstring on Windows is wchar_t, which is
// the same encoding Win32 EM_REPLACESEL expects). The caret is
// pinned to the end after each append so ES_AUTOVSCROLL keeps the
// newest line visible. No-op if the output edit isn't created yet.
void SendOutputMessage(const std::wstring& msg);

#endif // PICALCWIN32_CONTROLS_H_
