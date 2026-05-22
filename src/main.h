#ifndef PICALCWIN32_MAIN_H_
#define PICALCWIN32_MAIN_H_

#include "framework.h"
#include "picalc.h"
#include "utils.h"

// File name for Pi calculation results dump
static inline const wchar_t kResultsFile[] = L"picalc_result.txt";

// Registers our window class, one of the first things to run.
bool RegisterWndClass(HINSTANCE hInstance, LPCWSTR className);

// Creates the main window and shows it
bool InitWindow(HINSTANCE hInstance, LPCWSTR className, LPCWSTR title, int iCmdShow);

// Main window procedure
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Initializes app state
bool InitApp(HWND hWnd);

// Closes all windows and cleans up any resources.
void ShutDownApp();

// Shows help
bool LaunchHelp(HWND hWnd);

// About dialog handler
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

#endif // PICALCWIN32_MAIN_H_
