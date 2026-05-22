#ifndef PICALCWIN32_GLOBALS_H_
#define PICALCWIN32_GLOBALS_H_

#include "framework.h"

// Main client width/height
extern int cxClient;
extern int cyClient;

extern HINSTANCE g_hInstance; // This program instance, everything descends from this

extern HWND mainHwnd; // Our main window handle

extern HWND hStatusBar; // Our status bar

extern HWND hOutputEdit; // Multiline read-only edit, bottom pane (pi output / log)
extern HWND hSplitter;   // Draggable horizontal splitter between top + bottom panes

extern volatile bool g_running;  // True while a pi calculation is in progress

extern bool can_use_582_controls; // Whether we can use "modern" common controls from XP+

extern COLORREF g_bkg_color; // Window background color

#endif // PICALCWIN32_GLOBALS_H_
