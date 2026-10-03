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
#include <map>
#include <vector>

#include "yb/client/atomic_request_id_allocator.h"

#include "yb/common/retryable_request.h"

#include "yb/util/locks.h"

namespace yb::client::internal {

/// Counts the finished ids per window of FLAGS_client_request_id_window_size ids, instead of the
/// bitmap of AtomicRequestIdAllocator. Kept for comparison: the shared work per id is the same,
/// but a count tells how many ids of a window have finished and not which ones, so min_running
/// is window aligned rather than exact.
class CounterRequestIdAllocator {
 public:
  CounterRequestIdAllocator();
  ~CounterRequestIdAllocator();

  CounterRequestIdAllocator(const CounterRequestIdAllocator&) = delete;
  void operator=(const CounterRequestIdAllocator&) = delete;

  AtomicRequestIdAllocation Next();
  void Finish(RetryableRequestId id);

  RetryableRequestId TEST_min_running() const;
  int64_t TEST_num_overflows() const;

 private:
  using Counter = std::atomic<uint32_t>;

  static constexpr size_t kSlotStride = 0x9e3779b97f4a7c15ULL;

  size_t Slot(RetryableRequestId window) const {
    return (static_cast<size_t>(window) * kSlotStride) & (num_windows_ - 1);
  }

  RetryableRequestId WindowOf(RetryableRequestId id) const {
    return id >> window_shift_;
  }

  RetryableRequestId AdvanceMinRunning();
  void ElectAndAdvanceMinRunning();
  bool WindowComplete(RetryableRequestId window) const;

  void CountOverflow(RetryableRequestId window);
  bool DrainOverflow(RetryableRequestId min_window);

  const uint32_t window_size_;
  const size_t window_shift_;
  const size_t num_windows_;

  std::atomic<RetryableRequestId> next_id_{0};
  std::atomic<RetryableRequestId> min_running_id_{0};

  std::vector<Counter> counters_;

  std::atomic<bool> running_{false};

  std::atomic<int64_t> overflow_size_{0};
  std::atomic<int64_t> num_overflows_{0};

  simple_spinlock overflow_mutex_;
  std::map<RetryableRequestId, uint32_t> overflow_ GUARDED_BY(overflow_mutex_);
};

} // namespace yb::client::internal
