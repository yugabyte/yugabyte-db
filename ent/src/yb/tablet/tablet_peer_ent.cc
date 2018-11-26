// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_peer.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/operations/snapshot_operation.h"

namespace yb {
namespace tablet {
namespace enterprise {

std::unique_ptr<Operation> TabletPeer::CreateOperation(consensus::ReplicateMsg* replicate_msg) {
  if (replicate_msg->op_type() == consensus::SNAPSHOT_OP) {
      DCHECK(replicate_msg->has_snapshot_request()) << "SNAPSHOT_OP replica"
          " transaction must receive an TabletSnapshotOpRequestPB";
      return std::make_unique<SnapshotOperation>(
          std::make_unique<SnapshotOperationState>(tablet()));
  }

  return super::CreateOperation(replicate_msg);
}

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
