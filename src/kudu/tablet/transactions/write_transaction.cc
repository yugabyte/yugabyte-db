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

#include "kudu/tablet/transactions/write_transaction.h"

#include <algorithm>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/common/row_operations.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/walltime.h"
#include "kudu/rpc/rpc_context.h"
#include "kudu/server/hybrid_clock.h"
#include "kudu/tablet/row_op.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tablet/tablet_metrics.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/trace.h"

DEFINE_int32(tablet_inject_latency_on_apply_write_txn_ms, 0,
             "How much latency to inject when a write transaction is applied. "
             "For testing only!");
TAG_FLAG(tablet_inject_latency_on_apply_write_txn_ms, unsafe);
TAG_FLAG(tablet_inject_latency_on_apply_write_txn_ms, runtime);

namespace kudu {
namespace tablet {

using boost::bind;
using consensus::ReplicateMsg;
using consensus::CommitMsg;
using consensus::DriverType;
using consensus::WRITE_OP;
using tserver::TabletServerErrorPB;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;
using strings::Substitute;

WriteTransaction::WriteTransaction(WriteTransactionState* state, DriverType type)
  : Transaction(state, type, Transaction::WRITE_TXN),
  state_(state) {
  start_time_ = MonoTime::Now(MonoTime::FINE);
}

void WriteTransaction::NewReplicateMsg(gscoped_ptr<ReplicateMsg>* replicate_msg) {
  replicate_msg->reset(new ReplicateMsg);
  (*replicate_msg)->set_op_type(WRITE_OP);
  (*replicate_msg)->mutable_write_request()->CopyFrom(*state()->request());
}

Status WriteTransaction::Prepare() {
  TRACE_EVENT0("txn", "WriteTransaction::Prepare");
  TRACE("PREPARE: Starting");
  // Decode everything first so that we give up if something major is wrong.
  Schema client_schema;
  RETURN_NOT_OK_PREPEND(SchemaFromPB(state_->request()->schema(), &client_schema),
                        "Cannot decode client schema");
  if (client_schema.has_column_ids()) {
    // TODO: we have this kind of code a lot - add a new SchemaFromPB variant which
    // does this check inline.
    Status s = Status::InvalidArgument("User requests should not have Column IDs");
    state_->completion_callback()->set_error(s, TabletServerErrorPB::INVALID_SCHEMA);
    return s;
  }

  Tablet* tablet = state()->tablet_peer()->tablet();

  Status s = tablet->DecodeWriteOperations(&client_schema, state());
  if (!s.ok()) {
    // TODO: is MISMATCHED_SCHEMA always right here? probably not.
    state()->completion_callback()->set_error(s, TabletServerErrorPB::MISMATCHED_SCHEMA);
    return s;
  }

  // Now acquire row locks and prepare everything for apply
  RETURN_NOT_OK(tablet->AcquireRowLocks(state()));

  TRACE("PREPARE: finished.");
  return Status::OK();
}

Status WriteTransaction::Start() {
  TRACE_EVENT0("txn", "WriteTransaction::Start");
  TRACE("Start()");
  state_->tablet_peer()->tablet()->StartTransaction(state_.get());
  TRACE("Timestamp: $0", state_->tablet_peer()->clock()->Stringify(state_->timestamp()));
  return Status::OK();
}

// FIXME: Since this is called as a void in a thread-pool callback,
// it seems pointless to return a Status!
Status WriteTransaction::Apply(gscoped_ptr<CommitMsg>* commit_msg) {
  TRACE_EVENT0("txn", "WriteTransaction::Apply");
  TRACE("APPLY: Starting");

  if (PREDICT_FALSE(
          ANNOTATE_UNPROTECTED_READ(FLAGS_tablet_inject_latency_on_apply_write_txn_ms) > 0)) {
    TRACE("Injecting $0ms of latency due to --tablet_inject_latency_on_apply_write_txn_ms",
          FLAGS_tablet_inject_latency_on_apply_write_txn_ms);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_tablet_inject_latency_on_apply_write_txn_ms));
  }

  Tablet* tablet = state()->tablet_peer()->tablet();

  tablet->ApplyRowOperations(state());

  // Add per-row errors to the result, update metrics.
  int i = 0;
  for (const RowOp* op : state()->row_ops()) {
    if (state()->response() != nullptr && op->result->has_failed_status()) {
      // Replicas disregard the per row errors, for now
      // TODO check the per-row errors against the leader's, at least in debug mode
      WriteResponsePB::PerRowErrorPB* error = state()->response()->add_per_row_errors();
      error->set_row_index(i);
      error->mutable_error()->CopyFrom(op->result->failed_status());
    }

    state()->UpdateMetricsForOp(*op);
    i++;
  }

  // Create the Commit message
  commit_msg->reset(new CommitMsg());
  state()->ReleaseTxResultPB((*commit_msg)->mutable_result());
  (*commit_msg)->set_op_type(WRITE_OP);

  return Status::OK();
}

void WriteTransaction::PreCommit() {
  TRACE_EVENT0("txn", "WriteTransaction::PreCommit");
  TRACE("PRECOMMIT: Releasing row and schema locks");
  // Perform early lock release after we've applied all changes
  state()->release_row_locks();
  state()->ReleaseSchemaLock();
}

void WriteTransaction::Finish(TransactionResult result) {
  TRACE_EVENT0("txn", "WriteTransaction::Finish");
  if (PREDICT_FALSE(result == Transaction::ABORTED)) {
    TRACE("FINISH: aborting transaction");
    state()->Abort();
    return;
  }

  DCHECK_EQ(result, Transaction::COMMITTED);
  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("FINISH: making edits visible");
  state()->Commit();

  TabletMetrics* metrics = state_->tablet_peer()->tablet()->metrics();
  if (metrics) {
    // TODO: should we change this so it's actually incremented by the
    // Tablet code itself instead of this wrapper code?
    metrics->rows_inserted->IncrementBy(state_->metrics().successful_inserts);
    metrics->rows_updated->IncrementBy(state_->metrics().successful_updates);
    metrics->rows_deleted->IncrementBy(state_->metrics().successful_deletes);

    if (type() == consensus::LEADER) {
      if (state()->external_consistency_mode() == COMMIT_WAIT) {
        metrics->commit_wait_duration->Increment(state_->metrics().commit_wait_duration_usec);
      }
      uint64_t op_duration_usec =
          MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_time_).ToMicroseconds();
      switch (state()->external_consistency_mode()) {
        case CLIENT_PROPAGATED:
          metrics->write_op_duration_client_propagated_consistency->Increment(op_duration_usec);
          break;
        case COMMIT_WAIT:
          metrics->write_op_duration_commit_wait_consistency->Increment(op_duration_usec);
          break;
        case UNKNOWN_EXTERNAL_CONSISTENCY_MODE:
          break;
      }
    }
  }
}

string WriteTransaction::ToString() const {
  MonoTime now(MonoTime::Now(MonoTime::FINE));
  MonoDelta d = now.GetDeltaSince(start_time_);
  WallTime abs_time = WallTime_Now() - d.ToSeconds();
  string abs_time_formatted;
  StringAppendStrftime(&abs_time_formatted, "%Y-%m-%d %H:%M:%S", (time_t)abs_time, true);
  return Substitute("WriteTransaction [type=$0, start_time=$1, state=$2]",
                    DriverType_Name(type()), abs_time_formatted, state_->ToString());
}

WriteTransactionState::WriteTransactionState(TabletPeer* tablet_peer,
                                             const tserver::WriteRequestPB *request,
                                             tserver::WriteResponsePB *response)
  : TransactionState(tablet_peer),
    request_(request),
    response_(response),
    mvcc_tx_(nullptr),
    schema_at_decode_time_(nullptr) {
  if (request) {
    external_consistency_mode_ = request->external_consistency_mode();
  } else {
    external_consistency_mode_ = CLIENT_PROPAGATED;
  }
}


void WriteTransactionState::SetMvccTxAndTimestamp(gscoped_ptr<ScopedTransaction> mvcc_tx) {
  DCHECK(!mvcc_tx_) << "Mvcc transaction already started/set.";
  if (has_timestamp()) {
    DCHECK_EQ(timestamp(), mvcc_tx->timestamp());
  } else {
    set_timestamp(mvcc_tx->timestamp());
  }
  mvcc_tx_ = mvcc_tx.Pass();
}

void WriteTransactionState::set_tablet_components(
    const scoped_refptr<const TabletComponents>& components) {
  DCHECK(!tablet_components_) << "Already set";
  DCHECK(components);
  tablet_components_ = components;
}

void WriteTransactionState::AcquireSchemaLock(rw_semaphore* schema_lock) {
  TRACE("Acquiring schema lock in shared mode");
  shared_lock<rw_semaphore> temp(schema_lock);
  schema_lock_.swap(temp);
  TRACE("Acquired schema lock");
}

void WriteTransactionState::ReleaseSchemaLock() {
  shared_lock<rw_semaphore> temp;
  schema_lock_.swap(temp);
  TRACE("Released schema lock");
}

void WriteTransactionState::StartApplying() {
  CHECK_NOTNULL(mvcc_tx_.get())->StartApplying();
}

void WriteTransactionState::Abort() {
  if (mvcc_tx_.get() != nullptr) {
    // Abort the transaction.
    mvcc_tx_->Abort();
  }
  mvcc_tx_.reset();

  release_row_locks();
  ReleaseSchemaLock();

  // After commiting, we may respond to the RPC and delete the
  // original request, so null them out here.
  ResetRpcFields();
}
void WriteTransactionState::Commit() {
  if (mvcc_tx_.get() != nullptr) {
    // Commit the transaction.
    mvcc_tx_->Commit();
  }
  mvcc_tx_.reset();

  // After commiting, we may respond to the RPC and delete the
  // original request, so null them out here.
  ResetRpcFields();
}

void WriteTransactionState::ReleaseTxResultPB(TxResultPB* result) const {
  result->Clear();
  result->mutable_ops()->Reserve(row_ops_.size());
  for (RowOp* op : row_ops_) {
    result->mutable_ops()->AddAllocated(CHECK_NOTNULL(op->result.release()));
  }
}

void WriteTransactionState::UpdateMetricsForOp(const RowOp& op) {
  if (op.result->has_failed_status()) {
    return;
  }
  switch (op.decoded_op.type) {
    case RowOperationsPB::INSERT:
      tx_metrics_.successful_inserts++;
      break;
    case RowOperationsPB::UPDATE:
      tx_metrics_.successful_updates++;
      break;
    case RowOperationsPB::DELETE:
      tx_metrics_.successful_deletes++;
      break;
    case RowOperationsPB::UNKNOWN:
    case RowOperationsPB::SPLIT_ROW:
      break;
  }
}

void WriteTransactionState::release_row_locks() {
  // free the row locks
  for (RowOp* op : row_ops_) {
    op->row_lock.Release();
  }
}

WriteTransactionState::~WriteTransactionState() {
  Reset();
}

void WriteTransactionState::Reset() {
  // We likely shouldn't Commit() here. See KUDU-625.
  Commit();
  tx_metrics_.Reset();
  timestamp_ = Timestamp::kInvalidTimestamp;
  tablet_components_ = nullptr;
  schema_at_decode_time_ = nullptr;
}

void WriteTransactionState::ResetRpcFields() {
  lock_guard<simple_spinlock> l(&txn_state_lock_);
  request_ = nullptr;
  response_ = nullptr;
  STLDeleteElements(&row_ops_);
}

string WriteTransactionState::ToString() const {
  string ts_str;
  if (has_timestamp()) {
    ts_str = timestamp().ToString();
  } else {
    ts_str = "<unassigned>";
  }

  // Stringify the actual row operations (eg INSERT/UPDATE/etc)
  // NOTE: we'll eventually need to gate this by some flag if we want to avoid
  // user data escaping into the log. See KUDU-387.
  string row_ops_str = "[";
  {
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    const size_t kMaxToStringify = 3;
    for (int i = 0; i < std::min(row_ops_.size(), kMaxToStringify); i++) {
      if (i > 0) {
        row_ops_str.append(", ");
      }
      row_ops_str.append(row_ops_[i]->ToString(*DCHECK_NOTNULL(schema_at_decode_time_)));
    }
    if (row_ops_.size() > kMaxToStringify) {
      row_ops_str.append(", ...");
    }
    row_ops_str.append("]");
  }

  return Substitute("WriteTransactionState $0 [op_id=($1), ts=$2, rows=$3]",
                    this,
                    op_id().ShortDebugString(),
                    ts_str,
                    row_ops_str);
}

}  // namespace tablet
}  // namespace kudu
