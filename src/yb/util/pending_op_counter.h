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

#ifndef YB_UTIL_PENDING_OP_COUNTER_H_
#define YB_UTIL_PENDING_OP_COUNTER_H_

#include <atomic>
#include <mutex>

#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {
namespace util {

// This is used to track the number of pending operations using a certain resource (as of Apr 2018
// just the RocksDB database within a tablet) so we can safely wait for all operations to complete
// and destroy or replace the resource. This is similar to a shared mutex, but allows fine-grained
// control, such as preventing new operations from being started.
class PendingOperationCounter {
 public:
  // Using upper bits of counter as special flags.
  static constexpr uint64_t kDisabledDelta = 1ull << 48;
  static constexpr uint64_t kOpCounterMask = kDisabledDelta - 1;
  static constexpr uint64_t kDisabledCounterMask = ~kOpCounterMask;

  CHECKED_STATUS DisableAndWaitForOps(const MonoDelta& timeout) {
    Update(kDisabledDelta);
    return WaitForOpsToFinish(timeout);
  }

  // Due to the thread restriction of "timed_mutex::unlock()", this Unlock method must be called
  // in the same thread that invoked DisableAndWaitForOps(). This is fine for truncate, snapshot
  // restore, and tablet shutdown operations.
  void Enable(const bool unlock) {
    Update(-kDisabledDelta);
    if (unlock) {
      UnlockExclusiveOpMutex();
    }
  }

  void UnlockExclusiveOpMutex() {
    disable_.unlock();
  }

  uint64_t Increment() { return Update(1); }
  void Decrement() { Update(-1); }
  uint64_t Get() const {
    return counters_.load(std::memory_order::memory_order_acquire);
  }

  // Return pending operations counter value only.
  uint64_t GetOpCounter() const {
    return Get() & kOpCounterMask;
  }

  bool IsReady() const {
    return (Get() & kDisabledCounterMask) == 0;
  }

 private:
  CHECKED_STATUS WaitForOpsToFinish(const MonoDelta& timeout);

  uint64_t Update(uint64_t delta);

  // The upper 16 bits are used for storing the number of separate operations that have disabled the
  // resource. E.g. tablet shutdown running at the same time with Truncate/RestoreSnapshot.
  // The lower 48 bits are used to keep track of the number of concurrent read/write operations.
  std::atomic<uint64_t> counters_{0};

  // Mutex to disable the resource exclusively. This mutex is locked by DisableAndWaitForOps after
  // waiting for all shared-ownership operations to complete. We need this to avoid a race condition
  // between Raft operations that replace RocksDB (apply snapshot / truncate) and tablet shutdown.
  std::timed_mutex disable_;
};

// A convenience class to automatically increment/decrement a PendingOperationCounter. This is used
// for regular RocksDB read/write operations that are allowed to proceed in parallel. Constructing
// a ScopedPendingOperation might fail because the counter is in the disabled state. An instance
// of this class resembles a Result or a Status, because it can be used with the RETURN_NOT_OK
// macro.
class ScopedPendingOperation {
 public:
  // Object is not copyable, but movable.
  void operator=(const ScopedPendingOperation&) = delete;
  ScopedPendingOperation(const ScopedPendingOperation&) = delete;

  explicit ScopedPendingOperation(PendingOperationCounter* counter)
      : counter_(counter), ok_(false) {
    if (counter != nullptr) {
      if (counter_->IsReady()) {
        // The race condition between IsReady() and Increment() is OK, because we are checking if
        // anyone has started an exclusive operation since we did the increment, and don't proceed
        // with this shared-ownership operation in that case.
        ok_ = (counter->Increment() & PendingOperationCounter::kDisabledCounterMask) == 0;
      } else {
        ok_ = false;
        counter_ = nullptr; // Avoid decrementing the counter.
      }
    }
  }

  ScopedPendingOperation(ScopedPendingOperation&& op)
      : counter_(op.counter_), ok_(op.ok_) {
    op.counter_ = nullptr; // Moved ownership.
  }

  ~ScopedPendingOperation() {
    if (counter_ != nullptr) {
      counter_->Decrement();
    }
  }

  bool ok() const {
    return ok_;
  }

 private:
  PendingOperationCounter* counter_;

  bool ok_;
};

// RETURN_NOT_OK macro support.
// The error message currently mentions RocksDB because that is the only type of resource that
// this framework is used to protect as of Apr 2018.
inline Status MoveStatus(const ScopedPendingOperation& scoped) {
  return scoped.ok() ? Status::OK() : STATUS(Busy, "RocksDB store is busy");
}

// A convenience class to automatically pause/resume a PendingOperationCounter.
class ScopedPendingOperationPause {
 public:
  // Object is not copyable, but movable.
  void operator=(const ScopedPendingOperationPause&) = delete;
  ScopedPendingOperationPause(const ScopedPendingOperationPause&) = delete;

  ScopedPendingOperationPause(PendingOperationCounter* counter, const MonoDelta& timeout)
      : counter_(counter) {
    if (counter != nullptr) {
      status_ = counter->DisableAndWaitForOps(timeout);
    }
  }

  ScopedPendingOperationPause(ScopedPendingOperationPause&& p)
      : counter_(p.counter_), status_(std::move(p.status_)) {
    p.counter_ = nullptr; // Moved ownership.
  }

  // This is called during tablet shutdown to release the mutex that we took to prevent concurrent
  // exclusive-ownership operations on the RocksDB instance, such as truncation and snapshot
  // restoration. It is fine to release the mutex because these exclusive operations are not allowed
  // to happen after tablet shutdown anyway.
  void ReleaseMutexButKeepDisabled() {
    CHECK_OK(status_);
    CHECK_NOTNULL(counter_);
    counter_->UnlockExclusiveOpMutex();
    // Make sure the destructor has no effect when it runs.
    counter_ = nullptr;
  }

  // See PendingOperationCounter::Enable() for the thread restriction.
  ~ScopedPendingOperationPause() {
    if (counter_ != nullptr) {
      counter_->Enable(status_.IsOk());
    }
  }

  bool ok() const {
    return status_.IsOk();
  }

  Status&& status() {
    return std::move(status_);
  }

 private:
  PendingOperationCounter* counter_;
  Status status_;
};

// RETURN_NOT_OK macro support.
inline Status&& MoveStatus(ScopedPendingOperationPause&& p) {
  return std::move(p.status());
}

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_PENDING_OP_COUNTER_H_
