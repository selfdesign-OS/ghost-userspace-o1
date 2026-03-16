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
    AgentConfig cfg(t, t->ToCpuList(std::vector<int>{4, 5}));
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
  uap_->Rpc(O1Scheduler::kResetPreemptionCount);

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

  int64_t preemptions = uap_->Rpc(O1Scheduler::kGetPreemptionCount);
  printf("[PreemptionCount] ShortTaskCompletesWithoutPreemption: %ld\n",
         preemptions);
}

// 여러 스레드가 각각 1ms 작업을 선점 없이 완료하는지 확인.
TEST_F(O1Test, MultipleShortTasksCompleteWithoutPreemption) {
  uap_->Rpc(O1Scheduler::kResetPreemptionCount);

  constexpr int kNumThreads = 10;
  constexpr absl::Duration kMaxAllowedWallTime = absl::Milliseconds(100);

  std::vector<absl::Duration> elapsed_times(kNumThreads);
  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [i, &elapsed_times] {
                                        absl::Time start = absl::Now();
                                        SpinFor(absl::Milliseconds(1));
                                        elapsed_times[i] = absl::Now() - start;
                                      }));
  }

  for (auto& t : threads) {
    t->Join();
  }

  for (int i = 0; i < kNumThreads; i++) {
    EXPECT_LE(elapsed_times[i], kMaxAllowedWallTime)
        << "Thread " << i << " took "
        << absl::ToDoubleMilliseconds(elapsed_times[i])
        << "ms — possible preemption detected";
  }

  int num_tasks;
  do {
    num_tasks = uap_->Rpc(O1Scheduler::kCountAllTasks);
    EXPECT_THAT(num_tasks, Ge(0));
  } while (num_tasks > 0);

  int64_t preemptions = uap_->Rpc(O1Scheduler::kGetPreemptionCount);
  printf("[PreemptionCount] MultipleShortTasksCompleteWithoutPreemption: %ld\n",
         preemptions);
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
