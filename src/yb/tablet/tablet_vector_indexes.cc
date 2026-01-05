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
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/key_bounds.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/write_batch.h"

#include "yb/rpc/thread_pool.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/operation_counter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"

using namespace std::literals;

DEFINE_test_flag(int32, sleep_before_vector_index_backfill_seconds, 0,
    "Sleep specified amount of seconds before doing vector index backfill.");

DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

namespace {

METRIC_DEFINE_entity(vector_index);

class IndexReverseMappingReader : public docdb::DocVectorIndexReverseMappingReader {
 public:
  Status Init(const Tablet& tablet, const ReadHybridTime& read_ht) {
    rocksdb_op_ = tablet.CreateScopedRWOperationBlockingRocksDbShutdownStart();
    RETURN_NOT_OK(rocksdb_op_);

    iter_holder_ = VERIFY_RESULT(tablet.vector_indexes().CreateVectorMetadataIterator(read_ht));
    return Status::OK();
  }

  Result<Slice> Fetch(Slice key) override {
    return std::get<docdb::IntentAwareIteratorPtr>(iter_holder_)->FetchValue(key);
  }

 private:
  ScopedRWOperation rocksdb_op_;
  docdb::IntentAwareIteratorWithBounds iter_holder_;
};

class IndexContext : public docdb::DocVectorIndexContext {
 public:
  IndexContext(Tablet& tablet, const TableInfo& indexed_table, ColumnId vector_column_id)
      : tablet_(tablet),
        indexed_table_id_(indexed_table.table_id),
        projection_(indexed_table.schema(), { vector_column_id }) {
  }

  Result<docdb::DocVectorIndexReverseMappingReaderPtr> CreateReverseMappingReader(
      const ReadHybridTime& read_ht) const override {
    auto reader = std::make_unique<IndexReverseMappingReader>();
    RETURN_NOT_OK(reader->Init(tablet_, read_ht));
    return reader;
  }

  Result<docdb::DocRowwiseIteratorPtr> CreateVectorColumnIterator(
      HybridTime read_ht, std::optional<Slice> start_key) const {
    auto result = VERIFY_RESULT(tablet_.NewUninitializedDocRowIterator(
        projection_, ReadHybridTime::SingleTime(read_ht), indexed_table_id_));
    RETURN_NOT_OK(result->InitForTableType(
        TableType::PGSQL_TABLE_TYPE, start_key ? *start_key : Slice(), docdb::SkipSeek(!start_key),
        docdb::AddTablePrefixToKey::kTrue));
    return result;
  }

  const dockv::ReaderProjection& vector_column_projection() const {
    return projection_;
  }

 private:
  Tablet& tablet_;
  TableId indexed_table_id_;
  dockv::ReaderProjection projection_;
};

class IndexedTableReader {
 public:
  explicit IndexedTableReader(const docdb::DocVectorIndex& vector_index)
      : context_(down_cast<const IndexContext&>(vector_index.context())),
        row_(context_.vector_column_projection()) {
  }

  Status Init(HybridTime read_ht, Slice start_key) {
    iter_ = VERIFY_RESULT(context_.CreateVectorColumnIterator(read_ht, start_key));
    return Status::OK();
  }

  Result<bool> FetchNext() {
    for (;;) {
      auto result = VERIFY_RESULT(iter_->PgFetchNext(&row_));
      if (!result) {
        return false;
      }
      auto value = row_.GetValueByIndex(0);
      if (value) {
        return true;
      }
      // Skip entries with NULL vector value.
    }
  }

  Slice current_vector_slice() const {
    return row_.GetValueByIndex(0)->binary_value();
  }

  Slice current_ybctid() const {
    return iter_->GetTupleId();
  }

 private:
  const IndexContext& context_;
  dockv::PgTableRow row_;
  docdb::DocRowwiseIteratorPtr iter_;
};

} // namespace

// A way to block backfilling vector index after the first vector index chunk is flushed.
bool TEST_block_after_backfilling_first_vector_index_chunks = false;

TabletVectorIndexes::TabletVectorIndexes(
    Tablet* tablet,
    const VectorIndexThreadPoolProvider& thread_pool_provider,
    const VectorIndexCompactionTokenProvider& compaction_token_provider,
    const hnsw::BlockCachePtr& block_cache,
    MetricRegistry* metric_registry)
    : TabletComponent(tablet),
      thread_pool_provider_(thread_pool_provider),
      compaction_token_provider_(compaction_token_provider),
      block_cache_(block_cache),
      mem_tracker_(MemTracker::CreateTracker(-1, "vector_indexes", tablet->mem_tracker())),
      metric_registry_(metric_registry) {
}

Status TabletVectorIndexes::Open(const docdb::ConsensusFrontier* frontier)
    NO_THREAD_SAFETY_ANALYSIS {
  // Explicitly set the state of the controller, since table storages can be reopened
  // after being shutdown.
  RETURN_NOT_OK(shutdown_controller_.Start());

  if (frontier && frontier->has_vector_deletion()) {
    SetHasVectorDeletion();
  }
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
  SCHECK(shutdown_controller_.IsRunning(), ShutdownInProgress, "Tablet vector indexes shutdown");

  has_vector_indexes_ = true;
  if (vector_indexes_map_.count(index_table.table_id)) {
    LOG(DFATAL) << "Vector index for " << index_table.table_id << " already exists";
    return Status::OK();
  }
  RSTATUS_DCHECK_EQ(index_table.table_id, index_table.index_info->table_id(), InvalidArgument,
                    "Wrong table id in index");

  auto vector_index_thread_pool_provider = [this]() {
    return docdb::DocVectorIndexThreadPools {
        .thread_pool = thread_pool_provider_(VectorIndexThreadPoolType::kBackground),
        .insert_thread_pool = thread_pool_provider_(VectorIndexThreadPoolType::kInsert),
        .compaction_token = compaction_token_provider_(),
    };
  };

  auto index_context = std::make_unique<IndexContext>(
      tablet(), *indexed_table, ColumnId(index_table.index_info->vector_idx_options().column_id()));

  MetricEntityPtr vector_index_metric_entity;
  if (metric_registry_) {
    MetricEntity::AttributeMap attrs;
    attrs["table_id"] = index_table.table_id;
    attrs["table_name"] = index_table.table_name;
    attrs["table_type"] = TableType_Name(index_table.table_type);
    // Use the namespace name from the base table since the namespace name is not populated in
    // index_table object.
    attrs["namespace_name"] = indexed_table->namespace_name;
    vector_index_metric_entity =
        METRIC_ENTITY_vector_index.Instantiate(metric_registry_, index_table.table_id, attrs);
  }

  auto vector_index = VERIFY_RESULT(docdb::CreateDocVectorIndex(
      AddSuffixToLogPrefix(LogPrefix(), Format(" VI $0", index_table.table_id)),
      metadata().vector_index_dir(index_table.index_info->vector_idx_options()),
      vector_index_thread_pool_provider, indexed_table->doc_read_context->table_key_prefix(),
      index_table.hybrid_time, *index_table.index_info, std::move(index_context),
      block_cache_, MemTracker::CreateTracker(-1, index_table.table_id, mem_tracker_),
      vector_index_metric_entity));

  if (!bootstrap) {
    auto read_op = tablet().CreateScopedRWOperationBlockingRocksDbShutdownStart();
    if (read_op.ok()) {
      ScheduleBackfill(
          vector_index, indexed_table, Slice(), index_table.hybrid_time, index_table.op_id,
          std::make_shared<ScopedRWOperation>(std::move(read_op)));
    } else {
      LOG_WITH_PREFIX_AND_FUNC(WARNING)
          << "Failed to create operation for backfill: " << read_op.GetAbortedStatus();
    }
  }

  // Enable vector index compaction only when new vector index has been added to all collections.
  {
    auto it = vector_indexes_map_.emplace(index_table.table_id, std::move(vector_index)).first;
    auto scope_exit = !bootstrap
        ? MakeOptionalScopeExit([&it] { it->second->EnableAutoCompactions(); }) : std::nullopt;

    auto& indexes = vector_indexes_list_;
    if (!indexes) {
      indexes = std::make_shared<std::vector<docdb::DocVectorIndexPtr>>(1, it->second);
      return Status::OK();
    }
    if (indexes.use_count() == 1) {
      indexes->push_back(it->second);
      return Status::OK();
    }

    auto new_indexes = std::make_shared<docdb::DocVectorIndexes>();
    new_indexes->reserve(indexes->size() + 1);
    *new_indexes = *indexes;
    new_indexes->push_back(it->second);
    indexes = std::move(new_indexes);
  }

  return Status::OK();
}

namespace {

class VectorIndexBackfillHelper : public rocksdb::DirectWriter {
 public:
  explicit VectorIndexBackfillHelper(HybridTime backfill_ht, OpId op_id)
      : backfill_ht_(backfill_ht), op_id_(op_id) {}

  void Add(Slice ybctid, Slice value) {
    ybctids_.push_back(arena_.DupSlice(ybctid));
    entries_.emplace_back(docdb::DocVectorIndexInsertEntry {
      .value = ValueBuffer(value),
    });
  }

  size_t num_chunks() const {
    return num_chunks_;
  }

  bool NeedFlush() {
    return entries_.size() >= FLAGS_vector_index_initial_chunk_size;
  }

  Status Flush(Tablet& tablet, docdb::DocVectorIndex& index, Slice next_ybctid) {
    ++num_chunks_;
    {
      docdb::ConsensusFrontiers frontiers;
      frontiers.Smallest().set_op_id(op_id_);
      frontiers.Largest().set_op_id(op_id_);
      rocksdb::WriteBatch write_batch;
      write_batch.SetDirectWriter(this);
      tablet.WriteToRocksDB(frontiers, &write_batch, docdb::StorageDbType::kRegular);
    }

    docdb::ConsensusFrontiers frontiers;
    if (next_ybctid.empty()) {
      frontiers.Largest().SetBackfillDone();
    } else {
      frontiers.Largest().SetBackfillPosition(next_ybctid);
    }
    RETURN_NOT_OK_PREPEND(index.Insert(entries_, frontiers), "Insert entries");
    if (!next_ybctid.empty()) {
      entries_.clear();
      ybctids_.clear();
      arena_.Reset(ResetMode::kKeepLast);
    }
    return Status::OK();
  }

  Status Apply(rocksdb::DirectWriteHandler& handler) override {
    for (size_t i = 0; i != ybctids_.size(); ++i) {
      docdb::DocVectorIndex::ApplyReverseEntry(
          handler, ybctids_[i], entries_[i].value.AsSlice(), DocHybridTime(backfill_ht_, 0));
    }
    return Status::OK();
  }

 private:
  const HybridTime backfill_ht_;
  const OpId op_id_;
  docdb::DocVectorIndexInsertEntries entries_;
  std::vector<Slice> ybctids_;
  Arena arena_;
  size_t num_chunks_ = 0;
};

} // namespace

Status TabletVectorIndexes::Backfill(
    const docdb::DocVectorIndexPtr& vector_index, const TableInfo& indexed_table, Slice from_key,
    HybridTime backfill_ht, OpId op_id) {
  LOG_WITH_PREFIX_AND_FUNC(INFO)
      << "vector_index: " << AsString(*vector_index) << ", indexed_table: "
      << indexed_table.ToString() << ", from_key: " << from_key.ToDebugHexString()
      << ", backfill_ht: " << backfill_ht;

  if (FLAGS_TEST_sleep_before_vector_index_backfill_seconds) {
    std::this_thread::sleep_for(FLAGS_TEST_sleep_before_vector_index_backfill_seconds * 1s);
  }

  IndexedTableReader reader(*vector_index);
  RETURN_NOT_OK(reader.Init(backfill_ht, from_key));

  // Expecting one row at most.
  VectorIndexBackfillHelper helper(backfill_ht, op_id);
  for (;;) {
    if (tablet().IsShutdownRequested()) {
      LOG_WITH_FUNC(INFO) << "Exit: " << AsString(*vector_index);
      return Status::OK();
    }
    if (!shutdown_controller_.IsRunning()) {
      LOG_WITH_FUNC(INFO) << "Vector index shutdown: " << AsString(*vector_index);
      return Status::OK();
    }
    if (helper.num_chunks() && TEST_block_after_backfilling_first_vector_index_chunks) {
      std::this_thread::sleep_for(10ms);
      continue;
    }
    if (!VERIFY_RESULT(reader.FetchNext())) {
      break;
    }
    auto ybctid = reader.current_ybctid();
    if (helper.NeedFlush()) {
      RETURN_NOT_OK(helper.Flush(tablet(), *vector_index, ybctid));
    }
    helper.Add(ybctid, reader.current_vector_slice());
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
        vector_index, *indexed_table_info_res, backfill_key, (**table_info_res).hybrid_time,
        (**table_info_res).op_id, read_op);
  }
}

void TabletVectorIndexes::ScheduleBackfill(
    const docdb::DocVectorIndexPtr& vector_index, const TableInfoPtr& indexed_table, Slice key,
    HybridTime backfill_ht, OpId op_id, std::shared_ptr<ScopedRWOperation> read_op) {
  thread_pool_provider_(VectorIndexThreadPoolType::kBackfill)->EnqueueFunctor(
      [this, vector_index, backfill_ht, key = key.ToBuffer(), op_id, indexed_table,
       read_op = std::move(read_op)] {
    auto status = Backfill(vector_index, *indexed_table, key, backfill_ht, op_id);
    if (status.IsShutdownInProgress()) {
      LOG_WITH_PREFIX(WARNING) << "Backfill " << AsString(vector_index) << " failed: " << status;
    } else {
      LOG_IF_WITH_PREFIX(DFATAL, !status.ok())
          << "Backfill " << AsString(vector_index) << " failed: " << status;
    }
  });
}

void TabletVectorIndexes::StartShutdown() {
  auto scope = shutdown_controller_.StartShutdown();
  if (!scope) {
    LOG_IF_WITH_PREFIX(DFATAL, !scope.status().IsShutdownInProgress()) << scope.status();
    return;
  }

  if (!has_vector_indexes_.exchange(false)) {
    return;
  }

  {
    std::lock_guard lock(vector_indexes_mutex_);
    vector_indexes_cleanup_list_.swap(vector_indexes_list_);
    vector_indexes_map_.clear();
  }

  if (!vector_indexes_cleanup_list_) {
    return;
  }

  for (auto& index : *vector_indexes_cleanup_list_) {
    index->StartShutdown();
  }
}

void TabletVectorIndexes::CompleteShutdown(std::vector<std::string>& out_paths) {
  auto scope = shutdown_controller_.CompleteShutdown();
  if (!scope) {
    LOG_IF_WITH_PREFIX(DFATAL, !scope.status().IsShutdownInProgress()) << scope.status();
    return;
  }

  if (!vector_indexes_cleanup_list_) {
    return;
  }

  for (auto& index : *vector_indexes_cleanup_list_) {
    out_paths.push_back(index->path());
    index->CompleteShutdown();
  }

  // TODO(vector_index) It could happen that there are external references to vector index.
  // Wait actual shutdown.
  vector_indexes_cleanup_list_->clear();
}

docdb::DocVectorIndexPtr TabletVectorIndexes::IndexForTableUnlocked(const TableId& table_id) const {
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

docdb::DocVectorIndexPtr TabletVectorIndexes::IndexForTable(const TableId& table_id) const {
  SharedLock lock(vector_indexes_mutex_);
  return IndexForTableUnlocked(table_id);
}

docdb::DocVectorIndexesPtr TabletVectorIndexes::Collect(const std::vector<TableId>& table_ids) {
  if (!has_vector_indexes_.load(std::memory_order_acquire)) {
    return nullptr;
  }

  if (table_ids.empty()) {
    return List();
  }

  auto result = std::make_shared<docdb::DocVectorIndexes>();
  result->reserve(table_ids.size());
  {
    SharedLock lock(vector_indexes_mutex_);
    for (const auto& table_id : table_ids) {
      auto index = IndexForTableUnlocked(table_id);
      if (!index) {
        return nullptr;
      }
      result->push_back(std::move(index));
    }
  }
  return result;
}

docdb::DocVectorIndexesPtr TabletVectorIndexes::List() const {
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

docdb::DocVectorIndexPtr TabletVectorIndexes::RemoveTableFromList(const TableId& table_id) {
  if (!vector_indexes_list_) {
    return nullptr;
  }
  for (auto it = vector_indexes_list_->begin(); it != vector_indexes_list_->end(); ++it) {
    if ((**it).table_id() == table_id) {
      auto result = *it;
      auto& indexes = vector_indexes_list_;
      if (indexes.use_count() == 1) {
        indexes->erase(it);
        return result;
      }

      auto new_indexes = std::make_shared<docdb::DocVectorIndexes>();
      new_indexes->reserve(indexes->size() - 1);
      new_indexes->insert(new_indexes->end(), vector_indexes_list_->begin(), it);
      new_indexes->insert(new_indexes->end(), ++it, vector_indexes_list_->end());
      indexes = std::move(new_indexes);
      return result;
    }
  }
  return nullptr;
}

Status TabletVectorIndexes::Remove(const TableId& table_id) {
  if (!has_vector_indexes_.load()) {
    return Status::OK();
  }
  docdb::DocVectorIndexPtr index;
  {
    UniqueLock lock(vector_indexes_mutex_);
    auto it = vector_indexes_map_.find(table_id);
    if (it == vector_indexes_map_.end()) {
      return Status::OK();
    }
    index = RemoveTableFromList(table_id);
    RSTATUS_DCHECK(
        index != nullptr, IllegalState, "Index $0 present in map but missing in list", table_id);
    vector_indexes_map_.erase(it);
  }
  return index->Destroy();
}

Status TabletVectorIndexes::Verify() {
  auto list = List();
  if (!list) {
    return Status::OK();
  }
  auto read_ht = VERIFY_RESULT(tablet().SafeTime(RequireLease::kFalse));
  for (const auto& vector_index : *list) {
    while (!vector_index->BackfillDone()) {
      std::this_thread::sleep_for(10ms);
    }
    auto index_table = VERIFY_RESULT(metadata().GetTableInfo(vector_index->table_id()));
    auto indexed_table = VERIFY_RESULT(metadata().GetTableInfo(
        index_table->index_info->indexed_table_id()));
    IndexedTableReader reader(*vector_index);
    RETURN_NOT_OK(reader.Init(read_ht, Slice()));
    auto reverse_mapping_reader = VERIFY_RESULT(
        vector_index->context().CreateReverseMappingReader(ReadHybridTime::SingleTime(read_ht)));
    while (VERIFY_RESULT(reader.FetchNext())) {
      auto value = dockv::EncodedDocVectorValue::FromSlice(reader.current_vector_slice());
      auto vector_id = VERIFY_RESULT(value.DecodeId());
      auto ybctid = CHECK_RESULT(reverse_mapping_reader->Fetch(vector_id));
      if (reader.current_ybctid() != ybctid) {
        LOG_WITH_FUNC(DFATAL)
            << "Wrong reverse record for: " << vector_id << ": " << ybctid.ToDebugHexString()
            << ", while expected: " << reader.current_ybctid().ToDebugHexString();
      }
      if (!VERIFY_RESULT(vector_index->HasVectorId(vector_id))) {
        LOG_WITH_FUNC(DFATAL) << "Missing vector id in index: " << vector_id;
      }
    }
  }
  return Status::OK();
}

Result<docdb::IntentAwareIteratorWithBounds> TabletVectorIndexes::CreateVectorMetadataIterator(
    const ReadHybridTime& read_ht) const {
  static std::array<char, 2> upper_bound {
      dockv::KeyEntryTypeAsChar::kVectorIndexMetadata,
      dockv::KeyEntryTypeAsChar::kMaxByte
  };

  docdb::ReadOperationData read_operation_data;
  RETURN_NOT_OK(tablet().GetSafeTimeReadOperationData(read_ht, read_operation_data));

  // TODO(vector_index): do we need to specify bloom filter options?
  auto iter = docdb::CreateIntentAwareIterator(
      tablet().doc_db().FromRegularUnbounded(), docdb::BloomFilterOptions::Inactive(),
      rocksdb::kDefaultQueryId, TransactionOperationContext{}, read_operation_data);
  auto bounds = std::make_unique<docdb::IntentAwareIteratorBoundsScope>(
      Slice{&dockv::KeyEntryTypeAsChar::kVectorIndexMetadata, 1}, Slice{upper_bound}, iter.get()
  );

  return docdb::IntentAwareIteratorWithBounds { std::move(iter), std::move(bounds) };
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
  // TODO(vector_index) Check flush order between vector indexes and intents db
  for (const auto& index : *list_) {
    WARN_NOT_OK(index->Flush(), "Flush vector index");
  }
}

void VectorIndexList::EnableAutoCompactions() {
  if (!list_) {
    return;
  }

  for (const auto& index : *list_) {
    index->EnableAutoCompactions();
  }
}

void VectorIndexList::Compact() {
  if (!list_) {
    return;
  }

  for (const auto& index : *list_) {
    WARN_NOT_OK(index->Compact(), "Compact vector index");
  }
}

Status VectorIndexList::WaitForCompaction() {
  if (!list_) {
    return Status::OK();
  }

  for (const auto& index : *list_) {
    RETURN_NOT_OK(index->WaitForCompaction());
  }

  return Status::OK();
}

std::string VectorIndexList::ToString() const {
  return list_ ? AsString(*list_) : AsString(list_);
}

}  // namespace yb::tablet
