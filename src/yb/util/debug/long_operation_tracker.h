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

#include <mutex>

#include "yb/gutil/ref_counted.h"

#include "yb/util/monotime.h"

namespace yb {

// Tracks long running operation.
// If it does not complete within specified duration warning is added to log.
// Warning contains stack trace of thread that created this tracker.
//
// Registration is lock-free for typical durations: a background checker thread picks up new
// operations when it scans its intake queue, at least every 100ms while it is otherwise idle.
// A warning is therefore typically logged within 100ms of the operation deadline, though scans
// can be delayed while the checker thread is dumping stacks of other overdue operations.
// Registering an operation with a duration shorter than 200ms additionally wakes the checker
// thread, so that the stack trace warning is not skipped when such an operation expires and
// completes between two scans.
class LongOperationTracker {
 public:
  LongOperationTracker(const char* message, MonoDelta duration);

  LongOperationTracker(const LongOperationTracker&) = delete;
  void operator=(const LongOperationTracker&) = delete;

  // Defined out-of-line because they require a complete TrackedOperation type.
  LongOperationTracker();
  ~LongOperationTracker();
  LongOperationTracker(LongOperationTracker&& rhs);
  LongOperationTracker& operator=(LongOperationTracker&& rhs);

  void Swap(LongOperationTracker* rhs);

  struct TrackedOperation;

 private:
  scoped_refptr<TrackedOperation> tracked_operation_;
};


class TrackedUniqueLock {
 public:
  TrackedUniqueLock() = default;
  explicit TrackedUniqueLock(std::mutex& mutex) // NOLINT
      : lock_(mutex),
        lot_("TrackedUniqueLock", std::chrono::seconds(1)) {}

  void unlock() {
    lock_.unlock();
    lot_ = {};
  }

  void swap(TrackedUniqueLock& rhs) {
    lock_.swap(rhs.lock_);
    lot_.Swap(&rhs.lot_);
  }

  std::unique_lock<std::mutex>& impl() {
    return lock_;
  }

 private:
  std::unique_lock<std::mutex> lock_;
  LongOperationTracker lot_;
};

} // namespace yb
