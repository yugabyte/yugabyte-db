// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet-internal.h"

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

namespace yb {
namespace tablet {
namespace enterprise {

using strings::Substitute;
using yb::util::ScopedPendingOperation;

Status Tablet::PrepareForSnapshotOp(SnapshotOperationState* tx_state) {
  // Create snapshot must run when no reads/writes are in progress.
  tx_state->AcquireSchemaLock(&schema_lock_);

  return Status::OK();
}

Status Tablet::CreateSnapshot(SnapshotOperationState* tx_state) {
  // Prevent any concurrent flushes.
  std::lock_guard<Semaphore> lock(rowsets_flush_sem_);

  // The table type must be checked on the Master side.
  DCHECK_EQ(table_type_, TableType::YQL_TABLE_TYPE);

  Status s = rocksdb_->Flush(rocksdb::FlushOptions());
  if (!s.ok()) {
    LOG(WARNING) << "Rocksdb flush status: " << s.ToString();
    return STATUS(IllegalState, "Unable to flush rocksdb", s.ToString());
  }

  const string snapshots_dir = JoinPathSegments(metadata_->rocksdb_dir(), kSnapshotsDirName);
  RETURN_NOT_OK_PREPEND(metadata_->fs_manager()->CreateDirIfMissing(snapshots_dir),
      Substitute("Unable to create snapshots diretory $0", snapshots_dir));

  const string snapshot_dir = JoinPathSegments(snapshots_dir, tx_state->request()->snapshot_id());

  // Note: checkpoint::CreateCheckpoint() calls DisableFileDeletions()/EnableFileDeletions()
  //       for rocksdb object.
  s = CreateCheckpoint(snapshot_dir);
  VLOG(1) << "Complete checkpoint creation for tablet " << tablet_id()
          << " with result " << s.ToString()
          << " in folder " << metadata_->rocksdb_dir();
  return s;
}

Status Tablet::RestoreSnapshot(SnapshotOperationState* tx_state) {
  // Prevent any concurrent flushes.
  std::lock_guard<Semaphore> lock(rowsets_flush_sem_);

  // The table type must be checked on the Master side.
  DCHECK_EQ(table_type_, TableType::YQL_TABLE_TYPE);

  const string snapshots_dir = JoinPathSegments(metadata_->rocksdb_dir(), kSnapshotsDirName);
  const string snapshot_dir = JoinPathSegments(snapshots_dir, tx_state->request()->snapshot_id());

  const Status s = RestoreCheckpoint(snapshot_dir);
  VLOG(1) << "Complete checkpoint restoring for tablet " << tablet_id()
          << " with result " << s.ToString()
          << " in folder " << metadata_->rocksdb_dir();
  return s;
}

Status Tablet::RestoreCheckpoint(const std::string& dir) {
  GUARD_AGAINST_ROCKSDB_SHUTDOWN;

  CHECK_NE(table_type_, TableType::KUDU_COLUMNAR_TABLE_TYPE);

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);

  const rocksdb::SequenceNumber sequence_number = rocksdb_->GetLatestSequenceNumber();
  const string db_dir = rocksdb_->GetName();

  // Destroy DB object.
  // TODO: snapshot current DB and try to restore it in case of failure.
  rocksdb_.reset(nullptr);

  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, tablet_id(), rocksdb_statistics_, tablet_options_);

  rocksdb::Status s = rocksdb::DestroyDB(db_dir, rocksdb_options);
  if (!s.ok()) {
    LOG(WARNING) << "Cannot cleanup db files in directory " << db_dir << ": "
                 << s.ToString();
    return STATUS(IllegalState, "Cannot cleanup db files", s.ToString());
  }

  s = rocksdb::CopyDirectory(rocksdb_options.env, dir, db_dir, rocksdb::CreateIfMissing::kFalse);
  if (!s.ok()) {
    LOG(WARNING) << "Copy checkpoint files status: " << s.ToString();
    return STATUS(IllegalState, "Unable to copy checkpoint files", s.ToString());
  }

  // Reopen database from copied checkpoint.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  const Status db_open_status = OpenKeyValueTablet();
  if (db_open_status.ok()) {
    LOG(INFO) << "Checkpoint restored from " << dir;
    LOG(INFO) << "Sequence numbers: old=" << sequence_number
              << ", restored=" << rocksdb_->GetLatestSequenceNumber();
  } else {
    LOG(WARNING) << "Failed tablet db opening from checkpoint: " << db_open_status.ToString();
  }
  return db_open_status;
}

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
