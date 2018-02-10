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
    return STATUS(IllegalState, "Unable to flush rocksdb", s.ToString());
  }

  const string top_snapshots_dir = Tablet::SnapshotsDirName(metadata_->rocksdb_dir());
  RETURN_NOT_OK_PREPEND(metadata_->fs_manager()->CreateDirIfMissing(top_snapshots_dir),
      Substitute("Unable to create snapshots diretory $0", top_snapshots_dir));

  const string snapshot_dir = JoinPathSegments(top_snapshots_dir,
                                               tx_state->request()->snapshot_id());

  // Note: checkpoint::CreateCheckpoint() calls DisableFileDeletions()/EnableFileDeletions()
  //       for rocksdb object.
  s = CreateCheckpoint(snapshot_dir);
  VLOG(1) << "Complete checkpoint creation for tablet " << tablet_id()
          << " with result " << s << " in folder " << snapshot_dir;

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(tx_state->op_id());
  frontier.set_hybrid_time(tx_state->hybrid_time());
  RETURN_NOT_OK(SetFlushedFrontier(frontier));

  return s;
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

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
