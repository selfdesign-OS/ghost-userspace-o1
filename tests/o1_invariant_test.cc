// Copyright 2021 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// O1 스케줄러 불변식 검증 테스트
//
// 목적: CpuTick이 활성화된 상태에서 스케줄러 불변식이 유지되는지 검증한다.
// 불변식은 o1_scheduler.cc의 DCHECK로 구현되어 있으며, 디버그 빌드(-c dbg)에서만
// 위반 시 프로세스를 abort시킨다.
//
// 검증 대상 불변식:
//   INV-1: cs->current != nullptr → run_state == kOnCpu
//   INV-2: cs->current != nullptr → task->cpu == cpu.id()
//   INV-3: TaskOffCpu 진입 시 task->oncpu() (from_switchto 제외)
//   INV-4: TaskOnCpu 진입 시 task->_runnable() || task->queued()
//   INV-5: TaskOffCpu 진입 시 cs->current == task
//
// 실행 방법 (디버그 빌드 필수):
//   sudo bazel run -c dbg //:o1_invariant_test
//
// 테스트 통과 기준:
//   DCHECK가 한 번도 abort하지 않고 모든 태스크가 완료됨.
//   → 불변식이 모든 실행 구간에서 유지됨을 의미한다.
//
// 테스트 실패(abort) 기준:
//   DCHECK 메시지 출력 후 프로세스 종료.
//   → 불변식이 깨진 위치(tid, state, cpu)가 stderr에 출력됨.

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

class O1InvariantTest : public testing::Test {
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

  // 모든 ghost 태스크가 사라질 때까지 대기.
  // timeout 내에 완료되지 않으면 테스트 실패.
  static void WaitForAllTasksDone(absl::Duration timeout) {
    absl::Time deadline = absl::Now() + timeout;
    int num_tasks;
    do {
      num_tasks = uap_->Rpc(O1Scheduler::kCountAllTasks);
      ASSERT_GE(num_tasks, 0);
      if (absl::Now() > deadline) {
        FAIL() << "태스크가 " << absl::ToDoubleMilliseconds(timeout)
               << "ms 내에 완료되지 않음 (남은 태스크: " << num_tasks << ")"
               << " — 스케줄러 hang 의심";
      }
    } while (num_tasks > 0);
  }

  static AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* uap_;
};

AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* O1InvariantTest::uap_;

// [INV-1/INV-2 기준선]
// 태스크가 CpuTick(4ms) 전에 끝나는 시나리오.
// preempt_curr이 설정되지 않으므로 선점 경로를 통과하지 않는다.
// 이 테스트가 통과해야 이후 테스트의 실패가 선점 경로 버그임을 확신할 수 있다.
TEST_F(O1InvariantTest, BaselineShortTask) {
  absl::Duration elapsed;

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    absl::Time start = absl::Now();
    SpinFor(absl::Milliseconds(1));
    elapsed = absl::Now() - start;
  });
  t.Join();

  EXPECT_LE(elapsed, absl::Milliseconds(50))
      << "1ms 태스크가 50ms 초과 — CpuTick 없이도 스케줄러 이상";

  WaitForAllTasksDone(absl::Milliseconds(500));
}

// [INV-3/INV-5 핵심 검증]
// 태스크를 timeslice(10ms)를 넘는 30ms 동안 실행한다.
// HZ=250 환경에서 4ms tick × 3회 후 timeslice 소진 → preempt_curr=true.
//
// 이 경로:
//   CpuTick → CheckPreemptTick → preempt_curr=true
//   → O1Schedule preempt_curr 분기 실행
//   → TaskOffCpu 호출 (INV-3/INV-5 검사)
//   → MSG_TASK_PREEMPTED → TaskPreempted → TaskOffCpu 재진입 시도
//
// 현재 코드의 버그(이중 호출)가 있다면 INV-3/INV-5 DCHECK에서 abort된다.
TEST_F(O1InvariantTest, CpuTickPreemptionInvariant) {
  constexpr absl::Duration kTaskDuration = absl::Milliseconds(30);

  GhostThread t(GhostThread::KernelScheduler::kGhost,
                [&] { SpinFor(kTaskDuration); });
  t.Join();

  // 30ms > 10ms timeslice → 최소 1회 이상 선점이 발생해야 함
  int64_t preemptions = uap_->Rpc(O1Scheduler::kCountPreemptions);
  EXPECT_GT(preemptions, 0)
      << "30ms 태스크에서 선점이 발생하지 않음 — CpuTick 활성화 여부 확인 필요";

  fprintf(stderr, "[CpuTickPreemptionInvariant] preemptions=%lld\n",
          static_cast<long long>(preemptions));

  WaitForAllTasksDone(absl::Milliseconds(500));
}

// [INV-3/INV-5 동시성 검증]
// 여러 CPU에서 동시에 CpuTick 선점 경로를 통과할 때
// 각 CPU 독립적으로 불변식이 유지되는지 확인한다.
TEST_F(O1InvariantTest, ConcurrentCpuTickPreemptionInvariant) {
  constexpr int kNumThreads = 4;
  constexpr absl::Duration kTaskDuration = absl::Milliseconds(25);

  int64_t preemptions_before = uap_->Rpc(O1Scheduler::kCountPreemptions);

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [] { SpinFor(absl::Milliseconds(25)); }));
  }
  for (auto& th : threads) th->Join();

  int64_t new_preemptions =
      uap_->Rpc(O1Scheduler::kCountPreemptions) - preemptions_before;

  EXPECT_GT(new_preemptions, 0)
      << kNumThreads << "개 태스크(각 "
      << absl::ToInt64Milliseconds(kTaskDuration)
      << "ms)에서 선점이 전혀 발생하지 않음";

  fprintf(stderr,
          "[ConcurrentCpuTickPreemptionInvariant] "
          "threads=%d  new_preemptions=%lld\n",
          kNumThreads, static_cast<long long>(new_preemptions));

  WaitForAllTasksDone(absl::Milliseconds(500));
}

// [INV-4 검증]
// 태스크를 빠르게 생성/종료하여 TaskOnCpu가 잘못된 상태의 태스크를
// CPU에 올리는 경우(kBlocked/kOnCpu 상태로 커밋)가 없는지 확인한다.
TEST_F(O1InvariantTest, TaskOnCpuStateInvariant) {
  constexpr int kNumThreads = 8;

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [] { SpinFor(absl::Milliseconds(5)); }));
  }
  for (auto& th : threads) th->Join();

  WaitForAllTasksDone(absl::Milliseconds(500));
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
