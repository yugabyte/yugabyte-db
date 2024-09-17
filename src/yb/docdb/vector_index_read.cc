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

#include "yb/docdb/vector_index_read.h"

#include "yb/vector/graph_repr_defs.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"

#include "yb/util/endian_util.h"

namespace yb::docdb {

namespace {

template <class CoordinateType>
class VectorIndexRead {
 public:
  using Types = VectorIndexTypes<CoordinateType>;
  using IndexedVector = typename Types::IndexedVector;

  VectorIndexRead(
      const DocDB& doc_db, const ReadOperationData& read_operation_data)
      : iter_(CreateIntentAwareIterator(
            doc_db,
            // TODO(!!!) add bloom filter usage.
            BloomFilterMode::DONT_USE_BLOOM_FILTER,
            boost::none,
            rocksdb::kDefaultQueryId,
            TransactionOperationContext(),
            read_operation_data,
            /* file_filter= */ nullptr,
            /* iterate_upper_bound= */ nullptr,
            FastBackwardScan::kFalse)) {
  }

  Result<IndexedVector> GetVector(VertexId id) {
    auto key = MakeVectorIndexKey(id);
    iter_->Seek(key);
    auto kv = VERIFY_RESULT_REF(iter_->Fetch());
    if (!kv || kv.key != key.AsSlice() ||
        kv.value.TryConsumeByte(dockv::ValueEntryTypeAsChar::kTombstone)) {
      return IndexedVector();
    }

    return dockv::PrimitiveValue::DecodeFloatVector(kv.value);
  }

  Result<vectorindex::VectorNodeNeighbors> GetNeighbors(VertexId id, VectorIndexLevel level) {
    auto vertex_level_key_bytes = MakeVectorIndexKey(id, level);
    auto vertex_level_key = vertex_level_key_bytes.AsSlice();
    iter_->Seek(vertex_level_key);
    auto kv = VERIFY_RESULT_REF(iter_->Fetch());
    if (!kv || !kv.key.starts_with(vertex_level_key)) {
      return VectorNodeNeighbors();
    }

    VectorNodeNeighbors result;
    EncodedDocHybridTime full_write_time(EncodedDocHybridTime::kMin);
    // The list of neighbors.
    if (kv.key == vertex_level_key) {
      auto vector = VERIFY_RESULT(dockv::PrimitiveValue::DecodeUInt64Vector(kv.value));
      result.insert(vector.begin(), vector.end());
      full_write_time = kv.write_time;
      for (;;) {
        // Could be useful to seek in case a lot of calls to next does not move iterator to the next
        // key.
        kv = VERIFY_RESULT_REF(iter_->FetchNext());
        if (!kv || kv.key != vertex_level_key) {
          break;
        }
      }
    }

    auto prev_vertex_id = vectorindex::kInvalidVertexId;
    auto vertex_level_key_size = vertex_level_key.size();
    while (kv && kv.key.starts_with(vertex_level_key)) {
      if (kv.write_time < full_write_time) {
        kv = VERIFY_RESULT_REF(iter_->FetchNext());
        continue;
      }
      auto vertex_id_slice = kv.key.WithoutPrefix(vertex_level_key_size);
      RETURN_NOT_OK(dockv::ConsumeKeyEntryType(vertex_id_slice, dockv::KeyEntryType::kVertexId));
      auto vertex_id = VERIFY_RESULT((CheckedReadFull<uint64_t, BigEndian>(vertex_id_slice)));
      if (vertex_id != prev_vertex_id) {
        auto value_type = dockv::ConsumeValueEntryType(kv.value);
        if (value_type == dockv::ValueEntryType::kNullLow) {
          result.insert(vertex_id);
        } else if (value_type == dockv::ValueEntryType::kTombstone) {
          result.erase(vertex_id);
        } else {
          return STATUS_FORMAT(
              Corruption, "Unexpected value type for directed edge: $0 -> $1 at $2: $3",
              id, vertex_id, level, value_type);
        }
        prev_vertex_id = vertex_id;
      }
      kv = VERIFY_RESULT_REF(iter_->FetchNext());
    }

    return result;
  }

 private:
  std::unique_ptr<IntentAwareIterator> iter_;
  // TODO(!!!) DeadlineInfo& deadline_info_;
};

} // namespace

template <class CoordinateType>
auto VectorIndexStorage<CoordinateType>::GetVector(
    const ReadOperationData& read_operation_data, VertexId id)
    -> Result<VectorIndexStorage<CoordinateType>::IndexedVector> {
  VectorIndexRead<CoordinateType> read(doc_db_, read_operation_data);
  return read.GetVector(id);
}

template <class CoordinateType>
auto VectorIndexStorage<CoordinateType>::GetNeighbors(
    const ReadOperationData& read_operation_data, VertexId id, VectorIndexLevel level)
    -> Result<VectorNodeNeighbors> {
  VectorIndexRead<CoordinateType> read(doc_db_, read_operation_data);
  return read.GetNeighbors(id, level);
}

template class VectorIndexStorage<float>;

}  // namespace yb::docdb
