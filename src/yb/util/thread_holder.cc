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

#include "yb/util/thread_holder.h"

namespace yb {

void ThreadHolder::JoinAll() {
  if (verbose_) {
    LOG(INFO) << __func__;
  }

  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  if (verbose_) {
    LOG(INFO) << __func__ << " done";
  }
}

void WaitStopped(const CoarseDuration& duration, std::atomic<bool>* stop) {
  auto end = CoarseMonoClock::now() + duration;
  while (!stop->load(std::memory_order_acquire) && CoarseMonoClock::now() < end) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace yb
