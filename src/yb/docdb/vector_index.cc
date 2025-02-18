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

#include "yb/docdb/vector_index.h"

#include "yb/dockv/vector_id.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/qlexpr/index.h"

#include "yb/util/decimal.h"
#include "yb/util/endian_util.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"

#include "yb/vector_index/usearch_wrapper.h"
#include "yb/vector_index/vector_lsm.h"

DEFINE_RUNTIME_uint64(vector_index_initial_chunk_size, 100000,
                      "Number of vector in initial vector index chunk");

DEFINE_RUNTIME_PREVIEW_uint32(vector_index_ef, 128,
    "The \"expansion\" parameter for search");

DEFINE_RUNTIME_PREVIEW_uint32(vector_index_ef_construction, 256,
    "The \"expansion\" parameter during graph construction");

DEFINE_RUNTIME_PREVIEW_uint32(vector_index_num_neighbors_per_vertex, 32,
    "Number of neighbors per graph node");

DEFINE_RUNTIME_PREVIEW_uint32(vector_index_num_neighbors_per_vertex_base, 128,
    "Number of neighbors per graph node in base level graph");

namespace yb::docdb {

const std::string kVectorIndexDirPrefix = "vi-";

namespace {

template <template<class, class> class Factory, class LSM>
auto VectorLSMFactory(vector_index::DistanceKind distance_kind, size_t dimensions) {
  using FactoryImpl = vector_index::MakeVectorIndexFactory<Factory, LSM>;
  return [distance_kind, dimensions] {
    vector_index::HNSWOptions hnsw_options = {
      .dimensions = dimensions,
      .num_neighbors_per_vertex = FLAGS_vector_index_num_neighbors_per_vertex,
      .num_neighbors_per_vertex_base = FLAGS_vector_index_num_neighbors_per_vertex_base,
      .ef_construction = FLAGS_vector_index_ef_construction,
      .ef = FLAGS_vector_index_ef,
      .distance_kind = distance_kind,
    };
    return FactoryImpl::Create(hnsw_options);
  };
}

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

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
Result<typename vector_index::VectorLSMTypes<Vector, DistanceResult>::VectorIndexFactory>
    GetVectorLSMFactory(PgVectorIndexType type, vector_index::DistanceKind distance_kind,
                        size_t dimensions) {
  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;
  switch (type) {
    case PgVectorIndexType::HNSW:
      return VectorLSMFactory<vector_index::UsearchIndexFactory, LSM>(distance_kind, dimensions);
    case PgVectorIndexType::DUMMY: [[fallthrough]];
    case PgVectorIndexType::IVFFLAT: [[fallthrough]];
    case PgVectorIndexType::UNKNOWN_IDX:
      break;
  }
  return STATUS_FORMAT(
        NotSupported, "Vector index $0 is not supported", PgVectorIndexType_Name(type));
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
    const VectorIndexInsertEntry& entry) {

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

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class VectorIndexImpl : public VectorIndex {
 public:
  VectorIndexImpl(
      const TableId& table_id, Slice indexed_table_key_prefix, ColumnId column_id,
      HybridTime hybrid_time, const DocDB& doc_db)
      : table_id_(table_id), indexed_table_key_prefix_(indexed_table_key_prefix),
        column_id_(column_id), hybrid_time_(hybrid_time), doc_db_(doc_db) {
  }

  const TableId& table_id() const override {
    return table_id_;
  }

  Slice indexed_table_key_prefix() const override {
    return indexed_table_key_prefix_.AsSlice();
  }

  const std::string& path() const override {
    return lsm_.options().storage_dir;
  }

  ColumnId column_id() const override {
    return column_id_;
  }

  HybridTime hybrid_time() const override {
    return hybrid_time_;
  }

  Status Open(const std::string& log_prefix,
              const std::string& data_root_dir,
              rpc::ThreadPool& thread_pool,
              const PgVectorIdxOptionsPB& idx_options) {
    name_ = RemoveLogPrefixColon(log_prefix);
    typename LSM::Options lsm_options = {
      .log_prefix = log_prefix,
      .storage_dir = GetStorageDir(data_root_dir, DirName()),
      .vector_index_factory = VERIFY_RESULT((GetVectorLSMFactory<Vector, DistanceResult>(
          idx_options.idx_type(), ConvertDistanceKind(idx_options.dist_type()),
          idx_options.dimensions()))),
      .points_per_chunk = FLAGS_vector_index_initial_chunk_size,
      .thread_pool = &thread_pool,
      .frontiers_factory = [] { return std::make_unique<docdb::ConsensusFrontiers>(); },
    };
    return lsm_.Open(std::move(lsm_options));
  }

  Status Insert(
      const VectorIndexInsertEntries& entries, const rocksdb::UserFrontiers* frontiers) override {
    typename LSM::InsertEntries lsm_entries;
    lsm_entries.reserve(entries.size());
    for (const auto& entry : entries) {
      lsm_entries.push_back(VERIFY_RESULT(ConvertEntry<Vector>(entry)));
    }
    vector_index::VectorLSMInsertContext context {
      .frontiers = frontiers,
    };
    return lsm_.Insert(lsm_entries, context);
  }

  Result<VectorIndexSearchResult> Search(
      Slice vector, const vector_index::SearchOptions& options) override {
    auto entries = VERIFY_RESULT(lsm_.Search(
        VERIFY_RESULT(VectorFromYSQL<Vector>(vector)), options));

    // TODO(vector-index): check if ReadOptions are required.
    docdb::BoundedRocksDbIterator iter(doc_db_.regular, {}, doc_db_.key_bounds);

    VectorIndexSearchResult result;
    result.reserve(entries.size());
    for (auto& entry : entries) {
      auto key = dockv::VectorIdKey(entry.vector_id);
      const auto& db_entry = iter.Seek(key.AsSlice());
      if (!db_entry.Valid() || !db_entry.key.starts_with(key.AsSlice())) {
        return STATUS_FORMAT(NotFound, "Vector not found: $0", entry.vector_id);
      }

      result.push_back(VectorIndexSearchResultEntry {
        .encoded_distance = EncodeDistance(entry.distance),
        .key = KeyBuffer(db_entry.value),
      });
#ifndef NDEBUG
      if (result.size() > 1) {
        CHECK_GE(result.back().encoded_distance, result[result.size() - 2].encoded_distance);
      }
#endif
    }

    return result;
  }

  Result<EncodedDistance> Distance(Slice lhs, Slice rhs) override {
    auto lhs_vec = VERIFY_RESULT(VectorFromYSQL<Vector>(lhs));
    auto rhs_vec = VERIFY_RESULT(VectorFromYSQL<Vector>(rhs));
    return EncodeDistance(lsm_.Distance(lhs_vec, rhs_vec));
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

 private:
  std::string DirName() const {
    return kVectorIndexDirPrefix + table_id_;
  }

  const TableId table_id_;
  const KeyBuffer indexed_table_key_prefix_;
  const ColumnId column_id_;
  const HybridTime hybrid_time_;
  const DocDB doc_db_;

  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;

  std::string name_;
  LSM lsm_;
};

} // namespace

bool VectorIndex::BackfillDone() {
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

Result<VectorIndexPtr> CreateVectorIndex(
    const std::string& log_prefix,
    const std::string& data_root_dir,
    rpc::ThreadPool& thread_pool,
    Slice indexed_table_key_prefix,
    HybridTime hybrid_time,
    const qlexpr::IndexInfo& index_info,
    const DocDB& doc_db) {
  auto& options = index_info.vector_idx_options();
  auto result = std::make_shared<VectorIndexImpl<std::vector<float>, float>>(
      index_info.table_id(), indexed_table_key_prefix, ColumnId(options.column_id()), hybrid_time,
      doc_db);
  RETURN_NOT_OK(result->Open(log_prefix, data_root_dir, thread_pool, options));
  return result;
}

void AddVectorIndexReverseEntry(
    rocksdb::DirectWriteHandler* handler, Slice ybctid, Slice value, HybridTime write_ht) {
  DocHybridTimeBuffer ht_buf;
  auto encoded_write_time = ht_buf.EncodeWithValueType({ write_ht, 0 });
  handler->Put(
      dockv::VectorIndexReverseEntryKeyParts(value, encoded_write_time), {&ybctid, 1});
}

}  // namespace yb::docdb
