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
using tablet::RaftGroupMetadata;
using tablet::TabletPeer;

CHECKED_STATUS AddDirToSnapshotFiles(
    const std::string& dir, const std::string& prefix, const std::string& snapshot_id,
    google::protobuf::RepeatedPtrField<tablet::SnapshotFilePB>* out) {
  auto files = VERIFY_RESULT_PREPEND(
      Env::Default()->GetChildren(dir, ExcludeDots::kTrue),
      Format("Unable to list directory $0", dir));

  for (const string& file : files) {
    LOG(INFO) << "File: " << file;
    const auto path = JoinPathSegments(dir, file);
    const auto fname = prefix.empty() ? file : JoinPathSegments(prefix, file);

    if (VERIFY_RESULT(Env::Default()->IsDirectory(path))) {
      RETURN_NOT_OK(AddDirToSnapshotFiles(path, fname, snapshot_id, out));
      continue;
    }

    const uint64_t file_size = VERIFY_RESULT_PREPEND(
        Env::Default()->GetFileSize(path),
        Format("Unable to get file size for file $0", path));

    auto snapshot_file_pb = out->Add();
    auto& file_pb = *snapshot_file_pb->mutable_file();
    snapshot_file_pb->set_snapshot_id(snapshot_id);
    file_pb.set_name(fname);
    file_pb.set_size_bytes(file_size);
    file_pb.set_inode(VERIFY_RESULT(Env::Default()->GetFileINode(path)));
  }

  return Status::OK();
}

Status RemoteBootstrapSession::InitSnapshotFiles() {
  const scoped_refptr<RaftGroupMetadata>& metadata = tablet_peer_->tablet_metadata();
  auto* kv_store = tablet_superblock_.mutable_kv_store();
  kv_store->clear_snapshot_files();

  // Add snapshot files to tablet superblock.
  const string top_snapshots_dir = Tablet::SnapshotsDirName(kv_store->rocksdb_dir());
  vector<string> snapshots;

  if (metadata->fs_manager()->env()->FileExists(top_snapshots_dir)) {
    snapshots = VERIFY_RESULT_PREPEND(
        metadata->fs_manager()->ListDir(top_snapshots_dir),
        Substitute("Unable to list directory $0", top_snapshots_dir));
  }

  for (const string& dir_name : snapshots) {
    const string snapshot_dir = JoinPathSegments(top_snapshots_dir, dir_name);
    if (Tablet::IsTempSnapshotDir(snapshot_dir)) {
      continue;
    }

    // Ignore any non-directories (folder check-sum files, for example).
    if (!VERIFY_RESULT(Env::Default()->IsDirectory(snapshot_dir))) {
      continue;
    }

    RETURN_NOT_OK(AddDirToSnapshotFiles(
        snapshot_dir, "", dir_name, kv_store->mutable_snapshot_files()));
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
  const string snapshots_dir =
      Tablet::SnapshotsDirName(tablet_superblock_.kv_store().rocksdb_dir());
  return GetFilePiece(
      JoinPathSegments(snapshots_dir, snapshot_id), file_name, offset,
      client_maxlen, data, log_file_size, error_code);
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
