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
#include <semaphore.h>

#include "yb/util/callsite_profiling.h"
#include "yb/util/logging.h"

#include "yb/util/semaphore.h"

namespace yb {

Semaphore::Semaphore(int capacity)
  : count_(capacity) {
  DCHECK_GE(capacity, 0);

}

Semaphore::~Semaphore() {
}

void Semaphore::Acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return count_.load() > 0; });
  CHECK_GT(count_.load(), 0);
  count_.fetch_sub(1);
}

bool Semaphore::TryAcquire() {
  if (count_.load() > 0) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (count_.load() > 0) {
      count_.fetch_sub(1);
      return true;
    }
  }
  return false;
}

bool Semaphore::TimedAcquire(const MonoDelta& timeout) {
  auto time_stop = MonoTime::Now();
  time_stop.AddDelta(timeout);
  std::unique_lock<std::mutex> lock(mutex_);
  if (!cv_.wait_until(lock, time_stop.ToSteadyTimePoint(), [this] { return count_.load() > 0; })) {
    return false;
  }
  CHECK_GT(count_.load(), 0);
  count_.fetch_sub(1);
  return true;
}

void Semaphore::Release() {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_GE(count_.load(), 0);
  count_.fetch_add(1);
  YB_PROFILE(cv_.notify_all());
}

int Semaphore::GetValue() {
  return count_.load();
}

} // namespace yb
