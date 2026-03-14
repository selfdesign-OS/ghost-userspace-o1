// GHOST_DPRINT(absl::FPrintF) vs fprintf vs In-memory Ring Buffer 오버헤드 벤치마크
//
// 목적: 스케줄러 hot path에서 로깅 방식별 실제 오버헤드를 측정한다.
//       GHOST_DPRINT는 absl::FPrintF를 사용하므로 이를 함께 측정한다.
//
// 실행 방법:
//   bazel build //:logging_bench && sudo bazel-bin/logging_bench

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

// ── 측정 대상 1: fprintf ──────────────────────────────────────────────────

static void BenchFprintf(FILE* sink, int iterations,
                         std::vector<int64_t>& out_ns) {
  out_ns.resize(iterations);
  for (int i = 0; i < iterations; i++) {
    absl::Time t0 = absl::Now();
    fprintf(sink,
            "[Schedule] cpu=%-2d tid=%-6d remaining=%.2fms  elapsed=%.2fms\n",
            i % 8, 10000 + i, 9.87 - i * 0.001, 0.03);
    out_ns[i] = absl::ToInt64Nanoseconds(absl::Now() - t0);
  }
}

// ── 측정 대상 2: absl::FPrintF (실제 GHOST_DPRINT 내부 구현) ──────────────

static void BenchAbslFPrintF(FILE* sink, int iterations,
                              std::vector<int64_t>& out_ns) {
  out_ns.resize(iterations);
  for (int i = 0; i < iterations; i++) {
    absl::Time t0 = absl::Now();
    absl::FPrintF(sink,
                  "[Schedule] cpu=%-2d tid=%-6d remaining=%.2fms  elapsed=%.2fms\n",
                  i % 8, 10000 + i, 9.87 - i * 0.001, 0.03);
    out_ns[i] = absl::ToInt64Nanoseconds(absl::Now() - t0);
  }
}

// ── 측정 대상 3: In-memory Ring Buffer ────────────────────────────────────

struct RingEntry {
  int64_t ts_ns;
  char msg[96];
};

static constexpr int kRingSize = 1 << 17;  // 131072 entries
static RingEntry g_ring[kRingSize];
static std::atomic<uint32_t> g_ring_idx{0};

static inline void RingWrite(int cpu, int tid, double remaining,
                              double elapsed) {
  uint32_t idx = g_ring_idx.fetch_add(1, std::memory_order_relaxed)
                 & (kRingSize - 1);
  g_ring[idx].ts_ns = absl::ToUnixNanos(absl::Now());
  snprintf(g_ring[idx].msg, sizeof(g_ring[idx].msg),
           "[Schedule] cpu=%-2d tid=%-6d remaining=%.2fms  elapsed=%.2fms",
           cpu, tid, remaining, elapsed);
}

static void BenchRingBuffer(int iterations, std::vector<int64_t>& out_ns) {
  out_ns.resize(iterations);
  for (int i = 0; i < iterations; i++) {
    absl::Time t0 = absl::Now();
    RingWrite(i % 8, 10000 + i, 9.87 - i * 0.001, 0.03);
    out_ns[i] = absl::ToInt64Nanoseconds(absl::Now() - t0);
  }
}

// ── 통계 ──────────────────────────────────────────────────────────────────

struct Stats {
  double avg_ns, min_ns, p50_ns, p99_ns, max_ns;
};

static Stats ComputeStats(std::vector<int64_t>& samples) {
  std::sort(samples.begin(), samples.end());
  size_t n = samples.size();
  double sum = 0;
  for (int64_t v : samples) sum += v;
  return {sum / n,
          static_cast<double>(samples.front()),
          static_cast<double>(samples[n * 50 / 100]),
          static_cast<double>(samples[n * 99 / 100]),
          static_cast<double>(samples.back())};
}

static void PrintStats(const char* label, Stats& s) {
  fprintf(stdout,
          "  %-36s avg=%6.0fns  p50=%6.0fns  p99=%6.0fns  max=%6.0fns\n",
          label, s.avg_ns, s.p50_ns, s.p99_ns, s.max_ns);
}

// ── main ──────────────────────────────────────────────────────────────────

int main() {
  constexpr int kWarmup     = 1000;
  constexpr int kIterations = 50000;

  FILE* dev_null = fopen("/dev/null", "w");
  if (!dev_null) { perror("fopen /dev/null"); return 1; }

  fprintf(stdout, "\n=== Logging Overhead Benchmark ===\n");
  fprintf(stdout, "iterations: %d  (warmup: %d)\n\n", kIterations, kWarmup);

  // ── 워밍업 ──
  {
    std::vector<int64_t> dummy;
    BenchFprintf(dev_null, kWarmup, dummy);
    BenchAbslFPrintF(dev_null, kWarmup, dummy);
    BenchRingBuffer(kWarmup, dummy);
  }

  // ── 측정 ──
  std::vector<int64_t> s1, s2, s3, s4, s5;
  BenchFprintf(dev_null, kIterations, s1);
  BenchAbslFPrintF(dev_null, kIterations, s2);
  BenchFprintf(stderr, kIterations, s3);
  BenchAbslFPrintF(stderr, kIterations, s4);
  BenchRingBuffer(kIterations, s5);

  fclose(dev_null);

  Stats fs1 = ComputeStats(s1);
  Stats fs2 = ComputeStats(s2);
  Stats fs3 = ComputeStats(s3);
  Stats fs4 = ComputeStats(s4);
  Stats rs  = ComputeStats(s5);

  // ── 결과 출력 ──
  fprintf(stdout, "[결과]\n");
  PrintStats("fprintf        → /dev/null", fs1);
  PrintStats("absl::FPrintF  → /dev/null  (GHOST_DPRINT)", fs2);
  PrintStats("fprintf        → stderr(tty)", fs3);
  PrintStats("absl::FPrintF  → stderr(tty) (GHOST_DPRINT)", fs4);
  PrintStats("snprintf       → ring buffer", rs);

  fprintf(stdout,
          "\n[오버헤드 비율 vs ring buffer]\n"
          "  fprintf       /dev/null : %.1fx\n"
          "  absl::FPrintF /dev/null : %.1fx  ← GHOST_DPRINT 실제 조건\n"
          "  fprintf       tty       : %.1fx\n"
          "  absl::FPrintF tty       : %.1fx  ← 터미널 연결 시\n",
          fs1.avg_ns / rs.avg_ns,
          fs2.avg_ns / rs.avg_ns,
          fs3.avg_ns / rs.avg_ns,
          fs4.avg_ns / rs.avg_ns);

  fprintf(stdout,
          "\n[해석]\n"
          "  GHOST_DPRINT 1회 = %.0fns (absl::FPrintF, /dev/null 기준)\n"
          "  O1Schedule 1사이클(GHOST_DPRINT ~6회) = %.0fns\n"
          "  1ms 타임슬라이스 내 스케줄 사이클 최대 ≈ %.0f회\n",
          fs2.avg_ns,
          fs2.avg_ns * 6,
          1e6 / (fs2.avg_ns * 6));

  return 0;
}
