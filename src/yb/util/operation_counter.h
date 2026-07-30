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
#include <condition_variable>
#include <mutex>

#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(Stop);
YB_STRONGLY_TYPED_BOOL(Unlock);

// The shared/exclusive counter and the exclusive-disable mutex behind RWOperationCounter, kept
// together so the mutex's wait can observe the counter's terminal "stopped" state. counters_ packs
// the number of in-flight shared operations, the disable count, and the stop bit (see the .cc for
// the bit layout). Operations waiting in WaitMutexAndIncrement bail out with kStopped as soon as
// the stop bit is set, instead of blocking on the exclusive lock -- which, for a stop, is held
// until the guarded resource is destroyed. Using counters_ as the single source of truth keeps the
// state consistent with the lock-free fast path. See issue #32211. Used only by RWOperationCounter.
class RWOperationCounterLock {
 public:
  enum class IncrementResult {
    kSuccess,
    kFailed,
    kStopped,
  };

  // Lock-free fast-path shared increment. Returns true on success, false if the resource is
  // disabled or stopped (caller should fall back to WaitMutexAndIncrement).
  bool Increment();

  void Decrement();

  uint64_t Get() const {
    return counters_.load(std::memory_order::acquire);
  }

  // Number of in-flight shared operations only.
  uint64_t GetOpCounter() const;

  // Waits until the resource can be exclusively locked (no in-flight disable), increments the
  // shared count, and releases the lock -- returning kSuccess. Returns kStopped if the resource is
  // (or becomes) stopped, or kFailed on deadline. A stopped resource is reported promptly even
  // though the exclusive lock stays held for the whole shutdown. See issue #32211.
  IncrementResult WaitMutexAndIncrement(CoarseTimePoint deadline);

  // Acquires the exclusive lock, blocking until it is free or the deadline passes. Returns false on
  // timeout. Used by DisableAndWaitForOps; does not consult the stop state (the caller is the one
  // about to set it).
  bool Lock(const CoarseTimePoint& deadline);

  // Sets the disable bit (or, if stop, the stop bit) and, for a stop, wakes operations waiting in
  // WaitMutexAndIncrement so they bail out. Returns the resulting counter value.
  uint64_t Disable(Stop stop);

  // Clears the disable bit (or, if was_stop, the stop bit); optionally releases the exclusive lock.
  void Enable(Unlock unlock, Stop was_stop);

  // Releases the exclusive lock and wakes waiters.
  void unlock();

  uint64_t TEST_GetDisableCount() const;
  bool TEST_IsStopped() const;

 private:
  uint64_t Update(uint64_t delta);
  bool Stopped() const;

  std::atomic<uint64_t> counters_{0};
  std::mutex mutex_;
  std::condition_variable condition_variable_;
  bool is_locked_ = false;
};

class ScopedOperation;

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

// This is used to track the number of pending operations using a certain resource (such as
// the RocksDB database or the schema within a tablet) so we can safely wait for all operations to
// complete and destroy or replace the resource. This is similar to a shared mutex, but allows
// fine-grained control, such as preventing new operations from being started.
class RWOperationCounter {
 public:
  using IncrementResult = RWOperationCounterLock::IncrementResult;

  explicit RWOperationCounter(const std::string& resource_name) : resource_name_(resource_name) {}

  Status DisableAndWaitForOps(const CoarseTimePoint& deadline, Stop stop);

  void Enable(Unlock unlock, Stop was_stop) { lock_.Enable(unlock, was_stop); }

  void UnlockExclusiveOpMutex() { lock_.unlock(); }

  bool Increment() { return lock_.Increment(); }

  void Decrement() { lock_.Decrement(); }

  uint64_t Get() const { return lock_.Get(); }

  // Return pending operations counter value only.
  uint64_t GetOpCounter() const { return lock_.GetOpCounter(); }

  IncrementResult WaitMutexAndIncrement(CoarseTimePoint deadline) {
    return lock_.WaitMutexAndIncrement(deadline);
  }

  std::string resource_name() const {
    return resource_name_;
  }

  uint64_t TEST_GetDisableCount() const { return lock_.TEST_GetDisableCount(); }

  bool TEST_IsStopped() const { return lock_.TEST_IsStopped(); }

 private:
  Status WaitForOpsToFinish(
      const CoarseTimePoint& start_time, const CoarseTimePoint& deadline);

  RWOperationCounterLock lock_;

  std::string resource_name_;
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

  ScopedRWOperation()
      : ScopedRWOperation(
            /* counter = */ nullptr,
            /* abort_status_holder = */ nullptr,
            /* deadline = */ CoarseTimePoint()) {}

  explicit ScopedRWOperation(
      RWOperationCounter* counter, const CoarseTimePoint& deadline = CoarseTimePoint())
      : ScopedRWOperation(counter, /* abort_status_holder = */ nullptr, deadline) {}

  explicit ScopedRWOperation(
      RWOperationCounter* counter, const StatusHolder& abort_status_holder,
      const CoarseTimePoint& deadline = CoarseTimePoint())
      : ScopedRWOperation(counter, &abort_status_holder, deadline) {}

  ScopedRWOperation(ScopedRWOperation&& op) : data_{std::move(op.data_)} {
    op.data_.counter_ = nullptr;  // Moved ownership.
    op.data_.abort_status_holder_ = nullptr;
  }

  ~ScopedRWOperation();

  void operator=(ScopedRWOperation&& op) {
    Reset();
    data_ = std::move(op.data_);
    op.data_.counter_ = nullptr;
    op.data_.abort_status_holder_ = nullptr;
  }

  bool ok() const {
    return data_.counter_ != nullptr;
  }

  Status GetAbortedStatus() const;

  void Reset();

  std::string resource_name() const;

  bool stopped() const { return data_.stopped_; }

  static ScopedRWOperation TEST_Create() { return ScopedRWOperation(); }

 private:
  explicit ScopedRWOperation(
      RWOperationCounter* counter, const StatusHolder* abort_status_holder,
      const CoarseTimePoint& deadline);

  struct Data {
    RWOperationCounter* counter_ = nullptr;
    const StatusHolder* abort_status_holder_ = nullptr;
    std::string resource_name_;
#ifndef NDEBUG
    LongOperationTracker long_operation_tracker_;
#endif
    bool stopped_ = false;
  };

  Data data_;
};

// RETURN_NOT_OK macro support.
Status MoveStatus(const ScopedRWOperation& scoped);

// A convenience class to automatically pause/resume/stop a RWOperationCounter.
class ScopedRWOperationPause {
 public:
  // Object is not copyable, but movable.
  void operator=(const ScopedRWOperationPause&) = delete;
  ScopedRWOperationPause(const ScopedRWOperationPause&) = delete;

  ScopedRWOperationPause() {}
  // If stop is false, ScopedRWOperation constructor will wait while ScopedRWOperationPause is
  // alive.
  // If stop is true, ScopedRWOperation constructor will create an instance with an error (see
  // ScopedRWOperation::ok()) while ScopedRWOperationPause is alive.
  ScopedRWOperationPause(RWOperationCounter* counter, const CoarseTimePoint& deadline, Stop stop);

  ScopedRWOperationPause(ScopedRWOperationPause&& p) : data_(std::move(p.data_)) {
    p.data_.counter_ = nullptr;  // Moved ownership.
  }

  ~ScopedRWOperationPause();

  void Reset();

  std::string resource_name() const {
    return data_.counter_ ? data_.counter_->resource_name() : "null";
  }

  void operator=(ScopedRWOperationPause&& p) {
    Reset();
    data_ = std::move(p.data_);
    p.data_.counter_ = nullptr;  // Moved ownership.
  }

  // This is called during tablet shutdown to release the mutex that we took to prevent concurrent
  // exclusive-ownership operations on the RocksDB instance, such as truncation and snapshot
  // restoration. It is fine to release the mutex because these exclusive operations are not allowed
  // to happen after tablet shutdown anyway.
  void ReleaseMutexButKeepDisabled();

  bool ok() const {
    return data_.status_.ok();
  }

  Status&& status() {
    return std::move(data_.status_);
  }

 private:
  struct Data {
    RWOperationCounter* counter_ = nullptr;
    Status status_;
    Stop was_stop_ = Stop::kFalse;
  };
  Data data_;
};

// RETURN_NOT_OK macro support.
inline Status&& MoveStatus(ScopedRWOperationPause&& p) {
  return std::move(p.status());
}

} // namespace yb
