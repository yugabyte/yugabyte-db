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
#include "yb/hnsw/vector_index_test_base.h"

#include "yb/rocksdb/cache.h"

#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnswlib_include.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/vector_index_if.h"

using namespace std::chrono_literals;
using namespace yb::size_literals;

METRIC_DEFINE_entity(table);

namespace yb::hnsw {

using IndexImpl = HnswlibIndex<YbHnsw::DistanceType>;

constexpr size_t kConnectivity = 64;
constexpr size_t kExpansionAdd = 128;
constexpr size_t kExpansionSearch = 64;

struct AcceptAllVectors {
  bool operator()(const vector_index::VectorId& id) const {
    return true;
  }
};

// Builds the reference graph with the metric product YbHnsw searches with (UsearchMetric), so
// YbHnsw and hnswlib compute bitwise identical distances and their results can be compared exactly.
class UsearchSpace : public hnswlib::SpaceInterface<YbHnsw::DistanceType> {
 public:
  explicit UsearchSpace(const UsearchMetric::Impl& metric)
      : metric_(metric), data_size_(metric.bytes_per_vector()) {}

  size_t get_data_size() override {
    return data_size_;
  }

  hnswlib::DISTFUNC<YbHnsw::DistanceType> get_dist_func() override {
    return &DistFunc;
  }

  void* get_dist_func_param() override {
    return &metric_;
  }

 private:
  static YbHnsw::DistanceType DistFunc(const void* lhs, const void* rhs, const void* arg) {
    using unum::usearch::byte_t;
    return (*static_cast<const UsearchMetric::Impl*>(arg))(
        static_cast<const byte_t*>(lhs), static_cast<const byte_t*>(rhs));
  }

  UsearchMetric::Impl metric_;
  size_t data_size_;
};

class HnswlibFilter : public hnswlib::BaseFilterFunctor<vector_index::VectorId> {
 public:
  explicit HnswlibFilter(const vector_index::VectorFilter& filter) : filter_(filter) {}

  bool operator()(vector_index::VectorId id) override {
    return filter_(id);
  }

 private:
  const vector_index::VectorFilter& filter_;
};

class YbHnswTest : public VectorIndexTestBase {
 protected:
  YbHnswTest() {}

  void InsertRandomVector(Vector& holder) {
    RandomVector(holder);
    index_->addPoint(holder.data(), vector_index::VectorId::GenerateRandom());
  }

  void InsertRandomVectors(size_t count) {
    metric_ = UsearchMetric::Impl(
        dimensions_, unum::usearch::metric_kind_t::l2sq_k, unum::usearch::scalar_kind_t::f32_k);
    space_.emplace(metric_);
    yb_hnsw_.emplace(metric_, block_cache_);

    index_.emplace(
        &*space_, max_vectors_, kConnectivity, kExpansionAdd, /* random_seed= */ 100,
        /* allow_replace_deleted= */ false, kExpansionSearch);

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
    auto options = MakeSearchOptions(max_results);
    HnswlibFilter hnswlib_filter(options.filter);
    auto hnswlib_results = index_->searchKnnCloserFirst(
        query_vector.data(), max_results, &hnswlib_filter, options.ef);
    auto yb_hnsw_results = yb_hnsw_->Search(query_vector.data(), options, *context);
    ASSERT_EQ(hnswlib_results.size(), yb_hnsw_results.size());
    for (size_t j = 0; j != hnswlib_results.size(); ++j) {
      const auto& [distance, vector_id] = hnswlib_results[j];
      std::decay_t<decltype(yb_hnsw_results.front())> expected(vector_id, distance);
      ASSERT_EQ(AsString(expected), AsString(yb_hnsw_results[j]));
    }
  }

  vector_index::SearchOptions MakeSearchOptions(size_t max_results) const {
    return vector_index::SearchOptions {
      .max_num_results = max_results,
      .ef = kExpansionSearch,
      .filter = AcceptAllVectors(),
    };
  }

  std::vector<Vector> PrepareRandom(bool load, size_t num_vectors, size_t num_searches);
  Status InitYbHnsw(bool load);

  void TestSimple(bool load);
  void TestRandom(bool load, size_t background_threads);

  UsearchMetric::Impl metric_;
  std::optional<UsearchSpace> space_;
  std::optional<IndexImpl> index_;
  std::optional<YbHnsw> yb_hnsw_;
  YbHnswSearchContext context_;
};

Status YbHnswTest::InitYbHnsw(bool load) {
  auto path = GetTestPath("0.yb_hnsw");
  if (load) {
    {
      YbHnsw temp(metric_, block_cache_);
      RETURN_NOT_OK(temp.Import(*index_, path));
    }
    RETURN_NOT_OK(yb_hnsw_->Init(path));
  } else {
    RETURN_NOT_OK(yb_hnsw_->Import(*index_, path));
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

}  // namespace yb::hnsw
