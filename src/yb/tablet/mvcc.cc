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

#include <boost/circular_buffer.hpp>
#include <boost/variant.hpp>

#include "yb/gutil/macros.h"

#include "yb/util/atomic.h"
#include "yb/util/compare_util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/to_stream.h"
#include "yb/util/trace.h"

using std::ostream;

using namespace std::literals;

DEFINE_test_flag(int64, mvcc_op_trace_num_items, 32,
                 "Number of items to keep in an MvccManager operation trace. Set to 0 to disable "
                 "MVCC operation tracing.");

DEFINE_test_flag(int32, inject_mvcc_delay_add_leader_pending_ms, 0,
                 "Inject delay after MvccManager::AddLeaderPending read clock.");

namespace yb {
namespace tablet {

namespace {

struct SetLeaderOnlyModeTraceItem {
  bool leader_only;

  std::string ToString() const {
    return Format("SetLeaderOnlyMode $0", YB_STRUCT_TO_STRING(leader_only));
  }
};

struct SetLastReplicatedTraceItem {
  HybridTime ht;

  std::string ToString() const {
    return Format("SetLastReplicated $0", YB_STRUCT_TO_STRING(ht));
  }
};

struct SetPropagatedSafeTimeOnFollowerTraceItem {
  HybridTime ht;

  std::string ToString() const {
    return Format("SetPropagatedSafeTimeOnFollower $0", YB_STRUCT_TO_STRING(ht));
  }
};

struct UpdatePropagatedSafeTimeOnLeaderTraceItem {
  FixedHybridTimeLease ht_lease;
  HybridTime safe_time;

  std::string ToString() const {
    return Format("UpdatePropagatedSafeTimeOnLeader $0",
                  YB_STRUCT_TO_STRING(ht_lease, safe_time));
  }
};

struct AddLeaderPendingTraceItem {
  HybridTime ht;
  OpId op_id;

  std::string ToString() const {
    return Format("AddLeaderPending $0", YB_STRUCT_TO_STRING(ht, op_id));
  }
};

struct AddFollowerPendingTraceItem {
  HybridTime ht;
  OpId op_id;

  std::string ToString() const {
    return Format("AddFollowerPending $0", YB_STRUCT_TO_STRING(ht, op_id));
  }
};

struct ReplicatedTraceItem {
  HybridTime ht;
  OpId op_id;

  std::string ToString() const {
    return Format("Replicated $0", YB_STRUCT_TO_STRING(ht, op_id));
  }
};

struct AbortedTraceItem {
  HybridTime ht;
  OpId op_id;

  std::string ToString() const {
    return Format("Aborted $0", YB_STRUCT_TO_STRING(ht, op_id));
  }
};

struct SafeTimeTraceItem {
  HybridTime min_allowed;
  CoarseTimePoint deadline;
  FixedHybridTimeLease ht_lease;
  HybridTime safe_time;

  std::string ToString() const {
    return Format("SafeTime $0", YB_STRUCT_TO_STRING(min_allowed, deadline, ht_lease, safe_time));
  }
};

struct SafeTimeForFollowerTraceItem {
  HybridTime min_allowed;
  CoarseTimePoint deadline;
  SafeTimeWithSource safe_time_with_source;

  std::string ToString() const {
    return Format("SafeTimeForFollower $0",
                  YB_STRUCT_TO_STRING(min_allowed, deadline, safe_time_with_source));
  }
};

struct LastReplicatedHybridTimeTraceItem {
  HybridTime last_replicated;

  std::string ToString() const {
    return Format("LastReplicatedHybridTime $0", YB_STRUCT_TO_STRING(last_replicated));
  }
};

typedef boost::variant<
    SetLeaderOnlyModeTraceItem,
    SetLastReplicatedTraceItem,
    SetPropagatedSafeTimeOnFollowerTraceItem,
    UpdatePropagatedSafeTimeOnLeaderTraceItem,
    AddLeaderPendingTraceItem,
    AddFollowerPendingTraceItem,
    ReplicatedTraceItem,
    AbortedTraceItem,
    SafeTimeTraceItem,
    SafeTimeForFollowerTraceItem,
    LastReplicatedHybridTimeTraceItem
    > TraceItemVariant;

class ItemPrintingVisitor : public boost::static_visitor<>{
 public:
  explicit ItemPrintingVisitor(std::ostream* out, size_t index)
      : out_(*out),
        index_(index) {
  }

  template<typename T> void operator()(const T& t) const {
    out_ << index_ << ". " << t.ToString() << std::endl;
  }

 private:
  std::ostream& out_;
  size_t index_;
};

}  // namespace

std::string FixedHybridTimeLease::ToString() const {
  return YB_STRUCT_TO_STRING(time, lease);
}

class MvccManager::MvccOpTrace {
 public:
  explicit MvccOpTrace(size_t capacity) : items_(capacity) {}
  ~MvccOpTrace() = default;

  void Add(TraceItemVariant v) {
    items_.push_back(std::move(v));
  }

  void DumpTrace(ostream* out) const {
    if (items_.empty()) {
      *out << "No MVCC operations" << std::endl;
      return;
    }
    *out << "Recent " << items_.size() << " MVCC operations:" << std::endl;
    size_t i = 1;
    for (const auto& item : items_) {
      boost::apply_visitor(ItemPrintingVisitor(out, i), item);
      ++i;
    }
  }

 private:
  boost::circular_buffer_space_optimized<TraceItemVariant, std::allocator<TraceItemVariant>> items_;
};

struct MvccManager::InvariantViolationLoggingHelper {
  const std::string& log_prefix;
  MvccOpTrace* mvcc_op_trace;
};

std::ostream& operator<< (
    std::ostream& out,
    const MvccManager::InvariantViolationLoggingHelper& log_helper) {
  out << log_helper.log_prefix;
  log_helper.mvcc_op_trace->DumpTrace(&out);
  return out;
}

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
      clock_(std::move(clock)) {
  auto op_trace_num_items = GetAtomicFlag(&FLAGS_TEST_mvcc_op_trace_num_items);
  if (op_trace_num_items > 0) {
    op_trace_ = std::make_unique<MvccManager::MvccOpTrace>(op_trace_num_items);
  }
}

MvccManager::~MvccManager() {
}

void MvccManager::Replicated(HybridTime ht, const OpId& op_id) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ", " << op_id << ")";
  CHECK(!op_id.empty());

  {
    std::lock_guard lock(mutex_);
    if (op_trace_) {
      op_trace_->Add(ReplicatedTraceItem { .ht = ht, .op_id = op_id });
    }
    CHECK(!queue_.empty()) << InvariantViolationLogPrefix();
    CHECK_EQ(queue_.front(),
             (QueueItem{ .hybrid_time = ht, .op_id = op_id })) << InvariantViolationLogPrefix();
    queue_.pop_front();
    last_replicated_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::Aborted(HybridTime ht, const OpId& op_id) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ", " << op_id << ")";

  {
    std::lock_guard lock(mutex_);
    if (op_trace_) {
      op_trace_->Add(AbortedTraceItem { .ht = ht, .op_id = op_id });
    }
    CHECK(!queue_.empty()) << InvariantViolationLogPrefix();
    CHECK_EQ(queue_.back(),
             (QueueItem{ .hybrid_time = ht, .op_id = op_id }))
        << InvariantViolationLogPrefix() << "It is allowed to abort only last operation";
    queue_.pop_back();
  }
  cond_.notify_all();
}

bool BadNextOpId(const OpId& prev, const OpId& next) {
  if (prev.index >= next.index) {
    return true;
  }
  if (prev.term > next.term) {
    return true;
  }
  return false;
}

HybridTime MvccManager::AddLeaderPending(const OpId& op_id) {
  std::lock_guard lock(mutex_);
  auto ht = clock_->Now();
  AtomicFlagSleepMs(&FLAGS_TEST_inject_mvcc_delay_add_leader_pending_ms);
  VLOG_WITH_PREFIX(1) << __func__ << "(" << op_id << "), time: " << ht;
  AddPending(ht, op_id, /* is_follower_side= */ false);

  if (op_trace_) {
    op_trace_->Add(AddLeaderPendingTraceItem {
      .ht = ht,
      .op_id = op_id,
    });
  }

  return ht;
}

void MvccManager::AddFollowerPending(HybridTime ht, const OpId& op_id) {
  std::lock_guard lock(mutex_);
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ", " << op_id << ")";

  AddPending(ht, op_id, /* is_follower_side= */ true);

  if (op_trace_) {
    op_trace_->Add(AddFollowerPendingTraceItem {
      .ht = ht,
      .op_id = op_id,
    });
  }
}

void MvccManager::AddPending(HybridTime ht, const OpId& op_id, bool is_follower_side) {
  CHECK(!op_id.empty());

  HybridTime last_ht_in_queue = queue_.empty() ? HybridTime::kMin : queue_.back().hybrid_time;

  HybridTime sanity_check_lower_bound =
      std::max({
          max_safe_time_returned_with_lease_.safe_time,
          max_safe_time_returned_without_lease_.safe_time,
          max_safe_time_returned_for_follower_.safe_time,
          propagated_safe_time_,
          last_replicated_,
          last_ht_in_queue});

  if (ht <= sanity_check_lower_bound) {
    auto get_details_msg = [&](bool drain_aborted) {
      std::ostringstream ss;
#define LOG_INFO_FOR_HT_LOWER_BOUND_IMPL(full, safe_time) \
             "\n  " << YB_EXPR_TO_STREAM(full) \
          << "\n  " << (ht <= safe_time ? "!!! " : "") << YB_EXPR_TO_STREAM(ht <= safe_time) \
          << "\n  " << YB_EXPR_TO_STREAM( \
                           static_cast<int64_t>(ht.ToUint64() - safe_time.ToUint64())) \
          << "\n  " << YB_EXPR_TO_STREAM(ht.PhysicalDiff(safe_time)) \
          << "\n  "

#define LOG_INFO_FOR_HT_LOWER_BOUND_WITH_SOURCE(t) LOG_INFO_FOR_HT_LOWER_BOUND_IMPL(t, t.safe_time)
#define LOG_INFO_FOR_HT_LOWER_BOUND(t) LOG_INFO_FOR_HT_LOWER_BOUND_IMPL(t, t)

      ss << "New operation's hybrid time too low: " << ht << ", op id: " << op_id
         << LOG_INFO_FOR_HT_LOWER_BOUND_WITH_SOURCE(max_safe_time_returned_with_lease_)
         << LOG_INFO_FOR_HT_LOWER_BOUND_WITH_SOURCE(max_safe_time_returned_without_lease_)
         << LOG_INFO_FOR_HT_LOWER_BOUND_WITH_SOURCE(max_safe_time_returned_for_follower_)
         << LOG_INFO_FOR_HT_LOWER_BOUND(last_replicated_)
         << LOG_INFO_FOR_HT_LOWER_BOUND(last_ht_in_queue)
         << LOG_INFO_FOR_HT_LOWER_BOUND(propagated_safe_time_)
         << "\n  " << YB_EXPR_TO_STREAM(queue_.size())
         << "\n  " << YB_EXPR_TO_STREAM(queue_);
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
      ht = incremented_hybrid_time;
    }
#endif

    if (ht <= sanity_check_lower_bound) {
      LOG_WITH_PREFIX(FATAL) << InvariantViolationLogPrefix()
                             << get_details_msg(/* drain_aborted */ true);
    }
  }

  LOG_IF_WITH_PREFIX(DFATAL,
                     !queue_.empty() && BadNextOpId(queue_.back().op_id, op_id))
      << "Op sequence failure: " << AsString(queue_.back().op_id) << " followed by "
      << AsString(op_id) << " " << InvariantViolationLogPrefix();

  queue_.push_back(QueueItem {
    .hybrid_time = ht,
    .op_id = op_id,
  });
}

void MvccManager::SetLastReplicated(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ")";

  {
    std::lock_guard lock(mutex_);
    if (op_trace_) {
      op_trace_->Add(SetLastReplicatedTraceItem { .ht = ht });
    }
    last_replicated_ = ht;
  }
  cond_.notify_all();
}

void MvccManager::SetPropagatedSafeTimeOnFollower(HybridTime ht) {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht << ")";

  {
    std::lock_guard lock(mutex_);
    if (op_trace_) {
      op_trace_->Add(SetPropagatedSafeTimeOnFollowerTraceItem { .ht = ht });
    }
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

// NO_THREAD_SAFETY_ANALYSIS because this analysis does not work with unique_lock.
void MvccManager::UpdatePropagatedSafeTimeOnLeader(const FixedHybridTimeLease& ht_lease)
    NO_THREAD_SAFETY_ANALYSIS {
  VLOG_WITH_PREFIX(1) << __func__ << "(" << ht_lease << ")";

  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto safe_time = DoGetSafeTime(HybridTime::kMin,       // min_allowed
                                   CoarseTimePoint::max(), // deadline
                                   ht_lease,
                                   &lock);
#ifndef NDEBUG
    // This should only be called from RaftConsensus::UpdateMajorityReplicated, and ht_lease passed
    // in here should keep increasing, so we should not see propagated_safe_time_ going backwards.
    CHECK_GE(safe_time, propagated_safe_time_)
        << InvariantViolationLogPrefix()
        << "ht_lease: " << ht_lease;
    propagated_safe_time_ = safe_time;
#else
    // Do not crash in production.
    if (safe_time < propagated_safe_time_) {
      YB_LOG_EVERY_N_SECS(ERROR, 5) << LogPrefix()
          << "Previously saw " << YB_EXPR_TO_STREAM(propagated_safe_time_)
          << ", but now safe time is " << safe_time;
    } else {
      propagated_safe_time_ = safe_time;
    }
#endif

    if (op_trace_) {
      op_trace_->Add(UpdatePropagatedSafeTimeOnLeaderTraceItem {
        .ht_lease = ht_lease,
        .safe_time = safe_time
      });
    }
  }
  cond_.notify_all();
}

void MvccManager::SetLeaderOnlyMode(bool leader_only) {
  std::lock_guard lock(mutex_);
  if (op_trace_) {
    op_trace_->Add(SetLeaderOnlyModeTraceItem {
      .leader_only = leader_only
    });
  }
  leader_only_mode_ = leader_only;
}

// NO_THREAD_SAFETY_ANALYSIS because this analysis does not work with unique_lock.
HybridTime MvccManager::SafeTimeForFollower(
    HybridTime min_allowed, CoarseTimePoint deadline) const NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(mutex_);

  if (leader_only_mode_) {
    // If there are no followers (RF == 1), use SafeTime() because propagated_safe_time_ might not
    // have a valid value.
    return DoGetSafeTime(min_allowed, deadline, FixedHybridTimeLease(), &lock);
  }

  SafeTimeWithSource result;
  auto predicate = [this, &result, min_allowed] {
    // last_replicated_ is updated earlier than propagated_safe_time_, so because of concurrency it
    // could be greater than propagated_safe_time_.
    if (propagated_safe_time_ > last_replicated_) {
      if (queue_.empty() || propagated_safe_time_ < queue_.front().hybrid_time) {
        result.safe_time = propagated_safe_time_;
        result.source = SafeTimeSource::kPropagated;
      } else {
        result.safe_time = queue_.front().hybrid_time.Decremented();
        result.source = SafeTimeSource::kNextInQueue;
      }
    } else {
      result.safe_time = last_replicated_;
      result.source = SafeTimeSource::kLastReplicated;
    }
    VTRACE(3, "Current safe time $0. Source $1", yb::ToString(result.safe_time),
           yb::ToString(result.source));
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
      << InvariantViolationLogPrefix()
      << "result: " << result.ToString()
      << ", max_safe_time_returned_for_follower_: "
      << max_safe_time_returned_for_follower_.ToString();
  VTRACE(2, "Min requested safe time was $0", yb::ToString(min_allowed));
  VTRACE(2, "Returning safe time $0. Source $1", yb::ToString(result.safe_time),
         yb::ToString(result.source));
  max_safe_time_returned_for_follower_ = result;
  if (op_trace_) {
    op_trace_->Add(SafeTimeForFollowerTraceItem {
      .min_allowed = min_allowed,
      .deadline = deadline,
      .safe_time_with_source = result
    });
  }
  return result.safe_time;
}

// NO_THREAD_SAFETY_ANALYSIS because this analysis does not work with unique_lock.
HybridTime MvccManager::SafeTime(
    HybridTime min_allowed,
    CoarseTimePoint deadline,
    const FixedHybridTimeLease& ht_lease) const NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(mutex_);
  auto safe_time = DoGetSafeTime(min_allowed, deadline, ht_lease, &lock);
  if (op_trace_) {
    op_trace_->Add(SafeTimeTraceItem {
      .min_allowed = min_allowed,
      .deadline = deadline,
      .ht_lease = ht_lease,
      .safe_time = safe_time
    });
  }
  return safe_time;
}

HybridTime MvccManager::DoGetSafeTime(const HybridTime min_allowed,
                                      const CoarseTimePoint deadline,
                                      const FixedHybridTimeLease& ht_lease,
                                      std::unique_lock<std::mutex>* lock) const {
  DCHECK_ONLY_NOTNULL(lock);
  CHECK(ht_lease.lease.is_valid()) << InvariantViolationLogPrefix();
  CHECK_LE(min_allowed, ht_lease.lease) << InvariantViolationLogPrefix();

  const bool has_lease = !ht_lease.empty();
  // Because different calls that have current hybrid time leader lease as an argument can come to
  // us out of order, we might see an older value of hybrid time leader lease expiration after a
  // newer value. We mitigate this by always using the highest value we've seen.
  if (has_lease) {
    LOG_IF_WITH_PREFIX(DFATAL, !ht_lease.time.is_valid()) << "Bad ht lease: " << ht_lease;
  }

  HybridTime result;
  SafeTimeSource source = SafeTimeSource::kUnknown;
  auto predicate = [this, &result, &source, min_allowed, ht_lease, has_lease] {
    if (queue_.empty()) {
      result = ht_lease.time.is_valid()
          ? std::max(max_safe_time_returned_with_lease_.safe_time, ht_lease.time)
          : clock_->Now();
      source = SafeTimeSource::kNow;
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Now: " << result;
    } else {
      result = queue_.front().hybrid_time.Decremented();
      source = SafeTimeSource::kNextInQueue;
      VLOG_WITH_PREFIX(2) << "DoGetSafeTime, Queue front (decremented): " << result;
    }

    if (has_lease) {
      auto used_lease = std::max({ht_lease.lease, max_safe_time_returned_with_lease_.safe_time});
      if (result > used_lease) {
        result = used_lease;
        source = SafeTimeSource::kHybridTimeLease;
      }
    }

    // This function could be invoked at a follower, so it has a very old ht_lease. In this case it
    // is safe to read at least at last_replicated_.
    result = std::max(result, last_replicated_);
    VTRACE(3, "Current safe time $0. Source $1", yb::ToString(result),
           yb::ToString(source));

    return result >= min_allowed;
  };

  // In the case of an empty queue, the safe hybrid time to read at is only limited by hybrid time
  // ht_lease, which is by definition higher than min_allowed, so we would not get blocked.
  if (deadline == CoarseTimePoint::max()) {
    cond_.wait(*lock, predicate);
  } else if (!cond_.wait_until(*lock, deadline, predicate)) {
    return HybridTime::kInvalid;
  }
  VLOG_WITH_PREFIX_AND_FUNC(1)
      << "(" << min_allowed << ", " << ht_lease << "),  result = " << result;

  auto enforced_min_time = has_lease ? max_safe_time_returned_with_lease_.safe_time
                                     : max_safe_time_returned_without_lease_.safe_time;
  CHECK_GE(result, enforced_min_time)
      << InvariantViolationLogPrefix()
      << ": "
      << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
          has_lease,
          enforced_min_time.ToUint64() - result.ToUint64(),
          ht_lease,
          last_replicated_,
          clock_->Now(),
          ToString(deadline),
          queue_.size(),
          queue_);

  if (has_lease) {
    max_safe_time_returned_with_lease_ = { result, source };
  } else {
    max_safe_time_returned_without_lease_ = { result, source };
  }
  VTRACE(2, "Returning safe time $0. Source $1. Min requested safe time was $2",
         yb::ToString(result), yb::ToString(source), yb::ToString(min_allowed));
  return result;
}

HybridTime MvccManager::LastReplicatedHybridTime() const {
  std::lock_guard lock(mutex_);
  VLOG_WITH_PREFIX(1) << __func__ << "(), result = " << last_replicated_;
  if (op_trace_) {
    op_trace_->Add(LastReplicatedHybridTimeTraceItem {
      .last_replicated = last_replicated_
    });
  }
  return last_replicated_;
}

// Using NO_THREAD_SAFETY_ANALYSIS here because we're only reading op_trace_ here and it is set
// in the constructor.
MvccManager::InvariantViolationLoggingHelper MvccManager::InvariantViolationLogPrefix() const {
  return { prefix_, op_trace_.get() };
}

// Ditto regarding NO_THREAD_SAFETY_ANALYSIS.
void MvccManager::TEST_DumpTrace(std::ostream* out) NO_THREAD_SAFETY_ANALYSIS {
  if (op_trace_)
    op_trace_->DumpTrace(out);
}

std::string MvccManager::QueueItem::ToString() const {
  return YB_STRUCT_TO_STRING(hybrid_time, op_id);
}

bool MvccManager::QueueItem::Eq(const MvccManager::QueueItem& rhs) const {
  const auto& lhs = *this;
  return YB_STRUCT_EQUALS(hybrid_time, op_id);
}

}  // namespace tablet
}  // namespace yb
