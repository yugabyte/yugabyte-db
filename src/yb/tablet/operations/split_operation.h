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

#ifndef YB_TABLET_OPERATIONS_SPLIT_OPERATION_H
#define YB_TABLET_OPERATIONS_SPLIT_OPERATION_H

#include <yb/tserver/tserver_service.pb.h>

#include "yb/tablet/operations/operation.h"

namespace yb {
namespace tablet {

class TabletSplitter;

// Operation Context for the SplitTablet operation.
// Keeps track of the Operation states (request, result, ...).
class SplitOperationState : public OperationState {
 public:
  // Creates SplitOperationState for SplitTablet operation for `tablet`. Remembers
  // `tablet_splitter` in order to use it for actual execution of tablet splitting.
  // `consensus_for_abort` is only used when aborting operation.
  SplitOperationState(
      Tablet* tablet, consensus::RaftConsensus* consensus_for_abort,
      TabletSplitter* tablet_splitter, const tserver::SplitTabletRequestPB* request = nullptr);

  const tserver::SplitTabletRequestPB* request() const override { return request_; }

  consensus::RaftConsensus* raft_consensus() { return consensus_; }

  TabletSplitter& tablet_splitter() const { return *tablet_splitter_; }

  void UpdateRequestFromConsensusRound() override;

  std::string ToString() const override;

 private:
  consensus::RaftConsensus* const consensus_;
  TabletSplitter* const tablet_splitter_;
  const tserver::SplitTabletRequestPB* request_;
};

// Executes the SplitTablet operation.
class SplitOperation : public Operation {
 public:
  explicit SplitOperation(std::unique_ptr<SplitOperationState> state)
      : Operation(std::move(state), OperationType::kSplit) {}

  SplitOperationState* state() override {
    return pointer_cast<SplitOperationState*>(Operation::state());
  }

  const SplitOperationState* state() const override {
    return pointer_cast<const SplitOperationState*>(Operation::state());
  }

 private:
  consensus::ReplicateMsgPtr NewReplicateMsg() override;
  CHECKED_STATUS Prepare() override;
  CHECKED_STATUS DoReplicated(int64_t leader_term, Status* complete_status) override;
  CHECKED_STATUS DoAborted(const Status& status) override;
  std::string ToString() const override;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_OPERATIONS_SPLIT_OPERATION_H
