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
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/ann_methods/hnswlib_wrapper.h"

#include "yb/hnsw/vector_index_test_base.h"

#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"

#include "yb/vector_index/vector_index_if.h"

DECLARE_uint64(yb_hnsw_max_block_size);

using namespace yb::size_literals;

namespace yb::ann_methods {

using vector_index::DistanceKind;
using vector_index::FactoryMode;
using vector_index::HNSWOptions;
using vector_index::SearchOptions;
using vector_index::StoreVectorPayload;
using vector_index::VectorId;
using vector_index::VectorIndexIfPtr;
using vector_index::VectorIndexTraitsPtr;

// Checks hnswlib chunks and the YbHnsw chunks they are saved to.
class HnswlibTenantTest : public hnsw::VectorIndexTestBase {
 protected:
  static constexpr size_t kDimensions = 16;
  static constexpr size_t kNumTenants = 8;
  static constexpr size_t kVectorsPerTenant = 200;
  static constexpr size_t kNumVectors = kNumTenants * kVectorsPerTenant;
  static constexpr size_t kMaxResults = 10;
  static constexpr size_t kEf = 64;

  struct Entry {
    VectorId id;
    FloatVector vector;
    std::string tenant;
  };

  void SetUp() override {
    VectorIndexTestBase::SetUp();
    const HNSWOptions options = {
        .dimensions = kDimensions,
        .num_neighbors_per_vertex = 8,
        .num_neighbors_per_vertex_base = 16,
        .ef_construction = 64,
        .distance_kind = DistanceKind::kL2Squared,
    };
    // The payload of a vector in this test is its tenant.
    traits_ = ASSERT_RESULT((CreateHnswlibIndexTraits<FloatVector, float>(
        block_cache_, options, /* mem_tracker= */ nullptr,
        [](Slice payload) -> Result<Slice> { return payload; })));
    index_ = traits_->Create(FactoryMode::kCreate, StoreVectorPayload::kTrue);
    ASSERT_OK(index_->Reserve(
        kNumVectors, /* max_concurrent_inserts= */ 8, /* max_concurrent_reads= */ 8,
        rocksdb::Cache::ReservationMode::kAlways));

    // All tenants share the same distribution, so their vectors are close to each other.
    std::uniform_real_distribution<float> distribution(0, 1);
    for (size_t i = 0; i != kNumVectors; ++i) {
      FloatVector vector(kDimensions);
      for (auto& coordinate : vector) {
        coordinate = distribution(ThreadLocalRandom());
      }
      entries_.push_back(Entry {
        .id = VectorId::GenerateRandom(),
        .vector = std::move(vector),
        .tenant = TenantName(i / kVectorsPerTenant),
      });
    }
  }

  std::string TenantName(size_t tenant_idx) const {
    return Format("tenant_$0", tenant_idx) + tenant_suffix_;
  }

  Status Insert(const Entry& entry) {
    return index_->Insert(entry.id, entry.vector, Slice(entry.tenant));
  }

  static float Distance(const FloatVector& lhs, const FloatVector& rhs) {
    float result = 0;
    for (size_t i = 0; i != lhs.size(); ++i) {
      result += (lhs[i] - rhs[i]) * (lhs[i] - rhs[i]);
    }
    return result;
  }

  // Exact top kMaxResults among the vectors of the tenant.
  std::unordered_set<VectorId> BruteForce(const FloatVector& query, const std::string& tenant) {
    std::vector<std::pair<float, VectorId>> candidates;
    for (const auto& entry : entries_) {
      if (entry.tenant == tenant) {
        candidates.emplace_back(Distance(query, entry.vector), entry.id);
      }
    }
    std::partial_sort(
        candidates.begin(), candidates.begin() + kMaxResults, candidates.end());
    std::unordered_set<VectorId> result;
    for (size_t i = 0; i != kMaxResults; ++i) {
      result.insert(candidates[i].second);
    }
    return result;
  }

  // Checks that a tenant search returns only vectors of the tenant with high recall, and that
  // vectors are reachable from their tenant entry points.
  void CheckSearches(const vector_index::VectorIndexIf<FloatVector, float>& index) {
    std::uniform_real_distribution<float> distribution(0, 1);
    size_t found = 0;
    constexpr size_t kQueriesPerTenant = 10;
    for (size_t tenant_idx = 0; tenant_idx != kNumTenants; ++tenant_idx) {
      const auto tenant = TenantName(tenant_idx);
      for (size_t i = 0; i != kQueriesPerTenant; ++i) {
        FloatVector query(kDimensions);
        for (auto& coordinate : query) {
          coordinate = distribution(ThreadLocalRandom());
        }
        auto results = ASSERT_RESULT(index.Search(query, SearchOptions {
          .max_num_results = kMaxResults,
          .ef = kEf,
          .tenant = Slice(tenant),
        }));
        ASSERT_EQ(results.size(), kMaxResults);
        auto expected = BruteForce(query, tenant);
        for (const auto& result : results) {
          ASSERT_EQ(result.payload.AsSlice(), Slice(tenant));
          found += expected.contains(result.vector_id);
        }
      }
    }
    constexpr size_t kExpected = kNumTenants * kQueriesPerTenant * kMaxResults;
    LOG(INFO) << "Recall: " << found * 1.0 / kExpected;
    ASSERT_GE(found, kExpected * 0.95);

    // HNSW pruning could leave a few vectors without incoming edges, while a tenant cut off from
    // its entry point would leave many.
    size_t num_unreachable = 0;
    for (const auto& entry : entries_) {
      auto results = ASSERT_RESULT(index.Search(entry.vector, SearchOptions {
        .max_num_results = 1,
        .ef = kEf,
        .tenant = Slice(entry.tenant),
      }));
      ASSERT_EQ(results.size(), 1U);
      num_unreachable += results.front().vector_id != entry.id;
    }
    ASSERT_LE(num_unreachable, kNumVectors / 100);

    auto results = ASSERT_RESULT(index.Search(entries_.front().vector, SearchOptions {
      .max_num_results = kMaxResults,
      .ef = kEf,
      .tenant = Slice("missing_tenant"),
    }));
    ASSERT_TRUE(results.empty());
  }

  // Checks the inserted index, the chunk it is saved to, and the chunk loaded from the file.
  void CheckIndexAndChunks() {
    ASSERT_NO_FATALS(CheckSearches(*index_));
    const auto path = GetTestPath("chunk");
    auto saved = ASSERT_RESULT(index_->SaveToFile(path));
    ASSERT_NE(saved, nullptr);
    ASSERT_NO_FATALS(CheckSearches(*saved));
    auto loaded = traits_->Create(FactoryMode::kLoad, StoreVectorPayload::kTrue);
    ASSERT_OK(loaded->LoadFromFile(path, /* max_concurrent_reads= */ 1));
    ASSERT_NO_FATALS(CheckSearches(*loaded));
  }

  VectorIndexTraitsPtr<FloatVector, float> traits_;
  VectorIndexIfPtr<FloatVector, float> index_;
  std::vector<Entry> entries_;
  std::string tenant_suffix_;
};

// Vectors of a tenant are inserted back to back, as by a backfill of a table whose primary key
// starts with the tenant.
TEST_F(HnswlibTenantTest, GroupedByTenant) {
  for (const auto& entry : entries_) {
    ASSERT_OK(Insert(entry));
  }
  ASSERT_NO_FATALS(CheckIndexAndChunks());
}

TEST_F(HnswlibTenantTest, RandomOrder) {
  std::shuffle(entries_.begin(), entries_.end(), ThreadLocalRandom());
  for (const auto& entry : entries_) {
    ASSERT_OK(Insert(entry));
  }
  ASSERT_NO_FATALS(CheckIndexAndChunks());
}

// Long tenants with small blocks spread the YbHnsw tenant directory over several blocks.
TEST_F(HnswlibTenantTest, ManyTenantDirectoryBlocks) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_hnsw_max_block_size) = 1_KB;
  tenant_suffix_ = std::string(300, 'x');
  for (size_t i = 0; i != entries_.size(); ++i) {
    entries_[i].tenant = TenantName(i / kVectorsPerTenant);
  }
  std::shuffle(entries_.begin(), entries_.end(), ThreadLocalRandom());
  for (const auto& entry : entries_) {
    ASSERT_OK(Insert(entry));
  }
  ASSERT_NO_FATALS(CheckIndexAndChunks());
}

// The first vectors of all tenants are inserted concurrently, so concurrent first inserts of a
// tenant must not create separate clusters.
TEST_F(HnswlibTenantTest, ConcurrentInserts) {
  std::vector<size_t> order(kNumVectors);
  for (size_t i = 0; i != kNumVectors; ++i) {
    // Round robin over tenants.
    order[i] = (i % kNumTenants) * kVectorsPerTenant + i / kNumTenants;
  }
  ASSERT_OK(ProcessInParallel(
      /* num_threads= */ 8, static_cast<size_t>(0), kNumVectors,
      [this, &order](size_t i) { return Insert(entries_[order[i]]); }));
  ASSERT_NO_FATALS(CheckIndexAndChunks());
}

} // namespace yb::ann_methods
