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

#pragma once

#include <float.h>

#include <chrono>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/mpl/and.hpp>

#include "yb/ash/wait_state.h"
#include "yb/common/entity_ids.h"
#include "yb/common/transaction.h"
#include "yb/dockv/partition.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/ref_counted.h"

#include "yb/master/async_rpc_tasks_base.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/ysql_ddl_verification_task.h"

#include "yb/qlexpr/index.h"

#include "yb/server/monitored_task.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_fwd.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

#include "yb/tserver/tserver_admin.pb.h"

namespace yb {
namespace master {

class CatalogManager;

// Implements a multi-stage alter table. As of Dec 30 2019, used for adding an
// index to an existing table, such that the index can be backfilled with
// historic data in an online manner.
//
class MultiStageAlterTable {
 public:
  // Advance a YSQL index from WRITE_AND_DELETE to DO_BACKFILL on postgres's request through
  // CatalogManager::BackfillIndex, and alert tservers.  The backfill launches from
  // HandleSchemaVersionReported once every tablet has applied the new permission.  If
  // requester_transaction is provided, it is stored so that the backfill can monitor the liveness
  // of the PG backend that initiated it.
  static Status AdvanceYsqlIndexToBackfill(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& indexed_table,
      uint32_t current_version, const LeaderEpoch& epoch,
      std::optional<TransactionMetadata> requester_transaction);

  // Advance YCQL indexes through the multi stage permission state machine
  // (INDEX_PERM_DELETE_ONLY -> INDEX_PERM_WRITE_AND_DELETE -> INDEX_PERM_DO_BACKFILL, and
  // the removal stages), launching backfill or deletion when an index reaches the
  // corresponding permission.  Driven by the yb-admin backfill trigger through
  // CatalogManager::LaunchBackfillIndexForTable, so backfill deferrals are ignored: launching
  // deferred backfills is what the trigger is for.
  static Status AdvanceYcqlIndexPermissions(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& indexed_table,
      uint32_t current_version, const LeaderEpoch& epoch);

  // React to all tablets having applied the given schema version: continue the permission
  // state machine for YCQL, resume or launch pending backfills, and clear the fully applied
  // state when there is nothing left to do.
  static Status HandleSchemaVersionReported(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& indexed_table,
      uint32_t current_version, const LeaderEpoch& epoch);

  // Clears the fully_applied_* state for the given table and optionally sets it to RUNNING.
  // If the version has changed and does not match the expected version no
  // change is made.
  static Status ClearFullyAppliedAndUpdateState(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& table,
      std::optional<uint32_t> expected_version, bool update_state_to_running,
      const LeaderEpoch& epoch);

  // Copies the current schema, schema_version, indexes and index_info
  // into their fully_applied_* equivalents. This is useful to ensure
  // that the master returns the fully applied version of the table schema
  // while the next alter table is in progress.
  static void CopySchemaDetailsToFullyApplied(SysTablesEntryPB* state);

  // Updates and persists the IndexPermission corresponding to the index_table_id for
  // the indexed_table's TableInfo.
  // Returns whether any permissions were actually updated (leading to a version being incremented).
  static Result<bool> UpdateIndexPermission(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& indexed_table,
      const std::unordered_map<TableId, IndexPermissions>& perm_mapping, const LeaderEpoch& epoch,
      std::optional<uint32_t> current_version = std::nullopt);

 private:
  // What ClassifyIndexes found in the indexed table, bucketed by the transition each index is
  // eligible for.  It records no decisions: which buckets become work is up to the entry point.
  struct IndexClassification {
    // YCQL indexes at any permission other than READ_WRITE_AND_DELETE, DO_BACKFILL, and
    // INDEX_UNUSED, with the permission each moves to next.  The master drives these transitions
    // on its own.
    std::unordered_map<TableId, IndexPermissions> ycql_permission_updates;
    // YSQL indexes below INDEX_PERM_DO_BACKFILL, with the permission each moves to next.  In
    // practice, that is WRITE_AND_DELETE moving to DO_BACKFILL, since YSQL indexes are created at
    // WRITE_AND_DELETE.  Postgres drives these transitions, so they become work only when postgres
    // asks through CatalogManager::BackfillIndex.
    std::unordered_map<TableId, IndexPermissions> ysql_permission_updates;
    // Indexes at INDEX_PERM_DO_BACKFILL, in table order.  deferrable marks the ones an entry point
    // that honors backfill deferrals holds back for a later trigger.
    struct ReadyIndex {
      IndexInfoPB info;
      bool deferrable;
    };
    std::vector<ReadyIndex> ready_to_backfill;
    // YCQL indexes at INDEX_PERM_INDEX_UNUSED.  A YSQL index at that permission is ignored with a
    // warning, see ClassifyIndexes.
    std::vector<IndexInfoPB> indexes_to_delete;
    // The indexes of the table's recorded backfill job when no backfill is running: the job was
    // lost, most likely to a master failover, and must restart with exactly that set.
    std::optional<std::vector<IndexInfoPB>> lost_backfill;
    // Whether the table already has a backfill running.  Read before the indexed table's read lock
    // is taken.  It only decides whether ApplyIndexStateMachineActions treats a nonempty backfill
    // list as work to launch, and StartBackfillingData re-checks it in SetIsBackfilling.
    bool is_backfilling = false;
    bool is_ysql_table = false;
  };

  // The one decision an entry point makes from a classification: which ready indexes to back fill
  // now, and which to hold back as riders that launch only alongside them.
  struct BackfillChoice {
    std::vector<IndexInfoPB> indexes_to_backfill;
    std::vector<IndexInfoPB> deferred_indexes;
  };

  // Examine the indexed table under its read lock and bucket its indexes.  Returns nullopt if the
  // table's version differs from current_version, meaning another thread already launched the
  // next version.
  static std::optional<IndexClassification> ClassifyIndexes(
      const scoped_refptr<TableInfo>& indexed_table, uint32_t current_version);

  // Act on a classification and the entry point's backfill choice, for the two entry points that
  // carry backfill work.  Restarts a lost backfill in place of whatever the entry point chose,
  // truncates the list when batching is off, clears the fully applied state when there is nothing
  // to do, and otherwise hands the work to the per-table-type step below.
  static Status ApplyIndexStateMachineActions(
      CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table,
      const IndexClassification& classification, BackfillChoice choice, uint32_t current_version,
      const LeaderEpoch& epoch);

  // The YSQL step, reached only from the schema version report: start backfilling the ready
  // indexes.  Postgres drives YSQL permissions, so the report carries no permission update, and the
  // master deletes and defers only YCQL indexes.
  static Status LaunchYsqlBackfill(
      CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table,
      const IndexClassification& classification, BackfillChoice choice, uint32_t current_version,
      const LeaderEpoch& epoch);

  // The master drives YCQL index permissions itself: persist the next permission for every index
  // that has one and alert tservers, delete an index that reached INDEX_PERM_INDEX_UNUSED, or start
  // backfilling the ready indexes together with any deferred ones being released.
  static Status ApplyYcqlActions(
      CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table,
      const IndexClassification& classification, BackfillChoice choice, uint32_t current_version,
      const LeaderEpoch& epoch);

  // Start Index Backfill process/step for the specified table/index.  If requester_transaction is
  // provided, it will be used to monitor the liveness of the PG backend that initiated the
  // backfill.
  static Status StartBackfillingData(
      CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table,
      const std::vector<IndexInfoPB>& idx_infos, std::optional<uint32_t> expected_version,
      const LeaderEpoch& epoch,
      std::optional<TransactionMetadata> requester_transaction);
};

class BackfillTablet;
class BackfillChunk;
class BackfillTableJob;

// This class is responsible for backfilling the specified indexes on the
// indexed_table.
class BackfillTable : public std::enable_shared_from_this<BackfillTable> {
 public:
  BackfillTable(Master *master, ThreadPool *callback_pool,
                const scoped_refptr<TableInfo> &indexed_table,
                std::vector<IndexInfoPB> indexes,
                const scoped_refptr<NamespaceInfo> &ns_info,
                LeaderEpoch epoch,
                std::optional<TransactionMetadata> requester_transaction);

  Status Launch();

  Status UpdateSafeTime(const Status& s, HybridTime ht) EXCLUDES(mutex_);

  Status Done(const Status& s, const std::unordered_set<TableId>& failed_indexes);

  Master* master() { return master_; }

  ThreadPool* threadpool() { return callback_pool_; }

  const std::string& requested_index_names() const { return requested_index_names_; }

  int32_t schema_version() const { return schema_version_; }

  std::string LogPrefix() const;

  std::string description() const;

  enum class State : uint8_t {
    kRunning,
    kSuccess,
    kFailed,
  };

  State state() const {
    return state_.load(std::memory_order_acquire);
  }

  bool done() const {
    return state() != State::kRunning;
  }

  bool timestamp_chosen() const {
    return timestamp_chosen_.load(std::memory_order_acquire);
  }

  HybridTime read_time_for_backfill() const EXCLUDES(mutex_) {
    std::lock_guard l(mutex_);
    return read_time_for_backfill_;
  }

  const std::string GetNamespaceName() const;

  const std::vector<IndexInfoPB>& index_infos() const { return index_infos_; }

  // Immutable per-job uniqueness-check mode; set in the constructor (persisted value for a
  // resumed job, freshly selected for a new one) and persisted by Launch().
  UniqueIndexBackfillMode unique_index_backfill_mode() const {
    return unique_index_backfill_mode_;
  }

  const std::unordered_set<TableId> indexes_to_build() const;

  const TableId& indexed_table_id() const { return indexed_table_->id(); }

  scoped_refptr<TableInfo> table() { return indexed_table_; }

  Status UpdateRowsProcessedForIndexTable(
      const uint64_t num_rows_read_from_table_for_backfill,
      const std::unordered_map<TableId, double>& num_rows_backfilled_in_index);

  const LeaderEpoch& epoch() const { return epoch_; }

  bool using_table_locks() const { return using_table_locks_.load(std::memory_order_acquire); }

  const ash::WaitStateInfoPtr& wait_state() const { return wait_state_; }

  static bool GetIndexTableRetainsDeleteMarkers(const PersistentTableInfo& index_table);

  static void UnsetIndexTableRetainsDeleteMarkers(PersistentTableInfo* index_table);

  Status Abort(bool from_liveness = false);

 private:
  void LaunchBackfillOrAbort();
  Status WaitForTabletSplitting();
  Status DoLaunchBackfill();
  Status LaunchComputeSafeTimeForRead() EXCLUDES(mutex_);
  Status DoBackfill();

  Status MarkAllIndexesAsFailed();
  Status MarkAllIndexesAsSuccess();

  Status MarkIndexesAsFailed(
      const std::unordered_set<TableId>& indexes, const Status& backfill_status);
  Status MarkIndexesAsDesired(
      const std::unordered_set<TableId>& index_ids, BackfillJobPB_State state,
      const Status& backfill_status);

  Status AlterTableStateToAbort();
  Status AlterTableStateToSuccess();

  void StartRequesterLivenessMonitor();
  void StopLivenessMonitor();
  Status CheckIfDone();
  Status UpdateIndexPermissionsForIndexes();
  Status ClearCheckpointStateInTablets();
  Status SetSafeTimeAndStartBackfill(const HybridTime& read_time) EXCLUDES(mutex_);

  // Persist the value in read_time_for_backfill_ to the sys-catalog and start the backfill job.
  Status PersistSafeTimeAndStartBackfill() EXCLUDES(mutex_);

  // For the xCluster replicated backfill path, the source runs the backfill and replicates the
  // writes, so there is no local backfill and we can just mark the backfill as done.
  Status FinalizeReplicatedIndexBackfill(HybridTime source_backfill_ht);

  // We want to prevent major compactions from garbage collecting delete markers
  // on an index table, until the backfill process is complete.
  // This API is used at the end of a successful backfill to enable major compactions
  // to gc delete markers on an index table.
  Status AllowCompactionsToGCDeleteMarkers(const TableId& index_table_id);

  // Send the "backfill done request" to all tablets of the specified table.
  Status SendRpcToAllowCompactionsToGCDeleteMarkers(
      const TableInfoPtr& index_table);
  // Send the "backfill done request" to the specified tablet.
  Status SendRpcToAllowCompactionsToGCDeleteMarkers(
      const TabletInfoPtr& index_table_tablet, const std::string& table_id);

  Master* master_;
  ThreadPool* callback_pool_;
  const scoped_refptr<TableInfo> indexed_table_;
  const std::vector<IndexInfoPB> index_infos_;
  int32_t schema_version_;
  UniqueIndexBackfillMode unique_index_backfill_mode_ =
      UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_CHECK_ALL;

  std::atomic<State> state_{State::kRunning};
  std::atomic_bool timestamp_chosen_{false};
  std::atomic<size_t> tablets_pending_;
  std::atomic<size_t> num_tablets_;
  std::atomic_bool using_table_locks_{false};
  std::shared_ptr<BackfillTableJob> backfill_job_;
  mutable simple_spinlock mutex_;
  HybridTime read_time_for_backfill_ GUARDED_BY(mutex_){HybridTime::kMin};
  const std::unordered_set<TableId> requested_index_ids_;
  const std::string requested_index_names_;

  const scoped_refptr<NamespaceInfo> ns_info_;
  LeaderEpoch epoch_;
  ash::WaitStateInfoPtr wait_state_;
  std::optional<TransactionMetadata> requester_transaction_;
  std::weak_ptr<DdlRequesterLivenessTask> liveness_task_ GUARDED_BY(mutex_);
};


class BackfillTableJob : public server::MonitoredTask {
 public:
  explicit BackfillTableJob(std::shared_ptr<BackfillTable> backfill_table)
      : backfill_table_(backfill_table),
        requested_index_names_(backfill_table_->requested_index_names()) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kBackfillTable;
  }

  std::string type_name() const override { return "Backfill Table"; }

  std::string description() const override;

  void SetState(server::MonitoredTaskState new_state);

  server::MonitoredTaskState AbortAndReturnPrevState(
      const Status& status, bool call_task_finisher) override;

  void MarkDone();

 private:
  std::shared_ptr<BackfillTable> backfill_table_;
  const std::string requested_index_names_;
};

// A background task which is responsible for backfilling rows from a given
// tablet in the indexed table.
class BackfillTablet : public std::enable_shared_from_this<BackfillTablet> {
 public:
  BackfillTablet(
      std::shared_ptr<BackfillTable> backfill_table, TabletInfoPtr&& tablet);

  Status Launch() { return LaunchNextChunkOrDone(); }

  Status LaunchNextChunkOrDone();
  Status Done(
      const Status& status,
      const std::optional<std::string>& backfilled_until,
      const uint64_t num_rows_read_from_table_for_backfill,
      const std::unordered_map<TableId, double>& num_rows_backfilled_in_index,
      const std::unordered_set<TableId>& failed_indexes);

  Master* master() { return backfill_table_->master(); }

  ThreadPool* threadpool() { return backfill_table_->threadpool(); }

  HybridTime read_time_for_backfill() {
    return backfill_table_->read_time_for_backfill();
  }

  const std::unordered_set<TableId> indexes_to_build() {
    return backfill_table_->indexes_to_build();
  }
  const TableId& indexed_table_id() { return backfill_table_->indexed_table_id(); }
  const std::vector<IndexInfoPB>& index_infos() const { return backfill_table_->index_infos(); }

  UniqueIndexBackfillMode unique_index_backfill_mode() const {
    return backfill_table_->unique_index_backfill_mode();
  }

  const std::string& requested_index_names() { return backfill_table_->requested_index_names(); }

  int32_t schema_version() { return backfill_table_->schema_version(); }

  const TabletInfoPtr& tablet() { return tablet_; }

  bool done() const {
    return done_.load(std::memory_order_acquire);
  }

  std::string LogPrefix() const;

  const std::string GetNamespaceName() const { return backfill_table_->GetNamespaceName(); }

  const ash::WaitStateInfoPtr& wait_state() const {
    return backfill_table_->wait_state();
  }

 private:
  Status UpdateBackfilledUntil(
      const std::string& backfilled_until, const uint64_t num_rows_read_from_table_for_backfill,
      const std::unordered_map<TableId, double>& num_rows_backfilled_in_index);

  std::shared_ptr<BackfillTable> backfill_table_;
  const TabletInfoPtr tablet_;
  dockv::Partition partition_;

  // if non-empty, corresponds to the row in the tablet up to which
  // backfill has been already processed (non-inclusive). The next
  // request to backfill has to start backfilling from this row till
  // the end of the tablet range.
  std::string backfilled_until_;
  std::atomic_bool done_{false};
};

class GetSafeTimeForTablet : public RetryingTSRpcTaskWithTable {
 public:
  GetSafeTimeForTablet(
      std::shared_ptr<BackfillTable> backfill_table,
      const TabletInfoPtr& tablet,
      HybridTime min_cutoff,
      LeaderEpoch epoch)
      : RetryingTSRpcTaskWithTable(
            backfill_table->master(), backfill_table->threadpool(),
            std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)), tablet->table(),
            std::move(epoch),
            /* async_task_throttler */ nullptr),
        backfill_table_(backfill_table),
        tablet_(tablet),
        min_cutoff_(min_cutoff) {
    // No deadline for the task, refer to ComputeDeadline() for a single attempt deadline.
    deadline_ = MonoTime::Max();
  }

  Status Launch();

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kGetSafeTime;
  }

  std::string type_name() const override { return "Get SafeTime for Tablet"; }

  std::string description() const override {
    return yb::Format("GetSafeTime for $0 Backfilling index tables $1",
                      tablet_id(), backfill_table_->requested_index_names());
  }

 private:
  TabletId tablet_id() const override { return tablet_->id(); }

  void HandleResponse(int attempt) override;

  bool SendRequest(int attempt) override;

  void UnregisterAsyncTaskCallback() override;

  tserver::GetSafeTimeResponsePB resp_;
  const std::shared_ptr<BackfillTable> backfill_table_;
  const TabletInfoPtr tablet_;
  const HybridTime min_cutoff_;
};

// A background task which is responsible for backfilling rows in the partitions
// [start, end) on the indexed table.
class BackfillChunk : public RetryingTSRpcTaskWithTable {
 public:
  BackfillChunk(std::shared_ptr<BackfillTablet> backfill_tablet,
                const std::string& start_key,
                LeaderEpoch epoch);

  Status Launch();

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kBackfillTabletChunk;
  }

  std::string type_name() const override { return "Backfill Index Table"; }

  std::string description() const override;

  MonoTime ComputeDeadline() const override;

 private:
  TabletId tablet_id() const override { return backfill_tablet_->tablet()->id(); }

  void HandleResponse(int attempt) override;

  bool SendRequest(int attempt) override;

  void UnregisterAsyncTaskCallback() override;

  int num_max_retries() override;
  int max_delay_ms() override;

  TableType GetTableType() const {
    return backfill_tablet_->tablet()->table()->GetTableType();
  }

  const std::unordered_set<TableId> indexes_being_backfilled_;
  std::unordered_map<TableId, double> num_rows_backfilled_in_index_;
  tserver::BackfillIndexResponsePB resp_;
  std::shared_ptr<BackfillTablet> backfill_tablet_;
  std::string start_key_;
  const std::string requested_index_names_;
};

}  // namespace master
}  // namespace yb
