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

#include "yb/docdb/docdb_test_base.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/vector_lsm.h"

#include "yb/dockv/doc_key.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/path_util.h"

#include "yb/vector/ann_methods.h"
#include "yb/vector/hnswlib_wrapper.h"
#include "yb/vector/usearch_wrapper.h"

using namespace std::literals;

namespace yb::docdb {

using FloatVectorLSM = VectorLSM<std::vector<float>, float>;
using UsearchIndexFactory = MakeChunkFactory<vectorindex::UsearchIndexFactory, FloatVectorLSM>;
using HnswlibIndexFactory = MakeChunkFactory<vectorindex::HnswlibIndexFactory, FloatVectorLSM>;

class VectorLSMKeyValueStorageRocksDbWrapper : public VectorLSMKeyValueStorage {
 public:
  VectorLSMKeyValueStorageRocksDbWrapper(
      rocksdb::DB* db, const rocksdb::WriteOptions& write_options, ColumnId column_id)
      : db_(db), write_options_(write_options), column_id_(column_id) {}

  Status StoreBaseTableKeys(const BaseTableKeysBatch& batch, HybridTime write_time) {
    rocksdb::WriteBatch write_batch;
    dockv::KeyBytes key_buffer;
    ValueBuffer value_buffer;
    IntraTxnWriteId write_id = 0;
    for (const auto& [vertex_id, base_table_key] : batch) {
      MakeVertexIdKey(vertex_id, key_buffer);
      DocHybridTime doc_hybrid_time(write_time, write_id++);
      key_buffer.AppendKeyEntryType(dockv::KeyEntryType::kHybridTime);
      doc_hybrid_time.AppendEncodedInDocDbFormat(key_buffer.mutable_data());

      value_buffer.Clear();
      value_buffer.PushBack(dockv::ValueEntryTypeAsChar::kString);
      value_buffer.Append(base_table_key);
      write_batch.Put(key_buffer.AsSlice(), value_buffer.AsSlice());
    }
    return db_->Write(write_options_, &write_batch);
  }

  Result<KeyBuffer> ReadBaseTableKey(vectorindex::VertexId vertex_id) {
    auto iterator = CreateRocksDBIterator(
        db_, &docdb::KeyBounds::kNoBounds, docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
        boost::none, rocksdb::kDefaultQueryId, nullptr, nullptr,
        rocksdb::CacheRestartBlockKeys::kFalse);
    dockv::KeyBytes key_bytes;
    MakeVertexIdKey(vertex_id, key_bytes);
    iterator.Seek(key_bytes.AsSlice());
    if (iterator.Valid()) {
      const auto& entry = iterator.Entry();
      if (entry.key.starts_with(key_bytes.AsSlice())) {
        auto value = entry.value;
        if (!value.TryConsumeByte(dockv::ValueEntryTypeAsChar::kString)) {
          return STATUS_FORMAT(
              Corruption, "Vertex $0 has invalid value: $1", vertex_id, value.ToDebugHexString());
        }
        return KeyBuffer(value);
      }
    }
    return STATUS_FORMAT(NotFound, "Vertex id not found: $0", vertex_id);
  }

 private:
  void MakeVertexIdKey(vectorindex::VertexId vertex_id, dockv::KeyBytes& key_buffer) {
    key_buffer.Clear();
    key_buffer.AppendKeyEntryType(dockv::KeyEntryType::kColumnId);
    key_buffer.AppendColumnId(column_id_);
    key_buffer.AppendKeyEntryType(dockv::KeyEntryType::kVertexId);
    key_buffer.AppendUInt64(vertex_id);
    key_buffer.AppendKeyEntryType(dockv::KeyEntryType::kGroupEnd);
  }

  rocksdb::DB* db_;
  const rocksdb::WriteOptions& write_options_;
  const ColumnId column_id_;
};

class VectorLSMTest : public DocDBTestBase,
                      public testing::WithParamInterface<vectorindex::ANNMethodKind> {
 protected:
  VectorLSMTest()
      : insert_thread_pool_(rpc::ThreadPoolOptions {
          .name = "Insert Thread Pool",
          .max_workers = 10,
        }) {
  }

  void SetUp() override {
    DocDBTestBase::SetUp();
    key_value_storage_.emplace(rocksdb(), write_options(), ColumnId(42));
  }

  Schema CreateSchema() override {
    return Schema();
  }

  rpc::ThreadPool insert_thread_pool_;
  std::optional<VectorLSMKeyValueStorageRocksDbWrapper> key_value_storage_;
};

std::string VertexKey(vectorindex::VertexId vertex_id) {
  return Format("vertex_$0", vertex_id);
}

auto ChunkFactory(vectorindex::ANNMethodKind ann_method) {
  switch (ann_method) {
    case vectorindex::ANNMethodKind::kUsearch:
      return UsearchIndexFactory::Create;
    case vectorindex::ANNMethodKind::kHnswlib:
      return HnswlibIndexFactory::Create;
  }
  return decltype(&UsearchIndexFactory::Create)(nullptr);
}

TEST_P(VectorLSMTest, Simple) {
  constexpr int kDimensions = 4;

  FloatVectorLSM lsm;
  {
    FloatVectorLSM::Options options = {
      .storage_dir = JoinPathSegments(rocksdb_dir_, "vector_lsm"),
      .chunk_factory = [factory = ChunkFactory(GetParam())]() {
          vectorindex::HNSWOptions hnsw_options = {
            .dimensions = kDimensions,
          };
          return factory(hnsw_options);
        },
      .points_per_chunk = 1000,
      .key_value_storage = &*key_value_storage_,
      .insert_thread_pool = &insert_thread_pool_,
    };
    ASSERT_OK(lsm.Open(std::move(options)));
  }
  {
    FloatVectorLSM::InsertEntries entries;
    for (vectorindex::VertexId i = 1; i <= (1 << kDimensions); ++i) {
      auto bits = i - 1;
      FloatVector vector(kDimensions);
      for (int d = 0; d != kDimensions; ++d) {
        vector[d] = 1.f * ((bits >> d) & 1);
      }
      entries.emplace_back(FloatVectorLSM::InsertEntry {
        .vertex_id = i,
        .base_table_key = KeyBuffer(Slice(VertexKey(i))),
        .vector = std::move(vector),
      });
    }
    HybridTime write_time(1000, 0);
    ASSERT_OK(lsm.Insert(entries, write_time));
  }

  bool stop = false;
  FloatVectorLSM::Vector query_vector(kDimensions, 0.f);
  while (!stop) {
    stop = !lsm.TEST_HasBackgroundInserts();

    FloatVectorLSM::SearchOptions options = {
      .max_num_results = kDimensions + 1,
    };
    auto search_result = ASSERT_RESULT(lsm.Search(query_vector, options));

    ASSERT_EQ(search_result.size(), options.max_num_results);

    ASSERT_EQ(search_result[0].distance, 0);
    ASSERT_EQ(search_result[0].base_table_key.AsSlice().ToBuffer(), VertexKey(1));

    LOG(INFO) << "Search result: " << AsString(search_result);

    std::sort(search_result.begin(), search_result.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.base_table_key < rhs.base_table_key;
    });
    for (int d = 0; d != kDimensions; ++d) {
      ASSERT_EQ(search_result[d + 1].distance, 1);
      ASSERT_EQ(search_result[d + 1].base_table_key.AsSlice().ToBuffer(), VertexKey(1 + (1 << d)));
    }
  }
}

TEST_F(VectorLSMTest, MergeChunkResults) {
  using ChunkResults = std::vector<vectorindex::VertexWithDistance<float>>;
  ChunkResults a_src = {{5, 1}, {3, 3}, {1, 5}, {7, 7}};
  ChunkResults b_src = {{2, 2}, {3, 3}, {4, 4}, {9, 7}, {7, 7}};
  for (size_t i = 1; i != a_src.size() + b_src.size(); ++i) {
    auto a = a_src;
    auto b = b_src;
    MergeChunkResults(a, b, i);
    auto sum = a_src;
    sum.insert(sum.end(), b_src.begin(), b_src.end());
    std::sort(sum.begin(), sum.end());
    sum.erase(std::unique(sum.begin(), sum.end()), sum.end());
    sum.resize(std::min(i, sum.size()));
    ASSERT_EQ(a, sum);
  }
}

std::string ANNMethodKindToString(
    const testing::TestParamInfo<vectorindex::ANNMethodKind>& param_info) {
  return AsString(param_info.param);
}

INSTANTIATE_TEST_SUITE_P(
    ANNMethodKind, VectorLSMTest,
    ::testing::ValuesIn(vectorindex::kANNMethodKindArray),
    ANNMethodKindToString);

}  // namespace yb::docdb
