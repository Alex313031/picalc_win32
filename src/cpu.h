#ifndef PICALCWIN32_CPU_H_
#define PICALCWIN32_CPU_H_

#include "framework.h"

// Returns the number of logical processors on the system.
// Uses GetNativeSystemInfo on XP+ (correct under WOW64),
// falls back to GetSystemInfo on NT4/2000.
DWORD GetLogicalProcessorCount();

#endif // PICALCWIN32_CPU_H_
