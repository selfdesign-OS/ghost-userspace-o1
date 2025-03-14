// Copyright 2021 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef GHOST_SCHEDULERS_O1_O1_SCHEDULER_H
#define GHOST_SCHEDULERS_O1_O1_SCHEDULER_H

#include <deque>
#include <memory>

#include "lib/agent.h"
#include "lib/scheduler.h"

namespace ghost {

enum class O1TaskState {
  kBlocked,   // not on runqueue.
  kRunnable,  // transitory state:
              // 1. kBlocked->kRunnable->kQueued
              // 2. kQueued->kRunnable->kOnCpu
  kQueued,    // on runqueue.
  kOnCpu,     // running on cpu.
};

// For CHECK and friends.
std::ostream& operator<<(std::ostream& os, const O1TaskState& state);

struct O1Task : public Task<> {
  explicit O1Task(Gtid O1_task_gtid, ghost_sw_info sw_info)
      : Task<>(O1_task_gtid, sw_info) {}
  ~O1Task() override {}

  inline bool blocked() const { return run_state == O1TaskState::kBlocked; }
  inline bool queued() const { return run_state == O1TaskState::kQueued; }
  inline bool oncpu() const { return run_state == O1TaskState::kOnCpu; }

  // N.B. _runnable() is a transitory state typically used during runqueue
  // manipulation. It is not expected to be used from task msg callbacks.
  //
  // If you are reading this then you probably want to take a closer look
  // at queued() instead.
  inline bool _runnable() const {
    return run_state == O1TaskState::kRunnable;
  }

  void SetRemainingTime() {
    remaining_time = absl::Nanoseconds(10000000); // 10ms
  }

  bool UpdateRemainingTime(bool isOff);

  void SetRuntimeAtLastPick() {
    runtime_at_last_pick = absl::Now();
  }

  O1TaskState run_state = O1TaskState::kBlocked;
  int cpu = -1;

  // Whether the last execution was preempted or not.
  bool preempted = false;

  // A task's priority is boosted on a kernel preemption or a !deferrable
  // wakeup - basically when it may be holding locks or other resources
  // that prevent other tasks from making progress.
  bool prio_boost = false;

  // 마지막 remaining_time 업데이트한 시간
  absl::Time runtime_at_last_pick;
  // 남은 cpu 실행 시간
  absl::Duration remaining_time;
};

class O1Rq {
 public:
  O1Rq() = default;
  O1Rq(const O1Rq&) = delete;
  O1Rq& operator=(O1Rq&) = delete;

  void Swap();
  O1Task* Dequeue();
  void EnqueueActive(O1Task* task);
  void EnqueueExpired(O1Task* task);
  void Enqueue(O1Task* task);
  // Erase 'task' from the runqueue.
  //
  // Caller must ensure that 'task' is on the runqueue in the first place
  // (e.g. via task->queued()).
  void Erase(O1Task* task);

  size_t Size() const {
    absl::MutexLock lock(&mu_);
    return aq_.size();
  }

  absl::Mutex& GetMu_() {
    return mu_;
  }

  bool Empty() const { return Size() == 0; }

 private:
  mutable absl::Mutex mu_;
  std::deque<O1Task*> aq_ ABSL_GUARDED_BY(mu_);
  std::deque<O1Task*> eq_ ABSL_GUARDED_BY(mu_);
};

class O1Scheduler : public BasicDispatchScheduler<O1Task> {
 public:
  explicit O1Scheduler(Enclave* enclave, CpuList cpulist,
                         std::shared_ptr<TaskAllocator<O1Task>> allocator);
  ~O1Scheduler() final {}

  void Schedule(const Cpu& cpu, const StatusWord& sw);

  void EnclaveReady() final;
  Channel& GetDefaultChannel() final { return *default_channel_; };

  bool Empty(const Cpu& cpu) {
    CpuState* cs = cpu_state(cpu);
    return cs->run_queue.Empty();
  }

  void DumpState(const Cpu& cpu, int flags) final;
  std::atomic<bool> debug_runqueue_ = false;

  int CountAllTasks() {
    int num_tasks = 0;
    allocator()->ForEachTask([&num_tasks](Gtid gtid, const O1Task* task) {
      ++num_tasks;
      return true;
    });
    return num_tasks;
  }

  static constexpr int kDebugRunqueue = 1;
  static constexpr int kCountAllTasks = 2;

 protected:
  void TaskNew(O1Task* task, const Message& msg) final;
  void TaskRunnable(O1Task* task, const Message& msg) final;
  void TaskDeparted(O1Task* task, const Message& msg) final;
  void TaskDead(O1Task* task, const Message& msg) final;
  void TaskYield(O1Task* task, const Message& msg) final;
  void TaskBlocked(O1Task* task, const Message& msg) final;
  void TaskPreempted(O1Task* task, const Message& msg) final;
  void TaskSwitchto(O1Task* task, const Message& msg) final;
  void CpuTick(const Message& msg) final;

 private:
  // Checks if we should preempt the current task. If so, sets preempt_curr_.
  // Note: Should be called with this CPU's rq mutex lock held.
  void CheckPreemptTick(const Cpu& cpu);
  void O1Schedule(const Cpu& cpu, BarrierToken agent_barrier,
                    bool prio_boosted);
  void TaskOffCpu(O1Task* task, bool blocked, bool from_switchto);
  void TaskOnCpu(O1Task* task, Cpu cpu);
  void Migrate(O1Task* task, Cpu cpu, BarrierToken seqnum);
  Cpu AssignCpu(O1Task* task);
  void DumpAllTasks();

  struct CpuState {
    O1Task* current = nullptr;
    std::unique_ptr<Channel> channel = nullptr;
    O1Rq run_queue;
    // Should we keep running the current task.
    bool preempt_curr = false;
  } ABSL_CACHELINE_ALIGNED;

  inline CpuState* cpu_state(const Cpu& cpu) { return &cpu_states_[cpu.id()]; }

  inline CpuState* cpu_state_of(const O1Task* task) {
    CHECK_GE(task->cpu, 0);
    CHECK_LT(task->cpu, MAX_CPUS);
    return &cpu_states_[task->cpu];
  }

  CpuState cpu_states_[MAX_CPUS];
  Channel* default_channel_ = nullptr;
};

std::unique_ptr<O1Scheduler> MultiThreadedO1Scheduler(Enclave* enclave,
                                                          CpuList cpulist);
class O1Agent : public LocalAgent {
 public:
  O1Agent(Enclave* enclave, Cpu cpu, O1Scheduler* scheduler)
      : LocalAgent(enclave, cpu), scheduler_(scheduler) {}

  void AgentThread() override;
  Scheduler* AgentScheduler() const override { return scheduler_; }

 private:
  O1Scheduler* scheduler_;
};

template <class EnclaveType>
class FullO1Agent : public FullAgent<EnclaveType> {
 public:
  explicit FullO1Agent(AgentConfig config) : FullAgent<EnclaveType>(config) {
    scheduler_ =
        MultiThreadedO1Scheduler(&this->enclave_, *this->enclave_.cpus());
    this->StartAgentTasks();
    this->enclave_.Ready();
  }

  ~FullO1Agent() override {
    this->TerminateAgentTasks();
  }

  std::unique_ptr<Agent> MakeAgent(const Cpu& cpu) override {
    return std::make_unique<O1Agent>(&this->enclave_, cpu, scheduler_.get());
  }

  void RpcHandler(int64_t req, const AgentRpcArgs& args,
                  AgentRpcResponse& response) override {
    switch (req) {
      case O1Scheduler::kDebugRunqueue:
        scheduler_->debug_runqueue_ = true;
        response.response_code = 0;
        return;
      case O1Scheduler::kCountAllTasks:
        response.response_code = scheduler_->CountAllTasks();
        return;
      default:
        response.response_code = -1;
        return;
    }
  }

 private:
  std::unique_ptr<O1Scheduler> scheduler_;
};

}  // namespace ghost

#endif  // GHOST_SCHEDULERS_O1_O1_SCHEDULER_H
