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

#include "yb/util/debug/lock_debug.h"

#include "yb/util/logging.h"

namespace yb {

namespace {

thread_local NonRecursiveSharedLockBase* head = nullptr;

} // namespace

NonRecursiveSharedLockBase::NonRecursiveSharedLockBase(void* mutex)
    : mutex_(mutex), next_(head) {
  auto current = head;
  while (current != nullptr) {
    LOG_IF(DFATAL, current->mutex_ == mutex) << "Recursive shared lock";
    current = current->next_;
  }
  head = this;
}

NonRecursiveSharedLockBase::~NonRecursiveSharedLockBase() {
  head = next_;
}

void SingleThreadedMutex::lock() {
  auto old_value = locked_.exchange(true, std::memory_order_acq_rel);
  LOG_IF(DFATAL, old_value) << "Thread collision on lock";
}

void SingleThreadedMutex::unlock() {
  auto old_value = locked_.exchange(false, std::memory_order_acq_rel);
  LOG_IF(DFATAL, !old_value) << "Unlock of not locked mutex";
}

bool SingleThreadedMutex::try_lock() {
  return !locked_.exchange(true, std::memory_order_acq_rel);
}

}  // namespace yb
