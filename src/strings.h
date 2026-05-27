#ifndef PICALCWIN32_STRINGS_H_
#define PICALCWIN32_STRINGS_H_

#include "version.h"

// Strings to print
inline const wchar_t* const kCalculateMessage = L"Started calculating ";
inline const wchar_t* const kStoppedMessage   = L"Stopped calculating ";
inline const wchar_t* const kDoneMessage      = L"Done! ";
inline const wchar_t* const kIterMessage      = L"Num. Iterations: ";

// Controls strings
inline const wchar_t* const kNumDigitsLabel   = L"Number of Digits:";
inline const wchar_t* const kNumThreadsLabel  = L"Num. CPU Threads:";
inline const wchar_t* const kStartButtonLabel = L"Calculate!";
inline const wchar_t* const kStopButtonLabel  = L"Stop";
inline const wchar_t* const kShowConsoleLabel = L"Show Console";
inline const wchar_t* const kHideConsoleLabel = L"Hide Console";
inline const wchar_t* const kOpenResultLabel  = L"Open Result File";
inline const wchar_t* const kCloseResultLabel = L"Close Result File";
inline const wchar_t* const kClearResultLabel = L"Clear Result File";
inline const wchar_t* const kClearOutputLabel = L"Clear Status Pane";
inline const wchar_t* const kAboutButtonLabel = L"About";
inline const wchar_t* const kExitButtonLabel  = L"Exit";
inline const wchar_t* const kCntrlsGroupLabel = L"Controls";
inline const wchar_t* const kSysmonGroupLabel = L"System Monitor";
inline const wchar_t* const kResultPopupTitle = L"Pi Calculation Results";

// Combobox items: Order = display order.
inline const wchar_t* const kDigitOptions[] = {
  // Digit-count options offered in the num digitscombobox.
  L"10", L"100", L"1K", L"10K", L"100K", L"1M", L"10M", L"50M", L"Custom"
};

inline const wchar_t* const kThreadsOptions[] = {
  // Threads options offered in the num cpu threads combobox.
  L"1", L"2", L"4", L"6", L"8", L"16", L"32", L"Custom"
};

#endif // PICALCWIN32_STRINGS_H_
