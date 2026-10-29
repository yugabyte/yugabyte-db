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
#include <atomic>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/ann_methods/hnswlib_wrapper.h"

#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"
#include "yb/util/thread_holder.h"

#include "yb/vector_index/vector_index_if.h"

namespace yb::ann_methods {

using vector_index::DistanceKind;
using vector_index::FactoryMode;
using vector_index::HNSWOptions;
using vector_index::SearchOptions;
using vector_index::StoreVectorPayload;
using vector_index::VectorId;
using vector_index::VectorIndexIfPtr;

class HnswlibTenantTest : public YBTest {
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

  // The payload of a vector in this test is its tenant.
  static Result<Slice> TenantFromPayload(Slice payload) {
    return payload;
  }

  static Result<VectorIndexIfPtr<FloatVector, float>> CreateIndex(
      vector_index::VectorTenantExtractor tenant_extractor, size_t capacity) {
    const HNSWOptions options = {
        .dimensions = kDimensions,
        .num_neighbors_per_vertex = 8,
        .num_neighbors_per_vertex_base = 16,
        .ef_construction = 64,
        .distance_kind = DistanceKind::kL2Squared,
    };
    auto traits = VERIFY_RESULT((CreateHnswlibIndexTraits<FloatVector, float>(
        /* block_cache= */ nullptr, options, /* mem_tracker= */ nullptr,
        std::move(tenant_extractor))));
    auto index = traits->Create(FactoryMode::kCreate, StoreVectorPayload::kTrue);
    RETURN_NOT_OK(index->Reserve(
        capacity, /* max_concurrent_inserts= */ 8, /* max_concurrent_reads= */ 8,
        rocksdb::Cache::ReservationMode::kAlways));
    return index;
  }

  void SetUp() override {
    YBTest::SetUp();
    index_ = ASSERT_RESULT(CreateIndex(TenantFromPayload, kNumVectors));

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
        .tenant = Format("tenant_$0", i / kVectorsPerTenant),
      });
    }
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

  // Exact top kMaxResults among the vectors of the tenant, or all vectors when tenant is empty.
  std::unordered_set<VectorId> BruteForce(const FloatVector& query, const std::string& tenant) {
    std::vector<std::pair<float, VectorId>> candidates;
    for (const auto& entry : entries_) {
      if (tenant.empty() || entry.tenant == tenant) {
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

  // Checks that a tenant search returns only vectors of the tenant with high recall, that vectors
  // are reachable from their tenant entry points, and that a search without a tenant covers all
  // tenants.
  void CheckSearches() {
    std::uniform_real_distribution<float> distribution(0, 1);
    size_t found = 0;
    size_t found_all_tenants = 0;
    constexpr size_t kQueriesPerTenant = 10;
    for (size_t tenant_idx = 0; tenant_idx != kNumTenants; ++tenant_idx) {
      const auto tenant = Format("tenant_$0", tenant_idx);
      for (size_t i = 0; i != kQueriesPerTenant; ++i) {
        FloatVector query(kDimensions);
        for (auto& coordinate : query) {
          coordinate = distribution(ThreadLocalRandom());
        }
        auto results = ASSERT_RESULT(index_->Search(query, SearchOptions {
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

        results = ASSERT_RESULT(index_->Search(query, SearchOptions {
          .max_num_results = kMaxResults,
          .ef = kEf,
        }));
        ASSERT_EQ(results.size(), kMaxResults);
        expected = BruteForce(query, /* tenant= */ "");
        for (const auto& result : results) {
          found_all_tenants += expected.contains(result.vector_id);
        }
      }
    }
    constexpr size_t kExpected = kNumTenants * kQueriesPerTenant * kMaxResults;
    LOG(INFO) << "Recall: " << found * 1.0 / kExpected
              << ", recall without tenant: " << found_all_tenants * 1.0 / kExpected;
    ASSERT_GE(found, kExpected * 0.95);
    ASSERT_GE(found_all_tenants, kExpected * 0.95);

    // HNSW pruning could leave a few vectors without incoming edges, while a tenant cut off from
    // its entry point would leave many.
    size_t num_unreachable = 0;
    for (const auto& entry : entries_) {
      auto results = ASSERT_RESULT(index_->Search(entry.vector, SearchOptions {
        .max_num_results = 1,
        .ef = kEf,
        .tenant = Slice(entry.tenant),
      }));
      ASSERT_EQ(results.size(), 1U);
      num_unreachable += results.front().vector_id != entry.id;
    }
    ASSERT_LE(num_unreachable, kNumVectors / 100);

    auto results = ASSERT_RESULT(index_->Search(entries_.front().vector, SearchOptions {
      .max_num_results = kMaxResults,
      .ef = kEf,
      .tenant = Slice("missing_tenant"),
    }));
    ASSERT_TRUE(results.empty());
  }

  VectorIndexIfPtr<FloatVector, float> index_;
  std::vector<Entry> entries_;
};

// Vectors of a tenant are inserted back to back, as by a backfill of a table whose primary key
// starts with the tenant.
TEST_F(HnswlibTenantTest, GroupedByTenant) {
  for (const auto& entry : entries_) {
    ASSERT_OK(Insert(entry));
  }
  ASSERT_NO_FATALS(CheckSearches());
}

TEST_F(HnswlibTenantTest, RandomOrder) {
  std::shuffle(entries_.begin(), entries_.end(), ThreadLocalRandom());
  for (const auto& entry : entries_) {
    ASSERT_OK(Insert(entry));
  }
  ASSERT_NO_FATALS(CheckSearches());
}

// Vectors of all tenants are inserted concurrently, round robin over the tenants.
TEST_F(HnswlibTenantTest, ConcurrentInserts) {
  std::vector<size_t> order(kNumVectors);
  for (size_t i = 0; i != kNumVectors; ++i) {
    // Round robin over tenants.
    order[i] = (i % kNumTenants) * kVectorsPerTenant + i / kNumTenants;
  }
  ASSERT_OK(ProcessInParallel(
      /* num_threads= */ 8, static_cast<size_t>(0), kNumVectors,
      [this, &order](size_t i) { return Insert(entries_[order[i]]); }));
  ASSERT_NO_FATALS(CheckSearches());
}

// Concurrent first inserts of a tenant must agree on its entry point, otherwise the vectors of the
// other inserts are cut off from the tenant. The index already has a vector of another tenant: the
// first insert into an empty index holds the global lock of hnswlib until it is done.
TEST_F(HnswlibTenantTest, ConcurrentFirstInsertsOfTenant) {
  constexpr size_t kNumInserts = 8;
  // A round rarely hits the race, so it is repeated many times.
  constexpr size_t kNumRounds = 2000;
  const Slice tenant("tenant");
  for (size_t round = 0; round != kNumRounds; ++round) {
    auto index = ASSERT_RESULT(CreateIndex(TenantFromPayload, kNumInserts + 1));
    const auto& other = entries_[kNumInserts];
    ASSERT_OK(index->Insert(other.id, other.vector, Slice("other_tenant")));
    // The inserts spin until all of them are started, so that they run together.
    std::atomic<bool> start{false};
    ThreadHolder threads;
    for (size_t i = 0; i != kNumInserts; ++i) {
      threads.AddThreadFunctor([this, &index, &start, tenant, i] {
        while (!start.load(std::memory_order_acquire)) {
        }
        ASSERT_OK(index->Insert(entries_[i].id, entries_[i].vector, tenant));
      });
    }
    start.store(true, std::memory_order_release);
    threads.JoinAll();
    auto results = ASSERT_RESULT(index->Search(entries_.front().vector, SearchOptions {
      .max_num_results = kNumInserts,
      .ef = kEf,
      .tenant = tenant,
    }));
    ASSERT_EQ(results.size(), kNumInserts) << "Round " << round;
  }
}

// A search of a tenant needs an index whose vectors belong to tenants.
TEST_F(HnswlibTenantTest, TenantSearchWithoutTenants) {
  auto index = ASSERT_RESULT(CreateIndex(/* tenant_extractor= */ nullptr, /* capacity= */ 1));
  const auto& entry = entries_.front();
  ASSERT_OK(index->Insert(entry.id, entry.vector, Slice(entry.tenant)));
  ASSERT_NOK(index->Search(entry.vector, SearchOptions {
    .max_num_results = 1,
    .ef = kEf,
    .tenant = Slice(entry.tenant),
  }));
  auto results = ASSERT_RESULT(index->Search(entry.vector, SearchOptions {
    .max_num_results = 1,
    .ef = kEf,
  }));
  ASSERT_EQ(results.size(), 1U);
}

} // namespace yb::ann_methods
