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

#include "yb/tablet/mvcc.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"

namespace yb {
namespace tablet {

MvccManager::MvccManager(std::string prefix, server::ClockPtr clock)
    : prefix_(std::move(prefix)), clock_(std::move(clock)) {}

void MvccManager::Replicated(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << "Replicated(" << ht << ")";

  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(!queue_.empty());
  CHECK_EQ(queue_.front(), ht);
  PopFront(&lock);
  last_replicated_ = ht;
}

void MvccManager::Aborted(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << "Aborted(" << ht << ")";

  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(!queue_.empty());
  if (queue_.front() == ht) {
    PopFront(&lock);
  } else {
    aborted_.push(ht);
  }
}

void MvccManager::PopFront(std::lock_guard<std::mutex>* lock) {
  queue_.pop_front();
  CHECK_GE(queue_.size(), aborted_.size());
  while (!aborted_.empty()) {
    if (queue_.front() != aborted_.top()) {
      CHECK_LT(queue_.front(), aborted_.top());
      break;
    }
    queue_.pop_front();
    aborted_.pop();
  }
}

void MvccManager::AddPending(HybridTime* ht) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ht->is_valid()) {
    // ... otherwise this is a new transaction and we must assign a new hybrid_time. We assign
    // one in the present.
    *ht = clock_->Now();
    VLOG_WITH_PREFIX(1) << "AddPending(<invalid>), time from clock: " << *ht;
  } else {
    VLOG_WITH_PREFIX(1) << "AddPending(" << *ht << ")";
  }
  CHECK_GT(*ht, max_safe_time_returned_);
  if (!queue_.empty()) {
    CHECK_GT(*ht, queue_.back());
  }
  CHECK_GT(*ht, last_replicated_);
  queue_.push_back(*ht);
}

void MvccManager::SetLastReplicated(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << "SetLastReplicated(" << ht << ")";

  std::lock_guard<std::mutex> lock(mutex_);
  last_replicated_ = ht;
}

HybridTime MvccManager::SafeTimestampToRead(HybridTime limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  HybridTime result = !queue_.empty() ? queue_.front().Decremented() : clock_->Now();
  result = std::min(result, limit);
  result = std::max(result, last_replicated_); // Suitable to replica
  CHECK_GE(result, max_safe_time_returned_);
  max_safe_time_returned_ = result;
  VLOG_WITH_PREFIX(1) << "GetMaxSafeTimeToReadAt(), result = " << result;
  return result;
}

HybridTime MvccManager::LastReplicatedHybridTime() const {
  std::lock_guard<std::mutex> lock(mutex_);
  VLOG_WITH_PREFIX(1) << "LastReplicatedHybridTime(), result = " << last_replicated_;
  return last_replicated_;
}

}  // namespace tablet
}  // namespace yb
