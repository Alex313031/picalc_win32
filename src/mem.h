#ifndef PICALCWIN32_MEM_H_
#define PICALCWIN32_MEM_H_

#include "framework.h"

// Snapshot of system memory state, produced by UpdateMemStats on each tick.
// All byte fields are ULONGLONG so they hold 64-bit values from GlobalMemoryStatusEx.
struct MemStats {
  ULONGLONG ram_used;    // physical memory in use (bytes)
  ULONGLONG ram_total;   // total physical memory installed (bytes)
  ULONGLONG pf_used;     // commit charge in use - matches Task Manager "Page File" bar (bytes)
  ULONGLONG pf_limit;    // commit limit: RAM + pagefile combined (bytes)
  ULONGLONG vm_used;     // same as pf_used (commit charge = virtual memory committed)
  ULONGLONG vm_limit;    // same as pf_limit (commit limit)
  SIZE_T cache_bytes;    // system file cache current working-set size (bytes)
  SIZE_T cache_peak;     // system file cache peak working-set size (bytes)
  ULONGLONG cache_limit; // MaximumWorkingSet from SYSTEM_FILECACHE_INFORMATION;
                         // equals (ULONGLONG)(SIZE_T)-1 when OS-managed (no fixed cap)
};

// Populates *out with the current memory snapshot.
// Uses GlobalMemoryStatusEx (XP+, dynamic) with GlobalMemoryStatus (Win2K) fallback
// for RAM/commit data, and NtQuerySystemInformation(SystemFileCacheInformation)
// for the system cache. Returns false if all queries fail; *out is then zeroed.
bool UpdateMemStats(MemStats* out);

#endif // PICALCWIN32_MEM_H_
