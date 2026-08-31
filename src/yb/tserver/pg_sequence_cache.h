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

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "yb/common/pg_types.h"
#include "yb/util/condition_variable.h"
#include "yb/util/locks.h"
#include "yb/util/mutex.h"

namespace yb::tserver {

class PgSequenceCache {
 public:
  struct Entry {
   private:
    // Accessed in order to check/change available_ as well as to sleep on cv_.
    Mutex mutex_;
    ConditionVariable cv_;
    // Whether no thread is currently working on this resource.
    bool available_ GUARDED_BY(mutex_);
    // Whether this entry's range has values remaining.
    bool has_values_;
    int64_t curr_value_;
    int64_t last_value_;

    void CheckNotAvailable();

   public:
    Entry();

    // Get a single value if the entry has values.
    std::optional<int64_t> GetValueIfCached(int64_t inc_by) EXCLUDES(mutex_);

    // Update the cached range.
    void SetRange(int64_t first_value, int64_t last_value) EXCLUDES(mutex_);
    // Notify a waiting thread that it can access this entry. Do not call any other entry functions
    // after calling this function, unless you get it again when it is available from the cache
    // itself.
    void NotifyWaiter() EXCLUDES(mutex_);

    friend class PgSequenceCache;
  };

  // The mutex/data usage model:
  // 1. Call GetWhenAvailable - acquire the cache entry ownership for (db_oid, seq_oid):
  //    - Under the map lock (lock_), find or create the Entry.
  //    - Lock entry->mutex_.
  //    - While available_ == false, wait on cv_ (another thread owns the entry).
  //    - Set available_ = false - this thread now owns the entry.
  //    - Return the shared_ptr<Entry>. The mutex is released on return;
  //      ownership is not held by holding mutex_, but by available_ == false.
  // 2. Use the cache entry (caller, typically FetchSequenceTuple)
  //    While owned, the caller may touch the range without mutex_:
  // 3. A ScopeExit ensures release even on error, like in the code:
  //      std::shared_ptr<PgSequenceCache::Entry> cache_entry =
  //          VERIFY_RESULT(sequence_cache().GetWhenAvailable(.....));
  //      auto se = MakeOptionalScopeExit([&] { cache_entry->NotifyWaiter(); });
  // 4. Release the cache entry ownership - call NotifyWaiter:
  //    - Under mutex_, set available_ = true.
  //    - Call cv_.Signal() so the next waiter can acquire the entry ownership.
  // Until NotifyWaiter runs, other threads stay blocked in GetWhenAvailable / Invalidate
  // on that entry. That lets a long DocDB fetch run without holding mutex_, while still
  // serializing per-sequence cache access.

  // Wait on the cv until the id is available. If create_if_not_exists is true, create an entry when
  // none exists; otherwise return a null entry. Returns a timed out status if the thread timed out
  // while waiting.
  Result<std::shared_ptr<Entry>> GetWhenAvailable(
      const PgObjectId& sequence_id, const MonoTime& deadline, bool create_if_not_exists = true)
      EXCLUDES(lock_);

  // If an entry exists, acquire it via GetWhenAvailable, drop its cached range, then
  // NotifyWaiter. No-op if no entry exists.
  Status Invalidate(const PgObjectId& sequence_id, const MonoTime& deadline) EXCLUDES(lock_);

 private:
  simple_spinlock lock_;
  std::unordered_map<PgObjectId, std::shared_ptr<Entry>, PgObjectIdHash> cache_ GUARDED_BY(lock_);
};

}  // namespace yb::tserver
