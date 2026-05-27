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
// value clamped to [kMinTopHeight, cy - kMinBottomHeight - kSplitterHeight].
int GetSplitterY();

// Same as GetSplitterY() but applies the same clamping that LayoutChildren
// uses. Use this in paint handlers so the painted region matches where
// the children actually are, avoiding unpainted strips.
int GetClampedSplitterY(int client_h);

void SetSplitterY(int new_y);

// Appends `msg` as a single line to the output edit, followed by a
// CRLF so the next call starts on a fresh line. Text is passed
// through as UTF-16 (std::wstring on Windows is wchar_t, which is
// the same encoding Win32 EM_REPLACESEL expects). The caret is
// pinned to the end after each append so ES_AUTOVSCROLL keeps the
// newest line visible. No-op if the output edit isn't created yet.
void SendOutputMessage(const std::wstring& msg);

// Appends a visual separator (80 asterisks) to the output edit only.
// Used to break up sections of the log without spamming the console.
void PrintOutputSeparator();

// Prints a line to console (LOG(ERROR) when is_error, LOG(INFO)
// otherwise) and also appends it to the output edit control.
void EmitLine(const std::wstring& msg, bool is_error);

// Wipes the output edit. No-op if the control hasn't been created yet.
void ClearOutput();

// Returns the combo index and custom-injected flag that CreateChildControls
// chose for the threads combo (based on GetLogicalProcessorCount).
// main.cc reads these after CreateChildControls to seed s_prev_threads_sel
// and s_threads_custom_injected.
int  GetInitialThreadsSel();
bool IsInitialThreadsCustomInjected();

// Reads the current selection from the digits / threads combos and
// converts the displayed string ("1K", "1,000", "10M", "32") to an
// integer. Returns -1 when "Custom" is selected, the combo has no
// selection, or the value can't be parsed.
int GetSelectedDigits();
int GetSelectedThreads();

#endif // PICALCWIN32_CONTROLS_H_
