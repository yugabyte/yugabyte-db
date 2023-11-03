// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

namespace yb {

// Lock-free integer that keeps track of the highest value seen.
// Similar to Impala's RuntimeProfile::HighWaterMarkCounter.
// HighWaterMark::max_value() returns the highest value seen;
// HighWaterMark::current_value() returns the current value.
class HighWaterMark {
 public:
  explicit HighWaterMark(int64_t initial_value)
    : current_value_(initial_value),
      max_value_(initial_value) {
  }

  // Return the current value.
  int64_t current_value() const {
    return current_value_.load(std::memory_order_acquire);
  }

  // Return the max value.
  int64_t max_value() const {
    return max_value_.load(std::memory_order_acquire);
  }

  // If current value + 'delta' is <= 'max', increment current value
  // by 'delta' and return true; return false otherwise.
  bool TryIncrementBy(int64_t delta, int64_t max) {
    while (true) {
      int64_t old_val = current_value();
      int64_t new_val = old_val + delta;
      if (new_val > max) {
        return false;
      }
      if (PREDICT_TRUE(current_value_.compare_exchange_weak(
          old_val, new_val, std::memory_order_acq_rel))) {
        UpdateMax(new_val);
        return true;
      }
    }
  }

  void IncrementBy(int64_t amount) {
    UpdateMax(current_value_.fetch_add(amount, std::memory_order_acq_rel) + amount);
  }

  void set_value(int64_t v) {
    current_value_.store(v, std::memory_order_release);
    UpdateMax(v);
  }

 private:
  void UpdateMax(int64_t value) {
    int64_t old_value = max_value_.load(std::memory_order_acquire);
    while (old_value < value) {
      if (max_value_.compare_exchange_weak(old_value, value, std::memory_order_acq_rel)) {
        break;
      }
    }
  }

  std::atomic<int64_t> current_value_;
  std::atomic<int64_t> max_value_;
};

} // namespace yb
