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

#include "yb/util/two_group_mutex.h"

#include "yb/util/logging.h"

namespace yb {

void TwoGroupMutex::LockRead() { Lock(read_, write_); }
void TwoGroupMutex::UnlockRead() { Unlock(read_, write_); }
void TwoGroupMutex::LockWrite() { Lock(write_, read_); }
void TwoGroupMutex::UnlockWrite() { Unlock(write_, read_); }

void TwoGroupMutex::Lock(Side& mine, const Side& other) {
  std::unique_lock<std::mutex> lock(mutex_);
  // Join an in-progress phase of my group only while the other group has no one waiting; otherwise
  // wait for my next phase so the other group is not starved. An idle lock starts my phase.
  if (mode_ != other.mode && other.waiting == 0) {
    mode_ = mine.mode;
    ++active_;
    return;
  }
  ++mine.waiting;
  auto phase = mine.phase;
  mine.cond.wait(lock, [this, &mine, phase] { return mode_ == mine.mode && mine.phase != phase; });
  --mine.waiting;
  ++active_;
}

void TwoGroupMutex::Unlock(Side& mine, Side& other) {
  // The condition variable to wake, if any. Notified after the mutex is released so the woken
  // waiters do not immediately contend for a mutex this thread still holds.
  std::condition_variable* to_notify = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    DCHECK_GT(active_, 0);
    if (--active_ != 0) {
      return;
    }
    // Prefer handing off to the other group so a stream of one kind cannot starve the other.
    if (other.waiting != 0) {
      to_notify = &StartPhase(other);
    } else if (mine.waiting != 0) {
      to_notify = &StartPhase(mine);
    } else {
      mode_ = Mode::kIdle;
    }
  }
  if (to_notify != nullptr) {
    to_notify->notify_all();
  }
}

std::condition_variable& TwoGroupMutex::StartPhase(Side& side) {
  mode_ = side.mode;
  ++side.phase;
  return side.cond;
}

}  // namespace yb
