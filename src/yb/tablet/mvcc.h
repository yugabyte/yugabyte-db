// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_TABLET_MVCC_H_
#define YB_TABLET_MVCC_H_

#include <mutex>
#include <deque>
#include <queue>
#include <vector>

#include "yb/server/clock.h"

namespace yb {
namespace tablet {

// MvccManager is used to track operations.
// When new operation is initiated its time should be added using AddPending.
// When operation is replicated or aborted, MvccManager is notified using Replicated or Aborted
// methods.
// Operations could be replicated only in the same order as they were added.
// Time of newly added operation should be after time of all previously added operations.
class MvccManager {
 public:
  // `prefix` is used for logging.
  explicit MvccManager(std::string prefix, server::ClockPtr clock);

  // Sets time of last replicated operation, used after bootstrap.
  void SetLastReplicated(HybridTime ht);

  // Adds time of new tracked operation.
  // `ht` is in-out parameter.
  // In case of replica `ht` is already assigned, in case of leader we should assign ht by
  // by ourselves.
  // We pass ht as pointer here, because clock should be accessed with locked mutex, otherwise
  // SafeTimestampToRead could return time greater than added.
  void AddPending(HybridTime* ht);

  // Notifies that operation with appropriate time was replicated.
  // It should be first operation in queue.
  void Replicated(HybridTime ht);

  // Notifies that operation with appropriate time was aborted.
  void Aborted(HybridTime ht);

  // Returns maximal allowed timestamp to read. I.e. no operations that was initiated after this
  // call will receive hybrid time less than returned.
  HybridTime SafeTimestampToRead(HybridTime limit) const;

  // Returns time of last replicated operation.
  HybridTime LastReplicatedHybridTime() const;

 private:
  const std::string& LogPrefix() const { return prefix_; }
  void PopFront(std::lock_guard<std::mutex>* lock);

  std::string prefix_;
  server::ClockPtr clock_;
  mutable std::mutex mutex_;
  // Queue of times of tracked operations. It is ordered.
  std::deque<HybridTime> queue_;
  // Priority queue of aborted operations. Required because we could abort operations from the
  // middle of the queue.
  std::priority_queue<HybridTime, std::vector<HybridTime>, std::greater<>> aborted_;
  HybridTime last_replicated_ = HybridTime::kMin;
  mutable HybridTime max_safe_time_returned_ = HybridTime::kMin;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_MVCC_H_
