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

#include "yb/util/semaphore.h"

#include <semaphore.h>
#include "yb/util/logging.h"
#include "yb/gutil/walltime.h"
namespace yb {

Semaphore::Semaphore(int capacity) {
  DCHECK_GE(capacity, 0);
  if (sem_init(&sem_, 0, capacity) != 0) {
    Fatal("init");
  }
}

Semaphore::~Semaphore() {
  if (sem_destroy(&sem_) != 0) {
    Fatal("destroy");
  }
}

void Semaphore::Acquire() {
  while (true) {
    int ret = sem_wait(&sem_);
    if (ret == 0) {
      // TODO: would be nice to track acquisition time, etc.
      return;
    }

    if (errno == EINTR) continue;
    Fatal("wait");
  }
}

bool Semaphore::TryAcquire() {
  int ret = sem_trywait(&sem_);
  if (ret == 0) {
    return true;
  }
  if (errno == EAGAIN || errno == EINTR) {
    return false;
  }
  Fatal("trywait");
}

bool Semaphore::TimedAcquire(const MonoDelta& timeout) {
  int64_t microtime = GetCurrentTimeMicros();
  microtime += timeout.ToMicroseconds();

  struct timespec abs_timeout;
  MonoDelta::NanosToTimeSpec(microtime * MonoTime::kNanosecondsPerMicrosecond,
                             &abs_timeout);

  while (true) {
    int ret = sem_timedwait(&sem_, &abs_timeout);
    if (ret == 0) return true;
    if (errno == ETIMEDOUT) return false;
    if (errno == EINTR) continue;
    Fatal("timedwait");
  }
}

void Semaphore::Release() {
  PCHECK(sem_post(&sem_) == 0);
}

int Semaphore::GetValue() {
  int val;
  PCHECK(sem_getvalue(&sem_, &val) == 0);
  return val;
}

void Semaphore::Fatal(const char* action) {
  PLOG(FATAL) << "Could not " << action << " semaphore "
              << reinterpret_cast<void*>(&sem_);
  abort(); // unnecessary, but avoids gcc complaining
}

} // namespace yb
