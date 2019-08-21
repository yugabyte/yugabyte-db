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

#ifndef YB_MASTER_BACKFILL_INDEX_H
#define YB_MASTER_BACKFILL_INDEX_H

#include <string>
#include <vector>

#include "yb/common/index.h"
#include "yb/common/partition.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/server/monitored_task.h"
#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"

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
  // Returns true if the next version of table schema/info was kicked off.
  // Returns false otherwise -- so that the alter table operation can be
  // considered to have completed.
  static bool LaunchNextTableInfoVersionIfNecessary(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& Info);

  // Updates and persists the IndexPermission corresponding to the index_table_id for
  // the indexed_table's TableInfo.
  static Status UpdateIndexPermission(
      CatalogManager* mgr, const scoped_refptr<TableInfo>& indexed_table,
      const TableId& index_table_id, IndexPermissions new_perm);

 private:
  // Start Index Backfill process/step for the specified table/index.
  static void
  StartBackfillingData(CatalogManager *catalog_manager,
                       const scoped_refptr<TableInfo> &indexed_table,
                       IndexInfoPB idx_info);
};

class BackfillTablet;
class BackfillChunk;

// This class is responsible for backfilling the specified indices on the
// indexed_table.
class BackfillTable : public std::enable_shared_from_this<BackfillTable> {
 public:
  BackfillTable(Master *master, ThreadPool *callback_pool,
                const scoped_refptr<TableInfo> &indexed_table,
                std::vector<IndexInfoPB> indices)
      : master_(master), callback_pool_(callback_pool),
        indexed_table_(indexed_table), indices_to_build_(indices) {
    LOG_IF(DFATAL, indices_to_build_.size() != 1)
        << "As of Dec 2019, we only support "
        << "building one index at a time. indices_to_build_.size() = "
        << indices_to_build_.size();
    auto l = indexed_table_->LockForRead();
    schema_version_ = indexed_table_->metadata().state().pb.version();
  }

  void Launch();

  void UpdateSafeTime(const Status& s, HybridTime ht);

  void Done(const Status& s);

  Master* master() { return master_; }

  ThreadPool* threadpool() { return callback_pool_; }

  const std::vector<IndexInfoPB>& indices() const { return indices_to_build_; }

  int32_t schema_version() const { return schema_version_; }

  std::string LogPrefix() const;

  bool done() const {
    return done_.load(std::memory_order_acquire);
  }

  HybridTime read_time_for_backfill() const {
    std::lock_guard<simple_spinlock> l(mutex_);
    return read_time_for_backfill_;
  }

 private:
  void LaunchComputeSafeTimeForRead();

  void LaunchBackfill();

  void AlterTableStateToSuccess();

  void AlterTableStateToAbort();

  void ClearCheckpointStateInTablets();

  // We want to prevent major compactions from garbage collecting delete markers
  // on an index table, until the backfill process is complete.
  // This API is used at the end of a successful backfill to enable major compactions
  // to gc delete markers on an index table.
  void AllowCompactionsToGCDeleteMarkers(const TableId& index_table_id);

  // Send the "backfill done request" to all tablets of the specified table.
  void SendRpcToAllowCompactionsToGCDeleteMarkers(const scoped_refptr<TableInfo>& index_table);

  // Send the "backfill done request" to the specified tablet.
  void SendRpcToAllowCompactionsToGCDeleteMarkers(
      const scoped_refptr<TabletInfo>& index_table_tablet);

  Master* master_;
  ThreadPool* callback_pool_;
  const scoped_refptr<TableInfo> indexed_table_;
  const std::vector<IndexInfoPB> indices_to_build_;
  int32_t schema_version_;

  std::atomic_bool done_;
  std::atomic<size_t> tablets_pending_;
  mutable simple_spinlock mutex_;
  HybridTime read_time_for_backfill_ GUARDED_BY(mutex_){HybridTime::kMin};
};

// A background task which is responsible for backfilling rows from a given
// tablet in the indexed table.
class BackfillTablet : public std::enable_shared_from_this<BackfillTablet> {
 public:
  BackfillTablet(
      std::shared_ptr<BackfillTable> backfill_table, const scoped_refptr<TabletInfo>& tablet);

  void Launch() { LaunchNextChunk(); }

  void LaunchNextChunk();
  void Done(const Status& status);

  Master* master() { return backfill_table_->master(); }

  ThreadPool* threadpool() { return backfill_table_->threadpool(); }

  HybridTime read_time_for_backfill() {
    return backfill_table_->read_time_for_backfill();
  }

  const std::vector<IndexInfoPB>& indices() { return backfill_table_->indices(); }

  int32_t schema_version() { return backfill_table_->schema_version(); }

  // Returns the partition key corresponding to the end of this chunk. This is encoded
  // using the same hashing/partition scheme as used by the main/indexed table.
  // As of Dec 2019, we consider the whole tablet range to be one chunk. But the plan is
  // to subdivide this into smaller chunks going forward for better checkpointing
  // and restartability (#2615).
  std::string GetChunkEnd(std::string start) {
    // TODO(#2615) : Ideally we want to split one tablet into multiple chunks, so that
    // each chunk can make progress and finish relatively quickly.
    return partition_.partition_key_end();
  }

  const scoped_refptr<TabletInfo> tablet() { return tablet_; }

 private:
  std::shared_ptr<BackfillTable> backfill_table_;
  const scoped_refptr<TabletInfo> tablet_;
  Partition partition_;

  // partition keys corresponding to the start/end of the chunk being processed,
  // and how far backfill has been already processed.
  std::string chunk_start_, chunk_end_, processed_until_;
};

class GetSafeTimeForTablet : public RetryingTSRpcTask {
 public:
  GetSafeTimeForTablet(
      std::shared_ptr<BackfillTable> backfill_table,
      const scoped_refptr<TabletInfo>& tablet,
      HybridTime min_cutoff)
      : RetryingTSRpcTask(
            backfill_table->master(), backfill_table->threadpool(),
            gscoped_ptr<TSPicker>(new PickLeaderReplica(tablet)), tablet->table().get()),
        backfill_table_(backfill_table),
        tablet_(tablet),
        min_cutoff_(min_cutoff) {
    deadline_ = MonoTime::Max();  // Never time out.
  }

  void Launch();

  Type type() const override { return ASYNC_GET_SAFE_TIME; }

  std::string type_name() const override { return "Get SafeTime for Tablet"; }

  std::string description() const override {
    return yb::Format(
        "Fetch SafeTime for Backfilling index tablet for $0. Indices $1",
        tablet_,
        backfill_table_->indices());
  }

 private:
  TabletId tablet_id() const override { return tablet_->id(); }

  void HandleResponse(int attempt) override;

  bool SendRequest(int attempt) override;

  void UnregisterAsyncTaskCallback() override;

  TabletServerId permanent_uuid() {
    return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
  }

  tserver::GetSafeTimeResponsePB resp_;
  const std::shared_ptr<BackfillTable> backfill_table_;
  const scoped_refptr<TabletInfo> tablet_;
  const HybridTime min_cutoff_;
};

// A background task which is responsible for backfilling rows in the partitions
// [start, end) on the indexed table.
class BackfillChunk : public RetryingTSRpcTask {
 public:
  BackfillChunk(
      std::shared_ptr<BackfillTablet> backfill_tablet, const std::string& start,
      const std::string& end)
      : RetryingTSRpcTask(
            backfill_tablet->master(), backfill_tablet->threadpool(),
            gscoped_ptr<TSPicker>(new PickLeaderReplica(backfill_tablet->tablet())),
            backfill_tablet->tablet()->table().get()),
        backfill_tablet_(backfill_tablet),
        start_key_(start),
        end_key_(end) {
    deadline_ = MonoTime::Max();  // Never time out.
  }

  void Launch();

  Type type() const override { return ASYNC_BACKFILL_TABLET_CHUNK; }

  std::string type_name() const override { return "Backfill Index Table"; }

  std::string description() const override {
    return yb::Format(
        "Backfilling index tablet for $0 from $1 to $2 for indices $3",
        backfill_tablet_->tablet(), "start_key_", "end_key_",
        backfill_tablet_->indices());
  }

  MonoTime ComputeDeadline() override;

 private:
  TabletId tablet_id() const override { return backfill_tablet_->tablet()->id(); }

  void HandleResponse(int attempt) override;

  bool SendRequest(int attempt) override;

  void UnregisterAsyncTaskCallback() override;

  TabletServerId permanent_uuid() {
    return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
  }

  int num_max_retries() override;
  int max_delay_ms() override;

  tserver::BackfillIndexResponsePB resp_;
  std::shared_ptr<BackfillTablet> backfill_tablet_;
  std::string start_key_, end_key_;
};

}  // namespace master
}  // namespace yb

#endif  // YB_MASTER_BACKFILL_INDEX_H
