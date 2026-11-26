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

#include <map>
#include <memory>

#include "yb/util/mem_tracker.h"
#include "yb/util/random_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

#include "yb/vector_index/vector_lsm_deletions.h"

namespace yb::vector_index {

using ExpectedDeletions = std::map<VectorId, HybridTime>;

class VectorLSMDeletionMapTest : public YBTest {
 protected:
  // Checks map against expected: lookups, the order of SortedList, size and MinHybridTime.
  void CheckMap(
      const VectorLSMDeletionMap& map, const ExpectedDeletions& expected) {
    auto min_hybrid_time = HybridTime::kInvalid;
    for (const auto& [vector_id, hybrid_time] : expected) {
      ASSERT_EQ(map.Find(vector_id), hybrid_time);
      min_hybrid_time.MakeAtMost(hybrid_time);
    }
    VectorLSMDeletionList expected_list;
    for (const auto& [vector_id, hybrid_time] : expected) {
      expected_list.push_back(VectorLSMDeletion {
        .vector_id = vector_id,
        .hybrid_time = hybrid_time,
      });
    }
    ASSERT_EQ(AsString(map.SortedList()), AsString(expected_list));
    ASSERT_EQ(map.size(), expected.size());
    ASSERT_EQ(map.MinHybridTime(), min_hybrid_time);
  }

  void TestInsertAndFind(size_t num_prefix_bytes);
};

namespace {

VectorLSMDeletion RandomDeletion(size_t num_prefix_bytes) {
  auto vector_id = VectorId::GenerateRandom();
  // Ids that are not random share their leading bytes.
  memset(vector_id.data(), 0, num_prefix_bytes);
  return VectorLSMDeletion {
    .vector_id = vector_id,
    .hybrid_time = HybridTime(RandomUniformInt<uint64_t>(2, 1'000'000'000)),
  };
}

constexpr size_t kNumDeletions = 10000;

} // namespace

void VectorLSMDeletionMapTest::TestInsertAndFind(size_t num_prefix_bytes) {
  constexpr size_t kBatchSize = 100;
  constexpr size_t kNumMissing = 1000;

  VectorLSMDeletionMap map(/* mem_tracker= */ nullptr);
  ASSERT_EQ(map.MinHybridTime(), HybridTime::kInvalid);

  ExpectedDeletions expected;
  VectorLSMDeletionList deletions;
  for (size_t i = 0; i != kNumDeletions; ++i) {
    deletions.push_back(RandomDeletion(num_prefix_bytes));
    expected.emplace(deletions.back().vector_id, deletions.back().hybrid_time);
  }
  for (size_t i = 0; i < kNumDeletions; i += kBatchSize) {
    map.Insert(std::span(deletions).subspan(i, kBatchSize));
  }

  // A repeated deletion changes nothing, even with an earlier time.
  auto repeated = deletions.front();
  repeated.hybrid_time = HybridTime(1);
  map.Insert(std::span(&repeated, 1));

  ASSERT_NO_FATALS(CheckMap(map, expected));
  for (size_t i = 0; i != kNumMissing; ++i) {
    ASSERT_EQ(map.Find(RandomDeletion(num_prefix_bytes).vector_id), HybridTime::kInvalid);
  }
}

// Deletions are found by vector id, and listed in vector id order.
TEST_F(VectorLSMDeletionMapTest, InsertAndFind) {
  TestInsertAndFind(/* num_prefix_bytes= */ 0);
}

// Vector ids that are not random.
TEST_F(VectorLSMDeletionMapTest, CommonPrefix) {
  TestInsertAndFind(/* num_prefix_bytes= */ 8);
}

// The tracker has the shards and the deletions, and the map releases them.
TEST_F(VectorLSMDeletionMapTest, MemTracking) {
  constexpr size_t kNumDeletions = 1000;

  auto mem_tracker = MemTracker::CreateTracker("deletions");
  {
    VectorLSMDeletionMap map(mem_tracker);
    const auto shards_bytes = mem_tracker->consumption();
    ASSERT_GE(shards_bytes, sizeof(map));

    VectorLSMDeletionList deletions;
    for (size_t i = 0; i != kNumDeletions; ++i) {
      deletions.push_back(RandomDeletion(/* num_prefix_bytes= */ 0));
    }
    map.Insert(deletions);
    ASSERT_GE(
        mem_tracker->consumption(),
        shards_bytes + kNumDeletions * sizeof(VectorLSMDeletion));

    // A deletion of a vector already deleted takes no memory.
    const auto consumption = mem_tracker->consumption();
    map.Insert(std::span(deletions).first(1));
    ASSERT_EQ(mem_tracker->consumption(), consumption);
  }
  ASSERT_EQ(mem_tracker->consumption(), 0);
}

// Lookups run while other threads insert, and find every deletion inserted before them. The
// shards the writers grow add their memory to the tracker concurrently.
TEST_F(VectorLSMDeletionMapTest, Concurrent) {
  constexpr size_t kNumWriters = 4;
  constexpr size_t kNumReaders = 4;
  constexpr size_t kDeletionsPerWriter = 20000;
  constexpr size_t kBatchSize = 16;

  auto mem_tracker = MemTracker::CreateTracker("deletions");
  auto map_holder = std::make_unique<VectorLSMDeletionMap>(mem_tracker);
  auto& map = *map_holder;
  std::vector<VectorLSMDeletionList> deletions(kNumWriters);
  // The number of deletions of each writer inserted so far.
  std::vector<std::atomic<size_t>> num_inserted(kNumWriters);
  for (auto& writer_deletions : deletions) {
    for (size_t i = 0; i != kDeletionsPerWriter; ++i) {
      writer_deletions.push_back(RandomDeletion(/* num_prefix_bytes= */ 0));
    }
  }

  std::atomic<size_t> num_writers_running{kNumWriters};
  std::atomic<size_t> num_lookups{0};
  TestThreadHolder thread_holder;
  for (size_t writer = 0; writer != kNumWriters; ++writer) {
    thread_holder.AddThreadFunctor(
        [&map, &writer_deletions = deletions[writer], &inserted = num_inserted[writer],
         &num_writers_running] {
      for (size_t i = 0; i < writer_deletions.size(); i += kBatchSize) {
        map.Insert(std::span(writer_deletions).subspan(i, kBatchSize));
        inserted.store(i + kBatchSize, std::memory_order_release);
      }
      --num_writers_running;
    });
  }
  for (size_t reader = 0; reader != kNumReaders; ++reader) {
    thread_holder.AddThreadFunctor([&map, &deletions, &num_inserted, &num_writers_running,
                                    &num_lookups] {
      while (num_writers_running.load(std::memory_order_acquire)) {
        auto writer = RandomUniformInt<size_t>(0, kNumWriters - 1);
        auto inserted = num_inserted[writer].load(std::memory_order_acquire);
        if (!inserted) {
          continue;
        }
        const auto& deletion = deletions[writer][RandomUniformInt<size_t>(0, inserted - 1)];
        ASSERT_EQ(map.Find(deletion.vector_id), deletion.hybrid_time);
        ++num_lookups;
      }
    });
  }
  thread_holder.JoinAll();
  LOG(INFO) << "Lookups: " << num_lookups.load();

  ExpectedDeletions expected;
  for (const auto& writer_deletions : deletions) {
    for (const auto& deletion : writer_deletions) {
      expected.emplace(deletion.vector_id, deletion.hybrid_time);
    }
  }
  ASSERT_NO_FATALS(CheckMap(map, expected));
  ASSERT_GE(mem_tracker->consumption(), expected.size() * sizeof(VectorLSMDeletion));
  map_holder.reset();
  ASSERT_EQ(mem_tracker->consumption(), 0);
}

}  // namespace yb::vector_index
