//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <set>

#include <gtest/gtest.h>

#include "yb/rocksdb/db/skiplist.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/hash.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/testharness.h"

#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

typedef uint64_t Key;

struct TestComparator {
  int operator()(const Key& a, const Key& b) const {
    if (a < b) {
      return -1;
    } else if (a > b) {
      return +1;
    } else {
      return 0;
    }
  }
};

typedef SkipList<Key, TestComparator> TestSkipList;

class SkipTest : public RocksDBTest {};

TEST_F(SkipTest, Empty) {
  Arena arena;
  TestComparator cmp;
  SkipList<Key, TestComparator> list(cmp, &arena);
  ASSERT_TRUE(!list.Contains(10));

  SkipList<Key, TestComparator>::Iterator iter(&list);
  ASSERT_TRUE(!iter.Entry());
  iter.SeekToFirst();
  ASSERT_TRUE(!iter.Entry());
  iter.Seek(100);
  ASSERT_TRUE(!iter.Entry());
  iter.SeekToLast();
  ASSERT_TRUE(!iter.Entry());
}

TEST_F(SkipTest, InsertAndLookup) {
  const int N = 2000;
  const int R = 5000;
  Random rnd(1000);
  std::set<Key> keys;
  Arena arena;
  TestComparator cmp;
  SkipList<Key, TestComparator> list(cmp, &arena);
  for (int i = 0; i < N; i++) {
    Key key = rnd.Next() % R;
    if (keys.insert(key).second) {
      list.Insert(key);
    }
  }

  for (int i = 0; i < R; i++) {
    if (list.Contains(i)) {
      ASSERT_EQ(keys.count(i), 1U);
    } else {
      ASSERT_EQ(keys.count(i), 0U);
    }
  }

  // Simple iterator tests
  {
    SkipList<Key, TestComparator>::Iterator iter(&list);
    ASSERT_TRUE(!iter.Entry());

    iter.Seek(0);
    ASSERT_TRUE(iter.Entry());
    ASSERT_EQ(*(keys.begin()), iter.Entry());

    iter.SeekToFirst();
    ASSERT_TRUE(iter.Entry());
    ASSERT_EQ(*(keys.begin()), iter.Entry());

    iter.SeekToLast();
    ASSERT_TRUE(iter.Entry());
    ASSERT_EQ(*(keys.rbegin()), iter.Entry());
  }

  // Forward iteration test
  for (int i = 0; i < R; i++) {
    SkipList<Key, TestComparator>::Iterator iter(&list);
    iter.Seek(i);

    // Compare against model iterator
    std::set<Key>::iterator model_iter = keys.lower_bound(i);
    for (int j = 0; j < 3; j++) {
      if (model_iter == keys.end()) {
        ASSERT_TRUE(!iter.Entry());
        break;
      } else {
        ASSERT_TRUE(iter.Entry());
        ASSERT_EQ(*model_iter, iter.Entry());
        ++model_iter;
        iter.Next();
      }
    }
  }

  // Backward iteration test
  {
    SkipList<Key, TestComparator>::Iterator iter(&list);
    iter.SeekToLast();

    // Compare against model iterator
    for (std::set<Key>::reverse_iterator model_iter = keys.rbegin();
         model_iter != keys.rend();
         ++model_iter) {
      ASSERT_TRUE(iter.Entry());
      ASSERT_EQ(*model_iter, iter.Entry());
      iter.Prev();
    }
    ASSERT_TRUE(!iter.Entry());
  }
}

TEST_F(SkipTest, Erase) {
  constexpr int kNumIterations = 100;
  constexpr int kNumKeys = 200;

  for (int iteration = 0; iteration != kNumIterations; ++iteration) {
    Arena arena;
    TestComparator cmp;
    TestSkipList list(cmp, &arena);

    std::vector<Key> keys(kNumKeys);

    for (size_t i = 0; i != keys.size(); ++i) {
      keys[i] = yb::RandomUniformInt<Key>();
      list.Insert(keys[i]);
    }

    std::set<Key> left_keys(keys.begin(), keys.end());
    std::shuffle(keys.begin(), keys.end(), yb::ThreadLocalRandom());

    for (auto key : keys) {
      left_keys.erase(key);
      list.Erase(key, cmp);
      TestSkipList::Iterator iter(&list);
      iter.SeekToFirst();
      auto j = left_keys.begin();
      while (iter.Entry()) {
        ASSERT_NE(j, left_keys.end());

        ASSERT_EQ(iter.Entry(), *j);

        iter.Next();
        ++j;
      }
      ASSERT_EQ(j, left_keys.end());
    }
  }
}

TEST_F(SkipTest, EraseInsert) {
  constexpr int kNumIterations = 100;

  for (int iteration = 0; iteration != kNumIterations; ++iteration) {
    Arena arena;
    TestComparator cmp;
    TestSkipList list(cmp, &arena);

    std::set<Key> present_keys;

    for (size_t i = 0; i != 1000; ++i) {
      if (present_keys.empty() || yb::RandomUniformBool()) {
        Key key = yb::RandomUniformInt<Key>(1, 100);
        if (present_keys.count(key)) {
          continue;
        }
        LOG(INFO) << "Insert: " << key;
        present_keys.insert(key);
        list.Insert(key);
      } else {
        auto idx = yb::RandomUniformInt<size_t>(0, present_keys.size() - 1);
        auto it = present_keys.begin();
        std::advance(it, idx);
        LOG(INFO) << "Erase: " << *it;
        list.Erase(*it, cmp);
        present_keys.erase(it);
      }

      SCOPED_TRACE(yb::Format("i: $0", i));

      std::vector<Key> listed;
      TestSkipList::Iterator iter(&list);
      iter.SeekToFirst();
      while (iter.Entry()) {
        listed.push_back(iter.Entry());
        iter.Next();
      }
      ASSERT_EQ(yb::ToString(listed), yb::ToString(present_keys));
    }
  }
}

// We want to make sure that with a single writer and multiple
// concurrent readers (with no synchronization other than when a
// reader's iterator is created), the reader always observes all the
// data that was present in the skip list when the iterator was
// constructor.  Because insertions are happening concurrently, we may
// also observe new values that were inserted since the iterator was
// constructed, but we should never miss any values that were present
// at iterator construction time.
//
// We generate multi-part keys:
//     <key,gen,hash>
// where:
//     key is in range [0..K-1]
//     gen is a generation number for key
//     hash is hash(key,gen)
//
// The insertion code picks a random key, sets gen to be 1 + the last
// generation number inserted for that key, and sets hash to Hash(key,gen).
//
// At the beginning of a read, we snapshot the last inserted
// generation number for each key.  We then iterate, including random
// calls to Next() and Seek().  For every key we encounter, we
// check that it is either expected given the initial snapshot or has
// been concurrently added since the iterator started.
class ConcurrentTest {
 private:
  static const uint32_t K = 4;

  static uint64_t key(Key key) { return (key >> 40); }
  static uint64_t gen(Key key) { return (key >> 8) & 0xffffffffu; }
  static uint64_t hash(Key key) { return key & 0xff; }

  static uint64_t HashNumbers(uint64_t k, uint64_t g) {
    uint64_t data[2] = { k, g };
    return Hash(reinterpret_cast<char*>(data), sizeof(data), 0);
  }

  static Key MakeKey(uint64_t k, uint64_t g) {
    assert(sizeof(Key) == sizeof(uint64_t));
    assert(k <= K);  // We sometimes pass K to seek to the end of the skiplist
    assert(g <= 0xffffffffu);
    return ((k << 40) | (g << 8) | (HashNumbers(k, g) & 0xff));
  }

  static bool IsValidKey(Key k) {
    return hash(k) == (HashNumbers(key(k), gen(k)) & 0xff);
  }

  static Key RandomTarget(Random* rnd) {
    switch (rnd->Next() % 10) {
      case 0:
        // Seek to beginning
        return MakeKey(0, 0);
      case 1:
        // Seek to end
        return MakeKey(K, 0);
      default:
        // Seek to middle
        return MakeKey(rnd->Next() % K, 0);
    }
  }

  // Per-key generation
  struct State {
    std::atomic<int> generation[K];
    void Set(int k, int v) {
      generation[k].store(v, std::memory_order_release);
    }
    int Get(int k) { return generation[k].load(std::memory_order_acquire); }

    State() {
      for (unsigned int k = 0; k < K; k++) {
        Set(k, 0);
      }
    }
  };

  // Current state of the test
  State current_;

  Arena arena_;

  // SkipList is not protected by mu_.  We just use a single writer
  // thread to modify it.
  SkipList<Key, TestComparator> list_;

 public:
  ConcurrentTest() : list_(TestComparator(), &arena_) {}

  // REQUIRES: External synchronization
  void WriteStep(Random* rnd) {
    const uint32_t k = rnd->Next() % K;
    const int g = current_.Get(k) + 1;
    const Key new_key = MakeKey(k, g);
    list_.Insert(new_key);
    current_.Set(k, g);
  }

  void ReadStep(Random* rnd) {
    // Remember the initial committed state of the skiplist.
    State initial_state;
    for (unsigned int k = 0; k < K; k++) {
      initial_state.Set(k, current_.Get(k));
    }

    Key pos = RandomTarget(rnd);
    SkipList<Key, TestComparator>::Iterator iter(&list_);
    iter.Seek(pos);
    while (true) {
      Key current;
      if (!iter.Entry()) {
        current = MakeKey(K, 0);
      } else {
        current = iter.Entry();
        ASSERT_TRUE(IsValidKey(current)) << current;
      }
      ASSERT_LE(pos, current) << "should not go backwards";

      // Verify that everything in [pos,current) was not present in
      // initial_state.
      while (pos < current) {
        ASSERT_LT(key(pos), K) << pos;

        // Note that generation 0 is never inserted, so it is ok if
        // <*,0,*> is missing.
        ASSERT_TRUE((gen(pos) == 0U) ||
                    (gen(pos) > static_cast<uint64_t>(initial_state.Get(
                                    static_cast<int>(key(pos))))))
            << "key: " << key(pos) << "; gen: " << gen(pos)
            << "; initgen: " << initial_state.Get(static_cast<int>(key(pos)));

        // Advance to next key in the valid key space
        if (key(pos) < key(current)) {
          pos = MakeKey(key(pos) + 1, 0);
        } else {
          pos = MakeKey(key(pos), gen(pos) + 1);
        }
      }

      if (!iter.Entry()) {
        break;
      }

      if (rnd->Next() % 2) {
        iter.Next();
        pos = MakeKey(key(pos), gen(pos) + 1);
      } else {
        Key new_target = RandomTarget(rnd);
        if (new_target > pos) {
          pos = new_target;
          iter.Seek(new_target);
        }
      }
    }
  }
};
const uint32_t ConcurrentTest::K;

// Simple test that does single-threaded testing of the ConcurrentTest
// scaffolding.
TEST_F(SkipTest, ConcurrentWithoutThreads) {
  ConcurrentTest test;
  Random rnd(test::RandomSeed());
  for (int i = 0; i < 10000; i++) {
    test.ReadStep(&rnd);
    test.WriteStep(&rnd);
  }
}

class TestState {
 public:
  ConcurrentTest t_;
  int seed_;
  std::atomic<bool> quit_flag_;

  enum ReaderState {
    STARTING,
    RUNNING,
    DONE
  };

  explicit TestState(int s)
      : seed_(s), quit_flag_(false), state_(STARTING), state_cv_(&mu_) {}

  void Wait(ReaderState s) {
    mu_.Lock();
    while (state_ != s) {
      state_cv_.Wait();
    }
    mu_.Unlock();
  }

  void Change(ReaderState s) {
    mu_.Lock();
    state_ = s;
    state_cv_.Signal();
    mu_.Unlock();
  }

 private:
  port::Mutex mu_;
  ReaderState state_;
  port::CondVar state_cv_;
};

static void ConcurrentReader(void* arg) {
  TestState* state = reinterpret_cast<TestState*>(arg);
  Random rnd(state->seed_);
  state->Change(TestState::RUNNING);
  while (!state->quit_flag_.load(std::memory_order_acquire)) {
    state->t_.ReadStep(&rnd);
  }
  state->Change(TestState::DONE);
}

static void RunConcurrent(int run) {
  const int seed = test::RandomSeed() + (run * 100);
  Random rnd(seed);
  const int N = 1000;
  const int kSize = 1000;
  for (int i = 0; i < N; i++) {
    if ((i % 100) == 0) {
      fprintf(stderr, "Run %d of %d\n", i, N);
    }
    TestState state(seed + 1);
    Env::Default()->Schedule(ConcurrentReader, &state);
    state.Wait(TestState::RUNNING);
    for (int k = 0; k < kSize; k++) {
      state.t_.WriteStep(&rnd);
    }
    state.quit_flag_.store(true, std::memory_order_release);
    state.Wait(TestState::DONE);
  }
}

TEST_F(SkipTest, Concurrent1) { RunConcurrent(1); }
TEST_F(SkipTest, Concurrent2) { RunConcurrent(2); }
TEST_F(SkipTest, Concurrent3) { RunConcurrent(3); }
TEST_F(SkipTest, Concurrent4) { RunConcurrent(4); }
TEST_F(SkipTest, Concurrent5) { RunConcurrent(5); }
}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
