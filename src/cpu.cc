// Functions to get the number of logical CPUs, resource usage, etc.

#include "cpu.h"

#include "constants.h"
#include "utils.h"

// NtQuerySystemInformation is NT-native, available on Win2K.
// Loaded dynamically to keep the import table clean (no IAT entry).
typedef LONG(WINAPI* FnNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

// Info class 8: per-logical-CPU performance counters.
// KernelTime includes IdleTime; exclusive kernel = KernelTime - IdleTime.
static constexpr ULONG kSysProcessorPerfInfo = 8;

struct SysProcPerfInfo {
  LARGE_INTEGER IdleTime;
  LARGE_INTEGER KernelTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER DpcTime;
  LARGE_INTEGER InterruptTime;
  ULONG InterruptCount;
};

static FnNtQuerySystemInformation ResolveNtQuery() {
  static FnNtQuerySystemInformation pfn = nullptr;
  static bool s_resolved                = false;
  if (!s_resolved) {
    HMODULE hNtdll = GetModuleHandleW(kNtDll);
    if (hNtdll != nullptr) {
      pfn = reinterpret_cast<FnNtQuerySystemInformation>(
          GetProcAddress(hNtdll, "NtQuerySystemInformation"));
    }
    s_resolved = true;
  }
  return pfn;
}

DWORD GetLogicalProcessorCount() {
  SYSTEM_INFO si = {};

  // GetNativeSystemInfo is XP+ only. Resolve it dynamically so the import
  // table has no entry for it - a missing IAT entry crashes Win2K at load.
  typedef void(WINAPI * FnGetNativeSystemInfo)(LPSYSTEM_INFO);
  static FnGetNativeSystemInfo pfnGetNativeSystemInfo = nullptr;
  static bool s_resolved                              = false;
  if (!s_resolved) {
    HMODULE hKernel32      = GetModuleHandleW(kKernel32Dll);
    pfnGetNativeSystemInfo = reinterpret_cast<FnGetNativeSystemInfo>(
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

DWORD GetEffectiveThreadMax() {
  static const DWORD reported = GetLogicalProcessorCount();
  // Single-logical-CPU systems get an extra slot for the reasons in the
  // header comment: misreported HT/SMP and the small throughput win from
  // a +1 software thread under tight scheduling.
  DWORD chosen = reported;
  if (reported == 1) {
    chosen = 2;
  }
  return std::min(static_cast<DWORD>(kMaxNumThreads), chosen);
}

// Previous snapshot: zeroed at startup. s_prev_count == 0 means no snapshot
// taken yet; UpdateCpuStats saves the first snapshot and returns false.
static SysProcPerfInfo s_prev_cpus[256] = {};
static DWORD s_prev_count               = 0;

bool UpdateCpuStats(CpuStats* out) {
  if (out == nullptr) {
    return false;
  }
  FnNtQuerySystemInformation pfnNtQuery = ResolveNtQuery();
  if (pfnNtQuery == nullptr) {
    return false;
  }

  SysProcPerfInfo cur_cpus[256] = {};
  ULONG returned_bytes          = 0;
  LONG status = pfnNtQuery(kSysProcessorPerfInfo, cur_cpus, static_cast<ULONG>(sizeof(cur_cpus)),
                           &returned_bytes);
  if (status != 0 || returned_bytes < static_cast<ULONG>(sizeof(SysProcPerfInfo))) {
    return false;
  }
  const DWORD count = returned_bytes / static_cast<DWORD>(sizeof(SysProcPerfInfo));

  if (s_prev_count == 0) {
    memcpy(s_prev_cpus, cur_cpus, count * sizeof(SysProcPerfInfo));
    s_prev_count = count;
    return false;
  }

  const DWORD use_count = (count < s_prev_count) ? count : s_prev_count;

  LONGLONG sum_idle   = 0;
  LONGLONG sum_kernel = 0;
  LONGLONG sum_user   = 0;
  for (DWORD cpu_idx = 0; cpu_idx < use_count; ++cpu_idx) {
    sum_idle += cur_cpus[cpu_idx].IdleTime.QuadPart - s_prev_cpus[cpu_idx].IdleTime.QuadPart;
    sum_kernel += cur_cpus[cpu_idx].KernelTime.QuadPart - s_prev_cpus[cpu_idx].KernelTime.QuadPart;
    sum_user += cur_cpus[cpu_idx].UserTime.QuadPart - s_prev_cpus[cpu_idx].UserTime.QuadPart;
  }

  const LONGLONG total = sum_kernel + sum_user;
  if (total > 0) {
    const float inv = 100.0f / static_cast<float>(total);
    out->idle_pct   = static_cast<float>(sum_idle) * inv;
    out->kernel_pct = static_cast<float>(sum_kernel - sum_idle) * inv;
    out->user_pct   = static_cast<float>(sum_user) * inv;
    out->total_pct  = 100.0f - out->idle_pct;
    auto clamp100   = [](float v) { return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v); };
    out->idle_pct   = clamp100(out->idle_pct);
    out->kernel_pct = clamp100(out->kernel_pct);
    out->user_pct   = clamp100(out->user_pct);
    out->total_pct  = clamp100(out->total_pct);
  } else {
    out->idle_pct = out->user_pct = out->kernel_pct = out->total_pct = 0.0f;
  }

  memcpy(s_prev_cpus, cur_cpus, count * sizeof(SysProcPerfInfo));
  s_prev_count = count;
  return true;
}
