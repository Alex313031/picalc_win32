// Main Pi Calculation functions, using GMP and the Chudnovsky series.
//
// One Chudnovsky term yields ~14.18 decimal digits, so for `digits`
// decimal digits we compute N = digits / 14.18 + 1 terms via binary
// splitting (BS). BS is done in BinarySplitParallel with a *depth
// budget*: at each non-leaf node, if depth > 0 we fork the left
// subtree onto a worker (passing depth-1) and run the right subtree
// inline (also at depth-1); if depth == 0 we run both subtrees
// inline. Starting depth = ceil(log2(threads)) means the recursion
// produces exactly `threads` leaf-level subtrees of N/threads terms
// each - perfectly balanced sizing, no race for thread slots.
// Combines happen as each thread joins its forked child, so the
// merge phase distributes across threads instead of serialising
// onto one. Once the root PQT is in hand the orchestrator
// (CalcThreadProc) applies the closed-form
//
//   pi = 426880 * sqrt(10005) * Q(0,N) / T(0,N)
//
// to land on a final mpf_class pi.
//
// Cancellation: every node in the BS recursion polls g_stop_requested,
// so a Stop click only has to wait for the *current* mpz multiply to
// finish before the threads unwind out of BS. Matches the SuperPi
// "stop within a second or two" feel.
//
// On top of the depth budget, each combine does its four mpz_muls in
// parallel above an operand-size threshold (see Combine). Even at the
// root - one combine, would otherwise be one thread - we get ~4-way
// parallelism out of the four independent products, so the heaviest
// merges no longer fully serialise.

#include "picalc.h"

#include <atomic>
#include <sstream>

#include <logging.h>

#include "constants.h"
#include "controls.h"
#include "resource.h"
#include "strings.h"
#include "utils.h"

namespace {

// =========================================================================
// Chudnovsky series constants
// =========================================================================

constexpr long kChudA   = 13591409L;
constexpr long kChudB   = 545140134L;
constexpr long kChudC   = 640320L;
constexpr long kPiCoeff = 426880L;  // pi = kPiCoeff * sqrt(10005) * Q / T

// Decimal digits gained per Chudnovsky term. Used to size N from a
// user-requested digit count.
constexpr double kDigitsPerTerm = 14.181647462725477;

// =========================================================================
// Worker state
// =========================================================================

// "Should the running calculation give up?" - set by StopCalculation,
// polled at every BS recursion node.
std::atomic<bool> g_stop_requested(false);

// Handle to the orchestrator thread. Leaked across completed
// calculations and closed on the next StartCalculation.
HANDLE g_calc_thread = nullptr;

// Per-node binary-splitting tuple. T already folds in the leaf's
// (A + B*k) factor, so the final formula needs Q and T only.
struct PQT {
  mpz_class P;
  mpz_class Q;
  mpz_class T;
};

struct BSArgs {
  int lo;
  int hi;
  int depth;
  PQT result;
};

struct CalcArgs {
  int digits;
  int threads;
};

// =========================================================================
// Helpers
// =========================================================================

inline bool StopRequested() {
  return g_stop_requested.load(std::memory_order_relaxed);
}

// Per-level progress counters. Sized to comfortably cover any
// reasonable start_depth (ceil(log2(kMaxNumThreads))). Reset to 0
// at the start of each calc via ResetMergeProgress.
constexpr int kMaxMergeLevels = 33;
std::atomic<int>  g_combines_at_level[kMaxMergeLevels];
std::atomic<DWORD> g_level_start_tick[kMaxMergeLevels]; // tick when first combine at level fires
int g_start_depth       = 0; // Set once per calc
int g_total_depth       = 0; // g_start_depth + 1
DWORD g_calc_start_tick = 0; // Captured by CalcThreadProc

void ResetMergeProgress(int start_depth) {
  g_start_depth = start_depth;
  g_total_depth = g_start_depth + 1;
  for (int level_idx = 0; level_idx < kMaxMergeLevels; ++level_idx) {
    g_combines_at_level[level_idx].store(0, std::memory_order_relaxed);
    g_level_start_tick[level_idx].store(0, std::memory_order_relaxed);
  }
}

// Called from BinarySplitParallel after each combine. When the last
// combine at a given merge level finishes, emits a progress line so
// the user sees the merge tree climbing toward the root and knows
// the calc isn't stuck.
void NoteCombineComplete(int depth) {
  // depth < 1 = inside a leaf subtree (too many to log usefully).
  if (depth < 1 || depth >= kMaxMergeLevels) {
    return;
  }
  const int count    = g_combines_at_level[depth].fetch_add(1, std::memory_order_relaxed) + 1;
  const int expected = 1 << (g_start_depth - depth);
  if (count == 1) {
    g_level_start_tick[depth].store(GetTickCount(), std::memory_order_release);
  }
  if (count == expected) {
    const DWORD level_ms = GetTickCount() - g_level_start_tick[depth].load(std::memory_order_acquire);
    std::wostringstream line;
    line << L"Merge level " << depth << L" of " << g_total_depth << L" done ("
         << expected << L" mpf combines, " << (level_ms / 1000.0) << L"s.)";
    EmitLine(line.str(), false);
  }
}

void BinarySplitParallel(int lo, int hi, int depth, PQT* out);

DWORD WINAPI BSWorker(LPVOID thread_param) {
  BSArgs* args = static_cast<BSArgs*>(thread_param);
  BinarySplitParallel(args->lo, args->hi, args->depth, &args->result);
  return 0;
}

// sqrt(10005) needed for the final closed-form pi formula. Runs on
// its own worker so it can overlap with the BS instead of adding to
// the post-BS critical path.
struct SqrtArgs {
  mpf_class sqrt_result;  // Constructed at the calling thread's default precision.
};

DWORD WINAPI SqrtWorker(LPVOID thread_param) {
  SqrtArgs* args = static_cast<SqrtArgs*>(thread_param);
  mpf_sqrt_ui(args->sqrt_result.get_mpf_t(), 10005);
  return 0;
}

// One mpz multiplication on its own thread, used by Combine() to do the
// four products in a BS combine in parallel rather than sequentially.
struct MulArgs {
  const mpz_class* lhs;
  const mpz_class* rhs;
  mpz_class result;
};

DWORD WINAPI MulWorker(LPVOID thread_param) {
  MulArgs* args = static_cast<MulArgs*>(thread_param);
  args->result  = (*args->lhs) * (*args->rhs);
  return 0;
}

// Combines two BS subtrees. GMP's mpz_mul is single-threaded, so on the
// large operands near the root of the merge tree the four products of
// a standard combine serialise onto one core. Above an operand-size
// threshold we run three of them on worker threads and the fourth
// inline, ~4x-ing the wall time of every heavy combine including the
// root.
//
// Below the threshold the products are cheap enough that thread
// creation overhead (~100us on Win32) would outweigh the savings, so
// we keep the original serial path for those.
void Combine(const PQT& left_pqt, const PQT& right_pqt, PQT* out) {
  constexpr size_t kParallelMulThresholdLimbs = 8000;  // ~64KB at 8B/limb
  const bool parallelize =
      mpz_size(left_pqt.P.get_mpz_t()) > kParallelMulThresholdLimbs ||
      mpz_size(left_pqt.T.get_mpz_t()) > kParallelMulThresholdLimbs;
  if (!parallelize) {
    out->P = left_pqt.P * right_pqt.P;
    out->Q = left_pqt.Q * right_pqt.Q;
    out->T = right_pqt.Q * left_pqt.T + left_pqt.P * right_pqt.T;
    return;
  }
  MulArgs muls[3] = {
      {&left_pqt.P, &right_pqt.P, {}},  // P_out = left_pqt.P * right_pqt.P
      {&left_pqt.Q, &right_pqt.Q, {}},  // Q_out = left_pqt.Q * right_pqt.Q
      {&right_pqt.Q, &left_pqt.T, {}},  // T_left = right_pqt.Q * left_pqt.T
  };
  HANDLE handles[3] = {nullptr, nullptr, nullptr};
  for (int mul_idx = 0; mul_idx < 3; ++mul_idx) {
    handles[mul_idx] = CreateThread(nullptr, 0, MulWorker, &muls[mul_idx], 0, nullptr);
  }
  // Fourth mul on the current thread so we don't waste it blocking.
  mpz_class t_right = left_pqt.P * right_pqt.T;
  for (int mul_idx = 0; mul_idx < 3; ++mul_idx) {
    if (handles[mul_idx] == nullptr) {
      // CreateThread failed - do this one inline too.
      muls[mul_idx].result = (*muls[mul_idx].lhs) * (*muls[mul_idx].rhs);
    } else {
      WaitForSingleObject(handles[mul_idx], INFINITE);
      CloseHandle(handles[mul_idx]);
    }
  }
  out->P = std::move(muls[0].result);
  out->Q = std::move(muls[1].result);
  out->T = std::move(muls[2].result) + std::move(t_right);
}

// Recursive binary splitting with a per-path *depth budget*. Each
// recursion path carries its own remaining fork count, so workers
// can keep subdividing all the way down to balanced N/2^depth leaf
// subtrees - no race for a shared concurrency counter, no degenerate
// case where one worker ends up running half the calculation alone.
//
// The stop check at the top of every call means cancellation reaches
// us at the next call boundary (i.e. after the current mpz multiply).
void BinarySplitParallel(int lo, int hi, int depth, PQT* out) {
  if (StopRequested()) {
    return;
  }
  if (hi - lo == 1) {
    if (lo == 0) {
      out->P = 1;
      out->Q = 1;
      out->T = kChudA;
      return;
    }
    // Leaf for k >= 1:
    //   P_k = -(6k-5)(2k-1)(6k-1)
    //   Q_k = k^3 * C^3 / 24            (C = 640320)
    //   T_k = (A + B*k) * P_k
    out->P = mpz_class(2 * lo - 1) * (6 * lo - 1) * (6 * lo - 5);
    out->P = -out->P;
    const mpz_class chud_c = kChudC;
    out->Q = mpz_class(lo) * lo * lo * chud_c * chud_c * chud_c / 24;
    out->T = (mpz_class(lo) * kChudB + kChudA) * out->P;
    return;
  }
  const int mid = (lo + hi) / 2;
  PQT right_pqt;
  BSArgs left_args       = {lo, mid, depth - 1, {}};
  HANDLE thread_handle   = nullptr;
  if (depth > 0) {
    thread_handle = CreateThread(nullptr, 0, BSWorker, &left_args, 0, nullptr);
  }
  if (thread_handle != nullptr) {
    // Left runs on the worker, right runs here. Both progress in
    // parallel; we rejoin via WaitForSingleObject before the combine.
    BinarySplitParallel(mid, hi, depth - 1, &right_pqt);
    WaitForSingleObject(thread_handle, INFINITE);
    CloseHandle(thread_handle);
  } else {
    // Budget exhausted (or CreateThread failed): both subtrees inline.
    BinarySplitParallel(lo, mid, 0, &left_args.result);
    BinarySplitParallel(mid, hi, 0, &right_pqt);
  }
  // Standard BS combine, but with the four products parallelised on
  // large operands. See Combine() for the size threshold and rationale.
  //   P(lo,hi) = P(lo,mid) * P(mid,hi)
  //   Q(lo,hi) = Q(lo,mid) * Q(mid,hi)
  //   T(lo,hi) = Q(mid,hi) * T(lo,mid) + P(lo,mid) * T(mid,hi)
  Combine(left_args.result, right_pqt, out);
  NoteCombineComplete(depth);
}

// Formats the final mpf pi as "3.14159...", clipped to `maxDigits` of
// fractional precision. Uses mpf_class::get_str so GMP's own allocator
// handles the C-string lifetime.
//
// When the result would be truncated for display, we only ask GMP to
// convert `maxDigits + 1` decimal digits in the first place - the
// binary-to-decimal conversion is O(M(n) log n) and single-threaded,
// so for a 50M-digit calc with maxDigits=100 this saves ~20-30s of
// work whose output the user can't see anyway. The full mpf stays in
// the caller's variable for a future .txt export path.
std::wstring FormatPi(const mpf_class& pi, int digits, int maxDigits) {
  const bool truncated = (digits > maxDigits);
  const size_t need    = truncated ? static_cast<size_t>(maxDigits) + 1
                                   : static_cast<size_t>(digits) + 1;

  mp_exp_t exp_out = 0;
  std::string raw  = pi.get_str(exp_out, 10, need);
  if (raw.empty()) {
    return std::wstring();
  }
  std::string formatted;
  if (exp_out >= 1) {
    // Place the decimal point after the first digit. For pi
    // exp_out is always 1; the >= guards against weird edge cases.
    formatted.assign(raw, 0, 1);
    formatted += '.';
    formatted += raw.substr(1);
  } else {
    // Defensive - shouldn't happen for pi.
    formatted = "0.";
    formatted += raw;
  }
  if (truncated) {
    formatted += "... (Open result file to see full value)";
  }
  // ASCII narrow -> wide. mpf_get_str only emits 0-9 / '-' / '.'.
  return std::wstring(formatted.begin(), formatted.end());
}

// =========================================================================
// Orchestrator
// =========================================================================

DWORD WINAPI CalcThreadProc(LPVOID thread_param) {
  CalcArgs* outer  = static_cast<CalcArgs*>(thread_param);
  const int digits = outer->digits;
  int threads      = outer->threads;
  delete outer;

  OpenResultFile();
  // Separator at top of results
  PrintOutputSeparator();
  WriteSeparatorToResultFile();

  // Timestamp line, then banner: "Started Calculating N digits (Threads: T)".
  SYSTEMTIME local_time;
  GetLocalTime(&local_time);
  const WORD   hour_12  = (local_time.wHour % 12 == 0) ? 12 : local_time.wHour % 12;
  const wchar_t* ampm = (local_time.wHour < 12) ? L"AM" : L"PM";
  wchar_t timestamp_buf[32];
  swprintf(timestamp_buf, 32, L"%02u/%02u/%02u %02u:%02u:%02u %ls",
           local_time.wMonth, local_time.wDay, local_time.wYear % 100,
           hour_12, local_time.wMinute, local_time.wSecond, ampm);
  EmitLine(timestamp_buf, false);
  WriteLineToResultFile(timestamp_buf);
  std::wostringstream banner;
  banner << kCalculateMessage << digits << L" digits (Threads: " << threads << L")";
  EmitLine(banner.str(), false);
  WriteLineToResultFile(banner.str());

  const DWORD t_start = GetTickCount();
  g_calc_start_tick   = t_start;

  // GMP precision: digits * log2(10) + guard bits.
  const long long prec_bits = static_cast<long>(digits * 3.4) + 64;
  mpf_set_default_prec(prec_bits);

  // Kick off sqrt(10005) on its own worker. It needs the default mpf
  // precision we just set; by the time BS finishes the sqrt is
  // typically done too, so its 5-10s falls out of the post-BS
  // critical path. sqrt_args lives on the stack for the rest of the
  // function so the worker's writes stay valid until we read them.
  SqrtArgs sqrt_args;
  HANDLE sqrt_handle = CreateThread(nullptr, 0, SqrtWorker, &sqrt_args, 0, nullptr);

  int num_terms = static_cast<int>(digits / kDigitsPerTerm) + 1;
  if (num_terms < 1) {
    num_terms = 1;
  }
  if (num_terms < threads) {
    threads = num_terms;  // No point allowing more threads than there are terms.
  }

  // Pick the smallest depth that gives us >= `threads` leaf subtrees.
  // depth = ceil(log2(threads)). For threads = 32 (5950X) -> 5, which
  // produces exactly 32 leaf subtrees of N/32 terms each. Non-power-
  // of-2 thread counts round up (6 threads -> depth=3 -> 8 leaves);
  // the OS scheduler handles the slight oversubscription fine.
  int depth = 0;
  while ((1 << depth) < threads) {
    ++depth;
  }
  ResetMergeProgress(depth);

  PQT total;
  BinarySplitParallel(0, num_terms, depth, &total);
  const DWORD t_bs_end = GetTickCount();

  // Rejoin the parallel sqrt. Log the wait (or inline cost) as its own
  // stage so every gap between t_start and t_end is accounted for.
  if (sqrt_handle != nullptr) {
    WaitForSingleObject(sqrt_handle, INFINITE);
    CloseHandle(sqrt_handle);
    const DWORD sqrt_wait_ms = GetTickCount() - t_bs_end;
    if (sqrt_wait_ms > 0) {
      std::wostringstream line;
      line << L"Parallel sqrt: " << (sqrt_wait_ms / 1000.0) << L"s.";
      EmitLine(line.str(), false);
    }
  } else {
    // CreateThread failed — run sqrt inline and log it as a stage.
    const DWORD sq_start = GetTickCount();
    mpf_sqrt_ui(sqrt_args.sqrt_result.get_mpf_t(), 10005);
    std::wostringstream line;
    line << L"sqrt(10005): " << ((GetTickCount() - sq_start) / 1000.0) << L"s.";
    EmitLine(line.str(), false);
  }

  if (StopRequested()) {
    std::wostringstream stop_line;
    stop_line << kStoppedMessage << digits << L" digits.";
    EmitLine(stop_line.str(), false);
    g_running = false;
    return 0;
  }

  // pi = kPiCoeff * sqrt(10005) * Q / T  (constants derived from
  // factoring 640320^(3/2) = 640320 * 8 * sqrt(10005)). The mpf_div
  // here is single-threaded and ~3-5x the cost of one mpf_mul on
  // huge precision; broken out with a timing line so the user knows
  // the calc is in the divide phase, not stuck.
  const DWORD mpf_start = GetTickCount();
  const mpf_class q_f(total.Q);
  const mpf_class t_f(total.T);
  const mpf_class pi = (sqrt_args.sqrt_result * q_f * kPiCoeff) / t_f;
  {
    const DWORD elapsed = GetTickCount() - mpf_start;
    std::wostringstream line;
    line << L"Merge level " << g_total_depth << L" of " << g_total_depth
         << L" done (1 mpf divide, " << (elapsed / 1000.0) << L"s.)";
    EmitLine(line.str(), false);
    WriteLineToResultFile(line.str());
  }
  // Capture t_end immediately after the pi value is computed, before any
  // EmitLine calls (which cross thread boundaries and add UI latency).
  // This makes the total time a clean sum of the logged stages.
  const DWORD t_end     = GetTickCount();
  const DWORD elapsedMs = t_end - t_start;

  if (StopRequested()) {
    std::wostringstream stop_line;
    stop_line << kStoppedMessage << digits << L" digits.";
    EmitLine(stop_line.str(), false);
    g_running = false;
    return 0;
  }

  std::wostringstream iter_line;
  iter_line << kIterMessage << num_terms;
  EmitLine(iter_line.str(), false);
  WriteLineToResultFile(iter_line.str());
  std::wostringstream time_line;
  time_line << kDoneMessage << (elapsedMs / 1000.0) << L" seconds elapsed.";
  EmitLine(time_line.str(), false);
  WriteLineToResultFile(time_line.str());
  // Result, iterations (= BS leaves used), and elapsed time.
  // The UI caps display at kMaxPrintNumDigits - FormatPi only converts
  // that many digits so the cheap path stays cheap. The file always
  // gets the full conversion; for large digit counts that is the second
  // single-threaded GMP hotspot (after mpf_div above), hence the
  // timing line.
  EmitLine(L"Formatting result to decimal...", false);
  WriteLineToResultFile(L"Formatting result to decimal...");
  const std::wstring outpi = FormatPi(pi, digits, kMaxPrintNumDigits);
  const std::wstring outpi_full = FormatPi(pi, digits, digits);
  // Emit truncated Pi to the output area / console.
  EmitLine(std::wstring(L"Result: ") + outpi, false);
  // Write full untruncated Pi to the results file.
  LOG(DEBUG) << L"Writing results to " << kResultsFile;
  WriteLineToResultFile(std::wstring(L"Result: ") + outpi_full);
  // Flush after the potentially huge pi result
  const bool done = FlushResultFile();

  // Completion chime. Gated on the Settings -> Sound? menu via
  // g_sound_on. SND_ASYNC kicks playback off on a system thread so
  // we don't block the worker. PlaySoundW is documented thread-safe;
  // the stop-check above ensures we don't chime on a cancelled run.
  if (g_sound_on && done) {
    if (!PlayWav(IDR_TADA_WAV)) {
      EmitLine(L"Failed to play tada.wav", true);
    }
  }

  g_running = false;
  return 0;
}

}  // namespace

// =========================================================================
// Public API
// =========================================================================

bool StartCalculation(int digits, int threads) {
  if (g_running) {
    return false;
  }
  if (digits <= 0 || threads <= 0) {
    SendOutputMessage(L"Invalid digits/threads selection.");
    return false;
  }
  if (static_cast<unsigned>(digits) < kMinNumDigits) {
    digits = static_cast<int>(kMinNumDigits);
  }
  if (static_cast<unsigned>(digits) > kMaxNumDigits) {
    digits = static_cast<int>(kMaxNumDigits);
  }
  if (static_cast<unsigned>(threads) < kMinNumThreads) {
    threads = static_cast<int>(kMinNumThreads);
  }
  if (static_cast<unsigned>(threads) > kMaxNumThreads) {
    threads = static_cast<int>(kMaxNumThreads);
  }
  // Clean up the previous thread's HANDLE before spawning a new one.
  if (g_calc_thread != nullptr) {
    CloseHandle(g_calc_thread);
    g_calc_thread = nullptr;
  }
  g_stop_requested.store(false);
  g_running = true;
  CalcArgs* args = new CalcArgs;
  args->digits   = digits;
  args->threads  = threads;
  g_calc_thread  = CreateThread(nullptr, 0, CalcThreadProc, args, 0, nullptr);
  if (g_calc_thread == nullptr) {
    delete args;
    g_running = false;
    return false;
  }
  return true;
}

void StopCalculation() {
  if (!g_running) {
    return;
  }
  g_stop_requested.store(true);
}
