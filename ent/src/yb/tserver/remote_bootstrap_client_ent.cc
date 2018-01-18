// Copyright (c) YugaByte, Inc.

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

Status RemoteBootstrapClient::DownloadSnapshotFiles() {
  CHECK(started_);
  CHECK(downloaded_rocksdb_files_);

  const string& rocksdb_dir = new_superblock_->rocksdb_dir();
  const string top_snapshots_dir = JoinPathSegments(rocksdb_dir,
                                                    tablet::enterprise::kSnapshotsDirName);

  // Create the snapshots directory first.
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateDirIfMissingAndSync(top_snapshots_dir),
                        Substitute("Failed to create & sync top snapshots directory $0",
                                   top_snapshots_dir));

  for (auto const& file_pb : new_superblock_->snapshot_files()) {
    const string snapshot_dir = JoinPathSegments(top_snapshots_dir, file_pb.snapshot_id());

    RETURN_NOT_OK_PREPEND(fs_manager_->CreateDirIfMissingAndSync(snapshot_dir),
                          Substitute("Failed to create & sync snapshot directory $0",
                                     snapshot_dir));

    WritableFileOptions opts;
    opts.sync_on_close = true;
    gscoped_ptr<WritableFile> rocksdb_file;
    const string file_path = JoinPathSegments(snapshot_dir, file_pb.name());

    VLOG_WITH_PREFIX(2) << "Downloading snapshot file " << file_path;
    RETURN_NOT_OK(fs_manager_->env()->NewWritableFile(opts, file_path, &rocksdb_file));

    DataIdPB data_id;
    data_id.set_type(DataIdPB::SNAPSHOT_FILE);
    data_id.set_snapshot_id(file_pb.snapshot_id());
    data_id.set_file_name(file_pb.name());
    RETURN_NOT_OK_PREPEND(DownloadFile(data_id, rocksdb_file.get()),
                          Substitute("Unable to download snapshot file $0", file_path));
  }

  downloaded_snapshot_files_ = true;
  return Status::OK();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
