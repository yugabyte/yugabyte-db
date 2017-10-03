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

#ifndef KUDU_TABLET_TRANSACTION_DRIVER_H_
#define KUDU_TABLET_TRANSACTION_DRIVER_H_

#include <string>

#include "kudu/consensus/consensus.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/walltime.h"
#include "kudu/tablet/transactions/transaction.h"
#include "kudu/util/status.h"
#include "kudu/util/trace.h"

namespace kudu {
class ThreadPool;

namespace log {
class Log;
} // namespace log

namespace tablet {
class TransactionOrderVerifier;
class TransactionTracker;

// Base class for transaction drivers.
//
// TransactionDriver classes encapsulate the logic of coordinating the execution of
// an operation. The exact triggering of the methods differs based on whether the
// operation is being executed on a leader or replica, but the general flow is:
//
//  1 - Init() is called on a newly created driver object.
//      If the driver is instantiated from a REPLICA, then we know that
//      the operation is already "REPLICATING" (and thus we don't need to
//      trigger replication ourself later on).
//
//  2 - ExecuteAsync() is called. This submits PrepareAndStartTask() to prepare_pool_
//      and returns immediately.
//
//  3 - PrepareAndStartTask() calls Prepare() and Start() on the transaction.
//
//      Once successfully prepared, if we have not yet replicated (i.e we are leader),
//      also triggers consensus->Replicate() and changes the replication state to
//      REPLICATING.
//
//      On the other hand, if we have already successfully replicated (eg we are the
//      follower and ConsensusCommitted() has already been called, then we can move
//      on to ApplyAsync().
//
//  4 - The Consensus implementation calls ConsensusCommitted()
//
//      This is triggered by consensus when the commit index moves past our own
//      OpId. On followers, this can happen before Prepare() finishes, and thus
//      we have to check whether we have already done step 3. On leaders, we
//      don't start the consensus round until after Prepare, so this check always
//      passes.
//
//      If Prepare() has already completed, then we trigger ApplyAsync().
//
//  5 - ApplyAsync() submits ApplyTask() to the apply_pool_.
//      ApplyTask() calls transaction_->Apply().
//
//      When Apply() is called, changes are made to the in-memory data structures. These
//      changes are not visible to clients yet. After Apply() completes, a CommitMsg
//      is enqueued to the WAL in order to store information about the operation result
//      and provide correct recovery.
//
//      After the commit message has been enqueued in the Log, the driver executes Finalize()
//      which, in turn, makes transactions make their changes visible to other transactions.
//      After this step the driver replies to the client if needed and the transaction
//      is completed.
//      In-mem data structures that contain the changes made by the transaction can now
//      be made durable.
//
// [1] - see 'Implementation Techniques for Main Memory Database Systems', DeWitt et. al.
//
// This class is thread safe.
class TransactionDriver : public RefCountedThreadSafe<TransactionDriver> {

 public:
  // Construct TransactionDriver. TransactionDriver does not take ownership
  // of any of the objects pointed to in the constructor's arguments.
  TransactionDriver(TransactionTracker* txn_tracker,
                    consensus::Consensus* consensus,
                    log::Log* log,
                    ThreadPool* prepare_pool,
                    ThreadPool* apply_pool,
                    TransactionOrderVerifier* order_verifier);

  // Perform any non-constructor initialization. Sets the transaction
  // that will be executed.
  Status Init(gscoped_ptr<Transaction> transaction,
              consensus::DriverType driver);

  // Returns the OpId of the transaction being executed or an uninitialized
  // OpId if none has been assigned. Returns a copy and thus should not
  // be used in tight loops.
  consensus::OpId GetOpId();

  // Submits the transaction for execution.
  // The returned status acknowledges any error on the submission process.
  // The transaction will be replied to asynchronously.
  Status ExecuteAsync();

  // Aborts the transaction, if possible. Since transactions are executed in
  // multiple stages by multiple executors it might not be possible to stop
  // the transaction immediately, but this will make sure it is aborted
  // at the next synchronization point.
  void Abort(const Status& status);

  // Callback from Consensus when replication is complete, and thus the operation
  // is considered "committed" from the consensus perspective (ie it will be
  // applied on every node, and not ever truncated from the state machine history).
  // If status is anything different from OK() we don't proceed with the apply.
  //
  // see comment in the interface for an important TODO.
  void ReplicationFinished(const Status& status);

  std::string ToString() const;

  std::string ToStringUnlocked() const;

  std::string LogPrefix() const;

  // Returns the type of the transaction being executed by this driver.
  Transaction::TransactionType tx_type() const;

  // Returns the state of the transaction being executed by this driver.
  const TransactionState* state() const;

  const MonoTime& start_time() const { return start_time_; }

  Trace* trace() { return trace_.get(); }

 private:
  friend class RefCountedThreadSafe<TransactionDriver>;
  enum ReplicationState {
    // The operation has not yet been sent to consensus for replication
    NOT_REPLICATING,

    // Replication has been triggered (either because we are the leader and triggered it,
    // or because we are a follower and we started this operation in response to a
    // leader's call)
    REPLICATING,

    // Replication has failed, and we are certain that no other may have received the
    // operation (ie we failed before even sending the request off of our node).
    REPLICATION_FAILED,

    // Replication has succeeded.
    REPLICATED
  };

  enum PrepareState {
    NOT_PREPARED,
    PREPARED
  };

  ~TransactionDriver() {}

  // The task submitted to the prepare threadpool to prepare and start
  // the transaction. If PrepareAndStart() fails, calls HandleFailure.
  void PrepareAndStartTask();
  // Actually prepare and start.
  Status PrepareAndStart();

  // Submits ApplyTask to the apply pool.
  Status ApplyAsync();

  // Calls Transaction::Apply() followed by Consensus::Commit() with the
  // results from the Apply().
  void ApplyTask();

  // Sleeps until the transaction is allowed to commit based on the
  // requested consistency mode.
  Status CommitWait();

  // Handle a failure in any of the stages of the operation.
  // In some cases, this will end the operation and call its callback.
  // In others, where we can't recover, this will FATAL.
  void HandleFailure(const Status& s);

  // Called on Transaction::Apply() after the CommitMsg has been successfully
  // appended to the WAL.
  void Finalize();

  // Returns the mutable state of the transaction being executed by
  // this driver.
  TransactionState* mutable_state();

  // Return a short string indicating where the transaction currently is in the
  // state machine.
  static std::string StateString(ReplicationState repl_state,
                                 PrepareState prep_state);

  // Sets the timestamp on the response PB, if there is one.
  void SetResponseTimestamp(TransactionState* transaction_state,
                            const Timestamp& timestamp);

  TransactionTracker* const txn_tracker_;
  consensus::Consensus* const consensus_;
  log::Log* const log_;
  ThreadPool* const prepare_pool_;
  ThreadPool* const apply_pool_;
  TransactionOrderVerifier* const order_verifier_;

  Status transaction_status_;

  // Lock that synchronizes access to the transaction's state.
  mutable simple_spinlock lock_;

  // A copy of the transaction's OpId, set when the transaction first
  // receives one from Consensus and uninitialized until then.
  // TODO(todd): we have three separate copies of this now -- in TransactionState,
  // CommitMsg, and here... we should be able to consolidate!
  consensus::OpId op_id_copy_;

  // Lock that protects access to the driver's copy of the op_id, specifically.
  // GetOpId() is the only method expected to be called by threads outside
  // of the control of the driver, so we use a special lock to control access
  // otherwise callers would block for a long time for long running transactions.
  mutable simple_spinlock opid_lock_;

  // The transaction to be executed by this driver.
  gscoped_ptr<Transaction> transaction_;

  // Trace object for tracing any transactions started by this driver.
  scoped_refptr<Trace> trace_;

  const MonoTime start_time_;

  ReplicationState replication_state_;
  PrepareState prepare_state_;

  // The system monotonic time when the operation was prepared.
  // This is used for debugging only, not any actual operation ordering.
  MicrosecondsInt64 prepare_physical_timestamp_;

  DISALLOW_COPY_AND_ASSIGN(TransactionDriver);
};

}  // namespace tablet
}  // namespace kudu

#endif /* KUDU_TABLET_TRANSACTION_DRIVER_H_ */
