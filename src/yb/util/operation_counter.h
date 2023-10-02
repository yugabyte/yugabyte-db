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

#pragma once

#include <atomic>
#include <mutex>

#include "yb/util/cross_thread_mutex.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(Stop);
YB_STRONGLY_TYPED_BOOL(Unlock);

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
  explicit RWOperationCounter(const std::string& resource_name) : resource_name_(resource_name) {}

  Status DisableAndWaitForOps(const CoarseTimePoint& deadline, Stop stop);

  void Enable(Unlock unlock, Stop was_stop);

  void UnlockExclusiveOpMutex() {
    disable_.unlock();
  }

  bool Increment();

  void Decrement() { Update(-1); }
  uint64_t Get() const {
    return counters_.load(std::memory_order::acquire);
  }

  // Return pending operations counter value only.
  uint64_t GetOpCounter() const;

  bool WaitMutexAndIncrement(CoarseTimePoint deadline);

  std::string resource_name() const {
    return resource_name_;
  }

  uint64_t TEST_GetDisableCount() const;

  bool TEST_IsStopped() const;

 private:
  Status WaitForOpsToFinish(
      const CoarseTimePoint& start_time, const CoarseTimePoint& deadline);

  uint64_t Update(uint64_t delta);

  // The upper 16 bits are used for storing the number of separate operations that have disabled the
  // resource. E.g. tablet shutdown running at the same time with Truncate/RestoreSnapshot.
  // The lower 48 bits are used to keep track of the number of concurrent read/write operations.
  std::atomic<uint64_t> counters_{0};

  // Mutex to disable the resource exclusively. This mutex is locked by DisableAndWaitForOps after
  // waiting for all shared-ownership operations to complete. We need this to avoid a race condition
  // between Raft operations that replace RocksDB (apply snapshot / truncate) and tablet shutdown.
  yb::CrossThreadMutex disable_;

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

  std::string resource_name() const {
    return data_.counter_ ? data_.counter_->resource_name() : "null";
  }

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
