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
