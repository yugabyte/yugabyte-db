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

#include "kudu/util/semaphore.h"

#include <semaphore.h>
#include <glog/logging.h>
#include "kudu/gutil/walltime.h"

namespace kudu {

Semaphore::Semaphore(int capacity)
  : count_(capacity) {
  DCHECK_GE(capacity, 0);
  sem_ = dispatch_semaphore_create(capacity);
  CHECK_NOTNULL(sem_);
}

Semaphore::~Semaphore() {
  dispatch_release(sem_);
}

void Semaphore::Acquire() {
  // If the timeout is DISPATCH_TIME_FOREVER, then dispatch_semaphore_wait()
  // waits forever and always returns zero.
  CHECK(dispatch_semaphore_wait(sem_, DISPATCH_TIME_FOREVER) == 0);
  count_.IncrementBy(-1);
}

bool Semaphore::TryAcquire() {
  // The dispatch_semaphore_wait() function returns zero upon success and
  // non-zero after the timeout expires.
  if (dispatch_semaphore_wait(sem_, DISPATCH_TIME_NOW) == 0) {
    count_.IncrementBy(-1);
    return true;
  }
  return false;
}

bool Semaphore::TimedAcquire(const MonoDelta& timeout) {
  dispatch_time_t t = dispatch_time(DISPATCH_TIME_NOW, timeout.ToNanoseconds());
  if (dispatch_semaphore_wait(sem_, t) == 0) {
    count_.IncrementBy(-1);
    return true;
  }
  return false;
}

void Semaphore::Release() {
  dispatch_semaphore_signal(sem_);
  count_.IncrementBy(1);
}

int Semaphore::GetValue() {
  return count_.Load();
}

} // namespace kudu
