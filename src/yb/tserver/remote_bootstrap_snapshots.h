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

#pragma once

#include "yb/common/entity_ids_types.h"
#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/tserver/remote_bootstrap_session.h"

namespace yb {
namespace tserver {

class RemoteBootstrapSnapshotsComponent : public RemoteBootstrapComponent {
 public:
  RemoteBootstrapSnapshotsComponent(RemoteBootstrapFileDownloader* downloader,
                                    tablet::RaftGroupReplicaSuperBlockPB* new_superblock);

  Status CreateDirectories(const std::string& db_dir, FsManager* fs) override;
  Status Download() override;
  Status Download(const SnapshotId* snapshot_id);

 private:
  FsManager& fs_manager() const {
    return downloader_.fs_manager();
  }

  Status Download(
      const std::string& top_snapshots_dir, const tablet::SnapshotFilePB& snapshot,
      std::unordered_set<SnapshotId>* failed_snapshot_ids);

  RemoteBootstrapFileDownloader& downloader_;
  tablet::RaftGroupReplicaSuperBlockPB& new_superblock_;
};

class RemoteBootstrapSnapshotsSource : public RemoteBootstrapSource {
 public:
  RemoteBootstrapSnapshotsSource(
      tablet::TabletPeerPtr tablet_peer, tablet::RaftGroupReplicaSuperBlockPB* tablet_superblock) :
      tablet_peer_(std::move(tablet_peer)), tablet_superblock_(*tablet_superblock) {}

  static DataIdPB::IdType id_type() {
    return DataIdPB::SNAPSHOT_FILE;
  }

  Status Init() override;

  Status ValidateDataId(const DataIdPB& data_id) override;

  Status GetDataPiece(const DataIdPB& data_id, GetDataPieceInfo* info) override;

 private:
  tablet::TabletPeerPtr tablet_peer_;
  tablet::RaftGroupReplicaSuperBlockPB& tablet_superblock_;
};

} // namespace tserver
} // namespace yb
