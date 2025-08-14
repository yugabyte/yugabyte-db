// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS
// , WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/docdb/doc_vector_index.h"

#include <boost/algorithm/string.hpp>

#include "yb/ann_methods/usearch_wrapper.h"

#include "yb/dockv/doc_vector_id.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/qlexpr/index.h"

#include "yb/util/decimal.h"
#include "yb/util/endian_util.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"

#include "yb/vector_index/vectorann_util.h"
#include "yb/vector_index/vector_lsm.h"

DEFINE_RUNTIME_uint64(vector_index_initial_chunk_size, 100000,
    "Number of vector in initial vector index chunk");

DECLARE_bool(vector_index_dump_stats);
DECLARE_bool(vector_index_skip_filter_check);

namespace yb::docdb {

const std::string kVectorIndexDirPrefix = "vi-";

namespace {

vector_index::DistanceKind ConvertDistanceKind(PgVectorDistanceType dist_type) {
  switch (dist_type) {
    case PgVectorDistanceType::DIST_L2:
      return vector_index::DistanceKind::kL2Squared;
    case PgVectorDistanceType::DIST_IP:
      return vector_index::DistanceKind::kInnerProduct;
    case PgVectorDistanceType::DIST_COSINE:
      return vector_index::DistanceKind::kCosine;
    case PgVectorDistanceType::INVALID_DIST:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(PgVectorDistanceType, dist_type);
}

vector_index::HNSWOptions ConvertToHnswOptions(const PgVectorIdxOptionsPB& options) {
  return {
    .dimensions = options.dimensions(),
    .num_neighbors_per_vertex = options.hnsw().m(),
    .num_neighbors_per_vertex_base = options.hnsw().m0(),
    .ef_construction = options.hnsw().ef_construction(),
    .distance_kind = ConvertDistanceKind(options.dist_type()),
  };
}

template <template<class, class> class Factory, class LSM>
auto VectorLSMFactory(
    const hnsw::BlockCachePtr& block_cache, const PgVectorIdxOptionsPB& options,
    const MemTrackerPtr& mem_tracker) {
  using FactoryImpl = vector_index::MakeVectorIndexFactory<Factory, LSM>;
  return [block_cache, hnsw_options = ConvertToHnswOptions(options),
          backend = options.hnsw().backend(), mem_tracker](vector_index::FactoryMode mode) {
    return FactoryImpl::Create(mode, block_cache, hnsw_options, backend, mem_tracker);
  };
}

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
auto GetVectorLSMFactory(
    const hnsw::BlockCachePtr& block_cache, const PgVectorIdxOptionsPB& options,
    const MemTrackerPtr& mem_tracker)
    -> Result<vector_index::VectorIndexFactory<Vector, DistanceResult>> {
  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;
  switch (options.idx_type()) {
    case PgVectorIndexType::HNSW:
      return VectorLSMFactory<ann_methods::UsearchIndexFactory, LSM>(
          block_cache, options, mem_tracker);
    case PgVectorIndexType::DUMMY: [[fallthrough]];
    case PgVectorIndexType::IVFFLAT: [[fallthrough]];
    case PgVectorIndexType::UNKNOWN_IDX:
      break;
  }
  return STATUS_FORMAT(
        NotSupported, "Vector index $0 is not supported",
        PgVectorIndexType_Name(options.idx_type()));
}

std::string GetFileExtension(const PgVectorIdxOptionsPB& options) {
  switch (options.idx_type()) {
    case PgVectorIndexType::HNSW:
      return "." + boost::to_lower_copy(HnswBackend_Name(options.hnsw().backend()));
    case PgVectorIndexType::DUMMY: [[fallthrough]];
    case PgVectorIndexType::IVFFLAT: [[fallthrough]];
    case PgVectorIndexType::UNKNOWN_IDX:
      break;
  }
  FATAL_INVALID_PB_ENUM_VALUE(PgVectorIndexType, options.idx_type());
}

template<vector_index::IndexableVectorType Vector>
Result<Vector> VectorFromYSQL(Slice slice) {
  Slice original_slice = slice;
  size_t size = VERIFY_RESULT((CheckedRead<uint16_t, LittleEndian>(slice)));
  slice.RemovePrefix(2);
  RSTATUS_DCHECK_EQ(
      slice.size(), size * sizeof(typename Vector::value_type), Corruption,
      Format("Wrong vector value size, vector: $0", original_slice.ToDebugHexString()));
  Vector result;
  auto* input = slice.data();
  result.reserve(size);
  for (size_t i = 0; i != size; ++i) {
    result.push_back(Load<typename Vector::value_type, LittleEndian>(input));
    input += sizeof(typename Vector::value_type);
  }
  return result;
}

template<vector_index::IndexableVectorType Vector>
Result<vector_index::VectorLSMInsertEntry<Vector>> ConvertEntry(
    const DocVectorIndexInsertEntry& entry) {

  RSTATUS_DCHECK(!entry.value.empty(), InvalidArgument, "Vector value is not specified");

  auto encoded = dockv::EncodedDocVectorValue::FromSlice(entry.value.AsSlice());
  return vector_index::VectorLSMInsertEntry<Vector> {
    .vector_id = VERIFY_RESULT(encoded.DecodeId()),
    .vector = VERIFY_RESULT(VectorFromYSQL<Vector>(encoded.data)),
  };
}

EncodedDistance EncodeDistance(float distance) {
  uint32_t v = bit_cast<uint32_t>(distance);
  if (v >> 31) {
    return ~v;
  } else {
    return v ^ util::kInt32SignBitFlipMask;
  }
}

class VectorMergeFilter : public vector_index::VectorLSMMergeFilter {
 public:
  explicit VectorMergeFilter(const std::string& log_prefix, const DocDB& doc_db)
      : log_prefix_(log_prefix), iter_(doc_db.regular, {}, doc_db.key_bounds)
  {}

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  rocksdb::FilterDecision Filter(vector_index::VectorId vector_id) override {
    if (FLAGS_vector_index_skip_filter_check) {
      return rocksdb::FilterDecision::kKeep;
    }

    // TODO(vector_index): Revise once regular compaction correctly handles VectorId <=> ybctid;
    // additionally check the following points:
    // 1) Should tombstoned mapping be taken into account for filtering decision?
    //    Current understanding is that it should not be taken into account as even tombstoned
    //    records could still be required for some scenarious, and the regulard compaction should
    //    be responsible for the deletion of reverse mapping.
    // 2) Should DocRowwiseIterator created via tablet.NewUninitializedDocRowIterator() be used
    //    instead of BoundedRocksDbIterator over regular db?
    //    It may require additional changes to pass tablet itself of creating of a new factory
    //    which produces DocRowwiseIterator for reverse mapping reading.

    // Simple filtering by VectorId <=> ybctid presence in the regulard db.
    auto key = dockv::DocVectorKey(vector_id);
    const auto& db_entry = iter_.Seek(key.AsSlice());
    auto keep  = db_entry.Valid() && db_entry.key.starts_with(key.AsSlice());
    auto decision = keep ? rocksdb::FilterDecision::kKeep : rocksdb::FilterDecision::kDiscard;

    VLOG_WITH_PREFIX(4) << "Filtering " << vector_id << " => " << decision;
    return decision;
  }

 private:
  const std::string& log_prefix_;
  docdb::BoundedRocksDbIterator iter_;
};

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class DocVectorIndexImpl : public DocVectorIndex {
 public:
  DocVectorIndexImpl(
      const TableId& table_id, Slice indexed_table_key_prefix, ColumnId column_id,
      HybridTime hybrid_time, const DocDB& doc_db, const hnsw::BlockCachePtr& block_cache,
      const MemTrackerPtr& mem_tracker)
      : table_id_(table_id), indexed_table_key_prefix_(indexed_table_key_prefix),
        column_id_(column_id), hybrid_time_(hybrid_time), doc_db_(doc_db),
        block_cache_(block_cache), mem_tracker_(mem_tracker) {
  }

  const TableId& table_id() const override {
    return table_id_;
  }

  Slice indexed_table_key_prefix() const override {
    return indexed_table_key_prefix_.AsSlice();
  }

  const std::string& path() const override {
    return lsm_.StorageDir();
  }

  ColumnId column_id() const override {
    return column_id_;
  }

  HybridTime hybrid_time() const override {
    return hybrid_time_;
  }

  Status Open(const std::string& log_prefix,
              const std::string& data_root_dir,
              const DocVectorIndexThreadPoolProvider& thread_pool_provider,
              const PgVectorIdxOptionsPB& idx_options) {
    index_id_ = idx_options.id();
    name_ = RemoveLogPrefixColon(log_prefix);
    auto thread_pools = thread_pool_provider();
    typename LSM::Options lsm_options = {
      .log_prefix = log_prefix,
      .storage_dir = GetStorageDir(data_root_dir, DirName()),
      .vector_index_factory = VERIFY_RESULT((GetVectorLSMFactory<Vector, DistanceResult>(
          block_cache_, idx_options, mem_tracker_))),
      .vectors_per_chunk = FLAGS_vector_index_initial_chunk_size,
      .thread_pool = thread_pools.thread_pool,
      .insert_thread_pool = thread_pools.insert_thread_pool,
      .compaction_thread_pool = thread_pools.compaction_thread_pool,
      .frontiers_factory = [] { return std::make_unique<docdb::ConsensusFrontiers>(); },
      .vector_merge_filter_factory = [this]() {
        return std::make_unique<VectorMergeFilter>(
            std::cref(lsm_.LogPrefix()), std::cref(doc_db_));
      },
      .file_extension = GetFileExtension(idx_options),
    };
    return lsm_.Open(std::move(lsm_options));
  }

  Status Destroy() override {
    return lsm_.Destroy();
  }

  Status Insert(
      const DocVectorIndexInsertEntries& entries,
      const rocksdb::UserFrontiers& frontiers) override {
    typename LSM::InsertEntries lsm_entries;
    lsm_entries.reserve(entries.size());
    for (const auto& entry : entries) {
      lsm_entries.push_back(VERIFY_RESULT(ConvertEntry<Vector>(entry)));
    }
    vector_index::VectorLSMInsertContext context {
      .frontiers = &frontiers,
    };
    return lsm_.Insert(lsm_entries, context);
  }

  Result<DocVectorIndexSearchResult> Search(
      Slice vector, const vector_index::SearchOptions& options,
      bool could_have_missing_entries) override {
    auto entries = VERIFY_RESULT(lsm_.Search(
        VERIFY_RESULT(VectorFromYSQL<Vector>(vector)), options));

    auto dump_stats = FLAGS_vector_index_dump_stats;
    auto start_time = MonoTime::NowIf(dump_stats);

    // TODO(vector_index): check if ReadOptions are required.
    docdb::BoundedRocksDbIterator iter(doc_db_.regular, {}, doc_db_.key_bounds);

    DocVectorIndexSearchResult result;
    result.could_have_more_data = entries.size() >= options.max_num_results;
    result.entries.reserve(entries.size());
    for (auto& entry : entries) {
      auto key = dockv::DocVectorKey(entry.vector_id);
      const auto& db_entry = iter.Seek(key);
      if (!db_entry.Valid() || !db_entry.key.starts_with(key)) {
        if (could_have_missing_entries) {
          continue;
        }
        return STATUS_FORMAT(NotFound, "Vector not found: $0", entry.vector_id);
      }

      // TODO(vector_index): does it handle kTombstone in db_entry.value?
      result.entries.push_back(DocVectorIndexSearchResultEntry {
        .encoded_distance = EncodeDistance(entry.distance),
        .key = KeyBuffer(db_entry.value),
      });
#ifndef NDEBUG
      if (result.entries.size() > 1) {
        CHECK_GE(result.entries.back().encoded_distance,
                 result.entries[result.entries.size() - 2].encoded_distance);
      }
#endif
    }

    LOG_IF(INFO, dump_stats)
        << "VI_STATS: Convert vector id to ybctid time: "
        << (MonoTime::Now() - start_time).ToPrettyString()
        << ", entries: " << result.entries.size()
        << ", could_have_more_data: " << result.could_have_more_data;

    return result;
  }

  Result<EncodedDistance> Distance(Slice lhs, Slice rhs) override {
    auto lhs_vec = VERIFY_RESULT(VectorFromYSQL<Vector>(lhs));
    auto rhs_vec = VERIFY_RESULT(VectorFromYSQL<Vector>(rhs));
    return EncodeDistance(lsm_.Distance(lhs_vec, rhs_vec));
  }

  void EnableAutoCompactions() override {
    lsm_.EnableAutoCompactions();
  }

  Status Compact() override {
    return lsm_.Compact(/* wait = */ true);
  }

  Status Flush() override {
    return lsm_.Flush(false);
  }

  Status WaitForFlush() override {
    return lsm_.WaitForFlush();
  }

  ConsensusFrontierPtr GetFlushedFrontier() override {
    return down_cast<ConsensusFrontier>(lsm_.GetFlushedFrontier());
  }

  rocksdb::FlushAbility GetFlushAbility() override {
      return lsm_.GetFlushAbility();
  }

  Status CreateCheckpoint(const std::string& out) override {
    return lsm_.CreateCheckpoint(GetStorageCheckpointDir(out, DirName()));
  }

  const std::string& ToString() const override {
    return name_;
  }

  Result<bool> HasVectorId(const vector_index::VectorId& vector_id) const override {
    return lsm_.HasVectorId(vector_id);
  }

  Result<size_t> TotalEntries() const override {
    return lsm_.TotalEntries();
  }

 private:
  std::string DirName() const {
    return kVectorIndexDirPrefix + index_id_;
  }

  const TableId table_id_;
  const KeyBuffer indexed_table_key_prefix_;
  const ColumnId column_id_;
  const HybridTime hybrid_time_;
  const DocDB doc_db_;
  const hnsw::BlockCachePtr block_cache_;
  const MemTrackerPtr mem_tracker_;
  std::string index_id_;

  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;

  std::string name_;
  LSM lsm_;
};

} // namespace

bool DocVectorIndex::BackfillDone() {
  if (backfill_done_cache_.load()) {
    return true;
  }
  auto frontier = GetFlushedFrontier();
  if (frontier && frontier->backfill_done()) {
    backfill_done_cache_.store(true);
    return true;
  }
  return false;
}

void DocVectorIndex::ApplyReverseEntry(
    rocksdb::DirectWriteHandler& handler, Slice ybctid, Slice value, DocHybridTime write_ht) {
  DocHybridTimeBuffer ht_buf;
  auto encoded_write_time = ht_buf.EncodeWithValueType(write_ht);
  auto vector_id = dockv::EncodedDocVectorValue::FromSlice(value).id;
  handler.Put(dockv::DocVectorKeyAsParts(vector_id, encoded_write_time), { &ybctid, 1 });
}

Result<DocVectorIndexPtr> CreateDocVectorIndex(
    const std::string& log_prefix,
    const std::string& data_root_dir,
    const DocVectorIndexThreadPoolProvider& thread_pool_provider,
    Slice indexed_table_key_prefix,
    HybridTime hybrid_time,
    const qlexpr::IndexInfo& index_info,
    const DocDB& doc_db,
    const hnsw::BlockCachePtr& block_cache,
    const MemTrackerPtr& mem_tracker) {
  auto& options = index_info.vector_idx_options();
  auto result = std::make_shared<DocVectorIndexImpl<std::vector<float>, float>>(
      index_info.table_id(), indexed_table_key_prefix, ColumnId(options.column_id()), hybrid_time,
      doc_db, block_cache, mem_tracker);
  RETURN_NOT_OK(result->Open(log_prefix, data_root_dir, thread_pool_provider, options));
  return result;
}

} // namespace yb::docdb
