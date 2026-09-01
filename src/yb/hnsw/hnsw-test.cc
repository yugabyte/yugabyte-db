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

#include <array>
#include <cmath>
#include <set>

#include "yb/hnsw/hnsw.h"
#include "yb/hnsw/hnsw_block_cache.h"
#include "yb/hnsw/vector_index_test_base.h"

#include "yb/rocksdb/cache.h"

#include "yb/util/env.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/vector_index_if.h"

using namespace std::chrono_literals;
using namespace yb::size_literals;

METRIC_DEFINE_entity(table);

namespace yb::hnsw {

using IndexImpl = unum::usearch::index_dense_gt<vector_index::VectorId>;

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

class YbHnswTest : public VectorIndexTestBase {
 protected:
  YbHnswTest() {}

  void InsertRandomVector(Vector& holder) {
    RandomVector(holder);
    ASSERT_TRUE(index_.add(vector_index::VectorId::GenerateRandom(), holder.data()));
  }

  void InsertRandomVectors(size_t count) {
    metric_ = unum::usearch::metric_punned_t(
        dimensions_, unum::usearch::metric_kind_t::l2sq_k, unum::usearch::scalar_kind_t::f32_k);
    yb_hnsw_.emplace(metric_, block_cache_);

    index_ = IndexImpl::make(metric_, CreateIndexDenseConfig());
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
    auto options = MakeSearchOptions(max_results);
    auto usearch_results = index_.filtered_search(query_vector.data(), max_results, options.filter);
    auto yb_hnsw_results = yb_hnsw_->Search(query_vector.data(), options, *context);
    ASSERT_EQ(usearch_results.count, yb_hnsw_results.size());
    for (size_t j = 0; j != usearch_results.count; ++j) {
      std::decay_t<decltype(yb_hnsw_results.front())> expected(
          usearch_results[j].member.key, usearch_results[j].distance);
      ASSERT_EQ(AsString(expected), AsString(yb_hnsw_results[j]));
    }
  }

  vector_index::SearchOptions MakeSearchOptions(size_t max_results) const {
    return vector_index::SearchOptions {
      .max_num_results = max_results,
      .ef = index_.config().expansion_search,
      .filter = AcceptAllVectors(),
    };
  }

  std::vector<Vector> PrepareRandom(bool load, size_t num_vectors, size_t num_searches);
  Status InitYbHnsw(bool load);

  void TestSimple(bool load);
  void TestRandom(bool load, size_t background_threads);

  unum::usearch::metric_punned_t metric_;
  IndexImpl index_;
  std::optional<YbHnsw> yb_hnsw_;
  YbHnswSearchContext context_;
};

Status YbHnswTest::InitYbHnsw(bool load) {
  auto path = GetTestPath("0.yb_hnsw");
  if (load) {
    {
      YbHnsw temp(metric_, block_cache_);
      RETURN_NOT_OK(temp.Import(index_, path));
    }
    RETURN_NOT_OK(yb_hnsw_->Init(path));
  } else {
    RETURN_NOT_OK(yb_hnsw_->Import(index_, path));
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

// ------------------------------------------------------------------------------------------------
// Narrowed coordinate storage
// ------------------------------------------------------------------------------------------------

using vector_index::VectorStorageKind;

namespace {

// Offset of a footer's version byte: MakeFooter writes it first, and the footer's own length is
// the last 8 bytes of the file.
Result<uint8_t> ReadFooterVersion(Env& env, const std::string& path) {
  std::unique_ptr<RandomAccessFile> file;
  RETURN_NOT_OK(env.NewRandomAccessFile(path, &file));
  auto file_size = VERIFY_RESULT(file->Size());

  std::array<uint8_t, sizeof(uint64_t)> size_buffer;
  Slice size_slice;
  RETURN_NOT_OK(file->Read(
      file_size - size_buffer.size(), size_buffer.size(), &size_slice, size_buffer.data()));
  auto footer_size = LittleEndian::Load64(size_slice.data());

  uint8_t version = 0;
  Slice version_slice;
  RETURN_NOT_OK(file->Read(file_size - footer_size, 1, &version_slice, &version));
  return version;
}

size_t CountCommon(const YbHnsw::SearchResult& lhs, const YbHnsw::SearchResult& rhs) {
  std::set<vector_index::VectorId> ids;
  for (const auto& entry : lhs) {
    ids.insert(entry.vector_id);
  }
  size_t result = 0;
  for (const auto& entry : rhs) {
    result += ids.contains(entry.vector_id);
  }
  return result;
}

}  // namespace

// Exercises the hnswlib -> YbHnsw import path, which is the one narrowed storage is wired
// through, at both encodings.
class YbHnswStorageTest : public VectorIndexTestBase {
 protected:
  using HnswlibImpl = HnswlibIndex<YbHnsw::DistanceType>;

  void BuildHnswlibIndex(size_t num_vectors) {
    space_ = std::make_unique<hnswlib::L2Space>(dimensions_);
    hnswlib_index_ = std::make_unique<HnswlibImpl>(
        space_.get(), num_vectors, /* M= */ 16, /* ef_construction= */ 100);
    vectors_.reserve(num_vectors);
    for (size_t i = 0; i != num_vectors; ++i) {
      vectors_.push_back(RandomVector());
      hnswlib_index_->addPoint(
          vectors_.back().data(), vector_index::VectorId::GenerateRandom());
    }
  }

  // Uses the production metric factory path, so a wrong storage-kind-to-scalar-kind mapping shows
  // up here rather than only in a full cluster test.
  YbHnsw::MetricFactory MakeMetricFactory() const {
    vector_index::HNSWOptions options;
    options.dimensions = dimensions_;
    options.distance_kind = vector_index::DistanceKind::kL2Squared;
    return [options](size_t dimensions, VectorStorageKind storage_kind) {
      return std::make_unique<UsearchMetric>(
          options.CreateStoredMetric(dimensions, storage_kind));
    };
  }

  // Imports the built graph at `storage_kind`. When `reload` is set the returned instance is a
  // fresh YbHnsw that only ever saw the file, which is what a tserver restart looks like.
  Result<std::unique_ptr<YbHnsw>> Import(
      VectorStorageKind storage_kind, const std::string& name, bool reload = false) {
    auto path = GetTestPath(name);
    auto result = std::make_unique<YbHnsw>(MakeMetricFactory(), block_cache_);
    RETURN_NOT_OK(result->Import(*hnswlib_index_, path, storage_kind));
    if (reload) {
      result = std::make_unique<YbHnsw>(MakeMetricFactory(), block_cache_);
      RETURN_NOT_OK(result->Init(path));
    }
    return result;
  }

  vector_index::SearchOptions MakeSearchOptions(size_t max_results, size_t ef = 64) const {
    return vector_index::SearchOptions {
      .max_num_results = max_results,
      .ef = ef,
      .filter = AcceptAllVectors(),
    };
  }

  std::unique_ptr<hnswlib::SpaceInterface<YbHnsw::DistanceType>> space_;
  std::unique_ptr<HnswlibImpl> hnswlib_index_;
  std::vector<Vector> vectors_;
  YbHnswSearchContext context_;
};

// A float32 index must keep producing version-1 footers, byte for byte, or every existing
// deployment's files stop being readable by the release before this one.
TEST_F(YbHnswStorageTest, Float32KeepsFooterVersionOne) {
  BuildHnswlibIndex(200);

  ASSERT_OK(Import(VectorStorageKind::kFloat32, "f32.yb_hnsw"));
  ASSERT_EQ(ASSERT_RESULT(ReadFooterVersion(*Env::Default(), GetTestPath("f32.yb_hnsw"))), 1);

  ASSERT_OK(Import(VectorStorageKind::kFloat16, "f16.yb_hnsw"));
  ASSERT_EQ(ASSERT_RESULT(ReadFooterVersion(*Env::Default(), GetTestPath("f16.yb_hnsw"))), 2);
}

TEST_F(YbHnswStorageTest, HeaderReflectsStorageKind) {
  BuildHnswlibIndex(200);

  auto f32 = ASSERT_RESULT(Import(VectorStorageKind::kFloat32, "f32.yb_hnsw"));
  auto f16 = ASSERT_RESULT(Import(VectorStorageKind::kFloat16, "f16.yb_hnsw"));

  ASSERT_EQ(f32->header().storage_kind, VectorStorageKind::kFloat32);
  ASSERT_EQ(f16->header().storage_kind, VectorStorageKind::kFloat16);
  ASSERT_EQ(f32->header().dimensions, dimensions_);
  ASSERT_EQ(f16->header().dimensions, dimensions_);

  // Only the coordinates shrink; the record header does not.
  const auto record_overhead = f32->header().vector_data_size - dimensions_ * sizeof(float);
  ASSERT_EQ(f16->header().vector_data_size, record_overhead + dimensions_ * sizeof(uint16_t));

  // Half-size records mean roughly twice as many fit in a block of the same size.
  ASSERT_GT(f16->header().vector_data_amount_per_block,
            f32->header().vector_data_amount_per_block);
}

// The encoding has to come from the file, not from whatever the caller currently thinks. A stale
// setting here used to be undetectable: the metric would decode records at the wrong width and
// return plausible-looking garbage.
TEST_F(YbHnswStorageTest, InitTakesStorageKindFromTheFile) {
  BuildHnswlibIndex(200);
  ASSERT_OK(Import(VectorStorageKind::kFloat16, "f16.yb_hnsw"));

  YbHnsw reopened(MakeMetricFactory(), block_cache_);
  ASSERT_OK(reopened.Init(GetTestPath("f16.yb_hnsw")));
  ASSERT_EQ(reopened.header().storage_kind, VectorStorageKind::kFloat16);

  // And it still answers correctly, i.e. the metric it built matches the records.
  auto results = reopened.Search(
      vectors_.front().data(), MakeSearchOptions(1, /* ef= */ 200), context_);
  ASSERT_EQ(results.size(), 1);
  ASSERT_EQ(results.front().distance, 0.0f);
}

// Writer and query path share one conversion, so an indexed vector used as its own query must
// come back at distance zero. If they ever diverge, this is the first thing that breaks.
TEST_F(YbHnswStorageTest, SelfDistanceIsZero) {
  BuildHnswlibIndex(500);

  for (auto storage_kind : {VectorStorageKind::kFloat32, VectorStorageKind::kFloat16}) {
    auto index = ASSERT_RESULT(Import(storage_kind, Format("$0.yb_hnsw", storage_kind)));
    for (size_t i = 0; i != 20; ++i) {
      const auto& query = vectors_[i * (vectors_.size() / 20)];
      // An exact match is the global minimum by a wide margin; a generous ef makes finding it
      // a certainty, so a failure here means the codec, not the graph walk.
      auto results = index->Search(query.data(), MakeSearchOptions(1, /* ef= */ 200), context_);
      ASSERT_EQ(results.size(), 1) << storage_kind;
      ASSERT_EQ(results.front().distance, 0.0f)
          << "storage_kind: " << storage_kind << ", vector: " << i;
    }
  }
}

// Narrowing costs some accuracy, and this pins how much. The threshold is deliberately loose:
// uniform random vectors in low dimensions are a harder case than real embeddings, because
// neighbours sit at nearly identical distances and reorder easily.
TEST_F(YbHnswStorageTest, Float16KeepsRecall) {
  constexpr size_t kNumVectors = 4000;
  constexpr size_t kNumQueries = 200;
  constexpr size_t kMaxResults = 10;
  constexpr double kMinOverlap = 0.9;

  dimensions_ = 64;
  BuildHnswlibIndex(kNumVectors);

  auto f32 = ASSERT_RESULT(Import(VectorStorageKind::kFloat32, "f32.yb_hnsw"));
  auto f16 = ASSERT_RESULT(Import(VectorStorageKind::kFloat16, "f16.yb_hnsw"));

  size_t common = 0;
  double worst_distance_error = 0;
  for (size_t i = 0; i != kNumQueries; ++i) {
    auto query = RandomVector();
    auto f32_results = f32->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
    auto f16_results = f16->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
    ASSERT_EQ(f32_results.size(), f16_results.size());
    common += CountCommon(f32_results, f16_results);

    // Top-1 distances should agree closely even when the identities differ.
    worst_distance_error = std::max<double>(
        worst_distance_error,
        std::fabs(f32_results.front().distance - f16_results.front().distance) /
            std::max(f32_results.front().distance, 1e-6f));
  }

  const double overlap = static_cast<double>(common) / (kNumQueries * kMaxResults);
  LOG(INFO) << "float16 top-" << kMaxResults << " overlap with float32: " << overlap
            << ", worst relative top-1 distance error: " << worst_distance_error;
  ASSERT_GE(overlap, kMinOverlap);
  ASSERT_LE(worst_distance_error, 0.05);
}

// Compaction reads source chunks back out of the served file, so a reopened index has to survive
// the same searches. Covers Init() plus the block cache path.
TEST_F(YbHnswStorageTest, Float16SurvivesReload) {
  constexpr size_t kMaxResults = 10;
  dimensions_ = 32;
  BuildHnswlibIndex(2000);

  auto fresh = ASSERT_RESULT(Import(VectorStorageKind::kFloat16, "a.yb_hnsw"));
  auto reloaded = ASSERT_RESULT(
      Import(VectorStorageKind::kFloat16, "b.yb_hnsw", /* reload= */ true));

  for (size_t i = 0; i != 100; ++i) {
    auto query = RandomVector();
    auto from_fresh = fresh->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
    auto from_reloaded = reloaded->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
    ASSERT_EQ(from_fresh.size(), from_reloaded.size());
    for (size_t j = 0; j != from_fresh.size(); ++j) {
      ASSERT_EQ(AsString(from_fresh[j]), AsString(from_reloaded[j]));
    }
  }
}

// Out-of-range coordinates must not turn into infinities: a single inf coordinate makes every
// distance to that vector NaN, and NaN comparisons quietly corrupt the search heaps.
TEST_F(YbHnswStorageTest, OutOfRangeCoordinatesStayFinite) {
  dimensions_ = 8;
  space_ = std::make_unique<hnswlib::L2Space>(dimensions_);
  hnswlib_index_ = std::make_unique<HnswlibImpl>(
      space_.get(), 64, /* M= */ 16, /* ef_construction= */ 100);

  for (size_t i = 0; i != 32; ++i) {
    auto vector = RandomVector();
    if (i % 8 == 0) {
      // Outside fp16's range but comfortably inside float32's, so the graph is still built from
      // finite distances and this isolates the clamp rather than testing hnswlib with infinities.
      vector[i % dimensions_] = 1e6f;
      vector[(i + 1) % dimensions_] = -1e6f;
    }
    vectors_.push_back(vector);
    hnswlib_index_->addPoint(vector.data(), vector_index::VectorId::GenerateRandom());
  }

  auto index = ASSERT_RESULT(Import(VectorStorageKind::kFloat16, "clamped.yb_hnsw"));
  for (size_t i = 0; i != 16; ++i) {
    auto results = index->Search(RandomVector().data(), MakeSearchOptions(10), context_);
    ASSERT_FALSE(results.empty());
    for (const auto& entry : results) {
      ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
    }
  }
}

}  // namespace yb::hnsw
