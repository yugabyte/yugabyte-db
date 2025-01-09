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

#include <google/protobuf/any.pb.h>

#include "yb/rocksdb/metadata.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/path_util.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/ann_methods.h"
#include "yb/vector_index/hnswlib_wrapper.h"
#include "yb/vector_index/usearch_wrapper.h"
#include "yb/vector_index/vector_lsm.h"
#include "yb/vector_index/vector_lsm-test.pb.h"

using namespace std::literals;

DECLARE_uint64(TEST_vector_index_delay_saving_first_chunk_ms);
DECLARE_bool(TEST_vector_index_skip_update_metadata_during_shutdown);

namespace yb::vector_index {

using FloatVectorLSM = VectorLSM<std::vector<float>, float>;
using TestUsearchIndexFactory = MakeVectorIndexFactory<UsearchIndexFactory, FloatVectorLSM>;
using TestHnswlibIndexFactory = MakeVectorIndexFactory<HnswlibIndexFactory, FloatVectorLSM>;

class SimpleVectorLSMKeyValueStorage : public VectorLSMKeyValueStorage {
 public:
  SimpleVectorLSMKeyValueStorage() = default;

  Status StoreBaseTableKeys(const BaseTableKeysBatch& batch, const VectorLSMInsertContext&) {
    for (const auto& [vertex_id, base_table_key] : batch) {
      storage_.emplace(vertex_id, KeyBuffer(base_table_key));
    }
    return Status::OK();
  }

  Result<KeyBuffer> ReadBaseTableKey(VectorId vertex_id) {
    auto it = storage_.find(vertex_id);
    if (it == storage_.end()) {
      return STATUS_FORMAT(NotFound, "Vertex id not found: $0", vertex_id);
    }
    return it->second;
  }

 private:
  std::unordered_map<VectorId, KeyBuffer> storage_;
};

class TestFrontier : public rocksdb::UserFrontier {
 public:
  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<TestFrontier>(*this);
  }

  std::string ToString() const override {
    return YB_CLASS_TO_STRING(vertex_id);
  }

  void ToPB(google::protobuf::Any* any) const override {
    VectorLSMTestFrontierPB pb;
    pb.set_vertex_id(vertex_id_.data(), vertex_id_.size());
    any->PackFrom(pb);
  }

  bool Equals(const UserFrontier& pre_rhs) const override {
    const auto& lhs = *this;
    const auto& rhs = down_cast<const TestFrontier&>(pre_rhs);
    return YB_CLASS_EQUALS(vertex_id);
  }

  void Update(const UserFrontier& pre_rhs, rocksdb::UpdateUserValueType update_type) override {
    const auto& rhs = down_cast<const TestFrontier&>(pre_rhs);
    switch (update_type) {
      case rocksdb::UpdateUserValueType::kLargest:
        vertex_id_ = std::max(vertex_id_, rhs.vertex_id_);
        return;
      case rocksdb::UpdateUserValueType::kSmallest:
        vertex_id_ = std::min(vertex_id_, rhs.vertex_id_);
        return;
    }
    FATAL_INVALID_ENUM_VALUE(rocksdb::UpdateUserValueType, update_type);
  }

  bool IsUpdateValid(const UserFrontier& rhs, rocksdb::UpdateUserValueType type) const override {
    return true;
  }

  Slice FilterAsSlice() override {
    return Slice();
  }

  void ResetFilter() override {
  }

  void FromOpIdPBDeprecated(const OpIdPB& op_id) override {
  }

  Status FromPB(const google::protobuf::Any& any) override {
    VectorLSMTestFrontierPB pb;
    if (!any.UnpackTo(&pb)) {
      return STATUS_FORMAT(Corruption, "Unpack test frontier failed");
    }

    vertex_id_ = VERIFY_RESULT(FullyDecodeVectorId(pb.vertex_id()));
    return Status::OK();
  }

  uint64_t GetHybridTimeAsUInt64() const override {
    return 0;
  }

  VectorId vertex_id() const {
    return vertex_id_;
  }

  void SetVertexId(VectorId vertex_id) {
    vertex_id_ = vertex_id;
  }

 private:
  VectorId vertex_id_;
};

using TestFrontiers = rocksdb::UserFrontiersBase<TestFrontier>;

class VectorLSMTest : public YBTest, public testing::WithParamInterface<ANNMethodKind> {
 protected:
  VectorLSMTest()
      : thread_pool_(rpc::ThreadPoolOptions {
          .name = "Insert Thread Pool",
          .max_workers = 10,
        }) {
  }

  Status InitVectorLSM(FloatVectorLSM& lsm, size_t dimensions, size_t points_per_chunk);

  Status OpenVectorLSM(FloatVectorLSM& lsm, size_t dimensions, size_t points_per_chunk);

  Status InsertCube(
      FloatVectorLSM& lsm, size_t dimensions,
      size_t block_size = std::numeric_limits<size_t>::max(),
      size_t min_entry_idx = 0);

  void VerifyVectorLSM(FloatVectorLSM& lsm, size_t dimensions);

  void CheckQueryVector(
      FloatVectorLSM& lsm, size_t dimensions, const FloatVectorLSM::Vector& query_vector,
      size_t max_num_results);

  void TestBootstrap(bool flush);

  rpc::ThreadPool thread_pool_;
  SimpleVectorLSMKeyValueStorage key_value_storage_;
  FloatVectorLSM::InsertEntries  inserted_entries_;
};

std::string VertexKey(size_t vertex_id) {
  return Format("vertex_$0", vertex_id);
}

auto GetVectorIndexFactory(ANNMethodKind ann_method) {
  switch (ann_method) {
    case ANNMethodKind::kUsearch:
      return TestUsearchIndexFactory::Create;
    case ANNMethodKind::kHnswlib:
      return TestHnswlibIndexFactory::Create;
  }
  return decltype(&TestUsearchIndexFactory::Create)(nullptr);
}

FloatVectorLSM::InsertEntries CubeInsertEntries(size_t dimensions) {
  FloatVectorLSM::InsertEntries result;
  for (size_t i = 1; i <= (1ULL << dimensions); ++i) {
    auto bits = i - 1;
    FloatVector vector(dimensions);
    for (size_t d = 0; d != dimensions; ++d) {
      vector[d] = 1.f * ((bits >> d) & 1);
    }
    result.emplace_back(FloatVectorLSM::InsertEntry {
      .vertex_id = VectorId::GenerateRandom(),
      .base_table_key = KeyBuffer(Slice(VertexKey(i))),
      .vector = std::move(vector),
    });
  }
  return result;
}

auto GenerateVectorIds(size_t num) {
  std::vector<VectorId> result;
  result.reserve(num);
  while (result.size() < num) {
    result.emplace_back(VectorId::GenerateRandom());
  }
  return result;
}

Status VectorLSMTest::InsertCube(
    FloatVectorLSM& lsm, size_t dimensions, size_t block_size,
    size_t min_entry_idx) {
  inserted_entries_ = CubeInsertEntries(dimensions);
  for (size_t i = 0; i < inserted_entries_.size(); i += block_size) {
    auto begin = inserted_entries_.begin() + i;
    auto end = inserted_entries_.begin() + std::min(i + block_size, inserted_entries_.size());
    if (i < min_entry_idx) {
      ptrdiff_t delta = min_entry_idx - i;
      if (delta >= end - begin) {
        continue;
      }
      begin += delta;
    }
    FloatVectorLSM::InsertEntries block_entries(begin, end);
    TestFrontiers frontiers;
    frontiers.Smallest().SetVertexId(block_entries.front().vertex_id);
    frontiers.Largest().SetVertexId(block_entries.front().vertex_id);
    RETURN_NOT_OK(lsm.Insert(block_entries, { .frontiers = &frontiers }));
  }
  return Status::OK();
}

Status VectorLSMTest::OpenVectorLSM(
    FloatVectorLSM& lsm, size_t dimensions, size_t points_per_chunk) {

  std::string test_dir;
  RETURN_NOT_OK(Env::Default()->GetTestDirectory(&test_dir));
  test_dir = JoinPathSegments(test_dir, "vector_lsm_test_" + Uuid::Generate().ToString());

  FloatVectorLSM::Options options = {
    .storage_dir = JoinPathSegments(test_dir, "vector_lsm"),
    .vector_index_factory = [factory = GetVectorIndexFactory(GetParam()), dimensions]() {
        HNSWOptions hnsw_options = {
          .dimensions = dimensions,
        };
        return factory(hnsw_options);
      },
    .points_per_chunk = points_per_chunk,
    .key_value_storage = &key_value_storage_,
    .thread_pool = &thread_pool_,
    .frontiers_factory = [] { return std::make_unique<TestFrontiers>(); },
  };
  return lsm.Open(std::move(options));
}

Status VectorLSMTest::InitVectorLSM(
    FloatVectorLSM& lsm, size_t dimensions, size_t points_per_chunk) {
  RETURN_NOT_OK(OpenVectorLSM(lsm, dimensions, points_per_chunk));
  return InsertCube(lsm, dimensions, points_per_chunk);
}

void VectorLSMTest::VerifyVectorLSM(FloatVectorLSM& lsm, size_t dimensions) {
  CheckQueryVector(lsm, dimensions, FloatVectorLSM::Vector(dimensions, 0.f), dimensions + 1);
  CheckQueryVector(lsm, dimensions, FloatVectorLSM::Vector(dimensions, 1.f), dimensions + 1);
}

void VectorLSMTest::CheckQueryVector(
    FloatVectorLSM& lsm, size_t dimensions, const FloatVectorLSM::Vector& query_vector,
    size_t max_num_results) {
  bool stop = false;

  FloatVectorLSM::SearchResults expected_results;
  for (const auto& entry : inserted_entries_) {
    expected_results.push_back({
      .distance = lsm.Distance(query_vector, entry.vector),
      .base_table_key = entry.base_table_key,
    });
  }
  auto less_condition = [](const auto& lhs, const auto& rhs) {
    return lhs.distance == rhs.distance ? lhs.base_table_key < rhs.base_table_key
                                        : lhs.distance < rhs.distance;
  };
  std::sort(expected_results.begin(), expected_results.end(), less_condition);

  expected_results.resize(std::min(expected_results.size(), max_num_results));

  while (!stop) {
    stop = !lsm.TEST_HasBackgroundInserts();

    SearchOptions options = {
      .max_num_results = max_num_results,
    };
    auto search_result = ASSERT_RESULT(lsm.Search(query_vector, options));
    LOG(INFO) << "Search result: " << AsString(search_result);

    ASSERT_EQ(search_result.size(), expected_results.size());

    std::sort(search_result.begin(), search_result.end(), less_condition);

    for (size_t i = 0; i != expected_results.size(); ++i) {
      ASSERT_EQ(search_result[i].distance, expected_results[i].distance);
      ASSERT_EQ(search_result[i].base_table_key, expected_results[i].base_table_key);
    }
  }
}

TEST_P(VectorLSMTest, Simple) {
  constexpr size_t kDimensions = 4;

  FloatVectorLSM lsm;
  ASSERT_OK(InitVectorLSM(lsm, kDimensions, 1000));

  VerifyVectorLSM(lsm, kDimensions);
}

TEST_P(VectorLSMTest, MultipleChunks) {
  constexpr size_t kDimensions = 4;
  constexpr size_t kChunkSize = 4;

  FloatVectorLSM lsm;
  ASSERT_OK(InitVectorLSM(lsm, kDimensions, kChunkSize));

  VerifyVectorLSM(lsm, kDimensions);
}

void VectorLSMTest::TestBootstrap(bool flush) {
  constexpr size_t kDimensions = 4;
  constexpr size_t kChunkSize = 4;

  {
    FloatVectorLSM lsm;
    ASSERT_OK(InitVectorLSM(lsm, kDimensions, kChunkSize));
    if (flush) {
      ASSERT_OK(lsm.Flush(true));
    }
  }

  {
    FloatVectorLSM lsm;
    ASSERT_OK(OpenVectorLSM(lsm, kDimensions, kChunkSize));
    auto frontier_ptr = lsm.GetFlushedFrontier();

    // Find entry idx by frontier's vertex id, inserted on the first step (InitVectorLSM).
    size_t frontier_entry_idx = 0;
    if (frontier_ptr) {
      const auto frontier_vertex_id = down_cast<TestFrontier*>(frontier_ptr.get())->vertex_id();
      for (; frontier_entry_idx < inserted_entries_.size(); ++frontier_entry_idx) {
        if (inserted_entries_[frontier_entry_idx].vertex_id == frontier_vertex_id) {
          break;
        }
      }
      ASSERT_LT(frontier_entry_idx, inserted_entries_.size());
    }
    ASSERT_OK(InsertCube(lsm, kDimensions, kChunkSize, frontier_entry_idx));

    VerifyVectorLSM(lsm, kDimensions);
  }
}

TEST_P(VectorLSMTest, Bootstrap) {
  TestBootstrap(/* flush= */ false);
}

TEST_P(VectorLSMTest, BootstrapWithFlush) {
  TestBootstrap(/* flush= */ true);
}

TEST_P(VectorLSMTest, NotSavedChunk) {
  FLAGS_TEST_vector_index_delay_saving_first_chunk_ms = 1000 * kTimeMultiplier;
  FLAGS_TEST_vector_index_skip_update_metadata_during_shutdown = true;
  TestBootstrap(/* flush= */ false);
}

TEST_F(VectorLSMTest, MergeChunkResults) {
  const auto kIds = GenerateVectorIds(7);

  using ChunkResults = std::vector<VertexWithDistance<float>>;
  ChunkResults a_src = {{kIds[4], 1}, {kIds[2], 3}, {kIds[0], 5}, {kIds[5], 7}};
  ChunkResults b_src = {{kIds[1], 2}, {kIds[2], 3}, {kIds[3], 4}, {kIds[6], 7}, {kIds[5], 7}};
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
    const testing::TestParamInfo<ANNMethodKind>& param_info) {
  return AsString(param_info.param);
}

INSTANTIATE_TEST_SUITE_P(
    , VectorLSMTest, ::testing::ValuesIn(kANNMethodKindArray), ANNMethodKindToString);

}  // namespace yb::vector_index
