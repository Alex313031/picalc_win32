#ifndef PICALCWIN32_CPU_H_
#define PICALCWIN32_CPU_H_

#include "framework.h"

// Returns the number of logical processors on the system.
// Uses GetNativeSystemInfo on XP+ (correct under WOW64),
// falls back to GetSystemInfo on NT4/2000.
DWORD GetLogicalProcessorCount();

// Returns the upper bound on thread count the user is allowed to pick,
// applying both the hard cap (kMaxNumThreads) and the system's logical
// processor count, with one special case: when the OS reports exactly 1
// logical processor, the cap is raised to 2. Pentium 4 HT and legacy MPS
// Pentium 3 SMP systems sometimes underreport their thread count, and on
// any single-logical-core machine the +1 software thread tends to help
// throughput slightly (e.g. Ninja uses CPU+1 above 4 cores for the same
// scheduling reason). All call sites that gate user input on thread count
// (combobox filter, custom-input dialog cap) should use this rather than
// GetLogicalProcessorCount directly.
DWORD GetEffectiveThreadMax();

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
