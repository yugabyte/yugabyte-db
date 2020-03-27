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

#ifndef YB_COMMON_CONSISTENT_READ_POINT_H
#define YB_COMMON_CONSISTENT_READ_POINT_H

#include <mutex>
#include <unordered_map>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/read_hybrid_time.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(HadReadTime);

// ConsistentReadPoint tracks a consistent read point to read across tablets. Note that this class
// is not thread-safe except otherwise noted below.
class ConsistentReadPoint {
 public:
  // A map of tablet id to local limits.
  typedef std::unordered_map<TabletId, HybridTime> HybridTimeMap;

  explicit ConsistentReadPoint(const scoped_refptr<ClockBase>& clock);

  // Set the current time as the read point.
  void SetCurrentReadTime();

  // Set the read point to the specified read time with local limits.
  void SetReadTime(const ReadHybridTime& read_time, HybridTimeMap&& local_limits);

  const ReadHybridTime& GetReadTime() const { return read_time_; }

  // Get the read time of this read point for a tablet.
  ReadHybridTime GetReadTime(const TabletId& tablet) const;

  // Notify that a tablet requires restart. This method is thread-safe.
  void RestartRequired(const TabletId& tablet, const ReadHybridTime& restart_time);

  // Does the current read require restart?
  bool IsRestartRequired() const;

  // Restart read.
  void Restart();

  // Defer read hybrid time to global limit.
  void Defer();

  // Update the clock used by this consistent read point with the propagated time.
  void UpdateClock(HybridTime propagated_hybrid_time);

  // Return the current time to propagate.
  HybridTime Now() const;

  // Prepare the read time and local limits in a child transaction.
  void PrepareChildTransactionData(ChildTransactionDataPB* data) const;

  // Finish a child transaction and populate the restart read times in the result.
  void FinishChildTransactionResult(
      HadReadTime had_read_time, ChildTransactionResultPB* result) const;

  // Apply restart read times from a child transaction result. This method is thread-safe.
  void ApplyChildTransactionResult(const ChildTransactionResultPB& result);

  // Sets in transaction limit.
  void SetInTxnLimit(HybridTime value);

  ConsistentReadPoint& operator=(ConsistentReadPoint&& other);

 private:
  scoped_refptr<ClockBase> clock_;

  std::mutex mutex_;
  ReadHybridTime read_time_;
  HybridTime restart_read_ht_;

  // Local limits for separate tablets. Does not change during lifetime of a consistent read.
  // Times such that anything happening at that hybrid time or later is definitely after the
  // original request arrived and therefore does not have to be shown in results.
  HybridTimeMap local_limits_;

  // Restarts that happen during a consistent read. Used to initialise local_limits for restarted
  // read.
  HybridTimeMap restarts_;
};

} // namespace yb

#endif // YB_COMMON_CONSISTENT_READ_POINT_H
