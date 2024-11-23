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
#include "yb/vector_index/vector_index_if.h"
#include "yb/vector_index/vectorann_util.h"

namespace yb::vector_index {

// Test fixture class for Merge operation.
class HnswlibIndexMergeTest : public YBTest {
 protected:
  VectorIndexIfPtr<FloatVector, float> CreateAndFillIndex(size_t first_id, size_t num_entries) {
    auto result = index_factory_();
    CHECK_OK(result->Reserve(num_entries, 0, 0));
    for (size_t id = first_id; id != first_id + num_entries; ++id) {
      CHECK_OK(result->Insert(id, all_vectors_[id % 4]));
    }
    return result;
  }

  // Helper function to verify that all expected vertex_ids are in the search results.
  void VerifyExpectedVertexIds(const VectorIndexReaderIf<FloatVector, float>::SearchResult& results,
                               std::set<VertexId> expected_ids) {
    for (const auto& result : results) {
      ASSERT_TRUE(expected_ids.find(result.vertex_id) != expected_ids.end());
      expected_ids.erase(result.vertex_id); // Remove found ID from the set.
    }
    ASSERT_TRUE(expected_ids.empty()); // Verify all expected IDs were found.
  }

  void SetUp() override {
    // HNSW options setup with 3 dimensions and L2 distance.
    index_factory_ = []() -> VectorIndexIfPtr<FloatVector, float> {
      HNSWOptions hnsw_options = {
          .dimensions = 3,
          .max_neighbors_per_vertex = 16,
          .ef_construction = 20,
          .distance_kind = DistanceKind::kL2Squared};
      return HnswlibIndexFactory<FloatVector, float>::Create(hnsw_options);
    };
    index_a_ = CreateAndFillIndex(0, 2);
    index_b_ = CreateAndFillIndex(2, 2);
  }

  std::vector<float> all_vectors_[4] = {
    {0.1f, 0.2f, 0.3f},
    {0.4f, 0.5f, 0.6f},
    {0.7f, 0.8f, 0.9f},
    {1.0f, 1.1f, 1.2f}
  };

  VectorIndexFactory<FloatVector, float> index_factory_;
  VectorIndexIfPtr<FloatVector, float> index_a_;
  VectorIndexIfPtr<FloatVector, float> index_b_;
};

// Test case to verify the Merge method for HnswlibIndex.
TEST_F(HnswlibIndexMergeTest, TestMergeIndices) {
  // Perform merge operation.
  VectorIndexIfPtr<FloatVector, float> merged_index =
      Merge(index_factory_, index_a_, index_b_);

  // Check that the merged index contains all entries.
  auto result_a = ASSERT_RESULT(merged_index->Search(all_vectors_[0], 1));
  ASSERT_EQ(result_a.size(), 1);
  ASSERT_EQ(result_a[0].vertex_id, 0);

  auto result_b = ASSERT_RESULT(merged_index->Search(all_vectors_[2], 1));
  ASSERT_EQ(result_b.size(), 1);
  ASSERT_EQ(result_b[0].vertex_id, 2);

  // Verify the size of the merged index.
  auto all_results = ASSERT_RESULT(merged_index->Search(
      {0.0f, 0.0f, 0.0f}, 10)); // Assuming a query that fetches all.
  ASSERT_EQ(all_results.size(), 4); // Should contain all 4 entries.

  // Check that all expected vertex_ids are in the results.
  VerifyExpectedVertexIds(all_results, {0, 1, 2, 3});
}

// Test case to verify merging an empty index with a non-empty one.
TEST_F(HnswlibIndexMergeTest, TestMergeWithEmptyIndex) {
  // Create an empty index with the same options.
  VectorIndexIfPtr<FloatVector, float> empty_index = index_factory_();

  CHECK_OK(empty_index->Reserve(10, 0, 0));

  // Merge empty_index into index_a.
  VectorIndexIfPtr<FloatVector, float> merged_index =
      Merge(index_factory_, index_a_, empty_index);

  // Check that the merged index contains only the entries from index_a.
  auto all_results = ASSERT_RESULT(merged_index->Search(
      {0.0f, 0.0f, 0.0f}, 10));  // Query that fetches all.
  ASSERT_EQ(all_results.size(), 2);  // Should contain only the 2 entries from index_a.
  // Check that all expected vertex_ids are in the results.
  VerifyExpectedVertexIds(all_results, {0, 1});
}

}  // namespace yb::vector_index
