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

#ifndef YB_UTIL_DELAYER_H
#define YB_UTIL_DELAYER_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>

#include "yb/gutil/ref_counted.h"
#include "yb/util/monotime.h"
#include "yb/util/thread.h"

namespace yb {

// Utility class to execute actions with specified delay.
class Delayer {
 public:
  ~Delayer();

  // Execute action at specified time, but not earlier than previous action.
  void Delay(MonoTime when, std::function<void()> action);

 private:
  void Execute();

  std::mutex mutex_;
  std::condition_variable cond_;
  scoped_refptr<Thread> thread_;
  bool stop_ = false;
  std::deque<std::pair<MonoTime, std::function<void()>>> queue_;
};

} // namespace yb

#endif // YB_UTIL_DELAYER_H
