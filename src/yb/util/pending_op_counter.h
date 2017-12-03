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

#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {
namespace util {

// This is used to track the number of pending operations using a certain resource (e.g.
// a RocksDB database) so we can safely wait for all operations to complete and destroy the
// resource.
class PendingOperationCounter {
 public:
  // Using upper bits of counter as special flags.
  static constexpr uint64_t kDisabledDelta = 0x0001000000000000;
  static constexpr uint64_t kOpCounterMask = kDisabledDelta - 1;
  static constexpr uint64_t kDisabledCounterMask = ~kOpCounterMask;

  PendingOperationCounter() : counters_(0) {}

  CHECKED_STATUS DisableAndWaitForOps(const MonoDelta& timeout, uint64_t num_remaining_ops = 0) {
    Update(kDisabledDelta);
    return WaitForOpsToFinish(timeout, num_remaining_ops);
  }

  void Enable() { Update(-kDisabledDelta); }

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
  CHECKED_STATUS WaitForOpsToFinish(
      const MonoDelta& timeout, uint64_t num_remaining_ops = 0) const;

  uint64_t Update(uint64_t delta) {
    const uint64_t result = counters_.fetch_add(delta, std::memory_order::memory_order_release);
    // Ensure that there is no underflow in either counter.
    DCHECK_EQ((result & (1ull << 63)), 0); // Counter of Disable() calls.
    DCHECK_EQ((result & (kDisabledDelta >> 1)), 0); // Counter of pending operations.
    return result;
  }

  // Upper bits are used for storing number of Disable() calls.
  std::atomic<uint64_t> counters_;
};

// A convenience class to automatically increment/decrement a PendingOperationCounter.
class ScopedPendingOperation {
 public:
  // Object is not copyable, but movable.
  void operator=(const ScopedPendingOperation&) = delete;
  ScopedPendingOperation(const ScopedPendingOperation&) = delete;

  explicit ScopedPendingOperation(PendingOperationCounter* counter)
      : counter_(counter),
        orig_counter_value_(0) {
    if (counter != nullptr) {
      orig_counter_value_ = counter->Increment();
    }
  }

  ScopedPendingOperation(ScopedPendingOperation&& op) : counter_(op.counter_) {
    op.counter_ = nullptr; // Moved ownership.
  }

  ~ScopedPendingOperation() {
    if (counter_ != nullptr) {
      counter_->Decrement();
    }
  }

  bool ok() const {
    return (orig_counter_value_ & PendingOperationCounter::kDisabledCounterMask) == 0;
  }

 private:
  PendingOperationCounter* counter_;
  // Store in constructor original counter value to be able checking it later in ok().
  uint64_t orig_counter_value_;
};

// RETURN_NOT_OK macro support.
inline Status MoveStatus(const ScopedPendingOperation& scoped) {
  return scoped.ok() ? Status::OK() : STATUS(IllegalState, "RocksDB object is unavailable");
}

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_PENDING_OP_COUNTER_H_
