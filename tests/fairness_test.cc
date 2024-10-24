#include <stdio.h>
#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>

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

// 스레드를 동시에 시작하기 위한 변수
std::condition_variable cv;
std::mutex cv_m;
int ready = 0;
int num_threads_ready = 0; // 스레드가 준비 완료된 수

void FairnessTest(int num_threads) {
  std::vector<std::unique_ptr<GhostThread>> threads;
  std::vector<absl::Duration> execution_times(num_threads);

  // Pi 계산을 위한 작은 델타 값 설정 (더 작은 값일수록 계산 시간이 길어짐)
  const double delta = 0.0;

  // Pi 계산 함수
  auto calculatePi = [&]() {
    double pi = 0.0;
    for (int i = 0; i < 1000000000; i++) {
      pi += delta / (1.0 + ((i + 0.5) * delta) * ((i + 0.5) * delta));
    }
    return pi * 4.0;
  };

  // 각 스레드에서 실행할 작업 정의
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(new GhostThread(GhostThread::KernelScheduler::kGhost, [&, i] {
      // 준비 완료 상태로 설정
      {
        std::unique_lock<std::mutex> lock(cv_m);
        num_threads_ready++;
        if (num_threads_ready == num_threads) {
          ready = 1; // 모든 스레드가 준비되면 신호 전송
          cv.notify_all();
        } else {
          cv.wait(lock, [] { return ready == 1; }); // 준비 완료 신호를 기다림
        }
      }

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
    // 시간의 차이 계산 및 제곱
    auto diff = time - average_time;
    variance += absl::Nanoseconds(absl::ToDoubleNanoseconds(diff) * absl::ToDoubleNanoseconds(diff));
  }
  absl::Duration stddev = absl::Nanoseconds(std::sqrt(absl::ToDoubleNanoseconds(variance) / num_threads));
  printf("Standard deviation of execution times: %0.2f ms\n", absl::ToDoubleMilliseconds(stddev));
}

}  // namespace
}  // namespace ghost

int main() {
  {
    printf("FairnessTest\n");
    ghost::ScopedTime time;
    ghost::FairnessTest(100);
  }
  return 0;
}
