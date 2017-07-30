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

#include "yb/tablet/transactions/transaction_driver.h"

#include <mutex>

#include "yb/client/client.h"
#include "yb/consensus/consensus.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transactions/transaction_tracker.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/logging.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using std::shared_ptr;

using consensus::CommitMsg;
using consensus::Consensus;
using consensus::ConsensusRound;
using consensus::ReplicateMsg;
using consensus::DriverType;
using log::Log;
using server::Clock;

static const char* kHybridTimeFieldName = "hybrid_time";


////////////////////////////////////////////////////////////
// TransactionDriver
////////////////////////////////////////////////////////////

TransactionDriver::TransactionDriver(TransactionTracker *txn_tracker,
                                     Consensus* consensus,
                                     Log* log,
                                     PrepareThread* prepare_thread,
                                     ThreadPool* apply_pool,
                                     TransactionOrderVerifier* order_verifier,
                                     TableType table_type)
    : txn_tracker_(txn_tracker),
      consensus_(consensus),
      log_(log),
      prepare_thread_(prepare_thread),
      apply_pool_(apply_pool),
      order_verifier_(order_verifier),
      trace_(new Trace()),
      start_time_(MonoTime::Now(MonoTime::FINE)),
      replication_state_(NOT_REPLICATING),
      prepare_state_(NOT_PREPARED),
      table_type_(table_type) {
  if (Trace::CurrentTrace()) {
    Trace::CurrentTrace()->AddChildTrace(trace_.get());
  }
}

Status TransactionDriver::Init(gscoped_ptr<Transaction> transaction,
                               DriverType type) {
  transaction_ = transaction.Pass();

  if (type == consensus::REPLICA) {
    std::lock_guard<simple_spinlock> lock(opid_lock_);
    op_id_copy_ = transaction_->state()->op_id();
    DCHECK(op_id_copy_.IsInitialized());
    replication_state_ = REPLICATING;
  } else {
    DCHECK_EQ(type, consensus::LEADER);
    if (consensus_) {  // sometimes NULL in tests
      // Unretained is required to avoid a refcount cycle.
      consensus::ReplicateMsgPtr replicate_msg = transaction_->NewReplicateMsg();
      mutable_state()->set_consensus_round(
        consensus_->NewRound(std::move(replicate_msg),
                             Bind(&TransactionDriver::ReplicationFinished, Unretained(this))));
      if (table_type_ != TableType::KUDU_COLUMNAR_TABLE_TYPE) {
        mutable_state()->consensus_round()->SetAppendCallback(this);
      }
    }
  }

  RETURN_NOT_OK(txn_tracker_->Add(this));

  return Status::OK();
}

consensus::OpId TransactionDriver::GetOpId() {
  std::lock_guard<simple_spinlock> lock(opid_lock_);
  return op_id_copy_;
}

const TransactionState* TransactionDriver::state() const {
  return transaction_ != nullptr ? transaction_->state() : nullptr;
}

TransactionState* TransactionDriver::mutable_state() {
  return transaction_ != nullptr ? transaction_->state() : nullptr;
}

Transaction::TransactionType TransactionDriver::tx_type() const {
  return transaction_->tx_type();
}

string TransactionDriver::ToString() const {
  std::lock_guard<simple_spinlock> lock(lock_);
  return ToStringUnlocked();
}

string TransactionDriver::ToStringUnlocked() const {
  string ret = StateString(replication_state_, prepare_state_);
  if (transaction_ != nullptr) {
    ret += " " + transaction_->ToString();
  } else {
    ret += "[unknown txn]";
  }
  return ret;
}


Status TransactionDriver::ExecuteAsync() {
  VLOG_WITH_PREFIX(4) << "ExecuteAsync()";
  TRACE_EVENT_FLOW_BEGIN0("txn", "ExecuteAsync", this);
  ADOPT_TRACE(trace());

  Status s;
  if (replication_state_ == NOT_REPLICATING) {
    // We're a leader transaction. Before submitting, check that we are the leader and
    // determine the current term.
    s = consensus_->CheckLeadershipAndBindTerm(mutable_state()->consensus_round());
  }

  if (s.ok()) {
    s = prepare_thread_->Submit(this);
  }

  if (!s.ok()) {
    HandleFailure(s);
  }

  // TODO: make this return void
  return Status::OK();
}

void TransactionDriver::HandleConsensusAppend() {
  // YB tables only.
  CHECK_NE(table_type_, TableType::KUDU_COLUMNAR_TABLE_TYPE);
  transaction_->Start();
  auto* const replicate_msg = transaction_->state()->consensus_round()->replicate_msg().get();
  CHECK(!replicate_msg->has_hybrid_time());
  replicate_msg->set_hybrid_time(transaction_->state()->hybrid_time().ToUint64());
  replicate_msg->set_monotonic_counter(
      *transaction_->state()->tablet_peer()->tablet()->monotonic_counter());
}

void TransactionDriver::PrepareAndStartTask() {
  TRACE_EVENT_FLOW_END0("txn", "PrepareAndStartTask", this);
  Status prepare_status = PrepareAndStart();
  if (PREDICT_FALSE(!prepare_status.ok())) {
    HandleFailure(prepare_status);
  }
}

Status TransactionDriver::PrepareAndStart() {
  TRACE_EVENT1("txn", "PrepareAndStart", "txn", this);
  VLOG_WITH_PREFIX(4) << "PrepareAndStart()";
  // Actually prepare and start the transaction.
  prepare_physical_hybrid_time_ = GetMonoTimeMicros();
  RETURN_NOT_OK(transaction_->Prepare());

  // Only take the lock long enough to take a local copy of the
  // replication state and set our prepare state. This ensures that
  // exactly one of Replicate/Prepare callbacks will trigger the apply
  // phase.
  ReplicationState repl_state_copy;
  {
    std::lock_guard<simple_spinlock> lock(lock_);
    CHECK_EQ(prepare_state_, NOT_PREPARED);
    repl_state_copy = replication_state_;
  }

  if (table_type_ == TableType::KUDU_COLUMNAR_TABLE_TYPE ||
      repl_state_copy != NOT_REPLICATING) {
    // For Kudu tables and for non-leader codepath in YB tables, we want to call Start() as soon
    // as possible, because the transaction already has the hybrid_time assigned. This will get a
    // bit simpler as Kudu tables go away.
    transaction_->Start();
  }

  {
    std::lock_guard<simple_spinlock> lock(lock_);
    // No one should have modified prepare_state_ since we've read it under the lock a few lines
    // above, because PrepareAndStart should only run once per transaction.
    CHECK_EQ(prepare_state_, NOT_PREPARED);
    // After this update, the ReplicationFinished callback will be able to apply this transaction.
    // We can only do this after we've called Start()
    prepare_state_ = PREPARED;

    // On the replica (non-leader) side, the replication state might have been REPLICATING during
    // our previous acquisition of this lock, but it might have changed to REPLICATED in the
    // meantime. That would mean ReplicationFinished got called, but ReplicationFinished would not
    // trigger Apply unless the transaction is PREPARED, so we are responsible for doing that.
    // If we fail to capture the new replication state here, the transaction will never be applied.
    repl_state_copy = replication_state_;
  }

  switch (repl_state_copy) {
    case NOT_REPLICATING:
    {
      ReplicateMsg* replicate_msg = transaction_->state()->consensus_round()->replicate_msg().get();

      if (table_type_ == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
        // Kudu tables only: set the hybrid_time in the message, now that it's prepared.
        replicate_msg->set_hybrid_time(transaction_->state()->hybrid_time().ToUint64());

        // For YB tables, we set the hybrid_time at the same time as we append the entry to the
        // consensus queue, to guarantee that hybrid_times increase monotonically with Raft indexes.
      }

      {
        std::lock_guard<simple_spinlock> lock(lock_);
        replication_state_ = REPLICATING;
      }

      // After the batching changes from 07/2017, It is the caller's responsibility to call
      // Consensus::Replicate. See PrepareThread for details.
      return Status::OK();
    }
    case REPLICATING:
    {
      // Already replicating - nothing to trigger
      return Status::OK();
    }
    case REPLICATION_FAILED:
      DCHECK(!transaction_status_.ok());
      FALLTHROUGH_INTENDED;
    case REPLICATED:
    {
      // We can move on to apply.
      // Note that ApplyAsync() will handle the error status in the
      // REPLICATION_FAILED case.
      return ApplyAsync();
    }
  }
  FATAL_INVALID_ENUM_VALUE(ReplicationState, repl_state_copy);
}

void TransactionDriver::SetReplicationFailed(const Status& replication_status) {
  std::lock_guard<simple_spinlock> lock(lock_);
  CHECK_EQ(replication_state_, REPLICATING);
  transaction_status_ = replication_status;
  replication_state_ = REPLICATION_FAILED;
}

void TransactionDriver::HandleFailure(const Status& s) {
  VLOG_WITH_PREFIX(2) << "Failed transaction: " << s.ToString();
  CHECK(!s.ok());
  TRACE("HandleFailure($0)", s.ToString());

  ReplicationState repl_state_copy;

  {
    std::lock_guard<simple_spinlock> lock(lock_);
    transaction_status_ = s;
    repl_state_copy = replication_state_;
  }


  switch (repl_state_copy) {
    case NOT_REPLICATING:
    case REPLICATION_FAILED:
    {
      VLOG_WITH_PREFIX(1) << "Transaction " << ToString() << " failed prior to "
          "replication success: " << s.ToString();
      transaction_->Finish(Transaction::ABORTED);
      mutable_state()->completion_callback()->set_error(transaction_status_);
      mutable_state()->completion_callback()->TransactionCompleted();
      txn_tracker_->Release(this);
      return;
    }

    case REPLICATING:
    case REPLICATED:
    {
      LOG_WITH_PREFIX(FATAL) << "Cannot cancel transactions that have already replicated"
          << ": " << transaction_status_.ToString()
          << " transaction:" << ToString();
    }
  }
}

void TransactionDriver::ReplicationFinished(const Status& status) {
  consensus::OpId op_id_local;
  {
    std::lock_guard<simple_spinlock> op_id_lock(opid_lock_);
    // TODO: it's a bit silly that we have three copies of the opid:
    // one here, one in ConsensusRound, and one in TransactionState.

    op_id_copy_ = DCHECK_NOTNULL(mutable_state()->consensus_round())->id();
    DCHECK(op_id_copy_.IsInitialized());
    // We can't update mutable_state()->mutable_op_id() here, because it is guarded by a different
    // lock. Instead, we save it in a local variable and write it to the other location when
    // holding the other lock.
    op_id_local = op_id_copy_;
  }

  PrepareState prepare_state_copy;
  {
    std::lock_guard<simple_spinlock> lock(lock_);
    mutable_state()->mutable_op_id()->CopyFrom(op_id_local);
    CHECK_EQ(replication_state_, REPLICATING);
    if (status.ok()) {
      replication_state_ = REPLICATED;
    } else {
      replication_state_ = REPLICATION_FAILED;
      transaction_status_ = status;
    }
    prepare_state_copy = prepare_state_;
  }

  // If we have prepared and replicated, we're ready to move ahead and apply this operation.
  // Note that if we set the state to REPLICATION_FAILED above, ApplyAsync() will actually abort the
  // transaction, i.e. ApplyTask() will never be called and the transaction will never be applied to
  // the tablet.
  if (prepare_state_copy == PREPARED) {
    // We likely need to do cleanup if this fails so for now just
    // CHECK_OK
    CHECK_OK(ApplyAsync());
  }
}

void TransactionDriver::Abort(const Status& status) {
  CHECK(!status.ok());

  ReplicationState repl_state_copy;
  {
    std::lock_guard<simple_spinlock> lock(lock_);
    repl_state_copy = replication_state_;
    transaction_status_ = status;
  }

  // If the state is not NOT_REPLICATING we abort immediately and the transaction
  // will never be replicated.
  // In any other state we just set the transaction status, if the transaction's
  // Apply hasn't started yet this prevents it from starting, but if it has then
  // the transaction runs to completion.
  if (repl_state_copy == NOT_REPLICATING) {
    HandleFailure(status);
  }
}

Status TransactionDriver::ApplyAsync() {
  {
    std::unique_lock<simple_spinlock> lock(lock_);
    DCHECK_EQ(prepare_state_, PREPARED);
    if (transaction_status_.ok()) {
      DCHECK_EQ(replication_state_, REPLICATED);
      order_verifier_->CheckApply(op_id_copy_.index(),
                                  prepare_physical_hybrid_time_);
      // Now that the transaction is committed in consensus advance the safe time.
      if (transaction_->state()->external_consistency_mode() != COMMIT_WAIT) {
        transaction_->state()->tablet_peer()->tablet()->mvcc_manager()->
            OfflineAdjustSafeTime(transaction_->state()->hybrid_time());
      }
    } else {
      DCHECK_EQ(replication_state_, REPLICATION_FAILED);
      DCHECK(!transaction_status_.ok());
      lock.unlock();
      HandleFailure(transaction_status_);
      return Status::OK();
    }
  }

  TRACE_EVENT_FLOW_BEGIN0("txn", "ApplyTask", this);
  switch (table_type_) {
    case TableType::YQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
    case TableType::REDIS_TABLE_TYPE:
      // Key-value tables backed by RocksDB require that we apply changes synchronously to enforce
      // the order.
      ApplyTask();
      return Status::OK();
    case TableType::KUDU_COLUMNAR_TABLE_TYPE:
      return apply_pool_->SubmitClosure(Bind(&TransactionDriver::ApplyTask, Unretained(this)));
  }
  LOG(FATAL) << "Invalid table type: " << table_type_;
  return STATUS_FORMAT(IllegalState, "Invalid table type: $0", table_type_);
}

void TransactionDriver::ApplyTask() {
  TRACE_EVENT_FLOW_END0("txn", "ApplyTask", this);
  ADOPT_TRACE(trace());

#ifndef NDEBUG
  {
    std::lock_guard<simple_spinlock> lock(lock_);
    DCHECK_EQ(replication_state_, REPLICATED);
    DCHECK_EQ(prepare_state_, PREPARED);
  }
#endif

  // We need to ref-count ourself, since Commit() may run very quickly
  // and end up calling Finalize() while we're still in this code.
  scoped_refptr<TransactionDriver> ref(this);

  {
    gscoped_ptr<CommitMsg> commit_msg;
    CHECK_OK(transaction_->Apply(&commit_msg));
    if (commit_msg) {
      commit_msg->mutable_commited_op_id()->CopyFrom(op_id_copy_);
    }
    SetResponseHybridTime(transaction_->state(), transaction_->state()->hybrid_time());

    // If the client requested COMMIT_WAIT as the external consistency mode
    // calculate the latest that the prepare hybrid_time could be and wait
    // until now.earliest > prepare_latest. Only after this are the locks
    // released.
    if (mutable_state()->external_consistency_mode() == COMMIT_WAIT) {
      // TODO: only do this on the leader side
      TRACE("APPLY: Commit Wait.");
      // If we can't commit wait and have already applied we might have consistency
      // issues if we still reply to the client that the operation was a success.
      // On the other hand we don't have rollbacks as of yet thus we can't undo the
      // the apply either, so we just CHECK_OK for now.
      CHECK_OK(CommitWait());
    }

    transaction_->PreCommit();

    // We only write the "commit" records to the local log for legacy Kudu tables. We are not
    // writing these records for RocksDB-based tables.
    if (table_type_ == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
      TRACE_EVENT1("txn", "AsyncAppendCommit", "txn", this);
      CHECK_OK(log_->AsyncAppendCommit(commit_msg.Pass(), Bind(DoNothingStatusCB)));
    }

    Finalize();
  }
}

void TransactionDriver::SetResponseHybridTime(TransactionState* transaction_state,
                                             const HybridTime& hybrid_time) {
  google::protobuf::Message* response = transaction_state->response();
  if (response) {
    const google::protobuf::FieldDescriptor* ts_field =
        response->GetDescriptor()->FindFieldByName(kHybridTimeFieldName);
    response->GetReflection()->SetUInt64(response, ts_field, hybrid_time.ToUint64());
  }
}

Status TransactionDriver::CommitWait() {
  MonoTime before = MonoTime::Now(MonoTime::FINE);
  DCHECK(mutable_state()->external_consistency_mode() == COMMIT_WAIT);
  // TODO: we could plumb the RPC deadline in here, and not bother commit-waiting
  // if the deadline is already expired.
  RETURN_NOT_OK(
      mutable_state()->tablet_peer()->clock()->WaitUntilAfter(mutable_state()->hybrid_time(),
                                                              MonoTime::Max()));
  mutable_state()->mutable_metrics()->commit_wait_duration_usec =
      MonoTime::Now(MonoTime::FINE).GetDeltaSince(before).ToMicroseconds();
  return Status::OK();
}

void TransactionDriver::Finalize() {
  ADOPT_TRACE(trace());
  // TODO: this is an ugly hack so that the Release() call doesn't delete the
  // object while we still hold the lock.
  scoped_refptr<TransactionDriver> ref(this);
  std::lock_guard<simple_spinlock> lock(lock_);
  transaction_->Finish(Transaction::COMMITTED);
  mutable_state()->completion_callback()->TransactionCompleted();
  txn_tracker_->Release(this);
}


std::string TransactionDriver::StateString(ReplicationState repl_state,
                                           PrepareState prep_state) {
  string state_str;
  switch (repl_state) {
    case NOT_REPLICATING:
      StrAppend(&state_str, "NR-");  // For Not Replicating
      break;
    case REPLICATING:
      StrAppend(&state_str, "R-");  // For Replicating
      break;
    case REPLICATION_FAILED:
      StrAppend(&state_str, "RF-");  // For Replication Failed
      break;
    case REPLICATED:
      StrAppend(&state_str, "RD-");  // For Replication Done
      break;
    default:
      LOG(DFATAL) << "Unexpected replication state: " << repl_state;
  }
  switch (prep_state) {
    case PREPARED:
      StrAppend(&state_str, "P");
      break;
    case NOT_PREPARED:
      StrAppend(&state_str, "NP");
      break;
    default:
      LOG(DFATAL) << "Unexpected prepare state: " << prep_state;
  }
  return state_str;
}

std::string TransactionDriver::LogPrefix() const {

  ReplicationState repl_state_copy;
  PrepareState prep_state_copy;
  string ts_string;

  {
    std::lock_guard<simple_spinlock> lock(lock_);
    repl_state_copy = replication_state_;
    prep_state_copy = prepare_state_;
    ts_string = state()->has_hybrid_time() ? state()->hybrid_time().ToString() : "No hybrid_time";
  }

  string state_str = StateString(repl_state_copy, prep_state_copy);
  // We use the tablet and the peer (T, P) to identify ts and tablet and the hybrid_time (Ts) to
  // (help) identify the transaction. The state string (S) describes the state of the transaction.
  return strings::Substitute("T $0 P $1 S $2 Ts $3: ",
                             // consensus_ is NULL in some unit tests.
                             PREDICT_TRUE(consensus_) ? consensus_->tablet_id() : "(unknown)",
                             PREDICT_TRUE(consensus_) ? consensus_->peer_uuid() : "(unknown)",
                             state_str,
                             ts_string);
}

}  // namespace tablet
}  // namespace yb
