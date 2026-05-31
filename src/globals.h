#ifndef PICALCWIN32_GLOBALS_H_
#define PICALCWIN32_GLOBALS_H_

#include "framework.h"

// Main client width/height
extern int cxClient;
extern int cyClient;

extern HINSTANCE g_hInstance; // This program instance, everything descends from this

extern HWND mainHwnd; // Our main window handle

// extern HWND hStatusBar; // TODO: status bar (planned)

extern HWND hOutputEdit;        // Multiline read-only edit, bottom pane (pi output / log)
extern HWND hSplitter;          // Draggable horizontal splitter between top + bottom panes
extern HWND hDigitsLabel;       // Static label: "Number of Digits:"
extern HWND hDigitsCombo;       // Dropdown picker for the digit count
extern HWND hThreadsLabel;      // Static label: "Num. CPU Threads:"
extern HWND hThreadsCombo;      // Dropdown picker for the thread count
extern HWND hStartButton;       // "Calculate!" button
extern HWND hStopButton;        // "Stop" button
extern HWND hOpenOutButton;     // "Open Out File" button
extern HWND hClearResultButton; // "Clear Results" button
extern HWND hClearOutputButton; // "Clear Output" button
extern HWND hAboutButton;       // "About" button
extern HWND hConsoleButton;     // "Show/Hide Console" button (label tracks state)
extern HWND hExitButton;        // "Exit" button

extern std::atomic<bool> g_running; // True while a pi calculation is in progress

extern bool g_sound_on;         // Mirrors the Settings -> Sound? menu check state
extern bool g_colored_output;   // Mirrors the Settings -> Colored Output Pane check state
extern bool g_show_kernel_times; // Mirrors the Settings -> System Monitor -> Show Kernel Times state

extern bool can_use_582_controls; // Whether we can use "modern" common controls from XP+

extern COLORREF g_bkg_color; // Window background color

extern bool is_on_wine;

#endif // PICALCWIN32_GLOBALS_H_
