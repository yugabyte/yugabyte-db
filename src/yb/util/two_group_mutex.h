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

#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace yb {

// A mutex with two groups -- read and write -- where members of the same group run concurrently but
// the two groups never overlap. This is the two-group case of the group mutual exclusion problem
// (Joung, 2000): same-group requests share, different groups exclude. Unlike std::shared_mutex
// (a single exclusive writer), both groups are shared. It suits cases where each group coordinates
// internally and only cross-group overlap is unsafe -- e.g. a structure whose concurrent writers
// are safe among themselves and whose concurrent readers are safe among themselves, but where a
// reader must not observe a writer's partial state.
//
// Phases alternate without starvation. A request joins an in-progress phase of its own group only
// while the other group has no one waiting; once the other group is waiting, new same-group
// requests stop joining, so the active phase drains and then hands off to the waiting group
// (preferring the opposite group on each handoff). Same-group requests that arrive while their
// group is waiting are admitted together when that group's next phase begins. This trades full
// concurrent-entering for starvation freedom.
class TwoGroupMutex {
 public:
  void LockRead();
  void UnlockRead();
  void LockWrite();
  void UnlockWrite();

  // RAII scoped guards. Nested so the generic names don't collide with other ReadLock/WriteLock
  // guards in the codebase (e.g. rocksdb::ReadLock).
  class ReadLock {
   public:
    explicit ReadLock(TwoGroupMutex& mutex) : mutex_(mutex) { mutex_.LockRead(); }
    ~ReadLock() { mutex_.UnlockRead(); }

    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;
    ReadLock(ReadLock&&) = delete;
    ReadLock& operator=(ReadLock&&) = delete;

   private:
    TwoGroupMutex& mutex_;
  };

  class WriteLock {
   public:
    explicit WriteLock(TwoGroupMutex& mutex) : mutex_(mutex) { mutex_.LockWrite(); }
    ~WriteLock() { mutex_.UnlockWrite(); }

    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;
    WriteLock(WriteLock&&) = delete;
    WriteLock& operator=(WriteLock&&) = delete;

   private:
    TwoGroupMutex& mutex_;
  };

 private:
  enum class Mode { kIdle, kRead, kWrite };

  // Per-group wait state. The two groups are symmetric, so Lock()/Unlock() are written once and
  // parameterized by which group is "mine" and which is the "other".
  struct Side {
    explicit Side(Mode mode_value) : mode(mode_value) {}

    std::condition_variable cond;
    size_t waiting = 0;
    // Bumped each time this group's phase (re)starts, so a request that parked while its own group
    // was already active waits for the next phase instead of barging into the current one. With
    // several waiters queued for the same phase the bump can happen more than once: each waiter
    // wakes, takes its turn, and on unlock may itself start the next phase for the remaining
    // waiters, so the counter advances per active waiter rather than only when fresh requests park.
    size_t phase = 0;
    const Mode mode;
  };

  void Lock(Side& mine, const Side& other);
  void Unlock(Side& mine, Side& other);
  // Sets the active mode to side.mode and advances its phase counter, returning the condition
  // variable that should be notified (by the caller, after releasing mutex_) to wake its waiters.
  std::condition_variable& StartPhase(Side& side);

  std::mutex mutex_;
  Mode mode_ = Mode::kIdle;
  size_t active_ = 0;  // holders in the current phase
  Side read_{Mode::kRead};
  Side write_{Mode::kWrite};
};

}  // namespace yb
