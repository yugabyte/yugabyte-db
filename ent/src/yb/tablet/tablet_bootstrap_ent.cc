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
using tserver::TabletSnapshotOpRequestPB;

Status TabletBootstrap::HandleOperation(OperationType op_type,
                                        ReplicateMsg* replicate,
                                        const CommitMsg* commit) {
  if (op_type == consensus::SNAPSHOT_OP) {
    return PlayTabletSnapshotOpRequest(replicate);
  }

  return super::HandleOperation(op_type, replicate, commit);
}

Status TabletBootstrap::PlayTabletSnapshotOpRequest(ReplicateMsg* replicate_msg) {
  TabletSnapshotOpRequestPB* const snapshot = replicate_msg->mutable_snapshot_request();

  SnapshotOperationState tx_state(nullptr, snapshot);

  RETURN_NOT_OK(tablet_->PrepareForSnapshotOp(&tx_state));
  bool handled = false;

  // Apply the snapshot operation to the tablet.
  switch (snapshot->operation()) {
    case TabletSnapshotOpRequestPB::CREATE: {
      handled = true;
      RETURN_NOT_OK_PREPEND(tablet_->CreateSnapshot(&tx_state), "Failed to CreateSnapshot:");
      break;
    }
    case TabletSnapshotOpRequestPB::RESTORE: {
      handled = true;
      RETURN_NOT_OK_PREPEND(tablet_->RestoreSnapshot(&tx_state), "Failed to RestoreSnapshot:");
      break;
    }
    case TabletSnapshotOpRequestPB::UNKNOWN: break; // Not handled.
  }

  if (!handled) {
    FATAL_INVALID_ENUM_VALUE(tserver::TabletSnapshotOpRequestPB::Operation,
                             snapshot->operation());
  }
  return Status::OK();
}

} // namespace enterprise
} // namespace tablet
} // namespace yb
