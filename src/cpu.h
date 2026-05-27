#ifndef PICALCWIN32_CPU_H_
#define PICALCWIN32_CPU_H_

#include "framework.h"

// Returns the number of logical processors on the system.
// Uses GetNativeSystemInfo on XP+ (correct under WOW64),
// falls back to GetSystemInfo on NT4/2000.
DWORD GetLogicalProcessorCount();

// Per-tick CPU utilization percentages, each clamped to [0.0, 100.0].
// kernel_pct is exclusive of idle (idle is broken out separately), which
// matches the three-way breakdown Task Manager shows on XP.
struct CpuStats {
  float idle_pct;
  float user_pct;
  float kernel_pct;
  float total_pct;
};

// Queries NtQuerySystemInformation(SystemProcessorPerformanceInformation),
// sums per-CPU IdleTime / KernelTime / UserTime deltas across all logical
// processors, and writes the four percentage results into *out.
// Returns false on the first call (no prior snapshot yet) or on failure;
// *out is not modified on false. Subsequent calls compute true deltas.
bool UpdateCpuStats(CpuStats* out);

#endif // PICALCWIN32_CPU_H_
