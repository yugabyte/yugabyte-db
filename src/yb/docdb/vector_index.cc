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
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/write_batch.h"

#include "yb/util/decimal.h"
#include "yb/util/endian_util.h"
#include "yb/util/result.h"

#include "yb/vector_index/usearch_wrapper.h"
#include "yb/vector_index/vector_lsm.h"

DEFINE_RUNTIME_uint64(vector_index_initial_chunk_size, 1024,
                      "Number of vector in initial vector index chunk");

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

template<vector_index::IndexableVectorType Vector>
Result<Vector> VectorFromYSQL(Slice slice) {
  size_t size = VERIFY_RESULT((CheckedRead<uint16_t, LittleEndian>(slice)));
  slice.RemovePrefix(2);
  RSTATUS_DCHECK_EQ(
      slice.size(), size * sizeof(typename Vector::value_type),
      Corruption, Format("Wrong vector value size, vector: $0", slice.ToDebugHexString()));
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

  auto encoded = dockv::EncodedDocVectorValue::FromSlice(entry.value.AsSlice());
  return vector_index::VectorLSMInsertEntry<Vector> {
    .vertex_id = VERIFY_RESULT(encoded.DecodeId()),
    .base_table_key = entry.key,
    .vector = VERIFY_RESULT(VectorFromBinary<Vector>(encoded.data)),
  };
}

size_t EncodeDistance(float distance) {
  return bit_cast<uint32_t>(util::CanonicalizeFloat(distance));
}

struct VectorIndexInsertContext : public vector_index::VectorLSMInsertContext {
  rocksdb::DirectWriteHandler* handler;
  DocHybridTime write_time;
};

template<vector_index::IndexableVectorType Vector,
         vector_index::ValidDistanceResultType DistanceResult>
class VectorIndexImpl : public VectorIndex, public vector_index::VectorLSMKeyValueStorage {
 public:
  VectorIndexImpl(Slice indexed_table_key_prefix, ColumnId column_id, const DocDB& doc_db)
      : indexed_table_key_prefix_(indexed_table_key_prefix),
        column_id_(column_id), doc_db_(doc_db) {
  }

  Slice indexed_table_key_prefix() const override {
    return indexed_table_key_prefix_.AsSlice();
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
      .points_per_chunk = FLAGS_vector_index_initial_chunk_size,
      .key_value_storage = this,
      .thread_pool = &thread_pool,
      .frontiers_factory = [] { return std::make_unique<docdb::ConsensusFrontiers>(); },
    };
    return lsm_.Open(std::move(lsm_options));
  }

  Status Insert(
      const VectorIndexInsertEntries& entries,
      const rocksdb::UserFrontiers* frontiers,
      rocksdb::DirectWriteHandler* handler,
      DocHybridTime write_time) override {
    typename LSM::InsertEntries lsm_entries;
    lsm_entries.reserve(entries.size());
    for (const auto& entry : entries) {
      lsm_entries.push_back(VERIFY_RESULT(ConvertEntry<Vector>(entry)));
    }
    VectorIndexInsertContext context;
    context.frontiers = frontiers;
    context.handler = handler;
    context.write_time = write_time;
    return lsm_.Insert(lsm_entries, context);
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

 private:
  Status StoreBaseTableKeys(
      const vector_index::BaseTableKeysBatch& batch,
      const vector_index::VectorLSMInsertContext& insert_context) override {
    const auto& context = static_cast<const VectorIndexInsertContext&>(insert_context);
    for (const auto& [vector_id, base_table_key] : batch) {
      DocHybridTimeBuffer ht_buf;
      KeyBuffer kb;
      kb.PushBack(dockv::KeyEntryTypeAsChar::kVectorIndexMetadata);
      kb.PushBack(dockv::KeyEntryTypeAsChar::kVectorId);
      kb.Append(vector_id.AsSlice());
      kb.Append(ht_buf.EncodeWithValueType(context.write_time));
      auto kbs = kb.AsSlice();

      ValueBuffer vb;
      vb.Append(base_table_key);
      auto vbs = vb.AsSlice();
      context.handler->Put({&kbs, 1}, {&vbs, 1});
    }

    return Status::OK();
  }

  Result<KeyBuffer> ReadBaseTableKey(vector_index::VectorId vector_id) override {
    // TODO(vector-index) check if ReadOptions are required.
    docdb::BoundedRocksDbIterator iter(doc_db_.regular, {}, doc_db_.key_bounds);

    KeyBuffer key;
    key.PushBack(dockv::KeyEntryTypeAsChar::kVectorIndexMetadata);
    key.PushBack(dockv::KeyEntryTypeAsChar::kVectorId);
    key.Append(vector_id.AsSlice());
    const auto& entry = iter.Seek(key.AsSlice());
    if (!entry.Valid()) {
      return STATUS_FORMAT(NotFound, "Vector not found: $0", vector_id);
    }

    return KeyBuffer { entry.value };
  }

  const KeyBuffer indexed_table_key_prefix_;
  const ColumnId column_id_;

  using LSM = vector_index::VectorLSM<Vector, DistanceResult>;
  LSM lsm_;

  const DocDB doc_db_;
};

} // namespace

Result<VectorIndexPtr> CreateVectorIndex(
    const std::string& data_root_dir,
    rpc::ThreadPool& thread_pool,
    Slice indexed_table_key_prefix,
    const qlexpr::IndexInfo& index_info,
    const DocDB& doc_db) {
  auto path = Format("$0.vi-$1", data_root_dir, index_info.table_id());
  auto& options = index_info.vector_idx_options();
  auto result = std::make_shared<VectorIndexImpl<std::vector<float>, float>>(
      indexed_table_key_prefix, ColumnId(options.column_id()), doc_db);
  RETURN_NOT_OK(result->Open(path, thread_pool, options));
  return result;
}

}  // namespace yb::docdb
