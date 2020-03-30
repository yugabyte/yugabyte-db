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

#include "yb/util/operation_counter.h"

#include <thread>

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"

#include "yb/util/logging.h"

using strings::Substitute;

namespace yb {

OperationCounter::OperationCounter(const std::string& log_prefix) : log_prefix_(log_prefix) {
}

void OperationCounter::Shutdown() {
  auto wait_start = CoarseMonoClock::now();
  auto last_report = wait_start;
  for (;;) {
    auto value = value_.load(std::memory_order_acquire);
    if (value == 0) {
      break;
    }
    auto now = CoarseMonoClock::now();
    if (now > last_report + std::chrono::seconds(10)) {
      LOG_WITH_PREFIX(WARNING)
          << "Long wait for scope counter shutdown " << value << ": " << AsString(now - wait_start);
      last_report = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void OperationCounter::Release() {
  value_.fetch_sub(1, std::memory_order_acq_rel);
}

void OperationCounter::Acquire() {
  value_.fetch_add(1, std::memory_order_acq_rel);
}

ScopedOperation::ScopedOperation(OperationCounter* counter) : counter_(counter) {
  counter->Acquire();
}

uint64_t RWOperationCounter::Update(uint64_t delta) {
  const uint64_t result = counters_.fetch_add(delta, std::memory_order::memory_order_release);
  VLOG(2) << "[" << this << "] Update(" << static_cast<int64_t>(delta) << "), result = " << result;
  // Ensure that there is no underflow in either counter.
  DCHECK_EQ((result & (1ull << 63)), 0); // Counter of DisableAndWaitForOps() calls.
  DCHECK_EQ((result & (kDisabledDelta >> 1)), 0); // Counter of pending operations.
  return result;
}

// The implementation is based on OperationTracker::WaitForAllToFinish.
Status RWOperationCounter::WaitForOpsToFinish(const MonoDelta& timeout) {
  const int complain_ms = 1000;
  const MonoTime start_time = MonoTime::Now();
  int64_t num_pending_ops = 0;
  int num_complaints = 0;
  int wait_time_usec = 250;
  while ((num_pending_ops = GetOpCounter()) > 0) {
    const MonoDelta diff = MonoTime::Now() - start_time;
    if (diff > timeout) {
      return STATUS(TimedOut, Substitute(
          "Timed out waiting for all pending operations to complete. "
          "$0 transactions pending. Waited for $1",
          num_pending_ops, diff.ToString()));
    }
    const int64_t waited_ms = diff.ToMilliseconds();
    if (waited_ms / complain_ms > num_complaints) {
      LOG(WARNING) << Substitute("Waiting for $0 pending operations to complete now for $1 ms",
                                 num_pending_ops, waited_ms);
      num_complaints++;
    }
    wait_time_usec = std::min(wait_time_usec * 5 / 4, 1000000);
    SleepFor(MonoDelta::FromMicroseconds(wait_time_usec));
  }
  CHECK_EQ(num_pending_ops, 0) << "Number of pending operations must be 0";

  const MonoTime deadline = start_time + timeout;
  if (PREDICT_FALSE(!disable_.try_lock_until(deadline.ToSteadyTimePoint()))) {
    return STATUS(TimedOut, "Timed out waiting to disable the resource exclusively");
  }

  return Status::OK();
}

}  // namespace yb
