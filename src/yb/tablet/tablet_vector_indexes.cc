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

#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/vector_index.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/write_batch.h"

#include "yb/rpc/thread_pool.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/operation_counter.h"

using namespace std::literals;

DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

// A way to block backfilling vector index after the first vector index chunk is flushed.
bool TEST_block_after_backfilling_first_vector_index_chunks = false;

Status TabletVectorIndexes::Open() NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock lock(vector_indexes_mutex_, std::defer_lock);
  auto tables = metadata().GetAllColocatedTableInfos();
  std::sort(tables.begin(), tables.end(), [](const auto& lhs, const auto& rhs) {
    return lhs->table_id < rhs->table_id;
  });
  for (const auto& table_info : tables) {
    if (!table_info->NeedVectorIndex()) {
      continue;
    }
    VLOG_WITH_PREFIX_AND_FUNC(3) << table_info->ToString();
    if (!lock.owns_lock()) {
      lock.lock();
    }
    auto it = binary_search_iterator(
        tables.begin(), tables.end(), table_info->index_info->indexed_table_id(), std::less<void>(),
        [](const auto& table_info) { return table_info->table_id; });
    SCHECK(it != tables.end(), IllegalState,
           "Indexed table not found: $0", table_info->index_info->indexed_table_id());
    RETURN_NOT_OK(DoCreateIndex(*table_info, *it, /* bootstrap = */ true));
  }
  return Status::OK();
}

Status TabletVectorIndexes::CreateIndex(
    const TableInfo& index_table, const TableInfoPtr& indexed_table, bool bootstrap) {
  std::lock_guard lock(vector_indexes_mutex_);
  return DoCreateIndex(index_table, indexed_table, bootstrap);
}

Status TabletVectorIndexes::DoCreateIndex(
    const TableInfo& index_table, const TableInfoPtr& indexed_table, bool bootstrap) {
  has_vector_indexes_ = true;
  if (vector_indexes_map_.count(index_table.table_id)) {
    LOG(DFATAL) << "Vector index for " << index_table.table_id << " already exists";
    return Status::OK();
  }
  auto& thread_pool = *thread_pool_provider_();
  auto vector_index = VERIFY_RESULT(docdb::CreateVectorIndex(
      AddSuffixToLogPrefix(LogPrefix(), Format(" VI $0", index_table.table_id)),
      metadata().rocksdb_dir(), thread_pool,
      indexed_table->doc_read_context->table_key_prefix(), *index_table.index_info, doc_db()));
  if (!bootstrap) {
    auto read_op = tablet().CreateScopedRWOperationBlockingRocksDbShutdownStart();
    if (read_op.ok()) {
      ScheduleBackfill(
          vector_index, index_table.hybrid_time, indexed_table,
          std::make_shared<ScopedRWOperation>(std::move(read_op)));
    } else {
      LOG_WITH_PREFIX_AND_FUNC(WARNING)
          << "Failed to create operation for backfill: " << read_op.GetAbortedStatus();
    }
  }
  auto it = vector_indexes_map_.emplace(index_table.table_id, std::move(vector_index)).first;
  auto& indexes = vector_indexes_list_;
  if (!indexes) {
    indexes = std::make_shared<std::vector<docdb::VectorIndexPtr>>(1, it->second);
    return Status::OK();
  }
  if (indexes.use_count() == 1) {
    indexes->push_back(it->second);
    return Status::OK();
  }

  auto new_indexes = std::make_shared<docdb::VectorIndexes>();
  new_indexes->reserve(indexes->size() + 1);
  *new_indexes = *indexes;
  new_indexes->push_back(it->second);
  indexes = std::move(new_indexes);

  return Status::OK();
}

namespace {

class VectorIndexBackfillHelper : public rocksdb::DirectWriter {
 public:
  explicit VectorIndexBackfillHelper(HybridTime backfill_ht)
      : backfill_ht_(backfill_ht) {}

  void Add(Slice ybctid, Slice value) {
    ybctids_.push_back(arena_.DupSlice(ybctid));
    entries_.emplace_back(docdb::VectorIndexInsertEntry {
      .value = ValueBuffer(value),
    });
  }

  size_t num_chunks() const {
    return num_chunks_;
  }

  bool NeedFlush() {
    return entries_.size() >= FLAGS_vector_index_initial_chunk_size;
  }

  Status Flush(Tablet& tablet, docdb::VectorIndex& index, Slice next_ybctid) {
    ++num_chunks_;
    {
      rocksdb::WriteBatch write_batch;
      write_batch.SetDirectWriter(this);
      tablet.WriteToRocksDB(nullptr, &write_batch, docdb::StorageDbType::kRegular);
    }

    docdb::ConsensusFrontiers frontiers;
    if (next_ybctid.empty()) {
      frontiers.Largest().SetBackfillDone();
    } else {
      frontiers.Largest().SetBackfillPosition(next_ybctid);
    }
    RETURN_NOT_OK_PREPEND(index.Insert(entries_, &frontiers), "Insert entries");
    if (!next_ybctid.empty()) {
      entries_.clear();
      ybctids_.clear();
      arena_.Reset(ResetMode::kKeepLast);
    }
    return Status::OK();
  }

  Status Apply(rocksdb::DirectWriteHandler* handler) override {
    for (size_t i = 0; i != ybctids_.size(); ++i) {
      docdb::AddVectorIndexReverseEntry(
          handler, ybctids_[i], entries_[i].value.AsSlice(), backfill_ht_);
    }
    return Status::OK();
  }

 private:
  const HybridTime backfill_ht_;
  docdb::VectorIndexInsertEntries entries_;
  std::vector<Slice> ybctids_;
  Arena arena_;
  size_t num_chunks_ = 0;
};

} // namespace

Status TabletVectorIndexes::Backfill(
    const docdb::VectorIndexPtr& vector_index, const TableInfo& indexed_table, Slice from_key,
    HybridTime backfill_ht) {
  LOG_WITH_PREFIX_AND_FUNC(INFO)
      << "vector_index: " << AsString(*vector_index) << ", indexed_table: "
      << indexed_table.ToString() << ", from_key: " << from_key.ToDebugHexString()
      << ", backfill_ht: " << backfill_ht;

  auto read_ht = ReadHybridTime::SingleTime(backfill_ht);
  dockv::ReaderProjection projection(indexed_table.schema(), {vector_index->column_id()});
  auto iter = VERIFY_RESULT(tablet().NewUninitializedDocRowIterator(
      projection, read_ht, indexed_table.table_id));
  iter->InitForTableType(TableType::PGSQL_TABLE_TYPE, from_key);

  // Expecting one row at most.
  dockv::PgTableRow row(projection);
  VectorIndexBackfillHelper helper(backfill_ht);
  for (;;) {
    if (tablet().IsShutdownRequested()) {
      LOG_WITH_FUNC(INFO) << "Exit: " << AsString(*vector_index);
      return Status::OK();
    }
    if (helper.num_chunks() && TEST_block_after_backfilling_first_vector_index_chunks) {
      std::this_thread::sleep_for(10ms);
      continue;
    }
    if (!VERIFY_RESULT_PREPEND(iter->PgFetchNext(&row), "Fetch row")) {
      break;
    }
    auto value = row.GetValueByIndex(0);
    if (!value) {
      continue;
    }

    auto ybctid = iter->GetTupleId();
    if (helper.NeedFlush()) {
      RETURN_NOT_OK(helper.Flush(tablet(), *vector_index, ybctid));
    }
    helper.Add(ybctid, value->binary_value());
  }

  RETURN_NOT_OK(helper.Flush(tablet(), *vector_index, Slice()));

  LOG_WITH_PREFIX_AND_FUNC(INFO)
      << "Backfilled " << AsString(*vector_index) << " in " << helper.num_chunks() << " chunks";

  // TODO(vector_index) Need to handle scenario when regular db was not flushed before restart.
  RETURN_NOT_OK_PREPEND(Flush(FlushMode::kSync, FlushFlags::kRegular), "Flush regular DB");
  RETURN_NOT_OK_PREPEND(vector_index->Flush(), "Flush vector index");
  return Status::OK();
}

void TabletVectorIndexes::LaunchBackfillsIfNecessary() {
  auto list = VectorIndexesList();
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "list: " << AsString(list);
  if (!list) {
    return;
  }
  std::shared_ptr<ScopedRWOperation> read_op;
  for (const auto& vector_index : *list) {
    if (vector_index->BackfillDone()) {
      continue;
    }
    std::string backfill_key;
    auto flushed_frontier = vector_index->GetFlushedFrontier();
    if (flushed_frontier) {
      if (flushed_frontier->backfill_done()) {
        continue;
      }
      backfill_key = flushed_frontier->backfill_key();
    }
    if (backfill_key.empty()) {
      LOG_WITH_PREFIX_AND_FUNC(INFO)
          << "Start " << AsString(*vector_index);
    } else {
      LOG_WITH_PREFIX_AND_FUNC(INFO)
          << "Continue " << AsString(*vector_index) << " from "
          << Slice(backfill_key).ToDebugHexString();
    }

    auto table_info_res = metadata().GetTableInfo(vector_index->table_id());
    if (!table_info_res.ok()) {
        LOG_WITH_PREFIX_AND_FUNC(DFATAL)
            << "Failed to obtain table info for " << AsString(*vector_index) << ": "
            << table_info_res.status();
        continue;
    }

    const auto& indexed_table_id = (**table_info_res).index_info->indexed_table_id();
    auto indexed_table_info_res = metadata().GetTableInfo(indexed_table_id);
    if (!indexed_table_info_res.ok()) {
        LOG_WITH_PREFIX_AND_FUNC(DFATAL)
            << "Failed to obtain indexed table info for " << AsString(*vector_index) << "/"
            << indexed_table_id << ": " << indexed_table_info_res.status();
        continue;
    }

    if (!read_op) {
      read_op = std::make_shared<ScopedRWOperation>(
          tablet().CreateScopedRWOperationBlockingRocksDbShutdownStart());
    }
    if (!read_op->ok()) {
      LOG_WITH_PREFIX_AND_FUNC(WARNING)
          << "Failed to create operation for backfill: " << read_op->GetAbortedStatus();
      continue;
    }

    ScheduleBackfill(
        vector_index, (**table_info_res).hybrid_time, *indexed_table_info_res, read_op);
  }
}

void TabletVectorIndexes::ScheduleBackfill(
    const docdb::VectorIndexPtr& vector_index, HybridTime backfill_ht,
    const TableInfoPtr& indexed_table, std::shared_ptr<ScopedRWOperation> read_op) {
  thread_pool_provider_()->EnqueueFunctor(
      [this, vector_index, backfill_ht, indexed_table, read_op = std::move(read_op)] {
    auto status = Backfill(vector_index, *indexed_table, Slice(), backfill_ht);
    LOG_IF_WITH_PREFIX(DFATAL, !status.ok())
        << "Backfill " << AsString(vector_index) << " failed: " << status;
  });
}

void TabletVectorIndexes::CompleteShutdown(std::vector<std::string>& out_paths) {
  if (!has_vector_indexes_.load()) {
    return;
  }
  decltype(vector_indexes_list_) vector_indexes_list;
  {
    std::lock_guard lock(vector_indexes_mutex_);
    vector_indexes_list.swap(vector_indexes_list_);
    vector_indexes_map_.clear();
  }
  if (!vector_indexes_list) {
    return;
  }
  for (const auto& index : *vector_indexes_list) {
    out_paths.push_back(index->path());
  }
  // TODO(vector_index) It could happen that there are external references to vector index.
  // Wait actual shutdown.
}

docdb::VectorIndexPtr TabletVectorIndexes::IndexForTable(const TableId& table_id) const {
  SharedLock lock(vector_indexes_mutex_);
  auto it = vector_indexes_map_.find(table_id);
  if (it != vector_indexes_map_.end()) {
    return it->second;
  }

  LOG_WITH_PREFIX_AND_FUNC(DFATAL)
      << "Vector index query but don't have vector index for "
      << table_id << ", all vector indexes: "
      << CollectionToString(vector_indexes_map_, [](const auto& pair) { return pair.first; });
  return nullptr;
}

docdb::VectorIndexesPtr TabletVectorIndexes::List() const {
  if (!has_vector_indexes_.load(std::memory_order_acquire)) {
    return nullptr;
  }
  SharedLock lock(vector_indexes_mutex_);
  return vector_indexes_list_;
}

auto TabletVectorIndexes::FinishedBackfills()
    -> std::optional<google::protobuf::RepeatedPtrField<std::string>> {
  auto list = List();
  if (!list) {
    return std::nullopt;
  }
  google::protobuf::RepeatedPtrField<std::string> result;
  for (const auto& index : *list) {
    if (index->BackfillDone()) {
      *result.Add() = index->table_id();
    }
  }
  VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(result);
  return result;
}

void TabletVectorIndexes::FillMaxPersistentOpIds(
    boost::container::small_vector_base<OpId>& out, bool invalid_if_no_new_data) {
  auto list = List();
  if (!list) {
    return;
  }
  for (const auto& vector_index : *list) {
    out.push_back(MaxPersistentOpIdForDb(vector_index.get(), invalid_if_no_new_data));
  }
}

Status VectorIndexList::WaitForFlush() {
  if (!list_) {
    return Status::OK();
  }

  for (const auto& index : *list_) {
    RETURN_NOT_OK(index->WaitForFlush());
  }

  return Status::OK();
}

void VectorIndexList::Flush() {
  if (!list_) {
    return;
  }
  // TODO(vector-index) Check flush order between vector indexes and intents db
  for (const auto& index : *list_) {
    WARN_NOT_OK(index->Flush(), "Flush vector index");
  }
}

std::string VectorIndexList::ToString() const {
  return list_ ? AsString(*list_) : AsString(list_);
}

}  // namespace yb::tablet
