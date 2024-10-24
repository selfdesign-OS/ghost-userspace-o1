#include <stdio.h>
#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cmath>

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

  const double delta = 1.0 / 10000000000;

  // Pi 계산 함수
  auto calculatePi = [&]() {
    double pi = 0.0;
    for (int i = 0; i < 10000000; i++) {
      pi += delta / (1.0 + ((i + 0.5) * delta) * ((i + 0.5) * delta));
      pi += sin(i) * cos(i) / tan(i + 1.0);
    }
    return pi * 4.0;
  };

  // 스레드 생성 및 실행
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(new GhostThread(GhostThread::KernelScheduler::kGhost, [&, i] {
      // 스레드 준비 상태
      {
        std::unique_lock<std::mutex> lock(cv_m);
        num_threads_ready++;
        if (num_threads_ready == num_threads) {
          ready = 1;
          cv.notify_all();
        } else {
          cv.wait(lock, [] { return ready == 1; });
        }
      }

      // Pi 계산 시작
      absl::Time start = absl::Now();
      printf("Thread %d started\n", i);

      double result = calculatePi();

      absl::Time end = absl::Now();
      printf("Thread %d finished in %0.2f ms, Result: %f\n", i, absl::ToDoubleMilliseconds(end - start), result);
      execution_times[i] = end - start;
    }));
  }

  // 스레드 종료 대기
  for (auto& t : threads) {
    t->Join();
  }

  // 실행 시간 출력 및 평균 시간 계산
  double total_time_ms = 0.0;
  for (int i = 0; i < num_threads; i++) {
    double exec_time_ms = absl::ToDoubleMilliseconds(execution_times[i]);
    printf("Thread %d execution time: %0.2f ms\n", i, exec_time_ms);
    total_time_ms += exec_time_ms;
  }

  // 평균 실행 시간 계산
  double average_time_ms = total_time_ms / num_threads;
  printf("Average execution time: %0.2f ms\n", average_time_ms);

  // 표준 편차 계산
  double variance_ms = 0.0;
  for (const auto& time : execution_times) {
    double diff_ms = absl::ToDoubleMilliseconds(time) - average_time_ms;
    variance_ms += diff_ms * diff_ms;  // 제곱 차이 누적
  }

  // 분산의 평균을 구하고 제곱근을 취해 표준 편차 계산
  double stddev_ms = std::sqrt(variance_ms / num_threads);
  printf("Standard deviation of execution times: %0.2f ms\n", stddev_ms);
}


}  // namespace
}  // namespace ghost

int main() {
  {
    printf("FairnessTest\n");
    ghost::ScopedTime time;
    ghost::FairnessTest(16);
  }
  return 0;
}
