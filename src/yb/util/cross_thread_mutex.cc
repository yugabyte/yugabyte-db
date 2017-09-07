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

#include "cross_thread_mutex.h"

namespace yb {

void CrossThreadMutex::lock() {
  std::unique_lock<std::mutex> lk(mutex);
  condition_variable.wait(lk, [this]{return has_lock == false;});
  has_lock = true;
}

void CrossThreadMutex::unlock() {
  {
    std::lock_guard<std::mutex> lk(mutex);
    has_lock = false;
  }
  condition_variable.notify_one();
}

} // namespace yb
