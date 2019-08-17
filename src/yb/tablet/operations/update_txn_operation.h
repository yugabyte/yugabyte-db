//
// Copyright (c) YugaByte, Inc.
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
//

#ifndef YB_TABLET_OPERATIONS_UPDATE_TXN_OPERATION_H
#define YB_TABLET_OPERATIONS_UPDATE_TXN_OPERATION_H

#include <yb/tserver/tserver_service.pb.h>
#include "yb/tablet/transaction_coordinator.h"

#include "yb/tablet/operations/operation.h"

namespace yb {
namespace tablet {

class TransactionCoordinator;

class UpdateTxnOperationState : public OperationState {
 public:
  UpdateTxnOperationState(Tablet* tablet, const tserver::TransactionStatePB* request)
      : OperationState(tablet), request_(request) {}

  explicit UpdateTxnOperationState(Tablet* tablet)
      : UpdateTxnOperationState(tablet, nullptr) {}

  const tserver::TransactionStatePB* request() const override {
    return request_.load(std::memory_order_acquire);
  }

  void TakeRequest(tserver::TransactionStatePB* request) {
    request_holder_.reset(new tserver::TransactionStatePB);
    request_.store(request_holder_.get(), std::memory_order_release);
    request_holder_->Swap(request);
  }

  std::string ToString() const override;

 private:
  void UpdateRequestFromConsensusRound() override;

  std::unique_ptr<tserver::TransactionStatePB> request_holder_;
  std::atomic<const tserver::TransactionStatePB*> request_;
};

class UpdateTxnOperation : public Operation {
 public:
  explicit UpdateTxnOperation(std::unique_ptr<UpdateTxnOperationState> state)
      : Operation(std::move(state), OperationType::kUpdateTransaction) {}

  UpdateTxnOperationState* state() override {
    return down_cast<UpdateTxnOperationState*>(Operation::state());
  }

  const UpdateTxnOperationState* state() const override {
    return down_cast<const UpdateTxnOperationState*>(Operation::state());
  }

 private:
  TransactionCoordinator& transaction_coordinator() const;

  consensus::ReplicateMsgPtr NewReplicateMsg() override;
  CHECKED_STATUS Prepare() override;
  void DoStart() override;
  CHECKED_STATUS DoReplicated(int64_t leader_term, Status* complete_status) override;
  CHECKED_STATUS DoAborted(const Status& status) override;
  std::string ToString() const override;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_OPERATIONS_UPDATE_TXN_OPERATION_H
