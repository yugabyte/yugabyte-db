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
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef GFLAGS
#include <cstdio>
int main() {
  fprintf(stderr, "Please install gflags to run this test... Skipping...\n");
  return 0;
}
#else

#include <vector>
#include "yb/util/flags.h"

#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/util/arena.h"

#include "yb/util/enums.h"
#include "yb/util/test_util.h"

using GFLAGS::ParseCommandLineFlags;

DEFINE_NON_RUNTIME_int32(bits_per_key, 10, "");

namespace rocksdb {

static const int kVerbose = 1;

static Slice Key(size_t i, char* buffer) {
  memcpy(buffer, &i, sizeof(i));
  return Slice(buffer, sizeof(i));
}

static size_t NextLength(size_t length) {
  if (length < 10) {
    length += 1;
  } else if (length < 100) {
    length += 10;
  } else if (length < 1000) {
    length += 100;
  } else {
    length += 1000;
  }
  return length;
}

class BloomTest : public RocksDBTest {
 private:
  const FilterPolicy* policy_;
  std::string filter_;
  std::vector<std::string> keys_;

 public:
  BloomTest() : policy_(
      NewBloomFilterPolicy(FLAGS_bits_per_key)) {}

  ~BloomTest() {
    delete policy_;
  }

  void Reset() {
    keys_.clear();
    filter_.clear();
  }

  void Add(const Slice& s) {
    keys_.push_back(s.ToString());
  }

  void Build() {
    std::vector<Slice> key_slices;
    for (size_t i = 0; i < keys_.size(); i++) {
      key_slices.push_back(Slice(keys_[i]));
    }
    filter_.clear();
    policy_->CreateFilter(&key_slices[0], static_cast<int>(key_slices.size()),
                          &filter_);
    keys_.clear();
    if (kVerbose >= 2) DumpFilter();
  }

  size_t FilterSize() const {
    return filter_.size();
  }

  void DumpFilter() {
    fprintf(stderr, "F(");
    for (size_t i = 0; i+1 < filter_.size(); i++) {
      const unsigned int c = static_cast<unsigned int>(filter_[i]);
      for (int j = 0; j < 8; j++) {
        fprintf(stderr, "%c", (c & (1 <<j)) ? '1' : '.');
      }
    }
    fprintf(stderr, ")\n");
  }

  bool Matches(const Slice& s) {
    if (!keys_.empty()) {
      Build();
    }
    return policy_->KeyMayMatch(s, filter_);
  }

  double FalsePositiveRate() {
    char buffer[sizeof(size_t)];
    size_t result = 0;
    for (size_t i = 0; i < 10000; i++) {
      if (Matches(Key(i + 1000000000, buffer))) {
        result++;
      }
    }
    return result / 10000.0;
  }
};

TEST_F(BloomTest, EmptyFilter) {
  ASSERT_TRUE(!Matches("hello"));
  ASSERT_TRUE(!Matches("world"));
}

TEST_F(BloomTest, Small) {
  Add("hello");
  Add("world");
  ASSERT_TRUE(Matches("hello"));
  ASSERT_TRUE(Matches("world"));
  ASSERT_TRUE(!Matches("x"));
  ASSERT_TRUE(!Matches("foo"));
}

TEST_F(BloomTest, VaryingLengths) {
  char buffer[sizeof(size_t)];

  // Count number of filters that significantly exceed the false positive rate
  size_t mediocre_filters = 0;
  size_t good_filters = 0;

  for (size_t length = 1; length <= 10000; length = NextLength(length)) {
    Reset();
    for (size_t i = 0; i < length; i++) {
      Add(Key(i, buffer));
    }
    Build();

    ASSERT_LE(FilterSize(), (size_t)((length * 10 / 8) + 40)) << length;

    // All added keys must match
    for (size_t i = 0; i < length; i++) {
      ASSERT_TRUE(Matches(Key(i, buffer)))
          << "Length " << length << "; key " << i;
    }

    // Check false positive rate
    double rate = FalsePositiveRate();
    if (kVerbose >= 1) {
      LOG(INFO) << StringPrintf(
          "False positives: %5.2f%% @ length = %6zu ; bytes = %6zu\n", rate * 100.0, length,
          FilterSize());
    }
    ASSERT_LE(rate, 0.02);   // Must not be over 2%
    if (rate > 0.0125)
      mediocre_filters++;  // Allowed, but not too often
    else
      good_filters++;
  }
  if (kVerbose >= 1) {
    LOG(INFO) << StringPrintf("Filters: %zu good, %zu mediocre\n", good_filters, mediocre_filters);
  }
  ASSERT_LE(mediocre_filters, good_filters/5);
}

// Different bits-per-byte

class BloomTestContext {
 public:
  virtual ~BloomTestContext() {}

  virtual const FilterPolicy& filter_policy() const = 0;
  virtual size_t max_keys() const = 0;
  virtual void CheckFilterSize(size_t filter_size, size_t num_keys) const = 0;
};

class FullFilterBloomTestContext : public BloomTestContext {
 public:
  const FilterPolicy& filter_policy() const override { return *filter_policy_.get(); }

  size_t max_keys() const override { return 10000; }

  void CheckFilterSize(size_t filter_size, size_t num_keys) const override {
    ASSERT_LE(filter_size, (size_t)((num_keys * 10 / 8) + 128 + 5)) << num_keys;
  }

 private:
  std::unique_ptr<const FilterPolicy> filter_policy_{
      NewBloomFilterPolicy(FLAGS_bits_per_key, false)};
};

class FixedSizeFilterBloomTestContext : public BloomTestContext {
 public:
  const FilterPolicy& filter_policy() const override { return *filter_policy_.get(); }

  // For fixed-size filter we limit maximum number of keys depending on total bits in test itself
  // by checking ShouldFlush().
  size_t max_keys() const override { return std::numeric_limits<size_t>::max(); }

  void CheckFilterSize(size_t filter_size, size_t num_keys) const override {
    ASSERT_LE(filter_size, FilterPolicy::kDefaultFixedSizeFilterBits / 8 + 64 + 5) << num_keys;
  }

 private:
  std::unique_ptr<const FilterPolicy> filter_policy_{
      NewFixedSizeFilterPolicy(
          FilterPolicy::kDefaultFixedSizeFilterBits, FilterPolicy::kDefaultFixedSizeFilterErrorRate,
          nullptr)};
};

YB_DEFINE_ENUM(BuilderReaderBloomTestType, (kFullFilter)(kFixedSizeFilter));

namespace {

std::unique_ptr<const BloomTestContext> CreateContext(BuilderReaderBloomTestType type) {
  switch (type) {
    case BuilderReaderBloomTestType::kFullFilter:
      return std::make_unique<FullFilterBloomTestContext>();
    case BuilderReaderBloomTestType::kFixedSizeFilter:
      return std::make_unique<FixedSizeFilterBloomTestContext>();
  }
  FATAL_INVALID_ENUM_VALUE(BuilderReaderBloomTestType, type);
}

} // namespace

class BuilderReaderBloomTest : public RocksDBTest,
    public ::testing::WithParamInterface<BuilderReaderBloomTestType> {
 protected:
  std::unique_ptr<const BloomTestContext> context_;
  std::unique_ptr<FilterBitsBuilder> bits_builder_;
  std::unique_ptr<FilterBitsReader> bits_reader_;
  std::unique_ptr<const char[]> buf_;
  size_t filter_size_;

 public:
  BuilderReaderBloomTest() :
      context_(CreateContext(GetParam())),
      filter_size_(0) {
    Reset();
  }

  void Reset() {
    bits_builder_.reset(context_->filter_policy().GetFilterBitsBuilder());
    bits_reader_.reset(nullptr);
    buf_.reset(nullptr);
    filter_size_ = 0;
  }

  void Add(const Slice& s) {
    bits_builder_->AddKey(s);
  }

  bool ShouldFlush() {
    return bits_builder_->IsFull();
  }

  void Build() {
    Slice filter = bits_builder_->Finish(&buf_);
    bits_reader_.reset(context_->filter_policy().GetFilterBitsReader(filter));
    filter_size_ = filter.size();
  }

  size_t FilterSize() const {
    return filter_size_;
  }

  bool Matches(const Slice& s) {
    if (bits_reader_ == nullptr) {
      Build();
    }
    return bits_reader_->MayMatch(s);
  }

  double FalsePositiveRate() {
    char buffer[sizeof(size_t)];
    size_t result = 0;
    for (size_t i = 0; i < 10000; i++) {
      if (Matches(Key(i + 1000000000, buffer))) {
        result++;
      }
    }
    return result / 10000.0;
  }
};

TEST_P(BuilderReaderBloomTest, FullEmptyFilter) {
  // Empty filter is not match, at this level
  ASSERT_TRUE(!Matches("hello"));
  ASSERT_TRUE(!Matches("world"));
}

TEST_P(BuilderReaderBloomTest, FullSmall) {
  Add("hello");
  Add("world");
  ASSERT_TRUE(Matches("hello"));
  ASSERT_TRUE(Matches("world"));
  ASSERT_TRUE(!Matches("x"));
  ASSERT_TRUE(!Matches("foo"));
}

TEST_P(BuilderReaderBloomTest, FullVaryingLengths) {
  char buffer[sizeof(size_t)];

  // Count number of filters that significantly exceed the false positive rate
  size_t mediocre_filters = 0;
  size_t good_filters = 0;

  for (size_t length = 1; !ShouldFlush() && length < context_->max_keys();
      length = NextLength(length)) {
    Reset();
    for (size_t i = 0; i < length; i++) {
      Add(Key(i, buffer));
      if (ShouldFlush()) {
        length = i + 1;
        break;
      }
    }
    Build();

    context_->CheckFilterSize(FilterSize(), length);

    // All added keys must match
    for (size_t i = 0; i < length; i++) {
      ASSERT_TRUE(Matches(Key(i, buffer))) << "Length " << length << "; key " << i;
    }

    // Check false positive rate
    double rate = FalsePositiveRate();
    if (kVerbose >= 1) {
      LOG(INFO) << StringPrintf(
          "False positives: %5.2f%% @ length = %6zu ; bytes = %6zu\n", rate * 100.0, length,
          FilterSize());
    }
    ASSERT_LE(rate, 0.02);   // Must not be over 2%
    if (rate > 0.0125)
      mediocre_filters++;  // Allowed, but not too often
    else
      good_filters++;
  }
  if (kVerbose >= 1) {
    LOG(INFO) << StringPrintf("Filters: %zu good, %zu mediocre\n", good_filters, mediocre_filters);
  }
  ASSERT_LE(mediocre_filters, good_filters/5);
}

INSTANTIATE_TEST_CASE_P(, BuilderReaderBloomTest, ::testing::Values(
    BuilderReaderBloomTestType::kFullFilter,
    BuilderReaderBloomTestType::kFixedSizeFilter));

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}

#endif  // GFLAGS
