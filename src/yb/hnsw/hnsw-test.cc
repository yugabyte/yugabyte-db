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

#include "yb/rocksdb/cache.h"

#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"

using namespace std::chrono_literals;
using namespace yb::size_literals;

METRIC_DEFINE_entity(table);

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

  void VerifySearch(
      const Vector& query_vector, size_t max_results, YbHnswSearchContext* context = nullptr) {
    if (!context) {
      context = &context_;
    }
    vector_index::VectorFilter filter = AcceptAllVectors();
    auto usearch_results = index_.filtered_search(query_vector.data(), max_results, filter);
    auto yb_hnsw_results = yb_hnsw_.Search(query_vector.data(), max_results, filter, *context);
    ASSERT_EQ(usearch_results.count, yb_hnsw_results.size());
    for (size_t j = 0; j != usearch_results.count; ++j) {
      std::decay_t<decltype(yb_hnsw_results.front())> expected(
          usearch_results[j].member.key, usearch_results[j].distance);
      ASSERT_EQ(AsString(expected), AsString(yb_hnsw_results[j]));
    }
  }

  std::vector<Vector> PrepareRandom(bool load, size_t num_vectors, size_t num_searches);
  Status InitYbHnsw(bool load);

  void TestPerf();
  void TestSimple(bool load);
  void TestRandom(bool load, size_t background_threads);

  size_t dimensions_ = 8;
  size_t max_vectors_ = 65536;
  std::mt19937_64 rng_{42};
  unum::usearch::metric_punned_t metric_;
  IndexImpl index_;
  std::unique_ptr<MetricRegistry> metric_registry_ = std::make_unique<MetricRegistry>();
  MetricEntityPtr metric_entity_ = METRIC_ENTITY_table.Instantiate(metric_registry_.get(), "test");
  BlockCachePtr block_cache_ = std::make_shared<BlockCache>(
      *Env::Default(),
      MemTracker::GetRootTracker()->FindOrCreateTracker(1_GB, "block_cache"),
      metric_entity_,
      8_MB,
      4);
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

std::vector<Vector> YbHnswTest::PrepareRandom(
    bool load, size_t num_vectors, size_t num_searches) {
  EXPECT_LE(num_vectors, max_vectors_);
  InsertRandomVectors(num_vectors);
  EXPECT_OK(InitYbHnsw(load));

  std::vector<Vector> query_vectors(num_searches);
  for (auto& vector : query_vectors) {
    RandomVector(vector);
  }
  return query_vectors;
}

void YbHnswTest::TestRandom(bool load, size_t background_threads = 0) {
  constexpr size_t kNumVectors = 65535;
  constexpr size_t kNumSearches = 1024;
  constexpr size_t kMaxResults = 20;

  auto query_vectors = PrepareRandom(load, kNumVectors, kNumSearches);

  if (background_threads) {
    ThreadHolder threads;
    for (size_t i = 0; i < background_threads; ++i) {
      threads.AddThread([this, &stop = threads.stop_flag(), &query_vectors] {
        YbHnswSearchContext context;
        while (!stop.load()) {
          size_t index = RandomUniformInt<size_t>(0, query_vectors.size() - 1);
          ASSERT_NO_FATALS(VerifySearch(query_vectors[index], kMaxResults, &context));
        }
      });
    }
    threads.WaitAndStop(10s);
  } else {
    for (const auto& query_vector : query_vectors) {
      ASSERT_NO_FATALS(VerifySearch(query_vector, kMaxResults));
    }
  }

  LOG(INFO) << "Hit: " << block_cache_->metrics().hit->value();
  LOG(INFO) << "Queries: " << block_cache_->metrics().query->value();
  LOG(INFO) << "Read bytes: " << block_cache_->metrics().read->value();
  LOG(INFO) << "Evicted bytes: " << block_cache_->metrics().evict->value();
  LOG(INFO) << "Added bytes: " << block_cache_->metrics().add->value();
  LOG(INFO) << "Removed bytes: " << block_cache_->metrics().remove->value();
}

TEST_F(YbHnswTest, Random) {
  TestRandom(false);
}

TEST_F(YbHnswTest, Cache) {
  TestRandom(true);
}

TEST_F(YbHnswTest, ConcurrentCache) {
  TestRandom(true, 4);
}

void YbHnswTest::TestPerf() {
  auto div = static_cast<size_t>(std::sqrt(dimensions_ / 8));
  size_t num_vectors = RegularBuildVsSanitizers(65536, 4096) / div;
  size_t num_searches = RegularBuildVsSanitizers(65536, 4096) / div;
  constexpr size_t kMaxResults = 20;

  max_vectors_ = num_vectors;

  auto query_vectors = PrepareRandom(false, num_vectors, num_searches);
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
