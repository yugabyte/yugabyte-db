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

#ifndef YB_UTIL_OPERATION_COUNTER_H
#define YB_UTIL_OPERATION_COUNTER_H

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "yb/util/debug-util.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"

#ifndef NDEBUG
#include "yb/util/debug/long_operation_tracker.h"
#endif

namespace yb {

YB_STRONGLY_TYPED_BOOL(Stop);
YB_STRONGLY_TYPED_BOOL(Unlock);

class ScopedOperation;
class ScopedRWOperation;

// Class that counts acquired tokens and don't shutdown until this count drops to zero.
class OperationCounter {
 public:
  explicit OperationCounter(const std::string& log_prefix);

  void Shutdown();

  void Acquire();
  void Release();

 private:
  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  std::string log_prefix_;
  std::atomic<size_t> value_{0};
};

class ScopedOperation {
 public:
  ScopedOperation() = default;
  explicit ScopedOperation(OperationCounter* counter);

 private:
  struct ScopedCounterDeleter {
    void operator()(OperationCounter* counter) {
      counter->Release();
    }
  };

  std::unique_ptr<OperationCounter, ScopedCounterDeleter> counter_;
};

// This is used to track the number of pending operations using a certain resource (as of Apr 2018
// just the RocksDB database within a tablet) so we can safely wait for all operations to complete
// and destroy or replace the resource. This is similar to a shared mutex, but allows fine-grained
// control, such as preventing new operations from being started.
class RWOperationCounter {
 public:
  CHECKED_STATUS DisableAndWaitForOps(const MonoDelta& timeout, Stop stop);

  // Due to the thread restriction of "timed_mutex::unlock()", this Unlock method must be called
  // in the same thread that invoked DisableAndWaitForOps(). This is fine for truncate, snapshot
  // restore, and tablet shutdown operations.
  void Enable(Unlock unlock, Stop was_stop);

  void UnlockExclusiveOpMutex() {
    disable_.unlock();
  }

  bool Increment();

  void Decrement() { Update(-1); }
  uint64_t Get() const {
    return counters_.load(std::memory_order::memory_order_acquire);
  }

  // Return pending operations counter value only.
  uint64_t GetOpCounter() const;

  bool WaitMutexAndIncrement(CoarseTimePoint deadline);

 private:
  CHECKED_STATUS WaitForOpsToFinish(
      const CoarseTimePoint& start_time, const CoarseTimePoint& deadline);

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

// A convenience class to automatically increment/decrement a RWOperationCounter. This is used
// for regular RocksDB read/write operations that are allowed to proceed in parallel. Constructing
// a ScopedRWOperation might fail because the counter is in the disabled state. An instance
// of this class resembles a Result or a Status, because it can be used with the RETURN_NOT_OK
// macro.
class ScopedRWOperation {
 public:
  // Object is not copyable, but movable.
  void operator=(const ScopedRWOperation&) = delete;
  ScopedRWOperation(const ScopedRWOperation&) = delete;

  explicit ScopedRWOperation(RWOperationCounter* counter = nullptr,
                             const CoarseTimePoint& deadline = CoarseTimePoint());

  ScopedRWOperation(ScopedRWOperation&& op)
      : counter_(op.counter_)
#ifndef NDEBUG
      , long_operation_tracker_(std::move(op.long_operation_tracker_))
#endif
      {
    op.counter_ = nullptr; // Moved ownership.
  }

  ~ScopedRWOperation();

  bool ok() const {
    return counter_ != nullptr;
  }

  void Reset();

 private:
  RWOperationCounter* counter_ = nullptr;
#ifndef NDEBUG
  LongOperationTracker long_operation_tracker_;
#endif
};

// RETURN_NOT_OK macro support.
// The error message currently mentions RocksDB because that is the only type of resource that
// this framework is used to protect as of Apr 2018.
inline Status MoveStatus(const ScopedRWOperation& scoped) {
  return scoped.ok() ? Status::OK() : STATUS(Busy, "RocksDB store is busy");
}

// A convenience class to automatically pause/resume a RWOperationCounter.
class ScopedRWOperationPause {
 public:
  // Object is not copyable, but movable.
  void operator=(const ScopedRWOperationPause&) = delete;
  ScopedRWOperationPause(const ScopedRWOperationPause&) = delete;

  ScopedRWOperationPause(RWOperationCounter* counter, const MonoDelta& timeout, Stop stop);

  ScopedRWOperationPause(ScopedRWOperationPause&& p)
      : counter_(p.counter_), status_(std::move(p.status_)), was_stop_(p.was_stop_) {
    p.counter_ = nullptr; // Moved ownership.
  }

  // This is called during tablet shutdown to release the mutex that we took to prevent concurrent
  // exclusive-ownership operations on the RocksDB instance, such as truncation and snapshot
  // restoration. It is fine to release the mutex because these exclusive operations are not allowed
  // to happen after tablet shutdown anyway.
  void ReleaseMutexButKeepDisabled();

  // See RWOperationCounter::Enable() for the thread restriction.
  ~ScopedRWOperationPause() {
    if (counter_ != nullptr) {
      counter_->Enable(Unlock(status_.ok()), was_stop_);
    }
  }

  bool ok() const {
    return status_.ok();
  }

  Status&& status() {
    return std::move(status_);
  }

 private:
  RWOperationCounter* counter_ = nullptr;
  Status status_;
  Stop was_stop_;
};

// RETURN_NOT_OK macro support.
inline Status&& MoveStatus(ScopedRWOperationPause&& p) {
  return std::move(p.status());
}

} // namespace yb

#endif // YB_UTIL_OPERATION_COUNTER_H
