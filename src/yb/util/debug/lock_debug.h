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

#ifndef YB_UTIL_DEBUG_LOCK_DEBUG_H
#define YB_UTIL_DEBUG_LOCK_DEBUG_H

#include "yb/gutil/thread_annotations.h"

namespace yb {

class NonRecursiveSharedLockBase {
 public:
  explicit NonRecursiveSharedLockBase(void* mutex);
  ~NonRecursiveSharedLockBase();

 protected:
  void* mutex() const {
    return mutex_;
  }

 private:
  void* mutex_;
  NonRecursiveSharedLockBase* next_;
};

template<class Mutex>
class SCOPED_CAPABILITY NonRecursiveSharedLock : public NonRecursiveSharedLockBase {
 public:
  explicit NonRecursiveSharedLock(Mutex& mutex) ACQUIRE_SHARED(mutex) // NOLINT
      : NonRecursiveSharedLockBase(&mutex) {
    mutex.lock_shared();
  }

  ~NonRecursiveSharedLock() RELEASE() {
    static_cast<Mutex*>(mutex())->unlock_shared();
  }
};

}  // namespace yb

#endif  // YB_UTIL_DEBUG_LOCK_DEBUG_H
