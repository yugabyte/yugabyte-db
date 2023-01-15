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

#include "yb/tablet/operations/split_operation.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_error.h"
#include "yb/consensus/consensus_round.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_splitter.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"

using namespace std::literals;

namespace yb {
namespace tablet {

template <>
void RequestTraits<LWSplitTabletRequestPB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWSplitTabletRequestPB* request) {
  replicate->ref_split_request(request);
}

template <>
LWSplitTabletRequestPB* RequestTraits<LWSplitTabletRequestPB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_split_request();
}

void SplitOperation::AddedAsPending(const TabletPtr& tablet) {
  tablet->RegisterOperationFilter(this);
}

void SplitOperation::RemovedFromPending(const TabletPtr& tablet) {
  tablet->UnregisterOperationFilter(this);
}

Status SplitOperation::RejectionStatus(
    OpId split_op_id, OpId rejected_op_id, consensus::OperationType op_type,
    const TabletId& child1, const TabletId& child2) {
  auto status = STATUS_EC_FORMAT(
      IllegalState, consensus::ConsensusError(consensus::ConsensusErrorPB::TABLET_SPLIT),
      "Tablet split has been $0, operation $1 $2 should be retried to new tablets",
      split_op_id.empty() ? "applied" : Format("added to Raft log ($0)", split_op_id),
      OperationType_Name(op_type), rejected_op_id);
  return status.CloneAndAddErrorCode(SplitChildTabletIdsData({child1, child2}));
}

// Returns whether Raft operation of op_type is allowed to be added to Raft log of the tablet
// for which split tablet Raft operation has been already added to Raft log.
bool SplitOperation::ShouldAllowOpAfterSplitTablet(const consensus::OperationType op_type) {
  // Old tablet remains running for remote bootstrap purposes for some time and could receive
  // Raft operations.

  // If new OperationType is added, make an explicit deliberate decision whether new op type
  // should be allowed to be added into Raft log for old (pre-split) tablet.
  switch (op_type) {
      // We allow NO_OP, so old tablet can have leader changes in case of re-elections.
    case consensus::NO_OP: FALLTHROUGH_INTENDED;
      // We allow SNAPSHOT_OP, so old tablet can be restored.
    case consensus::SNAPSHOT_OP: FALLTHROUGH_INTENDED;
      // Allow CHANGE_CONFIG_OP, so the old tablet replicas can be moved between tservers while we
      // keep the tablet available.
    case consensus::CHANGE_CONFIG_OP:
      return true;
    case consensus::UNKNOWN_OP: FALLTHROUGH_INTENDED;
    case consensus::WRITE_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_METADATA_OP: FALLTHROUGH_INTENDED;
    case consensus::HISTORY_CUTOFF_OP: FALLTHROUGH_INTENDED;
    case consensus::UPDATE_TRANSACTION_OP: FALLTHROUGH_INTENDED;
    case consensus::TRUNCATE_OP: FALLTHROUGH_INTENDED;
    case consensus::SPLIT_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_AUTO_FLAGS_CONFIG_OP:
      return false;
  }
  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, op_type);
}

Status SplitOperation::CheckOperationAllowed(
    const OpId& id, consensus::OperationType op_type) const {
  if (id == op_id() || ShouldAllowOpAfterSplitTablet(op_type)) {
    return Status::OK();
  }

  // TODO(tsplit): for optimization - include new tablet IDs into response, so client knows
  // earlier where to retry.
  // TODO(tsplit): test - check that split_op_id_ is correctly aborted.
  // TODO(tsplit): test - check that split_op_id_ is correctly restored during bootstrap.
  return RejectionStatus(
      op_id(), id, op_type, request()->new_tablet1_id().ToBuffer(),
      request()->new_tablet2_id().ToBuffer());
}

Status SplitOperation::Prepare(IsLeaderSide is_leader_side) {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

Status SplitOperation::DoAborted(const Status& status) {
  VLOG_WITH_PREFIX(2) << "DoAborted";
  return status;
}

Status SplitOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  VLOG_WITH_PREFIX(2) << "Apply";
  return tablet_splitter().ApplyTabletSplit(
      this, /* raft_log = */ nullptr, /* committed_raft_config = */ boost::none);
}

}  // namespace tablet
}  // namespace yb
