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

#include "yb/tablet/transactions/write_transaction.h"

#include <algorithm>
#include <vector>

#include "yb/common/row_operations.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/walltime.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/row_op.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/trace.h"

DEFINE_int32(tablet_inject_latency_on_apply_write_txn_ms, 0,
             "How much latency to inject when a write transaction is applied. "
             "For testing only!");
TAG_FLAG(tablet_inject_latency_on_apply_write_txn_ms, unsafe);
TAG_FLAG(tablet_inject_latency_on_apply_write_txn_ms, runtime);

namespace yb {
namespace tablet {

using std::lock_guard;
using std::mutex;
using std::unique_ptr;
using consensus::ReplicateMsg;
using consensus::CommitMsg;
using consensus::DriverType;
using consensus::WRITE_OP;
using tserver::TabletServerErrorPB;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;
using strings::Substitute;

WriteTransaction::WriteTransaction(std::unique_ptr<WriteTransactionState> state, DriverType type)
  : Transaction(std::move(state), type, Transaction::WRITE_TXN),
    start_time_(MonoTime::FineNow()) {
}

consensus::ReplicateMsgPtr WriteTransaction::NewReplicateMsg() {
  auto result = std::make_shared<ReplicateMsg>();
  result->set_op_type(WRITE_OP);
  result->set_allocated_write_request(state()->mutable_request());
  return result;
}

Status WriteTransaction::Prepare() {
  TRACE_EVENT0("txn", "WriteTransaction::Prepare");
  TRACE("PREPARE: Starting");

  // Decode everything first so that we give up if something major is wrong.
  Schema client_schema;
  RETURN_NOT_OK_PREPEND(SchemaFromPB(state()->request()->schema(), &client_schema),
                        "Cannot decode client schema");
  if (client_schema.has_column_ids()) {
    // TODO: we have this kind of code a lot - add a new SchemaFromPB variant which
    // does this check inline.
    Status s = STATUS(InvalidArgument, "User requests should not have Column IDs");
    state()->completion_callback()->set_error(s, TabletServerErrorPB::INVALID_SCHEMA);
    return s;
  }

  auto* tablet = tablet_peer()->tablet();

  Status s = tablet->DecodeWriteOperations(&client_schema, state());
  if (!s.ok()) {
    // TODO: is MISMATCHED_SCHEMA always right here? probably not.
    state()->completion_callback()->set_error(s, TabletServerErrorPB::MISMATCHED_SCHEMA);
    return s;
  }

  // Acquire Kudu row locks. This is Kudu-specific and has no effect for YB tables.
  RETURN_NOT_OK(tablet->AcquireKuduRowLocks(state()));

  TRACE("PREPARE: finished.");
  return Status::OK();
}

void WriteTransaction::Start() {
  TRACE("Start()");
  state()->tablet_peer()->tablet()->StartTransaction(state());
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

  if (tablet->table_type() == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
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
  } else {
    // We don't use COMMIT messages for non-Kudu tables.
    commit_msg->reset(nullptr);
  }

  return Status::OK();
}

void WriteTransaction::PreCommit() {
  TRACE_EVENT0("txn", "WriteTransaction::PreCommit");
  TRACE("PRECOMMIT: Releasing row and schema locks");
  // Perform early lock release after we've applied all changes
  state()->ReleaseDocDbLocks(tablet_peer()->tablet());
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

  TabletMetrics* metrics = state()->tablet_peer()->tablet()->metrics();
  if (metrics) {
    // TODO: should we change this so it's actually incremented by the
    // Tablet code itself instead of this wrapper code?
    metrics->rows_inserted->IncrementBy(state()->metrics().successful_inserts);
    metrics->rows_updated->IncrementBy(state()->metrics().successful_updates);
    metrics->rows_deleted->IncrementBy(state()->metrics().successful_deletes);

    if (type() == consensus::LEADER) {
      if (state()->external_consistency_mode() == COMMIT_WAIT) {
        metrics->commit_wait_duration->Increment(state()->metrics().commit_wait_duration_usec);
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
                    DriverType_Name(type()), abs_time_formatted, state()->ToString());
}

WriteTransactionState::WriteTransactionState(TabletPeer* tablet_peer,
                                             const tserver::WriteRequestPB *request,
                                             tserver::WriteResponsePB *response)
    : TransactionState(tablet_peer),
      // We need to copy over the request from the RPC layer, as we're modifying it in the tablet
      // layer.
      request_(request ? new WriteRequestPB(*request) : nullptr),
      response_(response),
      mvcc_tx_(nullptr),
      schema_at_decode_time_(nullptr) {
  if (request) {
    external_consistency_mode_ = request->external_consistency_mode();
  } else {
    external_consistency_mode_ = CLIENT_PROPAGATED;
  }
}

void WriteTransactionState::SetMvccTxAndHybridTime(gscoped_ptr<ScopedWriteTransaction> mvcc_tx) {
  DCHECK(!mvcc_tx_) << "Mvcc transaction already started/set.";
  if (has_hybrid_time()) {
    DCHECK_EQ(hybrid_time(), mvcc_tx->hybrid_time());
  } else {
    set_hybrid_time(mvcc_tx->hybrid_time());
  }

  lock_guard<mutex> lock(mvcc_tx_mutex_);
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
  shared_lock<rw_semaphore> temp(*schema_lock);
  schema_lock_.swap(temp);
  TRACE("Acquired schema lock");
}

void WriteTransactionState::ReleaseSchemaLock() {
  shared_lock<rw_semaphore> temp;
  schema_lock_.swap(temp);
  TRACE("Released schema lock");
}

void WriteTransactionState::StartApplying() {
  lock_guard<mutex> lock(mvcc_tx_mutex_);
  if (!mvcc_tx_) {
    LOG(INFO) << "mvcc_tx is nullptr for hybrid_time " << hybrid_time() << ":\n" << GetStackTrace();
  }
  CHECK_NOTNULL(mvcc_tx_.get())->StartApplying();
}

void WriteTransactionState::Abort() {
  ResetMvccTx([](ScopedWriteTransaction* mvcc_tx) { mvcc_tx->Abort(); });

  ReleaseDocDbLocks(tablet_peer()->tablet());
  ReleaseSchemaLock();

  // After aborting, we may respond to the RPC and delete the
  // original request, so null them out here.
  ResetRpcFields();
}

void WriteTransactionState::Commit() {
  ResetMvccTx([](ScopedWriteTransaction* mvcc_tx) { mvcc_tx->Commit(); });

  // After committing, we may respond to the RPC and delete the
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

void WriteTransactionState::ReleaseDocDbLocks(Tablet* tablet) {
  // Free docdb multi-level locks.
  tablet->shared_lock_manager()->Unlock(docdb_locks_);

  if (tablet->table_type() == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
    // The code below is kudu-only and will be removed, along with the tablet parameter.
    for (RowOp *op : row_ops_) {
      op->row_lock.Release();
    }
  } else {
    CHECK(row_ops_.empty());
  }
}

WriteTransactionState::~WriteTransactionState() {
  Reset();
  // Ownership is with the Round object, if one exists, else with us.
  if (!consensus_round() && request_ != nullptr) {
    delete request_;
  }
}

void WriteTransactionState::Reset() {
  // We likely shouldn't Commit() here. See KUDU-625.
  Commit();
  tx_metrics_.Reset();
  hybrid_time_ = HybridTime::kInvalidHybridTime;
  tablet_components_ = nullptr;
  schema_at_decode_time_ = nullptr;
}

void WriteTransactionState::ResetRpcFields() {
  std::lock_guard<simple_spinlock> l(txn_state_lock_);
  response_ = nullptr;
  STLDeleteElements(&row_ops_);
}

void WriteTransactionState::ResetMvccTx(std::function<void(ScopedWriteTransaction*)> txn_action) {
  lock_guard<mutex> lock(mvcc_tx_mutex_);
  if (mvcc_tx_.get() != nullptr) {
    // Abort the transaction.
    txn_action(mvcc_tx_.get());
  }
  mvcc_tx_.reset();
}

string WriteTransactionState::ToString() const {
  string ts_str;
  if (has_hybrid_time()) {
    ts_str = hybrid_time().ToString();
  } else {
    ts_str = "<unassigned>";
  }

  // Stringify the actual row operations (eg INSERT/UPDATE/etc)
  // NOTE: we'll eventually need to gate this by some flag if we want to avoid
  // user data escaping into the log. See KUDU-387.
  string row_ops_str = "[";
  {
    std::lock_guard<simple_spinlock> l(txn_state_lock_);
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
}  // namespace yb
