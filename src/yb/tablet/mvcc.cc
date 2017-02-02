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

#include "yb/tablet/mvcc.h"

#include <algorithm>
#include <mutex>

#include <glog/logging.h>
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/port.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/server/logical_clock.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/stopwatch.h"

namespace yb { namespace tablet {

MvccManager::MvccManager(const scoped_refptr<server::Clock>& clock, bool enforce_invariants)
    : no_new_transactions_at_or_before_(HybridTime::kMin),
      earliest_in_flight_(HybridTime::kMax),
      max_write_timestamp_(HybridTime::kMin),
      enforce_invariants_(enforce_invariants),
      clock_(clock) {
  cur_snap_.all_committed_before_ = HybridTime::kInitialHybridTime;
  cur_snap_.none_committed_at_or_after_ = HybridTime::kInitialHybridTime;
}

HybridTime MvccManager::StartTransaction() {
  while (true) {
    HybridTime now = clock_->Now();
    std::lock_guard<LockType> l(lock_);
    if (PREDICT_TRUE(InitTransactionUnlocked(now))) {
      EnforceInvariantsIfNecessary(now);
      return now;
    }
  }
  // dummy return to avoid compiler warnings
  LOG(FATAL) << "Unreachable, added to avoid compiler warning.";
  return HybridTime::kInvalidHybridTime;
}
void MvccManager::EnforceInvariantsIfNecessary(const HybridTime& next) {
  if (enforce_invariants_) {
    DCHECK_GT(next, max_write_timestamp_) << "Timestamps assigned should be strictly monotonic.";
    max_write_timestamp_ = next;
  }
}

HybridTime MvccManager::StartTransactionAtLatest() {
  std::lock_guard<LockType> l(lock_);
  HybridTime now_latest = clock_->NowLatest();
  while (PREDICT_FALSE(!InitTransactionUnlocked(now_latest))) {
    now_latest = clock_->NowLatest();
  }

  // If in debug mode enforce that transactions have monotonically increasing
  // hybrid_times at all times
#ifndef NDEBUG
  if (!hybrid_times_in_flight_.empty()) {
    HybridTime max(std::max_element(hybrid_times_in_flight_.begin(),
                                   hybrid_times_in_flight_.end())->first);
    CHECK_EQ(max.value(), now_latest.value());
  }
#endif

  EnforceInvariantsIfNecessary(now_latest);
  return now_latest;
}

Status MvccManager::StartTransactionAtHybridTime(HybridTime hybrid_time) {
  std::lock_guard<LockType> l(lock_);
  if (PREDICT_FALSE(cur_snap_.IsCommitted(hybrid_time))) {
    return STATUS(IllegalState,
        strings::Substitute("HybridTime: $0 is already committed. Current Snapshot: $1",
                            hybrid_time.value(), cur_snap_.ToString()));
  }
  if (!InitTransactionUnlocked(hybrid_time)) {
    return STATUS(IllegalState,
        strings::Substitute("There is already a transaction with hybrid_time: $0 in flight.",
                            hybrid_time.value()));
  }

  EnforceInvariantsIfNecessary(hybrid_time);
  return Status::OK();
}

void MvccManager::StartApplyingTransaction(HybridTime hybrid_time) {
  std::lock_guard<LockType> l(lock_);
  auto it = hybrid_times_in_flight_.find(hybrid_time.value());
  if (PREDICT_FALSE(it == hybrid_times_in_flight_.end())) {
    LOG(FATAL) << "Cannot mark hybrid_time " << hybrid_time.ToString() << " as APPLYING: "
               << "not in the in-flight map.";
  }

  TxnState cur_state = it->second;
  if (PREDICT_FALSE(cur_state != RESERVED)) {
    LOG(FATAL) << "Cannot mark hybrid_time " << hybrid_time.ToString() << " as APPLYING: "
               << "wrong state: " << cur_state;
  }

  it->second = APPLYING;
}

bool MvccManager::InitTransactionUnlocked(const HybridTime& hybrid_time) {
  // Ensure that we didn't mark the given hybrid_time as "safe" in between
  // acquiring the time and taking the lock. This allows us to acquire hybrid_times
  // outside of the MVCC lock.
  if (PREDICT_FALSE(no_new_transactions_at_or_before_.CompareTo(hybrid_time) >= 0)) {
    return false;
  }
  // Since transactions only commit once they are in the past, and new
  // transactions always start either in the current time or the future,
  // we should never be trying to start a new transaction at the same time
  // as an already-committed one.
  DCHECK(!cur_snap_.IsCommitted(hybrid_time))
    << "Trying to start a new txn at already-committed hybrid_time "
    << hybrid_time.ToString()
    << " cur_snap_: " << cur_snap_.ToString();

  if (hybrid_time.CompareTo(earliest_in_flight_) < 0) {
    earliest_in_flight_ = hybrid_time;
  }

  return InsertIfNotPresent(&hybrid_times_in_flight_, hybrid_time.value(), RESERVED);
}

void MvccManager::CommitTransaction(HybridTime hybrid_time) {
  std::lock_guard<LockType> l(lock_);
  bool was_earliest = false;
  CommitTransactionUnlocked(hybrid_time, &was_earliest);

  // No more transactions will start with a ts that is lower than or equal
  // to 'hybrid_time', so we adjust the snapshot accordingly.
  if (no_new_transactions_at_or_before_.CompareTo(hybrid_time) < 0) {
    no_new_transactions_at_or_before_ = hybrid_time;
  }

  if (was_earliest) {
    // If this transaction was the earliest in-flight, we might have to adjust
    // the "clean" hybrid_time.
    AdjustCleanTime();
  }
}

void MvccManager::AbortTransaction(HybridTime hybrid_time) {
  std::lock_guard<LockType> l(lock_);

  // Remove from our in-flight list.
  TxnState old_state = RemoveInFlightAndGetStateUnlocked(hybrid_time);
  CHECK_EQ(old_state, RESERVED) << "transaction with hybrid_time " << hybrid_time.ToString()
                                << " cannot be aborted in state " << old_state;

  // If we're aborting the earliest transaction that was in flight,
  // update our cached value.
  if (earliest_in_flight_.CompareTo(hybrid_time) == 0) {
    AdvanceEarliestInFlightHybridTime();
  }
}

void MvccManager::OfflineCommitTransaction(HybridTime hybrid_time) {
  std::lock_guard<LockType> l(lock_);

  // Commit the transaction, but do not adjust 'all_committed_before_', that will
  // be done with a separate OfflineAdjustCurSnap() call.
  bool was_earliest = false;
  CommitTransactionUnlocked(hybrid_time, &was_earliest);

  if (was_earliest
      && no_new_transactions_at_or_before_.CompareTo(hybrid_time) >= 0) {
    // If this transaction was the earliest in-flight, we might have to adjust
    // the "clean" hybrid_time.
    AdjustCleanTime();
  }
}

MvccManager::TxnState MvccManager::RemoveInFlightAndGetStateUnlocked(HybridTime ts) {
  DCHECK(lock_.is_locked());

  auto it = hybrid_times_in_flight_.find(ts.value());
  if (it == hybrid_times_in_flight_.end()) {
    LOG(FATAL) << "Trying to remove hybrid_time which isn't in the in-flight set: "
               << ts.ToString();
  }
  TxnState state = it->second;
  hybrid_times_in_flight_.erase(it);
  return state;
}

void MvccManager::CommitTransactionUnlocked(HybridTime hybrid_time,
                                            bool* was_earliest_in_flight) {
  DCHECK(clock_->IsAfter(hybrid_time))
    << "Trying to commit a transaction with a future hybrid_time: "
    << hybrid_time.ToString() << ". Current time: " << clock_->Stringify(clock_->Now());

  *was_earliest_in_flight = earliest_in_flight_ == hybrid_time;

  // Remove from our in-flight list.
  TxnState old_state = RemoveInFlightAndGetStateUnlocked(hybrid_time);
  CHECK_EQ(old_state, APPLYING)
    << "Trying to commit a transaction which never entered APPLYING state: "
    << hybrid_time.ToString() << " state=" << old_state;

  // Add to snapshot's committed list
  cur_snap_.AddCommittedHybridTime(hybrid_time);

  // If we're committing the earliest transaction that was in flight,
  // update our cached value.
  if (*was_earliest_in_flight) {
    AdvanceEarliestInFlightHybridTime();
  }
}

void MvccManager::AdvanceEarliestInFlightHybridTime() {
  if (hybrid_times_in_flight_.empty()) {
    earliest_in_flight_ = HybridTime::kMax;
  } else {
    earliest_in_flight_ = HybridTime(std::min_element(hybrid_times_in_flight_.begin(),
                                                     hybrid_times_in_flight_.end())->first);
  }
}

void MvccManager::OfflineAdjustSafeTime(HybridTime safe_time) {
  std::lock_guard<LockType> l(lock_);

  // No more transactions will start with a ts that is lower than or equal
  // to 'safe_time', so we adjust the snapshot accordingly.
  if (no_new_transactions_at_or_before_.CompareTo(safe_time) < 0) {
    no_new_transactions_at_or_before_ = safe_time;
  }

  AdjustCleanTime();
}

// Remove any elements from 'v' which are < the given watermark.
static void FilterHybridTimes(std::vector<HybridTime::val_type>* v,
                             HybridTime::val_type watermark) {
  int j = 0;
  for (const auto& ts : *v) {
    if (ts >= watermark) {
      (*v)[j++] = ts;
    }
  }
  v->resize(j);
}

void MvccManager::AdjustCleanTime() {
  // There are two possibilities:
  //
  // 1) We still have an in-flight transaction earlier than 'no_new_transactions_at_or_before_'.
  //    In this case, we update the watermark to that transaction's hybrid_time.
  //
  // 2) There are no in-flight transactions earlier than 'no_new_transactions_at_or_before_'.
  //    (There may still be in-flight transactions with future hybrid_times due to
  //    commit-wait transactions which start in the future). In this case, we update
  //    the watermark to 'no_new_transactions_at_or_before_', since we know that no new
  //    transactions can start with an earlier hybrid_time.
  //
  // In either case, we have to add the newly committed ts only if it remains higher
  // than the new watermark.

  if (earliest_in_flight_.CompareTo(no_new_transactions_at_or_before_) < 0) {
    cur_snap_.all_committed_before_ = earliest_in_flight_;
  } else {
    cur_snap_.all_committed_before_ = no_new_transactions_at_or_before_;
  }

  // Filter out any committed hybrid_times that now fall below the watermark
  FilterHybridTimes(&cur_snap_.committed_hybrid_times_, cur_snap_.all_committed_before_.value());

  // it may also have unblocked some waiters.
  // Check if someone is waiting for transactions to be committed.
  if (PREDICT_FALSE(!waiters_.empty())) {
    auto iter = waiters_.begin();
    while (iter != waiters_.end()) {
      WaitingState* waiter = *iter;
      if (IsDoneWaitingUnlocked(*waiter)) {
        iter = waiters_.erase(iter);
        waiter->latch->CountDown();
        continue;
      }
      iter++;
    }
  }
}

Status MvccManager::WaitUntil(WaitFor wait_for, HybridTime ts,
                              const MonoTime& deadline) const {
  TRACE_EVENT2("tablet", "MvccManager::WaitUntil",
               "wait_for", wait_for == ALL_COMMITTED ? "all_committed" : "none_applying",
               "ts", ts.ToUint64())

  CountDownLatch latch(1);
  WaitingState waiting_state;
  {
    waiting_state.hybrid_time = ts;
    waiting_state.latch = &latch;
    waiting_state.wait_for = wait_for;

    std::lock_guard<LockType> l(lock_);
    if (IsDoneWaitingUnlocked(waiting_state)) return Status::OK();
    waiters_.push_back(&waiting_state);
  }
  if (waiting_state.latch->WaitUntil(deadline)) {
    return Status::OK();
  }
  // We timed out. We need to clean up our entry in the waiters_ array.

  std::lock_guard<LockType> l(lock_);
  // It's possible that while we were re-acquiring the lock, we did get
  // notified. In that case, we have no cleanup to do.
  if (waiting_state.latch->count() == 0) {
    return Status::OK();
  }

  waiters_.erase(std::find(waiters_.begin(), waiters_.end(), &waiting_state));
  return STATUS(TimedOut, strings::Substitute(
      "Timed out waiting for all transactions with ts < $0 to $1",
      clock_->Stringify(ts),
      wait_for == ALL_COMMITTED ? "commit" : "finish applying"));
}

bool MvccManager::IsDoneWaitingUnlocked(const WaitingState& waiter) const {
  switch (waiter.wait_for) {
    case ALL_COMMITTED:
      return AreAllTransactionsCommittedUnlocked(waiter.hybrid_time);
    case NONE_APPLYING:
      return !AnyApplyingAtOrBeforeUnlocked(waiter.hybrid_time);
  }
  LOG(FATAL); // unreachable
}

bool MvccManager::AreAllTransactionsCommittedUnlocked(HybridTime ts) const {
  if (hybrid_times_in_flight_.empty()) {
    // If nothing is in-flight, then check the clock. If the hybrid_time is in the past,
    // we know that no new uncommitted transactions may start before this ts.
    return ts.CompareTo(clock_->Now()) <= 0;
  }
  // If some transactions are in flight, then check the in-flight list.
  return !cur_snap_.MayHaveUncommittedTransactionsAtOrBefore(ts);
}

bool MvccManager::AnyApplyingAtOrBeforeUnlocked(HybridTime ts) const {
  for (const InFlightMap::value_type entry : hybrid_times_in_flight_) {
    if (entry.first <= ts.value()) {
      return true;
    }
  }
  return false;
}

void MvccManager::TakeSnapshot(MvccSnapshot *snap) const {
  std::lock_guard<LockType> l(lock_);
  *snap = cur_snap_;
}

Status MvccManager::WaitForCleanSnapshotAtHybridTime(HybridTime hybrid_time,
                                                    MvccSnapshot *snap,
                                                    const MonoTime& deadline) const {
  TRACE_EVENT0("tablet", "MvccManager::WaitForCleanSnapshotAtHybridTime");
  RETURN_NOT_OK(clock_->WaitUntilAfterLocally(hybrid_time, deadline));
  RETURN_NOT_OK(WaitUntil(ALL_COMMITTED, hybrid_time, deadline));
  *snap = MvccSnapshot(hybrid_time);
  return Status::OK();
}

void MvccManager::WaitForApplyingTransactionsToCommit() const {
  TRACE_EVENT0("tablet", "MvccManager::WaitForApplyingTransactionsToCommit");

  // Find the highest hybrid_time of an APPLYING transaction.
  HybridTime wait_for = HybridTime::kMin;
  {
    std::lock_guard<LockType> l(lock_);
    for (const InFlightMap::value_type entry : hybrid_times_in_flight_) {
      if (entry.second == APPLYING) {
        wait_for = HybridTime(std::max(entry.first, wait_for.value()));
      }
    }
  }

  // Wait until there are no transactions applying with that hybrid_time
  // or below. It's possible that we're a bit conservative here - more transactions
  // may enter the APPLYING set while we're waiting, but we will eventually
  // succeed.
  if (wait_for == HybridTime::kMin) {
    // None were APPLYING: we can just return.
    return;
  }
  CHECK_OK(WaitUntil(NONE_APPLYING, wait_for, MonoTime::Max()));
}

bool MvccManager::AreAllTransactionsCommitted(HybridTime ts) const {
  std::lock_guard<LockType> l(lock_);
  return AreAllTransactionsCommittedUnlocked(ts);
}

HybridTime MvccManager::GetCleanHybridTime() const {
  std::lock_guard<LockType> l(lock_);
  return cur_snap_.all_committed_before_;
}

void MvccManager::GetApplyingTransactionsHybridTimes(std::vector<HybridTime>* hybrid_times) const {
  std::lock_guard<LockType> l(lock_);
  hybrid_times->reserve(hybrid_times_in_flight_.size());
  for (const InFlightMap::value_type entry : hybrid_times_in_flight_) {
    if (entry.second == APPLYING) {
      hybrid_times->push_back(HybridTime(entry.first));
    }
  }
}

MvccManager::~MvccManager() {
  CHECK(waiters_.empty());
}

////////////////////////////////////////////////////////////
// MvccSnapshot
////////////////////////////////////////////////////////////

MvccSnapshot::MvccSnapshot()
  : all_committed_before_(HybridTime::kInitialHybridTime),
    none_committed_at_or_after_(HybridTime::kInitialHybridTime) {
}

MvccSnapshot::MvccSnapshot(const MvccManager &manager) {
  manager.TakeSnapshot(this);
}

MvccSnapshot::MvccSnapshot(const HybridTime& hybrid_time)
  : all_committed_before_(hybrid_time),
    none_committed_at_or_after_(hybrid_time) {
}

MvccSnapshot MvccSnapshot::CreateSnapshotIncludingAllTransactions() {
  return MvccSnapshot(HybridTime::kMax);
}

MvccSnapshot MvccSnapshot::CreateSnapshotIncludingNoTransactions() {
  return MvccSnapshot(HybridTime::kMin);
}

bool MvccSnapshot::IsCommittedFallback(const HybridTime& hybrid_time) const {
  for (const HybridTime::val_type& v : committed_hybrid_times_) {
    if (v == hybrid_time.value()) return true;
  }

  return false;
}

bool MvccSnapshot::MayHaveCommittedTransactionsAtOrAfter(const HybridTime& hybrid_time) const {
  return hybrid_time.CompareTo(none_committed_at_or_after_) < 0;
}

bool MvccSnapshot::MayHaveUncommittedTransactionsAtOrBefore(const HybridTime& hybrid_time) const {
  // The snapshot may have uncommitted transactions before 'hybrid_time' if:
  // - 'all_committed_before_' comes before 'hybrid_time'
  // - 'all_committed_before_' is precisely 'hybrid_time' but 'hybrid_time' isn't in the
  //   committed set.
  return hybrid_time.CompareTo(all_committed_before_) > 0 ||
      (hybrid_time.CompareTo(all_committed_before_) == 0 && !IsCommittedFallback(hybrid_time));
}

std::string MvccSnapshot::ToString() const {
  string ret("MvccSnapshot[committed={T|");

  if (committed_hybrid_times_.size() == 0) {
    StrAppend(&ret, "T < ", all_committed_before_.ToString(), "}]");
    return ret;
  }
  StrAppend(&ret, "T < ", all_committed_before_.ToString(),
            " or (T in {");

  bool first = true;
  for (HybridTime::val_type t : committed_hybrid_times_) {
    if (!first) {
      ret.push_back(',');
    }
    first = false;
    StrAppend(&ret, t);
  }
  ret.append("})}]");
  return ret;
}

void MvccSnapshot::AddCommittedHybridTimes(const std::vector<HybridTime>& hybrid_times) {
  for (const HybridTime& ts : hybrid_times) {
    AddCommittedHybridTime(ts);
  }
}

void MvccSnapshot::AddCommittedHybridTime(HybridTime hybrid_time) {
  if (IsCommitted(hybrid_time)) return;

  committed_hybrid_times_.push_back(hybrid_time.value());

  // If this is a new upper bound commit mark, update it.
  if (none_committed_at_or_after_.CompareTo(hybrid_time) <= 0) {
    none_committed_at_or_after_ = HybridTime(hybrid_time.value() + 1);
  }
}

////////////////////////////////////////////////////////////
// ScopedTransaction
////////////////////////////////////////////////////////////
ScopedTransaction::ScopedTransaction(MvccManager *mgr, HybridTimeAssignmentType assignment_type)
  : done_(false),
    manager_(DCHECK_NOTNULL(mgr)),
    assignment_type_(assignment_type) {

  switch (assignment_type_) {
    case NOW: {
      hybrid_time_ = mgr->StartTransaction();
      break;
    }
    case NOW_LATEST: {
      hybrid_time_ = mgr->StartTransactionAtLatest();
      break;
    }
    default: {
      LOG(FATAL) << "Illegal TransactionAssignmentType. Only NOW and NOW_LATEST are supported"
          " by this ctor.";
    }
  }
}

ScopedTransaction::ScopedTransaction(MvccManager *mgr,
                                     HybridTime hybrid_time)
    : done_(false),
      manager_(DCHECK_NOTNULL(mgr)),
      assignment_type_(PRE_ASSIGNED),
      hybrid_time_(hybrid_time) {
  CHECK_OK(mgr->StartTransactionAtHybridTime(hybrid_time));
}

ScopedTransaction::~ScopedTransaction() {
  if (!done_) {
    Abort();
  }
}

void ScopedTransaction::StartApplying() {
  manager_->StartApplyingTransaction(hybrid_time_);
}

void ScopedTransaction::Commit() {
  switch (assignment_type_) {
    case NOW:
    case NOW_LATEST: {
      manager_->CommitTransaction(hybrid_time_);
      done_ = true;
      return;
    }
    case PRE_ASSIGNED: {
      manager_->OfflineCommitTransaction(hybrid_time_);
      done_ = true;
      return;
    }
  }
  LOG(FATAL) << "Unexpected transaction assignment type " << assignment_type_;
}

void ScopedTransaction::Abort() {
  manager_->AbortTransaction(hybrid_time_);
  done_ = true;
}


}  // namespace tablet
}  // namespace yb
