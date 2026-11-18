//
// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include "yb/tablet/operations/operation.h"

#include "yb/tablet/operations.messages.h"

namespace yb {
namespace tablet {

class UpdateTxnOperation
    : public OperationBase<OperationType::kUpdateTransaction, LWTransactionStatePB> {
 public:
  template <class... Args>
  explicit UpdateTxnOperation(Args&&... args)
      : OperationBase(std::forward<Args>(args)...) {}

  bool use_mvcc() const override {
    return true;
  }

  // APPLYING (participant) and APPLIED_IN_ALL_INVOLVED_TABLETS (status tablet) are replicated after
  // the client was acked, just as the client's next transaction starts writing to the same tablets.
  bool replication_deferrable() const override;

  // APPLYING on the leader: the requester accepts the apply ack in the RPC response. If set, the
  // participant stores true here instead of sending APPLIED_IN_ONE_OF_INVOLVED_TABLETS.
  void set_applied_in_response(std::shared_ptr<std::atomic<bool>> value) {
    applied_in_response_ = std::move(value);
  }

 private:
  std::shared_ptr<std::atomic<bool>> applied_in_response_;

  Result<TransactionCoordinator*> transaction_coordinator() const;
  Status Prepare(IsLeaderSide is_leader_side) override;
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;
  Status DoAborted(const Status& status) override;
};

} // namespace tablet
} // namespace yb
