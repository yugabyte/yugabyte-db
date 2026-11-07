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

#include <condition_variable>

#include "yb/common/entity_ids_types.h"

#include "yb/consensus/consensus_round.h"

#include "yb/tablet/operation_filter.h"
#include "yb/tablet/operations.messages.h"
#include "yb/tablet/operations/operation.h"

#include "yb/tserver/tserver_admin.pb.h"

namespace yb {
namespace tablet {

class TabletSplitter;

// Operation Context for the SplitTablet operation.
// Keeps track of the Operation states (request, result, ...).
// Executes the SplitTablet operation.
class SplitOperation
    : public OperationBase<OperationType::kSplit, LWSplitTabletRequestPB>,
      public OperationFilter {
 public:
  SplitOperation(
      TabletPtr tablet, TabletSplitter* tablet_splitter,
      const LWSplitTabletRequestPB* request = nullptr)
      : OperationBase(std::move(tablet), request),
        tablet_splitter_(*CHECK_NOTNULL(tablet_splitter)) {}

  TabletSplitter& tablet_splitter() const { return tablet_splitter_; }

  static bool ShouldAllowOpAfterSplitTablet(consensus::OperationType op_type);

  static Status RejectionStatus(
      OpId split_op_id, OpId rejected_op_id, consensus::OperationType op_type,
      const std::vector<TabletId>& children);

  bool use_mvcc() const override {
    return true;
  }

 private:
  Status Prepare(IsLeaderSide is_leader_side) override;
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;
  Status DoAborted(const Status& status) override;
  void AddedAsPending(const TabletPtr& tablet) override;
  void RemovedFromPending(const TabletPtr& tablet) override;

  Status CheckOperationAllowed(
      const OpId& id, consensus::OperationType op_type) const override;

  TabletSplitter& tablet_splitter_;

  // Whether AddedAsPending registered this operation as the tablet's operation filter. The abort
  // path can run without the operation ever having been added as pending (see
  // RemovedFromPending).
  //
  // Plain bool: the accesses are never concurrent. A pre-replication abort runs on the
  // submitting thread after Operation::AddedToLeader returns (the failure propagates up the
  // same call stack to OperationDriver::HandleFailure), and post-replication completion
  // happens-after the append that ran AddedAsPending, ordered by the ReplicaState lock and
  // the driver's lock_; replication_state_ makes Replicated/Aborted mutually exclusive.
  bool filter_registered_ = false;
};

}  // namespace tablet
}  // namespace yb
