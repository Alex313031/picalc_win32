#ifndef PICALCWIN32_STRINGS_H_
#define PICALCWIN32_STRINGS_H_

#include "constants.h"

// All UI strings below are defined as const wchar_t[] in strings.cc. The
// linker places them in .rdata where the PE loader maps them at startup -
// no allocations, one symbol per string, and a bare address load at the
// call site (decays to const wchar_t* when passed to Win32 APIs).

// Strings to print
extern const wchar_t kCalculateMessage[];
extern const wchar_t kStoppedMessage[];
extern const wchar_t kDoneMessage[];
extern const wchar_t kIterMessage[];

// Controls strings
extern const wchar_t kNumDigitsLabel[];
extern const wchar_t kNumThreadsLabel[];
extern const wchar_t kStartButtonLabel[];
extern const wchar_t kStopButtonLabel[];
extern const wchar_t kShowConsoleLabel[];
extern const wchar_t kHideConsoleLabel[];
extern const wchar_t kOpenResultLabel[];
extern const wchar_t kCloseResultLabel[];
extern const wchar_t kClearResultLabel[];
extern const wchar_t kClearOutputLabel[];
extern const wchar_t kAboutButtonLabel[];
extern const wchar_t kExitButtonLabel[];
extern const wchar_t kCntrlsGroupLabel[];
extern const wchar_t kSysmonGroupLabel[];
extern const wchar_t kCPUGroupLabel[];
extern const wchar_t kMemGroupLabel[];
extern const wchar_t kResultPopupTitle[];
extern const wchar_t kRunTitle[];
extern const wchar_t kRunPrompt[];

// Dialog strings
extern const wchar_t kDigitsDlgTitle[];
extern const wchar_t kThreadsDlgTitle[];
extern const wchar_t kDigitsDlgPrompt[];
extern const wchar_t kThreadsDlgPrompt[];

// Hover tooltip text. Each control whose tooltip is wired in
// CreateChildControls / CreateSysmonControls references one of these.
// TOOLINFOW::lpszText captures the pointer at tooltip-creation time and the
// string must outlive the tooltip - extern wchar_t[] storage in .rdata
// satisfies that trivially.
extern const wchar_t kTooltipDigits[];
extern const wchar_t kTooltipThreads[];
extern const wchar_t kTooltipStart[];
extern const wchar_t kTooltipStop[];
extern const wchar_t kTooltipOpenPiTxt[];
extern const wchar_t kTooltipClrResult[];
extern const wchar_t kTooltipClrOutput[];
extern const wchar_t kTooltipConsole[];
extern const wchar_t kTooltipAbout[];
extern const wchar_t kTooltipExit[];
extern const wchar_t kTooltipOutput[];
extern const wchar_t kTooltipGraph[];
extern const wchar_t kTooltipCpuIdle[];
extern const wchar_t kTooltipCpuUser[];
extern const wchar_t kTooltipCpuKernel[];
extern const wchar_t kTooltipCpuTotal[];
extern const wchar_t kTooltipRam[];
extern const wchar_t kTooltipPageFile[];
extern const wchar_t kTooltipVirtMem[];
extern const wchar_t kTooltipSysCache[];

// Sysmon metric labels and initial value text
extern const wchar_t kMetricCpuIdle[];
extern const wchar_t kMetricCpuUser[];
extern const wchar_t kMetricCpuKernel[];
extern const wchar_t kMetricCpuUsage[];
extern const wchar_t kMetricRam[];
extern const wchar_t kMetricPageFile[];
extern const wchar_t kMetricVirtMem[];
extern const wchar_t kMetricSysCache[];
extern const wchar_t kMetricInitVal[];

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
