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
#include <vector>

#include <logging.h>

#include "constants.h"
#include "controls.h"
#include "strings.h"

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
  int a;
  int b;
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

void EmitLine(const std::wstring& msg) {
  // LOG sink is silent in release; SendOutputMessage always shows
  // (when the edit exists). Both are thread-safe: LOG has its own
  // mutex, SendMessageW marshals across thread boundaries.
  LOG(INFO) << msg;
  SendOutputMessage(msg);
}

inline bool StopRequested() {
  return g_stop_requested.load(std::memory_order_relaxed);
}

// Per-level progress counters. Sized to comfortably cover any
// reasonable start_depth (ceil(log2(kMaxNumThreads))). Reset to 0
// at the start of each calc via ResetMergeProgress.
constexpr int kMaxMergeLevels = 33;
std::atomic<int> g_combines_at_level[kMaxMergeLevels];
int g_start_depth      = 0;     // Set once per calc
DWORD g_calc_start_tick = 0;    // Captured by CalcThreadProc

void ResetMergeProgress(int start_depth) {
  g_start_depth = start_depth;
  for (int i = 0; i < kMaxMergeLevels; ++i) {
    g_combines_at_level[i].store(0, std::memory_order_relaxed);
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
  if (count == expected) {
    const DWORD elapsed_ms = GetTickCount() - g_calc_start_tick;
    std::wostringstream m;
    m << L"Merge level " << depth << L" of " << g_start_depth << L" done ("
      << expected << L" mpf multiply, " << (elapsed_ms / 1000.0) << L"s. elapsed)";
    EmitLine(m.str());
  }
}

void BinarySplitParallel(int a, int b, int depth, PQT* out);

DWORD WINAPI BSWorker(LPVOID lp) {
  BSArgs* args = static_cast<BSArgs*>(lp);
  BinarySplitParallel(args->a, args->b, args->depth, &args->result);
  return 0;
}

// sqrt(10005) needed for the final closed-form pi formula. Runs on
// its own worker so it can overlap with the BS instead of adding to
// the post-BS critical path.
struct SqrtArgs {
  mpf_class sqrt_result;  // Constructed at the calling thread's default precision.
};

DWORD WINAPI SqrtWorker(LPVOID lp) {
  SqrtArgs* args = static_cast<SqrtArgs*>(lp);
  mpf_sqrt_ui(args->sqrt_result.get_mpf_t(), 10005);
  return 0;
}

// One mpz multiplication on its own thread, used by Combine() to do the
// four products in a BS combine in parallel rather than sequentially.
struct MulArgs {
  const mpz_class* a;
  const mpz_class* b;
  mpz_class result;
};

DWORD WINAPI MulWorker(LPVOID lp) {
  MulArgs* args = static_cast<MulArgs*>(lp);
  args->result  = (*args->a) * (*args->b);
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
void Combine(const PQT& L, const PQT& R, PQT* out) {
  constexpr size_t kParallelMulThresholdLimbs = 8000;  // ~64KB at 8B/limb
  const bool parallelize =
      mpz_size(L.P.get_mpz_t()) > kParallelMulThresholdLimbs ||
      mpz_size(L.T.get_mpz_t()) > kParallelMulThresholdLimbs;
  if (!parallelize) {
    out->P = L.P * R.P;
    out->Q = L.Q * R.Q;
    out->T = R.Q * L.T + L.P * R.T;
    return;
  }
  MulArgs muls[3] = {
      {&L.P, &R.P, {}},  // P_out = L.P * R.P
      {&L.Q, &R.Q, {}},  // Q_out = L.Q * R.Q
      {&R.Q, &L.T, {}},  // T_left = R.Q * L.T
  };
  HANDLE handles[3] = {nullptr, nullptr, nullptr};
  for (int i = 0; i < 3; ++i) {
    handles[i] = CreateThread(nullptr, 0, MulWorker, &muls[i], 0, nullptr);
  }
  // Fourth mul on the current thread so we don't waste it blocking.
  mpz_class t_right = L.P * R.T;
  for (int i = 0; i < 3; ++i) {
    if (handles[i] == nullptr) {
      // CreateThread failed - do this one inline too.
      muls[i].result = (*muls[i].a) * (*muls[i].b);
    } else {
      WaitForSingleObject(handles[i], INFINITE);
      CloseHandle(handles[i]);
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
void BinarySplitParallel(int a, int b, int depth, PQT* out) {
  if (StopRequested()) {
    return;
  }
  if (b - a == 1) {
    if (a == 0) {
      out->P = 1;
      out->Q = 1;
      out->T = kChudA;
      return;
    }
    // Leaf for k >= 1:
    //   P_k = -(6k-5)(2k-1)(6k-1)
    //   Q_k = k^3 * C^3 / 24            (C = 640320)
    //   T_k = (A + B*k) * P_k
    out->P = mpz_class(2 * a - 1) * (6 * a - 1) * (6 * a - 5);
    out->P = -out->P;
    const mpz_class c = kChudC;
    out->Q = mpz_class(a) * a * a * c * c * c / 24;
    out->T = (mpz_class(a) * kChudB + kChudA) * out->P;
    return;
  }
  const int m = (a + b) / 2;
  PQT R;
  BSArgs left_args = {a, m, depth - 1, {}};
  HANDLE h         = nullptr;
  if (depth > 0) {
    h = CreateThread(nullptr, 0, BSWorker, &left_args, 0, nullptr);
  }
  if (h != nullptr) {
    // Left runs on the worker, right runs here. Both progress in
    // parallel; we rejoin via WaitForSingleObject before the combine.
    BinarySplitParallel(m, b, depth - 1, &R);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  } else {
    // Budget exhausted (or CreateThread failed): both subtrees inline.
    BinarySplitParallel(a, m, 0, &left_args.result);
    BinarySplitParallel(m, b, 0, &R);
  }
  // Standard BS combine, but with the four products parallelised on
  // large operands. See Combine() for the size threshold and rationale.
  //   P(a,b) = P(a,m) * P(m,b)
  //   Q(a,b) = Q(a,m) * Q(m,b)
  //   T(a,b) = Q(m,b) * T(a,m) + P(a,m) * T(m,b)
  Combine(left_args.result, R, out);
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

DWORD WINAPI CalcThreadProc(LPVOID lp) {
  CalcArgs* outer  = static_cast<CalcArgs*>(lp);
  const int digits = outer->digits;
  int threads      = outer->threads;
  delete outer;

  // Banner: "Started Calculating N digits (Threads: T)".
  std::wostringstream banner;
  banner << kCalculateMessage << digits << L" digits (Threads: " << threads << L")";
  EmitLine(banner.str());

  const DWORD t_start = GetTickCount();
  g_calc_start_tick   = t_start;

  // GMP precision: digits * log2(10) + guard bits.
  const long prec_bits = static_cast<long>(digits * 3.4) + 64;
  mpf_set_default_prec(prec_bits);

  // Kick off sqrt(10005) on its own worker. It needs the default mpf
  // precision we just set; by the time BS finishes the sqrt is
  // typically done too, so its 5-10s falls out of the post-BS
  // critical path. sqrt_args lives on the stack for the rest of the
  // function so the worker's writes stay valid until we read them.
  SqrtArgs sqrt_args;
  HANDLE sqrt_handle = CreateThread(nullptr, 0, SqrtWorker, &sqrt_args, 0, nullptr);

  int N = static_cast<int>(digits / kDigitsPerTerm) + 1;
  if (N < 1) {
    N = 1;
  }
  if (N < threads) {
    threads = N;  // No point allowing more threads than there are terms.
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

  // Progress beacon. The user sees this immediately after pressing
  // Calculate so the UI doesn't look frozen while the workers chew.
  PrintOutputSeparator();

  PQT total;
  BinarySplitParallel(0, N, depth, &total);

  // Rejoin the parallel sqrt before using it. If CreateThread had
  // failed at the top, do the sqrt inline now instead.
  if (sqrt_handle != nullptr) {
    WaitForSingleObject(sqrt_handle, INFINITE);
    CloseHandle(sqrt_handle);
  } else {
    mpf_sqrt_ui(sqrt_args.sqrt_result.get_mpf_t(), 10005);
  }

  if (StopRequested()) {
    std::wostringstream m;
    m << kStoppedMessage << digits << L" digits.";
    EmitLine(m.str());
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
    std::wostringstream m;
    m << L"Computed final Pi (mpf divide, " << (elapsed / 1000.0) << L"s. elapsed)";
    EmitLine(m.str());
  }

  if (StopRequested()) {
    std::wostringstream m;
    m << kStoppedMessage << digits << L" digits.";
    EmitLine(m.str());
    g_running = false;
    return 0;
  }

  // Result, iterations (= BS leaves used), and elapsed time.
  // Format pi to decimal. With the kMaxPrintNumDigits cap, FormatPi
  // only converts the digits we'll actually show - cheap. Without
  // the cap, this is the second single-threaded GMP hotspot (after
  // mpf_div above), hence the timing line.
  EmitLine(L"Formatting result to decimal.");
  const std::wstring outpi       = FormatPi(pi, digits, kMaxPrintNumDigits);
  // Emit final Pi output to hOutputEdit
  EmitLine(outpi);

  std::wostringstream iter_line;
  iter_line << kIterMessage << N;
  EmitLine(iter_line.str());

  const DWORD t_end     = GetTickCount();
  const DWORD elapsedMs = t_end - t_start;
  std::wostringstream time_line;
  time_line << kTimeMessage << (elapsedMs / 1000.0) << L" seconds elapsed";
  EmitLine(time_line.str());

  PrintOutputSeparator();

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
