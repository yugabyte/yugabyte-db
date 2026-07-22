// Copyright (c) YugabyteDB, Inc.
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

#include "yb/ash/wait_state.h"

#include "yb/qlexpr/index.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_util.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/rocksdb/utilities/checkpoint.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/restore_util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/util/atomic.h"
#include "yb/util/debug-util.h"
#include "yb/util/file_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/operation_counter.h"
#include "yb/util/path_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"

using std::string;

using namespace std::literals;

DEFINE_RUNTIME_bool(enable_async_snapshot_directory_cleanup, false,
    "Delete tablet snapshot directories asynchronously after logically tombstoning them.");

DEFINE_RUNTIME_int32(max_wait_for_aborting_transactions_during_restore_ms, 200,
    "How much time in milliseconds to wait for tablet transactions to abort while "
    "applying the raft restore operation to a tablet.");
TAG_FLAG(max_wait_for_aborting_transactions_during_restore_ms, advanced);

DEFINE_NON_RUNTIME_int32(snapshot_cleanup_pool_size, 4,
    "Maximum number of concurrent tablet snapshot directory cleanup tasks per process.");
TAG_FLAG(snapshot_cleanup_pool_size, advanced);

DEFINE_test_flag(int32, delay_tablet_split_metadata_restore_secs, 0,
    "How much time in secs to delay restoring tablet split metadata after restoring "
    "checkpoint.");

DEFINE_test_flag(int32, delay_tablet_export_metadata_ms, 0,
    "How much time in milliseconds to delay before exporting tablet metadata during "
    "snapshot creation.");

DEFINE_test_flag(double, delay_create_snapshot_probability, 0.0,
    "The probability to delay creating snapshot by 1 second");

DEFINE_test_flag(bool, pause_after_tombstoning_snapshot, false,
    "If true, pause snapshot deletion after tombstoning the snapshot directory "
    "until the flag is reset.");

METRIC_DEFINE_counter(
    tablet, snapshot_tombstone_successes, "Snapshot Tombstone Successes",
    yb::MetricUnit::kOperations,
    "Number of snapshot directories successfully renamed to deletion tombstones");
METRIC_DEFINE_counter(
    tablet, snapshot_tombstone_failures, "Snapshot Tombstone Failures", yb::MetricUnit::kOperations,
    "Number of snapshot logical deletion attempts that require retry");
METRIC_DEFINE_counter(
    tablet, snapshot_parent_sync_failures, "Snapshot Parent Directory Sync Failures",
    yb::MetricUnit::kOperations, "Number of failed snapshot parent directory sync attempts");
METRIC_DEFINE_counter(
    tablet, snapshot_cleanup_successes, "Snapshot Cleanup Successes", yb::MetricUnit::kOperations,
    "Number of snapshot tombstone directories successfully removed");
METRIC_DEFINE_counter(
    tablet, snapshot_cleanup_failures, "Snapshot Cleanup Failures", yb::MetricUnit::kOperations,
    "Number of failed recursive snapshot tombstone deletion attempts");
METRIC_DEFINE_counter(
    tablet, snapshot_cleanup_retries, "Snapshot Cleanup Retries", yb::MetricUnit::kOperations,
    "Number of delayed snapshot cleanup retry passes scheduled");
METRIC_DEFINE_gauge_uint64(
    tablet, snapshot_pending_logical_deletions, "Pending Logical Snapshot Deletions",
    yb::MetricUnit::kOperations,
    "Number of snapshot deletions still requiring a rename or durable parent sync");
METRIC_DEFINE_gauge_uint64(
    tablet, snapshot_pending_physical_deletions, "Pending Physical Snapshot Deletions",
    yb::MetricUnit::kOperations,
    "Number of snapshot deletions awaiting physical removal or durable parent sync");
METRIC_DEFINE_gauge_uint64(
    tablet, snapshot_oldest_pending_deletion_age_ms, "Oldest Pending Snapshot Deletion Age",
    yb::MetricUnit::kMilliseconds,
    "Approximate age in milliseconds of the oldest pending snapshot deletion");

DEFINE_test_flag(bool, pause_create_checkpoint, false,
    "If true, pause after acquiring checkpoint lock in CreateCheckpoint "
    "until the flag is reset.");

DEFINE_test_flag(int32, snapshot_cleanup_retry_delay_ms, 0,
    "If positive, override the snapshot cleanup retry backoff in tests.");

namespace yb::tablet {

namespace {

const std::string kTempSnapshotDirSuffix = ".tmp";
const std::string kDeletedSnapshotDirSuffix = ".deleted.tmp";
const std::string kTabletMetadataFile = "tablet.metadata";
const std::string kLastSnapshotPrefix = "last_snapshot.";

Result<bool> IsSnapshotDirectory(Env& env, const std::string& path) {
  auto is_directory = env.IsDirectory(path);
  if (!is_directory.ok()) {
    if (is_directory.status().IsNotFound()) {
      return false;
    }
    return is_directory.status();
  }
  if (!*is_directory) {
    return STATUS_FORMAT(IllegalState, "Snapshot path $0 is not a directory", path);
  }
  return true;
}

std::string TabletMetadataFile(const std::string& dir) {
  return JoinPathSegments(dir, kTabletMetadataFile);
}

std::string LastSnapshotHybridTimePath(
    const std::string& top_dir, SnapshotScheduleId schedule_id, HybridTime time) {
  return JoinPathSegments(
      top_dir, Format("$0$1.$2", kLastSnapshotPrefix, schedule_id, time.ToUint64()));
}

void CleanupLastSnapshotHybridTime(
    Env& env, const std::string& top_dir, SnapshotScheduleId schedule_id, HybridTime time) {
  auto path = LastSnapshotHybridTimePath(top_dir, schedule_id, time);
  VLOG(2) << "Cleanup snapshot ht: " << path;
  if (env.FileExists(path)) {
    WARN_NOT_OK(env.DeleteFile(path), "Failed to cleanup last snapshot time");
  }
}

}  // namespace

struct TabletSnapshots::RestoreMetadata {
  std::optional<Schema> schema;
  std::optional<qlexpr::IndexMap> index_map;
  uint32_t schema_version;
  bool hide;
  google::protobuf::RepeatedPtrField<ColocatedTableMetadata> colocated_tables_metadata;
};

struct TabletSnapshots::ColocatedTableMetadata {
  std::optional<Schema> schema;
  std::optional<qlexpr::IndexMap> index_map;
  uint32_t schema_version;
  TableId table_id;
};

struct TabletSnapshots::Metrics {
  Metrics(const scoped_refptr<MetricEntity>& entity, TabletSnapshots* snapshots)
      : tombstone_successes(METRIC_snapshot_tombstone_successes.Instantiate(entity)),
        tombstone_failures(METRIC_snapshot_tombstone_failures.Instantiate(entity)),
        parent_sync_failures(METRIC_snapshot_parent_sync_failures.Instantiate(entity)),
        cleanup_successes(METRIC_snapshot_cleanup_successes.Instantiate(entity)),
        cleanup_failures(METRIC_snapshot_cleanup_failures.Instantiate(entity)),
        cleanup_retries(METRIC_snapshot_cleanup_retries.Instantiate(entity)),
        pending_logical_deletions(METRIC_snapshot_pending_logical_deletions.Instantiate(entity, 0)),
        pending_physical_deletions(
            METRIC_snapshot_pending_physical_deletions.Instantiate(entity, 0)),
        oldest_pending_deletion_age_ms(
            METRIC_snapshot_oldest_pending_deletion_age_ms.InstantiateFunctionGauge(
                entity,
                Bind(&TabletSnapshots::OldestPendingDeletionAgeMs, Unretained(snapshots)))) {
    oldest_pending_deletion_age_ms->AutoDetach(&snapshots->metrics_detacher_);
  }

  scoped_refptr<Counter> tombstone_successes;
  scoped_refptr<Counter> tombstone_failures;
  scoped_refptr<Counter> parent_sync_failures;
  scoped_refptr<Counter> cleanup_successes;
  scoped_refptr<Counter> cleanup_failures;
  scoped_refptr<Counter> cleanup_retries;
  scoped_refptr<AtomicGauge<uint64_t>> pending_logical_deletions;
  scoped_refptr<AtomicGauge<uint64_t>> pending_physical_deletions;
  scoped_refptr<FunctionGauge<uint64_t>> oldest_pending_deletion_age_ms;
};

TabletSnapshots::TabletSnapshots(Tablet* tablet) : TabletComponent(tablet) {
  if (const auto& metric_entity = tablet->GetTabletMetricsEntity()) {
    metrics_ = std::make_unique<Metrics>(metric_entity, this);
  }
}

TabletSnapshots::~TabletSnapshots() {
  CompleteShutdown();
}

void TabletSnapshots::SetCleanupPool(ThreadPool* thread_pool, rpc::Scheduler* scheduler) {
  {
    std::lock_guard lock(cleanup_mutex_);
    if (shutting_down_) {
      return;
    }
    DCHECK(!cleanup_token_);
    cleanup_token_ = thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL);
    retry_task_tracker_.Bind(scheduler);
  }

  ScanDeletedSnapshotDirs();
  SubmitCleanup();
}

void TabletSnapshots::StartShutdown() {
  std::lock_guard lock(cleanup_mutex_);
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  retry_task_tracker_.StartShutdown();
}

void TabletSnapshots::CompleteShutdown() {
  std::unique_ptr<ThreadPoolToken> cleanup_token;
  {
    std::lock_guard lock(cleanup_mutex_);
    if (!shutting_down_) {
      shutting_down_ = true;
      retry_task_tracker_.StartShutdown();
    }
  }
  retry_task_tracker_.CompleteShutdown();
  {
    std::lock_guard lock(cleanup_mutex_);
    cleanup_token = std::move(cleanup_token_);
  }
  if (cleanup_token) {
    cleanup_token->Shutdown();
  }
  {
    std::lock_guard lock(cleanup_mutex_);
    pending_deletions_.clear();
    UpdatePendingMetricsUnlocked();
  }
}

Status TabletSnapshots::Open() {
  auto dir = metadata().snapshots_dir();
  if (!env().DirExists(dir)) {
    return Status::OK();
  }
  std::vector<std::pair<SnapshotScheduleId, HybridTime>> last_snapshot_ht_list;
  for (const auto& child : VERIFY_RESULT(env().GetChildren(dir, ExcludeDots::kTrue))) {
    if (!child.starts_with(kLastSnapshotPrefix)) {
      continue;
    }
    auto pos = child.find('.', kLastSnapshotPrefix.size());
    if (pos == std::string::npos) {
      LOG_WITH_PREFIX(DFATAL) << "Wrong last snapshot file name: " << child;
      continue;
    }
    auto snapshot_id_str = child.substr(
        kLastSnapshotPrefix.size(), pos - kLastSnapshotPrefix.size());
    auto hybrid_time_str = child.substr(pos + 1);
    auto snapshot_id = SnapshotScheduleIdFromString(snapshot_id_str);
    if (!snapshot_id.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Wrong snapshot id in " << child << ": " << snapshot_id.status();
      continue;
    }
    auto hybrid_time = CheckedStoll(hybrid_time_str);
    if (!hybrid_time.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Wrong hybrid time in " << child << ": " << hybrid_time.status();
      continue;
    }
    last_snapshot_ht_list.emplace_back(*snapshot_id, *hybrid_time);
  }
  std::sort(last_snapshot_ht_list.begin(), last_snapshot_ht_list.end());
  std::lock_guard lock(last_snapshot_ht_mutex_);
  for (auto it = last_snapshot_ht_list.begin(); it != last_snapshot_ht_list.end();) {
    auto next = it;
    ++next;
    if (next != last_snapshot_ht_list.end() && it->first == next->first) {
      CleanupLastSnapshotHybridTime(env(), dir, it->first, it->second);
    } else {
      last_snapshot_ht_.emplace(it->first, it->second);
    }
    it = next;
  }
  VLOG_WITH_PREFIX_AND_FUNC(1) << "last snapshot ht: " << AsString(last_snapshot_ht_);
  return Status::OK();
}

std::string TabletSnapshots::SnapshotsDirName(const std::string& rocksdb_dir) {
  return docdb::GetStorageDir(rocksdb_dir, docdb::kSnapshotsDirName);
}

bool TabletSnapshots::IsTempSnapshotDir(const std::string& dir) {
  return dir.ends_with(kTempSnapshotDirSuffix);
}

bool TabletSnapshots::IsDeletedSnapshotDir(const std::string& dir) {
  return ActiveSnapshotDirFromDeletedSnapshotDir(dir).ok();
}

Result<std::string> TabletSnapshots::ActiveSnapshotDirFromDeletedSnapshotDir(
    const std::string& deleted_snapshot_dir) {
  const auto base_name = BaseName(deleted_snapshot_dir);
  SCHECK(
      base_name.ends_with(kDeletedSnapshotDirSuffix), InvalidArgument,
      "Deleted snapshot path must end in .deleted.tmp");

  const auto op_id_end = base_name.size() - kDeletedSnapshotDirSuffix.size();
  SCHECK_GT(op_id_end, 0, InvalidArgument, "Deleted snapshot path has no snapshot id or OpId");
  const auto index_separator = base_name.rfind('.', op_id_end - 1);
  SCHECK_NE(
      index_separator, std::string::npos, InvalidArgument,
      "Deleted snapshot path has no Raft index");
  SCHECK_GT(index_separator, 0, InvalidArgument, "Deleted snapshot path has no snapshot id");
  const auto term_separator = base_name.rfind('.', index_separator - 1);
  SCHECK_NE(
      term_separator, std::string::npos, InvalidArgument, "Deleted snapshot path has no Raft term");
  SCHECK_GT(term_separator, 0, InvalidArgument, "Deleted snapshot path has no snapshot id");

  const auto op_id = VERIFY_RESULT(
      OpId::FromString(base_name.substr(term_separator + 1, op_id_end - term_separator - 1)));
  SCHECK(op_id.is_valid_not_empty(), InvalidArgument, "Deleted snapshot path has an invalid OpId");

  const auto active_base_name = base_name.substr(0, term_separator);
  const auto parent_dir = DirName(deleted_snapshot_dir);
  return parent_dir.empty() || parent_dir == "." ? active_base_name
                                                 : JoinPathSegments(parent_dir, active_base_name);
}

std::string TabletSnapshots::DeletedSnapshotDir(
    const std::string& snapshot_dir, const OpId& delete_op_id) {
  return Format("$0.$1$2", snapshot_dir, delete_op_id, kDeletedSnapshotDirSuffix);
}

bool TabletSnapshots::IsLastSnapshotTimeFilePath(const std::string& dir) {
  return dir.starts_with(kLastSnapshotPrefix);
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

  if (RandomActWithProbability(FLAGS_TEST_delay_create_snapshot_probability)) {
    LOG_WITH_PREFIX_AND_FUNC(INFO) << "TEST: Sleep";
    std::this_thread::sleep_for(1s);
  }

  ScopedRWOperation scoped_read_operation(&pending_op_counter_blocking_rocksdb_shutdown_start());
  RETURN_NOT_OK(scoped_read_operation);

  Status s;
  {
    SCOPED_WAIT_STATUS(Snapshot_WaitingForFlush);
    s = regular_db().Flush(rocksdb::FlushOptions(rocksdb::FlushReason::kSnapshotCreation));
  }

  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "RocksDB flush status: " << s;
    return s.CloneAndPrepend("Unable to flush RocksDB");
  }

  const std::string& snapshot_dir = data.snapshot_dir;

  Env& env = this->env();
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
      [this, &env, &exit_on_failure, &snapshot_dir, &tmp_snapshot_dir, &top_snapshots_dir] {
    bool do_sync = false;

    if (env.FileExists(tmp_snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env.DeleteRecursively(tmp_snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG_WITH_PREFIX(WARNING)
            << "Cannot recursively delete temp snapshot dir "
            << tmp_snapshot_dir << ": " << deletion_status;
      }
    }

    if (exit_on_failure && env.FileExists(snapshot_dir)) {
      do_sync = true;
      const Status deletion_status = env.DeleteRecursively(snapshot_dir);
      if (PREDICT_FALSE(!deletion_status.ok())) {
        LOG_WITH_PREFIX(WARNING)
            << "Cannot recursively delete snapshot dir " << snapshot_dir << ": " << deletion_status;
      }
    }

    if (do_sync) {
      const Status sync_status = env.SyncDir(top_snapshots_dir);
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
      env.RenameFile(tmp_snapshot_dir, snapshot_dir),
      Format("Cannot rename temp snapshot dir $0 to $1", tmp_snapshot_dir, snapshot_dir));
  RETURN_NOT_OK_PREPEND(
      env.SyncDir(top_snapshots_dir),
      Format("Cannot sync top snapshots dir $0", top_snapshots_dir));

  if (need_flush) {
    RETURN_NOT_OK(tablet().metadata()->Flush());
  }

  LOG_WITH_PREFIX(INFO) << "Complete snapshot creation in folder: " << snapshot_dir
                        << ", snapshot hybrid time: " << snapshot_hybrid_time;

  if (data.schedule_id) {
    auto path = LastSnapshotHybridTimePath(
        top_snapshots_dir, data.schedule_id, data.snapshot_hybrid_time);
    RETURN_NOT_OK(WriteStringToFile(&env, Slice(), path));
    HybridTime previous_last_snapshot_ht;
    {
      std::lock_guard lock(last_snapshot_ht_mutex_);
      auto& existing = last_snapshot_ht_[data.schedule_id];
      previous_last_snapshot_ht = existing;
      existing = data.snapshot_hybrid_time;
    }
    if (previous_last_snapshot_ht) {
      CleanupLastSnapshotHybridTime(
          env, top_snapshots_dir, data.schedule_id, previous_last_snapshot_ht);
    }
  }

  exit_on_failure = false;
  return Status::OK();
}

Env& TabletSnapshots::env() {
  return *metadata().fs_manager()->env();
}

FsManager* TabletSnapshots::fs_manager() {
  return metadata().fs_manager();
}

Status TabletSnapshots::CleanupSnapshotDir(const std::string& dir) {
  SCOPED_WAIT_STATUS(Snapshot_CleanupSnapshotDir);
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
      tablet().AbortActiveTransactions(deadline),
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
    RETURN_NOT_OK(
        SchemaFromPB(request.schema().ToGoogleProtobuf(), &restore_metadata.schema.value()));
    restore_metadata.index_map.emplace(ToRepeatedPtrField(request.indexes()));
    restore_metadata.schema_version = request.schema_version();
    restore_metadata.hide = request.hide();
  }

  for (const auto& entry : request.colocated_tables_metadata()) {
    auto* table_metadata = restore_metadata.colocated_tables_metadata.Add();
    table_metadata->schema_version = entry.schema_version();
    table_metadata->schema.emplace();
    RETURN_NOT_OK(SchemaFromPB(entry.schema().ToGoogleProtobuf(), &table_metadata->schema.value()));
    table_metadata->index_map.emplace(ToRepeatedPtrField(entry.indexes()));
    table_metadata->table_id = entry.table_id().ToBuffer();
  }
  Status s = RestoreCheckpoint(
      snapshot_dir, restore_at, restore_metadata, frontier, !request.schedule_id().empty(),
      operation->op_id());
  VLOG_WITH_PREFIX(1) << "Complete checkpoint restoring with result " << s << " in folder: "
                      << metadata().rocksdb_dir();
  int32 delay_time_secs = FLAGS_TEST_delay_tablet_split_metadata_restore_secs;
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
    rocksdb_options.compaction_style = rocksdb::kCompactionStyleNone;
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

// Get the map of snapshot cotable ids to the current cotable ids.
// The restored flushed frontiers can have cotable ids that are different from current cotable ids.
// This map is used to update the cotable ids in the restored flushed frontiers.
Result<docdb::CotableIdsMap> TabletSnapshots::GetCotableIdsMap(const std::string& snapshot_dir) {
  docdb::CotableIdsMap cotable_ids_map;
  if (snapshot_dir.empty() || !metadata().colocated()) {
    return cotable_ids_map;
  }
  auto snapshot_metadata_file = TabletMetadataFile(snapshot_dir);
  if (!env().FileExists(snapshot_metadata_file)) {
    return cotable_ids_map;
  }
  auto snapshot_metadata =
      VERIFY_RESULT(RaftGroupMetadata::LoadFromPath(fs_manager(), snapshot_metadata_file));
  for (const auto& snapshot_table_info : snapshot_metadata->GetColocatedTableInfos()) {
    auto current_table_info = metadata().GetTableInfo(
        snapshot_table_info->schema().colocation_id());
    if (!current_table_info.ok()) {
      if (!current_table_info.status().IsNotFound()) {
        return current_table_info.status();
      }
      LOG_WITH_PREFIX(WARNING) << "Table " << snapshot_table_info->table_id
                                << " not found: " << current_table_info.status();
    } else if ((*current_table_info)->cotable_id != snapshot_table_info->cotable_id) {
      cotable_ids_map[snapshot_table_info->cotable_id] = (*current_table_info)->cotable_id;
    }
  }
  if (!cotable_ids_map.empty()) {
    LOG_WITH_PREFIX(INFO) << "Cotable ids map: " << yb::ToString(cotable_ids_map);
  }
  return cotable_ids_map;
}

Status TabletSnapshots::RestoreCheckpoint(
    const std::string& snapshot_dir, HybridTime restore_at, const RestoreMetadata& restore_metadata,
    const docdb::ConsensusFrontier& frontier, bool is_pitr_restore, const OpId& op_id) {
  SCOPED_WAIT_STATUS(Snapshot_RestoreCheckpoint);
  LongOperationTracker long_operation_tracker("Restore checkpoint", 5s);

  // The following two lines can't just be changed to RETURN_NOT_OK(PauseReadWriteOperations()):
  // op_pause has to stay in scope until the end of the function.
  auto op_pauses = StartShutdownStorages(
      DisableFlushOnShutdown(!snapshot_dir.empty()), AbortOps::kTrue);

  std::lock_guard lock(create_checkpoint_lock());

  const string db_dir = regular_db().GetName();
  const std::string intents_db_dir = has_intents_db() ? intents_db().GetName() : std::string();

  if (snapshot_dir.empty()) {
    // Just change rocksdb hybrid time limit, because it should be in retention interval.
    // TODO(pitr) apply transactions and reset intents.
    CompleteShutdownStorages(op_pauses);
  } else {
    // Destroy DB object.
    // TODO: snapshot current DB and try to restore it in case of failure.
    for (const auto& path : CompleteShutdownStorages(op_pauses)) {
      if (env().FileExists(path)) {
        RETURN_NOT_OK(env().DeleteRecursively(path));
      }
    }

    auto s = CopyDirectory(
        &rocksdb_env(), snapshot_dir, db_dir,
        CopyOption::kUseHardLinks, CopyOption::kCreateIfMissing, CopyOption::kRecursive);
    if (PREDICT_FALSE(!s.ok())) {
      LOG_WITH_PREFIX(WARNING) << "Copy checkpoint files status: " << s;
      return STATUS(IllegalState, "Unable to copy checkpoint files", s.ToString());
    }

    RETURN_NOT_OK(MoveChildren(this->env(), db_dir, docdb::IncludeIntents::kFalse));

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
    RETURN_NOT_OK(patcher.ModifyFlushedFrontier(
        frontier, VERIFY_RESULT(GetCotableIdsMap(snapshot_dir))));
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

  if (!snapshot_dir.empty()) {
    auto snapshot_metadata_file = TabletMetadataFile(snapshot_dir);
    // Old snapshots could lack tablet metadata, so just do nothing in this case.
    if (env().FileExists(snapshot_metadata_file)) {
      LOG_WITH_PREFIX(INFO) << "Merging metadata with restored: " << snapshot_metadata_file
                            << " , force overwrite of schema packing " << !is_pitr_restore;
      RETURN_NOT_OK(tablet().metadata()->MergeWithRestored(
          snapshot_metadata_file,
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
  auto s = OpenStorages();
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed tablet db opening from checkpoint: " << s;
    return s;
  }

  LOG_WITH_PREFIX(INFO) << "Checkpoint restored from " << snapshot_dir;
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
      &rocksdb_env(), source_dir, dest_dir,
      CopyOption::kUseHardLinks, CopyOption::kCreateIfMissing, CopyOption::kRecursive));

  {
    rocksdb::Options rocksdb_options;
    tablet().InitRocksDBOptions(&rocksdb_options, LogPrefix());
    docdb::RocksDBPatcher patcher(dest_dir, rocksdb_options);

    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.SetHybridTimeFilter(std::nullopt, restore_at));
  }

  return dest_dir;
}

TabletSnapshots::TombstoneState TabletSnapshots::TryTombstoneSnapshotDir(
    PendingSnapshotDeletion* deletion) {
  std::lock_guard lock(create_checkpoint_lock());
  Result<bool> snapshot_dir_exists = false;
  if (!deletion->active_dir.empty()) {
    snapshot_dir_exists = IsSnapshotDirectory(env(), deletion->active_dir);
    if (!snapshot_dir_exists.ok()) {
      deletion->logical_pending = true;
      if (metrics_) {
        metrics_->tombstone_failures->Increment();
      }
      LOG_WITH_PREFIX(WARNING) << "Cannot check snapshot dir " << deletion->active_dir
                               << " while deleting it: " << snapshot_dir_exists.status();
      return TombstoneState::kRetry;
    }
  }
  const auto tombstone_dir_exists = IsSnapshotDirectory(env(), deletion->tombstone_dir);
  if (!tombstone_dir_exists.ok()) {
    deletion->logical_pending = true;
    if (metrics_) {
      metrics_->tombstone_failures->Increment();
    }
    LOG_WITH_PREFIX(WARNING) << "Cannot check deleted snapshot dir " << deletion->tombstone_dir
                             << " while deleting snapshot dir " << deletion->active_dir << ": "
                             << tombstone_dir_exists.status();
    return TombstoneState::kRetry;
  }

  deletion->logical_pending = *snapshot_dir_exists;
  deletion->physical_pending = *tombstone_dir_exists;
  if (*snapshot_dir_exists && *tombstone_dir_exists) {
    if (metrics_) {
      metrics_->tombstone_failures->Increment();
    }
    LOG_WITH_PREFIX(WARNING) << "Both snapshot dir " << deletion->active_dir
                             << " and deleted snapshot dir " << deletion->tombstone_dir << " exist";
    return TombstoneState::kBothPresent;
  }
  if (!*snapshot_dir_exists && !*tombstone_dir_exists) {
    return TombstoneState::kComplete;
  }

  if (*snapshot_dir_exists) {
    const Status rename_status = env().RenameFile(deletion->active_dir, deletion->tombstone_dir);
    if (PREDICT_FALSE(!rename_status.ok())) {
      if (metrics_) {
        metrics_->tombstone_failures->Increment();
      }
      LOG_WITH_PREFIX(WARNING) << "Cannot tombstone snapshot dir " << deletion->active_dir << " as "
                               << deletion->tombstone_dir << ": " << rename_status;
      return TombstoneState::kRetry;
    }
    deletion->logical_pending = false;
    deletion->physical_pending = true;
    if (metrics_) {
      metrics_->tombstone_successes->Increment();
    }
    LOG_WITH_PREFIX(INFO) << "Tombstoned snapshot dir " << deletion->active_dir << " as "
                          << deletion->tombstone_dir;
  }

  const Status sync_status = env().SyncDir(DirName(deletion->tombstone_dir));
  if (PREDICT_FALSE(!sync_status.ok())) {
    deletion->logical_pending = true;
    if (metrics_) {
      metrics_->parent_sync_failures->Increment();
    }
    LOG_WITH_PREFIX(WARNING) << "Cannot sync top snapshots dir " << DirName(deletion->tombstone_dir)
                             << " after tombstoning snapshot dir " << deletion->active_dir << ": "
                             << sync_status;
    return TombstoneState::kRetry;
  }
  deletion->logical_pending = false;
  return TombstoneState::kTombstoned;
}

bool TabletSnapshots::CleanupTombstonedSnapshot(PendingSnapshotDeletion* deletion) {
  if (deletion->parent_sync_pending) {
    const Status sync_status = env().SyncDir(DirName(deletion->tombstone_dir));
    if (PREDICT_FALSE(!sync_status.ok())) {
      if (metrics_) {
        metrics_->parent_sync_failures->Increment();
      }
      LOG_WITH_PREFIX(WARNING) << "Cannot retry syncing top snapshots dir "
                               << DirName(deletion->tombstone_dir)
                               << " after deleting tombstoned snapshot dir "
                               << deletion->tombstone_dir << ": " << sync_status;
      return false;
    }
    deletion->parent_sync_pending = false;
    deletion->physical_pending = false;
    PublishPendingDeletionState(*deletion);
    return !deletion->logical_pending;
  }

  const auto tombstone_state = TryTombstoneSnapshotDir(deletion);
  PublishPendingDeletionState(*deletion);
  if (tombstone_state == TombstoneState::kComplete) {
    return true;
  }
  if (tombstone_state == TombstoneState::kRetry) {
    return false;
  }

  TEST_PAUSE_IF_FLAG(TEST_pause_after_tombstoning_snapshot);
  {
    SCOPED_WAIT_STATUS(Snapshot_CleanupSnapshotDir);
    const Status deletion_status = env().DeleteRecursively(deletion->tombstone_dir);
    if (PREDICT_FALSE(!deletion_status.ok())) {
      deletion->physical_pending = true;
      if (metrics_) {
        metrics_->cleanup_failures->Increment();
      }
      LOG_WITH_PREFIX(WARNING) << "Cannot recursively delete tombstoned snapshot dir "
                               << deletion->tombstone_dir << ": " << deletion_status;
      return false;
    }
  }

  if (metrics_) {
    metrics_->cleanup_successes->Increment();
  }
  const auto top_snapshots_dir = DirName(deletion->tombstone_dir);
  const Status sync_status = env().SyncDir(top_snapshots_dir);
  if (PREDICT_FALSE(!sync_status.ok())) {
    deletion->parent_sync_pending = true;
    if (metrics_) {
      metrics_->parent_sync_failures->Increment();
    }
    LOG_WITH_PREFIX(WARNING) << "Cannot sync top snapshots dir " << top_snapshots_dir
                             << " after deleting tombstoned snapshot dir "
                             << deletion->tombstone_dir << ": " << sync_status;
    PublishPendingDeletionState(*deletion);
    return false;
  }
  deletion->physical_pending = false;
  PublishPendingDeletionState(*deletion);
  return !deletion->logical_pending;
}

void TabletSnapshots::ScheduleCleanup(PendingSnapshotDeletion deletion) {
  {
    std::lock_guard lock(cleanup_mutex_);
    if (shutting_down_) {
      return;
    }
    deletion.epoch = ++next_pending_deletion_epoch_;
    auto [it, inserted] = pending_deletions_.try_emplace(deletion.tombstone_dir, deletion);
    if (!inserted) {
      it->second.active_dir = std::move(deletion.active_dir);
      it->second.logical_pending = it->second.logical_pending || deletion.logical_pending;
      it->second.physical_pending = it->second.physical_pending || deletion.physical_pending;
      it->second.parent_sync_pending =
          it->second.parent_sync_pending || deletion.parent_sync_pending;
      it->second.epoch = deletion.epoch;
    }
    UpdatePendingMetricsUnlocked();
    if (retry_scheduled_) {
      retry_task_tracker_.Abort();
      retry_scheduled_ = false;
    }
  }
  SubmitCleanup();
}

void TabletSnapshots::SubmitCleanup() {
  std::lock_guard lock(cleanup_mutex_);
  if (shutting_down_ || !cleanup_token_ || cleanup_task_scheduled_ || pending_deletions_.empty()) {
    return;
  }
  cleanup_task_scheduled_ = true;
  const Status status = cleanup_token_->SubmitFunc([this] { RunCleanup(); });
  if (PREDICT_FALSE(!status.ok())) {
    cleanup_task_scheduled_ = false;
    LOG_WITH_PREFIX(WARNING) << "Cannot submit snapshot cleanup task: " << status;
  }
}

void TabletSnapshots::ScheduleRetry() {
  std::lock_guard lock(cleanup_mutex_);
  if (shutting_down_ || !cleanup_token_ || pending_deletions_.empty() || retry_scheduled_) {
    return;
  }
  const auto delay = FLAGS_TEST_snapshot_cleanup_retry_delay_ms > 0
                         ? FLAGS_TEST_snapshot_cleanup_retry_delay_ms * 1ms
                         : 1s * (1 << std::min(retry_attempt_++, 6U));
  retry_scheduled_ = true;
  if (metrics_) {
    metrics_->cleanup_retries->Increment();
  }
  UpdatePendingMetricsUnlocked();
  retry_task_tracker_.Schedule([this](const Status& status) {
    if (!status.ok()) {
      return;
    }
    {
      std::lock_guard lock(cleanup_mutex_);
      retry_scheduled_ = false;
    }
    SubmitCleanup();
  }, delay);
}

void TabletSnapshots::RunCleanup() {
  std::vector<PendingSnapshotDeletion> deletions;
  {
    std::lock_guard lock(cleanup_mutex_);
    deletions.reserve(pending_deletions_.size());
    for (const auto& [_, deletion] : pending_deletions_) {
      deletions.push_back(deletion);
    }
  }

  std::vector<bool> completed;
  completed.reserve(deletions.size());
  for (auto& deletion : deletions) {
    completed.push_back(CleanupTombstonedSnapshot(&deletion));
  }

  bool retry = false;
  {
    std::lock_guard lock(cleanup_mutex_);
    cleanup_task_scheduled_ = false;
    for (size_t i = 0; i != deletions.size(); ++i) {
      auto& deletion = deletions[i];
      auto it = pending_deletions_.find(deletion.tombstone_dir);
      if (it == pending_deletions_.end() || it->second.epoch != deletion.epoch) {
        continue;
      }
      if (completed[i]) {
        pending_deletions_.erase(it);
      } else {
        deletion.first_pending_at =
            std::min(deletion.first_pending_at, it->second.first_pending_at);
        it->second = std::move(deletion);
      }
    }
    if (pending_deletions_.empty()) {
      retry_attempt_ = 0;
    } else if (!shutting_down_) {
      retry = true;
    }
    UpdatePendingMetricsUnlocked();
  }
  if (retry) {
    ScheduleRetry();
  }
}

void TabletSnapshots::PublishPendingDeletionState(const PendingSnapshotDeletion& deletion) {
  std::lock_guard lock(cleanup_mutex_);
  auto it = pending_deletions_.find(deletion.tombstone_dir);
  if (it == pending_deletions_.end() || it->second.epoch != deletion.epoch) {
    return;
  }
  const auto first_pending_at = std::min(it->second.first_pending_at, deletion.first_pending_at);
  it->second = deletion;
  it->second.first_pending_at = first_pending_at;
  UpdatePendingMetricsUnlocked();
}

void TabletSnapshots::UpdatePendingMetricsUnlocked() {
  if (!metrics_) {
    return;
  }

  uint64_t logical_pending = 0;
  uint64_t physical_pending = 0;
  for (const auto& [_, deletion] : pending_deletions_) {
    logical_pending += deletion.logical_pending;
    physical_pending += deletion.physical_pending;
  }

  metrics_->pending_logical_deletions->set_value(logical_pending);
  metrics_->pending_physical_deletions->set_value(physical_pending);
}

uint64_t TabletSnapshots::OldestPendingDeletionAgeMs() {
  std::lock_guard lock(cleanup_mutex_);
  MonoTime oldest;
  for (const auto& [_, deletion] : pending_deletions_) {
    if (!oldest.Initialized() || deletion.first_pending_at < oldest) {
      oldest = deletion.first_pending_at;
    }
  }
  return oldest.Initialized() ? MonoTime::Now().GetDeltaSince(oldest).ToMilliseconds() : 0;
}

void TabletSnapshots::ScanDeletedSnapshotDirs() {
  const auto top_snapshots_dir = metadata().snapshots_dir();
  auto children = env().GetChildren(top_snapshots_dir, ExcludeDots::kTrue);
  if (!children.ok()) {
    if (!children.status().IsNotFound()) {
      LOG_WITH_PREFIX(WARNING) << "Cannot scan snapshot directories in " << top_snapshots_dir
                               << ": " << children.status();
    }
    return;
  }

  for (const auto& child : *children) {
    if (!child.ends_with(kDeletedSnapshotDirSuffix)) {
      continue;
    }
    const auto tombstone_dir = JoinPathSegments(top_snapshots_dir, child);
    auto active_dir = ActiveSnapshotDirFromDeletedSnapshotDir(tombstone_dir);
    if (!active_dir.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Cannot parse deleted snapshot dir " << tombstone_dir << ": "
                               << active_dir.status();
      continue;
    }
    ScheduleCleanup({
        .active_dir = std::move(*active_dir),
        .tombstone_dir = tombstone_dir,
        .logical_pending = true,
        .physical_pending = true,
    });
  }
}

Status TabletSnapshots::Delete(const SnapshotOperation& operation) {
  const std::string top_snapshots_dir = metadata().snapshots_dir();
  const auto& snapshot_id = operation.request()->snapshot_id();
  const auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(snapshot_id);
  const std::string snapshot_dir = JoinPathSegments(
      top_snapshots_dir, !txn_snapshot_id ? snapshot_id.ToBuffer() : txn_snapshot_id.ToString());
  PendingSnapshotDeletion deletion = {
    .active_dir = snapshot_dir,
    .tombstone_dir = DeletedSnapshotDir(snapshot_dir, operation.op_id()),
  };

  const auto tombstone_state = TryTombstoneSnapshotDir(&deletion);
  if (tombstone_state == TombstoneState::kComplete) {
    LOG_WITH_PREFIX(INFO) << "Complete snapshot deletion on tablet in folder: " << snapshot_dir;
    return Status::OK();
  }

  if (FLAGS_enable_async_snapshot_directory_cleanup) {
    ScheduleCleanup(std::move(deletion));
    return Status::OK();
  }

  if (tombstone_state == TombstoneState::kTombstoned) {
    CleanupTombstonedSnapshot(&deletion);
  }
  LOG_WITH_PREFIX(INFO) << "Complete snapshot deletion on tablet in folder: " << snapshot_dir;
  return Status::OK();
}

Status TabletSnapshots::CreateCheckpoint(
    const std::string& dir, CreateCheckpointIn create_checkpoint_in,
    TabletSnapshots::UseTryLock use_try_lock) {
  ScopedRWOperation scoped_read_operation(&pending_op_counter_blocking_rocksdb_shutdown_start());
  RETURN_NOT_OK(scoped_read_operation);

  Status status;
  {
    std::unique_lock lock(create_checkpoint_lock(), std::defer_lock);
    if (!use_try_lock) {
      lock.lock();
    } else if (!lock.try_lock()) {
        return STATUS(InternalError, "Unable to acquire checkpoint lock");
    }

    if (!has_regular_db()) {
      LOG_WITH_PREFIX(INFO) << "Skipped creating checkpoint in " << dir;
      return STATUS(NotSupported,
                    "Tablet does not have a RocksDB (could be a transaction status tablet)");
    }

    auto parent_dir = DirName(dir);
    RETURN_NOT_OK_PREPEND(metadata().fs_manager()->CreateDirIfMissing(parent_dir),
                          Format("Unable to create checkpoints directory $0", parent_dir));

    TEST_PAUSE_IF_FLAG(TEST_pause_create_checkpoint);

    // Order does not matter because we flush both DBs and does not have parallel writes.
    status = DoCreateCheckpoint(dir, create_checkpoint_in);
  }
  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Create checkpoint status: " << status;
    return STATUS_FORMAT(IllegalState, "Unable to create checkpoint: $0", status);
  }
  LOG_WITH_PREFIX(INFO) << "Checkpoint created in " << dir;

  TEST_last_rocksdb_checkpoint_dir_ = dir;

  return Status::OK();
}

Status TabletSnapshots::DoCreateCheckpoint(
    const std::string& dir, CreateCheckpointIn create_checkpoint_in) {
  const auto temp_intents_dir = docdb::GetStorageDir(dir, docdb::kIntentsDirName);
  if (has_intents_db()) {
    RETURN_NOT_OK(rocksdb::checkpoint::CreateCheckpoint(&intents_db(), temp_intents_dir));
  }
  RETURN_NOT_OK(rocksdb::checkpoint::CreateCheckpoint(&regular_db(), dir));

  // Vector indexes checkpoint must be created after rocksdb checkpoint
  // to be in sync with the flushed data.
  if (auto vector_indexes = VectorIndexesList()) {
    for (const auto& vector_index : *vector_indexes) {
      const auto storage_name = docdb::GetVectorIndexStorageName(vector_index->options());
      const auto checkpoint_dir = create_checkpoint_in == CreateCheckpointIn::kSubDir
          ? docdb::GetStorageCheckpointDir(dir, storage_name)
          : docdb::GetStorageDir(dir, storage_name);
      RETURN_NOT_OK(vector_index->CreateCheckpoint(checkpoint_dir));
    }
  }

  // TODO: not clear why temp dir is used rather than direct creation by the corresponding path.
  if (has_intents_db() && create_checkpoint_in == CreateCheckpointIn::kSubDir) {
    const auto final_intents_dir = docdb::GetStorageCheckpointDir(dir, docdb::kIntentsDirName);
    RETURN_NOT_OK(Env::Default()->RenameFile(temp_intents_dir, final_intents_dir));
  }

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

HybridTime TabletSnapshots::AllowedHistoryCutoff() {
  auto schedules = metadata().SnapshotSchedules();
  if (schedules.empty()) {
    return HybridTime::kMax;
  }
  auto result = HybridTime::kMax;
  std::lock_guard lock(last_snapshot_ht_mutex_);
  VLOG_WITH_PREFIX_AND_FUNC(4)
      << "schedules: " << AsString(schedules) << ", last snapshot ht: "
      << AsString(last_snapshot_ht_);
  for (const auto& schedule : schedules) {
    auto it = last_snapshot_ht_.find(schedule);
    result.MakeAtMost(it != last_snapshot_ht_.end() ? it->second : HybridTime::kMin);
  }
  return result;
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
      LWQLValuePB value_pb(nullptr);
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
      LWQLValuePB value_pb(nullptr);
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

} // namespace yb::tablet
