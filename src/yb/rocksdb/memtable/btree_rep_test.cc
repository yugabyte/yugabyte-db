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
#include <functional>
#include <random>
#include <set>
#include <thread>

#include <gtest/gtest.h>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/memtable_allocator.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/random_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

namespace rocksdb {

class BTreeRepTest : public RocksDBTest, public ::testing::WithParamInterface<bool> {
 protected:
  BTreeRepTest()
      : icmp_(GetParam() ? ReverseBytewiseComparator() : BytewiseComparator()),
        key_cmp_(icmp_),
        allocator_(&arena_, &write_buffer_),
        rep_(BTreeFactory().CreateMemTableRep(key_cmp_, &allocator_, nullptr, nullptr)) {}

  // Encodes an entry as MemTable::Add does and inserts it.
  const char* Add(const std::string& internal_key) {
    auto encoded_len =
        VarintLength(internal_key.size()) + internal_key.size() + VarintLength(0);
    char* buf;
    auto handle = rep_->Allocate(encoded_len, &buf);
    auto p = EncodeVarint32(buf, static_cast<uint32_t>(internal_key.size()));
    memcpy(p, internal_key.data(), internal_key.size());
    EncodeVarint32(p + internal_key.size(), 0);
    rep_->Insert(handle);
    return buf;
  }

  static std::string MakeInternalKey(const std::string& user_key, SequenceNumber seq) {
    std::string result = user_key;
    PutFixed64(&result, PackSequenceAndType(seq, kTypeValue));
    return result;
  }

  static std::string KeyOf(const char* entry) {
    return GetLengthPrefixedSlice(entry).ToBuffer();
  }

  // User keys with long shared prefixes, prefixes of each other and extreme bytes.
  static std::string RandomUserKey(std::mt19937_64* rng) {
    static const std::vector<std::string> kPrefixes = {
        "", "a", "aaaaaaaaaaaaaaaaaaaa", "table/0001/row/", std::string("\xff\xff", 2),
        std::string("\x00\x00\x00", 3), "table/0001/row/0000000000",
    };
    static const std::string kAlphabet("\x00\x01" "ab\xfe\xff", 6);
    auto result = kPrefixes[yb::RandomUniformInt<size_t>(0, kPrefixes.size() - 1, rng)];
    auto tail = yb::RandomUniformInt<size_t>(0, 24, rng);
    auto full_range = yb::RandomUniformBool(rng);
    for (size_t i = 0; i != tail; ++i) {
      result.push_back(full_range
          ? static_cast<char>(yb::RandomUniformInt<int>(0, 255, rng))
          : kAlphabet[yb::RandomUniformInt<size_t>(0, kAlphabet.size() - 1, rng)]);
    }
    return result;
  }

  std::string RandomInternalKey(std::mt19937_64* rng) {
    return MakeInternalKey(RandomUserKey(rng), yb::RandomUniformInt<SequenceNumber>(1, 5, rng));
  }

  struct ModelCompare {
    const InternalKeyComparator* icmp;
    bool operator()(const std::string& lhs, const std::string& rhs) const {
      return icmp->Compare(lhs, rhs) < 0;
    }
  };

  using Model = std::set<std::string, ModelCompare>;

  Model NewModel() {
    return Model(ModelCompare{&icmp_});
  }

  void CheckScan(const Model& model) {
    std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
    auto it = model.begin();
    for (auto entry = iter->SeekToFirst(); entry; entry = iter->Next(), ++it) {
      ASSERT_NE(it, model.end());
      ASSERT_EQ(KeyOf(entry), *it);
    }
    ASSERT_EQ(it, model.end());
    auto rit = model.rbegin();
    for (auto entry = iter->SeekToLast(); entry; entry = iter->Prev(), ++rit) {
      ASSERT_NE(rit, model.rend());
      ASSERT_EQ(KeyOf(entry), *rit);
    }
    ASSERT_EQ(rit, model.rend());
  }

  void CheckRandomWalks(const Model& model, std::mt19937_64* rng, size_t walks) {
    std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
    for (size_t i = 0; i != walks; ++i) {
      auto target = yb::RandomUniformBool(rng) && !model.empty()
          ? *std::next(model.begin(), yb::RandomUniformInt<size_t>(0, model.size() - 1, rng))
          : RandomInternalKey(rng);
      auto it = model.lower_bound(target);
      auto entry = iter->Seek(target);
      ASSERT_EQ(entry != nullptr, it != model.end()) << i;
      for (int step = 0; entry && step != 20; ++step) {
        ASSERT_EQ(KeyOf(entry), *it);
        ASSERT_TRUE(rep_->Contains(entry));
        if (yb::RandomUniformBool(rng)) {
          entry = iter->Next();
          ++it;
          ASSERT_EQ(entry != nullptr, it != model.end());
        } else {
          entry = iter->Prev();
          ASSERT_EQ(entry != nullptr, it != model.begin());
          if (it == model.begin()) {
            break;
          }
          --it;
        }
      }
    }
  }

  // One writer inserts keys in random order and erases those that erasable picks, erase_lag inserts
  // later, while readers seek and step. A key inserted before a reader's operation and never
  // erased must not be skipped.
  void TestConcurrent(size_t erase_lag, const std::function<bool(size_t)>& erasable);

  InternalKeyComparator icmp_;
  MemTable::KeyComparator key_cmp_;
  Arena arena_;
  WriteBuffer write_buffer_{0};
  MemTableAllocator allocator_;
  std::unique_ptr<MemTableRep> rep_;
};

INSTANTIATE_TEST_CASE_P(Comparator, BTreeRepTest, ::testing::Bool());

TEST_P(BTreeRepTest, Empty) {
  std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
  ASSERT_EQ(iter->SeekToFirst(), nullptr);
  ASSERT_EQ(iter->SeekToLast(), nullptr);
  ASSERT_EQ(iter->Seek(MakeInternalKey("a", 1)), nullptr);
}

TEST_P(BTreeRepTest, Random) {
  constexpr size_t kNumKeys = yb::RegularBuildVsSanitizers(100000, 10000);
  std::mt19937_64 rng(42);
  auto model = NewModel();
  std::vector<const char*> entries;
  while (model.size() != kNumKeys) {
    auto key = RandomInternalKey(&rng);
    if (model.insert(key).second) {
      entries.push_back(Add(key));
    }
    if (model.size() % (kNumKeys / 10) == 0) {
      ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 100));
    }
  }
  ASSERT_NO_FATALS(CheckScan(model));
  ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 10000));

  std::shuffle(entries.begin(), entries.end(), rng);
  entries.resize(entries.size() * 3 / 4);
  for (auto entry : entries) {
    ASSERT_TRUE(rep_->Erase(const_cast<char*>(entry), key_cmp_));
    ASSERT_FALSE(rep_->Erase(const_cast<char*>(entry), key_cmp_));
    model.erase(KeyOf(entry));
  }
  ASSERT_NO_FATALS(CheckScan(model));
  ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 10000));

  // Inserts after erases land in sparse leaves.
  while (model.size() != kNumKeys) {
    auto key = RandomInternalKey(&rng);
    if (model.insert(key).second) {
      Add(key);
    }
  }
  ASSERT_NO_FATALS(CheckScan(model));
  ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 10000));
}

TEST_P(BTreeRepTest, Sequential) {
  constexpr size_t kNumKeys = yb::RegularBuildVsSanitizers(100000, 10000);
  std::mt19937_64 rng(42);
  auto model = NewModel();
  for (size_t i = 0; i != kNumKeys; ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "row/%010zu", GetParam() ? kNumKeys - i : i);
    auto key = MakeInternalKey(buf, 1);
    model.insert(key);
    Add(key);
  }
  ASSERT_NO_FATALS(CheckScan(model));
  ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 10000));
}

// Keys that share less than every key before them shrink the shared prefix.
TEST_P(BTreeRepTest, SharedPrefixShrinks) {
  std::mt19937_64 rng(42);
  auto model = NewModel();
  std::string prefix(40, 'p');
  for (size_t len = prefix.size() + 1; len-- > 0;) {
    for (int i = 0; i != 1000; ++i) {
      auto key = MakeInternalKey(
          prefix.substr(0, len) + RandomUserKey(&rng),
          yb::RandomUniformInt<SequenceNumber>(1, 5, &rng));
      if (model.insert(key).second) {
        Add(key);
      }
    }
    ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 100));
  }
  ASSERT_NO_FATALS(CheckScan(model));
  ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 10000));
}

// A reader loads the root, then the root splits, then the reader reads the old root's version: the
// old root now covers only the left half, so the reader must start over from the new root.
TEST_P(BTreeRepTest, ReaderRacesRootSplit) {
#ifdef NDEBUG
  GTEST_SKIP() << "Sync points work only in debug builds";
#endif
  // Fills the root leaf, keeping the middle key for the split, so the split cuts the root in half.
  constexpr int kNumKeys = 63;
  constexpr int kMiddle = kNumKeys / 2;
  auto key = [](int i) {
    char buf[16];
    snprintf(buf, sizeof(buf), "key%03d", i);
    return MakeInternalKey(buf, 1);
  };
  for (int i = 0; i != kNumKeys; ++i) {
    if (i != kMiddle) {
      Add(key(i));
    }
  }

  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"BTreeRep::Descend:RootLoaded", "BTreeRep::InsertIntoParent:NewRoot"},
      {"BTreeRep::SplitLeaf:Done", "BTreeRep::Descend:ReadRootVersion"},
  });
  sync_point->EnableProcessing();
  std::thread writer([this, &key] {
    Add(key(kMiddle));
  });
  auto last = key(GetParam() ? 0 : kNumKeys - 1);
  std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
  auto entry = iter->Seek(last);
  writer.join();
  sync_point->DisableProcessing();
  sync_point->ClearTrace();

  ASSERT_NE(entry, nullptr);
  ASSERT_EQ(KeyOf(entry), last);
}

// A reader checks a root that has one child left, then the writer replaces the root with the child
// and splits the child as the new root: the reader must not descend the child as the whole tree.
TEST_P(BTreeRepTest, ReaderRacesRootCollapse) {
#ifdef NDEBUG
  GTEST_SKIP() << "Sync points work only in debug builds";
#endif
  constexpr int kNumKeys = 63;
  constexpr int kMiddle = kNumKeys / 2;
  std::vector<std::string> keys;
  for (int i = 0; i != kNumKeys; ++i) {
    char buf[16];
    snprintf(buf, sizeof(buf), "key%03d", i);
    keys.push_back(MakeInternalKey(buf, 1));
  }
  std::sort(keys.begin(), keys.end(), ModelCompare{&icmp_});
  // Splits the root leaf in the middle: keys [0, kMiddle) on the left.
  for (int i = 0; i != kNumKeys; ++i) {
    if (i != kMiddle) {
      Add(keys[i]);
    }
  }
  Add(keys[kMiddle]);
  // Erases the left leaf down to its minimum; the next erase merges the two leaves.
  std::vector<const char*> entries;
  {
    std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
    for (auto entry = iter->SeekToFirst(); entry; entry = iter->Next()) {
      entries.push_back(entry);
    }
  }
  for (int i = 0; i != kMiddle - 7; ++i) {
    ASSERT_TRUE(rep_->Erase(const_cast<char*>(entries[i]), key_cmp_));
  }

  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"BTreeRep::Rebalance:CollapseRoot", "BTreeRep::Descend:Start"},
      {"BTreeRep::Descend:RootChecked", "BTreeRep::Rebalance:ReplaceRoot"},
      {"BTreeRepTest::ReaderRacesRootCollapse:WritesDone", "BTreeRep::Descend:FollowRoot"},
  });
  sync_point->EnableProcessing();
  const char* found = nullptr;
  std::thread reader([this, &keys, &found] {
    std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
    found = iter->Seek(keys.back());
  });
  ASSERT_TRUE(rep_->Erase(const_cast<char*>(entries[kMiddle - 7]), key_cmp_));
  // Refills the merged root leaf and splits it in the middle.
  for (int i = 0; i != kMiddle - 6; ++i) {
    Add(keys[i]);
  }
  TEST_SYNC_POINT("BTreeRepTest::ReaderRacesRootCollapse:WritesDone");
  reader.join();
  sync_point->DisableProcessing();
  sync_point->ClearTrace();

  ASSERT_NE(found, nullptr);
  ASSERT_EQ(KeyOf(found), keys.back());
}

void BTreeRepTest::TestConcurrent(size_t erase_lag, const std::function<bool(size_t)>& erasable) {
  constexpr size_t kNumKeys = yb::RegularBuildVsSanitizers(300000, 30000);
  constexpr size_t kNumReaders = 4;
  std::mt19937_64 rng(42);

  auto model = NewModel();
  std::vector<std::string> keys;
  while (keys.size() != kNumKeys) {
    auto key = RandomInternalKey(&rng);
    if (model.insert(key).second) {
      keys.push_back(key);
    }
  }
  // Keys in insertion order, which is random.
  std::vector<std::pair<std::string, size_t>> sorted;
  for (size_t i = 0; i != keys.size(); ++i) {
    sorted.emplace_back(keys[i], i);
  }
  std::sort(sorted.begin(), sorted.end(), [this](const auto& lhs, const auto& rhs) {
    return icmp_.Compare(lhs.first, rhs.first) < 0;
  });
  auto sorted_index = [this, &sorted](const std::string& key) {
    auto it = std::lower_bound(
        sorted.begin(), sorted.end(), key, [this](const auto& lhs, const std::string& rhs) {
      return icmp_.Compare(lhs.first, rhs) < 0;
    });
    CHECK(it != sorted.end() && it->first == key);
    return static_cast<size_t>(it - sorted.begin());
  };

  std::atomic<size_t> published{0};
  std::atomic<size_t> total_steps{0};
  yb::TestThreadHolder thread_holder;
  for (size_t r = 0; r != kNumReaders; ++r) {
    thread_holder.AddThreadFunctor([&, r] {
      std::mt19937_64 reader_rng(r);
      std::unique_ptr<MemTableRep::Iterator> iter(rep_->GetIterator(nullptr));
      // Keys in sorted (begin, end) must be absent from the scan: inserted after it started, or
      // erasable.
      auto check_gap = [&](size_t begin, size_t end, size_t visible) {
        for (auto i = begin + 1; i < end; ++i) {
          auto rank = sorted[i].second;
          CHECK(rank >= visible || erasable(rank))
              << "Skipped " << Slice(sorted[i].first).ToDebugHexString() << ", rank " << rank
              << ", visible " << visible;
        }
      };
      size_t steps = 0;
      while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
        auto visible = published.load(std::memory_order_acquire);
        auto start = yb::RandomUniformInt<size_t>(0, sorted.size() - 1, &reader_rng);
        auto entry = iter->Seek(sorted[start].first);
        // Index of the last key known not to be past the iterator.
        auto prev_index = start - 1;
        if (!entry) {
          check_gap(start - 1, sorted.size(), visible);
          continue;
        }
        auto index = sorted_index(KeyOf(entry));
        CHECK_GE(index, start);
        check_gap(prev_index, index, visible);
        bool forward = yb::RandomUniformBool(&reader_rng);
        for (int step = 0; step != 50; ++step, ++steps) {
          entry = forward ? iter->Next() : iter->Prev();
          if (!entry) {
            if (forward) {
              check_gap(index, sorted.size(), visible);
            } else {
              check_gap(static_cast<size_t>(-1), index, visible);
            }
            break;
          }
          auto new_index = sorted_index(KeyOf(entry));
          if (forward) {
            CHECK_GT(new_index, index);
            check_gap(index, new_index, visible);
          } else {
            CHECK_LT(new_index, index);
            check_gap(new_index, index, visible);
          }
          index = new_index;
        }
      }
      total_steps.fetch_add(steps);
    });
  }

  std::vector<const char*> entries;
  for (size_t rank = 0; rank != keys.size(); ++rank) {
    entries.push_back(Add(keys[rank]));
    if (rank >= erase_lag && erasable(rank - erase_lag)) {
      ASSERT_TRUE(rep_->Erase(const_cast<char*>(entries[rank - erase_lag]), key_cmp_));
    }
    published.store(rank + 1, std::memory_order_release);
  }
  thread_holder.Stop();
  LOG(INFO) << "Reader steps: " << total_steps.load();
}

TEST_P(BTreeRepTest, Concurrent) {
  TestConcurrent(/* erase_lag= */ 100, [](size_t rank) { return rank % 4 == 3; });
}

// Erasing most keys makes nodes merge and move entries while readers step across them.
TEST_P(BTreeRepTest, ConcurrentErase) {
  TestConcurrent(/* erase_lag= */ 2000, [](size_t rank) { return rank % 8 != 0; });
}

// Erasing every key merges the tree back into a root leaf.
TEST_P(BTreeRepTest, EraseAll) {
  constexpr size_t kNumKeys = yb::RegularBuildVsSanitizers(50000, 5000);
  std::mt19937_64 rng(42);
  for (auto order : {"random", "ascending", "descending"}) {
    SCOPED_TRACE(order);
    auto model = NewModel();
    std::vector<const char*> entries;
    while (model.size() != kNumKeys) {
      auto key = RandomInternalKey(&rng);
      if (model.insert(key).second) {
        entries.push_back(Add(key));
      }
    }
    auto compare = [this](const char* lhs, const char* rhs) { return key_cmp_(lhs, rhs) < 0; };
    if (order == std::string("random")) {
      std::shuffle(entries.begin(), entries.end(), rng);
    } else {
      std::sort(entries.begin(), entries.end(), compare);
      if (order == std::string("descending")) {
        std::reverse(entries.begin(), entries.end());
      }
    }
    for (size_t i = 0; i != entries.size(); ++i) {
      ASSERT_TRUE(rep_->Erase(const_cast<char*>(entries[i]), key_cmp_));
      model.erase(KeyOf(entries[i]));
      if (i % (kNumKeys / 10) == 0) {
        ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 100));
      }
    }
    ASSERT_NO_FATALS(CheckScan(model));
    ASSERT_NO_FATALS(CheckRandomWalks(model, &rng, 10));
  }
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
