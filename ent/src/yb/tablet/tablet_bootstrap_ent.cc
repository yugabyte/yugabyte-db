// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_bootstrap.h"

#include "yb/consensus/log_anchor_registry.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/operations/snapshot_operation.h"

namespace yb {
namespace tablet {
namespace enterprise {

using consensus::OperationType;
using consensus::CommitMsg;
using consensus::ReplicateMsg;
using tserver::CreateTabletSnapshotRequestPB;

Status TabletBootstrap::HandleOperation(OperationType op_type,
                                        ReplicateMsg* replicate,
                                        const CommitMsg* commit) {
  if (op_type == consensus::SNAPSHOT_OP) {
    return PlayCreateTabletSnapshotRequest(replicate, commit);
  }

  return super::HandleOperation(op_type, replicate, commit);
}

Status TabletBootstrap::PlayCreateTabletSnapshotRequest(ReplicateMsg* replicate_msg,
                                                        const CommitMsg* commit_msg) {
  CreateTabletSnapshotRequestPB* snapshot = replicate_msg->mutable_snapshot_request();

  SnapshotOperationState tx_state(nullptr, snapshot);

  RETURN_NOT_OK(tablet_->PrepareForCreateSnapshot(&tx_state));

  // Apply the create snapshot to the tablet.
  RETURN_NOT_OK_PREPEND(tablet_->CreateSnapshot(&tx_state), "Failed to CreateSnapshot:");

  return commit_msg == nullptr ? Status::OK() : AppendCommitMsg(*commit_msg);
}

} // namespace enterprise
} // namespace tablet
} // namespace yb
