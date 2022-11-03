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

Strand::Strand(ThreadPool* thread_pool) : thread_pool_(*thread_pool) {}

Strand::~Strand() {
  const auto running = running_.load();
  const auto closing = closing_.load();
  const auto active_tasks = active_tasks_.load();
  LOG_IF(DFATAL, running || !closing || active_tasks) << Format(
      "Strand $0 has not been fully shut down, running: $1, closing: $2, active_tasks: $3",
      static_cast<void*>(this), running, closing, active_tasks);
}

void Strand::Enqueue(StrandTask* task) {
  if (closing_.load(std::memory_order_acquire)) {
    task->Done(STATUS(Aborted, "Strand closing"));
    return;
  }

  active_tasks_.fetch_add(1, std::memory_order_release);
  queue_.Push(task);

  bool expected = false;
  if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    thread_pool_.Enqueue(this);
  }
}

void Strand::Shutdown() {
  bool expected = false;
  if (!closing_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    LOG(DFATAL) << "Strand already closed";
    return;
  }

  while (active_tasks_.load(std::memory_order_acquire) ||
         running_.load(std::memory_order_acquire)) {
    // We expected shutdown to happen rarely, so just use busy wait here.
    std::this_thread::sleep_for(1ms);
  }
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
    running_.store(false, std::memory_order_release);
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

} // namespace rpc
} // namespace yb
