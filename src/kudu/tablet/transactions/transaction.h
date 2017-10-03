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

#ifndef KUDU_TABLET_TRANSACTION_H_
#define KUDU_TABLET_TRANSACTION_H_

#include <string>

#include "kudu/common/timestamp.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus.h"
#include "kudu/util/auto_release_pool.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"
#include "kudu/util/memory/arena.h"

namespace kudu {

namespace tablet {
class TabletPeer;
class TransactionCompletionCallback;
class TransactionState;

// All metrics associated with a Transaction.
struct TransactionMetrics {
  TransactionMetrics();
  void Reset();
  int successful_inserts;
  int successful_updates;
  int successful_deletes;
  uint64_t commit_wait_duration_usec;
};

// Base class for transactions.
// There are different implementations for different types (Write, AlterSchema, etc.).
// TransactionDriver implementations use Transactions along with Consensus to execute
// and replicate operations in a consensus configuration.
class Transaction {
 public:

  enum TransactionType {
    WRITE_TXN,
    ALTER_SCHEMA_TXN,
  };

  enum TraceType {
    NO_TRACE_TXNS = 0,
    TRACE_TXNS = 1
  };

  enum TransactionResult {
    COMMITTED,
    ABORTED
  };

  Transaction(TransactionState* state, consensus::DriverType type, TransactionType tx_type);

  // Returns the TransactionState for this transaction.
  virtual TransactionState* state() { return state_; }
  virtual const TransactionState* state() const { return state_; }

  // Returns whether this transaction is being executed on the leader or on a
  // replica.
  consensus::DriverType type() const { return type_; }

  // Returns this transaction's type.
  TransactionType tx_type() const { return tx_type_; }

  // Builds the ReplicateMsg for this transaction.
  virtual void NewReplicateMsg(gscoped_ptr<consensus::ReplicateMsg>* replicate_msg) = 0;

  // Executes the prepare phase of this transaction, the actual actions
  // of this phase depend on the transaction type, but usually are limited
  // to what can be done without actually changing data structures and without
  // side-effects.
  virtual Status Prepare() = 0;

  // Actually starts a transaction, assigning a timestamp to the transaction.
  // LEADER replicas execute this in or right after Prepare(), while FOLLOWER/LEARNER
  // replicas execute this right before the Apply() phase as the transaction's
  // timestamp is only available on the LEADER's commit message.
  // Once Started(), state might have leaked to other replicas/local log and the
  // transaction can't be cancelled without issuing an abort message.
  virtual Status Start() = 0;

  // Executes the Apply() phase of the transaction, the actual actions of
  // this phase depend on the transaction type, but usually this is the
  // method where data-structures are changed.
  virtual Status Apply(gscoped_ptr<consensus::CommitMsg>* commit_msg) = 0;

  // Executed after Apply() but before the commit is submitted to consensus.
  // Some transactions use this to perform pre-commit actions (e.g. write
  // transactions perform early lock release on this hook).
  // Default implementation does nothing.
  virtual void PreCommit() {}

  // Executed after the transaction has been applied and the commit message has
  // been appended to the log (though it might not be durable yet), or if the
  // transaction was aborted.
  // Implementations are expected to perform cleanup on this method, the driver
  // will reply to the client after this method call returns.
  // 'result' will be either COMMITTED or ABORTED, letting implementations
  // know what was the final status of the transaction.
  virtual void Finish(TransactionResult result) {}

  // Each implementation should have its own ToString() method.
  virtual std::string ToString() const = 0;

  virtual ~Transaction() {}

 private:
  // A private version of this transaction's transaction state so that
  // we can use base TransactionState methods on destructors.
  TransactionState* state_;
  const consensus::DriverType type_;
  const TransactionType tx_type_;
};

class TransactionState {
 public:

  // Returns the request PB associated with this transaction. May be NULL if
  // the transaction's state has been reset.
  virtual const google::protobuf::Message* request() const { return NULL; }

  // Returns the response PB associated with this transaction, or NULL.
  // This will only return a non-null object for leader-side transactions.
  virtual google::protobuf::Message* response() { return NULL; }

  // Sets the ConsensusRound for this transaction, if this transaction is
  // being executed through the consensus system.
  void set_consensus_round(const scoped_refptr<consensus::ConsensusRound>& consensus_round) {
    consensus_round_ = consensus_round;
    op_id_ = consensus_round_->id();
  }

  // Returns the ConsensusRound being used, if this transaction is being
  // executed through the consensus system or NULL if it's not.
  consensus::ConsensusRound* consensus_round() {
    return consensus_round_.get();
  }

  TabletPeer* tablet_peer() const {
    return tablet_peer_;
  }

  // Return metrics related to this transaction.
  const TransactionMetrics& metrics() const {
    return tx_metrics_;
  }

  TransactionMetrics* mutable_metrics() {
    return &tx_metrics_;
  }

  void set_completion_callback(gscoped_ptr<TransactionCompletionCallback> completion_clbk) {
    completion_clbk_.reset(completion_clbk.release());
  }

  // Returns the completion callback.
  TransactionCompletionCallback* completion_callback() {
    return DCHECK_NOTNULL(completion_clbk_.get());
  }

  // Sets a heap object to be managed by this transaction's AutoReleasePool.
  template<class T>
  T* AddToAutoReleasePool(T* t) {
    return pool_.Add(t);
  }

  // Sets an array heap object to be managed by this transaction's AutoReleasePool.
  template<class T>
  T* AddArrayToAutoReleasePool(T* t) {
    return pool_.AddArray(t);
  }

  // Return the arena associated with this transaction.
  // NOTE: this is not a thread-safe arena!
  Arena* arena() {
    return &arena_;
  }

  // Each implementation should have its own ToString() method.
  virtual std::string ToString() const = 0;

  // Sets the timestamp for the transaction
  virtual void set_timestamp(const Timestamp& timestamp) {
    // make sure we set the timestamp only once
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    DCHECK_EQ(timestamp_, Timestamp::kInvalidTimestamp);
    timestamp_ = timestamp;
  }

  Timestamp timestamp() const {
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    DCHECK(timestamp_ != Timestamp::kInvalidTimestamp);
    return timestamp_;
  }

  bool has_timestamp() const {
    lock_guard<simple_spinlock> l(&txn_state_lock_);
    return timestamp_ != Timestamp::kInvalidTimestamp;
  }

  consensus::OpId* mutable_op_id() {
    return &op_id_;
  }

  const consensus::OpId& op_id() const {
    return op_id_;
  }

  ExternalConsistencyMode external_consistency_mode() const {
    return external_consistency_mode_;
  }

 protected:
  explicit TransactionState(TabletPeer* tablet_peer);
  virtual ~TransactionState();

  TransactionMetrics tx_metrics_;

  // The tablet peer that is coordinating this transaction.
  TabletPeer* const tablet_peer_;

  // Optional callback to be called once the transaction completes.
  gscoped_ptr<TransactionCompletionCallback> completion_clbk_;

  AutoReleasePool pool_;

  // This transaction's timestamp. Protected by txn_state_lock_.
  Timestamp timestamp_;

  // The clock error when timestamp_ was read.
  uint64_t timestamp_error_;

  Arena arena_;

  // This OpId stores the canonical "anchor" OpId for this transaction.
  consensus::OpId op_id_;

  scoped_refptr<consensus::ConsensusRound> consensus_round_;

  // The defined consistency mode for this transaction.
  ExternalConsistencyMode external_consistency_mode_;

  // Lock that protects access to transaction state.
  mutable simple_spinlock txn_state_lock_;
};

// A parent class for the callback that gets called when transactions
// complete.
//
// This must be set in the TransactionState if the transaction initiator is to
// be notified of when a transaction completes. The callback belongs to the
// transaction context and is deleted along with it.
//
// NOTE: this is a concrete class so that we can use it as a default implementation
// which avoids callers having to keep checking for NULL.
class TransactionCompletionCallback {
 public:

  TransactionCompletionCallback();

  // Allows to set an error for this transaction and a mapping to a server level code.
  // Calling this method does not mean the transaction is completed.
  void set_error(const Status& status, tserver::TabletServerErrorPB::Code code);

  void set_error(const Status& status);

  bool has_error() const;

  const Status& status() const;

  const tserver::TabletServerErrorPB::Code error_code() const;

  // Subclasses should override this.
  virtual void TransactionCompleted();

  virtual ~TransactionCompletionCallback();

 protected:
  Status status_;
  tserver::TabletServerErrorPB::Code code_;
};

// TransactionCompletionCallback implementation that can be waited on.
// Helper to make async transactions, sync.
// This is templated to accept any response PB that has a TabletServerError
// 'error' field and to set the error before performing the latch countdown.
// The callback does *not* take ownership of either latch or response.
template<class ResponsePB>
class LatchTransactionCompletionCallback : public TransactionCompletionCallback {
 public:
  explicit LatchTransactionCompletionCallback(CountDownLatch* latch,
                                              ResponsePB* response)
    : latch_(DCHECK_NOTNULL(latch)),
      response_(DCHECK_NOTNULL(response)) {
  }

  virtual void TransactionCompleted() OVERRIDE {
    if (!status_.ok()) {
      StatusToPB(status_, response_->mutable_error()->mutable_status());
    }
    latch_->CountDown();
  }

 private:
  CountDownLatch* latch_;
  ResponsePB* response_;
};

// A transaction completion callback that takes a StatusCallback and simply
// calls it with the transaction status when it completes.
class StatusTransactionCompletionCallback : public TransactionCompletionCallback {
 public:
  explicit StatusTransactionCompletionCallback(StatusCallback callback)
      : callback_(std::move(callback)) {}

  virtual void TransactionCompleted() OVERRIDE {
    callback_.Run(status());
  }
 private:
  StatusCallback callback_;
};

}  // namespace tablet
}  // namespace kudu

#endif /* KUDU_TABLET_TRANSACTION_H_ */
