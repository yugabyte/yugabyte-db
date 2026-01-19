//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <vector>
#include <string>

#include <gtest/gtest.h>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/table/merger.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

std::string ValueForKey(Slice key) {
  std::string result;
  result.reserve(key.size());
  for (auto it = key.cend(); it != key.cdata();) {
    result.push_back(*--it);
  }
  return result;
}

class TestIteratorFilter : public IteratorFilter {
 public:
  bool Filter(
      const QueryOptions& options, Slice user_key, FilterKeyCache* cache, void* context) const {
    const auto& keys = *static_cast<std::vector<std::string>*>(context);
    auto it = std::lower_bound(keys.begin(), keys.end(), user_key);
    return it != keys.end() && Slice(*it).starts_with(user_key);
  }
};

class MergerTest : public RocksDBTest {
 public:
  std::vector<std::string> GenerateStrings(size_t len, int string_len) {
    std::vector<std::string> ret;
    ret.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      ret.push_back(test::RandomHumanReadableString(&rnd_, string_len));
    }
    return ret;
  }

  ~MergerTest() {
    if (filter_iterator_) {
      filter_iterator_->~InternalIterator();
    }
  }

  void AssertEquivalence() {
    auto a = merging_iterator_.get();
    auto b = single_iterator_.get();
    if (!a->Valid()) {
      ASSERT_TRUE(!b->Valid());
    } else {
      ASSERT_TRUE(b->Valid());
      ASSERT_EQ(b->key().ToString(), a->key().ToString());
      ASSERT_EQ(b->value().ToString(), a->value().ToString());
    }
  }

  void SeekToRandom() { Seek(test::RandomHumanReadableString(&rnd_, 5)); }

  void Seek(std::string target) {
    merging_iterator_->Seek(target);
    single_iterator_->Seek(target);
  }

  void SeekToFirst() {
    merging_iterator_->SeekToFirst();
    single_iterator_->SeekToFirst();
  }

  void SeekToLast() {
    merging_iterator_->SeekToLast();
    single_iterator_->SeekToLast();
  }

  void Next(int times) {
    for (int i = 0; i < times && merging_iterator_->Valid(); ++i) {
      AssertEquivalence();
      merging_iterator_->Next();
      single_iterator_->Next();
    }
    AssertEquivalence();
  }

  void Prev(int times) {
    for (int i = 0; i < times && merging_iterator_->Valid(); ++i) {
      AssertEquivalence();
      merging_iterator_->Prev();
      single_iterator_->Prev();
    }
    AssertEquivalence();
  }

  void NextAndPrev(int times) {
    for (int i = 0; i < times && merging_iterator_->Valid(); ++i) {
      AssertEquivalence();
      if (rnd_.OneIn(2)) {
        merging_iterator_->Prev();
        single_iterator_->Prev();
      } else {
        merging_iterator_->Next();
        single_iterator_->Next();
      }
    }
    AssertEquivalence();
  }

  void Generate(size_t num_iterators, size_t strings_per_iterator,
                int letters_per_string) {
    std::vector<InternalIterator*> small_iterators;
    for (size_t i = 0; i < num_iterators; ++i) {
      auto strings = GenerateStrings(strings_per_iterator, letters_per_string);
      small_iterators.push_back(new test::VectorIterator(strings));
      all_keys_.insert(all_keys_.end(), strings.begin(), strings.end());
    }

    merging_iterator_.reset(
        NewMergingIterator(BytewiseComparator(), &small_iterators[0],
                           static_cast<int>(small_iterators.size())));
    single_iterator_.reset(new test::VectorIterator(all_keys_));
  }

  void GenerateWithFilter(
      size_t num_iterators, size_t strings_per_iterator, int letters_per_string,
      size_t subkeys_per_iterator) {
    MergeIteratorBuilder builder(BytewiseComparator(), &arena_);

    builder.SetupIteratorFilter(&filter_, QueryOptions());

    std::vector<InternalIterator*> small_iterators;
    auto keys = GenerateStrings(strings_per_iterator * num_iterators, letters_per_string);
    all_keys_.insert(all_keys_.end(), keys.begin(), keys.end());
    subkeys_.resize(all_keys_.size());

    for (size_t i = 0; i < num_iterators; ++i) {
      std::vector<std::string> iterator_keys(
          keys.begin() + i * strings_per_iterator, keys.begin() + (i + 1) * strings_per_iterator);
      for (size_t j = 0; j != subkeys_per_iterator; ++j) {
        auto idx = rnd_.Uniform(narrow_cast<int>(all_keys_.size()));
        iterator_keys.push_back(
            keys[idx] + test::RandomHumanReadableString(&rnd_, letters_per_string));
        subkeys_[idx].push_back(iterator_keys.back());
      }
      std::sort(iterator_keys.begin(), iterator_keys.end());

      std::vector<std::string> values;
      values.reserve(iterator_keys.size());
      for (const auto& key : iterator_keys) {
        values.push_back(ValueForKey(key));
      }
      auto iterator = arena_.NewObject<test::VectorIterator>(iterator_keys, values);
      created_iterators_.push_back(iterator);
      builder.AddIterator(iterator);
    }

    for (auto& subkeys : subkeys_) {
      std::sort(subkeys.begin(), subkeys.end());
    }
    filter_iterator_ = builder.Finish();
  }

  void TestFilter(bool forward_only);

  Random rnd_{3};
  Arena arena_;
  TestIteratorFilter filter_;
  std::unique_ptr<InternalIterator> merging_iterator_;
  std::unique_ptr<InternalIterator> single_iterator_;
  InternalIterator* filter_iterator_ = nullptr;
  std::vector<std::string> all_keys_;
  std::vector<std::vector<std::string>> subkeys_;
  std::vector<test::VectorIterator*> created_iterators_;
};

TEST_F(MergerTest, SeekToRandomNextTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToRandom();
    AssertEquivalence();
    Next(50000);
  }
}

TEST_F(MergerTest, SeekToRandomNextSmallStringsTest) {
  Generate(1000, 50, 2);
  for (int i = 0; i < 10; ++i) {
    SeekToRandom();
    AssertEquivalence();
    Next(50000);
  }
}

TEST_F(MergerTest, SeekToRandomPrevTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToRandom();
    AssertEquivalence();
    Prev(50000);
  }
}

TEST_F(MergerTest, SeekToRandomRandomTest) {
  Generate(200, 50, 50);
  for (int i = 0; i < 3; ++i) {
    SeekToRandom();
    AssertEquivalence();
    NextAndPrev(5000);
  }
}

TEST_F(MergerTest, SeekToFirstTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToFirst();
    AssertEquivalence();
    Next(50000);
  }
}

TEST_F(MergerTest, SeekToLastTest) {
  Generate(1000, 50, 50);
  for (int i = 0; i < 10; ++i) {
    SeekToLast();
    AssertEquivalence();
    Prev(50000);
  }
}

void MergerTest::TestFilter(bool forward_only) {
  GenerateWithFilter(1000, 10, 10, 20);
  std::vector<size_t> indexes(all_keys_.size());
  std::iota(indexes.begin(), indexes.end(), 0);
  if (forward_only) {
    std::sort(indexes.begin(), indexes.end(), [this](size_t lhs, size_t rhs) {
      return all_keys_[lhs] < all_keys_[rhs];
    });
  }
  Slice prev_key = all_keys_.back();
  for (auto i : indexes) {
    if (!rnd_.OneIn(2)) {
      continue;
    }
    const auto& key = all_keys_[i];
    filter_iterator_->UpdateFilterKey(key, Slice());
    {
      const KeyValueEntry* entry;
      if (key > prev_key && rnd_.OneIn(2)) {
        for (auto iterator : created_iterators_) {
          iterator->ExpectSeekToPrefixOnly(false);
        }
        for (;;) {
          entry = &filter_iterator_->Next();
          ASSERT_TRUE(entry->Valid());
          ASSERT_LE(entry->key, key);
          if (entry->key == key) {
            break;
          }
        }
      } else {
        for (auto iterator : created_iterators_) {
          iterator->ExpectSeekToPrefixOnly(true);
        }
        entry = &filter_iterator_->Seek(key);
        ASSERT_TRUE(entry->Valid());
      }
      ASSERT_EQ(entry->key, key);
      ASSERT_EQ(entry->value, ValueForKey(key));
    }
    for (size_t j = 0, nj = rnd_.Uniform(narrow_cast<int>(subkeys_[i].size() + 1)); j != nj; ++j) {
      const auto& entry = filter_iterator_->Next();
      ASSERT_TRUE(entry.Valid());
      ASSERT_EQ(entry.key, subkeys_[i][j]);
      ASSERT_EQ(entry.value, ValueForKey(entry.key));
    }
    prev_key = key;
  }
}

TEST_F(MergerTest, RandomFilter) {
  TestFilter(false);
}

TEST_F(MergerTest, RandomFilterForwardOnly) {
  TestFilter(true);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
