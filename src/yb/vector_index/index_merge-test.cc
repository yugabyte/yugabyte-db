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

#include <thread>

#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

#include "yb/vector_index/hnswlib_wrapper.h"
#include "yb/vector_index/usearch_wrapper.h"
#include "yb/vector_index/vector_index_if.h"
#include "yb/vector_index/vectorann_util.h"

namespace yb::vector_index {

namespace {

template <class Container, class... Containers>
void MergeValuesImpl(std::set<typename std::decay_t<Container>::value_type>& out,
                     Container&& container, Containers&&... containers) {
  out.insert(container.begin(), container.end());
  if constexpr (sizeof...(Containers) > 0) {
    MergeValuesImpl(out, std::forward<Containers>(containers)...);
  }
}

template <class Container, class... Containers>
auto MergeValues(Container&& container, Containers&&... containers) {
  std::set<typename std::decay_t<Container>::value_type> out;
  MergeValuesImpl(out, std::forward<Container>(container), std::forward<Containers>(containers)...);
  return out;
}

} // namespace

// Test fixture class for Merge operation.
class IndexMergeTest : public YBTest {
 protected:
  struct IndexData {
    VectorIndexIfPtr<FloatVector, float> index;
    std::vector<VectorId> vector_ids;
    const size_t num_vectors() const { return vector_ids.size(); }
  };

  IndexData CreateAndFillIndex(
      VectorIndexFactory<FloatVector, float> index_factory, size_t first_id, size_t num_entries) {
    auto index = index_factory();
    CHECK_OK(index->Reserve(num_entries, 1, 1));

    std::vector<VectorId> ids;
    ids.reserve(num_entries);
    for (size_t id = first_id; id < first_id + num_entries; ++id) {
      ids.emplace_back(VectorId::GenerateRandom());
      CHECK_OK(index->Insert(ids.back(), input_vectors_[id % input_vectors_.size()]));
    }

    return {std::move(index), std::move(ids)};
  }

  // Helper function to verify that all expected vertex_ids are in the search results.
  void VerifyExpectedVertexIds(const VectorIndexReaderIf<FloatVector, float>::SearchResult& results,
                               std::set<VectorId>&& expected_ids) {
    for (const auto& result : results) {
      ASSERT_TRUE(expected_ids.find(result.vector_id) != expected_ids.end());
      expected_ids.erase(result.vector_id); // Remove found ID from the set.
    }
    ASSERT_TRUE(expected_ids.empty()); // Verify all expected IDs were found.
  }

  void TestMergeIndices(VectorIndexFactory<FloatVector, float> index_factory) {
      // Generate indexes for the input set.
    const auto half_size = input_vectors_.size() / 2;
    auto data_a = CreateAndFillIndex(index_factory, 0, half_size);
    auto data_b = CreateAndFillIndex(index_factory, half_size, half_size);

    VectorIndexIfPtr<FloatVector, float> merged_index =
      ASSERT_RESULT(Merge(index_factory, {data_a.index, data_b.index}));

    // Check that the merged index contains all entries.
    auto result_a = ASSERT_RESULT(merged_index->Search(
        input_vectors_[0], {.max_num_results = 1, .ef = 0}));
    ASSERT_EQ(result_a.size(), 1);
    ASSERT_EQ(result_a[0].vector_id, data_a.vector_ids[0]);

    auto result_b = ASSERT_RESULT(merged_index->Search(
        input_vectors_[half_size], {.max_num_results = 1, .ef = 0}));
    ASSERT_EQ(result_b.size(), 1);
    ASSERT_EQ(result_b[0].vector_id, data_b.vector_ids[0]);

    // Verify the size of the merged index.
    auto all_results = ASSERT_RESULT(merged_index->Search(
        {0.0f, 0.0f, 0.0f}, {.max_num_results = 10, .ef = 0})); // Query that fetches all.
    ASSERT_EQ(all_results.size(), data_a.num_vectors() + data_b.num_vectors());

    // Check that all expected vertex_ids are in the results.
    VerifyExpectedVertexIds(all_results, MergeValues(data_a.vector_ids, data_b.vector_ids));
  }

  void TestMergeWithEmptyIndex(VectorIndexFactory<FloatVector, float> index_factory) {
    // Create an empty index with the same options.
    VectorIndexIfPtr<FloatVector, float> empty_index = index_factory();
    CHECK_OK(empty_index->Reserve(10, 0, 0));

    // Generate indexes for the input set.
    auto data_a = CreateAndFillIndex(index_factory, 0, input_vectors_.size() / 2);

    // Merge empty_index with data_a.
    auto merged_index = ASSERT_RESULT(Merge(index_factory, {data_a.index, empty_index}));

    // Check that the merged index contains only the entries from data_a.
    auto all_results = ASSERT_RESULT(merged_index->Search(
        {0.0f, 0.0f, 0.0f}, {.max_num_results = 10, .ef = 0})); // Query that fetches all.
    ASSERT_EQ(all_results.size(), data_a.num_vectors());

    // Check that all expected vector ids are in the results.
    VerifyExpectedVertexIds(all_results, MergeValues(data_a.vector_ids));
  }

  void TestGetVector(VectorIndexFactory<FloatVector, float> index_factory) {
    auto data = CreateAndFillIndex(index_factory, 0, input_vectors_.size());
    for (size_t i = 0; i < input_vectors_.size(); ++i) {
      auto vector = ASSERT_RESULT(data.index->GetVector(data.vector_ids[i]));
      ASSERT_EQ(vector, input_vectors_[i]);
    }
  }

  void SetUp() override {
    const HNSWOptions hnsw_options = {
          .dimensions = 3,
          .max_neighbors_per_vertex = 15,
          .ef_construction = 20,
          .distance_kind = DistanceKind::kL2Squared};

    hnswlib_index_factory_ = [hnsw_options]() -> VectorIndexIfPtr<FloatVector, float> {
      return HnswlibIndexFactory<FloatVector, float>::Create(hnsw_options);
    };
    usearch_index_factory_ = [hnsw_options]() ->  VectorIndexIfPtr<FloatVector, float> {
      return UsearchIndexFactory<FloatVector, float>::Create(hnsw_options);
    };
  }

  const std::vector<std::vector<float>> input_vectors_ = {
    {0.1f, 0.2f, 0.3f},
    {0.4f, 0.5f, 0.6f},
    {0.7f, 0.8f, 0.9f},
    {1.0f, 1.1f, 1.2f}
  };

  VectorIndexFactory<FloatVector, float> hnswlib_index_factory_, usearch_index_factory_;
};

TEST_F(IndexMergeTest, TestMergeHnswlibIndices) {
  TestMergeIndices(hnswlib_index_factory_);
}

TEST_F(IndexMergeTest, TestMergeUsearchIndices) {
  TestMergeIndices(usearch_index_factory_);
}

// Test case to verify merging an empty index with a non-empty one.
TEST_F(IndexMergeTest, TestMergeWithEmptyHnswlibIndex) {
  TestMergeWithEmptyIndex(hnswlib_index_factory_);
}

TEST_F(IndexMergeTest, TestMergeUsearchWithEmptyUsearchIndex) {
  TestMergeWithEmptyIndex(usearch_index_factory_);
}

TEST_F(IndexMergeTest, TestGetVectorUsearchIndex) {
  TestGetVector(usearch_index_factory_);
}

}  // namespace yb::vector_index
