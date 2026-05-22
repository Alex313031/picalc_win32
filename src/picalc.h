#ifndef PICALCWIN32_PICALC_H_
#define PICALCWIN32_PICALC_H_

// Keep this at the top!
#include "framework.h"
#include "globals.h"

// Should be the only place GMP headers are included
// clang-format off
#include <gmp.h>
#include <gmpxx.h>
//clang-format on

// Kicks off a background calculation of pi to `digits` decimal digits
// using the Chudnovsky series with binary splitting, parallelised
// across `threads` Win32 worker threads. Output (start banner, result,
// done banner) is routed through both LOG(INFO) and SendOutputMessage
// so it appears in the console (when --debug) and the output edit.
//
// Values exceeding kMaxNumDigits / kMaxNumThreads are clamped down to
// those caps. Returns false if a calculation is already in progress,
// the args are invalid, or CreateThread failed.
bool StartCalculation(int digits, int threads);

// Signals the running calculation to abort at the next BS checkpoint.
// No-op if nothing is running. Stop is best-effort: the worker still
// has to finish whatever mpz multiply it's currently inside before
// the next g_stop_requested poll, so there can be a delay between
// click and "Stopped Calculating ..." landing in the output.
void StopCalculation();

#endif // PICALCWIN32_PICALC_H_
