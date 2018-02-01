// Copyright (c) YugaByte, Inc.

#include "yb/tserver/remote_bootstrap_session.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

namespace yb {
namespace tserver {
namespace enterprise {

using std::string;
using std::vector;

using strings::Substitute;
using tablet::enterprise::Tablet;
using tablet::TabletMetadata;
using tablet::TabletPeer;

Status RemoteBootstrapSession::InitSnapshotFiles() {
  const scoped_refptr<TabletMetadata>& metadata = tablet_peer_->tablet_metadata();
  tablet_superblock_.clear_snapshot_files();

  // Add snapshot files to tablet superblock.
  const string top_snapshots_dir = Tablet::SnapshotsDirName(tablet_superblock_.rocksdb_dir());

  vector<string> snapshots;
  if (metadata->fs_manager()->env()->FileExists(top_snapshots_dir)) {
    RETURN_NOT_OK_PREPEND(metadata->fs_manager()->ListDir(top_snapshots_dir, &snapshots),
                          Substitute("Unable to list directory $0", top_snapshots_dir));
  }

  for (const string& snapshot_id : snapshots) {
    if (snapshot_id == "." || snapshot_id == "..") {
      continue;
    }

    const string snapshot_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);
    vector<string> files;
    RETURN_NOT_OK_PREPEND(metadata->fs_manager()->ListDir(snapshot_dir, &files),
                          Substitute("Unable to list directory $0", snapshot_dir));

    for (const string& file : files) {
      if (file == "." || file == "..") {
        continue;
      }

      const string path = JoinPathSegments(snapshot_dir, file);
      const uint64_t file_size = VERIFY_RESULT_PREPEND(
          metadata->fs_manager()->env()->GetFileSize(path),
          Substitute("Unable to get file size for file $0", path));

      auto snapshot_file_pb = tablet_superblock_.mutable_snapshot_files()->Add();
      auto& file_pb = *snapshot_file_pb->mutable_file();
      snapshot_file_pb->set_snapshot_id(snapshot_id);
      file_pb.set_name(file);
      file_pb.set_size_bytes(file_size);
      file_pb.set_inode(VERIFY_RESULT(metadata->fs_manager()->env()->GetFileINode(path)));
    }
  }

  return Status::OK();
}

Status RemoteBootstrapSession::GetSnapshotFilePiece(const std::string snapshot_id,
                                                    const std::string file_name,
                                                    uint64_t offset,
                                                    int64_t client_maxlen,
                                                    std::string* data,
                                                    int64_t* log_file_size,
                                                    RemoteBootstrapErrorPB::Code* error_code) {
  const string snapshots_dir = Tablet::SnapshotsDirName(tablet_superblock_.rocksdb_dir());
  return GetFilePiece(
      JoinPathSegments(snapshots_dir, snapshot_id), file_name, offset,
      client_maxlen, data, log_file_size, error_code);
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
