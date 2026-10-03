// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#include "yb/util/strand.h"

#include <thread>

#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"

DEFINE_test_flag(int32, strand_done_inject_delay_ms, 0,
                 "Inject into Strand::Done after resetting running flag.");

using namespace std::literals;

namespace yb {

namespace {

const Status& StrandAbortedStatus() {
  static const Status result = STATUS(Aborted, "Strand shutdown");
  return result;
}

YB_DEFINE_ENUM(StrandTaskState, (kIdle)(kPending)(kRunning)(kStopped));

}

class Strand::Task : public ThreadPoolTask {
 public:
  explicit Task(std::atomic<size_t>& active_enqueues) : active_enqueues_(active_enqueues) {}

  ~Task() {
    const auto state = state_.load();
    const auto active_tasks = active_tasks_.load();
    LOG_IF(DFATAL, state != StrandTaskState::kStopped || active_tasks) << Format(
        "Strand task has not been fully shut down, state: $0, active_tasks: $1",
        state, active_tasks);
  }

  void BusyWait(bool destroy) {
    for (;;) {
      auto state = state_.load(std::memory_order_acquire);
      if (state == StrandTaskState::kIdle || state == StrandTaskState::kStopped) {
        if (active_tasks_.load(std::memory_order_acquire) == 0) {
          if (destroy) {
            DCHECK_EQ(state, StrandTaskState::kStopped);
            delete this;
          }
          break;
        }
      }
      std::this_thread::sleep_for(1ms);
    }
  }

  bool Enqueue(StrandTask* task) {
    // Increment with memory_order_acq_rel rather than with memory_order_release: we don't want the
    // Push or the compare/exchange on submitted_ later in the code to be reordered before this
    // increment.
    active_tasks_.fetch_add(1, std::memory_order_acq_rel);

    queue_.Push(task);

    auto expected = StrandTaskState::kIdle;
    return state_.compare_exchange_strong(
        expected, StrandTaskState::kPending, std::memory_order_acq_rel);
  }

  void Abort() {
    auto old_state = state_.exchange(StrandTaskState::kStopped, std::memory_order_acq_rel);
    if (old_state != StrandTaskState::kPending) {
      DCHECK_NE(old_state, StrandTaskState::kStopped);
      return BusyWait(true);
    }
    size_t processed_tasks = 0;
    while (auto* task = queue_.Pop()) {
      task->Done(StrandAbortedStatus());
      ++processed_tasks;
    }
    active_tasks_ -= processed_tasks;
  }

 private:
  void Done(const Status& status) override;

  void Run() override {
  }

  std::atomic<size_t>& active_enqueues_;

  // Whether the top-level strand task is currently running in the underlying thread pool.
  std::atomic<StrandTaskState> state_{StrandTaskState::kIdle};

  MPSCQueue<StrandTask> queue_;

  std::atomic<size_t> active_tasks_{0};
};

Strand::Strand(YBThreadPool* thread_pool)
    : ThreadSubPoolBase(thread_pool),
      task_(new Task(active_enqueues_)) {
  task_->set_run_token(run_token_);
}

Strand::~Strand() {
  const auto closing = Closing();
  LOG_IF(DFATAL, !closing) << Format(
      "Strand $0 has not been shut down, closing: $1",
      static_cast<void*>(this), closing);
}

bool Strand::Enqueue(StrandTask* task) {
  return EnqueueHelper([this, task](bool ok) {
    if (ok) {
      if (task_->Enqueue(task)) {
        // The Done method will handle the failure to enqueue.
        thread_pool_.Enqueue(task_.get());
      }

      return true;
    }
    task->Done(STATUS(Aborted, "Strand closing"));
    return false;
  });
}

void Strand::Task::Done(const Status& status) {
  {
    auto expected = StrandTaskState::kPending;
    if (!state_.compare_exchange_strong(
            expected, StrandTaskState::kRunning, std::memory_order_acq_rel)) {
      DCHECK_EQ(expected, StrandTaskState::kStopped);
      DCHECK(!status.ok());
      return BusyWait(true);
    }
  }

  for (;;) {
    size_t tasks_fetched = 0;
    while (auto* task = queue_.Pop()) {
      ++tasks_fetched;

      auto running = active_enqueues_.load(std::memory_order_acquire) < Strand::kStopMark;
      const auto& actual_status = running ? status : StrandAbortedStatus();
      if (actual_status.ok()) {
        task->Run();
      }
      task->Done(actual_status);
    }

    // Using sequential consistency here instead of memory_order_release to prevent reordering of
    // later reads/writes before this assignment and make it easier to reason about avoiding issues
    // similar to the one fixed by the following commit:
    // https://github.com/yugabyte/yugabyte-db/commit/8f9d00638387cff6fc1407229dce47338e89112a
    auto old_state = StrandTaskState::kRunning;
    state_.compare_exchange_strong(old_state, StrandTaskState::kIdle, std::memory_order_acq_rel);

    if (FLAGS_TEST_strand_done_inject_delay_ms > 0) {
      std::this_thread::sleep_for(FLAGS_TEST_strand_done_inject_delay_ms * 1ms);
    }
    // Decrease active_tasks_ and check whether tasks have been added while we were setting running
    // to false.
    if (active_tasks_.fetch_sub(tasks_fetched) > tasks_fetched) {
      // Got more operations, try stay in the loop.
      auto expected = StrandTaskState::kIdle;
      if (state_.compare_exchange_strong(expected, old_state, std::memory_order_acq_rel)) {
        continue;
      }
      if (old_state == StrandTaskState::kStopped) {
        DCHECK_EQ(expected, StrandTaskState::kStopped);
        continue;
      }
      // If someone else has flipped submitted_ to true, we can safely exit this function because
      // another task is already submitted to thread pool.
    }
    break;
  }
}

void Strand::AbortTasks() {
  task_.release()->Abort();
}

void Strand::TEST_BusyWait() {
  task_->BusyWait(false);
}

} // namespace yb
