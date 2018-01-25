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

  {
    std::lock_guard<std::mutex> lock(mutex_);
    CHECK(!queue_.empty());
    CHECK_EQ(queue_.front(), ht);
    PopFront(&lock);
    last_replicated_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::Aborted(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << "Aborted(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    CHECK(!queue_.empty());
    if (queue_.front() == ht) {
      PopFront(&lock);
    } else {
      aborted_.push(ht);
      return;
    }
  }
  cond_.notify_all();
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
  CHECK_GT(*ht, max_safe_time_returned_) << LogPrefix();
  CHECK_GT(*ht, max_safe_time_returned_for_follower_) << LogPrefix();
  if (!queue_.empty()) {
    CHECK_GT(*ht, queue_.back());
  }
  CHECK_GT(*ht, last_replicated_);
  queue_.push_back(*ht);
}

void MvccManager::SetLastReplicated(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << "SetLastReplicated(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_replicated_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::SetPropagatedSafeTime(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << "SetPropagatedSafeTime(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    propagated_safe_time_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::UpdatePropagatedSafeTime(HybridTime max_allowed) {
  VLOG_WITH_PREFIX(1) << "UpdatePropagatedSafeTime(" << max_allowed << ")";

  {
    std::unique_lock<std::mutex> lock(mutex_);
    propagated_safe_time_ = DoGetSafeTime(HybridTime::kMin, MonoTime::kMax, max_allowed, &lock);
  }
  cond_.notify_all();
}

HybridTime MvccManager::SafeTimeForFollower(
    HybridTime min_allowed, MonoTime deadline) const {
  std::unique_lock<std::mutex> lock(mutex_);
  HybridTime result;
  auto predicate = [this, &result, min_allowed] {
    // last_replicated_ is updated earlier than propagated_safe_time_, so because of
    // concurrency it could be greater than propagated_safe_time_.
    result = std::max(propagated_safe_time_, last_replicated_);
    return result >= min_allowed;
  };
  if (deadline == MonoTime::kMax) {
    cond_.wait(lock, predicate);
  } else if (!cond_.wait_until(lock, deadline.ToSteadyTimePoint(), predicate)) {
    return HybridTime::kInvalid;
  }
  VLOG_WITH_PREFIX(1) << "SafeTimeForFollower(" << min_allowed
                      << "), result = " << result;
  CHECK_GE(result, max_safe_time_returned_for_follower_) << LogPrefix();
  max_safe_time_returned_for_follower_ = result;
  return result;
}

HybridTime MvccManager::SafeTime(HybridTime min_allowed,
                                 MonoTime deadline,
                                 HybridTime max_allowed) const {
  std::unique_lock<std::mutex> lock(mutex_);
  return DoGetSafeTime(min_allowed, deadline, max_allowed, &lock);
}

HybridTime MvccManager::DoGetSafeTime(HybridTime min_allowed,
                                      MonoTime deadline,
                                      HybridTime max_allowed,
                                      std::unique_lock<std::mutex>* lock) const {
  DCHECK_ONLY_NOTNULL(lock);
  CHECK_LE(min_allowed, max_allowed);
  HybridTime result;
  auto predicate = [this, &result, min_allowed, max_allowed] {
    if (queue_.empty()) {
      result = clock_->Now();
      CHECK_GE(result, min_allowed);
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Now: " << result;
    } else {
      result = queue_.front().Decremented();
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Queue front: " << result;
    }

    result = std::min(result, max_allowed);
    // This function could be invoked at a follower, so it has a very old max_allowed.
    // In this case it is safe to read at last_replicated_ at least.
    result = std::max(result, last_replicated_);
    return result >= min_allowed;
  };
  // In the case of an empty queue, the safe hybrid time to read at is only limited by hybrid time
  // max_allowed, which is by definition higher than min_allowed, so we would not get blocked.
  if (deadline == MonoTime::kMax) {
    cond_.wait(*lock, predicate);
  } else if (!cond_.wait_until(*lock, deadline.ToSteadyTimePoint(), predicate)) {
    return HybridTime::kInvalid;
  }
  VLOG_WITH_PREFIX(1) << "DoGetSafeTime(" << min_allowed << ", "
                      << max_allowed << "), result = " << result;
  CHECK_GE(result, max_safe_time_returned_) << LogPrefix();
  max_safe_time_returned_ = result;
  return result;
}

HybridTime MvccManager::LastReplicatedHybridTime() const {
  std::lock_guard<std::mutex> lock(mutex_);
  VLOG_WITH_PREFIX(1) << "LastReplicatedHybridTime(), result = " << last_replicated_;
  return last_replicated_;
}

}  // namespace tablet
}  // namespace yb
