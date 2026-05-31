#ifndef PICALCWIN32_CPU_H_
#define PICALCWIN32_CPU_H_

#include "framework.h"

// NtQuerySystemInformation is NT-native, available on Win2K.
// Loaded dynamically to keep the import table clean (no IAT entry).
typedef LONG(WINAPI* FnNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

// Per-tick CPU utilization percentages, each clamped to [0.0, 100.0].
// kernel_pct is exclusive of idle (idle is broken out separately), which
// matches the three-way breakdown Task Manager shows on XP.
struct CpuStats {
  float idle_pct;
  float user_pct;
  float kernel_pct;
  float total_pct;
};

// Returns the number of logical processors on the system.
// Uses GetNativeSystemInfo on XP+ (correct under WOW64),
// falls back to GetSystemInfo on NT4/2000.
DWORD GetLogicalProcessorCount();

// Returns the ceiling on thread count for the dropdown picker (the threads
// combobox), applying both the hard cap (kMaxNumThreads) and the system's
// logical processor count, with one special case: when the OS reports
// exactly 1 logical processor, the cap is raised to 2. Pentium 4 HT and
// legacy MPS Pentium 3 SMP systems sometimes underreport their thread
// count, and on any single-logical-core machine the +1 software thread
// tends to help throughput slightly (e.g. Ninja uses CPU+1 above 4 cores
// for the same scheduling reason). Use this for the combobox filter only;
// the Custom-input dialog deliberately bypasses this and allows the full
// kMaxNumThreads so users can override for testing or misreporting CPUs.
DWORD GetEffectiveThreadMax();

// Queries NtQuerySystemInformation(SystemProcessorPerformanceInformation),
// sums per-CPU IdleTime / KernelTime / UserTime deltas across all logical
// processors, and writes the four percentage results into *out.
// Returns false on the first call (no prior snapshot yet) or on failure;
// *out is not modified on false. Subsequent calls compute true deltas.
bool UpdateCpuStats(CpuStats* out);

#endif // PICALCWIN32_CPU_H_
