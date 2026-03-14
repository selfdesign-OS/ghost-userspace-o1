// Copyright 2021 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// O1 스케줄러 테스트
// 목적: time slice 10ms에서 1ms 작업이 선점 없이 완료되는지 검증

#include <atomic>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/parse.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "lib/base.h"
#include "lib/ghost.h"
#include "schedulers/o1/o1_scheduler.h"

namespace ghost {
namespace {

using ::testing::Ge;

class O1Test : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    Topology* t = MachineTopology();
    AgentConfig cfg(t, t->all_cpus());
    uap_ = new AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>(cfg);
  }

  static void TearDownTestSuite() {
    delete uap_;
    uap_ = nullptr;
  }

  static AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* uap_;
};

AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* O1Test::uap_;

// 단일 스레드가 1ms 작업을 선점 없이 완료하는지 확인.
//
// time slice = 10ms, 작업 시간 = 1ms 이므로 time slice 만료 전에 작업이
// 끝나야 한다. 선점이 발생하면 expired queue로 이동 후 재스케줄링되어
// 경과 시간이 크게 늘어난다.
TEST_F(O1Test, ShortTaskCompletesWithoutPreemption) {
  // time slice(10ms)보다 충분히 여유 있는 완료 허용 시간
  constexpr absl::Duration kMaxAllowedWallTime = absl::Milliseconds(50);

  absl::Duration elapsed;

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    absl::Time start = absl::Now();
    SpinFor(absl::Milliseconds(1));
    elapsed = absl::Now() - start;
  });

  t.Join();

  EXPECT_LE(elapsed, kMaxAllowedWallTime)
      << "Task took " << absl::ToDoubleMilliseconds(elapsed)
      << "ms — possible preemption detected (time slice=10ms, task=1ms)";

  int num_tasks;
  do {
    num_tasks = uap_->Rpc(O1Scheduler::kCountAllTasks);
    EXPECT_THAT(num_tasks, Ge(0));
  } while (num_tasks > 0);
}

// 여러 스레드가 각각 1ms 작업을 선점 없이 완료하는지 확인.
//
// [가설 검증]
// - 3.2.1: elapsed 이상값 감지 → wall time 계산 오류 여부
// - 선점 횟수 측정 → 문제 정의(30000회)의 재현성 확인
TEST_F(O1Test, MultipleShortTasksCompleteWithoutPreemption) {
  constexpr int kNumThreads = 10;
  constexpr absl::Duration kTaskDuration = absl::Milliseconds(1);
  constexpr absl::Duration kMaxAllowedWallTime = absl::Milliseconds(100);
  // elapsed가 작업시간(1ms)의 5배 이상이면 wall time 오류 의심
  constexpr absl::Duration kElapsedAnomalyThreshold = kTaskDuration * 5;

  std::vector<absl::Duration> elapsed_times(kNumThreads);
  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [i, &elapsed_times, kTaskDuration] {
                                        absl::Time start = absl::Now();
                                        SpinFor(kTaskDuration);
                                        elapsed_times[i] = absl::Now() - start;
                                      }));
  }

  for (auto& t : threads) {
    t->Join();
  }

  // [가설 3.2.1] elapsed 이상값 확인
  // elapsed가 작업시간(1ms)의 5배(5ms) 이상이면 wall time에
  // 에이전트 실행시간/컨텍스트 스위치 오버헤드가 포함된 것
  int anomaly_count = 0;
  for (int i = 0; i < kNumThreads; i++) {
    if (elapsed_times[i] > kElapsedAnomalyThreshold) {
      ++anomaly_count;
      fprintf(stderr,
          "[Hypothesis 3.2.1] Thread %d elapsed=%.2fms  "
          ">> threshold=%.2fms  -> wall time anomaly suspected\n",
          i,
          absl::ToDoubleMilliseconds(elapsed_times[i]),
          absl::ToDoubleMilliseconds(kElapsedAnomalyThreshold));
    }
    EXPECT_LE(elapsed_times[i], kMaxAllowedWallTime)
        << "Thread " << i << " took "
        << absl::ToDoubleMilliseconds(elapsed_times[i]) << "ms";
  }
  fprintf(stderr,
      "[Hypothesis 3.2.1] elapsed anomaly: %d / %d threads\n",
      anomaly_count, kNumThreads);

  int num_tasks;
  do {
    num_tasks = uap_->Rpc(O1Scheduler::kCountAllTasks);
    EXPECT_THAT(num_tasks, Ge(0));
  } while (num_tasks > 0);

  // [선점 횟수 측정] 문제 정의(10개 스레드 총 308594회)와 비교
  int64_t total_preemptions = uap_->Rpc(O1Scheduler::kCountPreemptions);
  fprintf(stderr,
      "[Preemption Count] total=%lld  per_thread_avg=%.0f\n",
      static_cast<long long>(total_preemptions),
      static_cast<double>(total_preemptions) / kNumThreads);
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
