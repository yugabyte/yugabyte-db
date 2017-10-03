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

#include <algorithm>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <glog/logging.h>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/mathlimits.h"
#include "kudu/gutil/port.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/server/logical_clock.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/stopwatch.h"

namespace kudu { namespace tablet {

MvccManager::MvccManager(const scoped_refptr<server::Clock>& clock)
  : no_new_transactions_at_or_before_(Timestamp::kMin),
    earliest_in_flight_(Timestamp::kMax),
    clock_(clock) {
  cur_snap_.all_committed_before_ = Timestamp::kInitialTimestamp;
  cur_snap_.none_committed_at_or_after_ = Timestamp::kInitialTimestamp;
}

Timestamp MvccManager::StartTransaction() {
  while (true) {
    Timestamp now = clock_->Now();
    boost::lock_guard<LockType> l(lock_);
    if (PREDICT_TRUE(InitTransactionUnlocked(now))) {
      return now;
    }
  }
  // dummy return to avoid compiler warnings
  LOG(FATAL) << "Unreachable, added to avoid compiler warning.";
  return Timestamp::kInvalidTimestamp;
}

Timestamp MvccManager::StartTransactionAtLatest() {
  boost::lock_guard<LockType> l(lock_);
  Timestamp now_latest = clock_->NowLatest();
  while (PREDICT_FALSE(!InitTransactionUnlocked(now_latest))) {
    now_latest = clock_->NowLatest();
  }

  // If in debug mode enforce that transactions have monotonically increasing
  // timestamps at all times
#ifndef NDEBUG
  if (!timestamps_in_flight_.empty()) {
    Timestamp max(std::max_element(timestamps_in_flight_.begin(),
                                   timestamps_in_flight_.end())->first);
    CHECK_EQ(max.value(), now_latest.value());
  }
#endif

  return now_latest;
}

Status MvccManager::StartTransactionAtTimestamp(Timestamp timestamp) {
  boost::lock_guard<LockType> l(lock_);
  if (PREDICT_FALSE(cur_snap_.IsCommitted(timestamp))) {
    return Status::IllegalState(
        strings::Substitute("Timestamp: $0 is already committed. Current Snapshot: $1",
                            timestamp.value(), cur_snap_.ToString()));
  }
  if (!InitTransactionUnlocked(timestamp)) {
    return Status::IllegalState(
        strings::Substitute("There is already a transaction with timestamp: $0 in flight.",
                            timestamp.value()));
  }
  return Status::OK();
}

void MvccManager::StartApplyingTransaction(Timestamp timestamp) {
  boost::lock_guard<LockType> l(lock_);
  auto it = timestamps_in_flight_.find(timestamp.value());
  if (PREDICT_FALSE(it == timestamps_in_flight_.end())) {
    LOG(FATAL) << "Cannot mark timestamp " << timestamp.ToString() << " as APPLYING: "
               << "not in the in-flight map.";
  }

  TxnState cur_state = it->second;
  if (PREDICT_FALSE(cur_state != RESERVED)) {
    LOG(FATAL) << "Cannot mark timestamp " << timestamp.ToString() << " as APPLYING: "
               << "wrong state: " << cur_state;
  }

  it->second = APPLYING;
}

bool MvccManager::InitTransactionUnlocked(const Timestamp& timestamp) {
  // Ensure that we didn't mark the given timestamp as "safe" in between
  // acquiring the time and taking the lock. This allows us to acquire timestamps
  // outside of the MVCC lock.
  if (PREDICT_FALSE(no_new_transactions_at_or_before_.CompareTo(timestamp) >= 0)) {
    return false;
  }
  // Since transactions only commit once they are in the past, and new
  // transactions always start either in the current time or the future,
  // we should never be trying to start a new transaction at the same time
  // as an already-committed one.
  DCHECK(!cur_snap_.IsCommitted(timestamp))
    << "Trying to start a new txn at already-committed timestamp "
    << timestamp.ToString()
    << " cur_snap_: " << cur_snap_.ToString();

  if (timestamp.CompareTo(earliest_in_flight_) < 0) {
    earliest_in_flight_ = timestamp;
  }

  return InsertIfNotPresent(&timestamps_in_flight_, timestamp.value(), RESERVED);
}

void MvccManager::CommitTransaction(Timestamp timestamp) {
  boost::lock_guard<LockType> l(lock_);
  bool was_earliest = false;
  CommitTransactionUnlocked(timestamp, &was_earliest);

  // No more transactions will start with a ts that is lower than or equal
  // to 'timestamp', so we adjust the snapshot accordingly.
  if (no_new_transactions_at_or_before_.CompareTo(timestamp) < 0) {
    no_new_transactions_at_or_before_ = timestamp;
  }

  if (was_earliest) {
    // If this transaction was the earliest in-flight, we might have to adjust
    // the "clean" timestamp.
    AdjustCleanTime();
  }
}

void MvccManager::AbortTransaction(Timestamp timestamp) {
  boost::lock_guard<LockType> l(lock_);

  // Remove from our in-flight list.
  TxnState old_state = RemoveInFlightAndGetStateUnlocked(timestamp);
  CHECK_EQ(old_state, RESERVED) << "transaction with timestamp " << timestamp.ToString()
                                << " cannot be aborted in state " << old_state;

  // If we're aborting the earliest transaction that was in flight,
  // update our cached value.
  if (earliest_in_flight_.CompareTo(timestamp) == 0) {
    AdvanceEarliestInFlightTimestamp();
  }
}

void MvccManager::OfflineCommitTransaction(Timestamp timestamp) {
  boost::lock_guard<LockType> l(lock_);

  // Commit the transaction, but do not adjust 'all_committed_before_', that will
  // be done with a separate OfflineAdjustCurSnap() call.
  bool was_earliest = false;
  CommitTransactionUnlocked(timestamp, &was_earliest);

  if (was_earliest
      && no_new_transactions_at_or_before_.CompareTo(timestamp) >= 0) {
    // If this transaction was the earliest in-flight, we might have to adjust
    // the "clean" timestamp.
    AdjustCleanTime();
  }
}

MvccManager::TxnState MvccManager::RemoveInFlightAndGetStateUnlocked(Timestamp ts) {
  DCHECK(lock_.is_locked());

  auto it = timestamps_in_flight_.find(ts.value());
  if (it == timestamps_in_flight_.end()) {
    LOG(FATAL) << "Trying to remove timestamp which isn't in the in-flight set: "
               << ts.ToString();
  }
  TxnState state = it->second;
  timestamps_in_flight_.erase(it);
  return state;
}

void MvccManager::CommitTransactionUnlocked(Timestamp timestamp,
                                            bool* was_earliest_in_flight) {
  DCHECK(clock_->IsAfter(timestamp))
    << "Trying to commit a transaction with a future timestamp: "
    << timestamp.ToString() << ". Current time: " << clock_->Stringify(clock_->Now());

  *was_earliest_in_flight = earliest_in_flight_ == timestamp;

  // Remove from our in-flight list.
  TxnState old_state = RemoveInFlightAndGetStateUnlocked(timestamp);
  CHECK_EQ(old_state, APPLYING)
    << "Trying to commit a transaction which never entered APPLYING state: "
    << timestamp.ToString() << " state=" << old_state;

  // Add to snapshot's committed list
  cur_snap_.AddCommittedTimestamp(timestamp);

  // If we're committing the earliest transaction that was in flight,
  // update our cached value.
  if (*was_earliest_in_flight) {
    AdvanceEarliestInFlightTimestamp();
  }
}

void MvccManager::AdvanceEarliestInFlightTimestamp() {
  if (timestamps_in_flight_.empty()) {
    earliest_in_flight_ = Timestamp::kMax;
  } else {
    earliest_in_flight_ = Timestamp(std::min_element(timestamps_in_flight_.begin(),
                                                     timestamps_in_flight_.end())->first);
  }
}

void MvccManager::OfflineAdjustSafeTime(Timestamp safe_time) {
  boost::lock_guard<LockType> l(lock_);

  // No more transactions will start with a ts that is lower than or equal
  // to 'safe_time', so we adjust the snapshot accordingly.
  if (no_new_transactions_at_or_before_.CompareTo(safe_time) < 0) {
    no_new_transactions_at_or_before_ = safe_time;
  }

  AdjustCleanTime();
}

// Remove any elements from 'v' which are < the given watermark.
static void FilterTimestamps(std::vector<Timestamp::val_type>* v,
                             Timestamp::val_type watermark) {
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
  //    In this case, we update the watermark to that transaction's timestamp.
  //
  // 2) There are no in-flight transactions earlier than 'no_new_transactions_at_or_before_'.
  //    (There may still be in-flight transactions with future timestamps due to
  //    commit-wait transactions which start in the future). In this case, we update
  //    the watermark to 'no_new_transactions_at_or_before_', since we know that no new
  //    transactions can start with an earlier timestamp.
  //
  // In either case, we have to add the newly committed ts only if it remains higher
  // than the new watermark.

  if (earliest_in_flight_.CompareTo(no_new_transactions_at_or_before_) < 0) {
    cur_snap_.all_committed_before_ = earliest_in_flight_;
  } else {
    cur_snap_.all_committed_before_ = no_new_transactions_at_or_before_;
  }

  // Filter out any committed timestamps that now fall below the watermark
  FilterTimestamps(&cur_snap_.committed_timestamps_, cur_snap_.all_committed_before_.value());

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

Status MvccManager::WaitUntil(WaitFor wait_for, Timestamp ts,
                              const MonoTime& deadline) const {
  TRACE_EVENT2("tablet", "MvccManager::WaitUntil",
               "wait_for", wait_for == ALL_COMMITTED ? "all_committed" : "none_applying",
               "ts", ts.ToUint64())

  CountDownLatch latch(1);
  WaitingState waiting_state;
  {
    waiting_state.timestamp = ts;
    waiting_state.latch = &latch;
    waiting_state.wait_for = wait_for;

    boost::lock_guard<LockType> l(lock_);
    if (IsDoneWaitingUnlocked(waiting_state)) return Status::OK();
    waiters_.push_back(&waiting_state);
  }
  if (waiting_state.latch->WaitUntil(deadline)) {
    return Status::OK();
  }
  // We timed out. We need to clean up our entry in the waiters_ array.

  boost::lock_guard<LockType> l(lock_);
  // It's possible that while we were re-acquiring the lock, we did get
  // notified. In that case, we have no cleanup to do.
  if (waiting_state.latch->count() == 0) {
    return Status::OK();
  }

  waiters_.erase(std::find(waiters_.begin(), waiters_.end(), &waiting_state));
  return Status::TimedOut(strings::Substitute(
      "Timed out waiting for all transactions with ts < $0 to $1",
      clock_->Stringify(ts),
      wait_for == ALL_COMMITTED ? "commit" : "finish applying"));
}

bool MvccManager::IsDoneWaitingUnlocked(const WaitingState& waiter) const {
  switch (waiter.wait_for) {
    case ALL_COMMITTED:
      return AreAllTransactionsCommittedUnlocked(waiter.timestamp);
    case NONE_APPLYING:
      return !AnyApplyingAtOrBeforeUnlocked(waiter.timestamp);
  }
  LOG(FATAL); // unreachable
}

bool MvccManager::AreAllTransactionsCommittedUnlocked(Timestamp ts) const {
  if (timestamps_in_flight_.empty()) {
    // If nothing is in-flight, then check the clock. If the timestamp is in the past,
    // we know that no new uncommitted transactions may start before this ts.
    return ts.CompareTo(clock_->Now()) <= 0;
  }
  // If some transactions are in flight, then check the in-flight list.
  return !cur_snap_.MayHaveUncommittedTransactionsAtOrBefore(ts);
}

bool MvccManager::AnyApplyingAtOrBeforeUnlocked(Timestamp ts) const {
  for (const InFlightMap::value_type entry : timestamps_in_flight_) {
    if (entry.first <= ts.value()) {
      return true;
    }
  }
  return false;
}

void MvccManager::TakeSnapshot(MvccSnapshot *snap) const {
  boost::lock_guard<LockType> l(lock_);
  *snap = cur_snap_;
}

Status MvccManager::WaitForCleanSnapshotAtTimestamp(Timestamp timestamp,
                                                    MvccSnapshot *snap,
                                                    const MonoTime& deadline) const {
  TRACE_EVENT0("tablet", "MvccManager::WaitForCleanSnapshotAtTimestamp");
  RETURN_NOT_OK(clock_->WaitUntilAfterLocally(timestamp, deadline));
  RETURN_NOT_OK(WaitUntil(ALL_COMMITTED, timestamp, deadline));
  *snap = MvccSnapshot(timestamp);
  return Status::OK();
}

void MvccManager::WaitForCleanSnapshot(MvccSnapshot* snap) const {
  CHECK_OK(WaitForCleanSnapshotAtTimestamp(clock_->Now(), snap, MonoTime::Max()));
}

void MvccManager::WaitForApplyingTransactionsToCommit() const {
  TRACE_EVENT0("tablet", "MvccManager::WaitForApplyingTransactionsToCommit");

  // Find the highest timestamp of an APPLYING transaction.
  Timestamp wait_for = Timestamp::kMin;
  {
    boost::lock_guard<LockType> l(lock_);
    for (const InFlightMap::value_type entry : timestamps_in_flight_) {
      if (entry.second == APPLYING) {
        wait_for = Timestamp(std::max(entry.first, wait_for.value()));
      }
    }
  }

  // Wait until there are no transactions applying with that timestamp
  // or below. It's possible that we're a bit conservative here - more transactions
  // may enter the APPLYING set while we're waiting, but we will eventually
  // succeed.
  if (wait_for == Timestamp::kMin) {
    // None were APPLYING: we can just return.
    return;
  }
  CHECK_OK(WaitUntil(NONE_APPLYING, wait_for, MonoTime::Max()));
}

bool MvccManager::AreAllTransactionsCommitted(Timestamp ts) const {
  boost::lock_guard<LockType> l(lock_);
  return AreAllTransactionsCommittedUnlocked(ts);
}

int MvccManager::CountTransactionsInFlight() const {
  boost::lock_guard<LockType> l(lock_);
  return timestamps_in_flight_.size();
}

Timestamp MvccManager::GetCleanTimestamp() const {
  boost::lock_guard<LockType> l(lock_);
  return cur_snap_.all_committed_before_;
}

void MvccManager::GetApplyingTransactionsTimestamps(std::vector<Timestamp>* timestamps) const {
  boost::lock_guard<LockType> l(lock_);
  timestamps->reserve(timestamps_in_flight_.size());
  for (const InFlightMap::value_type entry : timestamps_in_flight_) {
    if (entry.second == APPLYING) {
      timestamps->push_back(Timestamp(entry.first));
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
  : all_committed_before_(Timestamp::kInitialTimestamp),
    none_committed_at_or_after_(Timestamp::kInitialTimestamp) {
}

MvccSnapshot::MvccSnapshot(const MvccManager &manager) {
  manager.TakeSnapshot(this);
}

MvccSnapshot::MvccSnapshot(const Timestamp& timestamp)
  : all_committed_before_(timestamp),
    none_committed_at_or_after_(timestamp) {
 }

MvccSnapshot MvccSnapshot::CreateSnapshotIncludingAllTransactions() {
  return MvccSnapshot(Timestamp::kMax);
}

MvccSnapshot MvccSnapshot::CreateSnapshotIncludingNoTransactions() {
  return MvccSnapshot(Timestamp::kMin);
}

bool MvccSnapshot::IsCommittedFallback(const Timestamp& timestamp) const {
  for (const Timestamp::val_type& v : committed_timestamps_) {
    if (v == timestamp.value()) return true;
  }

  return false;
}

bool MvccSnapshot::MayHaveCommittedTransactionsAtOrAfter(const Timestamp& timestamp) const {
  return timestamp.CompareTo(none_committed_at_or_after_) < 0;
}

bool MvccSnapshot::MayHaveUncommittedTransactionsAtOrBefore(const Timestamp& timestamp) const {
  // The snapshot may have uncommitted transactions before 'timestamp' if:
  // - 'all_committed_before_' comes before 'timestamp'
  // - 'all_committed_before_' is precisely 'timestamp' but 'timestamp' isn't in the
  //   committed set.
  return timestamp.CompareTo(all_committed_before_) > 0 ||
      (timestamp.CompareTo(all_committed_before_) == 0 && !IsCommittedFallback(timestamp));
}

std::string MvccSnapshot::ToString() const {
  string ret("MvccSnapshot[committed={T|");

  if (committed_timestamps_.size() == 0) {
    StrAppend(&ret, "T < ", all_committed_before_.ToString(),"}]");
    return ret;
  }
  StrAppend(&ret, "T < ", all_committed_before_.ToString(),
            " or (T in {");

  bool first = true;
  for (Timestamp::val_type t : committed_timestamps_) {
    if (!first) {
      ret.push_back(',');
    }
    first = false;
    StrAppend(&ret, t);
  }
  ret.append("})}]");
  return ret;
}

void MvccSnapshot::AddCommittedTimestamps(const std::vector<Timestamp>& timestamps) {
  for (const Timestamp& ts : timestamps) {
    AddCommittedTimestamp(ts);
  }
}

void MvccSnapshot::AddCommittedTimestamp(Timestamp timestamp) {
  if (IsCommitted(timestamp)) return;

  committed_timestamps_.push_back(timestamp.value());

  // If this is a new upper bound commit mark, update it.
  if (none_committed_at_or_after_.CompareTo(timestamp) <= 0) {
    none_committed_at_or_after_ = Timestamp(timestamp.value() + 1);
  }
}

////////////////////////////////////////////////////////////
// ScopedTransaction
////////////////////////////////////////////////////////////
ScopedTransaction::ScopedTransaction(MvccManager *mgr, TimestampAssignmentType assignment_type)
  : done_(false),
    manager_(DCHECK_NOTNULL(mgr)),
    assignment_type_(assignment_type) {

  switch (assignment_type_) {
    case NOW: {
      timestamp_ = mgr->StartTransaction();
      break;
    }
    case NOW_LATEST: {
      timestamp_ = mgr->StartTransactionAtLatest();
      break;
    }
    default: {
      LOG(FATAL) << "Illegal TransactionAssignmentType. Only NOW and NOW_LATEST are supported"
          " by this ctor.";
    }
  }
}

ScopedTransaction::ScopedTransaction(MvccManager *mgr, Timestamp timestamp)
  : done_(false),
    manager_(DCHECK_NOTNULL(mgr)),
    assignment_type_(PRE_ASSIGNED),
    timestamp_(timestamp) {
  CHECK_OK(mgr->StartTransactionAtTimestamp(timestamp));
}

ScopedTransaction::~ScopedTransaction() {
  if (!done_) {
    Abort();
  }
}

void ScopedTransaction::StartApplying() {
  manager_->StartApplyingTransaction(timestamp_);
}

void ScopedTransaction::Commit() {
  switch (assignment_type_) {
    case NOW:
    case NOW_LATEST: {
      manager_->CommitTransaction(timestamp_);
      break;
    }
    case PRE_ASSIGNED: {
      manager_->OfflineCommitTransaction(timestamp_);
      break;
    }
    default: {
      LOG(FATAL) << "Unexpected transaction assignment type.";
    }
  }

  done_ = true;
}

void ScopedTransaction::Abort() {
  manager_->AbortTransaction(timestamp_);
  done_ = true;
}


} // namespace tablet
} // namespace kudu
