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

          s = meta_->fs_manager()->env()->SyncDir(top_snapshots_dir);
          if (!s.ok()) {
            LOG(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir << ": " << s;
          }
        }
      }
    }
  }

  return super::OpenTablet();
}

} // namespace enterprise
} // namespace tablet
} // namespace yb
