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

#include <sstream>

#include "yb/util/logging.h"

namespace yb {
namespace tablet {

// ------------------------------------------------------------------------------------------------
// SafeTimeWithSource
// ------------------------------------------------------------------------------------------------

std::string SafeTimeWithSource::ToString() const {
  return Format("{ safe_time: $0 source: $1 }", safe_time, source);
}

// ------------------------------------------------------------------------------------------------
// MvccManager
// ------------------------------------------------------------------------------------------------

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
  const bool is_follower_side = ht->is_valid();
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_follower_side) {
    // This must be a follower-side transaction with already known hybrid time.
    VLOG_WITH_PREFIX(1) << "AddPending(" << *ht << ")";
  } else {
    // Otherwise this is a new transaction and we must assign a new hybrid_time. We assign one in
    // the present.
    *ht = clock_->Now();
    VLOG_WITH_PREFIX(1) << "AddPending(<invalid>), time from clock: " << *ht;
  }

  if (!queue_.empty() && *ht <= queue_.back() && !aborted_.empty()) {
    // To avoid crashing with an invariant violation on leader changes, we detect the case when
    // an entire tail of the operation queue has been aborted. Theoretically it is still possible
    // that the subset of aborted operations is not contiguous and/or does not end with the last
    // element of the queue. In practice, though, Raft should only abort and overwrite all
    // operations starting with a particular index and until the end of the log.
    auto iter = std::lower_bound(queue_.begin(), queue_.end(), aborted_.top());

    // Every hybrid time in aborted_ must also exist in queue_.
    CHECK(iter != queue_.end()) << LogPrefix();

    auto start_iter = iter;
    while (iter != queue_.end() && *iter == aborted_.top()) {
      aborted_.pop();
      iter++;
    }
    queue_.erase(start_iter, iter);
  }
  HybridTime last_ht_in_queue = queue_.empty() ? HybridTime::kMin : queue_.back();

  HybridTime sanity_check_lower_bound =
      std::max({
          max_safe_time_returned_with_lease_.safe_time,
          max_safe_time_returned_without_lease_.safe_time,
          max_safe_time_returned_for_follower_.safe_time,
          last_replicated_,
          last_ht_in_queue});

  if (*ht <= sanity_check_lower_bound) {
    auto get_details_msg = [&](bool drain_aborted) {
      std::ostringstream ss;
#define LOG_INFO_FOR_HT_LOWER_BOUND(t) \
             "\n  " << EXPR_VALUE_FOR_LOG(t) \
          << "\n  " << EXPR_VALUE_FOR_LOG(*ht < t.safe_time) \
          << "\n  " << EXPR_VALUE_FOR_LOG( \
                           static_cast<int64_t>(ht->ToUint64() - t.safe_time.ToUint64())) \
          << "\n  " << EXPR_VALUE_FOR_LOG(ht->PhysicalDiff(t.safe_time)) \
          << "\n  "

      ss << "New operation's hybrid time too low: " << *ht
         << LOG_INFO_FOR_HT_LOWER_BOUND(max_safe_time_returned_with_lease_)
         << LOG_INFO_FOR_HT_LOWER_BOUND(max_safe_time_returned_without_lease_)
         << LOG_INFO_FOR_HT_LOWER_BOUND(max_safe_time_returned_for_follower_)
         << LOG_INFO_FOR_HT_LOWER_BOUND(
                (SafeTimeWithSource{last_replicated_, SafeTimeSource::kUnknown}))
         << LOG_INFO_FOR_HT_LOWER_BOUND(
                (SafeTimeWithSource{last_ht_in_queue, SafeTimeSource::kUnknown}))
         << "\n  " << EXPR_VALUE_FOR_LOG(is_follower_side)
         << "\n  " << EXPR_VALUE_FOR_LOG(queue_.size())
         << "\n  " << EXPR_VALUE_FOR_LOG(queue_);
      if (drain_aborted) {
        std::vector<HybridTime> aborted;
        while (!aborted_.empty()) {
          aborted.push_back(aborted_.top());
          aborted_.pop();
        }
        ss << "\n  " << EXPR_VALUE_FOR_LOG(aborted);
      }
      return ss.str();
#undef LOG_INFO_FOR_HT_LOWER_BOUND
    };

#ifdef NDEBUG
    // In release mode, let's try to avoid crashing if possible if we ever hit this situation.
    // On the leader side, we can assign a timestamp that is high enough.
    if (!is_follower_side &&
        sanity_check_lower_bound &&
        sanity_check_lower_bound != HybridTime::kMax) {
      HybridTime incremented_hybrid_time = sanity_check_lower_bound.Incremented();
      YB_LOG_EVERY_N_SECS(ERROR, 5) << LogPrefix()
          << "Assigning an artificially incremented hybrid time: " << incremented_hybrid_time
          << ". This needs to be investigated. " << get_details_msg(/* drain_aborted */ false);
      *ht = incremented_hybrid_time;
    }
#endif

    if (*ht <= sanity_check_lower_bound) {
      LOG_WITH_PREFIX(FATAL) << get_details_msg(/* drain_aborted */ true);
    }
  }
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
    if (ht >= propagated_safe_time_) {
      propagated_safe_time_ = ht;
    } else {
      LOG_WITH_PREFIX(WARNING)
          << "Received propagated safe time " << ht << " less than the old value: "
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
    auto ht = DoGetSafeTime(HybridTime::kMin,       // min_allowed
                            CoarseTimePoint::max(), // deadline
                            ht_lease,
                            &lock);
#ifndef NDEBUG
    // This should only be called from RaftConsensus::UpdateMajorityReplicated, and ht_lease passed
    // in here should keep increasing, so we should not see propagated_safe_time_ going backwards.
    CHECK_GE(ht, propagated_safe_time_) << LogPrefix() << "ht_lease: " << ht_lease;
    propagated_safe_time_ = ht;
#else
    // Do not crash in production.
    if (ht < propagated_safe_time_) {
      YB_LOG_EVERY_N_SECS(ERROR, 5) << LogPrefix()
          << "Previously saw " << EXPR_VALUE_FOR_LOG(propagated_safe_time_)
          << ", but now safe time is " << ht;
    } else {
      propagated_safe_time_ = ht;
    }
#endif
  }
  cond_.notify_all();
}

void MvccManager::SetLeaderOnlyMode(bool leader_only) {
  std::unique_lock<std::mutex> lock(mutex_);
  leader_only_mode_ = leader_only;
}

HybridTime MvccManager::SafeTimeForFollower(
    HybridTime min_allowed, CoarseTimePoint deadline) const {
  std::unique_lock<std::mutex> lock(mutex_);

  if (leader_only_mode_) {
    // If there are no followers (RF == 1), use SafeTime()
    // because propagated_safe_time_ can be not updated.
    return DoGetSafeTime(min_allowed, deadline, HybridTime::kMax, &lock);
  }

  SafeTimeWithSource result;
  auto predicate = [this, &result, min_allowed] {
    // last_replicated_ is updated earlier than propagated_safe_time_, so because of concurrency it
    // could be greater than propagated_safe_time_.
    if (propagated_safe_time_ > last_replicated_) {
      result.safe_time = propagated_safe_time_;
      result.source = SafeTimeSource::kPropagated;
    } else {
      result.safe_time = last_replicated_;
      result.source = SafeTimeSource::kLastReplicated;
    }
    return result.safe_time >= min_allowed;
  };
  if (deadline == CoarseTimePoint::max()) {
    cond_.wait(lock, predicate);
  } else if (!cond_.wait_until(lock, deadline, predicate)) {
    return HybridTime::kInvalid;
  }
  VLOG_WITH_PREFIX(1) << "SafeTimeForFollower(" << min_allowed
                      << "), result = " << result.ToString();
  CHECK_GE(result.safe_time, max_safe_time_returned_for_follower_.safe_time)
      << LogPrefix() << "result: " << result.ToString()
      << ", max_safe_time_returned_for_follower_: "
      << max_safe_time_returned_for_follower_.ToString();
  max_safe_time_returned_for_follower_ = result;
  return result.safe_time;
}

HybridTime MvccManager::SafeTime(HybridTime min_allowed,
                                 CoarseTimePoint deadline,
                                 HybridTime ht_lease) const {
  std::unique_lock<std::mutex> lock(mutex_);
  return DoGetSafeTime(min_allowed, deadline, ht_lease, &lock);
}

HybridTime MvccManager::DoGetSafeTime(const HybridTime min_allowed,
                                      const CoarseTimePoint deadline,
                                      const HybridTime ht_lease,
                                      std::unique_lock<std::mutex>* lock) const {
  DCHECK_ONLY_NOTNULL(lock);
  CHECK(ht_lease.is_valid()) << LogPrefix();
  CHECK_LE(min_allowed, ht_lease) << LogPrefix();

  const bool has_lease = ht_lease.GetPhysicalValueMicros() < kMaxHybridTimePhysicalMicros;
  if (has_lease) {
    max_ht_lease_seen_ = std::max(ht_lease, max_ht_lease_seen_);
  }

  HybridTime result;
  SafeTimeSource source = SafeTimeSource::kUnknown;
  auto predicate = [this, &result, &source, min_allowed, has_lease] {
    if (queue_.empty()) {
      result = clock_->Now();
      source = SafeTimeSource::kNow;
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Now: " << result;
    } else {
      result = queue_.front().Decremented();
      source = SafeTimeSource::kNextInQueue;
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Queue front (decremented): " << result;
    }

    if (has_lease && result > max_ht_lease_seen_) {
      result = max_ht_lease_seen_;
      source = SafeTimeSource::kHybridTimeLease;
    }

    // This function could be invoked at a follower, so it has a very old ht_lease. In this case it
    // is safe to read at least at last_replicated_.
    result = std::max(result, last_replicated_);

    return result >= min_allowed;
  };

  // In the case of an empty queue, the safe hybrid time to read at is only limited by hybrid time
  // ht_lease, which is by definition higher than min_allowed, so we would not get blocked.
  if (deadline == CoarseTimePoint::max()) {
    cond_.wait(*lock, predicate);
  } else if (!cond_.wait_until(*lock, deadline, predicate)) {
    return HybridTime::kInvalid;
  }
  VLOG_WITH_PREFIX(1) << "DoGetSafeTime(" << min_allowed << ", "
                      << ht_lease << "), result = " << result;

  auto enforced_min_time = has_lease ? max_safe_time_returned_with_lease_.safe_time
                                     : max_safe_time_returned_without_lease_.safe_time;
  CHECK_GE(result, enforced_min_time) << LogPrefix()
      << ": " << EXPR_VALUE_FOR_LOG(has_lease)
      << ", " << EXPR_VALUE_FOR_LOG(enforced_min_time.ToUint64() - result.ToUint64())
      << ", " << EXPR_VALUE_FOR_LOG(ht_lease)
      << ", " << EXPR_VALUE_FOR_LOG(max_ht_lease_seen_)
      << ", " << EXPR_VALUE_FOR_LOG(last_replicated_)
      << ", " << EXPR_VALUE_FOR_LOG(clock_->Now())
      << ", " << EXPR_VALUE_FOR_LOG(ToString(deadline))
      << ", " << EXPR_VALUE_FOR_LOG(queue_.size())
      << ", " << EXPR_VALUE_FOR_LOG(queue_);

  if (has_lease) {
    max_safe_time_returned_with_lease_ = { result, source };
  } else {
    max_safe_time_returned_without_lease_ = { result, source };
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
