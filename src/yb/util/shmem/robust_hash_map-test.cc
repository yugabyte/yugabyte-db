// Copyright (c) YugaByte, Inc.
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
#include <functional>
#include <unordered_map>
#include <span>
#include <string_view>

#include "yb/util/math_util.h"
#include "yb/util/random_util.h"
#include "yb/util/shmem/reserved_address_segment.h"
#include "yb/util/shmem/robust_hash_map.h"
#include "yb/util/shmem/shared_mem_allocator.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using namespace std::literals;

namespace yb {

class RobustHashMapTest : public YBTest { };

TEST_F(RobustHashMapTest, TestSimple) {
  RobustHashMap<int, int> map;

  ASSERT_TRUE(map.try_emplace(1, 2).second);

  ASSERT_FALSE(map.empty());
  ASSERT_EQ(map.size(), 1);

  ASSERT_TRUE(map.contains(1));
  ASSERT_EQ(map.count(1), 1);

  auto itr = map.find(1);
  ASSERT_EQ(itr, map.begin());
  ASSERT_NE(itr, map.end());
  ASSERT_EQ(itr->first, 1);
  ASSERT_EQ(itr->second, 2);

  map.erase(1);
  ASSERT_TRUE(map.empty());
  ASSERT_EQ(map.size(), 0);
  ASSERT_FALSE(map.contains(1));
  ASSERT_EQ(map.count(1), 0);

  map.erase(map.try_emplace(1, 2).first);
  ASSERT_TRUE(map.empty());
  ASSERT_EQ(map.size(), 0);
  ASSERT_FALSE(map.contains(1));
  ASSERT_EQ(map.count(1), 0);

  itr = map.find(1);
  ASSERT_EQ(itr, map.begin());
  ASSERT_EQ(itr, map.end());

  map.insert(std::make_pair(10, 11));
  ASSERT_EQ(map.find(10)->second, 11);

  size_t old_capacity = map.capacity();
  map.reserve(old_capacity + 1);
  ASSERT_GT(map.capacity(), old_capacity);
}

TEST_F(RobustHashMapTest, TestLoad) {
  constexpr size_t kEntries = 100000;

  RobustHashMap<size_t, size_t> map;
  std::unordered_map<size_t, size_t> reference;

  for (size_t i = 0; i < kEntries; ++i) {
    map.try_emplace(i, -i);
    reference.try_emplace(i, -i);

    ASSERT_EQ(map.size(), i + 1);
    ASSERT_GE(map.capacity(), i + 1);
  }

  ASSERT_EQ(map.size(), reference.size());

  std::unordered_map<size_t, size_t> reconstructed;
  for (const auto& [key, value] : map) {
    reconstructed[key] = value;
  }

  ASSERT_EQ(reconstructed, reference);
}

TEST_F(RobustHashMapTest, TestDelete) {
  constexpr size_t kEntries = 100000;

  RobustHashMap<size_t, size_t> map;
  for (size_t i = 0; i < kEntries; ++i) {
    map.try_emplace(i, -i);
  }
  ASSERT_EQ(map.size(), kEntries);

  for (size_t i = 0; i < kEntries; ++i) {
    map.erase(i);
  }
  ASSERT_EQ(map.size(), 0);

  for (size_t i = 0; i < kEntries; ++i) {
    map.try_emplace(i, i);
  }
  ASSERT_EQ(map.size(), kEntries);

  for (size_t i = 0; i < kEntries; ++i) {
    ASSERT_EQ(map.find(i)->second, i);
  }

  map.clear();
  ASSERT_EQ(map.size(), 0);
  map.clear();
  ASSERT_EQ(map.size(), 0);
}

TEST_F(RobustHashMapTest, TestNoopCleanup) {
  constexpr size_t kIterations = 100000;

  RobustHashMap<size_t, size_t> map;
  for (size_t i = 0; i < kIterations; ++i) {
    if (i % 3 == 2) {
      map.erase(i - 1);
    } else {
      map.try_emplace(i, -i);
    }
    map.CleanupAfterCrash();
  }

  for (size_t i = 0; i < kIterations; i += 3) {
    ASSERT_EQ(map.find(i)->second, -i);
  }
  ASSERT_EQ(map.size(), ceil_div<size_t>(kIterations, 3));
}

class RobustHashMapCrashTest : public RobustHashMapTest {
 public:
  static constexpr size_t kSimpleWorkloadNumEntries = 16;
  static constexpr size_t kSimpleWorkloadMultiplier = 100;

  // Hash function to ensure simple workload gets entries in the same bin.
  struct Hash {
    size_t operator()(size_t x) const {
      return x % (kSimpleWorkloadNumEntries * kSimpleWorkloadMultiplier / 2);
    }
  };

  using Entry = std::pair<const size_t, size_t>;
  using Allocator = SharedMemoryAllocator<Entry>;
  using Map = RobustHashMap<size_t, size_t, Hash, std::equal_to<size_t>, Allocator>;

  std::string GenerateAllocatorPrefix() {
    return "test-" + RandomHumanReadableString(8);
  }

  void SetUp() override {
    ASSERT_OK(SetupAllocator());
    ASSERT_OK(CreateNewMap());
  }

  Status SetupAllocator() {
    auto allocator_prefix = GenerateAllocatorPrefix();
    address_segment_ = VERIFY_RESULT(AddressSegmentNegotiator::ReserveWithoutNegotiation());
    auto prepare_state = VERIFY_RESULT(backing_allocator_.Prepare(
        allocator_prefix, 0 /* user_data_size */));
    return backing_allocator_.InitOwner(address_segment_, std::move(prepare_state));
  }

  Status CreateNewMap() {
    LOG(INFO) << "Creating map";
    Allocator allocator(backing_allocator_);
    auto map = VERIFY_RESULT(SharedMemoryAllocator<Map>(backing_allocator_).Allocate());
    map_ = new (map) Map(allocator);
    return Status::OK();
  }

  Status TestCleanupCrash(std::string_view crash_point) {
    return ForkAndRunToCrashPoint([this] {
      map_->CleanupAfterCrash();
    }, crash_point);
  }

  Status PerformCrashTest(
      std::function<void(void)> operation,
      std::string_view initial_crash_point,
      std::initializer_list<std::string_view> recovery_crash_points) {
    map_->TEST_DumpMap();
    LOG(INFO) << "Running to initial crash point " << initial_crash_point;
    RETURN_NOT_OK(ForkAndRunToCrashPoint(operation, initial_crash_point));
    map_->TEST_DumpMap();
    for (std::string_view crash_point : recovery_crash_points) {
      LOG(INFO) << "Running to cleanup crash point " << crash_point;
      RETURN_NOT_OK(TestCleanupCrash(crash_point));
      map_->TEST_DumpMap();
    }
    LOG(INFO) << "Final clean up";
    map_->CleanupAfterCrash();
    map_->TEST_DumpMap();
    return map_->TEST_CheckRecoveryState();
  }

  void RunSimpleWorkload(size_t start_key) {
    size_t initial_size = map_->size();
    for (size_t i = 0; i < kSimpleWorkloadNumEntries; ++i) {
      map_->try_emplace(start_key + kSimpleWorkloadMultiplier * i, kSimpleWorkloadMultiplier * i);
    }
    CheckSimpleWorkload(initial_size + kSimpleWorkloadNumEntries, start_key);
  }

  void CheckSimpleWorkload(size_t expected_size, size_t start_key) {
    ASSERT_EQ(map_->size(), expected_size);
    size_t found_entries = 0;
    for (const auto& [key, value] : *map_) {
      if (start_key <= key &&
          key < start_key + kSimpleWorkloadMultiplier * kSimpleWorkloadNumEntries) {
        ASSERT_EQ(value, key - start_key);
      }
      ++found_entries;
    }
    ASSERT_EQ(map_->size(), found_entries);
  }

  void PerformResizeCrashTest(
      std::initializer_list<std::string_view> crash_points,
      std::initializer_list<std::string_view> recovery_crash_points,
      bool initial_empty,
      bool expect_rollback) {
    for (std::string_view crash_point : crash_points) {
      LOG(INFO) << "Performing resize crash test using crash point: " << crash_point;
      if (!initial_empty) {
        ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
      }
      size_t old_size = map_->size();
      size_t old_capacity = map_->capacity();
      size_t min_new_capacity = old_capacity + 10;
      ASSERT_OK(PerformCrashTest(
          [this, min_new_capacity] { map_->reserve(min_new_capacity); },
          crash_point,
          recovery_crash_points));
      if (expect_rollback) {
        ASSERT_EQ(map_->capacity(), old_capacity);
      } else {
        ASSERT_GT(map_->capacity(), old_capacity);
      }
      ASSERT_NO_FATALS(CheckSimpleWorkload(old_size, 0 /* start_key */));
      ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
      map_->clear();
    }
  }

  ReservedAddressSegment address_segment_;
  SharedMemoryBackingAllocator backing_allocator_;
  Map* map_;

  std::string crash_point_;
  size_t crash_recovery_upto_;
};

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestInsertCrashBeforeInProgressSaved)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  size_t old_size = map_->size();
  map_->reserve(old_size + 1);

  ASSERT_OK(PerformCrashTest(
      [this] { map_->try_emplace(-1, 2); },
      "RobustHashMap::DoInsert:1"sv,
      {}));

  ASSERT_EQ(map_->size(), old_size);
  ASSERT_EQ(map_->find(-1), map_->end());
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestReplayableInsert)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  size_t old_size = map_->size();
  map_->reserve(old_size + 1);

  ASSERT_OK(PerformCrashTest(
      [this] { map_->try_emplace(-1, 2); },
      "RobustHashMap::DoInsert:2"sv,
      {
        "RobustHashMap::Cleanup::Insert:1"sv,
        "RobustHashMap::DoInsert:1"sv,
        "RobustHashMap::DoInsert:2"sv,
        "RobustHashMap::DoInsert:3"sv,
      }));
  ASSERT_EQ(map_->size(), old_size + 1);
  ASSERT_NE(map_->find(-1), map_->end());
  ASSERT_EQ(map_->find(-1)->second, 2);
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestPartialInsertRecovery)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  size_t old_size = map_->size();
  map_->reserve(old_size + 1);

  ASSERT_OK(PerformCrashTest(
      [this] { map_->try_emplace(-1, 2); },
      "RobustHashMap::DoInsert:3"sv,
      {
        "RobustHashMap::Cleanup::Insert:1"sv,
        "RobustHashMap::Cleanup::InsertDelete:1"sv,
        "RobustHashMap::Cleanup::InsertDelete:2"sv,
      }));
  ASSERT_EQ(map_->size(), old_size + 1);
  ASSERT_NE(map_->find(-1), map_->end());
  ASSERT_EQ(map_->find(-1)->second, 2);
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestInsertCrashBeforeInProgressCleared)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  size_t old_size = map_->size();
  map_->reserve(old_size + 1);

  ASSERT_OK(PerformCrashTest(
      [this] { map_->try_emplace(-1, 2); },
      "RobustHashMap::DoInsert:4"sv,
      {}));
  ASSERT_EQ(map_->size(), old_size + 1);
  ASSERT_NE(map_->find(-1), map_->end());
  ASSERT_EQ(map_->find(-1)->second, 2);
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestDeleteCrashBeforeInProgressSaved)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  map_->try_emplace(-1, 2);
  size_t old_size = map_->size();

  ASSERT_OK(PerformCrashTest(
      [this] { map_->erase(-1); },
      "RobustHashMap::DoDelete:1"sv,
      {}));
  ASSERT_EQ(map_->size(), old_size);
  ASSERT_NE(map_->find(-1), map_->end());
  ASSERT_EQ(map_->find(-1)->second, 2);
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));

  ASSERT_OK(CreateNewMap());
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  map_->try_emplace(-1, 2);
  old_size = map_->size();

  ASSERT_OK(PerformCrashTest(
      [this] { map_->erase(-1); },
      "RobustHashMap::DoDelete:2"sv,
      {}));
  ASSERT_EQ(map_->size(), old_size);
  ASSERT_NE(map_->find(-1), map_->end());
  ASSERT_EQ(map_->find(-1)->second, 2);
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestReplayableDelete)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  map_->try_emplace(-1, 2);
  size_t old_size = map_->size();

  ASSERT_OK(PerformCrashTest(
      [this] { map_->erase(-1); },
      "RobustHashMap::DoDelete:3"sv,
      {
        "RobustHashMap::Cleanup::Delete:1"sv,
        "RobustHashMap::DoDelete:1"sv,
        "RobustHashMap::DoDelete:2"sv,
        "RobustHashMap::DoDelete:3"sv,
        "RobustHashMap::DoDelete:4"sv,
      }));
  ASSERT_EQ(map_->size(), old_size - 1);
  ASSERT_EQ(map_->find(-1), map_->end());
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestDeleteCrashBeforeInProgressCleared)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  map_->try_emplace(-1, 2);
  size_t old_size = map_->size();

  ASSERT_OK(PerformCrashTest(
      [this] { map_->erase(-1); },
      "RobustHashMap::DoDelete:4"sv,
      {}));
  ASSERT_EQ(map_->size(), old_size - 1);
  ASSERT_EQ(map_->find(-1), map_->end());
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));

  ASSERT_OK(CreateNewMap());
  ASSERT_NO_FATALS(RunSimpleWorkload(0 /* start_key */));
  map_->try_emplace(-1, 2);
  ASSERT_OK(PerformCrashTest(
      [this] { map_->erase(-1); },
      "RobustHashMap::DoDelete:5"sv,
      {}));
  ASSERT_EQ(map_->size(), old_size - 1);
  ASSERT_EQ(map_->find(-1), map_->end());
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestClearCrashReplayCleanup)) {
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
  map_->try_emplace(-1, 2);
  ASSERT_OK(PerformCrashTest(
      [this] { map_->clear(); },
      "RobustHashMap::DoClear:1"sv,
      {
        "RobustHashMap::Cleanup::Clear:1",
      }));
  ASSERT_EQ(map_->size(), 0);
  ASSERT_EQ(map_->find(-1), map_->end());
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));

  map_->try_emplace(-1, 2);
  ASSERT_OK(PerformCrashTest(
      [this] { map_->clear(); },
      "RobustHashMap::DoClear:2"sv,
      {
        "RobustHashMap::Cleanup::Clear:1",
      }));
  ASSERT_EQ(map_->size(), 0);
  ASSERT_EQ(map_->find(-1), map_->end());
  ASSERT_NO_FATALS(RunSimpleWorkload(1000000 /* start_key */));
}

TEST_F(RobustHashMapCrashTest,
       YB_DEBUG_ONLY_TEST(TestResizeFromZeroCrashBeforeNewTableSaved)) {
  ASSERT_NO_FATALS(PerformResizeCrashTest(
      {
        "RobustHashMap::DoResizeFromZero:1",
      },
      {
        "RobustHashMap::Cleanup::Clear:1",
      },
      true /* initial_empty */,
      true /* expect_rollback */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestResizeCrashBeforeNewTableSaved)) {
  ASSERT_NO_FATALS(PerformResizeCrashTest(
      {
        "RobustHashMap::DoResize:1",
      },
      {},
      false /* initial_empty */,
      true /* expect_rollback */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestResizeCrashDuringRehash)) {
  ASSERT_NO_FATALS(PerformResizeCrashTest(
      {
        "RobustHashMap::DoResize:2",
        "RobustHashMap::DoRehash:1",
        "RobustHashMap::DoResize:3",
        "RobustHashMap::DoResize:4",
      },
      {
        "RobustHashMap::DoResize:3",
        "RobustHashMap::DoRehash:1",
        "RobustHashMap::DoResize:4",
        "RobustHashMap::DoResize:5",
        "RobustHashMap::DoResize:6",
      },
      false /* initial_empty */,
      false /* expect_rollback */));
}

TEST_F(RobustHashMapCrashTest, YB_DEBUG_ONLY_TEST(TestResizeCrashDuringCleanup)) {
  ASSERT_NO_FATALS(PerformResizeCrashTest(
      {
        "RobustHashMap::DoResize:5",
        "RobustHashMap::DoResize:6",
      },
      {
        "RobustHashMap::DoResize:5",
        "RobustHashMap::DoResize:6",
      },
      false /* initial_empty */,
      false /* expect_rollback */));
}

} // namespace yb
