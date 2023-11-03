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

#pragma once

#include <mutex>
#include <string>

#include <boost/optional/optional.hpp>

#include "yb/common/hybrid_time.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/consensus_types.pb.h"

#include "yb/rpc/lightweight_message.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/operation_counter.h"
#include "yb/util/opid.h"

namespace yb {

class Synchronizer;

namespace tablet {

using OperationCompletionCallback = std::function<void(const Status&)>;

YB_DEFINE_ENUM(
    OperationType,
    ((kWrite, consensus::WRITE_OP))
    ((kChangeMetadata, consensus::CHANGE_METADATA_OP))
    ((kUpdateTransaction, consensus::UPDATE_TRANSACTION_OP))
    ((kSnapshot, consensus::SNAPSHOT_OP))
    ((kTruncate, consensus::TRUNCATE_OP))
    ((kEmpty, consensus::UNKNOWN_OP))
    ((kHistoryCutoff, consensus::HISTORY_CUTOFF_OP))
    ((kSplit, consensus::SPLIT_OP))
    ((kChangeAutoFlagsConfig, consensus::CHANGE_AUTO_FLAGS_CONFIG_OP)));

YB_STRONGLY_TYPED_BOOL(WasPending);
YB_STRONGLY_TYPED_BOOL(IsLeaderSide);

// Base class for transactions.  There are different implementations for different types (Write,
// AlterSchema, etc.) OperationDriver implementations use Operations along with Consensus to execute
// and replicate operations in a consensus configuration.
//
// Most methods in this class perform internal synchronization.
class Operation {
 public:
  enum TraceType {
    NO_TRACE_TXNS = 0,
    TRACE_TXNS = 1
  };

  explicit Operation(OperationType operation_type, TabletPtr tablet);

  // Returns this transaction's type.
  OperationType operation_type() const { return operation_type_; }

  // Builds the ReplicateMsg for this transaction.
  virtual consensus::ReplicateMsgPtr NewReplicateMsg() = 0;

  // Executes the prepare phase of this transaction. The actual actions of this phase depend on the
  // transaction type, but usually are limited to what can be done without actually changing shared
  // data structures (such as the RocksDB memtable) and without side-effects.
  virtual Status Prepare(IsLeaderSide is_leader_side) = 0;

  // Applies replicated operation, the actual actions of this phase depend on the
  // operation type, but usually this is the method where data-structures are changed.
  // Also it should notify callback if necessary.
  Status Replicated(int64_t leader_term, WasPending was_pending);

  // Abort operation. Release resources and notify callbacks.
  void Aborted(const Status& status, bool was_pending);

  // Each implementation should have its own ToString() method.
  virtual std::string ToString() const;

  std::string LogPrefix() const;

  void set_preparing_token(ScopedOperation&& preparing_token) {
    preparing_token_ = std::move(preparing_token);
  }

  void SubmittedToPreparer() {
    preparing_token_ = ScopedOperation();
  }

  // Returns the request PB associated with this transaction. May be NULL if the transaction's state
  // has been reset.
  virtual const rpc::LightweightMessage* request() const { return nullptr; }

  // Sets the ConsensusRound for this transaction, if this transaction is being executed through the
  // consensus system.
  void set_consensus_round(const scoped_refptr<consensus::ConsensusRound>& consensus_round);

  // Each subclass should provide a way to update the internal reference to the Message* request, so
  // we can avoid copying the request object all the time.
  virtual void UpdateRequestFromConsensusRound() = 0;

  // Returns the ConsensusRound being used, if this transaction is being executed through the
  // consensus system or NULL if it's not.
  consensus::ConsensusRound* consensus_round() {
    return consensus_round_atomic_.load(std::memory_order_acquire);
  }

  const consensus::ConsensusRound* consensus_round() const {
    return consensus_round_atomic_.load(std::memory_order_acquire);
  }

  // Returns a non-null shared pointer to the tablet or an error.
  Result<TabletPtr> tablet_safe() const;

  TabletPtr tablet_nullable() const {
    return tablet_.lock();
  }

  virtual void Release();

  void SetTablet(const TabletPtr& tablet) {
    CHECK_NOTNULL(tablet);
    tablet_ = tablet;
    tablet_is_set_.store(true, std::memory_order_release);
  }

  // Completion callback must be set while the operation is only known to the thread creating it.
  // TODO: construct the operation and set the completion callback using a single factory method.
  template <class F>
  void set_completion_callback(const F& completion_clbk) {
    completion_clbk_ = completion_clbk;
  }

  template <class F>
  void set_completion_callback(F&& completion_clbk) {
    completion_clbk_ = std::move(completion_clbk);
  }

  // Sets the hybrid_time for the transaction
  void set_hybrid_time(const HybridTime& hybrid_time) EXCLUDES(mutex_);

  HybridTime hybrid_time() const EXCLUDES(mutex_) {
    auto hybrid_time = hybrid_time_.load(std::memory_order_acquire);
    DCHECK(hybrid_time.is_valid());
    return hybrid_time;
  }

  HybridTime hybrid_time_even_if_unset() const EXCLUDES(mutex_) {
    return hybrid_time_.load(std::memory_order_acquire);
  }

  bool has_hybrid_time() const {
    return hybrid_time_even_if_unset().is_valid();
  }

  // Returns hybrid time that should be used for storing this operation result in RocksDB.
  // For instance it could be different from hybrid_time() for CDC.
  virtual HybridTime WriteHybridTime() const;

  // The setter acquires the mutex for historical reasons, even though the corresponding getter does
  // not.
  void set_op_id(const OpId& op_id) EXCLUDES(mutex_) {
    std::lock_guard l(mutex_);
    op_id_.store(op_id, std::memory_order_release);
  }

  const OpId op_id() const EXCLUDES(mutex_) {
    return op_id_.load(std::memory_order_acquire);
  }

  void CompleteWithStatus(const Status& status) const EXCLUDES(mutex_);

  // Whether we should use MVCC Manager to track this operation.
  virtual bool use_mvcc() const {
    return false;
  }

  // Initialize operation at leader side.
  // op_id - operation id.
  // committed_op_id - current committed operation id.
  Status AddedToLeader(const OpId& op_id, const OpId& committed_op_id);
  Status AddedToFollower();

  void Aborted(bool was_pending);
  void Replicated(WasPending was_pending);

  virtual ~Operation();

  bool tablet_is_set() { return tablet_is_set_.load(std::memory_order_acquire); }

 private:

  // Actual implementation of Replicated.
  // complete_status could be used to change completion status, i.e. callback will be invoked
  // with this status.
  virtual Status DoReplicated(int64_t leader_term, Status* complete_status) = 0;

  // Actual implementation of Aborted, should return status that should be passed to callback.
  virtual Status DoAborted(const Status& status) = 0;

  // A private version of this transaction's transaction state so that we can use base
  // Operation methods on destructors.
  const OperationType operation_type_;

  // These functions take a tablet shared pointer to avoid handling errors in case the tablet has
  // already been destroyed.
  virtual void AddedAsPending(const TabletPtr& tablet) {}
  virtual void RemovedFromPending(const TabletPtr& tablet) {}

  // This function is OK to call only if log prefix is already initialized.
  std::string GetLogPrefixUnsafe() const NO_THREAD_SAFETY_ANALYSIS { return log_prefix_; }

  // Sets the *tablet shared pointer if it is not already set. Returns true in case of success. Logs
  // the error as DFATAL and returns false in release mode in case the tablet is unavailable.
  __attribute__ ((warn_unused_result)) bool GetTabletOrLogError(
      TabletPtr* tablet, const char* state_str);

  // The tablet that is coordinating this transaction.
  TabletWeakPtr tablet_;
  std::atomic<bool> tablet_is_set_{false};

  // Optional callback to be called once the transaction completes.
  OperationCompletionCallback completion_clbk_;

  mutable std::atomic<bool> complete_{false};

  mutable simple_spinlock mutex_;

  // This transaction's hybrid_time.
  std::atomic<HybridTime> hybrid_time_{HybridTime::kInvalid};

  // This OpId stores the canonical "anchor" OpId for this transaction.
  std::atomic<OpId> op_id_;

  scoped_refptr<consensus::ConsensusRound> consensus_round_ GUARDED_BY(mutex_);
  // This atomic is used to access the consensus round without locking once it has been set.
  std::atomic<consensus::ConsensusRound*> consensus_round_atomic_{nullptr};

  ScopedOperation preparing_token_;

  mutable std::atomic<bool> log_prefix_initialized_{false};
  mutable simple_spinlock log_prefix_mutex_;
  mutable std::string log_prefix_ GUARDED_BY(log_prefix_mutex_);
};

template <class Request>
struct RequestTraits {
  static void SetAllocatedRequest(
      consensus::LWReplicateMsg* replicate, Request* request);

  static Request* MutableRequest(consensus::LWReplicateMsg* replicate);
};

consensus::LWReplicateMsg* CreateReplicateMsg(ThreadSafeArena* arena, OperationType op_type);

// Request here actually means serializable part of operation state.
// When creating new XxxOperation class inherited from OperationBase, it is better to declare
// separate protobuf XxxOperationDataPB wrapping original RPC request and use XxxOperationDataPB as
// a Request. This way it will be easier to add into XxxOperationDataPB more fields that are not
// part of RPC request, but should be stored in Raft log.
template <OperationType op_type, class Request, class Base = Operation>
class OperationBase : public Base {
 public:
  explicit OperationBase(TabletPtr tablet, const Request* request = nullptr)
      : Base(op_type, std::move(tablet)), request_(request) {}

  const Request* request() const override {
    return request_.load(std::memory_order_acquire);
  }

  Request* AllocateRequest() {
    request_holder_ = rpc::MakeSharedMessage<Request>();
    request_.store(request_holder_.get(), std::memory_order_release);
    return request_holder_.get();
  }

  Request* mutable_request() {
    return request_holder_.get();
  }

  void TakeRequest(std::shared_ptr<Request> request) {
    request_holder_ = std::move(request);
    request_.store(request_holder_.get(), std::memory_order_release);
  }

  consensus::ReplicateMsgPtr NewReplicateMsg() override {
    auto result = CreateReplicateMsg(&request_holder_->arena(), op_type);
    RequestTraits<Request>::SetAllocatedRequest(result, request_holder_.get());
    return consensus::ReplicateMsgPtr(request_holder_, result);
  }

  void UpdateRequestFromConsensusRound() override {
    UseRequest(RequestTraits<Request>::MutableRequest(
        Base::consensus_round()->replicate_msg().get()));
  }

 protected:
  void UseRequest(const Request* request) {
    request_.store(request, std::memory_order_release);
  }

 private:
  std::shared_ptr<Request> request_holder_;
  std::atomic<const Request*> request_;
};

class ExclusiveSchemaOperationBase : public Operation {
 public:
  template <class... Args>
  explicit ExclusiveSchemaOperationBase(Args&&... args)
      : Operation(std::forward<Args>(args)...) {}

  // Release the acquired schema lock.
  void ReleasePermitToken();

  void UsePermitToken(ScopedRWOperationPause&& token) {
    permit_token_ = std::move(token);
  }

 private:
  // Used to pause write operations from being accepted while alter is in progress.
  ScopedRWOperationPause permit_token_;
};

template <OperationType operation_type, class Request>
class ExclusiveSchemaOperation
    : public OperationBase<operation_type, Request, ExclusiveSchemaOperationBase> {
 public:
  template <class... Args>
  explicit ExclusiveSchemaOperation(Args&&... args)
      : OperationBase<operation_type, Request, ExclusiveSchemaOperationBase>(
            std::forward<Args>(args)...) {}

  void Release() override {
    ExclusiveSchemaOperationBase::ReleasePermitToken();

    // Make the request NULL since after this operation commits
    // the request may be deleted at any moment.
    OperationBase<operation_type, Request, ExclusiveSchemaOperationBase>::UseRequest(nullptr);
  }
};

template<class LatchPtr, class ResponsePBPtr>
auto MakeLatchOperationCompletionCallback(LatchPtr latch, ResponsePBPtr response) {
  return [latch, response](const Status& status) {
    if (!status.ok()) {
      StatusToPB(status, response->mutable_error()->mutable_status());
    }
    latch->CountDown();
  };
}

OperationCompletionCallback MakeWeakSynchronizerOperationCompletionCallback(
    std::weak_ptr<Synchronizer> synchronizer);

}  // namespace tablet
}  // namespace yb
