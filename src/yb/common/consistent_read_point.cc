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
#include "yb/common/consistent_read_point.h"

#include <mutex>
#include <type_traits>
#include <utility>

#include "yb/common/common.pb.h"

namespace yb {

ConsistentReadPoint::ConsistentReadPoint(const scoped_refptr<ClockBase>& clock)
    : clock_(clock) {
}

void ConsistentReadPoint::SetReadTimeUnlocked(
    const ReadHybridTime& read_time, HybridTimeMap* local_limits) {
  read_time_ = read_time;
  read_time_.local_limit = read_time.global_limit;
  restart_read_ht_ = read_time_.read;
  if (local_limits) {
    local_limits_ = std::move(*local_limits);
  } else {
    local_limits_.clear();
  }
  restarts_.clear();
}

void ConsistentReadPoint::SetCurrentReadTimeUnlocked(ClampUncertaintyWindow clamp) {
  SetReadTimeUnlocked(
      clamp ? ReadHybridTime::SingleTime(clock_->Now())
            : ReadHybridTime::FromHybridTimeRange(clock_->NowRange()));
}

void ConsistentReadPoint::SetReadTime(
    const ReadHybridTime& read_time, HybridTimeMap&& local_limits) {
  std::lock_guard lock(mutex_);
  SetReadTimeUnlocked(read_time, &local_limits);
}

void ConsistentReadPoint::SetCurrentReadTime(ClampUncertaintyWindow clamp) {
  std::lock_guard lock(mutex_);
  SetCurrentReadTimeUnlocked(clamp);
}

Status ConsistentReadPoint::TrySetDeferredCurrentReadTime() {
  std::lock_guard lock(mutex_);
  if (read_time_) {
    RSTATUS_DCHECK_EQ(
        read_time_.read, read_time_.global_limit, IllegalState, "Deferred read point is expected.");
  } else {
    SetCurrentReadTimeUnlocked();
    read_time_.read = read_time_.global_limit;
    restart_read_ht_ = read_time_.read;
  }
  return Status::OK();
}

ReadHybridTime ConsistentReadPoint::GetReadTime(const TabletId& tablet) const {
  std::lock_guard lock(mutex_);
  ReadHybridTime read_time = read_time_;
  if (read_time) {
    // Use the local limit for the tablet but no earlier than the read time we want.
    const auto it = local_limits_.find(tablet);
    if (it != local_limits_.end()) {
      read_time.local_limit = it->second;
    }
  }
  return read_time;
}

void ConsistentReadPoint::RestartRequired(const TabletId& tablet,
                                          const ReadHybridTime& restart_time) {
  std::lock_guard lock(mutex_);
  RestartRequiredUnlocked(tablet, restart_time);
}

void ConsistentReadPoint::RestartRequiredUnlocked(
    const TabletId& tablet, const ReadHybridTime& restart_time) {
  DCHECK(read_time_) << "Unexpected restart without a read time set";
  restart_read_ht_.MakeAtLeast(restart_time.read);
  // We should inherit per-tablet restart time limits before restart, doing it lazily.
  if (restarts_.empty()) {
    restarts_ = local_limits_;
  }
  UpdateLimitsMapUnlocked(tablet, restart_time.local_limit, &restarts_);
}

void ConsistentReadPoint::UpdateLocalLimit(const TabletId& tablet, HybridTime local_limit) {
  std::lock_guard lock(mutex_);
  UpdateLimitsMapUnlocked(tablet, local_limit, &local_limits_);
}

void ConsistentReadPoint::UpdateLimitsMapUnlocked(
    const TabletId& tablet, const HybridTime& local_limit, HybridTimeMap* map) {
  auto emplace_result = map->emplace(tablet, local_limit);
  bool inserted = emplace_result.second;
  if (!inserted) {
    auto& existing_local_limit = emplace_result.first->second;
    existing_local_limit = std::min(existing_local_limit, local_limit);
  }
}

bool ConsistentReadPoint::IsRestartRequired() const {
  std::lock_guard lock(mutex_);
  return IsRestartRequiredUnlocked();
}

bool ConsistentReadPoint::IsRestartRequiredUnlocked() const {
  return !restarts_.empty();
}

void ConsistentReadPoint::Restart() {
  std::lock_guard lock(mutex_);
  local_limits_.swap(restarts_);
  restarts_.clear();
  read_time_.read = restart_read_ht_;
}

void ConsistentReadPoint::Defer() {
  std::lock_guard lock(mutex_);
  read_time_.read = read_time_.global_limit;
}

void ConsistentReadPoint::UpdateClock(HybridTime propagated_hybrid_time) {
  clock_->Update(propagated_hybrid_time);
}

HybridTime ConsistentReadPoint::Now() const {
  return clock_->Now();
}

void ConsistentReadPoint::PrepareChildTransactionData(ChildTransactionDataPB* data) const {
  std::lock_guard lock(mutex_);
  read_time_.AddToPB(data);
  auto& local_limits = *data->mutable_local_limits();
  for (const auto& entry : local_limits_) {
    using PairType = std::remove_reference_t<decltype(*local_limits.begin())>;
    local_limits.insert(PairType(entry.first, entry.second.ToUint64()));
  }
}

void ConsistentReadPoint::FinishChildTransactionResult(
    HadReadTime had_read_time, ChildTransactionResultPB* result) const {
  std::lock_guard lock(mutex_);
  if (IsRestartRequiredUnlocked()) {
    result->set_restart_read_ht(restart_read_ht_.ToUint64());
    auto& restarts = *result->mutable_read_restarts();
    for (const auto& restart : restarts_) {
      using PairType = std::remove_reference_t<decltype(*restarts.begin())>;
      restarts.insert(PairType(restart.first, restart.second.ToUint64()));
    }
  } else {
    result->set_restart_read_ht(HybridTime::kInvalid.ToUint64());
  }

  if (!had_read_time && read_time_) {
    read_time_.ToPB(result->mutable_used_read_time());
  }
}

void ConsistentReadPoint::ApplyChildTransactionResult(const ChildTransactionResultPB& result) {
  std::lock_guard lock(mutex_);
  if (result.has_used_read_time()) {
    LOG_IF(DFATAL, read_time_)
        << "Read time already picked (" << read_time_
        << ", but child result contains used read time: "
        << result.used_read_time().ShortDebugString();
    read_time_ = ReadHybridTime::FromPB(result.used_read_time());
    restart_read_ht_ = read_time_.read;
  }

  HybridTime restart_read_ht(result.restart_read_ht());
  if (restart_read_ht.is_valid()) {
    ReadHybridTime read_time;
    read_time.read = restart_read_ht;
    for (const auto& restart : result.read_restarts()) {
      read_time.local_limit = HybridTime(restart.second);
      RestartRequiredUnlocked(restart.first, read_time);
    }
  }
}

void ConsistentReadPoint::SetInTxnLimit(HybridTime value) {
  std::lock_guard lock(mutex_);
  read_time_.in_txn_limit = value;
}

ReadHybridTime ConsistentReadPoint::GetReadTime() const {
  std::lock_guard lock(mutex_);
  return read_time_;
}

// NO_THREAD_SAFETY_ANALYSIS is required here because analysis does not understand std::lock.
void ConsistentReadPoint::MoveFrom(ConsistentReadPoint* rhs) NO_THREAD_SAFETY_ANALYSIS {
  std::lock(mutex_, rhs->mutex_);
  std::lock_guard lock1(mutex_, std::adopt_lock);
  std::lock_guard lock2(rhs->mutex_, std::adopt_lock);
  read_time_ = rhs->read_time_;
  restart_read_ht_ = rhs->restart_read_ht_;
  local_limits_ = std::move(rhs->local_limits_);
  restarts_ = std::move(rhs->restarts_);
}

ConsistentReadPoint::Momento ConsistentReadPoint::GetMomento() const {
  std::lock_guard lock(mutex_);
  return {read_time_, restart_read_ht_, local_limits_, restarts_};
}

void ConsistentReadPoint::SetMomento(ConsistentReadPoint::Momento&& momento) {
  std::lock_guard lock(mutex_);
  read_time_ = std::move(momento.read_time_);
  restart_read_ht_ = std::move(momento.restart_read_ht_);
  local_limits_ = std::move(momento.local_limits_);
  restarts_ = std::move(momento.restarts_);
}

} // namespace yb
