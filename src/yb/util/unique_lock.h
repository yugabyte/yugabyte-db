// Copyright (c) Yugabyte, Inc.
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

#ifndef YB_UTIL_UNIQUE_LOCK_H
#define YB_UTIL_UNIQUE_LOCK_H

#include <mutex>

#include "yb/gutil/thread_annotations.h"

namespace yb {

#if THREAD_ANNOTATIONS_ENABLED

#define UNIQUE_LOCK(lock_name, mutex) ::yb::UniqueLock<decltype(mutex)> lock_name(mutex);

// A wrapper unique_lock that supports thread annotations.
template<typename Mutex>
class SCOPED_CAPABILITY UniqueLock {
 public:
  explicit UniqueLock(Mutex &mutex) ACQUIRE(mutex) : unique_lock_(mutex) {}

  explicit UniqueLock(Mutex &mutex, std::defer_lock_t defer) : unique_lock_(mutex, defer) {}

  ~UniqueLock() RELEASE() = default;

  void unlock() RELEASE() { unique_lock_.unlock(); }
  void lock() ACQUIRE() { unique_lock_.lock(); }
 private:
  std::unique_lock<Mutex> unique_lock_;
};

#else

template<class Mutex>
using UniqueLock = std::unique_lock<Mutex>;

#define UNIQUE_LOCK(lock_name, mutex) std::unique_lock<decltype(mutex)> lock_name(mutex);

#endif

} // namespace yb

#endif  // YB_UTIL_UNIQUE_LOCK_H
