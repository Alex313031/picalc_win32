#ifndef PICALCWIN32_GLOBALS_H_
#define PICALCWIN32_GLOBALS_H_

#include "framework.h"

// Main client width/height
extern int cxClient;
extern int cyClient;

extern HINSTANCE g_hInstance; // This program instance, everything descends from this

extern HWND mainHwnd; // Our main window handle

extern HWND hStatusBar; // Our status bar

extern HWND hOutputEdit;  // Multiline read-only edit, bottom pane (pi output / log)
extern HWND hSplitter;    // Draggable horizontal splitter between top + bottom panes
extern HWND hDigitsLabel;  // Static label: "Number of Digits:"
extern HWND hDigitsCombo;  // Dropdown picker for the digit count
extern HWND hThreadsLabel; // Static label: "Num. CPU Threads:"
extern HWND hThreadsCombo; // Dropdown picker for the thread count
extern HWND hStartButton;   // "Calculate!" button
extern HWND hStopButton;    // "Stop" button
extern HWND hOpenOutButton; // "Open Out File" button
extern HWND hAboutButton;   // "About" button

extern volatile bool g_running;  // True while a pi calculation is in progress

extern bool g_sound_on; // Mirrors the Settings -> Sound? menu check state

extern bool can_use_582_controls; // Whether we can use "modern" common controls from XP+

extern COLORREF g_bkg_color; // Window background color

#endif // PICALCWIN32_GLOBALS_H_
