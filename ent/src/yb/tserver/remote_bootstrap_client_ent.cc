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

#include "yb/tserver/remote_bootstrap_client.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tserver/remote_bootstrap.pb.h"

namespace yb {
namespace tserver {
namespace enterprise {

using std::string;

using strings::Substitute;
using tablet::TabletStatusListener;
using tablet::enterprise::Tablet;

Status RemoteBootstrapClient::FetchAll(TabletStatusListener* status_listener) {
  RETURN_NOT_OK(super::FetchAll(status_listener));

  RETURN_NOT_OK(DownloadSnapshotFiles());
  return Status::OK();
}

Status RemoteBootstrapClient::Finish() {
  CHECK(started_);
  CHECK(downloaded_snapshot_files_);

  return super::Finish();
}

Status RemoteBootstrapClient::CreateTabletDirectories(const string& db_dir, FsManager* fs) {
  RETURN_NOT_OK(super::CreateTabletDirectories(db_dir, fs));

  const string top_snapshots_dir = Tablet::SnapshotsDirName(db_dir);
  // Create the snapshots directory.
  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissingAndSync(top_snapshots_dir),
                        Substitute("Failed to create & sync top snapshots directory $0",
                                   top_snapshots_dir));
  return Status::OK();
}

Status RemoteBootstrapClient::DownloadSnapshotFiles() {
  CHECK(started_);
  CHECK(downloaded_rocksdb_files_);

  const auto& kv_store = new_superblock_->kv_store();
  const string& rocksdb_dir = kv_store.rocksdb_dir();
  const string top_snapshots_dir = Tablet::SnapshotsDirName(rocksdb_dir);
  // Create the snapshots directory first.
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateDirIfMissingAndSync(top_snapshots_dir),
                        Substitute("Failed to create & sync top snapshots directory $0",
                                   top_snapshots_dir));

  DataIdPB data_id;
  data_id.set_type(DataIdPB::SNAPSHOT_FILE);
  for (auto const& file_pb : kv_store.snapshot_files()) {
    const string snapshot_dir = JoinPathSegments(top_snapshots_dir, file_pb.snapshot_id());

    RETURN_NOT_OK_PREPEND(fs_manager_->CreateDirIfMissingAndSync(snapshot_dir),
                          Substitute("Failed to create & sync snapshot directory $0",
                                     snapshot_dir));

    const string file_path = JoinPathSegments(snapshot_dir, file_pb.file().name());
    data_id.set_snapshot_id(file_pb.snapshot_id());
    RETURN_NOT_OK(DownloadFile(file_pb.file(), snapshot_dir, &data_id));
  }

  downloaded_snapshot_files_ = true;
  return Status::OK();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
