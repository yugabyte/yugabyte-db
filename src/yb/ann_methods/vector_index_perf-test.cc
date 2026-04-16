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

#include "yb/hnsw/hnsw.h"
#include "yb/hnsw/vector_index_test_base.h"

#include "yb/ann_methods/hnswlib_wrapper.h"
#include "yb/ann_methods/usearch_wrapper.h"

#include "yb/util/size_literals.h"

namespace yb::ann_methods {

using Vector = hnsw::Vector;
using DistanceResult = float;
using VectorIndexPtr = vector_index::VectorIndexIfPtr<Vector, DistanceResult>;

class VectorIndexPerfTest : public hnsw::VectorIndexTestBase {
 protected:
  void TestPerf();

  size_t BlockCacheCapacity() override {
    return 1_GB;
  }

  std::vector<Vector> PrepareRandom(size_t num_vectors, size_t num_searches) {
    EXPECT_LE(num_vectors, max_vectors_);
    InsertRandomVectors(num_vectors);

    return RandomVectors(num_searches);
  }

  void InsertRandomVectors(size_t count) {
    vector_index::HNSWOptions options = {
      .dimensions = dimensions_,
    };
    indexes_.emplace_back(
        "usearch",
        UsearchIndexFactory<Vector, DistanceResult>::Create(
            vector_index::FactoryMode::kCreate, block_cache_, options,
            HnswBackend::YB_HNSW, mem_tracker_));
    indexes_.emplace_back(
        "hnswlib",
        HnswlibIndexFactory<Vector, DistanceResult>::Create(
            vector_index::FactoryMode::kCreate, options));
    for (const auto& [_, index] : indexes_) {
      ASSERT_OK(index->Reserve(count, 1, 1));
    }

    Vector holder;
    for (size_t i = 0; i != count; ++i) {
      InsertRandomVector(holder);
    }

    indexes_.emplace_back(
        "yb_hnsw",
        ASSERT_RESULT(indexes_.front().second->SaveToFile(GetTestPath("0.yb_hnsw"))));
  }

  void InsertRandomVector(Vector& holder) {
    RandomVector(holder);
    auto uuid = vector_index::VectorId::GenerateRandom();
    for (const auto& [_, index] : indexes_) {
      ASSERT_OK(index->Insert(uuid, holder));
    }
  }

  std::vector<std::pair<std::string, VectorIndexPtr>> indexes_;
  MemTrackerPtr mem_tracker_ = MemTracker::GetRootTracker()->FindOrCreateTracker(1_GB, "usearch");
};

void VectorIndexPerfTest::TestPerf() {
  auto div = static_cast<size_t>(std::sqrt(dimensions_ / 8));
  size_t num_vectors = RegularBuildVsDebugVsSanitizers(65536, 8192, 4096) / div;
  size_t num_searches = RegularBuildVsDebugVsSanitizers(65536, 8192, 4096) / div;
  constexpr size_t kMaxResults = 20;

  max_vectors_ = num_vectors;

  auto query_vectors = PrepareRandom(num_vectors, num_searches);
  LOG(INFO) << "Num vectors: " << num_vectors << ", num searches: " << num_searches;
  std::vector<MonoDelta> times;
  auto search_options = vector_index::SearchOptions {
    .max_num_results = kMaxResults,
    .ef = 128,
  };
  for (const auto& [name, index] : indexes_) {
    auto start = MonoTime::Now();
    for (const auto& query_vector : query_vectors) {
      ASSERT_OK(index->Search(query_vector, search_options));
    }
    auto time = MonoTime::Now() - start;
    LOG(INFO) << name << " time: " << time;
    times.push_back(time);
  }

  for (size_t i = 0; i < indexes_.size(); ++i) {
    for (size_t j = i + 1; j < indexes_.size(); ++j) {
      LOG(INFO) << indexes_[j].first << "/" << indexes_[i].first << " rate: "
                << times[j].ToSeconds() / times[i].ToSeconds();
    }
  }
}

TEST_F(VectorIndexPerfTest, Perf8Dims) {
  TestPerf();
}

TEST_F(VectorIndexPerfTest, Perf128Dims) {
  dimensions_ = 128;
  TestPerf();
}

TEST_F(VectorIndexPerfTest, Perf2048Dims) {
  dimensions_ = 2048;
  TestPerf();
}

} // namespace yb::ann_methods
