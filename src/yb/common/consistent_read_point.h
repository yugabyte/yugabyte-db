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

#pragma once

#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/util/locks.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(HadReadTime);

// ConsistentReadPoint tracks a consistent read point to read across tablets.
class ConsistentReadPoint {
 public:
  // A map of tablet id to local limits.
  typedef std::unordered_map<TabletId, HybridTime> HybridTimeMap;

  explicit ConsistentReadPoint(const scoped_refptr<ClockBase>& clock);

  void MoveFrom(ConsistentReadPoint* rhs);

  // Set the current time as the read point.
  void SetCurrentReadTime() EXCLUDES(mutex_);

  // If read point is not set, use the current time as the read point and defer it to the global
  // limit. If read point was already set, return error if it is not deferred.
  Status TrySetDeferredCurrentReadTime() EXCLUDES(mutex_);

  // Set the read point to the specified read time with local limits.
  void SetReadTime(const ReadHybridTime& read_time, HybridTimeMap&& local_limits) EXCLUDES(mutex_);

  ReadHybridTime GetReadTime() const;

  // Get the read time of this read point for a tablet.
  ReadHybridTime GetReadTime(const TabletId& tablet) const EXCLUDES(mutex_);

  // Notify that a tablet requires restart. This method is thread-safe.
  void RestartRequired(const TabletId& tablet, const ReadHybridTime& restart_time) EXCLUDES(mutex_);

  void UpdateLocalLimit(const TabletId& tablet, HybridTime local_limit) EXCLUDES(mutex_);

  // Does the current read require restart?
  bool IsRestartRequired() const EXCLUDES(mutex_);

  // Restart read.
  void Restart() EXCLUDES(mutex_);

  // Defer read hybrid time to global limit.
  void Defer() EXCLUDES(mutex_);

  // Update the clock used by this consistent read point with the propagated time.
  void UpdateClock(HybridTime propagated_hybrid_time) EXCLUDES(mutex_);

  // Return the current time to propagate.
  HybridTime Now() const EXCLUDES(mutex_);

  // Prepare the read time and local limits in a child transaction.
  void PrepareChildTransactionData(ChildTransactionDataPB* data) const EXCLUDES(mutex_);

  // Finish a child transaction and populate the restart read times in the result.
  void FinishChildTransactionResult(
      HadReadTime had_read_time, ChildTransactionResultPB* result) const EXCLUDES(mutex_);

  // Apply restart read times from a child transaction result. This method is thread-safe.
  void ApplyChildTransactionResult(const ChildTransactionResultPB& result) EXCLUDES(mutex_);

  // Sets in transaction limit.
  void SetInTxnLimit(HybridTime value) EXCLUDES(mutex_);

 private:
  inline void SetReadTimeUnlocked(
      const ReadHybridTime& read_time, HybridTimeMap* local_limits = nullptr) REQUIRES(mutex_);
  void SetCurrentReadTimeUnlocked() REQUIRES(mutex_);
  void UpdateLimitsMapUnlocked(
      const TabletId& tablet, const HybridTime& local_limit, HybridTimeMap* map) REQUIRES(mutex_);
  void RestartRequiredUnlocked(const TabletId& tablet, const ReadHybridTime& restart_time)
      REQUIRES(mutex_);
  bool IsRestartRequiredUnlocked() const REQUIRES(mutex_);

  const scoped_refptr<ClockBase> clock_;

  mutable simple_spinlock mutex_;
  ReadHybridTime read_time_ GUARDED_BY(mutex_);
  HybridTime restart_read_ht_ GUARDED_BY(mutex_);

  // Local limits for separate tablets. Does not change during lifetime of a consistent read.
  // Times such that anything happening at that hybrid time or later is definitely after the
  // original request arrived and therefore does not have to be shown in results.
  HybridTimeMap local_limits_ GUARDED_BY(mutex_);

  // Restarts that happen during a consistent read. Used to initialise local_limits for restarted
  // read.
  HybridTimeMap restarts_ GUARDED_BY(mutex_);
};

} // namespace yb
