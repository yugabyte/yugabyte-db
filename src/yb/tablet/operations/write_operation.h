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

#ifndef YB_TABLET_OPERATIONS_WRITE_OPERATION_H
#define YB_TABLET_OPERATIONS_WRITE_OPERATION_H

#include <mutex>
#include <string>
#include <vector>

#include "yb/docdb/doc_operation.h"
#include "yb/docdb/lock_batch.h"

#include "yb/gutil/macros.h"

#include "yb/tablet/operations/operation.h"
#include "yb/tablet/tablet.pb.h"

#include "yb/util/locks.h"
#include "yb/util/operation_counter.h"

namespace yb {

namespace tserver {
class WriteRequestPB;
class WriteResponsePB;
}

namespace tablet {
class Tablet;

using docdb::LockBatch;

// An operation for a batch of inserts/mutates. This class holds and
// owns most everything related to a transaction, including the Replicate and Commit PB messages
//
// All the transaction related pointers are owned by this class
// and destroyed on Reset() or by the destructor.
//
// IMPORTANT: All the acquired locks will not be released unless the OperationState
// is either destroyed or Reset() or release_locks() is called. Beware of this
// or else there will be lock leaks.
//
// Used when logging to WAL in that we keep track of where inserts/updates
// were applied and add that information to the commit message that is stored
// on the WAL.
//
// NOTE: this class isn't thread safe.
class WriteOperation : public OperationBase<OperationType::kWrite, tserver::WriteRequestPB>  {
 public:
  WriteOperation(int64_t term,
                 CoarseTimePoint deadline, WriteOperationContext* context,
                 Tablet* tablet = nullptr,
                 tserver::WriteResponsePB *response = nullptr,
                 docdb::OperationKind kind = docdb::OperationKind::kWrite);

  ~WriteOperation();

  // Returns the prepared response to the client that will be sent when this
  // transaction is completed, if this transaction was started by a client.
  tserver::WriteResponsePB* response() {
    return response_;
  }

  // Releases the doc db locks and reset rpc fields.
  //
  // Note: request_ and response_ are set to nullptr after this method returns.
  void Release() override;

  // The QL write operations that return rowblocks that need to be returned as RPC sidecars
  // after the transaction completes.
  std::vector<std::unique_ptr<docdb::QLWriteOperation>>* ql_write_ops() {
    return &ql_write_ops_;
  }

  // Returns PGSQL write operations.
  // TODO(neil) These ops must report number of rows that was updated, deleted, or inserted.
  std::vector<std::unique_ptr<docdb::PgsqlWriteOperation>>* pgsql_write_ops() {
    return &pgsql_write_ops_;
  }

  // Moves the given lock batch into this object so it can be unlocked when the operation is
  // complete.
  void ReplaceDocDBLocks(LockBatch&& docdb_locks) {
    docdb_locks_ = std::move(docdb_locks);
  }

  // Releases all the DocDB locks acquired by this transaction.
  void ReleaseDocDbLocks();

  docdb::OperationKind kind() const {
    return kind_;
  }

  void set_force_txn_path() {
    force_txn_path_ = true;
  }

  bool force_txn_path() const {
    return force_txn_path_;
  }

  void SetTablet(Tablet* tablet) override;

  bool use_mvcc() const override {
    return true;
  }

  // Executes a Prepare for a write transaction
  //
  // Decodes the operations in the request PB and acquires row locks for each of the
  // affected rows.
  CHECKED_STATUS Prepare() override;

  ReadHybridTime read_time() {
    return ReadHybridTime::FromReadTimePB(*request());
  }

  HybridTime restart_read_ht() const {
    return restart_read_ht_;
  }

  void SetRestartReadHt(HybridTime value) {
    restart_read_ht_ = value;
  }

  CoarseTimePoint deadline() const {
    return deadline_;
  }

  docdb::DocOperations& doc_ops() {
    return doc_ops_;
  }

  static void StartSynchronization(
      std::unique_ptr<WriteOperation> operation, const Status& status) {
    // We release here, because DoStartSynchronization takes ownership on this.
    operation.release()->DoStartSynchronization(status);
  }

  void UseSubmitToken(ScopedRWOperation&& token) {
    submit_token_ = std::move(token);
  }

  void set_preparing_token(ScopedOperation&& preparing_token) {
    preparing_token_ = std::move(preparing_token);
  }

 private:
  friend class DelayedApplyOperation;

  // Actually starts the Mvcc transaction and assigns a hybrid_time to this transaction.
  void DoStartSynchronization(const Status& status);

  // Executes an Apply for a write transaction.
  //
  // Actually applies inserts/mutates into the tablet. After these start being
  // applied, the transaction must run to completion as there is currently no
  // means of undoing an update.
  //
  // After completing the inserts/mutates, the row locks and the mvcc transaction
  // can be released, allowing other transactions to update the same rows.
  // However the component lock must not be released until the commit msg, which
  // indicates where each of the inserts/mutates were applied, is persisted to
  // stable storage. Because of this ApplyTask must enqueue a CommitTask before
  // releasing both the row locks and deleting the MvccTransaction as we need to
  // make sure that Commits that touch the same set of rows are persisted in
  // order, for recovery.
  // This, of course, assumes that commits are executed in the same order they
  // are placed in the queue (but not necessarily in the same order of the
  // original requests) which is already a requirement of the consensus
  // algorithm.
  // Commits the mvcc transaction and updates the metrics.
  CHECKED_STATUS DoReplicated(int64_t leader_term, Status* complete_status) override;

  // Aborts the mvcc transaction.
  CHECKED_STATUS DoAborted(const Status& status) override;

  void SubmittedToPreparer() override {
    preparing_token_ = ScopedOperation();
  }

  // Reset the response, and row_ops_ (which refers to data
  // from the request). Request is owned by WriteOperation using a unique_ptr.
  // A copy is made at initialization, so we don't need to reset it.
  void ResetRpcFields();

  HybridTime WriteHybridTime() const override;

  // The QL write operations that return rowblocks that need to be returned as RPC sidecars
  // after the operation completes.
  std::vector<std::unique_ptr<docdb::QLWriteOperation>> ql_write_ops_;

  // The PGSQL write operations that return rowblocks that need to be returned as RPC sidecars
  // after the transaction completes.
  std::vector<std::unique_ptr<docdb::PgsqlWriteOperation>> pgsql_write_ops_;

  // Store the ids that have been locked for DocDB operation. They need to be released on commit
  // or if an error happens.
  LockBatch docdb_locks_;

  // True if we know that this operation is on a transactional table so make sure we go through the
  // transactional codepath.
  bool force_txn_path_ = false;

  const int64_t term_;
  ScopedOperation preparing_token_;
  ScopedRWOperation submit_token_;
  const CoarseTimePoint deadline_;
  WriteOperationContext* const context_;

  // Pointers to the rpc context, request and response, lifecycle
  // is managed by the rpc subsystem. These pointers maybe nullptr if the
  // operation was not initiated by an RPC call.
  tserver::WriteResponsePB* response_;

  docdb::OperationKind kind_;

  // this transaction's start time
  CoarseTimePoint start_time_;

  HybridTime restart_read_ht_;

  docdb::DocOperations doc_ops_;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_OPERATIONS_WRITE_OPERATION_H
