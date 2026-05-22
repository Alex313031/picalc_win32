#ifndef PICALCWIN32_STRINGS_H_
#define PICALCWIN32_STRINGS_H_

#include "version.h"

// Strings to print
inline const wchar_t* const kCalculateMessage = L"Started Calculating ";
inline const wchar_t* const kStoppedMessage   = L"Stopped Calculating ";
inline const wchar_t* const kDoneMessage      = L"Done Calculating ";
inline const wchar_t* const kIterMessage      = L"Iterations: ";
inline const wchar_t* const kTimeMessage      = L"Time: ";

// Controls strings
inline const wchar_t* const kNumDigitsLabel   = L"Number of Digits:";
inline const wchar_t* const kNumThreadsLabel  = L"Num. CPU Threads:";
inline const wchar_t* const kStartButtonLabel = L"Calculate!";
inline const wchar_t* const kStopButtonLabel  = L"Stop";
inline const wchar_t* const kShowConsoleLabel = L"Show Console";
inline const wchar_t* const kHideConsoleLabel = L"Hide Console";
inline const wchar_t* const kOpenOutFileLabel = L"Open Out File";
inline const wchar_t* const kAboutButtonLabel = L"About";

// Combobox items: Order = display order.
inline const wchar_t* const kDigitOptions[] = {
  // Digit-count options offered in the num digitscombobox.
  L"10", L"100", L"1K", L"10K", L"100K", L"1M", L"10M", L"Custom"
};

inline const wchar_t* const kThreadsOptions[] = {
  // Threads options offered in the num cpu threads combobox.
  L"1", L"2", L"4", L"6", L"8", L"16", L"32", L"Custom"
};

#endif // PICALCWIN32_STRINGS_H_
