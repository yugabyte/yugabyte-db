// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/tablet/operations/snapshot_operation.h"

////////////////////////////////////////////////////////////
// Tablet
////////////////////////////////////////////////////////////

namespace yb {
namespace tablet {
namespace enterprise {

using strings::Substitute;

Status Tablet::PrepareForCreateSnapshot(SnapshotOperationState* tx_state) {
  // Create snapshot must run when no reads/writes are in progress.
  tx_state->AcquireSchemaLock(&schema_lock_);

  return Status::OK();
}

Status Tablet::CreateSnapshot(SnapshotOperationState* tx_state) {
  // Prevent any concurrent flushes.
  std::lock_guard<Semaphore> lock(rowsets_flush_sem_);

  // The table type must be checked on the Master side.
  DCHECK(table_type_ == TableType::YQL_TABLE_TYPE);

  const string snapshots_dir = JoinPathSegments(metadata_->rocksdb_dir(), kSnapshotsDirName);
  RETURN_NOT_OK_PREPEND(metadata_->fs_manager()->CreateDirIfMissing(snapshots_dir),
      Substitute("Unable to create snapshots diretory $0", snapshots_dir));

  const string snapshot_dir = JoinPathSegments(snapshots_dir, tx_state->request()->snapshot_id());

  const Status s = CreateCheckpoint(snapshot_dir);
  VLOG(1) << "Complete checkpoint creation for tablet " << tablet_id()
          << " with result " << s.ToString()
          << " in folder " << metadata_->rocksdb_dir();
  return s;
}

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
