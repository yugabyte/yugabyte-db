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

#include "yb/tserver/pg_sequence_cache.h"

#include "yb/tserver/tserver_flags.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"

namespace yb {
namespace tserver {

// This function checks that available_ is false and is used for all member functions. However,
// we should not need to hold mutex_ to read available_ in this case because if available_ is
// false then we are the only thread accessing this entry so reading it is thread-safe. Thus,
// we annotate this with NO_THREAD_SAFETY_ANALYSIS.
void PgSequenceCache::Entry::CheckNotAvailable() NO_THREAD_SAFETY_ANALYSIS { DCHECK(!available_); }

PgSequenceCache::Entry::Entry() : cv_(&mutex_), available_(true), has_values_(false) {}

std::optional<int64_t> PgSequenceCache::Entry::GetValueIfCached(int64_t inc_by) {
  CheckNotAvailable();

  if (!has_values_) {
    return std::nullopt;
  }

  int64_t value = curr_value_;
  // Cast to 128-bits because if a sequence has cycle enabled, the last value can be close to max
  // long and increment can be negative, or the last value can be close to the min long and
  // increment can be positive.
  auto last_value = static_cast<__int128_t>(last_value_);
  // If we are at the end of our range.
  if ((inc_by >= 0 && value > last_value - inc_by) || (inc_by < 0 && value < last_value - inc_by)) {
    // Invalidate the cache entry so that the master server can get the next range.
    has_values_ = false;
  } else {
    curr_value_ += inc_by;
  }

  return value;
}

void PgSequenceCache::Entry::SetRange(int64_t first_value, int64_t last_value) {
  CheckNotAvailable();
  curr_value_ = first_value;
  last_value_ = last_value;
  has_values_ = true;
}

void PgSequenceCache::Entry::NotifyWaiter() {
  CheckNotAvailable();
  {
    MutexLock lock_guard(mutex_);
    available_ = true;
  }
  cv_.Signal();
}

Result<std::shared_ptr<PgSequenceCache::Entry>> PgSequenceCache::GetWhenAvailable(
    const PgObjectId& sequence_id, const MonoTime& deadline) {
  std::shared_ptr<Entry> entry;
  {
    std::lock_guard cache_lock_guard(lock_);
    if (!cache_.contains(sequence_id)) {
      VLOG(3) << "Create cache entry for sequence id " << sequence_id;
      cache_[sequence_id] = std::make_shared<Entry>();
    }

    entry = cache_[sequence_id];
  }

  VLOG(3) << "Getting entry for sequence id " << sequence_id
          << " from tserver sequence cache when available";
  MutexLock entry_lock_guard(entry->mutex_);
  // Wait for the id to be available, or until the specified timeout duration passes.
  while (!entry->available_) {
    if (!entry->cv_.WaitUntil(deadline)) {
      return STATUS_FORMAT(
          TimedOut, "Timed out while waiting for sequence id $0 to be available.", sequence_id);
    }
  }

  entry->available_ = false;
  return entry;
}

}  // namespace tserver
}  // namespace yb
