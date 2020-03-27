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

#ifndef YB_TABLET_TABLET_SNAPSHOTS_H
#define YB_TABLET_TABLET_SNAPSHOTS_H

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_component.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/status.h"

namespace rocksdb {

class DB;

}

namespace yb {

class PendingOperationCounter;
class rw_semaphore;

namespace tablet {

class TabletSnapshots : public TabletComponent {
 public:
  explicit TabletSnapshots(Tablet* tablet);

  // Restore snapshot for this tablet. In addition to backup/restore, this is used for initial
  // syscatalog RocksDB creation without the initdb overhead.
  CHECKED_STATUS Restore(SnapshotOperationState* tx_state);

  // Perform necessary steps during snapshot operation bootstrap.
  CHECKED_STATUS Bootstrap(SnapshotOperationState* tx_state);

  // Perform necessary steps when snapshot operation replicated.
  CHECKED_STATUS Replicated(SnapshotOperationState* tx_state);

  // Prepares the operation context for a snapshot operation.
  CHECKED_STATUS Prepare(SnapshotOperationState* tx_state);

  //------------------------------------------------------------------------------------------------
  // Create a RocksDB checkpoint in the provided directory. Only used when table_type_ ==
  // YQL_TABLE_TYPE.
  CHECKED_STATUS CreateCheckpoint(const std::string& dir);

  // Returns the location of the last rocksdb checkpoint. Used for tests only.
  std::string TEST_LastRocksDBCheckpointDir() { return TEST_last_rocksdb_checkpoint_dir_; }

  // Create snapshot for this tablet.
  CHECKED_STATUS Create(SnapshotOperationState* tx_state);

  CHECKED_STATUS CreateDirectories(const std::string& rocksdb_dir, FsManager* fs);

  static std::string SnapshotsDirName(const std::string& rocksdb_dir);

  static bool IsTempSnapshotDir(const std::string& dir);

 private:
  // Restore the RocksDB checkpoint from the provided directory.
  // Only used when table_type_ == YQL_TABLE_TYPE.
  CHECKED_STATUS RestoreCheckpoint(
      const std::string& dir, const docdb::ConsensusFrontier& frontier);

  // Delete snapshot for this tablet.
  CHECKED_STATUS Delete(SnapshotOperationState* tx_state);

  // Applies specified snapshot operation.
  CHECKED_STATUS Apply(SnapshotOperationState* tx_state);

  std::string TEST_last_rocksdb_checkpoint_dir_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TABLET_SNAPSHOTS_H
