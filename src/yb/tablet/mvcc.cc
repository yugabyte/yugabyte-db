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

#include "yb/util/logging.h"

namespace yb {
namespace tablet {

MvccManager::MvccManager(std::string prefix, server::ClockPtr clock)
    : prefix_(std::move(prefix)),
      clock_(std::move(clock)) {}

void MvccManager::Replicated(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    CHECK(!queue_.empty()) << LogPrefix();
    CHECK_EQ(queue_.front(), ht) << LogPrefix();
    PopFront(&lock);
    last_replicated_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::Aborted(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    CHECK(!queue_.empty()) << LogPrefix();
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
  CHECK_GE(queue_.size(), aborted_.size()) << LogPrefix();
  while (!aborted_.empty()) {
    if (queue_.front() != aborted_.top()) {
      CHECK_LT(queue_.front(), aborted_.top()) << LogPrefix();
      break;
    }
    queue_.pop_front();
    aborted_.pop();
  }
}

void MvccManager::AddPending(HybridTime* ht) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ht->is_valid()) {
    // This must be a follower-side transaction with already known hybrid time.
    VLOG_WITH_PREFIX(1) << "AddPending(" << *ht << ")";
  } else {
    // Otherwise this is a new transaction and we must assign a new hybrid_time. We assign one in
    // the present.
    *ht = clock_->Now();
    VLOG_WITH_PREFIX(1) << "AddPending(<invalid>), time from clock: " << *ht;
  }
  CHECK_GT(*ht, max_safe_time_returned_with_lease_) << LogPrefix();
  CHECK_GT(*ht, max_safe_time_returned_without_lease_) << LogPrefix();
  CHECK_GT(*ht, max_safe_time_returned_for_follower_) << LogPrefix();
  if (!queue_.empty()) {
    CHECK_GT(*ht, queue_.back()) << LogPrefix();
  }
  CHECK_GT(*ht, last_replicated_) << LogPrefix();
  queue_.push_back(*ht);
}

void MvccManager::SetLastReplicated(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_replicated_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::SetPropagatedSafeTimeOnFollower(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ")";

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ht > propagated_safe_time_) {
      propagated_safe_time_ = ht;
    } else {
      LOG(WARNING) << "Received propagated safe time " << ht << " less than the old value: "
                   << propagated_safe_time_ << ". This could happen on followers when a new leader "
                   << "is elected.";
    }
  }
  cond_.notify_all();
}

void MvccManager::UpdatePropagatedSafeTimeOnLeader(HybridTime ht_lease) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht_lease << ")";

  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto ht = DoGetSafeTime(HybridTime::kMin,  // min_allowed
                            MonoTime::kMax,    // deadline
                            ht_lease,
                            &lock);
    CHECK_GE(ht, propagated_safe_time_) << LogPrefix();
    propagated_safe_time_ = ht;
  }
  cond_.notify_all();
}

HybridTime MvccManager::SafeTimeForFollower(
    HybridTime min_allowed, MonoTime deadline) const {
  std::unique_lock<std::mutex> lock(mutex_);
  HybridTime result;
  auto predicate = [this, &result, min_allowed] {
    // last_replicated_ is updated earlier than propagated_safe_time_, so because of concurrency it
    // could be greater than propagated_safe_time_.
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
                                 HybridTime ht_lease) const {
  std::unique_lock<std::mutex> lock(mutex_);
  return DoGetSafeTime(min_allowed, deadline, ht_lease, &lock);
}

HybridTime MvccManager::DoGetSafeTime(const HybridTime min_allowed,
                                      const MonoTime deadline,
                                      const HybridTime ht_lease,
                                      std::unique_lock<std::mutex>* lock) const {
  DCHECK_ONLY_NOTNULL(lock);
  CHECK(ht_lease.is_valid());
  CHECK_LE(min_allowed, ht_lease) << LogPrefix();

  bool has_lease = false;
  if (ht_lease.GetPhysicalValueMicros() < kMaxHybridTimePhysicalMicros) {
    max_ht_lease_seen_ = std::max(ht_lease, max_ht_lease_seen_);
    has_lease = true;
  }

  HybridTime result;
  auto predicate = [this, &result, min_allowed, has_lease] {
    if (queue_.empty()) {
      result = clock_->Now();
      CHECK_GE(result, min_allowed) << LogPrefix();
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Now: " << result;
    } else {
      result = queue_.front().Decremented();
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Queue front (decremented): " << result;
    }

    if (has_lease) {
      result = std::min(result, max_ht_lease_seen_);
    }

    // This function could be invoked at a follower, so it has a very old ht_lease. In this case it
    // is safe to read at least at last_replicated_.
    result = std::max(result, last_replicated_);

    return result >= min_allowed;
  };

  // In the case of an empty queue, the safe hybrid time to read at is only limited by hybrid time
  // ht_lease, which is by definition higher than min_allowed, so we would not get blocked.
  if (deadline == MonoTime::kMax) {
    cond_.wait(*lock, predicate);
  } else if (!cond_.wait_until(*lock, deadline.ToSteadyTimePoint(), predicate)) {
    return HybridTime::kInvalid;
  }
  VLOG_WITH_PREFIX(1) << "DoGetSafeTime(" << min_allowed << ", "
                      << ht_lease << "), result = " << result;

  CHECK_GE(result, has_lease ? max_safe_time_returned_with_lease_
                             : max_safe_time_returned_without_lease_) << LogPrefix()
      << "has_lease=" << has_lease
      << ", ht_lease=" << ht_lease
      << ", max_ht_lease_seen_=" << max_ht_lease_seen_
      << ", last_replicated=" << last_replicated_
      << ", clock_->Now()=" << clock_->Now();
  if (has_lease) {
    max_safe_time_returned_with_lease_ = result;
  } else {
    max_safe_time_returned_without_lease_ = result;
  }
  return result;
}

HybridTime MvccManager::LastReplicatedHybridTime() const {
  std::lock_guard<std::mutex> lock(mutex_);
  VLOG_WITH_PREFIX(1) << __func__ << "(), result = " << last_replicated_;
  return last_replicated_;
}

}  // namespace tablet
}  // namespace yb
