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

#include <shared_mutex>

#include "yb/common/entity_ids_types.h"

#include "yb/hnsw/hnsw_fwd.h"

#include "yb/rocksdb/listener.h"

#include "yb/rpc/scheduler.h"

#include "yb/tablet/tablet_component.h"
#include "yb/tablet/tablet_options.h"

#include "yb/util/shutdown_controller.h"

namespace yb {

class ScopedRWOperation;

}

namespace yb::tablet {

class VectorIndexList {
 public:
  VectorIndexList() = default;
  explicit VectorIndexList(docdb::DocVectorIndexesPtr list) : list_(std::move(list)) {}

  void EnableAutoCompactions();
  void Compact(rocksdb::CompactionReason reason);
  void Flush();
  Status WaitForCompaction();
  Status WaitForFlush();
  // Returns true if this list is empty or every index has compacted inherited parent data for
  // its tablet's split_generation.
  bool ParentDataCompacted() const;

  // Returns the total size in bytes occupied on disk by all vector indexes in this list.
  uint64_t OnDiskSize() const;

  std::string ToString() const;

  explicit operator bool() const {
    return list_ != nullptr;
  }

  const docdb::DocVectorIndexes& operator*() const {
    return *list_;
  }

  const docdb::DocVectorIndexes* operator->() const {
    return list_.get();
  }

  // Returns the underlying collection of vector indexes, for passing into APIs that operate on it
  // directly (e.g. the docdb write/apply path).
  const docdb::DocVectorIndexesPtr& impl() const {
    return list_;
  }

 private:
  docdb::DocVectorIndexesPtr list_;
};

class TabletVectorIndexes :
    public TabletComponent,
    public docdb::DocVectorMetadataIteratorProvider {
 public:
  TabletVectorIndexes(
      Tablet* tablet,
      const VectorIndexThreadPoolProvider& thread_pool_provider,
      const VectorIndexCompactionTokenProvider& compaction_token_provider,
      const hnsw::BlockCachePtr& block_cache,
      MetricRegistry* metric_registry);

  Status Open(const docdb::ConsensusFrontier* frontier);

  // Creates vector index for specified index and indexed tables.
  // bootstrap is set to true only during initial tablet bootstrap, so nobody should
  // hold external pointer to vector index list at this moment.
  Status CreateIndex(
      const TableInfo& index_table, const TableInfoPtr& indexed_table, bool bootstrap)
      EXCLUDES(vector_indexes_mutex_);

  // Removes specified index, also destroying its data on disk.
  Status Remove(const TableId& table_id) EXCLUDES(vector_indexes_mutex_);

  // Instantiates vector indexes of the given indexed table that were skipped because the indexed
  // column was missing from the schema (see DoCreateIndex), once an alter brings the column back.
  Status CreateSkippedIndexes(const TableInfoPtr& indexed_table, bool bootstrap)
      EXCLUDES(vector_indexes_mutex_);

  // Returns a collection of vector indexes for the given vector index table ids. Returns an empty
  // list if at least one vector indexes is not found by the give table id. The order of vector
  // indexes in the returned collection is not guaranteed to be preserved.
  VectorIndexList Collect(
      const std::vector<TableId>& table_ids) EXCLUDES(vector_indexes_mutex_);

  // Returns the current list of vector indexes. The list stays valid while shutdown is in progress
  // and becomes empty only after CompleteShutdown tears the indexes down.
  VectorIndexList List() const EXCLUDES(vector_indexes_mutex_);

  // Returns true if at least one vector index on this tablet has not finished backfilling. Used to
  // postpone tablet splitting until the backfill completes (see GH#32321).
  bool HasActiveBackfill() const EXCLUDES(vector_indexes_mutex_);

  // Returns true if there are no vector indexes, or every open vector index has compacted
  // inherited parent data for its tablet's split_generation.
  bool ParentDataCompacted() const EXCLUDES(vector_indexes_mutex_);

  // Returns true if a post-split compaction is still required to drop inherited parent data.
  // Always false when vector_index_include_into_post_split_compaction is off, as in that case no
  // vector index post-split compaction is ever scheduled.
  bool PostSplitCompactionRequired() const EXCLUDES(vector_indexes_mutex_);

  void LaunchBackfillsIfNecessary();

  // Binds the scheduler used to retry backfills aborted by an operation pause.
  void SetScheduler(rpc::Scheduler* scheduler);

  // Cancels the pending backfill retry and waits for a running one. Called on tablet shutdown only:
  // a truncate or a restore shuts this component down just to replace the storages and re-opens it
  // right after, and the retry has to survive that.
  void StopBackfillRetry();

  void StartShutdown();
  void CompleteShutdown(std::vector<std::string>& out_paths);
  std::optional<google::protobuf::RepeatedPtrField<std::string>> FinishedBackfills();

  docdb::DocVectorIndexPtr IndexForTable(
      const TableId& table_id) const EXCLUDES(vector_indexes_mutex_);

  void FillMaxPersistentOpIds(
      boost::container::small_vector_base<OpId>& out, bool invalid_if_no_new_data);

  bool TEST_HasIndexes() const {
    return has_vector_indexes_.load();
  }

  Status Verify();

  Result<docdb::IntentAwareIteratorWithBounds> CreateVectorMetadataIterator(
      const ReadHybridTime& read_ht, docdb::DocDBStatistics* statistics) const;

  void SetHasVectorDeletion() {
    has_vector_deletion_.store(true);
  }

  bool has_vector_deletion() {
    return has_vector_deletion_.load();
  }

  // Largest split_generation recorded by InitFrontiers() across open indexes, 0 if none.
  uint64_t MaxPersistedSplitGeneration() const EXCLUDES(vector_indexes_mutex_);

 private:
  void ScheduleBackfill(
      const docdb::DocVectorIndexPtr& vector_index, const TableInfoPtr& indexed_table, Slice key,
      HybridTime backfill_ht, OpId op_id, std::shared_ptr<ScopedRWOperation> read_op);

  // Re-runs LaunchBackfillsIfNecessary after a delay, replacing the retry scheduled before it.
  void ScheduleBackfillRetry();

  Status Backfill(
      const docdb::DocVectorIndexPtr& vector_index, const TableInfo& indexed_table, Slice key,
      HybridTime backkfill_ht, OpId op_id);

  Status DoCreateIndex(
      const TableInfo& index_table, const TableInfoPtr& indexed_table, bool allow_inplace_insert)
      REQUIRES(vector_indexes_mutex_);

  docdb::DocVectorIndexPtr IndexForTableUnlocked(
      const TableId& table_id) const REQUIRES_SHARED(vector_indexes_mutex_);

  docdb::DocVectorIndexPtr RemoveTableFromList(const TableId& table_id)
      REQUIRES(vector_indexes_mutex_);

  const VectorIndexThreadPoolProvider thread_pool_provider_;
  const VectorIndexCompactionTokenProvider compaction_token_provider_;
  const hnsw::BlockCachePtr block_cache_;
  const MemTrackerPtr mem_tracker_;
  MetricRegistry* metric_registry_ = nullptr;

  std::atomic<bool> has_vector_indexes_{false};
  std::atomic<bool> has_vector_deletion_{false};

  mutable std::shared_mutex vector_indexes_mutex_;
  std::unordered_map<TableId, docdb::DocVectorIndexPtr> vector_indexes_map_
      GUARDED_BY(vector_indexes_mutex_);
  docdb::DocVectorIndexesPtr vector_indexes_list_ GUARDED_BY(vector_indexes_mutex_);

  ShutdownController shutdown_controller_;

  rpc::Scheduler* scheduler_ = nullptr;
  rpc::ScheduledTaskTracker backfill_retry_task_;
};

}  // namespace yb::tablet
