// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_bootstrap.h"

#include "yb/consensus/log_anchor_registry.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/operations/snapshot_operation.h"

namespace yb {
namespace tablet {
namespace enterprise {

using std::string;
using std::vector;

using consensus::OperationType;
using consensus::ReplicateMsg;
using tserver::TabletSnapshotOpRequestPB;

Result<bool> TabletBootstrap::OpenTablet() {
  // Disk clean-up: deleting temporary/incomplete snapshots.
  const string top_snapshots_dir = Tablet::SnapshotsDirName(meta_->rocksdb_dir());

  if (meta_->fs_manager()->env()->FileExists(top_snapshots_dir)) {
    vector<string> snapshot_dirs;
    Status s = meta_->fs_manager()->env()->GetChildren(
        top_snapshots_dir, ExcludeDots::kTrue, &snapshot_dirs);

    if (!s.ok()) {
      LOG(WARNING) << "Cannot get list of snapshot directories in "
                   << top_snapshots_dir << ": " << s;
    } else {
      for (const string& dir_name : snapshot_dirs) {
        const string snapshot_dir = JoinPathSegments(top_snapshots_dir, dir_name);

        if (Tablet::IsTempSnapshotDir(snapshot_dir)) {
          LOG(INFO) << "Deleting old temporary snapshot directory " << snapshot_dir;

          s = meta_->fs_manager()->env()->DeleteRecursively(snapshot_dir);
          if (!s.ok()) {
            LOG(WARNING) << "Cannot delete old temporary snapshot directory "
                         << snapshot_dir << ": " << s;
          }
        }
      }
    }
  }

  return super::OpenTablet();
}

Status TabletBootstrap::HandleOperation(OperationType op_type,
                                        ReplicateMsg* replicate) {
  if (op_type == consensus::SNAPSHOT_OP) {
    return PlayTabletSnapshotOpRequest(replicate);
  }

  return super::HandleOperation(op_type, replicate);
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
