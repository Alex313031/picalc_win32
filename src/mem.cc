// Functions to get the amount of RAM, pagefile usage, VM usage, etc.

#include "mem.h"

#include "utils.h"

// GlobalMemoryStatusEx is XP+; GlobalMemoryStatus is Win2K fallback.
// Both are in kernel32.dll — GlobalMemoryStatusEx is resolved dynamically
// so a missing IAT entry doesn't crash the Win2K loader.
typedef BOOL(WINAPI* FnGlobalMemoryStatusEx)(LPMEMORYSTATUSEX);

// NtQuerySystemInformation from ntdll.dll — NT-native, available on Win2K.
typedef LONG(WINAPI* FnNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

// Info class 18: per-pagefile size and usage (page counts).
static constexpr ULONG kSysPageFileInfo = 18;

// Info class 2: system performance counters (includes ResidentSystemCachePage).
static constexpr ULONG kSysPerfInfo = 2;

// Info class 21: system file cache stats.
static constexpr ULONG kSysFileCacheInfo = 21;

// Minimal layout for one NtQuerySystemInformation(18) entry.
// NextEntryOffset == 0 marks the last entry in the list.
// TotalSize / TotalInUse are in pages; multiply by dwPageSize to get bytes.
// A UNICODE_STRING + filename follows at offset 16; we don't need it.
struct SysPageFileEntry {
  ULONG NextEntryOffset;
  ULONG TotalSize;  // pages
  ULONG TotalInUse; // pages
  ULONG PeakUsage;  // pages
};

// Minimal Win2K-compatible layout (SIZE_T = 4 bytes in a 32-bit build).
struct SysFileCacheInfo {
  SIZE_T CurrentSize;
  SIZE_T PeakSize;
  ULONG PageFaultCount;
  SIZE_T MinimumWorkingSet;
  SIZE_T MaximumWorkingSet;
};

// Byte offset of ResidentSystemCachePage in SYSTEM_PERFORMANCE_INFORMATION.
// 4 LARGE_INTEGERs (32 bytes) + 33 ULONGs (132 bytes) = 164 = 0xA4.
static constexpr ULONG kResidentCachePageOffset = 164;

static FnGlobalMemoryStatusEx ResolveGlobalMemStatEx() {
  static FnGlobalMemoryStatusEx pfn = nullptr;
  static bool s_resolved            = false;
  if (!s_resolved) {
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32 != nullptr) {
      pfn = reinterpret_cast<FnGlobalMemoryStatusEx>(
          GetProcAddress(hKernel32, "GlobalMemoryStatusEx"));
    }
    s_resolved = true;
  }
  return pfn;
}

static FnNtQuerySystemInformation ResolveNtQuery() {
  static FnNtQuerySystemInformation pfn = nullptr;
  static bool s_resolved                = false;
  if (!s_resolved) {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll != nullptr) {
      pfn = reinterpret_cast<FnNtQuerySystemInformation>(
          GetProcAddress(hNtdll, "NtQuerySystemInformation"));
    }
    s_resolved = true;
  }
  return pfn;
}

static DWORD GetPageSize() {
  static DWORD s_page_size = 0;
  if (s_page_size == 0) {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    s_page_size = (si.dwPageSize > 0) ? si.dwPageSize : 4096u;
  }
  return s_page_size;
}

bool UpdateMemStats(MemStats* out) {
  if (out == nullptr) {
    return false;
  }
  *out        = {};
  bool any_ok = false;

  // -- RAM + commit charge via GlobalMemoryStatusEx (XP+) or GlobalMemoryStatus (Win2K) --
  FnGlobalMemoryStatusEx pfnEx = ResolveGlobalMemStatEx();
  if (pfnEx != nullptr) {
    MEMORYSTATUSEX ms = {};
    ms.dwLength       = sizeof(ms);
    if (pfnEx(&ms)) {
      out->ram_used  = ms.ullTotalPhys - ms.ullAvailPhys;
      out->ram_total = ms.ullTotalPhys;
      // vm_* = commit charge (RAM + pagefile backing for all committed pages).
      out->vm_used  = ms.ullTotalPageFile - ms.ullAvailPageFile;
      out->vm_limit = ms.ullTotalPageFile;
      any_ok        = true;
    }
  } else {
    // Win2K fallback: GlobalMemoryStatus (DWORD fields, 4 GB cap).
    MEMORYSTATUS ms = {};
    ms.dwLength     = sizeof(ms);
    GlobalMemoryStatus(&ms);
    out->ram_used  = static_cast<ULONGLONG>(ms.dwTotalPhys - ms.dwAvailPhys);
    out->ram_total = static_cast<ULONGLONG>(ms.dwTotalPhys);
    out->vm_used   = static_cast<ULONGLONG>(ms.dwTotalPageFile - ms.dwAvailPageFile);
    out->vm_limit  = static_cast<ULONGLONG>(ms.dwTotalPageFile);
    any_ok         = true;
  }

  // -- Pagefile size/usage + system cache via NtQuerySystemInformation --
  FnNtQuerySystemInformation pfnNtQuery = ResolveNtQuery();
  if (pfnNtQuery != nullptr) {
    // Actual pagefile(s): sum TotalSize and TotalInUse across all pagefiles.
    // 1 KB buffer handles ~10 pagefiles with typical path lengths.
    BYTE pf_buf[1024] = {};
    ULONG pf_returned = 0;
    LONG pf_status =
        pfnNtQuery(kSysPageFileInfo, pf_buf, static_cast<ULONG>(sizeof(pf_buf)), &pf_returned);
    if (pf_status == 0 && pf_returned >= static_cast<ULONG>(sizeof(SysPageFileEntry))) {
      const DWORD page_sz  = GetPageSize();
      ULONGLONG total_size = 0;
      ULONGLONG total_used = 0;
      const BYTE* ptr      = pf_buf;
      const BYTE* end      = pf_buf + pf_returned;
      while (ptr + sizeof(SysPageFileEntry) <= end) {
        const SysPageFileEntry* entry = reinterpret_cast<const SysPageFileEntry*>(ptr);
        total_size += static_cast<ULONGLONG>(entry->TotalSize) * page_sz;
        total_used += static_cast<ULONGLONG>(entry->TotalInUse) * page_sz;
        if (entry->NextEntryOffset == 0 || entry->NextEntryOffset < sizeof(SysPageFileEntry)) {
          break;
        }
        ptr += entry->NextEntryOffset;
      }
      out->pf_used  = total_used;
      out->pf_limit = total_size;
      any_ok        = true;
    }

    // Cache queries don't work on Wine — skip them so the display shows NaN.
    static const bool s_is_wine = IsRunningOnWine();
    if (!s_is_wine) {
      // Physically resident cache pages — matches Task Manager "System Cache".
      // SYSTEM_PERFORMANCE_INFORMATION is ~296-320 bytes depending on OS version;
      // NtQuerySI(2) returns STATUS_INFO_LENGTH_MISMATCH if the buffer is too small,
      // so use 512 bytes to cover all versions. Then read ResidentSystemCachePage
      // at offset 0xA4 from the returned data.
      BYTE perf_buf[512]  = {};
      ULONG perf_returned = 0;
      LONG perf_status =
          pfnNtQuery(kSysPerfInfo, perf_buf, static_cast<ULONG>(sizeof(perf_buf)), &perf_returned);
      if (perf_status == 0 &&
          perf_returned >= kResidentCachePageOffset + static_cast<ULONG>(sizeof(ULONG))) {
        ULONG resident_pages;
        memcpy(&resident_pages, perf_buf + kResidentCachePageOffset, sizeof(resident_pages));
        out->cache_bytes = static_cast<SIZE_T>(resident_pages) * GetPageSize();
        any_ok           = true;
      }

      // Cache peak and limit from class 21 (CurrentSize not used — see class 2 above).
      SysFileCacheInfo ci = {};
      ULONG returned      = 0;
      LONG status = pfnNtQuery(kSysFileCacheInfo, &ci, static_cast<ULONG>(sizeof(ci)), &returned);
      if (status == 0 && returned >= static_cast<ULONG>(2 * sizeof(SIZE_T))) {
        out->cache_peak  = ci.PeakSize;
        out->cache_limit = static_cast<ULONGLONG>(ci.MaximumWorkingSet);
        any_ok           = true;
      }
    }
  }

  return any_ok;
}
