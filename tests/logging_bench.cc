// 로깅 방식별 오버헤드 벤치마크
//
// 측정 대상:
//   1. fprintf        → /dev/null
//   2. absl::FPrintF  → /dev/null  (GHOST_DPRINT 실제 구현)
//   3. fprintf        → stderr(tty)
//   4. absl::FPrintF  → stderr(tty)
//   5. 전역 ring buffer  (atomic fetch_add)
//   6. per-CPU ring buffer (atomic 없음, 단일 소유권)
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

// ── 측정 대상 1/3: fprintf ────────────────────────────────────────────────

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

// ── 측정 대상 2/4: absl::FPrintF (실제 GHOST_DPRINT 내부 구현) ───────────

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

// ── 측정 대상 5: 전역 ring buffer (atomic fetch_add) ─────────────────────
// 여러 CPU의 에이전트가 하나의 버퍼를 공유할 때의 비용

struct RingEntry {
  int64_t ts_ns;
  char msg[96];
};

static constexpr int kGlobalRingSize = 1 << 17;  // 131072
static RingEntry g_global_ring[kGlobalRingSize];
static std::atomic<uint32_t> g_global_idx{0};

static void BenchGlobalRingBuffer(int iterations, std::vector<int64_t>& out_ns) {
  out_ns.resize(iterations);
  for (int i = 0; i < iterations; i++) {
    absl::Time t0 = absl::Now();
    uint32_t idx = g_global_idx.fetch_add(1, std::memory_order_relaxed)
                   & (kGlobalRingSize - 1);
    g_global_ring[idx].ts_ns = absl::ToUnixNanos(absl::Now());
    snprintf(g_global_ring[idx].msg, sizeof(g_global_ring[idx].msg),
             "[Schedule] cpu=%-2d tid=%-6d remaining=%.2fms  elapsed=%.2fms",
             i % 8, 10000 + i, 9.87 - i * 0.001, 0.03);
    out_ns[i] = absl::ToInt64Nanoseconds(absl::Now() - t0);
  }
}

// ── 측정 대상 6: per-CPU ring buffer (atomic 없음) ────────────────────────
// ghOSt per-CPU 에이전트 구조: 각 CPU의 에이전트만 자신의 버퍼에 씀
// → 동일 CPU에서 단일 소유권 보장, atomic 불필요
//
// O1Scheduler::CpuState에 아래 구조를 추가하는 설계를 반영:
//   struct CpuState {
//     ...
//     CpuTraceBuffer trace;  // per-CPU, lock-free
//   };

static constexpr int kCpuRingSize = 1 << 14;  // 16384 per CPU
static constexpr int kMaxCpus = 8;

struct CpuTraceBuffer {
  RingEntry entries[kCpuRingSize];
  uint32_t idx = 0;  // atomic 불필요: 해당 CPU 에이전트만 접근
} ABSL_CACHELINE_ALIGNED;  // false sharing 방지

static CpuTraceBuffer g_cpu_rings[kMaxCpus];

// cpu_id: 호출하는 에이전트의 CPU 번호 (항상 동일한 CPU에서만 호출)
static inline void CpuRingWrite(int cpu_id, int tid, double remaining,
                                 double elapsed) {
  CpuTraceBuffer& buf = g_cpu_rings[cpu_id % kMaxCpus];
  uint32_t idx = buf.idx++ & (kCpuRingSize - 1);
  buf.entries[idx].ts_ns = absl::ToUnixNanos(absl::Now());
  snprintf(buf.entries[idx].msg, sizeof(buf.entries[idx].msg),
           "[Schedule] cpu=%-2d tid=%-6d remaining=%.2fms  elapsed=%.2fms",
           cpu_id, tid, remaining, elapsed);
}

static void BenchCpuRingBuffer(int cpu_id, int iterations,
                                std::vector<int64_t>& out_ns) {
  out_ns.resize(iterations);
  for (int i = 0; i < iterations; i++) {
    absl::Time t0 = absl::Now();
    CpuRingWrite(cpu_id, 10000 + i, 9.87 - i * 0.001, 0.03);
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

static void PrintStats(const char* label, Stats& s, double ring_avg) {
  fprintf(stdout,
          "  %-42s avg=%6.0fns  p50=%6.0fns  p99=%6.0fns  (%4.1fx)\n",
          label, s.avg_ns, s.p50_ns, s.p99_ns, s.avg_ns / ring_avg);
}

// ── main ──────────────────────────────────────────────────────────────────

int main() {
  constexpr int kWarmup     = 1000;
  constexpr int kIterations = 50000;
  constexpr int kBenchCpu   = 2;  // 벤치마크에서 사용할 가상 CPU 번호

  FILE* dev_null = fopen("/dev/null", "w");
  if (!dev_null) { perror("fopen /dev/null"); return 1; }

  fprintf(stdout, "\n=== Logging Overhead Benchmark ===\n");
  fprintf(stdout, "iterations: %d  (warmup: %d)\n\n", kIterations, kWarmup);

  // ── 워밍업 ──
  {
    std::vector<int64_t> dummy;
    BenchFprintf(dev_null, kWarmup, dummy);
    BenchAbslFPrintF(dev_null, kWarmup, dummy);
    BenchGlobalRingBuffer(kWarmup, dummy);
    BenchCpuRingBuffer(kBenchCpu, kWarmup, dummy);
  }

  // ── 측정 ──
  std::vector<int64_t> s1, s2, s3, s4, s5, s6;
  BenchFprintf(dev_null,    kIterations, s1);
  BenchAbslFPrintF(dev_null, kIterations, s2);
  BenchFprintf(stderr,      kIterations, s3);
  BenchAbslFPrintF(stderr,  kIterations, s4);
  BenchGlobalRingBuffer(    kIterations, s5);
  BenchCpuRingBuffer(kBenchCpu, kIterations, s6);

  fclose(dev_null);

  Stats st1 = ComputeStats(s1);
  Stats st2 = ComputeStats(s2);
  Stats st3 = ComputeStats(s3);
  Stats st4 = ComputeStats(s4);
  Stats st5 = ComputeStats(s5);
  Stats st6 = ComputeStats(s6);

  // per-CPU ring buffer를 기준(1.0x)으로 배율 표시
  double base = st6.avg_ns;

  fprintf(stdout, "[결과]  (배율 기준: per-CPU ring buffer)\n");
  PrintStats("fprintf        → /dev/null",          st1, base);
  PrintStats("absl::FPrintF  → /dev/null (GHOST_DPRINT)", st2, base);
  PrintStats("fprintf        → stderr(tty)",         st3, base);
  PrintStats("absl::FPrintF  → stderr(tty)",          st4, base);
  PrintStats("전역 ring buffer  (atomic fetch_add)", st5, base);
  PrintStats("per-CPU ring buffer (atomic 없음) ★",  st6, base);

  fprintf(stdout,
          "\n[per-CPU ring buffer vs 전역 ring buffer]\n"
          "  전역: avg=%.0fns  per-CPU: avg=%.0fns  → %.1fx 빠름\n",
          st5.avg_ns, st6.avg_ns, st5.avg_ns / st6.avg_ns);

  fprintf(stdout,
          "\n[스케줄러 적용 시 예상 효과]\n"
          "  현재 GHOST_DPRINT(absl::FPrintF) 6회/사이클 = %.0fns\n"
          "  per-CPU ring buffer 교체 후 6회/사이클     = %.0fns\n"
          "  사이클당 절약 시간                          = %.0fns (%.1fx 개선)\n",
          st2.avg_ns * 6,
          st6.avg_ns * 6,
          st2.avg_ns * 6 - st6.avg_ns * 6,
          st2.avg_ns / st6.avg_ns);

  return 0;
}
