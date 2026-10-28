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

#include "yb/ann_methods/hnswlib_wrapper.h"
#include "yb/ann_methods/usearch_wrapper.h"

#include "yb/common/common.pb.h"

#include "yb/dockv/doc_vector_id.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/read_operation_data.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/gutil/casts.h"

#include "yb/hnsw/hnsw_block_cache.h"

#include "yb/qlexpr/index.h"

#include "yb/util/decimal.h"
#include "yb/util/endian_util.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"

#include "yb/vector_index/vectorann_util.h"
#include "yb/vector_index/vector_lsm.h"

METRIC_DEFINE_event_stats(table, vector_index_convert_us,
    "Time to convert list of vector ids to ybctids", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that operations spent converting list of vector ids to ybctids.");
METRIC_DEFINE_event_stats(table, vector_index_filter_checked,
    "Checked entries during vector index search", yb::MetricUnit::kEntries,
    "Number of entries checked by filter during vector index search.");
METRIC_DEFINE_event_stats(table, vector_index_filter_accepted,
    "Accepted entries during vector index search", yb::MetricUnit::kEntries,
    "Number of entries passed the filter during vector index search.");
METRIC_DEFINE_event_stats(table, vector_index_filter_removed,
    "Removed entries during vector index search", yb::MetricUnit::kEntries,
    "Number of removed entries touched during vector index search.");
METRIC_DEFINE_event_stats(table, vector_index_read_data_us,
    "Time to read data for vector index entries", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that operations spent reading data for found vectors.");
METRIC_DEFINE_event_stats(table, vector_index_read_intents_us,
    "Time to read data for vector index intents", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that operations spent reading vector index intents.");
METRIC_DEFINE_event_stats(table, vector_index_merge_us,
    "Time to merge vector index entries from LSM and intents", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that operations spent merging vector index entries found in intents and "
    "LSM.");
METRIC_DEFINE_event_stats(table, vector_index_found_intents,
    "Found intents during vector index search", yb::MetricUnit::kEntries,
    "Number of intents found during vector index search.");
METRIC_DEFINE_event_stats(table, vector_index_result_size,
    "Resulting entries of vector index search", yb::MetricUnit::kEntries,
    "Number of entries returned by vector index search.");
METRIC_DEFINE_counter(table, vector_index_backfill_inserted_entries,
    "Backfilled entries of vector index", yb::MetricUnit::kEntries,
    "Number of entries inserted into the vector index during backfill. Tracks the progress of "
    "vector index backfill.");

DEFINE_test_flag(bool, vector_index_skip_insert, false,
    "Skip inserting vectors into vector indexes, leaving the rows without index entries.");

DEFINE_test_flag(bool, vector_index_corrupt_inserted_vectors, false,
    "Alter the first coordinate of every vector inserted into vector indexes, so the index "
    "stores vectors which differ from the ones in the rows.");

DEFINE_test_flag(bool, vector_index_corrupt_inserted_payloads, false,
    "Append a byte to the ybctid in the payload of every vector inserted into vector indexes, so "
    "the payload references a row which does not exist.");

DEFINE_test_flag(bool, vector_index_skip_reverse_mapping_write, false,
    "Skip writing vector reverse mapping entries and tombstones.");

DEFINE_test_flag(bool, vector_index_duplicate_inserted_vectors, false,
    "Insert every vector into vector indexes twice, into different chunks.");

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

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
Result<vector_index::VectorIndexTraitsPtr<Vector, DistanceResult>> HnswTraits(
    const hnsw::BlockCachePtr& block_cache, const PgVectorIdxOptionsPB& options,
    const MemTrackerPtr& mem_tracker) {
  auto hnsw_options = ConvertToHnswOptions(options);
  auto backend = options.hnsw().backend();
  switch (backend) {
    case HnswBackend::YB_HNSW_USEARCH:
      return ann_methods::CreateUsearchIndexTraits<Vector, DistanceResult>(
          block_cache, hnsw_options, mem_tracker);
    case HnswBackend::YB_HNSW_HNSWLIB:
      return ann_methods::CreateHnswlibIndexTraits<Vector, DistanceResult>(
          block_cache, hnsw_options, mem_tracker);
    case HnswBackend::DEPRECATED_USEARCH: [[fallthrough]];
    case HnswBackend::DEPRECATED_HNSWLIB:
      break;
  }
  return STATUS_FORMAT(
      NotSupported,
      "Vector index backend $0 is no longer supported. Roll back to the previous release and drop "
      "the index before upgrading",
      HnswBackend_Name(backend));
}

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
auto GetVectorLSMTraits(
    const hnsw::BlockCachePtr& block_cache, const PgVectorIdxOptionsPB& options,
    const MemTrackerPtr& mem_tracker)
    -> Result<vector_index::VectorIndexTraitsPtr<Vector, DistanceResult>> {
  switch (options.idx_type()) {
    case PgVectorIndexType::HNSW:
      return HnswTraits<Vector, DistanceResult>(block_cache, options, mem_tracker);
    case PgVectorIndexType::DEPRECATED_DUMMY: [[fallthrough]];
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

// Inverse of VectorFromYSQL: dimensions, the unused int16 of the pgvector header, coordinates.
template<vector_index::IndexableVectorType Vector>
void VectorToYSQL(const Vector& vector, ValueBuffer* out) {
  using Coordinate = typename Vector::value_type;
  out->clear();
  out->reserve(2 * sizeof(uint16_t) + vector.size() * sizeof(Coordinate));
  char buffer[2 * sizeof(uint16_t)] = {};
  Store<uint16_t, LittleEndian>(buffer, narrow_cast<uint16_t>(vector.size()));
  out->Append(Slice(buffer, sizeof(buffer)));
  // Store takes integers only, so the coordinate goes through the unsigned type of its size,
  // the same one Load returns for it.
  using RawCoordinate = decltype(LoadRaw<Coordinate, LittleEndian>(nullptr));
  for (const auto& coordinate : vector) {
    char coordinate_buffer[sizeof(Coordinate)];
    Store<RawCoordinate, LittleEndian>(coordinate_buffer, bit_cast<RawCoordinate>(coordinate));
    out->Append(Slice(coordinate_buffer, sizeof(coordinate_buffer)));
  }
}

template<vector_index::IndexableVectorType Vector>
Result<vector_index::VectorLSMInsertEntry<Vector>> ConvertEntry(
    const DocVectorIndexInsertEntry& entry, bool store_payload) {

  RSTATUS_DCHECK(!entry.value.empty(), InvalidArgument, "Vector value is not specified");
  RSTATUS_DCHECK(
      !store_payload || !entry.ybctid.empty(), InvalidArgument, "Vector ybctid is not specified");

  auto encoded = dockv::EncodedDocVectorValue::FromSlice(entry.value.AsSlice());
  auto vector = VERIFY_RESULT(VectorFromYSQL<Vector>(encoded.data));
  if (FLAGS_TEST_vector_index_corrupt_inserted_vectors && !vector.empty()) {
    vector.front() += 1;
  }
  ValueBuffer payload;
  if (store_payload) {
    if (FLAGS_TEST_vector_index_corrupt_inserted_payloads) {
      // Sorts after every key of the row.
      dockv::KeyBytes ybctid(entry.ybctid.AsSlice(), dockv::KeyEntryTypeAsChar::kMaxByte);
      payload = dockv::DocVectorIndexPayload(ybctid.AsSlice());
    } else {
      payload = dockv::DocVectorIndexPayload(entry.ybctid.AsSlice());
    }
  }
  return vector_index::VectorLSMInsertEntry<Vector> {
    .vector_id = VERIFY_RESULT(encoded.DecodeId()),
    .vector = std::move(vector),
    .payload = std::move(payload),
  };
}

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class DocVectorIndexViewImpl : public DocVectorIndexView {
 public:
  using VectorIndexPtr = vector_index::VectorIndexIfPtr<Vector, DistanceResult>;

  explicit DocVectorIndexViewImpl(std::vector<VectorIndexPtr> indexes)
      : indexes_(std::move(indexes)) {}

  Status ForEachVector(const DocVectorIndexEntryCallback& callback) const override {
    ValueBuffer buffer;
    for (const auto& index : indexes_) {
      for (const auto& entry : *index) {
        VectorToYSQL(entry.vector, &buffer);
        RETURN_NOT_OK(callback(DocVectorIndexEntry {
          .vector_id = entry.vector_id,
          .vector = buffer.AsSlice(),
          .payload = entry.payload,
        }));
      }
    }
    return Status::OK();
  }

  size_t NumVectors() const override {
    size_t result = 0;
    for (const auto& index : indexes_) {
      result += index->Size();
    }
    return result;
  }

 private:
  const std::vector<VectorIndexPtr> indexes_;
};

EncodedDistance EncodeDistance(float distance) {
  uint32_t v = bit_cast<uint32_t>(distance);
  if (v >> 31) {
    return ~v;
  } else {
    return v ^ util::kInt32SignBitFlipMask;
  }
}

// Whether the row the payload points to is within the tablet key bounds.
Result<bool> IsPayloadWithinKeyBounds(const KeyBounds& key_bounds, Slice payload) {
  if (!key_bounds.IsInitialized()) {
    return true;
  }
  return key_bounds.IsWithinBounds(VERIFY_RESULT(dockv::DocVectorIndexPayloadYbctid(payload)));
}

class VectorMergeFilter : public vector_index::VectorLSMMergeFilter {
 public:
  VectorMergeFilter(
      const std::string& log_prefix, const KeyBounds& key_bounds,
      DocVectorIndexReverseMappingReaderPtr reverse_mapping_reader)
      : log_prefix_(log_prefix), key_bounds_(key_bounds),
        reverse_mapping_reader_(std::move(reverse_mapping_reader)) {
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  rocksdb::FilterDecision Filter(vector_index::VectorId vector_id, Slice payload) override {
    if (FLAGS_vector_index_skip_filter_check) {
      return rocksdb::FilterDecision::kKeep;
    }

    // Let's not filter the vector in case of error.
    auto decision = rocksdb::FilterDecision::kKeep;

    if (!payload.empty()) {
      // The index stores payloads, so it has no reverse mapping entries at all and compaction
      // cannot tell whether the row the payload points to still exists.
      // Vectors of the sibling tablet left by a split are dropped by the key bounds.
      auto within_bounds = IsPayloadWithinKeyBounds(key_bounds_, payload);
      if (!within_bounds.ok()) {
        LOG_WITH_PREFIX(DFATAL)
            << "Failed to decode payload of " << vector_id << ": " << within_bounds.status();
      } else if (!*within_bounds) {
        decision = rocksdb::FilterDecision::kDiscard;
      }
      VLOG_WITH_PREFIX(4) << "Filtering " << vector_id << " => " << decision;
      return decision;
    }

    // Use Fetch (raw value), not FetchYbctid: we only care whether a reverse-mapping entry
    // exists. Decoding is unnecessary, and FetchYbctid would treat tombstones as missing
    // while this filter relies on regular compaction to clean those up. Fetch also drops
    // live mappings whose ybctid is outside tablet key bounds.
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
  const KeyBounds& key_bounds_;
  DocVectorIndexReverseMappingReaderPtr reverse_mapping_reader_;
};

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class DocVectorIndexImpl : public DocVectorIndex {
 public:
  DocVectorIndexImpl(
      const TableId& table_id, const PgVectorIdxOptionsPB& options, HybridTime hybrid_time,
      Slice indexed_table_key_prefix, TableWritesReverseMapping table_writes_reverse_mapping,
      DocVectorIndexContextPtr vector_index_context,
      const hnsw::BlockCachePtr& block_cache, const MemTrackerPtr& mem_tracker,
      const MetricEntityPtr& metric_entity)
      : table_id_(table_id),
        indexed_table_key_prefix_(indexed_table_key_prefix),
        options_(options),
        stores_payload_(options.store_payload() || !table_writes_reverse_mapping),
        table_writes_reverse_mapping_(table_writes_reverse_mapping),
        hybrid_time_(hybrid_time),
        context_(std::move(vector_index_context)),
        block_cache_(block_cache),
        mem_tracker_(mem_tracker),
        metric_entity_(metric_entity),
        metrics_(metric_entity) {
    DCHECK_ONLY_NOTNULL(context_.get());
  }

  const TableId& table_id() const override {
    return table_id_;
  }

  bool StoresPayload() const override {
    return stores_payload_;
  }

  bool HasReverseMapping() const override {
    return table_writes_reverse_mapping_;
  }

  Result<DocVectorIndexViewPtr> CreateImmutableView() override {
    return std::make_unique<DocVectorIndexViewImpl<Vector, DistanceResult>>(
        VERIFY_RESULT(lsm_.ManifestIndexes()));
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

  uint64_t split_generation() const override {
    return split_generation_;
  }

  const DocVectorIndexContext& context() const override {
    return *context_;
  }

  const DocVectorIndexMetrics& metrics() const override {
    return metrics_;
  }

  Status Open(const std::string& log_prefix,
              const std::string& storage_dir,
              uint64_t split_generation,
              const DocVectorIndexThreadPoolProvider& thread_pool_provider) {
    auto merge_filter_factory = [this]() -> typename LSM::Options::MergeFilterFactory::result_type {
      auto reader =
          VERIFY_RESULT(context_->CreateReverseMappingReader(ReadHybridTime::Max(), nullptr));
      return std::make_unique<VectorMergeFilter>(
          lsm_.LogPrefix(), context_->key_bounds(), std::move(reader));
    };

    name_ = RemoveLogPrefixColon(log_prefix);
    auto thread_pools = thread_pool_provider();
    typename LSM::Options lsm_options = {
      .log_prefix = log_prefix,
      .storage_dir = storage_dir,
      .vector_index_traits = VERIFY_RESULT((GetVectorLSMTraits<Vector, DistanceResult>(
          block_cache_, options_, mem_tracker_))),
      .thread_pool = thread_pools.thread_pool,
      .insert_thread_pool = thread_pools.insert_thread_pool,
      .compaction_token = thread_pools.compaction_token,
      .frontiers_factory = [] { return std::make_unique<docdb::ConsensusFrontiers>(); },
      .vector_merge_filter_factory = std::move(merge_filter_factory),
      .file_extension = GetVectorIndexChunkFileExtension(options_),
      .metric_entity = metric_entity_,
      .block_cache_capacity = block_cache_ ? block_cache_->capacity() : 0,
      // Fixed by master at index creation: the chunk payload mode and the reverse mapping
      // writes must stay consistent for the whole lifetime of the index.
      .store_vector_payload = vector_index::StoreVectorPayload(StoresPayload()),
    };
    RETURN_NOT_OK(lsm_.Open(std::move(lsm_options)));

    return InitFrontiers(split_generation);
  }

  Status Destroy() override {
    return lsm_.Destroy();
  }

  Status Insert(
      const DocVectorIndexInsertEntries& entries,
      const InsertOptions& insert_options) override {
    typename LSM::InsertEntries lsm_entries;
    if (!FLAGS_TEST_vector_index_skip_insert) {
      lsm_entries.reserve(entries.size());
      for (const auto& entry : entries) {
        lsm_entries.push_back(VERIFY_RESULT(ConvertEntry<Vector>(entry, StoresPayload())));
      }
    }
    vector_index::VectorLSMInsertContext context {
      .frontiers = insert_options.frontiers,
      .chunk_size = insert_options.chunk_size,
      .reservation_mode = insert_options.reservation_mode,
      .chunk_holds = insert_options.chunk_holds,
    };
    RETURN_NOT_OK(lsm_.Insert(lsm_entries, context));
    if (FLAGS_TEST_vector_index_duplicate_inserted_vectors && !lsm_entries.empty()) {
      // The mutable chunk could keep only one copy.
      RETURN_NOT_OK(lsm_.Flush(/* wait= */ false));
      return lsm_.Insert(std::move(lsm_entries), context);
    }
    return Status::OK();
  }

  size_t EstimateNumVectorsForBytes(size_t bytes_limit) const override {
    return lsm_.EstimateNumVectorsForBytes(bytes_limit);
  }

  size_t DefaultChunkCapacity() const override {
    return lsm_.DefaultChunkCapacity();
  }

  Result<DocVectorIndexSearchResult> Search(
      Slice vector, const vector_index::SearchOptions& options, bool could_have_missing_entries,
      DocVectorIndexReverseMappingReader& reverse_mapping_reader) override {
    auto entries = VERIFY_RESULT(SearchWithinKeyBounds(
        VERIFY_RESULT(VectorFromYSQL<Vector>(vector)), options));

    auto dump_stats = FLAGS_vector_index_dump_stats;
    auto start_time = MonoTime::Now();

    // Let a test apply a DELETE's reverse-mapping tombstone between the filter's read and the
    // resolution below, reproducing the "Vector not found" race.
    TEST_SYNC_POINT("DocVectorIndexImpl::Search:AfterFilter");
    TEST_SYNC_POINT("DocVectorIndexImpl::Search:BeforeResolve");

    // Entries which carry a payload resolve directly from it. Other entries are
    // resolved with the caller's reader -- the same one the filter used -- so both see one
    // snapshot. Otherwise a DELETE whose reverse-mapping tombstone lands between the two reads lets
    // the filter accept an entry that resolves empty here; such a row is still dropped when its
    // ybctid is fetched (the row delete is intent-tracked, unlike the physical-only reverse map).
    DocVectorIndexSearchResult result;
    VLOG_WITH_FUNC(4) << "could_have_missing_entries: " << could_have_missing_entries
                      << ", entries.size(): " << entries.size()
                      << ", options.max_num_results: " << options.max_num_results;
    result.could_have_more_data =
        could_have_missing_entries && entries.size() >= options.max_num_results;
    result.entries.reserve(entries.size());
    for (auto& entry : entries) {
      Slice ybctid;
      if (!entry.payload.empty()) {
        ybctid = VERIFY_RESULT(dockv::DocVectorIndexPayloadYbctid(entry.payload.AsSlice()));
      }
      const auto ybctid_from_payload = !ybctid.empty();
      if (ybctid.empty()) {
        // The index does not store payloads, resolve the ybctid via the reverse mapping.
        ybctid = VERIFY_RESULT(reverse_mapping_reader.FetchYbctid(entry.vector_id));
        if (ybctid.empty()) {
          if (could_have_missing_entries) {
            continue;
          }
          return STATUS_FORMAT(NotFound, "Vector not found: $0", entry.vector_id);
        }
      }
      VLOG_WITH_FUNC(4)
          << "vector_id: " << entry.vector_id << ", ybctid: " << ybctid.ToDebugHexString()
          << ", source: " << (ybctid_from_payload ? "payload" : "reverse mapping");

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

    auto passed = MonoTime::Now() - start_time;
    metrics_.convert_us->Increment(passed.ToMicroseconds());

    LOG_IF(INFO, dump_stats)
        << "VI_STATS: Convert vector id to ybctid time: "
        << passed.ToPrettyString()
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

  storage::FrontierInfo GetFrontiers(storage::FrontierKinds kinds) override {
    return lsm_.GetFrontiers(kinds);
  }

  Status ModifyFlushedFrontier(
      const ConsensusFrontier& frontier, rocksdb::FrontierModificationMode mode) override {
    if (mode == rocksdb::FrontierModificationMode::kForce) {
      RETURN_NOT_OK(lsm_.ModifyChunkFrontiers([&frontier](storage::UserFrontiers& frontiers) {
        down_cast<ConsensusFrontier&>(frontiers.Smallest()).set_op_id(frontier.op_id());
        down_cast<ConsensusFrontier&>(frontiers.Largest()).set_op_id(frontier.op_id());
      }));
    }
    // An empty insert only carries the frontier into the mutable chunk, the following flush
    // persists it. Same shape as the split generation update above.
    ConsensusFrontiers frontiers;
    frontiers.Largest().set_op_id(frontier.op_id());
    RETURN_NOT_OK(Insert(
        DocVectorIndexInsertEntries{}, InsertOptions{ .frontiers = &frontiers, }));
    // Wait for the flush: callers stamp an OpId to make it durable, and an ungraceful restart
    // could otherwise happen before the chunk reaches the manifest.
    RETURN_NOT_OK(Flush());
    return WaitForFlush();
  }

  Result<bool> AdvanceFlushedOpId(const OpId& op_id) override {
    ConsensusFrontiers frontiers;
    frontiers.Largest().set_op_id(op_id);
    return lsm_.AdvanceFlushedFrontier(frontiers);
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

  Result<size_t> TotalEntries() const override {
    return lsm_.TotalEntries();
  }

  uint64_t OnDiskSize() const override {
    return lsm_.OnDiskSize();
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

  const std::string& LogPrefix() const {
    return lsm_.LogPrefix();
  }

  Result<typename LSM::SearchResults> SearchWithinKeyBounds(
      const Vector& query_vector, const vector_index::SearchOptions& options) const {
    const auto& key_bounds = context_->key_bounds();
    if (!StoresPayload() || !key_bounds.IsInitialized()) {
      return lsm_.Search(query_vector, options);
    }
    // Chunks inherited from the split parent keep the sibling tablet vectors until compaction.
    // Skip them, so they do not displace the vectors of this tablet from the result.
    auto search_options = options;
    search_options.filter = [&key_bounds, &filter = options.filter](
        const vector_index::VectorId& vector_id, Slice payload) {
      // A payload which fails to decode is passed through: the caller's filter
      // or the ybctid resolution in Search reports it if it reaches either.
      auto within_bounds = IsPayloadWithinKeyBounds(key_bounds, payload);
      return (!within_bounds.ok() || *within_bounds) && filter(vector_id, payload);
    };
    return lsm_.Search(query_vector, search_options);
  }

  // Detects whether a new split occurred. If so, records the new split generation and
  // split_min_chunk_serial_no so ParentDataCompacted() can check whether all inherited chunks gone.
  Status InitFrontiers(uint64_t split_generation) {
    auto frontier = GetFlushedFrontier();

    // Sanity check. Absent frontier is possible only if there are no data chunks yet.
    // It is safe to read MinSerialNo() as no flush could be happening at this point.
    DCHECK(frontier || !lsm_.MinSerialNo());

    const auto persisted_split_generation = frontier ? frontier->split_generation() : 0;
    if (persisted_split_generation > split_generation) {
      // Having split_generation == 0 (taken from the superblock) with a non-zero split_generation
      // from the vector index manifest means the superblock lost the field: either an older build
      // rewrote KvStoreInfo without it, or the metadata came from a snapshot predating the field.
      // Tablet restores the superblock from the vector indexes manifests after open.
      if (split_generation == 0) {
        LOG_WITH_PREFIX(WARNING)
            << "Superblock split_generation is 0 while vector index manifest has "
            << persisted_split_generation
            << "; will be restored after tablet opened";
      } else {
        // Any other mismatch is unexpected.
        LOG_WITH_PREFIX(DFATAL)
            << "Persisted split generation " << persisted_split_generation << " is greater than "
            << "the current split generation " << split_generation;
      }
    }

    // No split happened, just pick the current split_min_chunk_serial_no for the cases when
    // previous post-split compaction was not yet completed.
    if (split_generation <= persisted_split_generation) {
      split_generation_ = persisted_split_generation;
      split_min_chunk_serial_no_ = frontier ? frontier->split_min_chunk_serial_no() : 0;
      return Status::OK();
    }

    // No frontier means the index has no data chunks yet. Which means we can treat it as
    // all parent data compacted and avoid persisting frontiers without chunks.
    // However, we still need to persist the split generation to not treat it as a new split
    // when opening the index, when it is created on a split child.
    if (frontier) {
      // A new split happened since the last recording, update split_min_chunk_serial_no.
      split_min_chunk_serial_no_ = lsm_.LastSerialNo() + 1;
    }

    // Update the split generation.
    DCHECK_GT(split_generation, 0);
    split_generation_ = split_generation;

    // Persist the new split generation together with split_min_chunk_serial_no, so
    // ParentDataCompacted() can later tell whether all pre-split chunks have been compacted away.
    ConsensusFrontiers frontiers;
    frontiers.Largest().SetSplitGeneration(split_generation_);
    if (split_min_chunk_serial_no_) {
      frontiers.Largest().SetSplitMinChunkSerialNo(split_min_chunk_serial_no_);
    }

    // No need to wait for flush here, as Insert happens on vector index open, before any data
    // is inserted, and VectorLSM keeps flushes ordered, so the boundary can not miss a chunk.
    RETURN_NOT_OK(Insert(
        DocVectorIndexInsertEntries{}, InsertOptions{ .frontiers = &frontiers, }));
    return Flush();
  }

  // Checks whether all data chunks have serial_no >= split_min_chunk_serial_no.
  // split_min_chunk_serial_no == 0 or no data chunks means no pending post-split compaction.
  bool ComputeParentDataCompacted() const override {
    // All legacy vector indexes (created before introducing this parameter) are treated as
    // already compacted, because there's no simple way to tell whether they were really compacted.
    if (split_min_chunk_serial_no_ == 0) {
      return true;
    }

    // Treat no chunks as all parent data has been compacted.
    auto min_serial_no = lsm_.MinSerialNo();
    if (!min_serial_no) {
      return true;
    }

    return *min_serial_no >= split_min_chunk_serial_no_;
  }

  const TableId table_id_;
  const KeyBuffer indexed_table_key_prefix_;
  const PgVectorIdxOptionsPB options_;
  const bool stores_payload_;
  const bool table_writes_reverse_mapping_;
  const HybridTime hybrid_time_;
  const DocVectorIndexContextPtr context_;
  const hnsw::BlockCachePtr block_cache_;
  const MemTrackerPtr mem_tracker_;
  const MetricEntityPtr metric_entity_;
  const DocVectorIndexMetrics metrics_;

  // Both are set by InitFrontiers() during Open() and stay immutable afterwards.
  uint64_t split_generation_ = 0;
  uint64_t split_min_chunk_serial_no_ = 0;

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
  if (value.empty()) {
    return Slice{};
  }

  auto decoded = VERIFY_RESULT(dockv::EncodedDocVectorMetaValue::Decode(value));
  return decoded.IsTombstone() ? Slice{} : decoded.ybctid;
}

ConsensusFrontierPtr DocVectorIndex::GetFlushedFrontier() {
  return down_cast<ConsensusFrontier>(
      GetFrontiers(storage::FrontierKinds{storage::FrontierKind::kFlushed}).flushed);
}

storage::UserFrontierRange DocVectorIndex::GetInMemoryFrontiers() {
  return GetFrontiers(storage::FrontierKinds{
      storage::FrontierKind::kInMemorySmallest,
      storage::FrontierKind::kInMemoryLargest}).in_memory;
}

storage::UserFrontierPtr DocVectorIndex::GetInMemoryFrontier(storage::UpdateUserValueType type) {
  if (type == storage::UpdateUserValueType::kSmallest) {
    return GetFrontiers(storage::FrontierKinds{
        storage::FrontierKind::kInMemorySmallest}).in_memory.smallest;
  }
  return GetFrontiers(storage::FrontierKinds{
      storage::FrontierKind::kInMemoryLargest}).in_memory.largest;
}

bool DocVectorIndex::IsIndexedTableKey(Slice key) const {
  return key.starts_with(indexed_table_key_prefix());
}

Result<bool> DocVectorIndex::BackfillDone() {
  if (backfill_done_cache_.load()) {
    return true;
  }
  auto frontier = GetFlushedFrontier();
  if (frontier && frontier->backfill_done()) {
    backfill_done_cache_.store(true);
    return true;
  }
  std::lock_guard lock(backfill_status_mutex_);
  if (!backfill_status_.ok()) {
    return backfill_status_;
  }
  return false;
}

void DocVectorIndex::SetBackfillStatus(const Status& status) {
  std::lock_guard lock(backfill_status_mutex_);
  backfill_status_ = status;
}

bool DocVectorIndex::ParentDataCompacted() {
  if (parent_data_compacted_cache_.load(std::memory_order_relaxed)) {
    return true;
  }

  if (ComputeParentDataCompacted()) {
    parent_data_compacted_cache_.store(true, std::memory_order_relaxed);
    return true;
  }

  return false;
}

void DocVectorIndex::ApplyReverseEntry(
    rocksdb::DirectWriteHandler& handler, Slice ybctid, Slice value, DocHybridTime write_ht,
    ColumnId column_id, Slice table_key_prefix) {
  if (FLAGS_TEST_vector_index_skip_reverse_mapping_write) {
    return;
  }
  DocHybridTimeBuffer ht_buf;
  auto encoded_write_time = ht_buf.EncodeWithValueType(write_ht);
  auto vector_id = dockv::EncodedDocVectorValue::FromSlice(value).id;

  // Legacy format.
  if (column_id == kInvalidColumnId) {
    handler.Put(dockv::DocVectorKeyAsParts(vector_id, encoded_write_time), { &ybctid, 1 });
    return;
  }

  auto value_buffer = dockv::DocVectorMetaValue(table_key_prefix, ybctid, column_id);
  auto value_slice = value_buffer.AsSlice();
  handler.Put(dockv::DocVectorKeyAsParts(vector_id, encoded_write_time), { &value_slice, 1 });
}

Result<DocVectorIndexPtr> CreateDocVectorIndex(
    const std::string& log_prefix,
    const std::string& storage_dir,
    const DocVectorIndexThreadPoolProvider& thread_pool_provider,
    Slice indexed_table_key_prefix,
    TableWritesReverseMapping table_writes_reverse_mapping,
    HybridTime hybrid_time,
    uint64_t split_generation,
    const qlexpr::IndexInfo& index_info,
    DocVectorIndexContextPtr vector_index_context,
    const hnsw::BlockCachePtr& block_cache,
    const MemTrackerPtr& mem_tracker,
    const MetricEntityPtr& metric_entity) {
  auto result = std::make_shared<DocVectorIndexImpl<std::vector<float>, float>>(
      index_info.table_id(), index_info.vector_idx_options(), hybrid_time, indexed_table_key_prefix,
      table_writes_reverse_mapping, std::move(vector_index_context), block_cache, mem_tracker,
      metric_entity);
  RETURN_NOT_OK(result->Open(log_prefix, storage_dir, split_generation, thread_pool_provider));
  return result;
}

DocVectorIndexMetrics::DocVectorIndexMetrics(const MetricEntityPtr& metric_entity)
    : convert_us(METRIC_vector_index_convert_us.Instantiate(metric_entity)),
      filter_checked(METRIC_vector_index_filter_checked.Instantiate(metric_entity)),
      filter_accepted(METRIC_vector_index_filter_checked.Instantiate(metric_entity)),
      filter_removed(METRIC_vector_index_filter_checked.Instantiate(metric_entity)),
      read_data_us(METRIC_vector_index_read_data_us.Instantiate(metric_entity)),
      read_intents_us(METRIC_vector_index_read_intents_us.Instantiate(metric_entity)),
      merge_us(METRIC_vector_index_merge_us.Instantiate(metric_entity)),
      found_intents(METRIC_vector_index_found_intents.Instantiate(metric_entity)),
      result_size(METRIC_vector_index_result_size.Instantiate(metric_entity)),
      backfill_inserted_entries(
          METRIC_vector_index_backfill_inserted_entries.Instantiate(metric_entity)) {
}

} // namespace yb::docdb
