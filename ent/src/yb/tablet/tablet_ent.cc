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
using yb::util::PendingOperationCounter;

Status Tablet::PrepareForSnapshotOp(SnapshotOperationState* tx_state) {
  tx_state->AcquireSchemaLock(&schema_lock_);

  return Status::OK();
}

Status Tablet::CreateSnapshot(SnapshotOperationState* tx_state) {
  ScopedPendingOperation scoped_read_operation(&pending_op_counter_);
  RETURN_NOT_OK(scoped_read_operation);

  Status s = regular_db_->Flush(rocksdb::FlushOptions());
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "RocksDB flush status: " << s;
    return s.CloneAndPrepend("Unable to flush RocksDB");
  }

  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  RETURN_NOT_OK_PREPEND(metadata_->fs_manager()->CreateDirIfMissingAndSync(top_snapshots_dir),
      Substitute("Unable to create snapshots directory $0", top_snapshots_dir));

  Env* const env = metadata_->fs_manager()->env();
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir,
                                               tx_state->request()->snapshot_id());
  // Delete previous snapshot in the same directory if it exists.
  if (env->FileExists(snapshot_dir)) {
    LOG_WITH_PREFIX(INFO) << "Deleting old snapshot dir " << snapshot_dir;
    RETURN_NOT_OK_PREPEND(env->DeleteRecursively(snapshot_dir),
                          "Cannot recursively delete old snapshot dir " + snapshot_dir);
    RETURN_NOT_OK_PREPEND(env->SyncDir(top_snapshots_dir),
                          "Cannot sync top snapshots dir " + top_snapshots_dir);
  }

  LOG_WITH_PREFIX(INFO) << "Started tablet snapshot creation for tablet " << tablet_id()
                        << " in folder " << snapshot_dir;

  const string tmp_snapshot_dir = snapshot_dir + kTempSnapshotDirSuffix;

  // Delete temp directory if it exists.
  if (env->FileExists(tmp_snapshot_dir)) {
    LOG_WITH_PREFIX(INFO) << "Deleting old temp snapshot dir " << tmp_snapshot_dir;
    RETURN_NOT_OK_PREPEND(env->DeleteRecursively(tmp_snapshot_dir),
                          "Cannot recursively delete old temp snapshot dir " + tmp_snapshot_dir);
    RETURN_NOT_OK_PREPEND(env->SyncDir(top_snapshots_dir),
                          "Cannot sync top snapshots dir " + top_snapshots_dir);
  }

  bool exit_on_failure = true;
  // Delete snapshot (RocksDB checkpoint) directories on exit.
  BOOST_SCOPE_EXIT(env, &exit_on_failure, &snapshot_dir, &tmp_snapshot_dir, &top_snapshots_dir,
                   this_) {
    bool do_sync = false;

    if (env->FileExists(tmp_snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env->DeleteRecursively(tmp_snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG(WARNING)
            << this_->LogPrefix() << "Cannot recursively delete temp snapshot dir "
            << tmp_snapshot_dir << ": " << deletion_status;
      }
    }

    if (exit_on_failure && env->FileExists(snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env->DeleteRecursively(snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG(WARNING) << this_->LogPrefix() << "Cannot recursively delete snapshot dir "
                     << snapshot_dir << ": " << deletion_status;
      }
    }

    if (do_sync) {
      const Status sync_status = env->SyncDir(top_snapshots_dir);
      if (PREDICT_FALSE(!sync_status.ok())) {
        LOG(WARNING) << this_->LogPrefix() << "Cannot sync top snapshots dir " << top_snapshots_dir
                     << ": " << sync_status;
      }
    }
  } BOOST_SCOPE_EXIT_END;

  // Note: checkpoint::CreateCheckpoint() calls DisableFileDeletions()/EnableFileDeletions()
  //       for the RocksDB object.
  s = CreateCheckpoint(tmp_snapshot_dir);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Cannot create RocksDB checkpoint: " << s;
    return s.CloneAndPrepend("Cannot create RocksDB checkpoint");
  }

  RETURN_NOT_OK_PREPEND(env->RenameFile(tmp_snapshot_dir, snapshot_dir),
                        Substitute("Cannot rename temp snapshot dir $0 to $1",
                                   tmp_snapshot_dir,
                                   snapshot_dir));
  RETURN_NOT_OK_PREPEND(env->SyncDir(top_snapshots_dir),
                        Substitute("Cannot sync top snapshots dir $0", top_snapshots_dir));

  // Record the fact that we've executed the "create snapshot" Raft operation. We are not forcing
  // the flushed frontier to have this exact value, although in practice it will, since this is the
  // latest operation we've ever executed in this Raft group. This way we keep the current value
  // of history cutoff.
  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  RETURN_NOT_OK(ModifyFlushedFrontier(frontier, rocksdb::FrontierModificationMode::kUpdate));

  LOG_WITH_PREFIX(INFO) << "Complete snapshot creation for tablet " << tablet_id()
                        << " in folder " << snapshot_dir;

  exit_on_failure = false;
  return Status::OK();
}

Status Tablet::RestoreSnapshot(SnapshotOperationState* tx_state) {
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

  const rocksdb::SequenceNumber sequence_number = regular_db_->GetLatestSequenceNumber();
  const string db_dir = regular_db_->GetName();
  const std::string intents_db_dir = intents_db_ ? intents_db_->GetName() : std::string();

  // Destroy DB object.
  // TODO: snapshot current DB and try to restore it in case of failure.
  intents_db_.reset();
  regular_db_.reset();

  rocksdb::Options rocksdb_options;
  docdb::InitRocksDBOptions(&rocksdb_options, tablet_id(), rocksdb_statistics_, tablet_options_);

  Status s = rocksdb::DestroyDB(db_dir, rocksdb_options);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Cannot cleanup db files in directory " << db_dir << ": " << s;
    return STATUS(IllegalState, "Cannot cleanup db files", s.ToString());
  }

  if (!intents_db_dir.empty()) {
    s = rocksdb::DestroyDB(intents_db_dir, rocksdb_options);
    if (PREDICT_FALSE(!s.ok())) {
      LOG_WITH_PREFIX(WARNING) << "Cannot cleanup db files in directory " << intents_db_dir << ": "
                               << s;
      return STATUS(IllegalState, "Cannot cleanup intents db files", s.ToString());
    }
  }

  s = rocksdb::CopyDirectory(rocksdb_options.env, dir, db_dir, rocksdb::CreateIfMissing::kTrue);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Copy checkpoint files status: " << s;
    return STATUS(IllegalState, "Unable to copy checkpoint files", s.ToString());
  }

  if (!intents_db_dir.empty()) {
    auto intents_tmp_dir = JoinPathSegments(dir, tablet::kIntentsSubdir);
    rocksdb_options.env->RenameFile(intents_db_dir, intents_db_dir);
  }

  // Reopen database from copied checkpoint.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  s = OpenKeyValueTablet(DisableCompactions::kTrue);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed tablet db opening from checkpoint: " << s;
    return s;
  }

  docdb::ConsensusFrontier final_frontier = frontier;
  rocksdb::UserFrontierPtr checkpoint_flushed_frontier = regular_db_->GetFlushedFrontier();

  // The history cutoff we are setting after restoring to this snapshot is determined by the
  // compactions that were done in the checkpoint, not in the old state of RocksDB in this replica.
  if (checkpoint_flushed_frontier) {
    final_frontier.set_history_cutoff(
        down_cast<docdb::ConsensusFrontier&>(*checkpoint_flushed_frontier).history_cutoff());
  }

  s = ModifyFlushedFrontier(final_frontier, rocksdb::FrontierModificationMode::kForce);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed tablet DB setting flushed op id: " << s;
    return s;
  }

  LOG_WITH_PREFIX(INFO) << "Checkpoint restored from " << dir;
  LOG_WITH_PREFIX(INFO) << "Sequence numbers: old=" << sequence_number
            << ", restored=" << regular_db_->GetLatestSequenceNumber();

  LOG_WITH_PREFIX(INFO) << "Re-enabling compactions";
  s = EnableCompactions();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to enable compactions after restoring a checkpoint";
    return s;
  }

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
  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir,
                                               tx_state->request()->snapshot_id());

  std::lock_guard<std::mutex> lock(create_checkpoint_lock_);
  Env* const env = metadata_->fs_manager()->env();

  if (env->FileExists(snapshot_dir)) {
    const Status deletion_status = env->DeleteRecursively(snapshot_dir);
    if (PREDICT_FALSE(!deletion_status.ok())) {
      LOG_WITH_PREFIX(WARNING) << "Cannot recursively delete snapshot dir " << snapshot_dir
                               << ": " << deletion_status;
    }

    const Status sync_status = env->SyncDir(top_snapshots_dir);
    if (PREDICT_FALSE(!sync_status.ok())) {
      LOG_WITH_PREFIX(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir
                               << ": " << sync_status;
    }
  }

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  // Here we are just recording the fact that we've executed the "delete snapshot" Raft operation
  // so that it won't get replayed if we crash. No need to force the flushed frontier to be the
  // exact value set above.
  RETURN_NOT_OK(ModifyFlushedFrontier(frontier, rocksdb::FrontierModificationMode::kUpdate));

  LOG_WITH_PREFIX(INFO) << "Complete snapshot deletion on tablet " << tablet_id()
                        << " in folder " << snapshot_dir;

  return Status::OK();
}

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
