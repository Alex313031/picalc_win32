#ifndef PICALCWIN32_RESULTS_H_
#define PICALCWIN32_RESULTS_H_

#include "constants.h"
#include "framework.h"

// Maximum bytes read from the result file into the viewer window. The edit
// control's text limit is raised to match. Increase for very large runs.
inline constexpr DWORD kMaxResultViewBytes = static_cast<DWORD>(kMaxEditLoadBytes);

// Registers the result viewer window class. Call once from WM_CREATE.
bool RegisterResultWindowClass(HINSTANCE hInstance);

// Opens the viewer if closed, closes it if open. Updates the button label.
void ToggleResultWindow(HWND parent);

// Closes and destroys the viewer window. Safe to call when already closed.
bool CloseResultWindow();

// Returns true when the viewer window is currently open.
bool IsResultWindowOpen();

// Returns the result viewer HWND, or nullptr when closed.
// Used by the message loop to route keyboard input correctly.
HWND GetResultHwnd();

// Re-reads the result file and updates the viewer's edit control.
// No-op when the viewer is not open.
void ReloadResultWindow();

#endif // PICALCWIN32_RESULTS_H_
