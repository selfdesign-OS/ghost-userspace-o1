#include <stdio.h>

#include <atomic>
#include <memory>
#include <vector>
#include <barrier>

#include "lib/base.h"
#include "lib/ghost.h"

// A series of simple tests for ghOSt schedulers.

namespace ghost {
namespace {

struct ScopedTime {
  ScopedTime() { start = absl::Now(); }
  ~ScopedTime() {
    printf(" took %0.2f ms\n", absl::ToDoubleMilliseconds(absl::Now() - start));
  }
  absl::Time start;
};

void FairnessTest(int num_threads) {
  std::vector<std::unique_ptr<GhostThread>> threads;
  std::vector<absl::Duration> execution_times(num_threads);

  // Pi 계산을 위한 작은 델타 값 설정 (더 작은 값일수록 계산 시간이 길어짐)
  const double delta = 1.0 / 100000000.0;

  // Pi 계산 함수
  auto calculatePi = [&]() {
    double pi = 0.0;
    for (int i = 0; i < 100000000; i++) {
      pi += delta / (1.0 + ((i + 0.5) * delta) * ((i + 0.5) * delta));
    }
    return pi * 4.0;
  };

  // 스레드들이 동시에 시작할 수 있도록 배리어(barrier) 생성
  std::barrier sync_point(num_threads);

  // 각 스레드에서 실행할 작업 정의
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(new GhostThread(GhostThread::KernelScheduler::kGhost, [&, i] {
      // 배리어에서 대기하여 모든 스레드가 여기 도달하면 동시에 시작
      sync_point.arrive_and_wait();

      absl::Time start = absl::Now();
      calculatePi();
      absl::Time end = absl::Now();
      execution_times[i] = end - start;
    }));
  }

  // 모든 스레드 실행
  for (auto& t : threads) {
    t->Join();
  }

  // 실행 시간 출력 및 분포 분석
  absl::Duration total_time;
  for (int i = 0; i < num_threads; i++) {
    printf("Thread %d execution time: %0.2f ms\n", i, absl::ToDoubleMilliseconds(execution_times[i]));
    total_time += execution_times[i];
  }

  absl::Duration average_time = total_time / num_threads;
  printf("Average execution time: %0.2f ms\n", absl::ToDoubleMilliseconds(average_time));

  // 표준 편차 계산 (공정성 측정)
  absl::Duration variance = absl::ZeroDuration();
  for (const auto& time : execution_times) {
    variance += (time - average_time) * (time - average_time);
  }
  absl::Duration stddev = absl::Nanoseconds(std::sqrt(absl::ToDoubleNanoseconds(variance) / num_threads));
  printf("Standard deviation of execution times: %0.2f ms\n", absl::ToDoubleMilliseconds(stddev));
}

}  // namespace
}  // namespace ghost

int main() {
  {
    printf("FiarnessTest\n");
    ghost::ScopedTime time;
    ghost::FairnessTest(100);
  }
  return 0;
}
