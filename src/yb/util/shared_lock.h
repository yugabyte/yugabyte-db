//
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

#pragma once

#include <shared_mutex>

#include "yb/gutil/thread_annotations.h"

namespace yb {

// A wrapper around std::shared_lock that supports thread annotations.
template<typename Mutex>
class SCOPED_CAPABILITY SharedLock {
 public:
  explicit SharedLock(Mutex &mutex) ACQUIRE_SHARED(mutex) : m_lock(mutex) {}
  ~SharedLock() RELEASE() = default;

 private:
  std::shared_lock<Mutex> m_lock;
};

} // namespace yb
