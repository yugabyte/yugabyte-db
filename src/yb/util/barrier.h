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

#ifndef YB_UTIL_BARRIER_H
#define YB_UTIL_BARRIER_H
#pragma once

#include "yb/gutil/macros.h"
#include "yb/util/condition_variable.h"
#include "yb/util/mutex.h"
#include "yb/util/thread_restrictions.h"

namespace yb {

// Implementation of pthread-style Barriers.
class Barrier {
 public:
  // Initialize the barrier with the given initial count.
  explicit Barrier(int count) :
      cond_(&mutex_),
      count_(count),
      initial_count_(count) {
    DCHECK_GT(count, 0);
  }

  ~Barrier() {
  }

  // Wait until all threads have reached the barrier.
  // Once all threads have reached the barrier, the barrier is reset
  // to the initial count.
  void Wait() {
    ThreadRestrictions::AssertWaitAllowed();
    MutexLock l(mutex_);
    if (--count_ == 0) {
      count_ = initial_count_;
      cycle_count_++;
      cond_.Broadcast();
      return;
    }

    int initial_cycle = cycle_count_;
    while (cycle_count_ == initial_cycle) {
      cond_.Wait();
    }
  }

 private:
  Mutex mutex_;
  ConditionVariable cond_;
  int count_;
  uint32_t cycle_count_ = 0;
  const int initial_count_;
  DISALLOW_COPY_AND_ASSIGN(Barrier);
};

} // namespace yb
#endif // YB_UTIL_BARRIER_H
