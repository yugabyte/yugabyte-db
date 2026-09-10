// Copyright (c) YugabyteDB, Inc.
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
#pragma once

#include <atomic>

#include "yb/gutil/macros.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

namespace yb {
namespace master {

// Tracks completion of a batch of AsyncAlterTable RPCs that were dispatched together (e.g. the
// per-tablet fan-out from CreateCDCStream's SetAllCDCSDKRetentionBarriers for one batch of
// tables). The dispatcher constructs a tracker pre-sized to the total number of per-tablet RPCs
// the batch will produce. Each AsyncAlterTable's terminal Finished() callback calls OnComplete()
// to drop one count. The dispatcher waits on the tracker before moving on to the next batch.
//
// The tracker captures the first non-OK status seen across the batch. Subsequent errors are
// silently dropped to keep the contract simple. The dispatcher is responsible for cleanup
// (e.g. invoking RollbackFailedCreateCDCSDKStream when Wait returns a non-OK status).
class AlterTableBatchTracker {
 public:
  // expected_count is the total number of per-tablet AsyncAlterTable terminal events the
  // dispatcher will produce.
  explicit AlterTableBatchTracker(int expected_count)
      : latch_(expected_count) {}

  // Called from AsyncAlterTable::Finished() when the task reaches a terminal state (success or
  // permanent failure). Idempotent at the task level since Finished() fires exactly once per
  // task instance.
  void OnComplete(const Status& s) {
    if (!s.ok()) {
      RecordFirstError(s);
    }
    latch_.CountDown();
  }

  // Called by the dispatcher when an AlterTable() call returns an error before its per-tablet
  // AsyncAlterTable tasks were scheduled. We pre-counted those tablets into the latch up front;
  // this drains that share so Wait() doesn't hang. The error is recorded as the batch's first
  // error if no error has been recorded yet.
  void OnDispatchSkipped(int num_tablets, const Status& s) {
    if (!s.ok()) {
      RecordFirstError(s);
    }
    for (int i = 0; i < num_tablets; ++i) {
      latch_.CountDown();
    }
  }

  // Blocks until either the latch reaches 0 or the deadline expires.
  //   - returns the first error recorded via OnComplete / OnDispatchSkipped, if any;
  //   - returns TimedOut if the deadline expired before the latch drained;
  //   - returns OK otherwise.
  Status Wait(CoarseTimePoint deadline) {
    const auto now = CoarseMonoClock::Now();
    if (deadline <= now) {
      // No time left -- still try a non-blocking check in case everything's already done.
      if (latch_.count() == 0) {
        return FirstError();
      }
      return STATUS(TimedOut,
          "Deadline already passed while waiting for AsyncAlterTable batch to complete");
    }
    const auto timeout = MonoDelta::FromMicroseconds(
        ToMicroseconds(deadline - now));
    if (!latch_.WaitFor(timeout)) {
      return STATUS_FORMAT(TimedOut,
          "Timed out waiting for AsyncAlterTable batch to complete; remaining=$0",
          latch_.count());
    }
    return FirstError();
  }

  // Number of outstanding events still expected. For diagnostics / logging.
  uint64_t outstanding() const { return latch_.count(); }

 private:
  void RecordFirstError(const Status& s) {
    if (has_error_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    std::lock_guard<simple_spinlock> l(error_lock_);
    first_error_ = s;
  }

  Status FirstError() {
    if (!has_error_.load(std::memory_order_acquire)) {
      return Status::OK();
    }
    std::lock_guard<simple_spinlock> l(error_lock_);
    return first_error_;
  }

  CountDownLatch latch_;
  std::atomic<bool> has_error_{false};
  mutable simple_spinlock error_lock_;
  Status first_error_ GUARDED_BY(error_lock_);

  DISALLOW_COPY_AND_ASSIGN(AlterTableBatchTracker);
};

}  // namespace master
}  // namespace yb
