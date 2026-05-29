// String helper functions

#include "strings.h"

// =========================================================================
// UI string storage. Declarations live in strings.h. Each symbol is a
// const wchar_t[] in .rdata - the PE loader maps these in at startup, the
// OS shares the pages across processes, and call sites get a bare address
// load (no pointer indirection, no allocation).
// =========================================================================

// clang-format off
// Strings to print
const wchar_t kCalculateMessage[] = L"Started calculating ";
const wchar_t kStoppedMessage[]   = L"Stopped calculating ";
const wchar_t kDoneMessage[]      = L"Done! ";
const wchar_t kIterMessage[]      = L"Num. Iterations: ";
// Controls strings
const wchar_t kNumDigitsLabel[]   = L"Number of Digits:";
const wchar_t kNumThreadsLabel[]  = L"Num. CPU Threads:";
const wchar_t kStartButtonLabel[] = L"Calculate!";
const wchar_t kStopButtonLabel[]  = L"Stop";
const wchar_t kShowConsoleLabel[] = L"Show Console";
const wchar_t kHideConsoleLabel[] = L"Hide Console";
const wchar_t kOpenResultLabel[]  = L"Open Result File";
const wchar_t kCloseResultLabel[] = L"Close Result File";
const wchar_t kClearResultLabel[] = L"Clear Result File";
const wchar_t kClearOutputLabel[] = L"Clear Status Pane";
const wchar_t kAboutButtonLabel[] = L"About";
const wchar_t kExitButtonLabel[]  = L"Exit";
const wchar_t kCntrlsGroupLabel[] = L"Controls";
const wchar_t kSysmonGroupLabel[] = L"System Monitor";
const wchar_t kCPUGroupLabel[]    = L"CPU Usage";
const wchar_t kMemGroupLabel[]    = L"MEM Usage";
const wchar_t kResultPopupTitle[] = L"Pi Calculation Results";
const wchar_t kRunTitle[]         = L"Create New Task";
const wchar_t kRunPrompt[]        = L"Type the name of a program, folder, or file, and PiCalc will open it for you.";
// Dialog strings
const wchar_t kDigitsDlgTitle[]   = L"Custom Digit Count";
const wchar_t kThreadsDlgTitle[]  = L"Custom Thread Count";
const wchar_t kDigitsDlgPrompt[]  = L"Enter number of Digits: ";
const wchar_t kThreadsDlgPrompt[] = L"Enter number of CPU Threads: ";
// Hover tooltip text
const wchar_t kTooltipDigits[]    = L"Number of decimal digits of Pi to calculate.";
const wchar_t kTooltipThreads[]   = L"Number of CPU threads used for the calculation.";
const wchar_t kTooltipStart[]     = L"Start calculating Pi with the selected digit and thread counts.";
const wchar_t kTooltipStop[]      = L"Cancel the current Pi calculation.";
const wchar_t kTooltipOpenPiTxt[] = L"Open the Pi results file\n(" PI_TXT_NAME L", re-written on every new calculation).";
const wchar_t kTooltipClrResult[] = L"Delete all saved Pi results in " PI_TXT_NAME L" from disk.";
const wchar_t kTooltipClrOutput[] = L"Clear the status pane below, does not affect the saved results file.";
const wchar_t kTooltipConsole[]   = L"Toggle the debug logging console window on or off.";
const wchar_t kTooltipAbout[]     = L"Show version and credits.";
const wchar_t kTooltipExit[]      = L"Quit App.";
const wchar_t kTooltipOutput[]    = L"Status messages, results, and log output from the current session.";
const wchar_t kTooltipGraph[]     = L"CPU Usage % Graph";
const wchar_t kTooltipCpuIdle[]   = L"Percentage of CPU time spent idle (not running any processes)";
const wchar_t kTooltipCpuUser[]   = L"Percentage of CPU time spent running user-mode code (i.e. applications)";
const wchar_t kTooltipCpuKernel[] = L"Percentage of CPU time spent running kernel level code (drivers, syscalls, etc.)";
const wchar_t kTooltipCpuTotal[]  = L"Overall CPU utilization across all cores.";
const wchar_t kTooltipRam[]       = L"Physical Memory in use / Total Installed RAM";
const wchar_t kTooltipPageFile[]  = L"Page File usage / Max Page File size";
const wchar_t kTooltipVirtMem[]   = L"Total committed virtual memory - RAM & Page File space promised to processes.";
const wchar_t kTooltipSysCache[]  = L"System file cache usage - RAM the OS holds to cache recently accessed disk data.";
// Sysmon metric labels and initial value text
const wchar_t kMetricCpuIdle[]    = L"CPU Idle: ";
const wchar_t kMetricCpuUser[]    = L"CPU User: ";
const wchar_t kMetricCpuKernel[]  = L"CPU Kernel: ";
const wchar_t kMetricCpuUsage[]   = L"CPU Total: ";
const wchar_t kMetricRam[]        = L"RAM Usage: ";
const wchar_t kMetricPageFile[]   = L"Page File Usage: ";
const wchar_t kMetricVirtMem[]    = L"Commit Charge: ";
const wchar_t kMetricSysCache[]   = L"System Cache: ";
const wchar_t kMetricInitVal[]    = L"NaN";
// clang-format on

// =========================================================================
// Helpers
// =========================================================================

std::string ToANSI(const wchar_t* in) {
  if (in == nullptr) {
    return {};
  }
  int len = WideCharToMultiByte(CP_ACP, 0, in, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(len - 1), '\0');
  WideCharToMultiByte(CP_ACP, 0, in, -1, &out[0], len, nullptr, nullptr);
  return out;
}

std::string ToANSI(const std::wstring& in) {
  return ToANSI(in.c_str());
}

std::string ToANSI(const std::wstring* in) {
  return (in != nullptr) ? ToANSI(*in) : std::string{};
}

std::wstring ToWide(const char* in) {
  if (in == nullptr) {
    return {};
  }
  int len = MultiByteToWideChar(CP_ACP, 0, in, -1, nullptr, 0);
  if (len <= 1) {
    return {};
  }
  std::wstring out(static_cast<size_t>(len - 1), L'\0');
  MultiByteToWideChar(CP_ACP, 0, in, -1, &out[0], len);
  return out;
}

std::wstring ToWide(const std::string& in) {
  return ToWide(in.c_str());
}

std::wstring ToWide(const std::string* in) {
  return (in != nullptr) ? ToWide(*in) : std::wstring{};
}

void FormatBytes(wchar_t* buf, size_t cnt, ULONGLONG bytes) {
  const double val = static_cast<double>(bytes);
  if (bytes >= kGB) {
    swprintf(buf, cnt, L"%.2f GB", val / static_cast<double>(kGB));
  } else if (bytes >= kMB) {
    swprintf(buf, cnt, L"%.2f MB", val / static_cast<double>(kMB));
  } else {
    swprintf(buf, cnt, L"%.2f KB", val / static_cast<double>(kKB));
  }
}

void FormatBytesPair(wchar_t* buf, size_t cnt, ULONGLONG used, ULONGLONG limit) {
  const double dused  = static_cast<double>(used);
  const double dlimit = static_cast<double>(limit);
  if (limit >= kGB) {
    swprintf(buf, cnt, L"%.2f / %.2f GB", dused / static_cast<double>(kGB),
             dlimit / static_cast<double>(kGB));
  } else if (limit >= kMB) {
    swprintf(buf, cnt, L"%.2f / %.2f MB", dused / static_cast<double>(kMB),
             dlimit / static_cast<double>(kMB));
  } else {
    swprintf(buf, cnt, L"%.2f / %.2f KB", dused / static_cast<double>(kKB),
             dlimit / static_cast<double>(kKB));
  }
}
