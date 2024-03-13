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
#include "yb/rpc/strand.h"

#include <thread>

#include "yb/util/flags.h"
#include "yb/util/status.h"

DEFINE_test_flag(int32, strand_done_inject_delay_ms, 0,
                 "Inject into Strand::Done after resetting running flag.");

using namespace std::literals;

namespace yb {
namespace rpc {

namespace {

const Status& StrandAbortedStatus() {
  static const Status result = STATUS(Aborted, "Strand shutdown");
  return result;
}

}

Strand::Strand(ThreadPool* thread_pool) : ThreadSubPoolBase(thread_pool) {}

Strand::~Strand() {
  const auto running = running_.load();
  const auto closing = closing_.load();
  const auto active_tasks = active_tasks_.load();
  LOG_IF(DFATAL, running || !closing || active_tasks) << Format(
      "Strand $0 has not been fully shut down, running: $1, closing: $2, active_tasks: $3",
      static_cast<void*>(this), running, closing, active_tasks);
}

bool Strand::Enqueue(StrandTask* task) {
  if (closing_.load(std::memory_order_acquire)) {
    task->Done(STATUS(Aborted, "Strand closing"));
    return false;
  }

  // Increment with memory_order_acq_rel rather than with memory_order_release: we don't want the
  // Push or the compare/exchange on running_ later in the code to be reordered before this
  // increment.
  active_tasks_.fetch_add(1, std::memory_order_acq_rel);

  queue_.Push(task);

  bool expected = false;
  if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    // The Done method will handle the failure to enqueue.
    thread_pool_.Enqueue(this);
  }
  return true;
}

void Strand::Run() {
  // Actual work is performed in Done.
  // Because we need `Status`, i.e. if `Strand` task aborted, because of thread pool shutdown.
  // We should abort all enqueued tasks.
}

void Strand::Done(const Status& status) {
  for (;;) {
    size_t tasks_fetched = 0;
    while (StrandTask *task = queue_.Pop()) {
      ++tasks_fetched;

      const auto& actual_status =
          !closing_.load(std::memory_order_acquire) ? status : StrandAbortedStatus();
      if (actual_status.ok()) {
        task->Run();
      }
      task->Done(actual_status);
    }

    // Using sequential consistency here instead of memory_order_release to prevent reordering of
    // later reads/writes before this assignment and make it easier to reason about avoiding issues
    // similar to the one fixed by the following commit:
    // https://github.com/yugabyte/yugabyte-db/commit/8f9d00638387cff6fc1407229dce47338e89112a
    running_ = false;

    if (FLAGS_TEST_strand_done_inject_delay_ms > 0) {
      std::this_thread::sleep_for(FLAGS_TEST_strand_done_inject_delay_ms * 1ms);
    }
    // Decrease active_tasks_ and check whether tasks have been added while we were setting running
    // to false.
    if (active_tasks_.fetch_sub(tasks_fetched) > tasks_fetched) {
      // Got more operations, try stay in the loop.
      bool expected = false;
      if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        continue;
      }
      // If someone else has flipped running_ to true, we can safely exit this function because
      // another task is already submitted to thread pool.
    }
    break;
  }
}

bool Strand::IsIdle() {
  // Use sequential consistency for safety. This is only used during shutdown.
  return ThreadSubPoolBase::IsIdle() && !running_;
}

} // namespace rpc
} // namespace yb
