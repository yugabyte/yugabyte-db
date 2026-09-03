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
// Narrowed coordinate storage through the VectorIndexIf wrapper stack -- the path a VectorLSM
// chunk takes: build in RAM, SaveToFile, then Create(kLoad) + LoadFromFile. The parts that matter
// are the ones reading stored records back out: iteration, which is how compaction consumes
// source chunks, and Distance, which callers reach with full-precision vectors.

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "yb/ann_methods/hnswlib_wrapper.h"

#include "yb/hnsw/hnsw_block_cache.h"
#include "yb/hnsw/vector_index_test_base.h"

#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

#include "yb/vector_index/coordinate_codec.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::ann_methods {

using vector_index::FactoryMode;
using vector_index::HNSWOptions;
using vector_index::RerankStorageKind;
using vector_index::VectorId;
using vector_index::VectorIndexIfPtr;
using vector_index::VectorStorageKind;

using DistanceResult = float;
using IndexPtr = VectorIndexIfPtr<FloatVector, DistanceResult>;

class YbHnswStorageWrapperTest : public hnsw::VectorIndexTestBase {
 protected:
  size_t BlockCacheCapacity() override {
    return 256_MB;
  }

  HNSWOptions Options(
      VectorStorageKind storage_kind,
      RerankStorageKind rerank_kind = RerankStorageKind::kNone) const {
    HNSWOptions options;
    options.dimensions = dimensions_;
    options.num_neighbors_per_vertex = 16;
    options.num_neighbors_per_vertex_base = 32;
    options.ef_construction = 100;
    options.storage_kind = storage_kind;
    options.rerank_kind = rerank_kind;
    return options;
  }

  // Builds an in-RAM hnswlib chunk, flushes it, then reopens it the way a restart would: a fresh
  // instance from the traits factory that has only ever seen the file.
  IndexPtr BuildAndReload(
      VectorStorageKind storage_kind, const std::string& name,
      RerankStorageKind rerank_kind = RerankStorageKind::kNone) {
    auto options = Options(storage_kind, rerank_kind);
    auto traits = CHECK_RESULT((CreateHnswlibIndexTraits<FloatVector, DistanceResult>(
        block_cache_, options, HnswBackend::YB_HNSW_HNSWLIB, mem_tracker_)));

    auto mutable_index = traits->Create(FactoryMode::kCreate);
    CHECK_OK(mutable_index->Reserve(
        vectors_.size(), 1, 1, rocksdb::Cache::ReservationMode::kAlways));
    for (size_t i = 0; i != vectors_.size(); ++i) {
      CHECK_OK(mutable_index->Insert(ids_[i], vectors_[i]));
    }

    const auto path = GetTestPath(name);
    // SaveToFile returns the file-backed index directly; drop it and go through the load
    // factory instead, so this covers the restart path rather than the flush path.
    CHECK_RESULT(mutable_index->SaveToFile(path));

    auto loaded = traits->Create(FactoryMode::kLoad);
    CHECK_OK(loaded->LoadFromFile(path, 1));
    return loaded;
  }

  void GenerateVectors(size_t count) {
    vectors_ = RandomVectors(count);
    ids_.clear();
    ids_.reserve(count);
    for (size_t i = 0; i != count; ++i) {
      ids_.push_back(VectorId::GenerateRandom());
    }
  }

  // The shipping int8 configuration.
  IndexPtr BuildAndReloadInt8(const std::string& name) {
    return BuildAndReload(VectorStorageKind::kInt8, name, RerankStorageKind::kFloat16);
  }

  vector_index::SearchOptions MakeSearchOptions(size_t max_results, size_t ef = 64) const {
    return vector_index::SearchOptions {
      .max_num_results = max_results,
      .ef = ef,
    };
  }

  std::vector<hnsw::Vector> vectors_;
  std::vector<VectorId> ids_;
  MemTrackerPtr mem_tracker_ =
      MemTracker::GetRootTracker()->FindOrCreateTracker(1_GB, "yb_hnsw_storage_test");
};

// VectorLSM's MergingIterator reads every source chunk through this iterator on every compaction,
// and reading float32-wide from a float16 record would run off the end of each one -- so this
// checks both that iteration terminates correctly and that its values round-trip.
TEST_F(YbHnswStorageWrapperTest, IterationDecodesNarrowedRecords) {
  constexpr size_t kNumVectors = 500;
  dimensions_ = 24;
  GenerateVectors(kNumVectors);

  for (auto storage_kind : {VectorStorageKind::kFloat32, VectorStorageKind::kFloat16}) {
    auto index = BuildAndReload(storage_kind, Format("iter_$0.yb_hnsw", storage_kind));
    ASSERT_EQ(index->Size(), kNumVectors) << storage_kind;

    std::map<VectorId, hnsw::Vector> seen;
    for (const auto& [vector_id, vector] : *index) {
      ASSERT_EQ(vector.size(), dimensions_) << storage_kind;
      ASSERT_TRUE(seen.emplace(vector_id, vector).second)
          << "duplicate id from iteration: " << vector_id << ", " << storage_kind;
    }
    ASSERT_EQ(seen.size(), kNumVectors) << storage_kind;

    // Every yielded vector must equal the original narrowed through the same encoding, which is
    // exactly the invariant that keeps compaction from degrading an index over time.
    for (size_t i = 0; i != kNumVectors; ++i) {
      auto it = seen.find(ids_[i]);
      ASSERT_NE(it, seen.end()) << "missing id " << ids_[i] << ", " << storage_kind;

      std::vector<std::byte> narrowed(
          vector_index::CoordinateBytes(storage_kind, dimensions_));
      vector_index::NarrowCoordinates(
          storage_kind, vectors_[i].data(), dimensions_, narrowed.data());
      hnsw::Vector expected(dimensions_);
      vector_index::WidenCoordinates(
          storage_kind, narrowed.data(), dimensions_, expected.data());

      ASSERT_EQ(expected, it->second) << "vector " << i << ", " << storage_kind;
    }
  }
}

// Re-narrowing what iteration produced has to reproduce the same bytes, or precision would drift
// on every compaction.
TEST_F(YbHnswStorageWrapperTest, CompactionRoundTripIsStable) {
  dimensions_ = 32;
  GenerateVectors(400);

  auto index = BuildAndReload(VectorStorageKind::kFloat16, "roundtrip.yb_hnsw");

  auto narrow = [this](const hnsw::Vector& vector) {
    std::vector<std::byte> out(
        vector_index::CoordinateBytes(VectorStorageKind::kFloat16, dimensions_));
    vector_index::NarrowCoordinates(
        VectorStorageKind::kFloat16, vector.data(), dimensions_, out.data());
    return out;
  };

  for (const auto& [vector_id, vector] : *index) {
    // vector came out of a float16 record, so narrowing it again must be a no-op.
    std::vector<std::byte> widened_then_narrowed = narrow(vector);
    hnsw::Vector again(dimensions_);
    vector_index::WidenCoordinates(
        VectorStorageKind::kFloat16, widened_then_narrowed.data(), dimensions_, again.data());
    ASSERT_EQ(vector, again) << vector_id;
  }
}

// Distance() takes full-precision vectors but the metric decodes stored records, so it has to
// narrow its arguments. Without that it compares float32 bytes with a float16 metric and returns
// nonsense.
TEST_F(YbHnswStorageWrapperTest, DistanceNarrowsFullPrecisionArguments) {
  dimensions_ = 16;
  GenerateVectors(200);

  auto f32 = BuildAndReload(VectorStorageKind::kFloat32, "dist_f32.yb_hnsw");
  auto f16 = BuildAndReload(VectorStorageKind::kFloat16, "dist_f16.yb_hnsw");

  for (size_t i = 0; i != 50; ++i) {
    const auto& lhs = vectors_[i];
    const auto& rhs = vectors_[(i + 37) % vectors_.size()];

    const auto exact = f32->Distance(lhs, rhs);
    const auto narrowed = f16->Distance(lhs, rhs);

    ASSERT_TRUE(std::isfinite(narrowed)) << "pair " << i;
    ASSERT_GT(exact, 0.0f) << "pair " << i;
    ASSERT_LE(std::fabs(narrowed - exact) / exact, 0.01) << "pair " << i;

    // A vector against itself is exactly zero under either encoding.
    ASSERT_EQ(f16->Distance(lhs, lhs), 0.0f) << "pair " << i;
  }
}

// End to end through the wrapper: the loaded index answers searches, and narrowing costs little
// enough accuracy that the top results still largely agree with full precision.
TEST_F(YbHnswStorageWrapperTest, SearchAgreesWithFullPrecision) {
  constexpr size_t kNumVectors = 4000;
  constexpr size_t kNumQueries = 200;
  constexpr size_t kMaxResults = 10;

  dimensions_ = 64;
  GenerateVectors(kNumVectors);

  auto f32 = BuildAndReload(VectorStorageKind::kFloat32, "search_f32.yb_hnsw");
  auto f16 = BuildAndReload(VectorStorageKind::kFloat16, "search_f16.yb_hnsw");

  size_t common = 0;
  for (size_t i = 0; i != kNumQueries; ++i) {
    auto query = RandomVector();
    auto from_f32 = ASSERT_RESULT(f32->Search(query, MakeSearchOptions(kMaxResults)));
    auto from_f16 = ASSERT_RESULT(f16->Search(query, MakeSearchOptions(kMaxResults)));
    ASSERT_EQ(from_f32.size(), kMaxResults);
    ASSERT_EQ(from_f16.size(), kMaxResults);

    std::set<VectorId> f32_ids;
    for (const auto& entry : from_f32) {
      f32_ids.insert(entry.vector_id);
    }
    for (const auto& entry : from_f16) {
      common += f32_ids.contains(entry.vector_id);
      ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
    }
  }

  const double overlap = static_cast<double>(common) / (kNumQueries * kMaxResults);
  LOG(INFO) << "float16 top-" << kMaxResults << " overlap with float32: " << overlap;
  ASSERT_GE(overlap, 0.9);
}


// The compaction hazard the two-tier record exists to avoid. Compaction re-inserts what this
// iterator yields into a new chunk, which derives its own quantization scale, so decoding the int8
// coordinates would feed already-quantized values into a fresh quantization and lose a little more
// on every merge. The rerank copy round-trips exactly, making a vector a fixed point across
// arbitrarily many compactions.
TEST_F(YbHnswStorageWrapperTest, IterationReadsTheRerankCopy) {
  constexpr size_t kNumVectors = 500;
  dimensions_ = 24;
  GenerateVectors(kNumVectors);

  auto index = BuildAndReloadInt8("iter_i8.yb_hnsw");
  ASSERT_EQ(index->Size(), kNumVectors);

  size_t count = 0;
  double worst_error = 0;
  for (const auto& [vector_id, vector] : *index) {
    ASSERT_EQ(vector.size(), dimensions_);
    auto it = std::find(ids_.begin(), ids_.end(), vector_id);
    ASSERT_NE(it, ids_.end()) << "unknown id from iteration: " << vector_id;
    const auto& original = vectors_[it - ids_.begin()];

    // Must be the float16 round trip of the original, not the int8 one. Exact equality against
    // float16 rather than a tolerance is what distinguishes them: an int8 decode would be off by up
    // to half a quantization step, orders of magnitude more.
    std::vector<std::byte> narrowed(
        vector_index::CoordinateBytes(VectorStorageKind::kFloat16, dimensions_));
    vector_index::NarrowCoordinates(
        VectorStorageKind::kFloat16, original.data(), dimensions_, narrowed.data());
    hnsw::Vector expected(dimensions_);
    vector_index::WidenCoordinates(
        VectorStorageKind::kFloat16, narrowed.data(), dimensions_, expected.data());
    ASSERT_EQ(expected, vector) << "id " << vector_id;

    for (size_t i = 0; i != dimensions_; ++i) {
      worst_error = std::max<double>(worst_error, std::fabs(vector[i] - original[i]));
    }
    ++count;
  }
  ASSERT_EQ(count, kNumVectors);
  LOG(INFO) << "worst absolute coordinate error from iteration: " << worst_error;
}

// Repeated compaction must not degrade a vector. Feeding what iteration yields back through a
// fresh chunk models one merge; doing it several times is what would expose compounding error.
TEST_F(YbHnswStorageWrapperTest, Int8SurvivesRepeatedCompaction) {
  constexpr size_t kNumVectors = 300;
  constexpr size_t kGenerations = 4;
  dimensions_ = 32;
  GenerateVectors(kNumVectors);

  std::map<VectorId, hnsw::Vector> previous;
  for (size_t generation = 0; generation != kGenerations; ++generation) {
    auto index = BuildAndReloadInt8(Format("gen_$0.yb_hnsw", generation));

    std::map<VectorId, hnsw::Vector> current;
    for (const auto& [vector_id, vector] : *index) {
      ASSERT_TRUE(current.emplace(vector_id, vector).second) << vector_id;
    }
    ASSERT_EQ(current.size(), kNumVectors);

    if (generation != 0) {
      // Bit-identical to the previous generation, which is the fixed point that makes compaction
      // safe to run any number of times. Compared per id rather than whole-map, so a failure
      // names the vector that drifted instead of dumping every coordinate in the chunk.
      for (const auto& [vector_id, vector] : current) {
        auto it = previous.find(vector_id);
        ASSERT_NE(it, previous.end()) << "generation " << generation << " lost " << vector_id;
        ASSERT_EQ(vector, it->second)
            << "generation " << generation << " drifted for " << vector_id;
      }
    }
    previous = std::move(current);

    // The next generation is built from what this one yielded, i.e. from stored records rather
    // than from the originals -- which is what a real compaction does.
    for (size_t i = 0; i != kNumVectors; ++i) {
      vectors_[i] = previous[ids_[i]];
    }
  }
}

// Distance() feeds VectorLSM's comparison of in-flight vectors against chunk results, so it has
// to be in the metric's own units. Computing it with the int8 metric would return a value in
// quantized units -- comparable within one chunk and meaningless anywhere else.
TEST_F(YbHnswStorageWrapperTest, Int8DistanceUsesTheRerankMetric) {
  dimensions_ = 16;
  GenerateVectors(200);

  auto f32 = BuildAndReload(VectorStorageKind::kFloat32, "dist_f32.yb_hnsw");
  auto f16 = BuildAndReload(VectorStorageKind::kFloat16, "dist_f16.yb_hnsw");
  auto i8 = BuildAndReloadInt8("dist_i8.yb_hnsw");

  for (size_t i = 0; i != 50; ++i) {
    const auto& lhs = vectors_[i];
    const auto& rhs = vectors_[(i + 37) % vectors_.size()];

    const auto exact = f32->Distance(lhs, rhs);
    ASSERT_GT(exact, 0.0f) << "pair " << i;

    // Agrees with float16 to the bit, because that is the metric it uses.
    ASSERT_EQ(i8->Distance(lhs, rhs), f16->Distance(lhs, rhs)) << "pair " << i;
    // And therefore lands within float16's error of full precision, not int8's.
    ASSERT_LE(std::fabs(i8->Distance(lhs, rhs) - exact) / exact, 0.01) << "pair " << i;
    ASSERT_EQ(i8->Distance(lhs, lhs), 0.0f) << "pair " << i;
  }
}

// The scale is derived per chunk, so two chunks of the same index hold different ones. A search
// that quantized its query with a cached or shared scale would decode one of them wrong.
TEST_F(YbHnswStorageWrapperTest, ScalesDifferAcrossChunks) {
  constexpr size_t kNumVectors = 300;
  dimensions_ = 16;

  // Two chunks whose coordinate ranges differ by two orders of magnitude.
  GenerateVectors(kNumVectors);
  auto narrow_range = vectors_;
  auto wide_range = vectors_;
  for (auto& vector : wide_range) {
    for (auto& coordinate : vector) {
      coordinate *= 100.0f;
    }
  }

  vectors_ = narrow_range;
  auto small = BuildAndReloadInt8("scale_small.yb_hnsw");
  vectors_ = wide_range;
  auto large = BuildAndReloadInt8("scale_large.yb_hnsw");

  // Each chunk must be searchable on its own terms: a query drawn from its own range finds its
  // exact match at distance zero, which only holds if the query was quantized with that chunk's
  // scale.
  for (size_t i = 0; i != 20; ++i) {
    auto from_small = ASSERT_RESULT(small->Search(narrow_range[i], MakeSearchOptions(1, 200)));
    ASSERT_EQ(from_small.size(), 1);
    ASSERT_EQ(from_small.front().distance, 0.0f) << "narrow-range vector " << i;

    auto from_large = ASSERT_RESULT(large->Search(wide_range[i], MakeSearchOptions(1, 200)));
    ASSERT_EQ(from_large.size(), 1);
    ASSERT_EQ(from_large.front().distance, 0.0f) << "wide-range vector " << i;
  }

  // Distances the two chunks report are directly comparable, which is what lets VectorLSM merge
  // them: reranking puts both into the metric's units regardless of their scales.
  const auto small_self = small->Distance(narrow_range[0], narrow_range[1]);
  const auto large_self = large->Distance(wide_range[0], wide_range[1]);
  // wide_range is narrow_range scaled by 100, so L2 squared is 10000x.
  ASSERT_LE(std::fabs(large_self / small_self - 10000.0) / 10000.0, 0.01)
      << "small: " << small_self << ", large: " << large_self;
}

// End to end through the wrapper, and the reason the encoding is shippable: int8 traversal alone
// loses recall, and reranking recovers it to float16's level.
TEST_F(YbHnswStorageWrapperTest, Int8SearchAgreesWithFullPrecision) {
  constexpr size_t kNumVectors = 4000;
  constexpr size_t kNumQueries = 200;
  constexpr size_t kMaxResults = 10;

  dimensions_ = 64;
  GenerateVectors(kNumVectors);

  auto f32 = BuildAndReload(VectorStorageKind::kFloat32, "search_f32.yb_hnsw");
  auto f16 = BuildAndReload(VectorStorageKind::kFloat16, "search_f16.yb_hnsw");
  auto i8 = BuildAndReloadInt8("search_i8.yb_hnsw");
  auto i8_bare = BuildAndReload(VectorStorageKind::kInt8, "search_i8_bare.yb_hnsw");

  size_t common_f16 = 0, common_i8 = 0, common_bare = 0;
  for (size_t i = 0; i != kNumQueries; ++i) {
    auto query = RandomVector();
    auto options = MakeSearchOptions(kMaxResults);
    auto from_f32 = ASSERT_RESULT(f32->Search(query, options));
    ASSERT_EQ(from_f32.size(), kMaxResults);

    std::set<VectorId> f32_ids;
    for (const auto& entry : from_f32) {
      f32_ids.insert(entry.vector_id);
    }

    for (auto [index, common] : std::initializer_list<std::pair<IndexPtr*, size_t*>>{
             {&f16, &common_f16}, {&i8, &common_i8}, {&i8_bare, &common_bare}}) {
      auto results = ASSERT_RESULT((*index)->Search(query, options));
      ASSERT_EQ(results.size(), kMaxResults);
      for (const auto& entry : results) {
        *common += f32_ids.contains(entry.vector_id);
        ASSERT_TRUE(std::isfinite(entry.distance)) << entry;
      }
    }
  }

  const double denominator = kNumQueries * kMaxResults;
  const double f16_overlap = common_f16 / denominator;
  const double i8_overlap = common_i8 / denominator;
  const double bare_overlap = common_bare / denominator;
  LOG(INFO) << "top-" << kMaxResults << " overlap with float32: float16 " << f16_overlap
            << ", int8 + float16 rerank " << i8_overlap << ", int8 alone " << bare_overlap;

  ASSERT_GE(f16_overlap, 0.9);
  // Reranking cannot beat the encoding it reranks with, so float16 is the ceiling. That the
  // rerank ran at all is asserted below by the units of the distance rather than by comparing
  // recall against `bare`, whose gap depends on the data.
  ASSERT_GE(i8_overlap, f16_overlap - 0.02);

  // What proves the rerank ran, deterministically: each reported distance must be exactly what
  // the float16 index gives for the same pair, because that is the computation MakeResult
  // performs. Without a rerank tier the traversal's quantized distance is reported instead.
  std::map<VectorId, size_t> index_by_id;
  for (size_t i = 0; i != ids_.size(); ++i) {
    index_by_id.emplace(ids_[i], i);
  }
  for (size_t i = 0; i != 20; ++i) {
    auto query = RandomVector();
    auto options = MakeSearchOptions(kMaxResults);
    auto reranked = ASSERT_RESULT(i8->Search(query, options));
    auto quantized = ASSERT_RESULT(i8_bare->Search(query, options));
    ASSERT_EQ(reranked.size(), kMaxResults);
    ASSERT_EQ(quantized.size(), kMaxResults);

    for (const auto& entry : reranked) {
      auto it = index_by_id.find(entry.vector_id);
      ASSERT_NE(it, index_by_id.end()) << entry;
      ASSERT_EQ(entry.distance, f16->Distance(query, vectors_[it->second]))
          << "reported distance is not the float16 metric's: " << entry;
    }
    const auto& nearest = quantized.front();
    auto it = index_by_id.find(nearest.vector_id);
    ASSERT_NE(it, index_by_id.end()) << nearest;
    ASSERT_NE(nearest.distance, f16->Distance(query, vectors_[it->second]))
        << "un-reranked search reported a float16 distance: " << nearest;
  }
}

}  // namespace yb::ann_methods
