// O1 스케줄러 종합 테스트
//
// 기본 동작부터 고부하 스트레스까지 O1 스케줄러의 핵심 동작을 검증한다.
// 타임슬라이스: 10ms
//
// 실행 방법:
//   sudo bazel run //:o1_basic_test

#include <atomic>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/parse.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "lib/base.h"
#include "lib/ghost.h"
#include "schedulers/o1/o1_scheduler.h"

namespace ghost {
namespace {

// 모든 테스트가 하나의 에이전트 프로세스를 공유한다.
class O1BasicTest : public testing::Test {
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

  // 모든 ghost 태스크가 스케줄러에서 사라질 때까지 기다린다.
  static void WaitForAllTasksDone() {
    int num_tasks;
    do {
      num_tasks = uap_->Rpc(O1Scheduler::kCountAllTasks);
    } while (num_tasks > 0);
  }

  static AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* uap_;
};

AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* O1BasicTest::uap_;

// ─────────────────────────────────────────────────────────────
// 1. 기본 완료 — 단일 태스크가 완료됨
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, SingleTaskCompletes) {
  std::atomic<bool> done{false};

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    done.store(true, std::memory_order_release);
  });

  t.Join();

  EXPECT_TRUE(done.load());
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 2. 다중 태스크 완료 — 10개 태스크가 모두 완료됨
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, MultipleTasksAllComplete) {
  constexpr int kNumThreads = 10;
  std::atomic<int> completed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        SpinFor(absl::Milliseconds(1));
                                        completed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(completed.load(), kNumThreads);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 3. 짧은 태스크는 선점 없이 완료 — 1ms < 타임슬라이스(10ms)
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, ShortTaskCompletesWithinTimeslice) {
  // 1ms 작업은 타임슬라이스(10ms)보다 짧으므로 선점 없이 완료되어야 한다.
  // wall-time이 50ms를 넘으면 비정상적인 지연이 발생한 것이다.
  constexpr absl::Duration kTaskWork = absl::Milliseconds(1);
  constexpr absl::Duration kLimit = absl::Milliseconds(50);

  absl::Duration elapsed;

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    absl::Time start = absl::Now();
    SpinFor(kTaskWork);
    elapsed = absl::Now() - start;
  });

  t.Join();

  EXPECT_LE(elapsed, kLimit)
      << "elapsed=" << absl::ToDoubleMilliseconds(elapsed) << "ms";
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 4. 긴 태스크도 결국 완료 — 선점이 여러 번 발생해도 완료됨
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, LongTaskEventuallyCompletes) {
  // 50ms 작업 → 타임슬라이스(10ms)로 약 5회 선점 예상.
  // 완료만 검증한다 (hang 여부 확인).
  constexpr absl::Duration kTaskWork = absl::Milliseconds(50);
  std::atomic<bool> done{false};

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    SpinFor(kTaskWork);
    done.store(true);
  });

  t.Join();

  EXPECT_TRUE(done.load());
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 5. sleep/wakeup 사이클 — 블록 후 재개되어 완료됨
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, SleepWakeupCompletes) {
  // 태스크가 여러 번 sleep → wakeup 사이클을 거쳐 완료됨을 확인.
  constexpr int kCycles = 5;
  std::atomic<int> wakeups{0};

  GhostThread t(GhostThread::KernelScheduler::kGhost, [&] {
    for (int i = 0; i < kCycles; i++) {
      absl::SleepFor(absl::Milliseconds(2));
      wakeups.fetch_add(1);
    }
  });

  t.Join();

  EXPECT_EQ(wakeups.load(), kCycles);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 6. CPU bound + I/O bound 혼합 워크로드
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, MixedCpuAndIoWorkload) {
  constexpr int kCpuBound = 5;
  constexpr int kIoBound = 5;
  std::atomic<int> completed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kCpuBound + kIoBound);

  // CPU bound: 타임슬라이스 경계 근처에서 spin
  for (int i = 0; i < kCpuBound; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        SpinFor(absl::Milliseconds(15));
                                        completed.fetch_add(1);
                                      }));
  }

  // I/O bound: 짧은 sleep을 반복
  for (int i = 0; i < kIoBound; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        for (int j = 0; j < 3; j++) {
                                          absl::SleepFor(absl::Milliseconds(3));
                                        }
                                        completed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(completed.load(), kCpuBound + kIoBound);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 7. 공유 카운터 정확성 — 여러 태스크가 원자 카운터를 증가
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, SharedAtomicCounterCorrectness) {
  constexpr int kNumThreads = 20;
  constexpr int kIncPerThread = 1000;
  std::atomic<int> counter{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&counter] {
                                        for (int j = 0; j < kIncPerThread; j++) {
                                          counter.fetch_add(
                                              1, std::memory_order_relaxed);
                                        }
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(counter.load(), kNumThreads * kIncPerThread);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 8. 배리어 동기화 — 모든 태스크가 동시에 시작점에 도달
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, BarrierSynchronization) {
  constexpr int kNumThreads = 10;
  absl::Barrier barrier(kNumThreads);
  std::atomic<int> passed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&barrier, &passed] {
                                        barrier.Block();
                                        passed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(passed.load(), kNumThreads);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 9. 기아 없음 — expired queue의 태스크도 완료됨
//    타임슬라이스를 소진한 태스크가 expired queue로 이동된 뒤
//    active queue가 비면 swap되어 재실행되는 O1 큐 스왑 동작 검증.
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, NoStarvationQueueSwap) {
  // 타임슬라이스(10ms)를 여러 번 소진하는 태스크들을 동시 실행.
  // 일부 태스크가 expired queue에서도 반드시 실행되어 완료되어야 한다.
  constexpr int kNumThreads = 8;
  constexpr absl::Duration kTaskWork = absl::Milliseconds(40);  // 약 4 timeslice
  std::atomic<int> completed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        SpinFor(kTaskWork);
                                        completed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(completed.load(), kNumThreads);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 10. 스트레스 — 100개 스레드 동시 실행
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, Stress100Threads) {
  constexpr int kNumThreads = 100;
  std::atomic<int> completed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        SpinFor(absl::Milliseconds(1));
                                        completed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(completed.load(), kNumThreads);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 11. 스트레스 — 1000개 스레드 (SimpleExpMany 재현)
//     boosted_priority 버그가 재발하면 이 테스트에서 hang된다.
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, Stress1000Threads) {
  constexpr int kNumThreads = 1000;
  std::atomic<int> completed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        SpinFor(absl::Milliseconds(1));
                                        completed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(completed.load(), kNumThreads);
  WaitForAllTasksDone();
}

// ─────────────────────────────────────────────────────────────
// 12. 스트레스 — 1000개 스레드 + sleep/wakeup 혼합
//     sleep으로 인한 MSG_TASK_BLOCKED/WAKEUP 메시지가 폭발적으로
//     발생하는 상황에서 배리어 동기화가 유지되는지 검증.
// ─────────────────────────────────────────────────────────────
TEST_F(O1BasicTest, Stress1000ThreadsWithSleep) {
  constexpr int kNumThreads = 1000;
  std::atomic<int> completed{0};

  std::vector<std::unique_ptr<GhostThread>> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        std::make_unique<GhostThread>(GhostThread::KernelScheduler::kGhost,
                                      [&completed] {
                                        absl::SleepFor(absl::Milliseconds(1));
                                        SpinFor(absl::Milliseconds(1));
                                        completed.fetch_add(1);
                                      }));
  }

  for (auto& t : threads) t->Join();

  EXPECT_EQ(completed.load(), kNumThreads);
  WaitForAllTasksDone();
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
