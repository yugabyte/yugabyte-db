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

#include <float.h>

#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/mpl/and.hpp>
#include "yb/util/flags.h"

#include "yb/common/entity_ids.h"
#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/ref_counted.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"

#include "yb/server/monitored_task.h"

#include "yb/util/status_fwd.h"
#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/shared_lock.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

namespace yb {
namespace master {

class CatalogManager;

// Implements a multi-stage alter table. As of Dec 30 2019, used for adding an
// index to an existing table, such that the index can be backfilled with
// historic data in an online manner.
//
class MultiStageAlterTable {
 public:
  // Launches the next stage of the multi stage schema change. Updates the
  // table info, upon the completion of an alter table round if we are in the
  // middle of an index backfill. Will update the IndexPermission from
  // INDEX_PERM_DELETE_ONLY -> INDEX_PERM_WRITE_AND_DELETE -> BACKFILL
  static Status LaunchNextTableInfoVersionIfNecessary(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& Info, uint32_t current_version,
      const LeaderEpoch& epoch, bool respect_backfill_deferrals = true);

  // Clears the fully_applied_* state for the given table and optionally sets it to RUNNING.
  // If the version has changed and does not match the expected version no
  // change is made.
  static Status ClearFullyAppliedAndUpdateState(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& table,
      boost::optional<uint32_t> expected_version, bool update_state_to_running,
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
      const std::unordered_map<TableId, IndexPermissions>& perm_mapping,
      const LeaderEpoch& epoch,
      boost::optional<uint32_t> current_version = boost::none);

  // TODO(jason): make this private when closing issue #6218.
  // Start Index Backfill process/step for the specified table/index.
  static Status
  StartBackfillingData(CatalogManager *catalog_manager,
                       const scoped_refptr<TableInfo> &indexed_table,
                       const std::vector<IndexInfoPB>& idx_infos,
                       boost::optional<uint32_t> expected_version,
                       const LeaderEpoch& epoch);
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
                LeaderEpoch epoch);

  Status Launch();

  Status UpdateSafeTime(const Status& s, HybridTime ht) EXCLUDES(mutex_);

  Status Done(const Status& s, const std::unordered_set<TableId>& failed_indexes);

  Master* master() { return master_; }

  ThreadPool* threadpool() { return callback_pool_; }

  const std::string& requested_index_names() const { return requested_index_names_; }

  int32_t schema_version() const { return schema_version_; }

  std::string LogPrefix() const;

  std::string description() const;

  bool done() const {
    return done_.load(std::memory_order_acquire);
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

  const std::unordered_set<TableId> indexes_to_build() const;

  const TableId& indexed_table_id() const { return indexed_table_->id(); }

  scoped_refptr<TableInfo> table() { return indexed_table_; }

  Status UpdateRowsProcessedForIndexTable(const uint64_t number_rows_processed);

  const uint64_t number_rows_processed() const { return number_rows_processed_; }

  const LeaderEpoch& epoch() const { return epoch_; }

 private:
  void LaunchBackfillOrAbort();
  Status WaitForTabletSplitting();
  Status DoLaunchBackfill();
  Status LaunchComputeSafeTimeForRead() EXCLUDES(mutex_);
  Status DoBackfill();

  Status MarkAllIndexesAsFailed();
  Status MarkAllIndexesAsSuccess();

  Status MarkIndexesAsFailed(
      const std::unordered_set<TableId>& indexes, const std::string& message);
  Status MarkIndexesAsDesired(
      const std::unordered_set<TableId>& index_ids, BackfillJobPB_State state,
      const std::string message);

  Status AlterTableStateToAbort();
  Status AlterTableStateToSuccess();

  Status Abort();
  Status CheckIfDone();
  Status UpdateIndexPermissionsForIndexes();
  Status ClearCheckpointStateInTablets();
  Status SetSafeTimeAndStartBackfill(const HybridTime& read_time) EXCLUDES(mutex_);

  // Persist the value in read_time_for_backfill_ to the sys-catalog and start the backfill job.
  Status PersistSafeTimeAndStartBackfill() EXCLUDES(mutex_);

  // We want to prevent major compactions from garbage collecting delete markers
  // on an index table, until the backfill process is complete.
  // This API is used at the end of a successful backfill to enable major compactions
  // to gc delete markers on an index table.
  Status AllowCompactionsToGCDeleteMarkers(const TableId& index_table_id);

  // Send the "backfill done request" to all tablets of the specified table.
  Status SendRpcToAllowCompactionsToGCDeleteMarkers(
      const scoped_refptr<TableInfo> &index_table);
  // Send the "backfill done request" to the specified tablet.
  Status SendRpcToAllowCompactionsToGCDeleteMarkers(
      const scoped_refptr<TabletInfo> &index_table_tablet, const std::string &table_id);

  Master* master_;
  ThreadPool* callback_pool_;
  const scoped_refptr<TableInfo> indexed_table_;
  const std::vector<IndexInfoPB> index_infos_;
  int32_t schema_version_;
  std::atomic<uint64> number_rows_processed_;

  std::atomic_bool done_{false};
  std::atomic_bool timestamp_chosen_{false};
  std::atomic<size_t> tablets_pending_;
  std::atomic<size_t> num_tablets_;
  std::shared_ptr<BackfillTableJob> backfill_job_;
  mutable simple_spinlock mutex_;
  HybridTime read_time_for_backfill_ GUARDED_BY(mutex_){HybridTime::kMin};
  const std::unordered_set<TableId> requested_index_ids_;
  const std::string requested_index_names_;

  const scoped_refptr<NamespaceInfo> ns_info_;
  LeaderEpoch epoch_;
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

  server::MonitoredTaskState AbortAndReturnPrevState(const Status& status) override;

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
      std::shared_ptr<BackfillTable> backfill_table, const scoped_refptr<TabletInfo>& tablet);

  Status Launch() { return LaunchNextChunkOrDone(); }

  Status LaunchNextChunkOrDone();
  Status Done(
      const Status& status,
      const boost::optional<std::string>& backfilled_until,
      const uint64_t number_rows_processed,
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

  const std::string& requested_index_names() { return backfill_table_->requested_index_names(); }

  int32_t schema_version() { return backfill_table_->schema_version(); }

  const scoped_refptr<TabletInfo> tablet() { return tablet_; }

  bool done() const {
    return done_.load(std::memory_order_acquire);
  }

  std::string LogPrefix() const;

  const std::string GetNamespaceName() const { return backfill_table_->GetNamespaceName(); }

 private:
  Status UpdateBackfilledUntil(
      const std::string& backfilled_until, const uint64_t number_rows_processed);

  std::shared_ptr<BackfillTable> backfill_table_;
  const scoped_refptr<TabletInfo> tablet_;
  dockv::Partition partition_;

  // if non-empty, corresponds to the row in the tablet up to which
  // backfill has been already processed (non-inclusive). The next
  // request to backfill has to start backfilling from this row till
  // the end of the tablet range.
  std::string backfilled_until_;
  std::atomic_bool done_{false};
};

class GetSafeTimeForTablet : public RetryingTSRpcTask {
 public:
  GetSafeTimeForTablet(
      std::shared_ptr<BackfillTable> backfill_table,
      const scoped_refptr<TabletInfo>& tablet,
      HybridTime min_cutoff,
      LeaderEpoch epoch)
      : RetryingTSRpcTask(
            backfill_table->master(), backfill_table->threadpool(),
            std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)), tablet->table().get(),
            std::move(epoch),
            /* async_task_throttler */ nullptr),
        backfill_table_(backfill_table),
        tablet_(tablet),
        min_cutoff_(min_cutoff) {
    deadline_ = MonoTime::Max();  // Never time out.
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

  TabletServerId permanent_uuid();

  tserver::GetSafeTimeResponsePB resp_;
  const std::shared_ptr<BackfillTable> backfill_table_;
  const scoped_refptr<TabletInfo> tablet_;
  const HybridTime min_cutoff_;
};

// A background task which is responsible for backfilling rows in the partitions
// [start, end) on the indexed table.
class BackfillChunk : public RetryingTSRpcTask {
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

  MonoTime ComputeDeadline() override;

 private:
  TabletId tablet_id() const override { return backfill_tablet_->tablet()->id(); }

  void HandleResponse(int attempt) override;

  bool SendRequest(int attempt) override;

  void UnregisterAsyncTaskCallback() override;

  TabletServerId permanent_uuid();

  int num_max_retries() override;
  int max_delay_ms() override;

  TableType GetTableType() const {
    return backfill_tablet_->tablet()->table()->GetTableType();
  }

  const std::unordered_set<TableId> indexes_being_backfilled_;
  tserver::BackfillIndexResponsePB resp_;
  std::shared_ptr<BackfillTablet> backfill_tablet_;
  std::string start_key_;
  const std::string requested_index_names_;
};

}  // namespace master
}  // namespace yb
