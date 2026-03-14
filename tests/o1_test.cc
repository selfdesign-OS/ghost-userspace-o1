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

// [가설 3.1.1 검증] CFS 태스크에 의한 선점 여부 확인
// 커널 부팅 시 isolcpus=2-3 등으로 고립시킨 CPU를 지정한다.
// 기본값은 all_cpus (고립 없는 일반 테스트)
ABSL_FLAG(std::string, ghost_cpus, "", "ghost enclave에 사용할 CPU 목록 (예: 2-3). "
                                       "비어있으면 전체 CPU 사용.");

// [가설 3.1.2 검증] CpuTick에 의한 선점 여부 확인
// --disable_cpu_tick=true 로 실행하면 CpuTick 구독을 끄고 테스트한다.
// 이 플래그는 o1_scheduler.cc의 FLAGS_disable_cpu_tick과 동일한 플래그다.
ABSL_DECLARE_FLAG(bool, disable_cpu_tick);

namespace ghost {
namespace {

using ::testing::Ge;

class O1Test : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    Topology* t = MachineTopology();

    std::string cpus_flag = absl::GetFlag(FLAGS_ghost_cpus);
    CpuList ghost_cpus = cpus_flag.empty()
                             ? t->all_cpus()
                             : t->ParseCpuStr(cpus_flag);
    CHECK(!ghost_cpus.Empty()) << "ghost_cpus가 비어있습니다: " << cpus_flag;

    AgentConfig cfg(t, ghost_cpus);
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
TEST_F(O1Test, ShortTaskCompletesWithoutPreemption) {
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

// [가설 3.1.1 검증] CFS 선점 여부 확인
// --ghost_cpus 없이 실행: CFS 태스크가 ghost 코어에 들어올 수 있는 환경
// --ghost_cpus=2-3 실행:  커널 isolcpus로 고립된 코어만 사용 → CFS 간섭 없음
//
// 두 결과의 선점 횟수를 비교하여 CFS 태스크 간섭 여부를 판단한다.
//   - 고립 후 선점 횟수가 현저히 줄면 → 가설 3.1.1 (CFS 선점) 원인 확정
//   - 고립 후에도 선점 횟수 동일    → 가설 3.1.1 배제, 다음 가설로 이동
TEST_F(O1Test, MultipleShortTasksCompleteWithoutPreemption) {
  constexpr int kNumThreads = 10;
  constexpr absl::Duration kMaxAllowedWallTime = absl::Milliseconds(100);

  std::string cpus_flag = absl::GetFlag(FLAGS_ghost_cpus);
  bool tick_disabled = absl::GetFlag(FLAGS_disable_cpu_tick);
  fprintf(stderr,
          "[Hypothesis 3.1.1] ghost_cpus=%s\n"
          "[Hypothesis 3.1.2] disable_cpu_tick=%s\n",
          cpus_flag.empty() ? "all (no isolation)" : cpus_flag.c_str(),
          tick_disabled ? "true (CpuTick OFF)" : "false (CpuTick ON)");

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
        << absl::ToDoubleMilliseconds(elapsed_times[i]) << "ms";
  }

  int num_tasks;
  do {
    num_tasks = uap_->Rpc(O1Scheduler::kCountAllTasks);
    EXPECT_THAT(num_tasks, Ge(0));
  } while (num_tasks > 0);

  // 선점 횟수 출력 - 가설 3.1.1 / 3.1.2 비교용
  int64_t total_preemptions = uap_->Rpc(O1Scheduler::kCountPreemptions);
  fprintf(stderr,
      "[Result] preemption_total=%lld  per_thread_avg=%.0f\n"
      "  [3.1.1] ghost_cpus=%s  -> 고립 후 횟수 감소 시 CFS 선점이 원인\n"
      "  [3.1.2] CpuTick=%s     -> OFF 후 횟수 감소 시 CpuTick 선점이 원인\n",
      static_cast<long long>(total_preemptions),
      static_cast<double>(total_preemptions) / kNumThreads,
      cpus_flag.empty() ? "all (no isolation)" : cpus_flag.c_str(),
      tick_disabled ? "OFF" : "ON");
}

}  // namespace
}  // namespace ghost

int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
