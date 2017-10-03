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

#ifndef KUDU_TABLET_WRITE_TRANSACTION_H_
#define KUDU_TABLET_WRITE_TRANSACTION_H_

#include <string>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/gutil/macros.h"
#include "kudu/tablet/lock_manager.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/tablet/transactions/transaction.h"
#include "kudu/util/locks.h"

namespace kudu {
struct DecodedRowOperation;
class ConstContiguousRow;
class RowwiseRowBlockPB;

namespace consensus {
class Consensus;
}

namespace tserver {
class WriteRequestPB;
class WriteResponsePB;
}

namespace tablet {
struct RowOp;
class RowSetKeyProbe;
struct TabletComponents;

// A TransactionState for a batch of inserts/mutates. This class holds and
// owns most everything related to a transaction, including:
// - A RowOp structure for each of the rows being inserted or mutated, which itself
//   contains:
//   - decoded/projected data
//   - row lock reference
//   - result of this particular insert/mutate operation, once executed
// - the Replicate and Commit PB messages
//
// All the transaction related pointers are owned by this class
// and destroyed on Reset() or by the destructor.
//
// IMPORTANT: All the acquired locks will not be released unless the TransactionState
// is either destroyed or Reset() or release_locks() is called. Beware of this
// or else there will be lock leaks.
//
// Used when logging to WAL in that we keep track of where inserts/updates
// were applied and add that information to the commit message that is stored
// on the WAL.
//
// NOTE: this class isn't thread safe.
class WriteTransactionState : public TransactionState {
 public:
  WriteTransactionState(TabletPeer* tablet_peer = NULL,
                        const tserver::WriteRequestPB *request = NULL,
                        tserver::WriteResponsePB *response = NULL);
  virtual ~WriteTransactionState();

  // Returns the result of this transaction in its protocol buffers form.
  // The transaction result holds information on exactly which memory stores
  // were mutated in the context of this transaction and can be used to
  // perform recovery.
  //
  // This releases part of the state of the transaction, and will crash
  // if called more than once.
  void ReleaseTxResultPB(TxResultPB* result) const;

  // Returns the original client request for this transaction, if there was
  // one.
  const tserver::WriteRequestPB *request() const OVERRIDE {
    return request_;
  }

  // Returns the prepared response to the client that will be sent when this
  // transaction is completed, if this transaction was started by a client.
  tserver::WriteResponsePB *response() OVERRIDE {
    return response_;
  }

  // Set the MVCC transaction associated with this Write operation.
  // This must be called exactly once, during the PREPARE phase just
  // after the MvccManager has assigned a timestamp.
  // This also copies the timestamp from the MVCC transaction into the
  // WriteTransactionState object.
  void SetMvccTxAndTimestamp(gscoped_ptr<ScopedTransaction> mvcc_tx);

  // Set the Tablet components that this transaction will write into.
  // Called exactly once at the beginning of Apply, before applying its
  // in-memory edits.
  void set_tablet_components(const scoped_refptr<const TabletComponents>& components);

  // Take a shared lock on the given schema lock.
  // This is required prior to decoding rows so that the schema does
  // not change in between performing the projection and applying
  // the writes.
  void AcquireSchemaLock(rw_semaphore* schema_lock);

  // Release the already-acquired schema lock.
  void ReleaseSchemaLock();


  void set_schema_at_decode_time(const Schema* schema) {
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    schema_at_decode_time_ = schema;
  }

  const Schema* schema_at_decode_time() const {
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    return schema_at_decode_time_;
  }

  const TabletComponents* tablet_components() const {
    return tablet_components_.get();
  }

  // Notifies the MVCC manager that this operation is about to start applying
  // its in-memory edits. After this method is called, the transaction _must_
  // Commit() within a bounded amount of time (there may be other threads
  // blocked on it).
  void StartApplying();

  // Commits the Mvcc transaction and releases the component lock. After
  // this method is called all the inserts and mutations will become
  // visible to other transactions.
  //
  // Only one of Commit() or Abort() should be called.
  // REQUIRES: StartApplying() was called.
  //
  // Note: request_ and response_ are set to NULL after this method returns.
  void Commit();

  // Aborts the mvcc transaction and releases the component lock.
  // Only one of Commit() or Abort() should be called.
  //
  // REQUIRES: StartApplying() must never have been called.
  void Abort();

  // Returns all the prepared row writes for this transaction. Usually called
  // on the apply phase to actually make changes to the tablet.
  const std::vector<RowOp*>& row_ops() const {
    return row_ops_;
  }

  void swap_row_ops(std::vector<RowOp*>* new_ops) {
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    row_ops_.swap(*new_ops);
  }

  void UpdateMetricsForOp(const RowOp& op);

  // Releases all the row locks acquired by this transaction.
  void release_row_locks();

  // Resets this TransactionState, releasing all locks, destroying all prepared
  // writes, clearing the transaction result _and_ committing the current Mvcc
  // transaction.
  void Reset();

  virtual std::string ToString() const OVERRIDE;

 private:
  // Reset the RPC request, response, and row_ops_ (which refers to data
  // from the request).
  void ResetRpcFields();

  // pointers to the rpc context, request and response, lifecyle
  // is managed by the rpc subsystem. These pointers maybe NULL if the
  // transaction was not initiated by an RPC call.
  const tserver::WriteRequestPB* request_;
  tserver::WriteResponsePB* response_;

  // The row operations which are decoded from the request during PREPARE
  // Protected by superclass's txn_state_lock_.
  std::vector<RowOp*> row_ops_;

  // The MVCC transaction, set up during PREPARE phase
  gscoped_ptr<ScopedTransaction> mvcc_tx_;

  // The tablet components, acquired at the same time as mvcc_tx_ is set.
  scoped_refptr<const TabletComponents> tablet_components_;

  // A lock held on the tablet's schema. Prevents concurrent schema change
  // from racing with a write.
  shared_lock<rw_semaphore> schema_lock_;

  // The Schema of the tablet when the transaction was first decoded.
  // This is verified at APPLY time to ensure we don't have races against
  // schema change.
  // Protected by superclass's txn_state_lock_.
  const Schema* schema_at_decode_time_;

  DISALLOW_COPY_AND_ASSIGN(WriteTransactionState);
};

// Executes a write transaction.
class WriteTransaction : public Transaction {
 public:
  WriteTransaction(WriteTransactionState* tx_state, consensus::DriverType type);

  virtual WriteTransactionState* state() OVERRIDE { return state_.get(); }
  virtual const WriteTransactionState* state() const OVERRIDE { return state_.get(); }

  void NewReplicateMsg(gscoped_ptr<consensus::ReplicateMsg>* replicate_msg) OVERRIDE;

  // Executes a Prepare for a write transaction
  //
  // Decodes the operations in the request PB and acquires row locks for each of the
  // affected rows. This results in adding 'RowOp' objects for each of the operations
  // into the WriteTransactionState.
  virtual Status Prepare() OVERRIDE;

  // Actually starts the Mvcc transaction and assigns a timestamp to this transaction.
  virtual Status Start() OVERRIDE;

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
  virtual Status Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) OVERRIDE;

  // Releases the row locks (Early Lock Release).
  virtual void PreCommit() OVERRIDE;

  // If result == COMMITTED, commits the mvcc transaction and updates
  // the metrics, if result == ABORTED aborts the mvcc transaction.
  virtual void Finish(TransactionResult result) OVERRIDE;

  virtual std::string ToString() const OVERRIDE;

 private:
  // this transaction's start time
  MonoTime start_time_;

  gscoped_ptr<WriteTransactionState> state_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WriteTransaction);
};

}  // namespace tablet
}  // namespace kudu

#endif /* KUDU_TABLET_WRITE_TRANSACTION_H_ */
