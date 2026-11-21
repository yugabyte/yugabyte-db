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

#include "yb/util/rw_semaphore.h"

#include "yb/util/flags.h"
#include "yb/util/thread.h"

DEFINE_NON_RUNTIME_bool(use_absl_rw_spinlock, false,
    "Implement rw_spinlock, rw_semaphore, PerCpuRwMutex and RWCLock with absl::Mutex, whose "
    "waiters block, instead of the original locks, whose rw_spinlock waiters poll with "
    "nanosleep. Each lock reads this when it is constructed.");
TAG_FLAG(use_absl_rw_spinlock, advanced);

namespace yb {

bool UseAbslRwSpinlock() {
  return FLAGS_use_absl_rw_spinlock;
}

#ifndef NDEBUG
void PollingRwSemaphore::AssignWriterTid() {
  writer_tid_ = Thread::CurrentThreadId();
}
#endif

}  // namespace yb
