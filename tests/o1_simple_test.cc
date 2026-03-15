// Copyright 2021 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// O1 스케줄러 기본 테스트 (재현성 보존용)
//
// VirtualBox 환경에서는 host 스케줄러 간섭으로 실패하던 테스트.
// 네이티브 Linux (bare metal) 환경에서 통과 확인 (commit: f30e95f).
//
// 목적: time slice 10ms에서 1ms 작업이 선점 없이 완료되는지 검증
//
// 실행 예시:
//   sudo bazel run //tests:o1_simple_test -- --ghost_cpus=3,4

#include <sstream>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
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

static CpuList GetGhostCpus(Topology* t) {
  std::string s = absl::GetFlag(FLAGS_ghost_cpus);
  if (s.empty()) return t->all_cpus();
  std::vector<int> ids;
  std::istringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) ids.push_back(std::stoi(token));
  return t->ToCpuList(ids);
}

static void WaitForAllTasksDone(
    AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* uap,
    absl::Duration timeout = absl::Seconds(5)) {
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

class O1SimpleTest : public testing::Test {
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

AgentProcess<FullO1Agent<LocalEnclave>, AgentConfig>* O1SimpleTest::uap_;

// 단일 스레드가 1ms 작업을 선점 없이 완료하는지 확인.
//
// time slice = 10ms, 작업 시간 = 1ms 이므로 time slice 만료 전에 작업이
// 끝나야 한다. 선점이 발생하면 expired queue로 이동 후 재스케줄링되어
// 경과 시간이 크게 늘어난다.
TEST_F(O1SimpleTest, ShortTaskCompletesWithoutPreemption) {
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

  WaitForAllTasksDone(uap_);
}

// 여러 스레드가 각각 1ms 작업을 선점 없이 완료하는지 확인.
// CPU 2개, 스레드 4개 → 최대 2 라운드. 부하를 낮춰 불안정한 환경에서도 통과.
TEST_F(O1SimpleTest, MultipleShortTasksCompleteWithoutPreemption) {
  constexpr int kNumThreads = 4;
  constexpr absl::Duration kMaxAllowedWallTime = absl::Milliseconds(500);

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

  for (auto& t : threads) t->Join();

  for (int i = 0; i < kNumThreads; i++) {
    EXPECT_LE(elapsed_times[i], kMaxAllowedWallTime)
        << "Thread " << i << " took "
        << absl::ToDoubleMilliseconds(elapsed_times[i])
        << "ms — possible preemption detected";
  }

  WaitForAllTasksDone(uap_);
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
