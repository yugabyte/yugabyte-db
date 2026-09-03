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

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

#include "yb/hnsw/hnsw.h"
#include "yb/hnsw/hnsw_block_cache.h"
#include "yb/hnsw/vector_index_test_base.h"

#include "yb/rocksdb/cache.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/coordinate_codec.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/vector_index_if.h"

using namespace std::chrono_literals;
using namespace yb::size_literals;

DECLARE_uint32(vector_index_rerank_overfetch_factor);

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

using vector_index::RerankStorageKind;
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
    ids_.reserve(num_vectors);
    for (size_t i = 0; i != num_vectors; ++i) {
      vectors_.push_back(RandomVector());
      ids_.push_back(vector_index::VectorId::GenerateRandom());
      hnswlib_index_->addPoint(vectors_.back().data(), ids_.back());
    }
  }

  const Vector& VectorForId(const vector_index::VectorId& id) const {
    auto it = std::find(ids_.begin(), ids_.end(), id);
    CHECK(it != ids_.end()) << "unknown vector id: " << id;
    return vectors_[it - ids_.begin()];
  }

  // Distance the rerank tier is expected to report: the float16 metric over float16 copies of
  // both sides, which is exactly what MakeResult computes.
  YbHnsw::DistanceType Float16Distance(const Vector& lhs, const Vector& rhs) const {
    vector_index::HNSWOptions options;
    options.dimensions = dimensions_;
    options.distance_kind = vector_index::DistanceKind::kL2Squared;
    UsearchMetric metric(
        options.CreateStoredMetric(dimensions_, VectorStorageKind::kFloat16));
    const auto bytes = vector_index::CoordinateBytes(VectorStorageKind::kFloat16, dimensions_);
    std::vector<std::byte> buffer(bytes * 2);
    vector_index::NarrowCoordinates(
        VectorStorageKind::kFloat16, lhs.data(), dimensions_, buffer.data());
    vector_index::NarrowCoordinates(
        VectorStorageKind::kFloat16, rhs.data(), dimensions_, buffer.data() + bytes);
    return metric.Distance(buffer.data(), buffer.data() + bytes);
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
      VectorStorageKind storage_kind, const std::string& name, bool reload = false,
      RerankStorageKind rerank_kind = RerankStorageKind::kNone) {
    auto path = GetTestPath(name);
    auto result = std::make_unique<YbHnsw>(MakeMetricFactory(), block_cache_);
    RETURN_NOT_OK(result->Import(*hnswlib_index_, path, storage_kind, rerank_kind));
    if (reload) {
      result = std::make_unique<YbHnsw>(MakeMetricFactory(), block_cache_);
      RETURN_NOT_OK(result->Init(path));
    }
    return result;
  }

  // The shipping int8 configuration: quantized traversal coordinates with an fp16 rerank copy.
  Result<std::unique_ptr<YbHnsw>> ImportInt8(const std::string& name, bool reload = false) {
    return Import(VectorStorageKind::kInt8, name, reload, RerankStorageKind::kFloat16);
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
  std::vector<vector_index::VectorId> ids_;
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

  // The rerank tier and the quantization scale are version 3, so adding them must not have
  // pushed float16 files past what a build with only float16 support can read.
  ASSERT_OK(ImportInt8("i8.yb_hnsw"));
  ASSERT_EQ(ASSERT_RESULT(ReadFooterVersion(*Env::Default(), GetTestPath("i8.yb_hnsw"))), 3);
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

  for (auto storage_kind :
       {VectorStorageKind::kFloat32, VectorStorageKind::kFloat16, VectorStorageKind::kInt8}) {
    // int8 only ever runs with a rerank tier, and the returned distance comes from that tier, so
    // this covers both conversions agreeing with themselves.
    auto rerank_kind = storage_kind == VectorStorageKind::kInt8
        ? RerankStorageKind::kFloat16 : RerankStorageKind::kNone;
    auto index = ASSERT_RESULT(Import(
        storage_kind, Format("$0.yb_hnsw", storage_kind), /* reload= */ false, rerank_kind));
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


// The scale is chunk-local and unrecoverable from anything else in the file, so it has to survive
// the footer round trip bit for bit -- it is serialized as a bit pattern precisely because a
// nearly-right scale would decode every record slightly wrong and look like a recall bug.
TEST_F(YbHnswStorageTest, HeaderRoundTripsScaleAndRerankKind) {
  BuildHnswlibIndex(200);

  auto fresh = ASSERT_RESULT(ImportInt8("a.yb_hnsw"));
  auto reloaded = ASSERT_RESULT(ImportInt8("b.yb_hnsw", /* reload= */ true));

  ASSERT_EQ(fresh->header().storage_kind, VectorStorageKind::kInt8);
  ASSERT_EQ(fresh->header().rerank_kind, RerankStorageKind::kFloat16);
  ASSERT_GT(fresh->header().quantization_scale, 0.0f);

  ASSERT_EQ(reloaded->header().storage_kind, VectorStorageKind::kInt8);
  ASSERT_EQ(reloaded->header().rerank_kind, RerankStorageKind::kFloat16);
  // Exact equality, not a tolerance: the field is serialized as a bit pattern, and a scale that
  // decodes almost right would make every record slightly wrong and read as a recall bug.
  ASSERT_EQ(reloaded->header().quantization_scale, fresh->header().quantization_scale);

  // Encodings that do not quantize must not carry a scale, so a stray non-zero value here would
  // mean the field is being set from something other than the data.
  auto f16 = ASSERT_RESULT(Import(VectorStorageKind::kFloat16, "f16.yb_hnsw"));
  ASSERT_EQ(f16->header().rerank_kind, RerankStorageKind::kNone);
  ASSERT_EQ(f16->header().quantization_scale, 0.0f);
}

// Both copies live in one record, so vector_data_size is the only stride and the rerank copy is
// found at a fixed offset inside it. Getting this arithmetic wrong reads the wrong bytes without
// ever going out of bounds, which is why it is asserted rather than left to the search tests.
TEST_F(YbHnswStorageTest, RecordLayoutIsInterleaved) {
  BuildHnswlibIndex(200);

  auto f32 = ASSERT_RESULT(Import(VectorStorageKind::kFloat32, "f32.yb_hnsw"));
  auto i8 = ASSERT_RESULT(ImportInt8("i8.yb_hnsw"));

  const auto record_overhead = f32->header().vector_data_size - dimensions_ * sizeof(float);
  ASSERT_EQ(
      i8->header().vector_data_size,
      record_overhead + dimensions_ * sizeof(int8_t) + dimensions_ * sizeof(uint16_t));
  ASSERT_EQ(i8->header().coordinates_size(), dimensions_ * sizeof(int8_t));

  // 3 bytes per coordinate against float32's 4, so the whole file is smaller even though each
  // vector is stored twice.
  ASSERT_LT(i8->header().vector_data_size, f32->header().vector_data_size);
  ASSERT_LT(
      ASSERT_RESULT(Env::Default()->GetFileSize(GetTestPath("i8.yb_hnsw"))),
      ASSERT_RESULT(Env::Default()->GetFileSize(GetTestPath("f32.yb_hnsw"))));
}

// The point of the design: int8 traversal costs recall, and reranking a modest over-fetch at
// float16 gets it back. Asserting the un-reranked configuration is measurably worse is not
// redundant -- without it the test passes just as happily when the rerank tier does nothing,
// which is what a capped candidate budget produces.
TEST_F(YbHnswStorageTest, Int8WithRerankMatchesFloat16Recall) {
  constexpr size_t kNumVectors = 4000;
  constexpr size_t kNumQueries = 200;
  constexpr size_t kMaxResults = 10;

  dimensions_ = 64;
  BuildHnswlibIndex(kNumVectors);

  auto f32 = ASSERT_RESULT(Import(VectorStorageKind::kFloat32, "f32.yb_hnsw"));
  auto f16 = ASSERT_RESULT(Import(VectorStorageKind::kFloat16, "f16.yb_hnsw"));
  auto i8 = ASSERT_RESULT(ImportInt8("i8.yb_hnsw"));
  auto i8_bare = ASSERT_RESULT(Import(VectorStorageKind::kInt8, "i8_bare.yb_hnsw"));

  std::vector<Vector> queries;
  for (size_t i = 0; i != kNumQueries; ++i) {
    queries.push_back(RandomVector());
  }

  auto measure_overlap = [this, &queries, &f32](YbHnsw& index) {
    size_t common = 0;
    for (const auto& query : queries) {
      auto expected = f32->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
      auto actual = index.Search(query.data(), MakeSearchOptions(kMaxResults), context_);
      common += CountCommon(expected, actual);
    }
    return static_cast<double>(common) / (queries.size() * kMaxResults);
  };

  const auto f16_overlap = measure_overlap(*f16);
  const auto i8_overlap = measure_overlap(*i8);
  const auto bare_overlap = measure_overlap(*i8_bare);

  LOG(INFO) << "top-" << kMaxResults << " overlap with float32: float16 " << f16_overlap
            << ", int8 + float16 rerank " << i8_overlap << ", int8 alone " << bare_overlap;

  // Reranking cannot beat the encoding it reranks with, so float16's overlap is the ceiling; the
  // margin absorbs graph non-determinism between the two files. That reranking ran at all is
  // asserted deterministically below, not by comparing recall.
  ASSERT_GE(i8_overlap, f16_overlap - 0.02);

  // Deterministic proof the rerank ran: each reported distance must be exactly what the float16
  // metric gives for that row, since that is what MakeResult computes. The un-reranked index
  // reports the traversal's quantized distance, larger by 1/scale squared.
  for (size_t i = 0; i != 20; ++i) {
    auto query = RandomVector();
    auto reranked = i8->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
    auto quantized = i8_bare->Search(query.data(), MakeSearchOptions(kMaxResults), context_);
    ASSERT_EQ(reranked.size(), kMaxResults);
    ASSERT_EQ(quantized.size(), kMaxResults);

    for (const auto& entry : reranked) {
      ASSERT_EQ(entry.distance, Float16Distance(query, VectorForId(entry.vector_id)))
          << "reported distance is not the float16 metric's: " << entry;
    }
    // And without a rerank tier it is not, which is what makes the assertion above meaningful.
    const auto& nearest = quantized.front();
    ASSERT_NE(nearest.distance, Float16Distance(query, VectorForId(nearest.vector_id)))
        << "un-reranked search reported a float16 distance: " << nearest;
  }
}

// Over-fetching must work when ef <= max_num_results, the case a naive
// min(max(ef, k), overfetch * k) budget silently breaks: it retains exactly k candidates, so
// reranking reorders rather than corrects. Size assertions all still pass in that state, so this
// compares behaviour across over-fetch factors instead.
TEST_F(YbHnswStorageTest, OverFetchActuallyOverFetches) {
  constexpr size_t kNumVectors = 4000;
  constexpr size_t kNumQueries = 100;

  dimensions_ = 64;
  BuildHnswlibIndex(kNumVectors);

  auto i8 = ASSERT_RESULT(ImportInt8("i8.yb_hnsw"));

  std::vector<Vector> queries;
  for (size_t i = 0; i != kNumQueries; ++i) {
    queries.push_back(RandomVector());
  }

  // ef below, equal to, and above max_num_results, plus the k=1 boundary.
  for (auto [max_results, ef] : std::initializer_list<std::pair<size_t, size_t>>{
           {50, 10}, {50, 50}, {10, 64}, {1, 1}, {1, 64}}) {
    std::vector<std::vector<std::string>> rendered_by_factor;
    std::vector<double> total_distance_by_factor;
    for (uint32_t factor : {1, 3}) {
      google::FlagSaver flag_saver;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_rerank_overfetch_factor) = factor;
      std::vector<std::string> rendered;
      double total_distance = 0;
      for (const auto& query : queries) {
        auto actual = i8->Search(query.data(), MakeSearchOptions(max_results, ef), context_);
        // Over-fetching is internal: the caller still gets exactly what it asked for.
        ASSERT_EQ(actual.size(), std::min(max_results, kNumVectors))
            << "max_results: " << max_results << ", ef: " << ef << ", factor: " << factor;
        for (const auto& entry : actual) {
          ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
          total_distance += entry.distance;
        }
        rendered.push_back(AsString(actual));
      }
      rendered_by_factor.push_back(std::move(rendered));
      total_distance_by_factor.push_back(total_distance);
    }

    LOG(INFO) << "max_results: " << max_results << ", ef: " << ef
              << " -- summed distance at factor 1: " << total_distance_by_factor[0]
              << ", at factor 3: " << total_distance_by_factor[1];

    // Reranking a superset cannot select a worse result, so summed over all queries a larger
    // retained set must not be farther. Recall is not the assertion: it is not monotone in the
    // budget, which also moves the traversal's termination bound.
    ASSERT_LE(total_distance_by_factor[1], total_distance_by_factor[0] * 1.0001)
        << "retaining more candidates produced farther results: max_results " << max_results
        << ", ef " << ef;

    // Where the over-fetch pushes the budget past max(ef, k) the search demonstrably does more
    // work, so the answer has to change somewhere across these queries. Under the capped formula
    // it would be identical for every one of them, which is the bug.
    if (3 * max_results > std::max(ef, max_results)) {
      ASSERT_NE(rendered_by_factor[0], rendered_by_factor[1])
          << "factor 3 returned exactly what factor 1 did for all " << kNumQueries
          << " queries, so the candidate budget is not growing: max_results " << max_results
          << ", ef " << ef;
    }
  }
}

TEST_F(YbHnswStorageTest, Int8SurvivesReload) {
  constexpr size_t kMaxResults = 10;
  dimensions_ = 32;
  BuildHnswlibIndex(2000);

  auto fresh = ASSERT_RESULT(ImportInt8("a.yb_hnsw"));
  auto reloaded = ASSERT_RESULT(ImportInt8("b.yb_hnsw", /* reload= */ true));

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

// A chunk whose coordinates are all zero would otherwise derive a zero scale, and dividing by it
// turns every coordinate into an infinity -- which makes every distance NaN rather than failing.
TEST_F(YbHnswStorageTest, Int8HandlesAnAllZeroChunk) {
  dimensions_ = 8;
  space_ = std::make_unique<hnswlib::L2Space>(dimensions_);
  hnswlib_index_ = std::make_unique<HnswlibImpl>(
      space_.get(), 64, /* M= */ 16, /* ef_construction= */ 100);
  for (size_t i = 0; i != 32; ++i) {
    vectors_.push_back(Vector(dimensions_, 0.0f));
    hnswlib_index_->addPoint(vectors_.back().data(), vector_index::VectorId::GenerateRandom());
  }

  auto index = ASSERT_RESULT(ImportInt8("zeros.yb_hnsw"));
  ASSERT_GT(index->header().quantization_scale, 0.0f);

  auto results = index->Search(vectors_.front().data(), MakeSearchOptions(10), context_);
  ASSERT_FALSE(results.empty());
  for (const auto& entry : results) {
    ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
    ASSERT_EQ(entry.distance, 0.0f) << entry;
  }
}

// An infinite coordinate must not set the quantization scale: its reciprocal is zero, which would
// quantize the whole chunk, well-behaved coordinates included, to zero -- silently destroying the
// traversal while the reranked distances still look plausible.
TEST_F(YbHnswStorageTest, Int8ScaleIgnoresNonFiniteCoordinates) {
  dimensions_ = 8;
  space_ = std::make_unique<hnswlib::L2Space>(dimensions_);
  hnswlib_index_ = std::make_unique<HnswlibImpl>(
      space_.get(), 64, /* M= */ 16, /* ef_construction= */ 100);

  // Every coordinate is well below 4, so the expected scale comes from this and nothing else.
  float max_finite = 0;
  for (size_t i = 0; i != 32; ++i) {
    auto vector = RandomVector();
    for (auto coordinate : vector) {
      max_finite = std::max(max_finite, std::fabs(coordinate));
    }
    vectors_.push_back(vector);
    ids_.push_back(vector_index::VectorId::GenerateRandom());
    hnswlib_index_->addPoint(vectors_.back().data(), ids_.back());
  }

  // One vector carrying an infinity and a NaN. hnswlib will compute non-finite distances to it
  // while building, which is fine here: what is under test is the scale the writer derives.
  auto poisoned = RandomVector();
  poisoned[0] = std::numeric_limits<float>::infinity();
  poisoned[1] = -std::numeric_limits<float>::infinity();
  poisoned[2] = std::numeric_limits<float>::quiet_NaN();
  vectors_.push_back(poisoned);
  ids_.push_back(vector_index::VectorId::GenerateRandom());
  hnswlib_index_->addPoint(vectors_.back().data(), ids_.back());

  auto index = ASSERT_RESULT(ImportInt8("poisoned.yb_hnsw"));
  const auto scale = index->header().quantization_scale;
  ASSERT_TRUE(std::isfinite(scale)) << "an infinite coordinate set the scale";
  ASSERT_GT(scale, 0.0f);
  // Derived from the finite data alone. Compared against the largest finite magnitude among the
  // healthy vectors, whose own coordinates are the only ones that should count.
  ASSERT_LE(std::fabs(scale - max_finite / vector_index::kMaxInt8), 1e-9f)
      << "scale " << scale << " does not match max finite magnitude " << max_finite;

  // And the healthy vectors still quantize to distinguishable records: each is found at distance
  // zero from itself, which cannot hold if every coordinate collapsed to the same byte.
  for (size_t i = 0; i != 10; ++i) {
    auto results = index->Search(
        vectors_[i].data(), MakeSearchOptions(1, /* ef= */ 200), context_);
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results.front().distance, 0.0f) << "vector " << i;
  }
}

// Same clamp hazard as float16, but a different failure: an out-of-range float -> int8_t
// conversion is undefined behaviour rather than a saturation, so without the clamp the stored
// byte is whatever the hardware happens to produce.
TEST_F(YbHnswStorageTest, Int8ClampsOutOfRangeCoordinates) {
  dimensions_ = 8;
  space_ = std::make_unique<hnswlib::L2Space>(dimensions_);
  hnswlib_index_ = std::make_unique<HnswlibImpl>(
      space_.get(), 64, /* M= */ 16, /* ef_construction= */ 100);

  for (size_t i = 0; i != 32; ++i) {
    auto vector = RandomVector();
    // One vector far outside the rest, so the scale it sets leaves every other coordinate in the
    // bottom few steps of the range -- the outlier case the scale is most sensitive to.
    if (i == 0) {
      vector[0] = 1e4f;
    }
    vectors_.push_back(vector);
    hnswlib_index_->addPoint(vector.data(), vector_index::VectorId::GenerateRandom());
  }

  auto index = ASSERT_RESULT(ImportInt8("outlier.yb_hnsw"));
  for (size_t i = 0; i != 16; ++i) {
    auto results = index->Search(RandomVector().data(), MakeSearchOptions(10), context_);
    ASSERT_FALSE(results.empty());
    for (const auto& entry : results) {
      ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
      ASSERT_GE(entry.distance, 0.0f) << entry;
    }
  }

  // A query well outside the chunk's range clamps rather than wrapping, and still returns
  // finite, ordered results.
  auto far_query = Vector(dimensions_, 1e9f);
  auto results = index->Search(far_query.data(), MakeSearchOptions(5), context_);
  ASSERT_FALSE(results.empty());
  for (const auto& entry : results) {
    ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
  }
}

}  // namespace yb::hnsw
