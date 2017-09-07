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

#ifndef YB_UTIL_SHARED_MUTEX_H_
#define YB_UTIL_SHARED_MUTEX_H_

#include <mutex>
#include <condition_variable>

namespace yb {

// This is a wrapper around std::mutex which can be locked and unlocked from different threads.
class CrossThreadMutex {

private:
  std::mutex mutex;
  std::condition_variable condition_variable;
  bool has_lock;
public:
  CrossThreadMutex() : mutex(), condition_variable(), has_lock(false) {}

  void lock();

  void unlock();
};
} // namespace yb


#endif //PROJECT_SHARED_MUTEX_H
