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
  PendingOperationCounter() : num_pending_operations_(0) {}

  void Increment() { Update(1); }
  void Decrement() { Update(-1); }
  int64_t Get() const {
    return num_pending_operations_.load(std::memory_order::memory_order_acquire);
  }

  CHECKED_STATUS WaitForAllOpsToFinish(const MonoDelta& timeout) const;

 private:
  void Update(int64_t delta) {
    num_pending_operations_.fetch_add(delta, std::memory_order::memory_order_release);
  }

  std::atomic<int64_t> num_pending_operations_;
};

// A convenience class to automatically increment/decrement a PendingOperationCounter.
class ScopedPendingOperation {
 public:
  explicit ScopedPendingOperation(PendingOperationCounter* counter) : counter_(counter) {
    if (counter_ != nullptr) {
      counter_->Increment();
    }
  }

  ~ScopedPendingOperation() {
    if (counter_ != nullptr) {
      counter_->Decrement();
    }
  }

 private:
  PendingOperationCounter* counter_;
};

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_PENDING_OP_COUNTER_H_
