// Functions to get the number of logical CPUs, resource usage, etc.

#include "cpu.h"

#include "constants.h"
#include "utils.h"

DWORD GetLogicalProcessorCount() {
  SYSTEM_INFO si = {};

  // GetNativeSystemInfo is XP+ only. Resolve it dynamically so the import
  // table has no entry for it — a missing IAT entry crashes Win2K at load.
  typedef void(WINAPI * FnGetNativeSystemInfo)(LPSYSTEM_INFO);
  static FnGetNativeSystemInfo pfnGetNativeSystemInfo = nullptr;
  static bool s_resolved = false;
  if (!s_resolved) {
    HMODULE hKernel32       = GetModuleHandleW(L"kernel32.dll");
    pfnGetNativeSystemInfo  = reinterpret_cast<FnGetNativeSystemInfo>(
        hKernel32 ? GetProcAddress(hKernel32, "GetNativeSystemInfo") : nullptr);
    s_resolved = true;
  }

  if (pfnGetNativeSystemInfo) {
    pfnGetNativeSystemInfo(&si);
  } else {
    GetSystemInfo(&si);
  }
  return (si.dwNumberOfProcessors > 0) ? si.dwNumberOfProcessors : 1;
}
