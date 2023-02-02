// Copyright (c) Yugabyte, Inc.
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

#include "yb/util/condition_variable.h"
#include "yb/util/locks.h"
#include "yb/util/mutex.h"

namespace yb {
namespace tserver {

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

  // Wait on the cv until the id is available and create an entry for this id if one didn't exist.
  // This function returns the entry if successfully waited, or a time out status if the thread
  // timed out while waiting.
  Result<std::shared_ptr<Entry>> GetWhenAvailable(int64_t sequence_id, const MonoTime& deadline)
      EXCLUDES(lock_);

 private:
  simple_spinlock lock_;
  std::unordered_map<int64_t, std::shared_ptr<Entry>> cache_ GUARDED_BY(lock_);
};

}  // namespace tserver
}  // namespace yb
