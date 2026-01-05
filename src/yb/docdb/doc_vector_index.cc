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
#include "yb/docdb/doc_rowwise_iterator.h"
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

template <class LSM>
typename LSM::Options::VectorIndexFactory VectorLSMFactory(
    const hnsw::BlockCachePtr& block_cache, const PgVectorIdxOptionsPB& options,
    const MemTrackerPtr& mem_tracker) {
  auto hnsw_options = ConvertToHnswOptions(options);
  switch (options.hnsw().backend()) {
    case HnswBackend::USEARCH: [[fallthrough]];
    case HnswBackend::YB_HNSW: {
      using FactoryImpl = vector_index::MakeVectorIndexFactory<
          ann_methods::UsearchIndexFactory, LSM>;
      return [block_cache, hnsw_options,
              backend = options.hnsw().backend(), mem_tracker](vector_index::FactoryMode mode) {
        return FactoryImpl::Create(mode, block_cache, hnsw_options, backend, mem_tracker);
      };
    }
    case HnswBackend::HNSWLIB: {
      using FactoryImpl = vector_index::MakeVectorIndexFactory<
          ann_methods::HnswlibIndexFactory, LSM>;
      return [hnsw_options](vector_index::FactoryMode mode) {
        return FactoryImpl::Create(mode, hnsw_options);
      };
    }
  }
  FATAL_INVALID_PB_ENUM_VALUE(HnswBackend, options.hnsw().backend());
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
      return VectorLSMFactory<LSM>(block_cache, options, mem_tracker);
    case PgVectorIndexType::DUMMY: [[fallthrough]];
    case PgVectorIndexType::IVFFLAT: [[fallthrough]];
    case PgVectorIndexType::UNKNOWN_IDX:
      break;
  }
  return STATUS_FORMAT(
        NotSupported, "Vector index $0 is not supported",
        PgVectorIndexType_Name(options.idx_type()));
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
  VectorMergeFilter(
      const std::string& log_prefix, DocVectorIndexReverseMappingReaderPtr reverse_mapping_reader)
      : log_prefix_(log_prefix), reverse_mapping_reader_(std::move(reverse_mapping_reader)) {
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  rocksdb::FilterDecision Filter(vector_index::VectorId vector_id) override {
    if (FLAGS_vector_index_skip_filter_check) {
      return rocksdb::FilterDecision::kKeep;
    }

    // Let's not filter the vector in case of error.
    auto decision = rocksdb::FilterDecision::kKeep;
    auto ybctid = reverse_mapping_reader_->Fetch(vector_id);
    if (!ybctid.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Failed to fetch ybctid, status: " << ybctid.status();
    } else if ((*ybctid).empty()) {
      // Simple filtering by VectorId <=> ybctid presence in the regular db. No need to check if
      // a vector is tombstoned as regular compactions take care of obsolete entries cleanup.
      decision = rocksdb::FilterDecision::kDiscard;
    }

    VLOG_WITH_PREFIX(4) << "Filtering " << vector_id << " => " << decision;
    return decision;
  }

 private:
  const std::string& log_prefix_;
  DocVectorIndexReverseMappingReaderPtr reverse_mapping_reader_;
};

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class DocVectorIndexImpl : public DocVectorIndex {
 public:
  DocVectorIndexImpl(
      const TableId& table_id, const PgVectorIdxOptionsPB& options, HybridTime hybrid_time,
      Slice indexed_table_key_prefix, DocVectorIndexContextPtr vector_index_context,
      const hnsw::BlockCachePtr& block_cache, const MemTrackerPtr& mem_tracker,
      const MetricEntityPtr& metric_entity)
      : table_id_(table_id), indexed_table_key_prefix_(indexed_table_key_prefix),
        options_(options), hybrid_time_(hybrid_time),
        context_(std::move(vector_index_context)),
        block_cache_(block_cache), mem_tracker_(mem_tracker),
        metric_entity_(metric_entity) {
    DCHECK_ONLY_NOTNULL(context_.get());
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

  const PgVectorIdxOptionsPB& options() const override {
    return options_;
  }

  ColumnId column_id() const override {
    return ColumnId(options_.column_id());
  }

  HybridTime hybrid_time() const override {
    return hybrid_time_;
  }

  const DocVectorIndexContext& context() const override {
    return *context_;
  }

  Status Open(const std::string& log_prefix,
              const std::string& storage_dir,
              const DocVectorIndexThreadPoolProvider& thread_pool_provider) {
    auto merge_filter_factory = [this]() -> typename LSM::Options::MergeFilterFactory::result_type {
      auto reader = VERIFY_RESULT(
          context_->CreateReverseMappingReader(ReadHybridTime::Max()));
      return std::make_unique<VectorMergeFilter>(lsm_.LogPrefix(), std::move(reader));
    };

    name_ = RemoveLogPrefixColon(log_prefix);
    auto thread_pools = thread_pool_provider();
    typename LSM::Options lsm_options = {
      .log_prefix = log_prefix,
      .storage_dir = storage_dir,
      .vector_index_factory = VERIFY_RESULT((GetVectorLSMFactory<Vector, DistanceResult>(
          block_cache_, options_, mem_tracker_))),
      .vectors_per_chunk = FLAGS_vector_index_initial_chunk_size,
      .thread_pool = thread_pools.thread_pool,
      .insert_thread_pool = thread_pools.insert_thread_pool,
      .compaction_token = thread_pools.compaction_token,
      .frontiers_factory = [] { return std::make_unique<docdb::ConsensusFrontiers>(); },
      .vector_merge_filter_factory = std::move(merge_filter_factory),
      .file_extension = GetVectorIndexChunkFileExtension(options_),
      .metric_entity = metric_entity_,
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

    auto reverse_mapping_reader = VERIFY_RESULT(
        context_->CreateReverseMappingReader(ReadHybridTime::Max()));

    DocVectorIndexSearchResult result;
    VLOG_WITH_FUNC(4) << "could_have_missing_entries: " << could_have_missing_entries
                      << ", entries.size(): " << entries.size()
                      << ", options.max_num_results: " << options.max_num_results;
    result.could_have_more_data =
        could_have_missing_entries && entries.size() >= options.max_num_results;
    result.entries.reserve(entries.size());
    for (auto& entry : entries) {
      auto ybctid = VERIFY_RESULT(reverse_mapping_reader->Fetch(entry.vector_id));
      VLOG_WITH_FUNC(4)
          << "vector_id: " << entry.vector_id << ", ybctid: " << ybctid.ToDebugHexString();
      if (ybctid.empty()) {
        if (could_have_missing_entries) {
          continue;
        }
        return STATUS_FORMAT(NotFound, "Vector not found: $0", entry.vector_id);
      }

      // TODO(vector_index): does it handle kTombstone in db_entry.value?
      result.entries.push_back(DocVectorIndexSearchResultEntry {
        .encoded_distance = EncodeDistance(entry.distance),
        .key = KeyBuffer(ybctid),
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
    return lsm_.Compact(/* wait = */ false);
  }

  Status WaitForCompaction() override {
    return lsm_.WaitForCompaction();
  }

  Status Flush() override {
    return lsm_.Flush(/* wait = */ false);
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
    return lsm_.CreateCheckpoint(out);
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

  void StartShutdown() override {
    lsm_.StartShutdown();
  }

  void CompleteShutdown() override {
    lsm_.CompleteShutdown();
  }

  bool TEST_HasBackgroundInserts() const override {
    return lsm_.TEST_HasBackgroundInserts();
  }

  size_t TEST_NextManifestFileNo() const override {
    return lsm_.TEST_NextManifestFileNo();
  }

 private:
  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;

  const TableId table_id_;
  const KeyBuffer indexed_table_key_prefix_;
  const PgVectorIdxOptionsPB options_;
  const HybridTime hybrid_time_;
  const DocVectorIndexContextPtr context_;
  const hnsw::BlockCachePtr block_cache_;
  const MemTrackerPtr mem_tracker_;
  const MetricEntityPtr metric_entity_;

  std::string name_;
  LSM lsm_;
};

} // namespace

Result<Slice> DocVectorIndexReverseMappingReader::Fetch(
    const vector_index::VectorId& vector_id) {
  auto key = dockv::DocVectorKey(vector_id);
  return Fetch(key);
}

Result<Slice> DocVectorIndexReverseMappingReader::FetchYbctid(
    const vector_index::VectorId& vector_id) {
  auto value = VERIFY_RESULT(Fetch(vector_id));

  // All non-ybctid values should be excluded.
  if (value.starts_with(dockv::ValueEntryTypeAsChar::kTombstone)) {
    return Slice{};
  }

  return value;
}

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
    const std::string& storage_dir,
    const DocVectorIndexThreadPoolProvider& thread_pool_provider,
    Slice indexed_table_key_prefix,
    HybridTime hybrid_time,
    const qlexpr::IndexInfo& index_info,
    DocVectorIndexContextPtr vector_index_context,
    const hnsw::BlockCachePtr& block_cache,
    const MemTrackerPtr& mem_tracker,
    const MetricEntityPtr& metric_entity) {
  auto result = std::make_shared<DocVectorIndexImpl<std::vector<float>, float>>(
      index_info.table_id(), index_info.vector_idx_options(), hybrid_time, indexed_table_key_prefix,
      std::move(vector_index_context), block_cache, mem_tracker, metric_entity);
  RETURN_NOT_OK(result->Open(log_prefix, storage_dir, thread_pool_provider));
  return result;
}

} // namespace yb::docdb
