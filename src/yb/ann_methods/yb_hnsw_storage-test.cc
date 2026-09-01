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
// Covers narrowed coordinate storage through the VectorIndexIf wrapper stack -- the path a
// VectorLSM chunk actually takes: build in RAM, SaveToFile, then Create(kLoad) + LoadFromFile.
// The interesting parts are the ones that read stored records back out: iteration, which is how
// compaction consumes source chunks, and Distance, which callers reach with full-precision
// vectors.

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

  HNSWOptions Options(VectorStorageKind storage_kind) const {
    HNSWOptions options;
    options.dimensions = dimensions_;
    options.num_neighbors_per_vertex = 16;
    options.num_neighbors_per_vertex_base = 32;
    options.ef_construction = 100;
    options.storage_kind = storage_kind;
    return options;
  }

  // Builds an in-RAM hnswlib chunk, flushes it, then reopens it the way a restart would: a fresh
  // instance from the traits factory that has only ever seen the file.
  IndexPtr BuildAndReload(VectorStorageKind storage_kind, const std::string& name) {
    auto options = Options(storage_kind);
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

// VectorLSM's MergingIterator reads every source chunk through this iterator on every
// compaction. Reading float32-wide from a float16 record would run off the end of each record
// into the next one, so this checks both that iteration terminates correctly and that the values
// it yields round-trip.
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

}  // namespace yb::ann_methods
