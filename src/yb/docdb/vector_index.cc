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

#include "yb/common/schema.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/qlexpr/index.h"
#include "yb/qlexpr/index_column.h"

#include "yb/util/decimal.h"
#include "yb/util/endian_util.h"
#include "yb/util/result.h"

#include "yb/vector_index/ann_methods.h"
#include "yb/vector_index/vector_lsm.h"

namespace yb::docdb {

namespace {

template <template<class, class> class Factory, class LSM>
auto VectorLSMFactory(size_t dimensions) {
  using FactoryImpl = vector_index::MakeVectorIndexFactory<Factory, LSM>;
  return [dimensions] {
    vector_index::HNSWOptions hnsw_options = {
      .dimensions = dimensions,
    };
    return FactoryImpl::Create(hnsw_options);
  };
}

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
Result<typename vector_index::VectorLSMTypes<Vector, DistanceResult>::VectorIndexFactory>
    GetVectorLSMFactory(PgVectorIndexType type, size_t dimensions) {
  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;
  switch (type) {
    case PgVectorIndexType::HNSW:
      return VectorLSMFactory<vector_index::UsearchIndexFactory, LSM>(dimensions);
    case PgVectorIndexType::DUMMY: [[fallthrough]];
    case PgVectorIndexType::IVFFLAT: [[fallthrough]];
    case PgVectorIndexType::UNKNOWN_IDX:
      break;
  }
  return STATUS_FORMAT(
        NotSupported, "Vector index $0 is not supported", PgVectorIndexType_Name(type));
}

std::atomic<vector_index::VertexId> vertex_id_serial_no_{0}; // TODO(vector_index)

template<vector_index::IndexableVectorType Vector>
Result<Vector> VectorFromYSQL(Slice slice) {
  size_t size = VERIFY_RESULT((CheckedRead<uint16_t, LittleEndian>(slice)));
  slice.RemovePrefix(2);
  RSTATUS_DCHECK_EQ(
      slice.size(), size * sizeof(typename Vector::value_type),
      Corruption, "Wrong vector value size");
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
Result<Vector> VectorFromBinary(Slice slice) {
  RSTATUS_DCHECK_EQ(
      dockv::ConsumeValueEntryType(&slice), dockv::ValueEntryType::kString,
      Corruption, "Unexpected value type for vector");
  return VectorFromYSQL<Vector>(slice);
}

template<vector_index::IndexableVectorType Vector>
Result<vector_index::VectorLSMInsertEntry<Vector>> ConvertEntry(
    const VectorIndexInsertEntry& entry) {
  return vector_index::VectorLSMInsertEntry<Vector> {
    .vertex_id = ++vertex_id_serial_no_,
    .base_table_key = entry.key,
    .vector = VERIFY_RESULT(VectorFromBinary<Vector>(entry.value.AsSlice())),
  };
}

size_t EncodeDistance(float distance) {
  return bit_cast<uint32_t>(util::CanonicalizeFloat(distance));
}

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class VectorIndexImpl : public VectorIndex, public vector_index::VectorLSMKeyValueStorage {
 public:
  explicit VectorIndexImpl(ColumnId column_id) : column_id_(column_id) {
  }

  ColumnId column_id() const override {
    return column_id_;
  }

  Status Open(const std::string& path,
              rpc::ThreadPool& thread_pool,
              const PgVectorIdxOptionsPB& idx_options) {
    typename LSM::Options lsm_options = {
      .storage_dir = path,
      .vector_index_factory = VERIFY_RESULT((GetVectorLSMFactory<Vector, DistanceResult>(
          idx_options.idx_type(), idx_options.dimensions()))),
      .points_per_chunk = 1000, // TODO(vector_index) pick points per chunk from somewhere
      .key_value_storage = this, // TODO(vector_index) implement key value storage using rocksdb
      .thread_pool = &thread_pool,
      .frontiers_factory = [] { return std::make_unique<docdb::ConsensusFrontiers>(); },
    };
    return lsm_.Open(std::move(lsm_options));
  }

  Status Insert(
      const VectorIndexInsertEntries& entries, HybridTime write_time,
      const rocksdb::UserFrontiers* frontiers) override {
    typename LSM::InsertEntries lsm_entries;
    lsm_entries.reserve(entries.size());
    for (const auto& entry : entries) {
      lsm_entries.push_back(VERIFY_RESULT(ConvertEntry<Vector>(entry)));
    }
    return lsm_.Insert(lsm_entries, write_time, frontiers);
  }

  Result<VectorIndexSearchResult> Search(Slice vector, size_t max_num_results) override {
    typename LSM::SearchOptions options = {
      .max_num_results = max_num_results,
    };
    auto entries = VERIFY_RESULT(lsm_.Search(
        VERIFY_RESULT(VectorFromYSQL<Vector>(vector)), options));
    VectorIndexSearchResult result;
    result.reserve(entries.size());
    for (auto& entry : entries) {
      result.push_back(VectorIndexSearchResultEntry {
        .encoded_distance = EncodeDistance(entry.distance),
        .key = entry.base_table_key,
      });
    }
    return result;
  }

 private:
  Status StoreBaseTableKeys(
      const vector_index::BaseTableKeysBatch& batch, HybridTime write_time) override {
    std::lock_guard lock(storage_mutex_);
    for (const auto& [vertex, base_table_key] : batch) {
      vertex_id_to_key_map_.emplace(vertex, base_table_key);
    }
    return Status::OK();
  }

  Result<KeyBuffer> ReadBaseTableKey(vector_index::VertexId vertex_id) override {
    std::lock_guard lock(storage_mutex_);
    auto it = vertex_id_to_key_map_.find(vertex_id);
    if (it == vertex_id_to_key_map_.end()) {
      return STATUS_FORMAT(NotFound, "Vertex not found: $0", vertex_id);
    }
    return it->second;
  }

  const ColumnId column_id_;

  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;
  LSM lsm_;

  // TODO(vector_index) Use actual storage implementation when ready
  std::mutex storage_mutex_;
  std::unordered_map<vector_index::VertexId, KeyBuffer> vertex_id_to_key_map_
      GUARDED_BY(storage_mutex_);
};

} // namespace

Result<VectorIndexPtr> CreateVectorIndex(
    const std::string& data_root_dir, rpc::ThreadPool& thread_pool,
    const qlexpr::IndexInfo& index_info) {
  auto path = Format("$0.vi-$1", data_root_dir, index_info.table_id());
  auto& options = index_info.vector_idx_options();
  auto result = std::make_shared<VectorIndexImpl<std::vector<float>, float>>(
      ColumnId(options.column_id()));
  RETURN_NOT_OK(result->Open(path, thread_pool, options));
  return result;
}

}  // namespace yb::docdb
