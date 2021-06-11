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

#include "yb/consensus/consensus_error.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_splitter.h"

using namespace std::literals;

namespace yb {
namespace tablet {

SplitOperationState::SplitOperationState(
    Tablet* tablet, TabletSplitter* tablet_splitter, const tserver::SplitTabletRequestPB* request)
    : OperationState(tablet),
      tablet_splitter_(tablet_splitter),
      request_(request) {
  if (!tablet_splitter) {
    LOG(FATAL) << "tablet_splitter is not set for tablet " << tablet->tablet_id();
  }
}

std::string SplitOperationState::ToString() const {
  auto req = request();
  return Format("SplitOperationState [$0]", !req ? "(none)"s : req->ShortDebugString());
}

void SplitOperationState::UpdateRequestFromConsensusRound() {
  request_ = consensus_round()->replicate_msg()->mutable_split_request();
}

void SplitOperationState::AddedAsPending() {
  tablet()->RegisterOperationFilter(this);
}

void SplitOperationState::RemovedFromPending() {
  tablet()->UnregisterOperationFilter(this);
}

Status SplitOperationState::RejectionStatus(
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
bool SplitOperationState::ShouldAllowOpAfterSplitTablet(const consensus::OperationType op_type) {
  // Old tablet remains running for remote bootstrap purposes for some time and could receive
  // Raft operations.

  // If new OperationType is added, make an explicit deliberate decision whether new op type
  // should be allowed to be added into Raft log for old (pre-split) tablet.
  switch (op_type) {
    case consensus::NO_OP:
      // We allow NO_OP, so old tablet can have leader changes in case of re-elections.
      return true;
    case consensus::UNKNOWN_OP: FALLTHROUGH_INTENDED;
    case consensus::WRITE_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_METADATA_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_CONFIG_OP: FALLTHROUGH_INTENDED;
    case consensus::HISTORY_CUTOFF_OP: FALLTHROUGH_INTENDED;
    case consensus::UPDATE_TRANSACTION_OP: FALLTHROUGH_INTENDED;
    case consensus::SNAPSHOT_OP: FALLTHROUGH_INTENDED;
    case consensus::TRUNCATE_OP: FALLTHROUGH_INTENDED;
    case consensus::SPLIT_OP:
      return false;
  }
  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, op_type);
}

Status SplitOperationState::CheckOperationAllowed(
    const OpId& id, consensus::OperationType op_type) const {
  if (id == op_id() || ShouldAllowOpAfterSplitTablet(op_type)) {
    return Status::OK();
  }

  // TODO(tsplit): for optimization - include new tablet IDs into response, so client knows
  // earlier where to retry.
  // TODO(tsplit): test - check that split_op_id_ is correctly aborted.
  // TODO(tsplit): test - check that split_op_id_ is correctly restored during bootstrap.
  return RejectionStatus(
      op_id(), id, op_type, request()->new_tablet1_id(), request()->new_tablet2_id());
}

consensus::ReplicateMsgPtr SplitOperation::NewReplicateMsg() {
  auto result = std::make_shared<consensus::ReplicateMsg>();
  result->set_op_type(consensus::SPLIT_OP);
  *result->mutable_split_request() = *state()->request();
  return result;
}

Status SplitOperation::Prepare() {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

Status SplitOperation::DoAborted(const Status& status) {
  VLOG_WITH_PREFIX(2) << "DoAborted";
  return status;
}

Status SplitOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  VLOG_WITH_PREFIX(2) << "Apply";
  return state()->tablet_splitter().ApplyTabletSplit(state(), /* raft_log= */ nullptr);
}

std::string SplitOperation::ToString() const {
  return Format("SplitOperation { state: $0 }", *state());
}

}  // namespace tablet
}  // namespace yb
