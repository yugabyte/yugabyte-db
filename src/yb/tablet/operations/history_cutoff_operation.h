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

#ifndef YB_TABLET_OPERATIONS_HISTORY_CUTOFF_OPERATION_H
#define YB_TABLET_OPERATIONS_HISTORY_CUTOFF_OPERATION_H

#include "yb/tablet/operations/operation.h"

namespace yb {
namespace tablet {

class HistoryCutoffOperationState : public OperationStateBase<consensus::HistoryCutoffPB> {
 public:
  template <class... Args>
  explicit HistoryCutoffOperationState(Args&&... args)
      : OperationStateBase(std::forward<Args>(args)...) {}

  CHECKED_STATUS Replicated(int64_t leader_term);

 private:
  void UpdateRequestFromConsensusRound() override;
};

class HistoryCutoffOperation : public Operation {
 public:
  explicit HistoryCutoffOperation(std::unique_ptr<HistoryCutoffOperationState> state)
      : Operation(std::move(state), OperationType::kHistoryCutoff) {}

  HistoryCutoffOperationState* state() override {
    return down_cast<HistoryCutoffOperationState*>(Operation::state());
  }

  const HistoryCutoffOperationState* state() const override {
    return down_cast<const HistoryCutoffOperationState*>(Operation::state());
  }

 private:
  consensus::ReplicateMsgPtr NewReplicateMsg() override;
  CHECKED_STATUS Prepare() override;
  CHECKED_STATUS DoReplicated(int64_t leader_term, Status* complete_status) override;
  CHECKED_STATUS DoAborted(const Status& status) override;
  std::string ToString() const override;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_OPERATIONS_HISTORY_CUTOFF_OPERATION_H
