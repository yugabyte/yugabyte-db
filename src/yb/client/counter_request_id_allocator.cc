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

#include "yb/client/counter_request_id_allocator.h"

#include <algorithm>

#include "yb/gutil/bits.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"

DEFINE_RUNTIME_uint32(client_request_id_window_size, 64,
    "Number of consecutive retryable request ids that share a counter of the finished ones, "
    "rounded up to a power of two. Only used by CounterRequestIdAllocator.");

DEFINE_RUNTIME_uint32(client_request_id_num_windows, 4096,
    "Number of window counters, rounded up to a power of two. Only used by "
    "CounterRequestIdAllocator.");

namespace yb::client::internal {

CounterRequestIdAllocator::CounterRequestIdAllocator()
    : window_size_(
          1U << Bits::Log2Ceiling64(std::max<uint32_t>(1, FLAGS_client_request_id_window_size))),
      window_shift_(Bits::Log2Ceiling64(window_size_)),
      num_windows_(
          1ULL << Bits::Log2Ceiling64(std::max<uint32_t>(1, FLAGS_client_request_id_num_windows))),
      counters_(num_windows_) {
}

CounterRequestIdAllocator::~CounterRequestIdAllocator() {
  DCHECK(!running_.load(std::memory_order_acquire));
}

AtomicRequestIdAllocation CounterRequestIdAllocator::Next() {
  auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
  return AtomicRequestIdAllocation {
    .id = id,
    .min_running = min_running_id_.load(std::memory_order_acquire),
  };
}

void CounterRequestIdAllocator::Finish(RetryableRequestId id) {
  auto window = WindowOf(id);
  auto min_window = WindowOf(min_running_id_.load(std::memory_order_acquire));
  if (PREDICT_FALSE(static_cast<size_t>(window - min_window) >= num_windows_)) {
    CountOverflow(window);
    return;
  }
  if (counters_[Slot(window)].fetch_add(1, std::memory_order_acq_rel) + 1 != window_size_) {
    return;
  }
  ElectAndAdvanceMinRunning();
}

void CounterRequestIdAllocator::ElectAndAdvanceMinRunning() {
  auto expected = false;
  if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return;
  }
  for (;;) {
    auto window = AdvanceMinRunning();
    if (PREDICT_TRUE(running_.exchange(false, std::memory_order_acq_rel))) {
      if (WindowComplete(window)) {
        expected = false;
        if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
          continue;
        }
      }
    } else {
      LOG(DFATAL) << "running_ is false while a thread is advancing min running request id";
    }
    return;
  }
}

RetryableRequestId CounterRequestIdAllocator::AdvanceMinRunning() {
  auto window = WindowOf(min_running_id_.load(std::memory_order_relaxed));
  for (;;) {
    auto advanced = false;
    while (WindowComplete(window)) {
      counters_[Slot(window)].store(0, std::memory_order_relaxed);
      ++window;
      advanced = true;
    }
    if (advanced) {
      min_running_id_.store(window << window_shift_, std::memory_order_release);
    }
    if (!overflow_size_.load(std::memory_order_relaxed) || !DrainOverflow(window)) {
      return window;
    }
  }
}

bool CounterRequestIdAllocator::WindowComplete(RetryableRequestId window) const {
  return counters_[Slot(window)].load(std::memory_order_acquire) == window_size_;
}

void CounterRequestIdAllocator::CountOverflow(RetryableRequestId window) {
  num_overflows_.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard lock(overflow_mutex_);
  ++overflow_[window];
  overflow_size_.fetch_add(1, std::memory_order_release);
}

bool CounterRequestIdAllocator::DrainOverflow(RetryableRequestId min_window) {
  auto result = false;
  std::lock_guard lock(overflow_mutex_);
  while (!overflow_.empty()) {
    auto it = overflow_.begin();
    if (static_cast<size_t>(it->first - min_window) >= num_windows_) {
      break;
    }
    counters_[Slot(it->first)].fetch_add(it->second, std::memory_order_acq_rel);
    overflow_size_.fetch_sub(it->second, std::memory_order_release);
    overflow_.erase(it);
    result = true;
  }
  return result;
}

RetryableRequestId CounterRequestIdAllocator::TEST_min_running() const {
  return min_running_id_.load(std::memory_order_acquire);
}

int64_t CounterRequestIdAllocator::TEST_num_overflows() const {
  return num_overflows_.load(std::memory_order_relaxed);
}

} // namespace yb::client::internal
