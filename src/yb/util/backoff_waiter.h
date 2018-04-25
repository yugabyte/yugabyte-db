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

#ifndef YB_UTIL_BACKOFF_WAITER_H
#define YB_UTIL_BACKOFF_WAITER_H

#include <chrono>

namespace yb {

// Utility class for waiting.
// It tracks number of attempts and exponentially increase sleep timeout.
class BackoffWaiter {
 public:
  explicit BackoffWaiter(
      std::chrono::steady_clock::time_point deadline,
      std::chrono::steady_clock::duration max_wait = std::chrono::steady_clock::duration::max())
      : deadline_(deadline), max_wait_(max_wait) {}

  bool Wait();

  int attempt() const {
    return attempt_;
  }

  // Resets attempt counter, w/o modifying deadline.
  void Restart() {
    attempt_ = 0;
  }

 private:
  int attempt_ = 0;
  std::chrono::steady_clock::time_point deadline_;
  std::chrono::steady_clock::duration max_wait_;
};

} // namespace yb

#endif // YB_UTIL_BACKOFF_WAITER_H
