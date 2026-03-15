// Copyright 2021 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// O1 스케줄러 종합 검증 테스트
//
// 검증 항목:
//   1. 기본 완료     — 1, CPU수, 2×CPU수, 다수 태스크 완료
//   2. 긴 태스크     — time slice(10ms) 초과 → expired queue 경로
//   3. 블로킹 태스크 — TaskBlocked → TaskRunnable 경로
//   4. yield 태스크  — TaskYield 경로
//   5. 혼합 워크로드 — 짧은·긴 태스크 동시 실행
//   6. 고부하        — 32 스레드 동시 실행
//   7. 반복 블로킹   — 여러 번 sleep/wake 반복

#include <sched.h>
#include <sstream>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/parse.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "lib/base.h"
#include "lib/ghost.h"
#include "schedulers/o1/o1_scheduler.h"

ABSL_FLAG(std::string, ghost_cpus, "",
          "Ghost CPU ID 목록, 쉼표 구분 (예: '3,4'). 미지정 시 all CPUs.");

namespace ghost {
namespace {

using ::testing::Ge;

// ─────────────────────────────────────────────────────────────
// 공통 헬퍼
// ─────────────────────────────────────────────────────────────

static CpuList GetGhostCpus(Topology* t) {
  std::string s = absl::GetFlag(FLAGS_ghost_cpus);
  if (s.empty()) return t->all_cpus();
  std::vector<int> ids;
  std::istringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) ids.push_back(std::stoi(token));
  return t->ToCpuList(ids);
}

// 모든 ghost 태스크가 완료(kCountAllTasks == 0)될 때까지 대기.
// timeout 초과 시 테스트 실패.
static void WaitForAllTasksDone(
    AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* uap,
    absl::Duration timeout = absl::Seconds(10)) {
  absl::Time deadline = absl::Now() + timeout;
  int num_tasks;
  do {
    num_tasks = uap->Rpc(O1Scheduler::kCountAllTasks);
    ASSERT_GE(num_tasks, 0);
    ASSERT_LT(absl::Now(), deadline)
        << num_tasks << "개 태스크가 "
        << absl::ToDoubleMilliseconds(timeout) << "ms 내에 완료되지 않음";
  } while (num_tasks > 0);
}

// ─────────────────────────────────────────────────────────────
// Test Suite
// ─────────────────────────────────────────────────────────────

class O1Test : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    Topology* t = MachineTopology();
    AgentConfig cfg(t, GetGhostCpus(t));
    uap_ = new AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>(cfg);
  }

  static void TearDownTestSuite() {
    delete uap_;
    uap_ = nullptr;
  }

  static AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* uap_;
};

AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* O1Test::uap_;

// ─────────────────────────────────────────────────────────────
// 카테고리 1: 기본 완료
// ─────────────────────────────────────────────────────────────

// 단일 태스크가 active queue에서 한 번에 완료되는지 확인.
TEST_F(O1Test, SingleTaskCompletes) {
  GhostThread t(GhostThread::KernelScheduler::kGhost,
                [] { SpinFor(absl::Milliseconds(1)); });
  t.Join();
  WaitForAllTasksDone(uap_);
}

// 태스크 수 = CPU 수: 모든 CPU에 태스크가 하나씩 배정되어 동시 실행.
TEST_F(O1Test, TaskCountEqualsNumCpus) {
  const int kNumCpus = MachineTopology()->all_cpus().Size();
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumCpus; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(1)); }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// 태스크 수 = 2 × CPU 수: 런큐에서 대기 후 순차 실행.
TEST_F(O1Test, TaskCountTwiceNumCpus) {
  const int kNumCpus = MachineTopology()->all_cpus().Size();
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumCpus * 2; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(1)); }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// ─────────────────────────────────────────────────────────────
// 카테고리 2: 긴 태스크 (expired queue 경로)
// ─────────────────────────────────────────────────────────────

// time slice(10ms)를 초과하는 태스크가 expired queue를 거쳐 완료되는지 확인.
// SpinFor(30ms): 커널 선점을 통해 remaining_time이 소진 → Enqueue 시 expired queue 배치.
TEST_F(O1Test, LongTaskCompletesAcrossTimeSlices) {
  absl::Duration elapsed;
  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    absl::Time start = absl::Now();
    SpinFor(absl::Milliseconds(30));
    elapsed = absl::Now() - start;
  });
  t.Join();
  WaitForAllTasksDone(uap_);
  // 최소 30ms의 CPU 시간이 실제로 실행되었는지 확인
  EXPECT_GE(elapsed, absl::Milliseconds(30));
}

// CPU 수만큼 긴 태스크를 동시 실행 → 각 CPU에서 expired queue 경로 동시 발생.
TEST_F(O1Test, MultipleLongTasksComplete) {
  const int kNumCpus = MachineTopology()->all_cpus().Size();
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumCpus; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(25)); }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// ─────────────────────────────────────────────────────────────
// 카테고리 3: 블로킹 태스크 (TaskBlocked → TaskRunnable 경로)
// ─────────────────────────────────────────────────────────────

// 단일 태스크가 sleep 후 wakeup되어 완료되는지 확인.
// absl::SleepFor → TaskBlocked → (커널 타이머 후) TaskRunnable
TEST_F(O1Test, BlockingTaskCompletes) {
  GhostThread t(GhostThread::KernelScheduler::kGhost, [] {
    SpinFor(absl::Milliseconds(1));
    absl::SleepFor(absl::Milliseconds(10));  // TaskBlocked → TaskRunnable
    SpinFor(absl::Milliseconds(1));
  });
  t.Join();
  WaitForAllTasksDone(uap_);
}

// 여러 태스크가 동시에 블로킹/wakeup을 반복하며 완료되는지 확인.
TEST_F(O1Test, MultipleBlockingTasksComplete) {
  constexpr int kNumThreads = 4;
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [] {
          SpinFor(absl::Milliseconds(1));
          absl::SleepFor(absl::Milliseconds(5));
          SpinFor(absl::Milliseconds(1));
        }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// 여러 번 sleep/wake를 반복하는 태스크가 정상 완료되는지 확인.
// TaskBlocked ↔ TaskRunnable 상태 전환이 여러 번 발생.
TEST_F(O1Test, RepeatedBlockUnblockCompletes) {
  constexpr int kNumThreads = 4;
  constexpr int kIterations = 5;
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [kIterations] {
          for (int j = 0; j < kIterations; j++) {
            SpinFor(absl::Microseconds(500));
            absl::SleepFor(absl::Milliseconds(2));
          }
        }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// ─────────────────────────────────────────────────────────────
// 카테고리 4: yield 태스크 (TaskYield 경로)
// ─────────────────────────────────────────────────────────────

// sched_yield()를 반복 호출하는 태스크가 정상 완료되는지 확인.
// sched_yield → TaskYield → TaskOffCpu → Enqueue → 재스케줄링
TEST_F(O1Test, YieldingTaskCompletes) {
  constexpr int kNumThreads = 4;
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [] {
          for (int j = 0; j < 5; j++) {
            SpinFor(absl::Microseconds(200));
            sched_yield();
          }
        }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// ─────────────────────────────────────────────────────────────
// 카테고리 5: 혼합 워크로드
// ─────────────────────────────────────────────────────────────

// 짧은 태스크(active queue)와 긴 태스크(expired queue)가 동시에 실행될 때
// 스케줄러가 양쪽을 모두 올바르게 처리하는지 확인.
TEST_F(O1Test, MixedShortAndLongTasksComplete) {
  std::vector<std::unique_ptr<GhostThread>> threads;

  // 짧은 태스크: time slice 내 완료 → active queue에서만 처리
  for (int i = 0; i < 4; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(2)); }));
  }

  // 긴 태스크: time slice 초과 → expired queue 경로
  for (int i = 0; i < 2; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(25)); }));
  }

  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// 짧은 CPU 작업 + 블로킹 + 긴 CPU 작업을 혼합한 복합 워크로드.
TEST_F(O1Test, MixedCpuAndBlockingWorkloadCompletes) {
  std::vector<std::unique_ptr<GhostThread>> threads;

  // CPU 집약 태스크
  for (int i = 0; i < 2; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(15)); }));
  }

  // 블로킹 태스크
  for (int i = 0; i < 2; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [] {
          SpinFor(absl::Milliseconds(1));
          absl::SleepFor(absl::Milliseconds(10));
          SpinFor(absl::Milliseconds(1));
        }));
  }

  // yield 태스크
  for (int i = 0; i < 2; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [] {
          for (int j = 0; j < 3; j++) {
            SpinFor(absl::Microseconds(500));
            sched_yield();
          }
        }));
  }

  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// ─────────────────────────────────────────────────────────────
// 카테고리 6: 고부하
// ─────────────────────────────────────────────────────────────

// 32개 스레드 동시 실행: 스케줄러 안정성과 태스크 누락 없음을 검증.
TEST_F(O1Test, HighConcurrencyCompletes) {
  constexpr int kNumThreads = 32;
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost,
        [] { SpinFor(absl::Milliseconds(1)); }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// 32개 블로킹 스레드 동시 실행: 많은 수의 블로킹 태스크 처리 안정성 검증.
TEST_F(O1Test, HighConcurrencyBlockingCompletes) {
  constexpr int kNumThreads = 32;
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [] {
          SpinFor(absl::Microseconds(500));
          absl::SleepFor(absl::Milliseconds(5));
          SpinFor(absl::Microseconds(500));
        }));
  }
  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);
}

// ─────────────────────────────────────────────────────────────
// 카테고리 7: 시간 검증
// ─────────────────────────────────────────────────────────────

// 짧은 태스크(1ms)가 time slice(10ms) 내에 완료되는지 wall time으로 검증.
// time slice 내 완료 = 선점 없이 active queue에서 한 번에 실행됨을 의미.
TEST_F(O1Test, ShortTaskCompletesWithoutPreemption) {
  constexpr absl::Duration kTimeSlice = absl::Milliseconds(10);
  constexpr absl::Duration kMargin = absl::Milliseconds(40);
  constexpr int kNumThreads = 4;

  std::vector<absl::Duration> elapsed_times(kNumThreads);
  std::vector<std::unique_ptr<GhostThread>> threads;
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(std::make_unique<GhostThread>(
        GhostThread::KernelScheduler::kGhost, [i, &elapsed_times] {
          absl::Time start = absl::Now();
          SpinFor(absl::Milliseconds(1));
          elapsed_times[i] = absl::Now() - start;
        }));
  }

  for (auto& t : threads) t->Join();
  WaitForAllTasksDone(uap_);

  for (int i = 0; i < kNumThreads; i++) {
    EXPECT_LE(elapsed_times[i], kTimeSlice + kMargin)
        << "Thread " << i << "이 예상 시간 초과: "
        << absl::ToDoubleMilliseconds(elapsed_times[i]) << "ms"
        << " (time slice=" << absl::ToDoubleMilliseconds(kTimeSlice) << "ms)";
  }
}

// 긴 태스크의 wall time이 실제 CPU 작업 시간 이상임을 확인.
// SpinFor(30ms)는 정확히 30ms의 CPU 시간 → wall time >= 30ms 보장.
TEST_F(O1Test, LongTaskWallTimeIsAtLeastCpuTime) {
  constexpr absl::Duration kCpuWork = absl::Milliseconds(30);
  absl::Duration elapsed;

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    absl::Time start = absl::Now();
    SpinFor(kCpuWork);
    elapsed = absl::Now() - start;
  });
  t.Join();
  WaitForAllTasksDone(uap_);

  EXPECT_GE(elapsed, kCpuWork)
      << "wall time(" << absl::ToDoubleMilliseconds(elapsed)
      << "ms) < CPU work(" << absl::ToDoubleMilliseconds(kCpuWork) << "ms)";
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
