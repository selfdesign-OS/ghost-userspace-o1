// Copyright 2021 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "schedulers/o1/o1_scheduler.h"

#include <memory>

namespace ghost {

O1Scheduler::O1Scheduler(Enclave* enclave, CpuList cpulist,
                             std::shared_ptr<TaskAllocator<O1Task>> allocator)
    : BasicDispatchScheduler(enclave, std::move(cpulist),
                             std::move(allocator)) {
  for (const Cpu& cpu : cpus()) {
    // TODO: extend Cpu to get numa node.
    int node = 0;
    CpuState* cs = cpu_state(cpu);
    cs->channel = enclave->MakeChannel(GHOST_MAX_QUEUE_ELEMS, node,
                                       MachineTopology()->ToCpuList({cpu}));
    // This channel pointer is valid for the lifetime of O1Scheduler
    if (!default_channel_) {
      default_channel_ = cs->channel.get();
    }
  }
}

bool O1Task::UpdateRemainingTime(bool isOff) {
  absl::Duration elapsed = absl::Now() - runtime_at_last_pick;
  absl::Duration before = remaining_time;
  remaining_time -= elapsed;

  GHOST_DPRINT(1, stderr,
      "[UpdateRemainingTime] caller=%-11s tid=%-6d  elapsed=%6.3fms  %.2fms -> %.2fms%s",
      isOff ? "TaskOffCpu" : "CpuTick",
      gtid.tid(),
      absl::ToDoubleMilliseconds(elapsed),
      absl::ToDoubleMilliseconds(before),
      absl::ToDoubleMilliseconds(remaining_time),
      (!isOff && remaining_time <= absl::ZeroDuration()) ? "  *** TIMESLICE EXPIRED ***" : "");

  if (!isOff) {
    SetRuntimeAtLastPick();
    if (remaining_time <= absl::ZeroDuration()) {
      return true;
    }
  }
  return false;
}

void O1Scheduler::DumpAllTasks() {
  GHOST_DPRINT(1, stderr, "[DumpAllTasks]");
  fprintf(stderr, "task        state   cpu\n");
  allocator()->ForEachTask([](Gtid gtid, const O1Task* task) {
    absl::FPrintF(stderr, "%-12s%-8d%-8d%c%c\n", gtid.describe(),
                  task->run_state, task->cpu, task->preempted ? 'P' : '-',
                  task->prio_boost ? 'B' : '-');
    return true;
  });
}

void O1Scheduler::DumpState(const Cpu& cpu, int flags) {
  GHOST_DPRINT(1, stderr, "[DumpState] cpu=%d", cpu.id());
  if (flags & Scheduler::kDumpAllTasks) {
    DumpAllTasks();
  }

  CpuState* cs = cpu_state(cpu);
  if (!(flags & Scheduler::kDumpStateEmptyRQ) && !cs->current &&
      cs->run_queue.Empty()) {
    return;
  }

  const O1Task* current = cs->current;
  const O1Rq* rq = &cs->run_queue;
  absl::FPrintF(stderr, "[DumpState] cpu=%d  current=tid:%-6s  aq_size=%lu\n",
                cpu.id(),
                current ? std::to_string(current->gtid.tid()).c_str() : "none",
                rq->Size());
}

void O1Scheduler::EnclaveReady() {
  GHOST_DPRINT(1, stderr, "[EnclaveReady]");
  for (const Cpu& cpu : cpus()) {
    CpuState* cs = cpu_state(cpu);
    Agent* agent = enclave()->GetAgent(cpu);

    // AssociateTask may fail if agent barrier is stale.
    while (!cs->channel->AssociateTask(agent->gtid(), agent->barrier(),
                                       /*status=*/nullptr)) {
      CHECK_EQ(errno, ESTALE);
    }
  }

  // Enable tick msg delivery here instead of setting AgentConfig.tick_config_
  // because the agent subscribing the default channel (mostly the
  // channel/agent for the front CPU in the enclave) can get CpuTick messages
  // for another CPU in the enclave while this function is trying to associate
  // each agent to its corresponding channel.
  enclave()->SetDeliverTicks(true);
}

// Implicitly thread-safe because it is only called from one agent associated
// with the default queue.
Cpu O1Scheduler::AssignCpu(O1Task* task) {
  static auto begin = cpus().begin();
  static auto end = cpus().end();
  static auto next = end;

  if (next == end) {
    next = begin;
  }
  Cpu assigned = next++;
  GHOST_DPRINT(1, stderr, "[AssignCpu] tid=%-6d  -> cpu=%d  remaining=%.2fms",
      task->gtid.tid(), assigned.id(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  return assigned;
}

void O1Scheduler::Migrate(O1Task* task, Cpu cpu, BarrierToken seqnum) {
  GHOST_DPRINT(1, stderr, "[Migrate] tid=%-6d  -> cpu=%d  remaining=%.2fms",
      task->gtid.tid(), cpu.id(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  CHECK_EQ(task->run_state, O1TaskState::kRunnable);
  CHECK_EQ(task->cpu, -1);

  CpuState* cs = cpu_state(cpu);
  const Channel* channel = cs->channel.get();
  CHECK(channel->AssociateTask(task->gtid, seqnum, /*status=*/nullptr));

  task->cpu = cpu.id();

  // Make task visible in the new runqueue *after* changing the association
  // (otherwise the task can get oncpu while producing into the old queue).
  cs->run_queue.Enqueue(task);

  // Get the agent's attention so it notices the new task.
  enclave()->GetAgent(cpu)->Ping();
}

void O1Scheduler::TaskNew(O1Task* task, const Message& msg) {
  const ghost_msg_payload_task_new* payload =
      static_cast<const ghost_msg_payload_task_new*>(msg.payload());
  task->SetRemainingTime();
  task->seqnum = msg.seqnum();
  task->run_state = O1TaskState::kBlocked;

  GHOST_DPRINT(1, stderr, "[TaskNew] tid=%-6d  timeslice=%.2fms  runnable=%s",
      task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time),
      payload->runnable ? "yes" : "no");

  if (payload->runnable) {
    task->run_state = O1TaskState::kRunnable;
    Cpu cpu = AssignCpu(task);
    Migrate(task, cpu, msg.seqnum());
  } else {
    // Wait until task becomes runnable to avoid race between migration
    // and MSG_TASK_WAKEUP showing up on the default channel.
  }
}

void O1Scheduler::TaskRunnable(O1Task* task, const Message& msg) {
  GHOST_DPRINT(1, stderr, "[TaskRunnable] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  const ghost_msg_payload_task_wakeup* payload =
      static_cast<const ghost_msg_payload_task_wakeup*>(msg.payload());

  CHECK(task->blocked());
  task->run_state = O1TaskState::kRunnable;

  // A non-deferrable wakeup gets the same preference as a preempted task.
  // This is because it may be holding locks or resources needed by other
  // tasks to make progress.
  task->prio_boost = !payload->deferrable;

  if (task->cpu < 0) {
    // There cannot be any more messages pending for this task after a
    // MSG_TASK_WAKEUP (until the agent puts it oncpu) so it's safe to
    // migrate.
    Cpu cpu = AssignCpu(task);
    Migrate(task, cpu, msg.seqnum());
  } else {
    CpuState* cs = cpu_state_of(task);
    cs->run_queue.Enqueue(task);
  }
}

void O1Scheduler::TaskDeparted(O1Task* task, const Message& msg) {
  GHOST_DPRINT(1, stderr, "[TaskDeparted] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  const ghost_msg_payload_task_departed* payload =
      static_cast<const ghost_msg_payload_task_departed*>(msg.payload());

  if (task->oncpu() || payload->from_switchto) {
    TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);
  } else if (task->queued()) {
    CpuState* cs = cpu_state_of(task);
    cs->run_queue.Erase(task);
  } else {
    CHECK(task->blocked());
  }

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }

  allocator()->FreeTask(task);
}

void O1Scheduler::TaskDead(O1Task* task, const Message& msg) {
  GHOST_DPRINT(1, stderr, "[TaskDead] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  CHECK(task->blocked());
  allocator()->FreeTask(task);
}

void O1Scheduler::TaskYield(O1Task* task, const Message& msg) {
  GHOST_DPRINT(1, stderr, "[TaskYield] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  const ghost_msg_payload_task_yield* payload =
      static_cast<const ghost_msg_payload_task_yield*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  CpuState* cs = cpu_state_of(task);
  cs->run_queue.Enqueue(task);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void O1Scheduler::TaskBlocked(O1Task* task, const Message& msg) {
  GHOST_DPRINT(1, stderr, "[TaskBlocked] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  const ghost_msg_payload_task_blocked* payload =
      static_cast<const ghost_msg_payload_task_blocked*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/true, payload->from_switchto);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void O1Scheduler::TaskPreempted(O1Task* task, const Message& msg) {
  int64_t count = preemption_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  GHOST_DPRINT(1, stderr,
      "[TaskPreempted] cpu=%-2d tid=%-6d  remaining=%.2fms  total_preemptions=%-8lld  *** kernel preempted ***",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time),
      static_cast<long long>(count));
  const ghost_msg_payload_task_preempt* payload =
      static_cast<const ghost_msg_payload_task_preempt*>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  task->preempted = true;
  task->prio_boost = true;
  CpuState* cs = cpu_state_of(task);
  cs->run_queue.Enqueue(task);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void O1Scheduler::TaskSwitchto(O1Task* task, const Message& msg) {
  GHOST_DPRINT(1, stderr, "[TaskSwitchto] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  TaskOffCpu(task, /*blocked=*/true, /*from_switchto=*/false);
}

void O1Scheduler::CpuTick(const Message& msg) {
  const ghost_msg_payload_cpu_tick* payload =
      static_cast<const ghost_msg_payload_cpu_tick*>(msg.payload());
  Cpu cpu = topology()->cpu(payload->cpu);
  CpuState* cs = cpu_state(cpu);

  absl::MutexLock lock(&cs->run_queue.GetMu_());
  cs->run_queue.GetMu_().AssertHeld(); // lock 잡았는지 확인
GHOST_DPRINT(1, stderr,
          "[CpuTick] cpu=%-2d tid=%-6d  remaining=%.2fms",
          cpu.id(), cs->current->gtid.tid(),
          absl::ToDoubleMilliseconds(cs->current->remaining_time));
  // We do not actually need any logic in CpuTick for preemption. Since
  // CpuTick messages wake up the agent, CfsSchedule will eventually be
  // called, which contains the logic for figuring out if we should run the
  // task that was running before we got preempted the agent or if we should
  // reach into our rb tree.
  CheckPreemptTick(cpu);
}

void O1Scheduler::CheckPreemptTick(const Cpu& cpu)
  ABSL_NO_THREAD_SAFETY_ANALYSIS {
  CpuState* cs = cpu_state(cpu);
  cs->run_queue.GetMu_().AssertHeld();
  if (cs->current) {
    if (cs->current->UpdateRemainingTime(/*isTaskOffCpu=*/false)) {
      GHOST_DPRINT(1, stderr,
          "[CheckPreemptTick] cpu=%-2d tid=%-6d  remaining=%.2fms -> preempt_curr=true",
          cpu.id(), cs->current->gtid.tid(),
          absl::ToDoubleMilliseconds(cs->current->remaining_time));
      cs->preempt_curr = true;
    }
  }
}


void O1Scheduler::TaskOffCpu(O1Task* task, bool blocked,
                               bool from_switchto) {
  GHOST_DPRINT(1, stderr, "[TaskOffCpu] cpu=%-2d tid=%-6d  remaining=%.2fms  -> %s",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time),
      blocked ? "kBlocked" : "kRunnable");
  CpuState* cs = cpu_state_of(task);
  
  //time slice update
  if (cs->current->UpdateRemainingTime(/*isTaskOffCpu=*/true)) {
    cs->preempt_curr = true;
  }

  if (task->oncpu()) {
    CHECK_EQ(cs->current, task);
    cs->current = nullptr;
  } else {
    CHECK(from_switchto);
    CHECK_EQ(task->run_state, O1TaskState::kBlocked);
  }

  task->run_state =
      blocked ? O1TaskState::kBlocked : O1TaskState::kRunnable;
}

void O1Scheduler::TaskOnCpu(O1Task* task, Cpu cpu) {
  CpuState* cs = cpu_state(cpu);
  cs->current = task;

  task->run_state = O1TaskState::kOnCpu;
  task->SetRuntimeAtLastPick();
  task->cpu = cpu.id();
  task->preempted = false;
  task->prio_boost = false;

  GHOST_DPRINT(1, stderr, "[TaskOnCpu] cpu=%-2d tid=%-6d  remaining=%.2fms",
      cpu.id(), task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
}

void O1Scheduler::O1Schedule(const Cpu& cpu, BarrierToken agent_barrier,
                                 bool prio_boost) {
  CpuState* cs = cpu_state(cpu);
  O1Task* next = nullptr;

  if (cs->preempt_curr) {
    O1Task* prev = cs->current;
    if (prev) {
      GHOST_DPRINT(1, stderr,
          "[O1Schedule] cpu=%-2d  PREEMPT tid=%-6d  remaining=%.2fms -> re-enqueue",
          cpu.id(), prev->gtid.tid(),
          absl::ToDoubleMilliseconds(prev->remaining_time));
      TaskOffCpu(cs->current, /*blocked=*/false, /*from_switchto=*/false);
      cs->run_queue.Enqueue(prev);
    }
    cs->preempt_curr = false;
  }

  if (!prio_boost) {
    next = cs->current;
    if (!next) next = cs->run_queue.Dequeue();
  }

  RunRequest* req = enclave()->GetRunRequest(cpu);
  if (next) {
    // Wait for 'next' to get offcpu before switching to it. This might seem
    // superfluous because we don't migrate tasks past the initial assignment
    // of the task to a cpu. However a SwitchTo target can migrate and run on
    // another CPU behind the agent's back. This is usually undetectable from
    // the agent's pov since the SwitchTo target is blocked and thus !on_rq.
    //
    // However if 'next' happens to be the last task in a SwitchTo chain then
    // it is possible to process TASK_WAKEUP(next) before it has gotten off
    // the remote cpu. The 'on_cpu()' check below handles this scenario.
    //
    // See go/switchto-ghost for more details.
    while (next->status_word.on_cpu()) {
      Pause();
    }

    req->Open({
        .target = next->gtid,
        .target_barrier = next->seqnum,
        .agent_barrier = agent_barrier,
        .commit_flags = COMMIT_AT_TXN_COMMIT,
    });

    if (req->Commit()) {
      TaskOnCpu(next, cpu);
    } else {
      GHOST_DPRINT(1, stderr,
          "[O1Schedule] cpu=%-2d  COMMIT FAILED tid=%-6d  state=%d -> re-enqueue",
          cpu.id(), next->gtid.tid(), req->state());

      if (next == cs->current) {
        TaskOffCpu(next, /*blocked=*/false, /*from_switchto=*/false);
      }

      next->prio_boost = true;
      cs->run_queue.Enqueue(next);
    }
  } else {
    GHOST_DPRINT(1, stderr, "[O1Schedule] cpu=%-2d  IDLE", cpu.id());
    int flags = 0;
    if (prio_boost && (cs->current || !cs->run_queue.Empty())) {
      flags = RTLA_ON_IDLE;
    }
    req->LocalYield(agent_barrier, flags);
  }
}

void O1Scheduler::Schedule(const Cpu& cpu, const StatusWord& agent_sw) {
  BarrierToken agent_barrier = agent_sw.barrier();
  CpuState* cs = cpu_state(cpu);

  Message msg;

    while (!(msg = Peek(cs->channel.get())).empty()) {
      DispatchMessage(msg);
      Consume(cs->channel.get(), msg);
    }

  // O1Schedule(cpu, agent_barrier, agent_sw.boosted_priority());
  O1Schedule(cpu, agent_barrier, false);
}

void O1Rq::Enqueue(O1Task* task) {
  CHECK_GE(task->cpu, 0);
  CHECK_EQ(task->run_state, O1TaskState::kRunnable);

  task->run_state = O1TaskState::kQueued;

  absl::MutexLock lock(&mu_);

  if (task->remaining_time > absl::ZeroDuration()) {
    if (task->prio_boost)
      aq_.push_front(task);
    else
      aq_.push_back(task);
    GHOST_DPRINT(1, stderr,
        "[Enqueue -> active ] cpu=%-2d tid=%-6d  remaining=%.2fms  prio=%-5s  aq_size=%zu",
        task->cpu, task->gtid.tid(),
        absl::ToDoubleMilliseconds(task->remaining_time),
        task->prio_boost ? "HIGH" : "norm",
        aq_.size());
  } else {
    task->SetRemainingTime();
    if (task->prio_boost)
      eq_.push_front(task);
    else
      eq_.push_back(task);
    GHOST_DPRINT(1, stderr,
        "[Enqueue -> expired] cpu=%-2d tid=%-6d  timeslice_reset=%.2fms  prio=%-5s  eq_size=%zu",
        task->cpu, task->gtid.tid(),
        absl::ToDoubleMilliseconds(task->remaining_time),
        task->prio_boost ? "HIGH" : "norm",
        eq_.size());
  }
}

void O1Rq::EnqueueActive(O1Task* task) {
  CHECK_GE(task->cpu, 0);
  CHECK_EQ(task->run_state, O1TaskState::kRunnable);

  task->run_state = O1TaskState::kQueued;

  absl::MutexLock lock(&mu_);
  if (task->prio_boost)
    aq_.push_front(task);
  else
    aq_.push_back(task);
}

void O1Rq::EnqueueExpired(O1Task* task) {
  CHECK_GE(task->cpu, 0);
  CHECK_EQ(task->run_state, O1TaskState::kRunnable);

  task->run_state = O1TaskState::kQueued;

  absl::MutexLock lock(&mu_);
  task->SetRemainingTime();
  if (task->prio_boost)
    eq_.push_front(task);
  else
    eq_.push_back(task);
}

void O1Rq::Swap() {
  GHOST_DPRINT(1, stderr,
      "[Swap] active empty -> swap with expired  eq_size=%zu -> becomes active",
      eq_.size());
  std::swap(aq_, eq_);
}

O1Task* O1Rq::Dequeue() {
  absl::MutexLock lock(&mu_);
  if (aq_.empty()) {
    if (eq_.empty()) {
      GHOST_DPRINT(1, stderr, "[Dequeue] both queues empty -> idle");
      return nullptr;
    } else {
      Swap();
    }
  }

  O1Task* task = aq_.front();
  CHECK(task->queued());
  task->run_state = O1TaskState::kRunnable;
  aq_.pop_front();
  GHOST_DPRINT(1, stderr,
      "[Dequeue] cpu=%-2d tid=%-6d  remaining=%.2fms  aq_size=%zu",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time),
      aq_.size());
  return task;
}

void O1Rq::Erase(O1Task* task) {
  GHOST_DPRINT(1, stderr, "[Erase] cpu=%-2d tid=%-6d  remaining=%.2fms",
      task->cpu, task->gtid.tid(),
      absl::ToDoubleMilliseconds(task->remaining_time));
  CHECK_EQ(task->run_state, O1TaskState::kQueued);
  absl::MutexLock lock(&mu_);
  size_t size = aq_.size();
  if (size > 0) {
    // Check if 'task' is at the back of the runqueue (common case).
    size_t pos = size - 1;
    if (aq_[pos] == task) {
      aq_.erase(aq_.cbegin() + pos);
      task->run_state = O1TaskState::kRunnable;
      return;
    }

    // Now search for 'task' from the beginning of the runqueue.
    for (pos = 0; pos < size - 1; pos++) {
      if (aq_[pos] == task) {
        aq_.erase(aq_.cbegin() + pos);
        task->run_state =  O1TaskState::kRunnable;
        return;
      }
    }
  }
  
  size = eq_.size();
  if (size > 0) {
    // Check if 'task' is at the back of the runqueue (common case).
    size_t pos = size - 1;
    if (eq_[pos] == task) {
      eq_.erase(eq_.cbegin() + pos);
      task->run_state = O1TaskState::kRunnable;
      return;
    }

    // Now search for 'task' from the beginning of the runqueue.
    for (pos = 0; pos < size - 1; pos++) {
      if (eq_[pos] == task) {
        eq_.erase(eq_.cbegin() + pos);
        task->run_state =  O1TaskState::kRunnable;
        return;
      }
    }
  }
  CHECK(false);
}

std::unique_ptr<O1Scheduler> MultiThreadedO1Scheduler(Enclave* enclave,
                                                          CpuList cpulist) {
  auto allocator = std::make_shared<ThreadSafeMallocTaskAllocator<O1Task>>();
  auto scheduler = std::make_unique<O1Scheduler>(enclave, std::move(cpulist),
                                                   std::move(allocator));
  return scheduler;
}

void O1Agent::AgentThread() {
  gtid().assign_name("Agent:" + std::to_string(cpu().id()));
  if (verbose() > 1) {
    printf("Agent tid:=%d\n", gtid().tid());
  }
  SignalReady();
  WaitForEnclaveReady();

  PeriodicEdge debug_out(absl::Seconds(1));

  while (!Finished() || !scheduler_->Empty(cpu())) {
    scheduler_->Schedule(cpu(), status_word());

    if (verbose() && debug_out.Edge()) {
      static const int flags = verbose() > 1 ? Scheduler::kDumpStateEmptyRQ : 0;
      if (scheduler_->debug_runqueue_) {
        scheduler_->debug_runqueue_ = false;
        scheduler_->DumpState(cpu(), Scheduler::kDumpAllTasks);
      } else {
        scheduler_->DumpState(cpu(), flags);
      }
    }
  }
}

std::ostream& operator<<(std::ostream& os, const O1TaskState& state) {
  switch (state) {
    case O1TaskState::kBlocked:
      return os << "kBlocked";
    case O1TaskState::kRunnable:
      return os << "kRunnable";
    case O1TaskState::kQueued:
      return os << "kQueued";
    case O1TaskState::kOnCpu:
      return os << "kOnCpu";
  }
}

}  //  namespace ghost
