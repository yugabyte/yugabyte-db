// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet.h"

#include <boost/scope_exit.hpp>

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/util/stopwatch.h"

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

using namespace std::literals;

namespace yb {
namespace tablet {
namespace enterprise {

using strings::Substitute;
using yb::util::ScopedPendingOperation;
using yb::util::ScopedPendingOperationPause;
using yb::util::PendingOperationCounter;

Status Tablet::PrepareForSnapshotOp(SnapshotOperationState* tx_state) {
  // Create snapshot must run when no reads/writes are in progress.
  tx_state->AcquireSchemaLock(&schema_lock_);

  return Status::OK();
}

Status Tablet::CreateSnapshot(SnapshotOperationState* tx_state) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  // The table type must be checked on the Master side.
  DCHECK_EQ(table_type_, TableType::YQL_TABLE_TYPE);

  Status s = rocksdb_->Flush(rocksdb::FlushOptions());
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Rocksdb flush status: " << s;
    return s.CloneAndPrepend("Unable to flush rocksdb");
  }

  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  RETURN_NOT_OK_PREPEND(metadata_->fs_manager()->CreateDirIfMissingAndSync(top_snapshots_dir),
      Substitute("Unable to create snapshots directory $0", top_snapshots_dir));

  Env* const env = metadata_->fs_manager()->env();
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir,
                                               tx_state->request()->snapshot_id());
  // Delete previous snapshot in the same directory if it exists.
  if (env->FileExists(snapshot_dir)) {
    LOG(INFO) << "Deleting old snapshot dir " << snapshot_dir;
    RETURN_NOT_OK_PREPEND(env->DeleteRecursively(snapshot_dir),
                          "Cannot recursively delete old snapshot dir " + snapshot_dir);
    RETURN_NOT_OK_PREPEND(env->SyncDir(top_snapshots_dir),
                          "Cannot sync top snapshots dir " + top_snapshots_dir);
  }

  LOG(INFO) << "Started tablet snapshot creation for tablet " << tablet_id()
            << " in folder " << snapshot_dir;

  const string tmp_snapshot_dir = snapshot_dir + kTempSnapshotDirSuffix;

  // Delete temp directory if it exists.
  if (env->FileExists(tmp_snapshot_dir)) {
    LOG(INFO) << "Deleting old temp snapshot dir " << tmp_snapshot_dir;
    RETURN_NOT_OK_PREPEND(env->DeleteRecursively(tmp_snapshot_dir),
                          "Cannot recursively delete old temp snapshot dir " + tmp_snapshot_dir);
    RETURN_NOT_OK_PREPEND(env->SyncDir(top_snapshots_dir),
                          "Cannot sync top snapshots dir " + top_snapshots_dir);
  }

  bool exit_on_failure = true;
  // Delete snapshot (RocksDB checkpoint) directories on exit.
  BOOST_SCOPE_EXIT(env, &exit_on_failure, &snapshot_dir, &tmp_snapshot_dir, &top_snapshots_dir) {
    bool do_sync = false;

    if (env->FileExists(tmp_snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env->DeleteRecursively(tmp_snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG(WARNING) << "Cannot recursively delete temp snapshot dir " << tmp_snapshot_dir
                     << ": " << deletion_status;
      }
    }

    if (exit_on_failure && env->FileExists(snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env->DeleteRecursively(snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG(WARNING) << "Cannot recursively delete snapshot dir " << snapshot_dir
                     << ": " << deletion_status;
      }
    }

    if (do_sync) {
      const Status sync_status = env->SyncDir(top_snapshots_dir);
      if (PREDICT_FALSE(!sync_status.ok())) {
        LOG(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir
                     << ": " << sync_status;
      }
    }
  } BOOST_SCOPE_EXIT_END;

  // Note: checkpoint::CreateCheckpoint() calls DisableFileDeletions()/EnableFileDeletions()
  //       for rocksdb object.
  s = CreateCheckpoint(tmp_snapshot_dir);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Cannot create db checkpoint: " << s;
    return s.CloneAndPrepend("Cannot create db checkpoint");
  }

  RETURN_NOT_OK_PREPEND(env->RenameFile(tmp_snapshot_dir, snapshot_dir),
                        Substitute("Cannot rename temp snapshot dir $0 to $1",
                                   tmp_snapshot_dir,
                                   snapshot_dir));
  RETURN_NOT_OK_PREPEND(env->SyncDir(top_snapshots_dir),
                        Substitute("Cannot sync top snapshots dir $0", top_snapshots_dir));

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  RETURN_NOT_OK(SetFlushedFrontier(frontier));

  LOG(INFO) << "Complete snapshot creation for tablet " << tablet_id()
            << " in folder " << snapshot_dir;

  exit_on_failure = false;
  return Status::OK();
}

Status Tablet::RestoreSnapshot(SnapshotOperationState* tx_state) {
  // The table type must be checked on the Master side.
  DCHECK_EQ(table_type_, TableType::YQL_TABLE_TYPE);

  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir,
                                               tx_state->request()->snapshot_id());

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  const Status s = RestoreCheckpoint(snapshot_dir, frontier);
  VLOG(1) << "Complete checkpoint restoring for tablet " << tablet_id()
          << " with result " << s << " in folder " << metadata_->rocksdb_dir();
  return s;
}

Status Tablet::RestoreCheckpoint(const std::string& dir, const docdb::ConsensusFrontier& frontier) {
  auto op_pause = PauseReadWriteOperations();
  RETURN_NOT_OK(op_pause);

  // Check if tablet is in shutdown mode.
  if (IsShutdownRequested()) {
    return STATUS(IllegalState, "Tablet was shut down");
  }

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);

  const rocksdb::SequenceNumber sequence_number = rocksdb_->GetLatestSequenceNumber();
  const string db_dir = rocksdb_->GetName();

  // Destroy DB object.
  // TODO: snapshot current DB and try to restore it in case of failure.
  rocksdb_.reset(nullptr);

  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, tablet_id(), rocksdb_statistics_, tablet_options_);

  Status s = rocksdb::DestroyDB(db_dir, rocksdb_options);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Cannot cleanup db files in directory " << db_dir << ": " << s;
    return STATUS(IllegalState, "Cannot cleanup db files", s.ToString());
  }

  s = rocksdb::CopyDirectory(rocksdb_options.env, dir, db_dir, rocksdb::CreateIfMissing::kTrue);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Copy checkpoint files status: " << s;
    return STATUS(IllegalState, "Unable to copy checkpoint files", s.ToString());
  }

  // Reopen database from copied checkpoint.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  s = OpenKeyValueTablet();
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Failed tablet db opening from checkpoint: " << s;
    return s;
  }

  s = SetFlushedFrontier(frontier);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Failed tablet db setting flushed op id: " << s;
    return s;
  }

  LOG(INFO) << "Checkpoint restored from " << dir;
  LOG(INFO) << "Sequence numbers: old=" << sequence_number
            << ", restored=" << rocksdb_->GetLatestSequenceNumber();

  return Status::OK();
}

Status Tablet::CreateTabletDirectories(const string& db_dir, FsManager* fs) {
  // Create the tablet directories first.
  RETURN_NOT_OK(super::CreateTabletDirectories(db_dir, fs));

  const string top_snapshots_dir = Tablet::SnapshotsDirName(db_dir);
  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissingAndSync(top_snapshots_dir),
                        Substitute("Unable to create snapshots directory $0",
                                   top_snapshots_dir));
  return Status::OK();
}

Status Tablet::DeleteSnapshot(SnapshotOperationState* tx_state) {
  // The table type must be checked on the Master side.
  DCHECK_EQ(table_type_, TableType::YQL_TABLE_TYPE);

  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir,
                                               tx_state->request()->snapshot_id());

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);
  Env* const env = metadata_->fs_manager()->env();

  if (env->FileExists(snapshot_dir)) {
    const Status deletion_status = env->DeleteRecursively(snapshot_dir);
    if (PREDICT_FALSE(!deletion_status.ok())) {
      LOG(WARNING) << "Cannot recursively delete snapshot dir " << snapshot_dir
                   << ": " << deletion_status;
    }

    const Status sync_status = env->SyncDir(top_snapshots_dir);
    if (PREDICT_FALSE(!sync_status.ok())) {
      LOG(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir
                   << ": " << sync_status;
    }
  }

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  RETURN_NOT_OK(SetFlushedFrontier(frontier));

  LOG(INFO) << "Complete snapshot deletion on tablet " << tablet_id()
            << " in folder " << snapshot_dir;

  return Status::OK();
}

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
