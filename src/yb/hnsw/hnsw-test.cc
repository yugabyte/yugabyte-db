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
#include "yb/hnsw/hnsw_block_cache.h"

#include "yb/util/random_util.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"

namespace yb::hnsw {

using IndexImpl = unum::usearch::index_dense_gt<vector_index::VectorId>;
using Vector = std::vector<float>;

unum::usearch::index_dense_config_t CreateIndexDenseConfig() {
  unum::usearch::index_dense_config_t config;
  config.connectivity = 64;
  config.connectivity_base = 128;
  config.expansion_add = 128;
  config.expansion_search = 64;
  return config;
}

struct AcceptAllVectors {
  bool operator()(const vector_index::VectorId& id) const {
    return true;
  }
};

class YbHnswTest : public YBTest {
 protected:
  YbHnswTest() : yb_hnsw_(metric_) {}

  void RandomVector(Vector& out) {
    out.clear();
    out.reserve(dimensions_);
    std::uniform_real_distribution<float> dist(0.0, 1.0);
    while (out.size() < dimensions_) {
      out.push_back(dist(rng_));
    }
  }

  Vector RandomVector() {
    Vector result;
    RandomVector(result);
    return result;
  }

  void InsertRandomVector(Vector& holder) {
    RandomVector(holder);
    ASSERT_TRUE(index_.add(vector_index::VectorId::GenerateRandom(), holder.data()));
  }

  void InsertRandomVectors(size_t count) {
    metric_ = unum::usearch::metric_punned_t(
        dimensions_, unum::usearch::metric_kind_t::l2sq_k, unum::usearch::scalar_kind_t::f32_k);
    index_ = IndexImpl::make( metric_, CreateIndexDenseConfig());
    auto rounded_num_vectors = unum::usearch::ceil2(max_vectors_);
    index_.reserve(unum::usearch::index_limits_t(rounded_num_vectors * 2 / 3, 16));

    Vector holder;
    for (size_t i = 0; i != count; ++i) {
      InsertRandomVector(holder);
    }
  }

  void VerifySearch(const Vector& query_vector, size_t max_results) {
    vector_index::VectorFilter filter = AcceptAllVectors();
    auto usearch_results = index_.filtered_search(query_vector.data(), max_results, filter);
    auto yb_hnsw_results = yb_hnsw_.Search(query_vector.data(), max_results, filter, context_);
    ASSERT_EQ(usearch_results.count, yb_hnsw_results.size());
    for (size_t j = 0; j != usearch_results.count; ++j) {
      std::decay_t<decltype(yb_hnsw_results.front())> expected(
          usearch_results[j].member.key, usearch_results[j].distance);
      ASSERT_EQ(AsString(expected), AsString(yb_hnsw_results[j]));
    }
  }

  std::vector<Vector> PrepareRandom(size_t num_vectors, size_t num_searches);
  Status InitYbHnsw(bool load);

  void TestPerf();
  void TestSimple(bool load);

  size_t dimensions_ = 8;
  size_t max_vectors_ = 65536;
  std::mt19937_64 rng_{42};
  unum::usearch::metric_punned_t metric_;
  IndexImpl index_;
  BlockCachePtr block_cache_ = std::make_shared<BlockCache>(*Env::Default());
  YbHnsw yb_hnsw_;
  YbHnswSearchContext context_;
};

Status YbHnswTest::InitYbHnsw(bool load) {
  auto path = GetTestPath("0.yb_hnsw");
  if (load) {
    {
      YbHnsw temp(metric_);
      RETURN_NOT_OK(temp.Import(index_, path, block_cache_));
    }
    RETURN_NOT_OK(yb_hnsw_.Init(path, block_cache_));
  } else {
    RETURN_NOT_OK(yb_hnsw_.Import(index_, path, block_cache_));
  }
  return Status::OK();
}

void YbHnswTest::TestSimple(bool load) {
  constexpr size_t kNumVectors = 100;
  constexpr size_t kNumSearches = 10;
  constexpr size_t kMaxResults = 10;

  InsertRandomVectors(kNumVectors);
  ASSERT_OK(InitYbHnsw(load));

  Vector query_vector;
  for (size_t i = 0; i != kNumSearches; ++i) {
    RandomVector(query_vector);
    ASSERT_NO_FATALS(VerifySearch(query_vector, kMaxResults));
  }
}

TEST_F(YbHnswTest, Simple) {
  TestSimple(/* load= */ false);
}

TEST_F(YbHnswTest, Persistence) {
  TestSimple(/* load= */ true);
}

std::vector<Vector> YbHnswTest::PrepareRandom(size_t num_vectors, size_t num_searches) {
  EXPECT_LE(num_vectors, max_vectors_);
  InsertRandomVectors(num_vectors);
  EXPECT_OK(InitYbHnsw(false));

  std::vector<Vector> query_vectors(num_searches);
  for (auto& vector : query_vectors) {
    RandomVector(vector);
  }
  return query_vectors;
}

TEST_F(YbHnswTest, Random) {
  constexpr size_t kNumVectors = 16384;
  constexpr size_t kNumSearches = 1024;
  constexpr size_t kMaxResults = 20;

  auto query_vectors = PrepareRandom(kNumVectors, kNumSearches);

  for (const auto& query_vector : query_vectors) {
    ASSERT_NO_FATALS(VerifySearch(query_vector, kMaxResults));
  }
}

void YbHnswTest::TestPerf() {
  auto div = static_cast<size_t>(std::sqrt(dimensions_ / 8));
  size_t num_vectors = RegularBuildVsSanitizers(65536, 4096) / div;
  size_t num_searches = RegularBuildVsSanitizers(65536, 4096) / div;
  constexpr size_t kMaxResults = 20;

  max_vectors_ = num_vectors;

  auto query_vectors = PrepareRandom(num_vectors, num_searches);
  YbHnswSearchContext context;
  vector_index::VectorFilter filter = AcceptAllVectors();
  MonoTime start = MonoTime::Now();
  for (const auto& query_vector : query_vectors) {
    index_.filtered_search(query_vector.data(), kMaxResults, filter);
  }
  MonoTime mid = MonoTime::Now();
  for (const auto& query_vector : query_vectors) {
    yb_hnsw_.Search(query_vector.data(), kMaxResults, filter, context);
  }
  MonoTime finish = MonoTime::Now();
  auto usearch_time = mid - start;
  auto yb_hnsw_time = finish - mid;
  LOG(INFO) << "Num vectors: " << num_vectors << ", num searches: " << num_searches;

  LOG(INFO) << "Usearch time: " << usearch_time << ", YbHnsw time: " << yb_hnsw_time
            << ", rate: " << yb_hnsw_time.ToSeconds() / usearch_time.ToSeconds();
}

TEST_F(YbHnswTest, Perf8Dims) {
  TestPerf();
}

TEST_F(YbHnswTest, Perf128Dims) {
  dimensions_ = 128;
  TestPerf();
}

TEST_F(YbHnswTest, Perf2048Dims) {
  dimensions_ = 2048;
  TestPerf();
}

}  // namespace yb::hnsw
