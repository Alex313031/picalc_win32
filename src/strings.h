#ifndef PICALCWIN32_STRINGS_H_
#define PICALCWIN32_STRINGS_H_

#include "framework.h"

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
inline const wchar_t* const kCPUGroupLabel    = L"CPU Usage";
inline const wchar_t* const kMemGroupLabel    = L"MEM Usage";
inline const wchar_t* const kResultPopupTitle = L"Pi Calculation Results";
inline const wchar_t* const kRunTitle         = L"Create New Task";
inline const wchar_t* const kRunPrompt =
    L"Type the name of a program, folder, or file, and PiCalc will open it for you.";

// Dialog strings
inline const wchar_t* const kDigitsDlgTitle   = L"Custom Digit Count";
inline const wchar_t* const kThreadsDlgTitle  = L"Custom Thread Count";
inline const wchar_t* const kDigitsDlgPrompt  = L"Enter number of Digits: ";
inline const wchar_t* const kThreadsDlgPrompt = L"Enter number of CPU Threads: ";

// Sysmon metric labels and initial value text
inline const wchar_t* const kMetricCpuIdle   = L"CPU Idle: ";
inline const wchar_t* const kMetricCpuUser   = L"CPU User: ";
inline const wchar_t* const kMetricCpuKernel = L"CPU Kernel: ";
inline const wchar_t* const kMetricCpuUsage  = L"CPU Total: ";
inline const wchar_t* const kMetricRam       = L"RAM Usage: ";
inline const wchar_t* const kMetricPageFile  = L"Page File Usage: ";
inline const wchar_t* const kMetricVirtMem   = L"Commit Charge: ";
inline const wchar_t* const kMetricSysCache  = L"System Cache: ";
inline const wchar_t* const kMetricInitVal   = L"NaN";

// Combobox items: Order = display order.
inline const wchar_t* const kDigitOptions[] = {
    // Digit-count options offered in the num digitscombobox.
    L"10", L"100", L"1K", L"10K", L"100K", L"1M", L"10M", L"50M", L"Custom"};

inline const wchar_t* const kThreadsOptions[] = {
    // Threads options offered in the num cpu threads combobox.
    L"1", L"2", L"4", L"6", L"8", L"16", L"32", L"Custom"};

// Wide <-> ANSI conversion helpers (CP_ACP; returns empty string on null/error).
std::string ToANSI(const wchar_t* in);
std::string ToANSI(const std::wstring& in);
std::string ToANSI(const std::wstring* in);
std::wstring ToWide(const char* in);
std::wstring ToWide(const std::string& in);
std::wstring ToWide(const std::string* in);

// Formats a byte count into the most readable unit: "1.23 GB", "456.00 MB", etc.
void FormatBytes(wchar_t* buf, size_t cnt, ULONGLONG bytes);

// Formats a used/limit pair with a shared unit suffix: "1.23 / 16.00 GB".
// Unit is chosen by limit (the larger value).
void FormatBytesPair(wchar_t* buf, size_t cnt, ULONGLONG used, ULONGLONG limit);

#endif // PICALCWIN32_STRINGS_H_
