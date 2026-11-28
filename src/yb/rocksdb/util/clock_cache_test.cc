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

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/cache_metrics.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;
using namespace yb::size_literals;

METRIC_DECLARE_entity(server);

namespace rocksdb {

namespace {

constexpr QueryId kTestQueryId = 1;
constexpr size_t kCharge = 1_KB;

std::string EncodeKey(int k) {
  // Longer than 16 bytes, like real block cache keys.
  std::string result = "block-cache-key-prefix/";
  PutFixed32(&result, k);
  return result;
}

// Values remember their key and are never freed during a test, so that a deleter running twice,
// or a lookup returning a deleted value, is detected instead of being undefined behavior.
struct TestValue {
  static constexpr uint64_t kAlive = 0xA11CEA11CEA11CEULL;
  static constexpr uint64_t kDead = 0xDEADDEADDEADDEADULL;

  explicit TestValue(int k) : key(k) {}

  std::atomic<uint64_t> state{kAlive};
  const int key;
};

class ValueTracker {
 public:
  TestValue* New(int key) {
    std::lock_guard lock(mutex_);
    return &values_.emplace_back(key);
  }

  static void Deleter(const Slice& key, void* value) {
    auto* v = static_cast<TestValue*>(value);
    auto expected = TestValue::kAlive;
    if (!v->state.compare_exchange_strong(expected, TestValue::kDead)) {
      LOG(FATAL) << "Value of key " << v->key << " deleted twice";
    }
    current_->deleted_.fetch_add(1);
  }

  size_t allocated() {
    std::lock_guard lock(mutex_);
    return values_.size();
  }

  size_t deleted() const { return deleted_.load(); }

  static ValueTracker* current_;

 private:
  std::mutex mutex_;
  std::deque<TestValue> values_;
  std::atomic<size_t> deleted_{0};
};

ValueTracker* ValueTracker::current_ = nullptr;

} // namespace

// The parameter is estimated_entry_charge: 0 for AutoHyperClockTable, otherwise
// FixedHyperClockTable.
class ClockCacheTest : public RocksDBTest, public ::testing::WithParamInterface<size_t> {
 protected:
  void SetUp() override {
    RocksDBTest::SetUp();
    ValueTracker::current_ = &tracker_;
  }

  void TearDown() override {
    cache_.reset();
    // Destroying the cache frees every entry exactly once.
    ASSERT_EQ(tracker_.allocated(), tracker_.deleted());
    ValueTracker::current_ = nullptr;
    RocksDBTest::TearDown();
  }

  std::shared_ptr<Cache> NewCache(
      size_t capacity, int num_shard_bits = 0, bool strict_capacity_limit = false) {
    auto cache = NewHyperClockCache(capacity, num_shard_bits, strict_capacity_limit, GetParam());
    CHECK_NOTNULL(cache.get());
    return cache;
  }

  void UseCache(size_t capacity, int num_shard_bits = 0, bool strict_capacity_limit = false) {
    cache_ = NewCache(capacity, num_shard_bits, strict_capacity_limit);
  }

  Status Insert(int key, size_t charge = kCharge, QueryId query_id = kTestQueryId) {
    return cache_->Insert(
        EncodeKey(key), query_id, tracker_.New(key), charge, &ValueTracker::Deleter);
  }

  Cache::Handle* InsertAndRef(int key, size_t charge = kCharge) {
    Cache::Handle* handle = nullptr;
    EXPECT_OK(cache_->Insert(
        EncodeKey(key), kTestQueryId, tracker_.New(key), charge, &ValueTracker::Deleter,
        &handle));
    return handle;
  }

  // Returns the key stored in the value, or -1 on a miss.
  int Lookup(int key, QueryId query_id = kTestQueryId) {
    auto* handle = cache_->Lookup(EncodeKey(key), query_id);
    if (!handle) {
      return -1;
    }
    auto* value = static_cast<TestValue*>(cache_->Value(handle));
    EXPECT_EQ(value->state.load(), TestValue::kAlive);
    const int result = value->key;
    cache_->Release(handle);
    return result;
  }

  ValueTracker tracker_;
  std::shared_ptr<Cache> cache_;
};

INSTANTIATE_TEST_CASE_P(
    HyperClockCache, ClockCacheTest, ::testing::Values(0, kCharge),
    [](const ::testing::TestParamInfo<size_t>& info) {
      return info.param == 0 ? "Auto" : "Fixed";
    });

TEST_P(ClockCacheTest, HitAndMiss) {
  UseCache(1_MB);
  ASSERT_EQ(-1, Lookup(100));

  ASSERT_OK(Insert(100));
  ASSERT_EQ(100, Lookup(100));
  ASSERT_EQ(-1, Lookup(200));

  ASSERT_OK(Insert(200));
  ASSERT_EQ(100, Lookup(100));
  ASSERT_EQ(200, Lookup(200));
  ASSERT_EQ(-1, Lookup(300));
  ASSERT_EQ(2 * kCharge, cache_->GetUsage());
}

// Unlike the LRU cache, a second insert of a key keeps the first entry and drops the new value.
// The block cache only inserts the same contents again for a key.
TEST_P(ClockCacheTest, InsertExistingKeyKeepsFirstEntry) {
  UseCache(1_MB);
  ASSERT_OK(Insert(100));
  const auto deleted_before = tracker_.deleted();
  ASSERT_OK(cache_->Insert(
      EncodeKey(100), kTestQueryId, tracker_.New(101), kCharge, &ValueTracker::Deleter));
  ASSERT_EQ(100, Lookup(100));
  ASSERT_EQ(deleted_before + 1, tracker_.deleted());
  ASSERT_EQ(kCharge, cache_->GetUsage());

  // With a handle, the caller gets a usable handle that lookups do not see.
  auto* handle = InsertAndRef(100);
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(static_cast<TestValue*>(cache_->Value(handle))->state.load(), TestValue::kAlive);
  ASSERT_EQ(100, Lookup(100));
  const auto deleted_with_handle = tracker_.deleted();
  cache_->Release(handle);
  ASSERT_EQ(deleted_with_handle + 1, tracker_.deleted());
}

TEST_P(ClockCacheTest, Erase) {
  UseCache(1_MB);
  cache_->Erase(EncodeKey(200));
  ASSERT_EQ(0, tracker_.deleted());

  ASSERT_OK(Insert(100));
  ASSERT_OK(Insert(200));
  cache_->Erase(EncodeKey(100));
  ASSERT_EQ(-1, Lookup(100));
  ASSERT_EQ(200, Lookup(200));
  ASSERT_EQ(1, tracker_.deleted());

  cache_->Erase(EncodeKey(100));
  ASSERT_EQ(1, tracker_.deleted());
  ASSERT_EQ(kCharge, cache_->GetUsage());
}

TEST_P(ClockCacheTest, EntriesArePinned) {
  UseCache(1_MB);
  ASSERT_OK(Insert(100));
  auto* h1 = cache_->Lookup(EncodeKey(100), kTestQueryId);
  ASSERT_NE(h1, nullptr);
  ASSERT_EQ(kCharge, cache_->GetPinnedUsage());

  cache_->Erase(EncodeKey(100));
  ASSERT_EQ(-1, Lookup(100));
  ASSERT_EQ(0, tracker_.deleted());
  ASSERT_EQ(100, static_cast<TestValue*>(cache_->Value(h1))->key);

  cache_->Release(h1);
  ASSERT_EQ(1, tracker_.deleted());
  ASSERT_EQ(0, cache_->GetPinnedUsage());
  ASSERT_EQ(0, cache_->GetUsage());
}

TEST_P(ClockCacheTest, UsageAndPinnedUsage) {
  constexpr size_t kCapacity = 1_MB;
  UseCache(kCapacity, 2);
  std::vector<Cache::Handle*> handles;
  for (int i = 0; i != 100; ++i) {
    const size_t charge = kCharge + i;
    if (i % 2) {
      ASSERT_OK(Insert(i, charge));
    } else {
      handles.push_back(InsertAndRef(i, charge));
      ASSERT_EQ(charge, cache_->GetUsage(handles.back()));
    }
  }
  size_t total = 0;
  size_t pinned = 0;
  for (int i = 0; i != 100; ++i) {
    total += kCharge + i;
    pinned += i % 2 ? 0 : kCharge + i;
  }
  ASSERT_EQ(total, cache_->GetUsage());
  ASSERT_EQ(pinned, cache_->GetPinnedUsage());

  // Overfill the cache: usage stays near capacity and the pinned entries stay.
  for (int i = 1000; i != 3000; ++i) {
    ASSERT_OK(Insert(i));
  }
  ASSERT_LE(cache_->GetUsage(), kCapacity + 64 * kCharge);
  ASSERT_EQ(pinned, cache_->GetPinnedUsage());
  for (int i = 0; i < 100; i += 2) {
    ASSERT_EQ(i, Lookup(i));
  }

  for (auto* handle : handles) {
    cache_->Release(handle);
  }
  ASSERT_EQ(0, cache_->GetPinnedUsage());
}

// Entries that keep being looked up survive a stream of entries that are used once.
TEST_P(ClockCacheTest, FrequentlyUsedEntriesSurvive) {
  constexpr int kCapacityEntries = 256;
  UseCache(kCapacityEntries * kCharge);
  constexpr int kHot = 16;
  for (int i = 0; i != kHot; ++i) {
    ASSERT_OK(Insert(i));
  }
  int cold_hits = 0;
  for (int i = kHot; i != kHot + 20 * kCapacityEntries; ++i) {
    ASSERT_OK(Insert(i));
    for (int hot = 0; hot != kHot; ++hot) {
      ASSERT_EQ(hot, Lookup(hot)) << "after inserting " << i;
    }
    cold_hits += Lookup(i - kCapacityEntries / 2) >= 0;
  }
  // Recently inserted entries are mostly still there too.
  ASSERT_GT(cold_hits, 10 * kCapacityEntries);
  ASSERT_LE(cache_->GetUsage(), (kCapacityEntries + 8) * kCharge);
}

TEST_P(ClockCacheTest, PinnedEntriesAreNotEvicted) {
  constexpr int kCapacityEntries = 64;
  UseCache(kCapacityEntries * kCharge);
  auto* pinned = InsertAndRef(7);
  for (int i = 100; i != 100 + 10 * kCapacityEntries; ++i) {
    ASSERT_OK(Insert(i));
  }
  ASSERT_EQ(7, Lookup(7));
  cache_->Release(pinned);
  for (int i = 1000; i != 1000 + 10 * kCapacityEntries; ++i) {
    ASSERT_OK(Insert(i));
  }
  ASSERT_EQ(-1, Lookup(7));
}

TEST_P(ClockCacheTest, NoCacheQueryId) {
  UseCache(1_MB);
  auto* value = tracker_.New(1);
  Cache::Handle* handle = nullptr;
  ASSERT_OK(cache_->Insert(
      EncodeKey(1), kNoCacheQueryId, value, kCharge, &ValueTracker::Deleter, &handle));
  ASSERT_EQ(handle, nullptr);
  ASSERT_EQ(0, cache_->GetUsage());
  ASSERT_EQ(-1, Lookup(1));
  // As with the LRU cache, the caller still owns the value.
  ValueTracker::Deleter(Slice(), value);

  ASSERT_OK(Insert(2));
  ASSERT_EQ(-1, Lookup(2, kNoCacheQueryId));
  ASSERT_EQ(2, Lookup(2, kInMultiTouchId));
}

TEST_P(ClockCacheTest, SetCapacity) {
  constexpr int kEntries = 64;
  UseCache(2 * kEntries * kCharge, 2);
  std::vector<Cache::Handle*> handles;
  for (int i = 0; i != kEntries; ++i) {
    if (i < 8) {
      handles.push_back(InsertAndRef(i));
    } else {
      ASSERT_OK(Insert(i));
    }
  }
  ASSERT_EQ(kEntries * kCharge, cache_->GetUsage());

  // Shrinking evicts unpinned entries right away.
  cache_->SetCapacity(16 * kCharge);
  ASSERT_EQ(16 * kCharge, cache_->GetCapacity());
  ASSERT_LE(cache_->GetUsage(), 16 * kCharge + 4 * kCharge);
  for (int i = 0; i != 8; ++i) {
    ASSERT_EQ(i, Lookup(i));
  }

  cache_->SetCapacity(4 * kEntries * kCharge);
  for (int i = 1000; i != 1000 + 2 * kEntries; ++i) {
    ASSERT_OK(Insert(i));
  }
  ASSERT_GE(cache_->GetUsage(), 2 * kEntries * kCharge);
  for (auto* handle : handles) {
    cache_->Release(handle);
  }
}

TEST_P(ClockCacheTest, StrictCapacityLimit) {
  UseCache(4 * kCharge, 0, /* strict_capacity_limit= */ true);
  ASSERT_TRUE(cache_->HasStrictCapacityLimit());
  std::vector<Cache::Handle*> handles;
  for (int i = 0; i != 4; ++i) {
    handles.push_back(InsertAndRef(i));
    ASSERT_NE(handles.back(), nullptr);
  }

  // Full of pinned entries: insert with a handle fails and leaves the value to the caller.
  auto* value = tracker_.New(10);
  Cache::Handle* handle = reinterpret_cast<Cache::Handle*>(1);
  auto s = cache_->Insert(
      EncodeKey(10), kTestQueryId, value, kCharge, &ValueTracker::Deleter, &handle);
  ASSERT_TRUE(s.IsIncomplete()) << s;
  ASSERT_EQ(handle, nullptr);
  ASSERT_EQ(value->state.load(), TestValue::kAlive);
  ValueTracker::Deleter(Slice(), value);

  // Without a handle, the cache deletes the value.
  const auto deleted_before = tracker_.deleted();
  s = Insert(11);
  ASSERT_TRUE(s.IsIncomplete()) << s;
  ASSERT_EQ(deleted_before + 1, tracker_.deleted());
  ASSERT_EQ(4 * kCharge, cache_->GetUsage());

  for (auto* h : handles) {
    cache_->Release(h);
  }
  ASSERT_OK(Insert(12));
  ASSERT_LE(cache_->GetUsage(), 4 * kCharge);
}

TEST_P(ClockCacheTest, ConsumeAndReleaseSpace) {
  constexpr size_t kCapacity = 64 * kCharge;
  UseCache(kCapacity);
  for (int i = 0; i != 64; ++i) {
    ASSERT_OK(Insert(i));
  }
  ASSERT_EQ(kCapacity, cache_->GetUsage());

  // A reservation within capacity shrinks the space for entries right away.
  ASSERT_TRUE(ASSERT_RESULT(cache_->ConsumeSpace(48 * kCharge, Cache::ReservationMode::kStrict)));
  ASSERT_LE(cache_->GetUsage(), 16 * kCharge);
  ASSERT_EQ(kCapacity, cache_->GetCapacity());

  // kStrict rejects a reservation that does not fit, kAlways applies it.
  ASSERT_NOK(cache_->ConsumeSpace(32 * kCharge, Cache::ReservationMode::kStrict));
  ASSERT_FALSE(ASSERT_RESULT(cache_->ConsumeSpace(32 * kCharge, Cache::ReservationMode::kAlways)));
  ASSERT_EQ(0, cache_->GetUsage());

  cache_->ReleaseSpace(80 * kCharge);
  for (int i = 100; i != 164; ++i) {
    ASSERT_OK(Insert(i));
  }
  ASSERT_GE(cache_->GetUsage(), 60 * kCharge);
}

TEST_P(ClockCacheTest, Evict) {
  UseCache(1_MB, 2);
  std::vector<Cache::Handle*> handles;
  for (int i = 0; i != 100; ++i) {
    if (i < 10) {
      handles.push_back(InsertAndRef(i));
    } else {
      ASSERT_OK(Insert(i));
    }
  }
  ASSERT_GE(cache_->Evict(20 * kCharge), 20 * kCharge);
  ASSERT_LE(cache_->GetUsage(), 80 * kCharge);

  // EvictAll drops every unpinned entry.
  const auto evicted = cache_->EvictAll();
  ASSERT_GT(evicted, 0);
  ASSERT_EQ(10 * kCharge, cache_->GetUsage());
  for (int i = 0; i != 10; ++i) {
    ASSERT_EQ(i, Lookup(i));
  }
  ASSERT_EQ(0, cache_->EvictAll());

  for (auto* handle : handles) {
    cache_->Release(handle);
  }
  ASSERT_EQ(10 * kCharge, cache_->EvictAll());
  ASSERT_EQ(0, cache_->GetUsage());
}

TEST_P(ClockCacheTest, ApplyToAllCacheEntries) {
  UseCache(1_MB, 3);
  std::vector<Cache::Handle*> handles;
  for (int i = 0; i != 50; ++i) {
    ASSERT_OK(Insert(i, kCharge + i));
  }
  handles.push_back(InsertAndRef(50, kCharge + 50));

  static std::mutex mutex;
  static std::vector<std::pair<int, size_t>> seen;
  seen.clear();
  cache_->ApplyToAllCacheEntries(
      [](void* value, size_t charge) {
        std::lock_guard lock(mutex);
        seen.emplace_back(static_cast<TestValue*>(value)->key, charge);
      },
      /* thread_safe= */ true);
  std::sort(seen.begin(), seen.end());
  ASSERT_EQ(seen.size(), 51);
  for (int i = 0; i != 51; ++i) {
    ASSERT_EQ(seen[i].first, i);
    ASSERT_EQ(seen[i].second, kCharge + i);
  }
  for (auto* handle : handles) {
    cache_->Release(handle);
  }
}

TEST_P(ClockCacheTest, NewId) {
  UseCache(1_MB);
  const auto a = cache_->NewId();
  const auto b = cache_->NewId();
  ASSERT_NE(a, b);
}

TEST_P(ClockCacheTest, Metrics) {
  yb::MetricRegistry registry;
  auto entity = METRIC_ENTITY_server.Instantiate(&registry, "test");
  UseCache(64 * kCharge, 1);
  cache_->SetMetrics(entity);
  yb::CacheMetrics metrics(entity);

  std::vector<Cache::Handle*> handles;
  for (int i = 0; i != 200; ++i) {
    if (i < 5) {
      handles.push_back(InsertAndRef(i));
    } else {
      ASSERT_OK(Insert(i));
    }
    // Every entry is in the table, so the gauge matches the usage exactly.
    ASSERT_EQ(cache_->GetUsage(), metrics.cache_usage->value());
  }
  ASSERT_EQ(metrics.cache_usage->value(), metrics.multi_touch_cache_usage->value());
  ASSERT_EQ(0, metrics.single_touch_cache_usage->value());

  cache_->EvictAll();
  ASSERT_EQ(5 * kCharge, metrics.cache_usage->value());
  for (auto* handle : handles) {
    cache_->Release(handle);
  }
  cache_->Erase(EncodeKey(0));
  ASSERT_EQ(4 * kCharge, metrics.cache_usage->value());
  cache_.reset();
  ASSERT_EQ(0, metrics.cache_usage->value());
  ASSERT_EQ(0, metrics.multi_touch_cache_usage->value());
}

// AutoHyperClockTable grows its hash table as entries are added.
TEST_P(ClockCacheTest, ManyEntries) {
  constexpr int kEntries = 100000;
  UseCache(kEntries * kCharge, 4);
  for (int i = 0; i != kEntries; ++i) {
    ASSERT_OK(Insert(i));
  }
  int hits = 0;
  for (int i = 0; i != kEntries; ++i) {
    hits += Lookup(i) == i;
  }
  // FixedHyperClockTable sizes its table for capacity / estimated_entry_charge entries.
  ASSERT_GE(hits, kEntries * 95 / 100);
  ASSERT_LE(cache_->GetUsage(), kEntries * kCharge + 16 * kCharge);
}

// Many threads insert, look up, release and erase overlapping keys while another thread evicts
// and changes the capacity. Checks that lookups only return live values with the right key, and
// that each value is deleted exactly once.
TEST_P(ClockCacheTest, ConcurrentStress) {
  constexpr int kKeys = 20000;
  constexpr size_t kCapacity = 4000 * kCharge;
  UseCache(kCapacity, 3);
  const int num_threads = std::max<int>(8, std::thread::hardware_concurrency());
  const auto duration = yb::RegularBuildVsSanitizers(10s, 30s);

  std::atomic<bool> stop{false};
  std::atomic<size_t> lookups{0};
  std::atomic<size_t> hits{0};
  std::atomic<bool> failed{false};
  std::vector<std::thread> threads;
  for (int t = 0; t != num_threads; ++t) {
    threads.emplace_back([&, t] {
      std::vector<Cache::Handle*> held;
      std::mt19937_64 rng(t);
      while (!stop.load(std::memory_order_relaxed)) {
        // Skew keys so that some are hot, like index blocks.
        const int key = rng() % 4 == 0 ? static_cast<int>(rng() % 16)
                                        : static_cast<int>(rng() % kKeys);
        const auto op = rng() % 100;
        if (op < 60) {
          auto* handle = cache_->Lookup(EncodeKey(key), kTestQueryId);
          lookups.fetch_add(1, std::memory_order_relaxed);
          if (handle) {
            hits.fetch_add(1, std::memory_order_relaxed);
            auto* value = static_cast<TestValue*>(cache_->Value(handle));
            if (value->state.load() != TestValue::kAlive || value->key != key) {
              failed = true;
              LOG(ERROR) << "Lookup of " << key << " returned key " << value->key << " state "
                         << value->state.load();
            }
            if (held.size() < 4 && rng() % 2) {
              held.push_back(handle);
            } else {
              cache_->Release(handle);
            }
          }
        } else if (op < 90) {
          Cache::Handle* handle = nullptr;
          const auto charge = kCharge / 2 + rng() % kCharge;
          auto s = cache_->Insert(
              EncodeKey(key), rng() % 8 ? kTestQueryId : kInMultiTouchId, tracker_.New(key),
              charge, &ValueTracker::Deleter, rng() % 2 ? &handle : nullptr);
          if (!s.ok()) {
            failed = true;
            LOG(ERROR) << "Insert failed: " << s;
          }
          if (handle) {
            cache_->Release(handle);
          }
        } else if (op < 97) {
          cache_->Erase(EncodeKey(key));
        } else if (!held.empty()) {
          cache_->Release(held.back());
          held.pop_back();
        }
      }
      for (auto* handle : held) {
        cache_->Release(handle);
      }
    });
  }
  threads.emplace_back([&] {
    std::mt19937_64 rng(12345);
    while (!stop.load(std::memory_order_relaxed)) {
      switch (rng() % 4) {
        case 0:
          cache_->Evict(rng() % (100 * kCharge) + 1);
          break;
        case 1:
          cache_->SetCapacity(kCapacity / 2 + rng() % kCapacity);
          break;
        case 2: {
          auto reservation = rng() % (kCapacity / 4);
          ASSERT_RESULT(cache_->ConsumeSpace(reservation, Cache::ReservationMode::kAlways));
          std::this_thread::sleep_for(1ms);
          cache_->ReleaseSpace(reservation);
          break;
        }
        case 3:
          if (rng() % 50 == 0) {
            cache_->EvictAll();
          }
          break;
      }
      std::this_thread::sleep_for(1ms);
    }
  });

  std::this_thread::sleep_for(duration);
  stop = true;
  for (auto& thread : threads) {
    thread.join();
  }
  ASSERT_FALSE(failed.load());
  LOG(INFO) << "Threads: " << num_threads << ", lookups: " << lookups << ", hits: " << hits
            << ", values: " << tracker_.allocated() << ", usage: " << cache_->GetUsage();
  ASSERT_GT(hits.load(), 0);
  ASSERT_EQ(0, cache_->GetPinnedUsage());

  cache_->SetCapacity(kCapacity);
  for (int i = 0; i != 16; ++i) {
    ASSERT_OK(Insert(kKeys + i));
  }
  ASSERT_LE(cache_->GetUsage(), kCapacity + 64 * kCharge);
}

// Compares LRU and hyper clock cache throughput when all threads read the same few entries, like
// many concurrent reads of the same index blocks. Only logs the numbers.
TEST(ClockCacheBenchmarkTest, HotKeys) {
  constexpr int kHotKeys = 8;
  const int num_threads = std::max<int>(8, std::thread::hardware_concurrency());
  const auto duration = yb::RegularBuildVsSanitizers(3s, 1s);
  auto dummy_deleter = [](const Slice&, void*) {};

  for (const auto& [name, cache] : {
           std::pair{"lru", NewLRUCache(1_GB, 6)},
           std::pair{"hyper_clock", NewHyperClockCache(1_GB, 6)}}) {
    static int value;
    for (int i = 0; i != kHotKeys; ++i) {
      ASSERT_OK(cache->Insert(EncodeKey(i), kTestQueryId, &value, 32_KB, dummy_deleter));
    }
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> ops{0};
    std::vector<std::thread> threads;
    for (int t = 0; t != num_threads; ++t) {
      threads.emplace_back([&, t] {
        std::vector<std::string> keys;
        for (int i = 0; i != kHotKeys; ++i) {
          keys.push_back(EncodeKey(i));
        }
        uint64_t local_ops = 0;
        size_t i = t;
        while (!stop.load(std::memory_order_relaxed)) {
          auto* handle = cache->Lookup(keys[i++ % kHotKeys], kTestQueryId);
          CHECK_NOTNULL(handle);
          cache->Release(handle);
          ++local_ops;
        }
        ops += local_ops;
      });
    }
    std::this_thread::sleep_for(duration);
    stop = true;
    for (auto& thread : threads) {
      thread.join();
    }
    const auto seconds = std::chrono::duration<double>(duration).count();
    LOG(INFO) << name << ": " << num_threads << " threads, " << kHotKeys << " hot keys: "
              << static_cast<uint64_t>(ops / seconds) << " lookup+release per second";
    ASSERT_GT(ops.load(), 0);
  }
}

} // namespace rocksdb
