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

#include "yb/tablet/tablet_component.h"
#include "yb/tablet/tablet_options.h"

namespace yb {

class ScopedRWOperation;

}

namespace yb::tablet {

class VectorIndexList {
 public:
  VectorIndexList() = default;
  explicit VectorIndexList(docdb::DocVectorIndexesPtr list) : list_(std::move(list)) {}

  void Compact();
  void Flush();
  Status WaitForFlush();

  std::string ToString() const;

  explicit operator bool() const {
    return list_ != nullptr;
  }

  const docdb::DocVectorIndexes& operator*() const {
    return *list_;
  }

 private:
  docdb::DocVectorIndexesPtr list_;
};

class TabletVectorIndexes : public TabletComponent {
 public:
  TabletVectorIndexes(Tablet* tablet, const VectorIndexThreadPoolProvider& thread_pool_provider)
      : TabletComponent(tablet), thread_pool_provider_(thread_pool_provider) {}

  Status Open();
  // Creates vector index for specified index and indexed tables.
  // bootstrap is set to true only during initial tablet bootstrap, so nobody should
  // hold external pointer to vector index list at this moment.
  Status CreateIndex(
      const TableInfo& index_table, const TableInfoPtr& indexed_table, bool bootstrap)
      EXCLUDES(vector_indexes_mutex_);

  // Returns a collection of vector indexes for the given vector index table ids. Returns nullptr
  // if at least one vector indexes is not found by the give table id. The order of vector indexes
  // in the returned collection is not guaranteed to be preserved.
  docdb::DocVectorIndexesPtr Collect(const std::vector<TableId>& table_ids);

  docdb::DocVectorIndexesPtr List() const EXCLUDES(vector_indexes_mutex_);

  void LaunchBackfillsIfNecessary();
  void CompleteShutdown(std::vector<std::string>& out_paths);
  std::optional<google::protobuf::RepeatedPtrField<std::string>> FinishedBackfills();

  docdb::DocVectorIndexPtr IndexForTable(const TableId& table_id) const;

  void FillMaxPersistentOpIds(
      boost::container::small_vector_base<OpId>& out, bool invalid_if_no_new_data);

  bool TEST_HasIndexes() const {
    return has_vector_indexes_.load();
  }

  Status Verify();

 private:
  void ScheduleBackfill(
      const docdb::DocVectorIndexPtr& vector_index, HybridTime backfill_ht, OpId op_id,
      const TableInfoPtr& indexed_table, std::shared_ptr<ScopedRWOperation> read_op);
  Status Backfill(
      const docdb::DocVectorIndexPtr& vector_index, const TableInfo& indexed_table, Slice key,
      HybridTime backkfill_ht, OpId op_id);

  Status DoCreateIndex(
      const TableInfo& index_table, const TableInfoPtr& indexed_table, bool allow_inplace_insert)
      REQUIRES(vector_indexes_mutex_);

  docdb::DocVectorIndexPtr IndexForTableUnlocked(
      const TableId& table_id) const REQUIRES_SHARED(vector_indexes_mutex_);

  const VectorIndexThreadPoolProvider thread_pool_provider_;

  std::atomic<bool> has_vector_indexes_{false};
  mutable std::shared_mutex vector_indexes_mutex_;
  std::unordered_map<TableId, docdb::DocVectorIndexPtr> vector_indexes_map_
      GUARDED_BY(vector_indexes_mutex_);
  docdb::DocVectorIndexesPtr vector_indexes_list_ GUARDED_BY(vector_indexes_mutex_);
};

}  // namespace yb::tablet
