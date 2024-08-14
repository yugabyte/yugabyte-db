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

#pragma once

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/tablet/restore_util.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_component.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/opid.h"
#include "yb/util/status_fwd.h"

namespace rocksdb {

class DB;

}

namespace yb {

class Env;
class FsManager;
class RWOperationCounter;
class rw_semaphore;

namespace tablet {

class TabletRestorePatch;

YB_DEFINE_ENUM(CreateIntentsCheckpointIn, (kSubDir)(kUseIntentsDbSuffix));

struct CreateSnapshotData {
  HybridTime snapshot_hybrid_time;
  HybridTime hybrid_time;
  OpId op_id;
  std::string snapshot_dir;
  SnapshotScheduleId schedule_id;
};

class TabletSnapshots : public TabletComponent {
 public:
  explicit TabletSnapshots(Tablet* tablet);

  // Create snapshot for this tablet.
  Status Create(SnapshotOperation* operation);

  Status Create(const CreateSnapshotData& data);

  // Restore snapshot for this tablet. In addition to backup/restore, this is used for initial
  // syscatalog RocksDB creation without the initdb overhead.
  Status Restore(SnapshotOperation* operation);

  // Delete snapshot for this tablet.
  Status Delete(const SnapshotOperation& operation);

  Status RestoreFinished(SnapshotOperation* operation);

  // Prepares the operation context for a snapshot operation.
  Status Prepare(SnapshotOperation* operation);

  Result<std::string> RestoreToTemporary(const TxnSnapshotId& snapshot_id, HybridTime restore_at);

  //------------------------------------------------------------------------------------------------
  // Create a RocksDB checkpoint in the provided directory. Only used when table_type_ ==
  // YQL_TABLE_TYPE.
  // use_subdir_for_intents specifies whether to create intents DB checkpoint inside
  // <dir>/<kIntentsSubdir> or <dir>.<kIntentsDBSuffix>
  Status CreateCheckpoint(
      const std::string& dir,
      CreateIntentsCheckpointIn create_intents_checkpoint_in =
          CreateIntentsCheckpointIn::kUseIntentsDbSuffix);

  // Returns the location of the last rocksdb checkpoint. Used for tests only.
  std::string TEST_LastRocksDBCheckpointDir() { return TEST_last_rocksdb_checkpoint_dir_; }

  Status CreateDirectories(const std::string& rocksdb_dir, FsManager* fs);

  static std::string SnapshotsDirName(const std::string& rocksdb_dir);

  static bool IsTempSnapshotDir(const std::string& dir);

 private:
  struct RestoreMetadata;
  struct ColocatedTableMetadata;

  // Restore the RocksDB checkpoint from the provided directory.
  // Only used when table_type_ == YQL_TABLE_TYPE.
  Status RestoreCheckpoint(
      const std::string& snapshot_dir, HybridTime restore_at, const RestoreMetadata& metadata,
      const docdb::ConsensusFrontier& frontier, bool is_pitr_restore, const OpId& op_id);

  // Applies specified snapshot operation.
  Status Apply(SnapshotOperation* operation);

  Status CleanupSnapshotDir(const std::string& dir);
  Env& env();
  FsManager* fs_manager();

  Status RestorePartialRows(SnapshotOperation* operation);

  Result<TabletRestorePatch> GenerateRestoreWriteBatch(
      const tserver::TabletSnapshotOpRequestPB& request, docdb::DocWriteBatch* write_batch);

  Result<docdb::CotableIdsMap> GetCotableIdsMap(const std::string& snapshot_dir);

  std::string TEST_last_rocksdb_checkpoint_dir_;
};

struct SequencesDataInfo {
  SequencesDataInfo(std::optional<int64_t> last_value, std::optional<bool> is_called)
    : last_value(last_value), is_called(is_called) {}

  SequencesDataInfo(const SequencesDataInfo& other) {
    last_value = other.last_value;
    is_called = other.is_called;
  }

  std::optional<int64_t> last_value = std::nullopt;
  std::optional<bool> is_called = std::nullopt;
};

std::ostream& operator<<(std::ostream& out, const SequencesDataInfo& value);

class TabletRestorePatch : public RestorePatch {
 public:
  TabletRestorePatch(
      FetchState* existing_state, FetchState* restoring_state,
      docdb::DocWriteBatch* doc_batch, tablet::TableInfo* table_info, int64_t db_oid)
      : RestorePatch(existing_state, restoring_state, doc_batch, table_info),
        db_oid_(db_oid) {}

  Status Finish() override;

 private:
  Result<bool> ShouldSkipEntry(const Slice& key, const Slice& value) override;

  Status ProcessCommonEntry(
      const Slice& key, const Slice& existing_value, const Slice& restoring_value) override;

  Status ProcessRestoringOnlyEntry(
      const Slice& restoring_key, const Slice& restoring_value) override;

  Status ProcessExistingOnlyEntry(
      const Slice& existing_key, const Slice& existing_value) override;

  Status UpdateColumnValueInMap(const Slice& key, const Slice& value,
      std::map<docdb::DocKey, SequencesDataInfo>* key_to_seq_info_map);

  int64_t db_oid_;

  std::map<docdb::DocKey, SequencesDataInfo> existing_key_to_seq_info_map_;
  std::map<docdb::DocKey, SequencesDataInfo> restoring_key_to_seq_info_map_;
};

} // namespace tablet
} // namespace yb
