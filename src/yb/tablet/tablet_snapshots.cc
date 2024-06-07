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

#include "yb/tablet/tablet_snapshots.h"

#include <boost/algorithm/string/predicate.hpp>

#include "yb/qlexpr/index.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_write_batch.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/rocksdb/utilities/checkpoint.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/restore_util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/debug-util.h"
#include "yb/util/file_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/operation_counter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::string;

using namespace std::literals;

DEFINE_test_flag(int32, delay_tablet_split_metadata_restore_secs, 0,
                 "How much time in secs to delay restoring tablet split metadata after restoring "
                 "checkpoint.");

DEFINE_test_flag(int32, delay_tablet_export_metadata_ms, 0,
                 "How much time in milliseconds to delay before exporting tablet metadata during "
                 "snapshot creation.");

DEFINE_RUNTIME_int32(max_wait_for_aborting_transactions_during_restore_ms, 200,
                     "How much time in milliseconds to wait for tablet transactions to abort while "
                     "applying the raft restore operation to a tablet.");
TAG_FLAG(max_wait_for_aborting_transactions_during_restore_ms, advanced);

namespace yb {
namespace tablet {

namespace {

const std::string kTempSnapshotDirSuffix = ".tmp";
const std::string kTabletMetadataFile = "tablet.metadata";

std::string TabletMetadataFile(const std::string& dir) {
  return JoinPathSegments(dir, kTabletMetadataFile);
}

} // namespace

struct TabletSnapshots::RestoreMetadata {
  boost::optional<Schema> schema;
  boost::optional<qlexpr::IndexMap> index_map;
  uint32_t schema_version;
  bool hide;
  google::protobuf::RepeatedPtrField<ColocatedTableMetadata> colocated_tables_metadata;
};

struct TabletSnapshots::ColocatedTableMetadata {
  boost::optional<Schema> schema;
  boost::optional<qlexpr::IndexMap> index_map;
  uint32_t schema_version;
  std::string table_id;
};

TabletSnapshots::TabletSnapshots(Tablet* tablet) : TabletComponent(tablet) {}

std::string TabletSnapshots::SnapshotsDirName(const std::string& rocksdb_dir) {
  return rocksdb_dir + kSnapshotsDirSuffix;
}

bool TabletSnapshots::IsTempSnapshotDir(const std::string& dir) {
  return boost::ends_with(dir, kTempSnapshotDirSuffix);
}

Status TabletSnapshots::Prepare(SnapshotOperation* operation) {
  return Status::OK();
}

Status TabletSnapshots::Create(SnapshotOperation* operation) {
  return Create(CreateSnapshotData {
    .snapshot_hybrid_time = HybridTime::FromPB(operation->request()->snapshot_hybrid_time()),
    .hybrid_time = operation->hybrid_time(),
    .op_id = operation->op_id(),
    .snapshot_dir = VERIFY_RESULT(operation->GetSnapshotDir()),
    .schedule_id = TryFullyDecodeSnapshotScheduleId(operation->request()->schedule_id()),
  });
}

Status TabletSnapshots::Create(const CreateSnapshotData& data) {
  LongOperationTracker long_operation_tracker("Create snapshot", 5s);

  ScopedRWOperation scoped_read_operation(&pending_op_counter_blocking_rocksdb_shutdown_start());
  RETURN_NOT_OK(scoped_read_operation);

  Status s = regular_db().Flush(rocksdb::FlushOptions());
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "RocksDB flush status: " << s;
    return s.CloneAndPrepend("Unable to flush RocksDB");
  }

  const std::string& snapshot_dir = data.snapshot_dir;

  Env* const env = metadata().fs_manager()->env();
  auto snapshot_hybrid_time = data.snapshot_hybrid_time;
  auto is_transactional_snapshot = snapshot_hybrid_time.is_valid();

  // Delete previous snapshot in the same directory if it exists.
  RETURN_NOT_OK(CleanupSnapshotDir(snapshot_dir));

  LOG_WITH_PREFIX(INFO) << "Started tablet snapshot creation in folder: " << snapshot_dir;

  const auto top_snapshots_dir = DirName(snapshot_dir);
  const auto tmp_snapshot_dir = snapshot_dir + kTempSnapshotDirSuffix;

  // Delete temp directory if it exists.
  RETURN_NOT_OK(CleanupSnapshotDir(tmp_snapshot_dir));

  bool exit_on_failure = true;
  // Delete snapshot (RocksDB checkpoint) directories on exit.
  auto se = ScopeExit(
      [this, env, &exit_on_failure, &snapshot_dir, &tmp_snapshot_dir, &top_snapshots_dir] {
    bool do_sync = false;

    if (env->FileExists(tmp_snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env->DeleteRecursively(tmp_snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG_WITH_PREFIX(WARNING)
            << "Cannot recursively delete temp snapshot dir "
            << tmp_snapshot_dir << ": " << deletion_status;
      }
    }

    if (exit_on_failure && env->FileExists(snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env->DeleteRecursively(snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG_WITH_PREFIX(WARNING)
            << "Cannot recursively delete snapshot dir " << snapshot_dir << ": " << deletion_status;
      }
    }

    if (do_sync) {
      const Status sync_status = env->SyncDir(top_snapshots_dir);
      if (PREDICT_FALSE(!sync_status.ok())) {
        LOG_WITH_PREFIX(WARNING)
            << "Cannot sync top snapshots dir " << top_snapshots_dir << ": " << sync_status;
      }
    }
  });

  DisableSchemaGC disable_schema_gc(tablet().metadata());

  // Note: checkpoint::CreateCheckpoint() calls DisableFileDeletions()/EnableFileDeletions()
  //       for the RocksDB object.
  s = CreateCheckpoint(tmp_snapshot_dir);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Cannot create RocksDB checkpoint: " << s;
    return s.CloneAndPrepend("Cannot create RocksDB checkpoint");
  }

  if (is_transactional_snapshot) {
    rocksdb::Options rocksdb_options;
    tablet().InitRocksDBOptions(&rocksdb_options, LogPrefix());
    docdb::RocksDBPatcher patcher(tmp_snapshot_dir, rocksdb_options);

    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.SetHybridTimeFilter(std::nullopt, snapshot_hybrid_time));
  }

  AtomicFlagSleepMs(&FLAGS_TEST_delay_tablet_export_metadata_ms);

  bool need_flush = data.schedule_id && tablet().metadata()->AddSnapshotSchedule(data.schedule_id);

  RETURN_NOT_OK(tablet().metadata()->SaveTo(TabletMetadataFile(tmp_snapshot_dir)));

  RETURN_NOT_OK_PREPEND(
      env->RenameFile(tmp_snapshot_dir, snapshot_dir),
      Format("Cannot rename temp snapshot dir $0 to $1", tmp_snapshot_dir, snapshot_dir));
  RETURN_NOT_OK_PREPEND(
      env->SyncDir(top_snapshots_dir),
      Format("Cannot sync top snapshots dir $0", top_snapshots_dir));

  if (need_flush) {
    RETURN_NOT_OK(tablet().metadata()->Flush());
  }

  LOG_WITH_PREFIX(INFO) << "Complete snapshot creation in folder: " << snapshot_dir
                        << ", snapshot hybrid time: " << snapshot_hybrid_time;

  exit_on_failure = false;
  return Status::OK();
}

Env& TabletSnapshots::env() {
  return *metadata().fs_manager()->env();
}

Status TabletSnapshots::CleanupSnapshotDir(const std::string& dir) {
  auto& env = this->env();
  if (!env.FileExists(dir)) {
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Deleting old snapshot dir " << dir;
  RETURN_NOT_OK_PREPEND(env.DeleteRecursively(dir),
                        "Cannot recursively delete old snapshot dir " + dir);
  auto top_snapshots_dir = DirName(dir);
  RETURN_NOT_OK_PREPEND(env.SyncDir(top_snapshots_dir),
                        "Cannot sync top snapshots dir " + top_snapshots_dir);

  return Status::OK();
}

Status TabletSnapshots::Restore(SnapshotOperation* operation) {
  const std::string snapshot_dir = VERIFY_RESULT(operation->GetSnapshotDir());
  const auto& request = *operation->request();
  auto restore_at = HybridTime::FromPB(request.snapshot_hybrid_time());
  auto restoration_id = TryFullyDecodeTxnSnapshotRestorationId(request.restoration_id());

  // This logic is to partially restore the sequences tablet. Because the sequences tablet is shared
  // among all YSQL dbs we do not want to restore the entire tablet.
  if (request.db_oid()) {
    RETURN_NOT_OK(RestorePartialRows(operation));
    return tablet().RestoreStarted(restoration_id);
  }

  VLOG_WITH_PREFIX_AND_FUNC(1) << YB_STRUCT_TO_STRING(snapshot_dir, restore_at);
  auto deadline =
      CoarseMonoClock::Now() +
      MonoDelta::FromMilliseconds(FLAGS_max_wait_for_aborting_transactions_during_restore_ms);
  WARN_NOT_OK(
      tablet().AbortSQLTransactions(deadline),
      Format("Cannot abort transactions for tablet $0 during restore", tablet().tablet_id()));

  if (!snapshot_dir.empty()) {
    RETURN_NOT_OK_PREPEND(
        FileExists(&rocksdb_env(), snapshot_dir),
        Format("Snapshot directory does not exist: $0", snapshot_dir));
  }

  docdb::ConsensusFrontier frontier;
  frontier.set_op_id(operation->op_id());
  frontier.set_hybrid_time(operation->hybrid_time());
  RestoreMetadata restore_metadata;
  if (request.has_schema()) {
    restore_metadata.schema.emplace();
    RETURN_NOT_OK(SchemaFromPB(
        request.schema().ToGoogleProtobuf(), restore_metadata.schema.get_ptr()));
    restore_metadata.index_map.emplace(ToRepeatedPtrField(request.indexes()));
    restore_metadata.schema_version = request.schema_version();
    restore_metadata.hide = request.hide();
  }

  for (const auto& entry : request.colocated_tables_metadata()) {
    auto* table_metadata = restore_metadata.colocated_tables_metadata.Add();
    table_metadata->schema_version = entry.schema_version();
    table_metadata->schema.emplace();
    RETURN_NOT_OK(SchemaFromPB(
        entry.schema().ToGoogleProtobuf(), table_metadata->schema.get_ptr()));
    table_metadata->index_map.emplace(ToRepeatedPtrField(entry.indexes()));
    table_metadata->table_id = entry.table_id().ToBuffer();
  }
  Status s = RestoreCheckpoint(
      snapshot_dir, restore_at, restore_metadata, frontier,
      !request.schedule_id().empty(), operation->op_id());
  VLOG_WITH_PREFIX(1) << "Complete checkpoint restoring with result " << s << " in folder: "
                      << metadata().rocksdb_dir();
  int32 delay_time_secs = GetAtomicFlag(&FLAGS_TEST_delay_tablet_split_metadata_restore_secs);
  if (delay_time_secs > 0) {
    SleepFor(MonoDelta::FromSeconds(delay_time_secs));
  }
  if (s.ok() && restoration_id) {
    s = tablet().RestoreStarted(restoration_id);
  }
  return s;
}

Status TabletSnapshots::RestorePartialRows(SnapshotOperation* operation) {
  ScopedRWOperation pending_op(&pending_op_counter_blocking_rocksdb_shutdown_start());
  docdb::DocWriteBatch write_batch(
      tablet().doc_db(), docdb::InitMarkerBehavior::kOptional, pending_op, nullptr);

  auto restore_patch = VERIFY_RESULT(GenerateRestoreWriteBatch(
      operation->request()->ToGoogleProtobuf(), &write_batch));
  if (restore_patch.TotalTickerCount() != 0 || VLOG_IS_ON(3)) {
    LOG(INFO) << "PITR: Sequences data tablet: " << tablet().tablet_id()
              << ", " << restore_patch.TickersToString();
  }

  WriteToRocksDB(
      &write_batch, operation->WriteHybridTime(), operation->op_id(), &tablet(), std::nullopt);
  return Status::OK();
}

Result<TabletRestorePatch> TabletSnapshots::GenerateRestoreWriteBatch(
    const tserver::TabletSnapshotOpRequestPB& request, docdb::DocWriteBatch* write_batch) {
  FetchState existing_state(tablet().doc_db(), ReadHybridTime::Max());
  RETURN_NOT_OK(existing_state.SetPrefix(""));

  // The non-empty snapshot id means the snapshot being used to restore contains this sequences data
  // tablet, so we construct a restore patch based on db_oid. Otherwise, we clean up current state.
  if (!request.snapshot_id().empty()) {
    // Restore snapshot to temporary folder and create rocksdb out of it.
    LOG_WITH_PREFIX(INFO) << "Restoring only rows with db oid " << request.db_oid();
    auto snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(request.snapshot_id()));
    auto restore_at = HybridTime::FromPB(request.snapshot_hybrid_time());
    auto dir = VERIFY_RESULT(RestoreToTemporary(snapshot_id, restore_at));
    rocksdb::Options rocksdb_options;
    std::string log_prefix = LogPrefix();
    // Remove ": " to patch suffix.
    log_prefix.erase(log_prefix.size() - 2);
    tablet().InitRocksDBOptions(&rocksdb_options, log_prefix + " [TMP]: ");
    auto db = VERIFY_RESULT(rocksdb::DB::Open(rocksdb_options, dir));
    auto doc_db = docdb::DocDB::FromRegularUnbounded(db.get());

    FetchState restoring_state(doc_db, ReadHybridTime::SingleTime(restore_at));
    RETURN_NOT_OK(restoring_state.SetPrefix(""));

    TabletRestorePatch restore_patch(
        &existing_state, &restoring_state, write_batch,
        tablet().metadata()->primary_table_info().get(), request.db_oid());
    RETURN_NOT_OK(restore_patch.PatchCurrentStateFromRestoringState());
    RETURN_NOT_OK(restore_patch.Finish());
    return std::move(restore_patch);
  } else {
    LOG_WITH_PREFIX(INFO) << "Cleaning only rows with db oid " << request.db_oid();
    TabletRestorePatch restore_patch(
        &existing_state, nullptr, write_batch,
        tablet().metadata()->primary_table_info().get(), request.db_oid());
    RETURN_NOT_OK(restore_patch.PatchCurrentStateFromRestoringState());
    RETURN_NOT_OK(restore_patch.Finish());
    return std::move(restore_patch);
  }
}

Status TabletSnapshots::RestoreCheckpoint(
    const std::string& dir, HybridTime restore_at, const RestoreMetadata& restore_metadata,
    const docdb::ConsensusFrontier& frontier, bool is_pitr_restore, const OpId& op_id) {
  LongOperationTracker long_operation_tracker("Restore checkpoint", 5s);

  // The following two lines can't just be changed to RETURN_NOT_OK(PauseReadWriteOperations()):
  // op_pause has to stay in scope until the end of the function.
  auto op_pauses = StartShutdownRocksDBs(DisableFlushOnShutdown(!dir.empty()), AbortOps::kTrue);

  std::lock_guard lock(create_checkpoint_lock());

  const string db_dir = regular_db().GetName();
  const std::string intents_db_dir = has_intents_db() ? intents_db().GetName() : std::string();

  if (dir.empty()) {
    // Just change rocksdb hybrid time limit, because it should be in retention interval.
    // TODO(pitr) apply transactions and reset intents.
    CompleteShutdownRocksDBs(op_pauses);
  } else {
    // Destroy DB object.
    // TODO: snapshot current DB and try to restore it in case of failure.
    RETURN_NOT_OK(DeleteRocksDBs(CompleteShutdownRocksDBs(op_pauses)));

    auto s = CopyDirectory(
        &rocksdb_env(), dir, db_dir, UseHardLinks::kTrue, CreateIfMissing::kTrue);
    if (PREDICT_FALSE(!s.ok())) {
      LOG_WITH_PREFIX(WARNING) << "Copy checkpoint files status: " << s;
      return STATUS(IllegalState, "Unable to copy checkpoint files", s.ToString());
    }
    auto tablet_metadata_file = TabletMetadataFile(db_dir);
    if (env().FileExists(tablet_metadata_file)) {
      RETURN_NOT_OK(env().DeleteFile(tablet_metadata_file));
    }
  }

  {
    rocksdb::Options rocksdb_options;
    tablet().InitRocksDBOptions(&rocksdb_options, LogPrefix());
    docdb::RocksDBPatcher patcher(db_dir, rocksdb_options);

    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.ModifyFlushedFrontier(frontier));
    if (restore_at) {
      RETURN_NOT_OK(patcher.SetHybridTimeFilter(std::nullopt, restore_at));
    }
  }

  bool need_flush = false;

  if (restore_metadata.schema) {
    // TODO(pitr) check deleted columns
    // OpId::Invalid() is used to indicate the callee to not
    // set last_applied_change_metadata_op_id field of tablet metadata.
    tablet().metadata()->SetSchema(
        *restore_metadata.schema, *restore_metadata.index_map, {} /* deleted_columns */,
        restore_metadata.schema_version, op_id);
    tablet().metadata()->SetHidden(restore_metadata.hide);
    need_flush = true;
  }

  for (const auto& colocated_table_metadata : restore_metadata.colocated_tables_metadata) {
    LOG(INFO) << "Setting schema, index information and schema version for table "
              << colocated_table_metadata.table_id;
    // OpId::Invalid() is used to indicate the callee to not
    // set last_applied_change_metadata_op_id field of tablet metadata.
    tablet().metadata()->SetSchema(
        *colocated_table_metadata.schema, *colocated_table_metadata.index_map,
        {} /* deleted_columns */,
        colocated_table_metadata.schema_version, op_id,
        colocated_table_metadata.table_id);
    need_flush = true;
  }

  if (!dir.empty()) {
    auto tablet_metadata_file = TabletMetadataFile(dir);
    // Old snapshots could lack tablet metadata, so just do nothing in this case.
    if (env().FileExists(tablet_metadata_file)) {
      LOG_WITH_PREFIX(INFO) << "Merging metadata with restored: " << tablet_metadata_file
                            << " , force overwrite of schema packing " << !is_pitr_restore;
      RETURN_NOT_OK(tablet().metadata()->MergeWithRestored(
          tablet_metadata_file,
          is_pitr_restore ? dockv::OverwriteSchemaPacking::kFalse
              : dockv::OverwriteSchemaPacking::kTrue));
      need_flush = true;
    }
  }

  if (need_flush) {
    RETURN_NOT_OK(tablet().metadata()->Flush());
    RefreshYBMetaDataCache();
  }

  // Reopen database from copied checkpoint.
  // Note: db_dir == metadata()->rocksdb_dir() is still valid db dir.
  auto s = OpenRocksDBs();
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed tablet db opening from checkpoint: " << s;
    return s;
  }

  LOG_WITH_PREFIX(INFO) << "Checkpoint restored from " << dir;
  LOG_WITH_PREFIX(INFO) << "Re-enabling compactions";
  s = tablet().EnableCompactions(&op_pauses.blocking_rocksdb_shutdown_start);
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to enable compactions after restoring a checkpoint";
    return s;
  }

  // Schedule post split compaction after compaction enabled on the tablet.
  tablet().TriggerPostSplitCompactionIfNeeded();

  // Ensure that op_pauses stays in scope throughout this function.
  for (auto* op_pause : op_pauses.AsArray()) {
    DFATAL_OR_RETURN_NOT_OK(op_pause->status());
  }

  return Status::OK();
}

Result<std::string> TabletSnapshots::RestoreToTemporary(
    const TxnSnapshotId& snapshot_id, HybridTime restore_at) {
  auto source_dir = JoinPathSegments(
      VERIFY_RESULT(metadata().TopSnapshotsDir()), snapshot_id.ToString());
  auto dest_dir = source_dir + kTempSnapshotDirSuffix;
  RETURN_NOT_OK(CleanupSnapshotDir(dest_dir));
  RETURN_NOT_OK(CopyDirectory(
      &rocksdb_env(), source_dir, dest_dir, UseHardLinks::kTrue, CreateIfMissing::kTrue));

  {
    rocksdb::Options rocksdb_options;
    tablet().InitRocksDBOptions(&rocksdb_options, LogPrefix());
    docdb::RocksDBPatcher patcher(dest_dir, rocksdb_options);

    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.SetHybridTimeFilter(std::nullopt, restore_at));
  }

  return dest_dir;
}

Status TabletSnapshots::Delete(const SnapshotOperation& operation) {
  const std::string top_snapshots_dir = metadata().snapshots_dir();
  const auto& snapshot_id = operation.request()->snapshot_id();
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(snapshot_id);
  const std::string snapshot_dir = JoinPathSegments(
      top_snapshots_dir, !txn_snapshot_id ? snapshot_id.ToBuffer() : txn_snapshot_id.ToString());

  std::lock_guard lock(create_checkpoint_lock());
  Env* const env = metadata().fs_manager()->env();

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
  frontier.set_op_id(operation.op_id());
  frontier.set_hybrid_time(operation.hybrid_time());
  // Here we are just recording the fact that we've executed the "delete snapshot" Raft operation
  // so that it won't get replayed if we crash. No need to force the flushed frontier to be the
  // exact value set above.
  RETURN_NOT_OK(tablet().ModifyFlushedFrontier(
      frontier, rocksdb::FrontierModificationMode::kUpdate));

  LOG_WITH_PREFIX(INFO) << "Complete snapshot deletion on tablet in folder: " << snapshot_dir;

  return Status::OK();
}

Status TabletSnapshots::CreateCheckpoint(
    const std::string& dir, const CreateIntentsCheckpointIn create_intents_checkpoint_in) {
  ScopedRWOperation scoped_read_operation(&pending_op_counter_blocking_rocksdb_shutdown_start());
  RETURN_NOT_OK(scoped_read_operation);

  auto temp_intents_dir = dir + kIntentsDBSuffix;
  auto final_intents_dir = JoinPathSegments(dir, kIntentsSubdir);

  std::lock_guard lock(create_checkpoint_lock());

  if (!has_regular_db()) {
    LOG_WITH_PREFIX(INFO) << "Skipped creating checkpoint in " << dir;
    return STATUS(NotSupported,
                  "Tablet does not have a RocksDB (could be a transaction status tablet)");
  }

  auto parent_dir = DirName(dir);
  RETURN_NOT_OK_PREPEND(metadata().fs_manager()->CreateDirIfMissing(parent_dir),
                        Format("Unable to create checkpoints directory $0", parent_dir));

  // Order does not matter because we flush both DBs and does not have parallel writes.
  Status status;
  if (has_intents_db()) {
    status = rocksdb::checkpoint::CreateCheckpoint(&intents_db(), temp_intents_dir);
  }
  if (status.ok()) {
    status = rocksdb::checkpoint::CreateCheckpoint(&regular_db(), dir);
  }
  if (status.ok() && has_intents_db() &&
      create_intents_checkpoint_in == CreateIntentsCheckpointIn::kUseIntentsDbSuffix) {
    status = Env::Default()->RenameFile(temp_intents_dir, final_intents_dir);
  }

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Create checkpoint status: " << status;
    return STATUS_FORMAT(IllegalState, "Unable to create checkpoint: $0", status);
  }
  LOG_WITH_PREFIX(INFO) << "Checkpoint created in " << dir;

  TEST_last_rocksdb_checkpoint_dir_ = dir;

  return Status::OK();
}

Status TabletSnapshots::CreateDirectories(const string& rocksdb_dir, FsManager* fs) {
  const auto top_snapshots_dir = SnapshotsDirName(rocksdb_dir);
  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissingAndSync(top_snapshots_dir),
                        Format("Unable to create snapshots directory $0", top_snapshots_dir));
  return Status::OK();
}

Status TabletSnapshots::RestoreFinished(SnapshotOperation* operation) {
  return tablet().RestoreFinished(
      VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(operation->request()->restoration_id())),
      HybridTime::FromPB(operation->request()->restoration_hybrid_time()));
}

Result<bool> TabletRestorePatch::ShouldSkipEntry(const Slice& key, const Slice& value) {
  KeyBuffer key_copy;
  key_copy = key;
  dockv::SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(
      key_copy.AsSlice(), dockv::HybridTimeRequired::kFalse));
  // Get the db_oid.
  int64_t db_oid = sub_doc_key.doc_key().hashed_group()[0].GetInt64();
  if (db_oid != db_oid_) {
    return true;
  }
  return false;
}

Status TabletRestorePatch::UpdateColumnValueInMap(
    const Slice& key, const Slice& value,
    std::map<dockv::DocKey, SequencesDataInfo>* key_to_seq_info_map) {
  dockv::SubDocKey decoded_key;
  RETURN_NOT_OK(decoded_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));

  auto last_value_opt = VERIFY_RESULT(GetInt64ColumnValue(
      decoded_key, value, table_info_, "last_value"));
  auto is_called_opt = VERIFY_RESULT(GetBoolColumnValue(
      decoded_key, value, table_info_, "is_called"));

  if (!last_value_opt && !is_called_opt) {
    return Status::OK();
  }
  std::optional<int64_t> updated_last_value = last_value_opt;
  std::optional<bool> updated_is_called = is_called_opt;
  const auto& doc_key = decoded_key.doc_key();
  auto it = key_to_seq_info_map->find(doc_key);
  if (it != key_to_seq_info_map->end()) {
    // Only update if last_value has increased.
    if (it->second.last_value) {
      if (!last_value_opt || *(it->second.last_value) >= *last_value_opt) {
        updated_last_value = *(it->second.last_value);
      }
    }
    // Only update if is_called has changed from false to true.
    if (it->second.is_called) {
      if (!is_called_opt || *(it->second.is_called) == true) {
        updated_is_called = *(it->second.is_called);
      }
    }
    key_to_seq_info_map->erase(doc_key);
  }
  SequencesDataInfo seq_values(updated_last_value, updated_is_called);
  key_to_seq_info_map->emplace(doc_key, seq_values);
  VLOG_WITH_FUNC(3) << "Inserted in map " << doc_key.ToString() << ": " << seq_values;
  return Status::OK();
}

Status TabletRestorePatch::ProcessCommonEntry(
    const Slice& key, const Slice& existing_value, const Slice& restoring_value) {
  RETURN_NOT_OK(RestorePatch::ProcessCommonEntry(key, existing_value, restoring_value));
  RETURN_NOT_OK(UpdateColumnValueInMap(
      key, existing_value, &existing_key_to_seq_info_map_));
  return UpdateColumnValueInMap(
      key, restoring_value, &restoring_key_to_seq_info_map_);
}

Status TabletRestorePatch::ProcessRestoringOnlyEntry(
    const Slice& restoring_key, const Slice& restoring_value) {
  RETURN_NOT_OK(RestorePatch::ProcessRestoringOnlyEntry(restoring_key, restoring_value));
  return UpdateColumnValueInMap(
      restoring_key, restoring_value, &restoring_key_to_seq_info_map_);
}

Status TabletRestorePatch::ProcessExistingOnlyEntry(
    const Slice& existing_key, const Slice& existing_value) {
  RETURN_NOT_OK(RestorePatch::ProcessExistingOnlyEntry(existing_key, existing_value));
  return UpdateColumnValueInMap(
      existing_key, existing_value, &existing_key_to_seq_info_map_);
}

Status TabletRestorePatch::Finish() {
  for (const auto& doc_key_and_value : restoring_key_to_seq_info_map_) {
    auto value_to_insert = doc_key_and_value.second;
    auto it = existing_key_to_seq_info_map_.find(doc_key_and_value.first);
    if (it != existing_key_to_seq_info_map_.end()) {
      value_to_insert = it->second;
    }
    // Insert this kv into the write batch.
    if (value_to_insert.last_value) {
      QLValuePB value_pb;
      value_pb.set_int64_value(*(value_to_insert.last_value));
      VLOG_WITH_FUNC(3) << doc_key_and_value.first << ": " << *(value_to_insert.last_value);
      auto column_id = VERIFY_RESULT(table_info_->schema().ColumnIdByName("last_value"));
      auto doc_path = dockv::DocPath(
          doc_key_and_value.first.Encode(), dockv::KeyEntryValue::MakeColumnId(column_id));
      RETURN_NOT_OK(DocBatch()->SetPrimitive(
          doc_path, docdb::ValueRef(value_pb, SortingType::kNotSpecified)));
      IncrementTicker(RestoreTicker::kInserts);
    }
    if (value_to_insert.is_called) {
      QLValuePB value_pb;
      value_pb.set_bool_value(*(value_to_insert.is_called));
      VLOG_WITH_FUNC(3) << doc_key_and_value.first << ": " << *(value_to_insert.is_called);
      auto column_id = VERIFY_RESULT(table_info_->schema().ColumnIdByName("is_called"));
      auto doc_path = dockv::DocPath(
          doc_key_and_value.first.Encode(), dockv::KeyEntryValue::MakeColumnId(column_id));
      RETURN_NOT_OK(DocBatch()->SetPrimitive(
          doc_path, docdb::ValueRef(value_pb, SortingType::kNotSpecified)));
      IncrementTicker(RestoreTicker::kInserts);
    }
  }
  return Status::OK();
}

std::ostream& operator<<(std::ostream& out, const SequencesDataInfo& value) {
  out << "[last_value: " << (value.last_value ? std::to_string(*(value.last_value)) : "none")
      << ", is_called: " << (value.is_called ? (*(value.is_called) ? "true" : "false") : "none")
      << "]";
  return out;
}

} // namespace tablet
} // namespace yb
