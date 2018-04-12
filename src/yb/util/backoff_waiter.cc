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

#include "yb/util/backoff_waiter.h"

#include <thread>

#include "yb/util/random_util.h"

namespace yb {

bool BackoffWaiter::Wait() {
  auto now = std::chrono::steady_clock::now();
  if (now >= deadline_) {
    return false;
  }
  ++attempt_;

  auto remaining = deadline_ - now;
  int64_t base_delay_ms = 1 << (++attempt_ + 3); // 1st retry delayed 2^4 ms, 2nd 2^5, etc..
  int64_t jitter_ms = RandomUniformInt(0, 50);
  auto delay = std::min<decltype(remaining)>(
      std::chrono::milliseconds(base_delay_ms + jitter_ms), remaining);
  std::this_thread::sleep_for(delay);
  return true;
}

} // namespace yb
