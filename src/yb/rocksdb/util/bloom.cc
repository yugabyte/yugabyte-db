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

#include <math.h>

#include "yb/rocksdb/filter_policy.h"

#include "yb/rocksdb/util/hash.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/util/slice.h"
#include "yb/util/math_util.h"

namespace rocksdb {

class BlockBasedFilterBlockBuilder;
class FullFilterBlockBuilder;
class FixedSizeFilterBlockBuilder;
typedef FilterPolicy::FilterType FilterType;

namespace {
static const double LOG2 = log(2);

inline void AddHash(uint32_t h, char* data, size_t num_lines, size_t total_bits,
    size_t num_probes) {
  DCHECK_GT(num_lines, 0);
  DCHECK_GT(total_bits, 0);
  DCHECK_EQ(num_lines % 2, 1);

  const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
  size_t b = (h % num_lines) * (CACHE_LINE_SIZE * 8);

  for (uint32_t i = 0; i < num_probes; ++i) {
    // Since CACHE_LINE_SIZE is defined as 2^n, this line will be optimized
    // to a simple operation by compiler.
    const size_t bitpos = b + (h % (CACHE_LINE_SIZE * 8));
    assert(bitpos < total_bits);
    data[bitpos / 8] |= (1 << (bitpos % 8));

    h += delta;
  }
}

class FullFilterBitsBuilder : public FilterBitsBuilder {
 public:
  explicit FullFilterBitsBuilder(const size_t bits_per_key,
                                 const size_t num_probes)
      : bits_per_key_(bits_per_key),
        num_probes_(num_probes) {
    assert(bits_per_key_);
  }

  ~FullFilterBitsBuilder() {}

  void AddKey(const Slice& key) override {
    uint32_t hash = BloomHash(key);
    if (hash_entries_.size() == 0 || hash != hash_entries_.back()) {
      hash_entries_.push_back(hash);
    }
  }

  // Create a filter that for hashes [0, n-1], the filter is allocated here
  // When creating filter, it is ensured that
  // total_bits = num_lines * CACHE_LINE_SIZE * 8
  // dst len is >= kMetaDataSize (5), 1 for num_probes, 4 for num_lines
  // Then total_bits = (len - kMetaDataSize) * 8, and cache_line_size could be calculated
  // +----------------------------------------------------------------+
  // |              filter data with length total_bits / 8            |
  // +----------------------------------------------------------------+
  // |                                                                |
  // | ...                                                            |
  // |                                                                |
  // +----------------------------------------------------------------+
  // | ...                | num_probes : 1 byte | num_lines : 4 bytes |
  // +----------------------------------------------------------------+
  Slice Finish(std::unique_ptr<const char[]>* buf) override {
    uint32_t total_bits, num_lines;
    char* data = ReserveSpace(static_cast<int>(hash_entries_.size()),
                              &total_bits, &num_lines);
    assert(data);

    if (total_bits != 0 && num_lines != 0) {
      for (auto h : hash_entries_) {
        AddHash(h, data, num_lines, total_bits, num_probes_);
      }
    }
    data[total_bits / 8] = static_cast<char>(num_probes_);
    EncodeFixed32(data + total_bits / 8 + 1, static_cast<uint32_t>(num_lines));

    const char* const_data = data;
    buf->reset(const_data);
    hash_entries_.clear();

    return Slice(data, total_bits / 8 + kMetaDataSize);
  }

  virtual bool IsFull() const override { return false; }

  static constexpr size_t kMetaDataSize = 5; // in bytes

 private:
  size_t bits_per_key_;
  size_t num_probes_;
  std::vector<uint32_t> hash_entries_;

  // Get totalbits that optimized for cpu cache line
  uint32_t GetTotalBitsForLocality(uint32_t total_bits);

  // Reserve space for new filter
  char* ReserveSpace(const int num_entry, uint32_t* total_bits,
      uint32_t* num_lines);

  // No Copy allowed
  FullFilterBitsBuilder(const FullFilterBitsBuilder&);
  void operator=(const FullFilterBitsBuilder&);
};

uint32_t FullFilterBitsBuilder::GetTotalBitsForLocality(uint32_t total_bits) {
  uint32_t num_lines = yb::ceil_div(total_bits, CACHE_LINE_SIZE * 8);

  // Make num_lines an odd number to make sure more bits are involved
  // when determining which block.
  if (num_lines % 2 == 0) {
    num_lines++;
  }
  return num_lines * (CACHE_LINE_SIZE * 8);
}

char* FullFilterBitsBuilder::ReserveSpace(const int num_entry,
    uint32_t* total_bits, uint32_t* num_lines) {
  assert(bits_per_key_);
  char* data = nullptr;
  if (num_entry != 0) {
    uint32_t total_bits_tmp = num_entry * static_cast<uint32_t>(bits_per_key_);

    *total_bits = GetTotalBitsForLocality(total_bits_tmp);
    *num_lines = *total_bits / (CACHE_LINE_SIZE * 8);
    assert(*total_bits > 0 && *total_bits % 8 == 0);
  } else {
    // filter is empty, just leave space for metadata
    *total_bits = 0;
    *num_lines = 0;
  }

  // Reserve space for Filter
  uint32_t sz = *total_bits / 8;
  sz += kMetaDataSize;  // 4 bytes for num_lines, 1 byte for num_probes

  data = new char[sz];
  memset(data, 0, sz);
  return data;
}


class FullFilterBitsReader : public FilterBitsReader {
 public:
  explicit FullFilterBitsReader(const Slice& contents, Logger* logger)
      : logger_(logger),
        data_(const_cast<char*>(contents.cdata())),
        data_len_(static_cast<uint32_t>(contents.size())),
        num_probes_(0),
        num_lines_(0) {
    assert(data_);
    GetFilterMeta(contents, &num_probes_, &num_lines_);
    // Sanitize broken parameters
    if (num_lines_ != 0 && data_len_ != num_lines_ * CACHE_LINE_SIZE +
        FullFilterBitsBuilder::kMetaDataSize) {
      RLOG(InfoLogLevel::ERROR_LEVEL, logger, "Bloom filter data is broken, won't be used.");
      FAIL_IF_NOT_PRODUCTION();
      num_lines_ = 0;
      num_probes_ = 0;
    }
  }

  ~FullFilterBitsReader() {}

  bool MayMatch(const Slice& entry) override {
    if (data_len_ <= FullFilterBitsBuilder::kMetaDataSize) { // remain same with original filter
      return false;
    }
    // Other Error params, including a broken filter, regarded as match
    if (num_probes_ == 0 || num_lines_ == 0) return true;
    uint32_t hash = BloomHash(entry);
    return HashMayMatch(hash, Slice(data_, data_len_),
                        num_probes_, num_lines_);
  }

 private:
  Logger* logger_;
  // Filter meta data
  char* data_;
  uint32_t data_len_;
  size_t num_probes_;
  uint32_t num_lines_;

  // Get num_probes, and num_lines from filter
  // If filter format broken, set both to 0.
  void GetFilterMeta(const Slice& filter, size_t* num_probes,
                             uint32_t* num_lines);

  // "filter" contains the data appended by a preceding call to
  // CreateFilterFromHash() on this class.  This method must return true if
  // the key was in the list of keys passed to CreateFilter().
  // This method may return true or false if the key was not on the
  // list, but it should aim to return false with a high probability.
  //
  // hash: target to be checked
  // filter: the whole filter, including meta data bytes
  // num_probes: number of probes, read before hand
  // num_lines: filter metadata, read before hand
  // Before calling this function, need to ensure the input meta data
  // is valid.
  bool HashMayMatch(const uint32_t hash, const Slice& filter,
      const size_t num_probes, const uint32_t num_lines);

  // No Copy allowed
  FullFilterBitsReader(const FullFilterBitsReader&);
  void operator=(const FullFilterBitsReader&);
};

void FullFilterBitsReader::GetFilterMeta(const Slice& filter,
    size_t* num_probes, uint32_t* num_lines) {
  uint32_t len = static_cast<uint32_t>(filter.size());
  if (len <= FullFilterBitsBuilder::kMetaDataSize) {
    // filter is empty or broken
    *num_probes = 0;
    *num_lines = 0;
    return;
  }

  *num_probes = filter.data()[len - FullFilterBitsBuilder::kMetaDataSize];
  *num_lines = DecodeFixed32(filter.data() + len - 4);
}

inline bool FullFilterBitsReader::HashMayMatch(const uint32_t hash, const Slice& filter,
    const size_t num_probes, const uint32_t num_lines) {
  uint32_t len = static_cast<uint32_t>(filter.size());
  if (len <= FullFilterBitsBuilder::kMetaDataSize)
    return false; // Remain the same with original filter.

  // It is ensured the params are valid before calling it
  assert(num_probes != 0);
  assert(num_lines != 0 &&
      (len - FullFilterBitsBuilder::kMetaDataSize) % num_lines == 0);
  // cache_line_size is calculated here based on filter metadata instead of using CACHE_LINE_SIZE.
  // The reason may be to support deserialization of filters which are already persisted in case we
  // change CACHE_LINE_SIZE or if machine architecture is changed.
  uint32_t cache_line_size = (len - FullFilterBitsBuilder::kMetaDataSize) / num_lines;
  const char* data = filter.cdata();

  uint32_t h = hash;
  const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
  uint32_t b = (h % num_lines) * (cache_line_size * 8);

  for (uint32_t i = 0; i < num_probes; ++i) {
    // Since CACHE_LINE_SIZE is defined as 2^n, this line will be optimized
    //  to a simple and operation by compiler.
    const uint32_t bitpos = b + (h % (cache_line_size * 8));
    if (((data[bitpos / 8]) & (1 << (bitpos % 8))) == 0) {
      return false;
    }

    h += delta;
  }

  return true;
}

// An implementation of filter policy
class BloomFilterPolicy : public FilterPolicy {
 public:
  explicit BloomFilterPolicy(int bits_per_key, bool use_block_based_builder)
      : bits_per_key_(bits_per_key), hash_func_(BloomHash),
        use_block_based_builder_(use_block_based_builder) {
    initialize();
  }

  ~BloomFilterPolicy() {
  }

  FilterType GetFilterType() const override {
    return use_block_based_builder_ ? FilterType::kBlockBasedFilter : FilterType::kFullFilter;
  }

  const char* Name() const override {
    return "rocksdb.BuiltinBloomFilter";
  }

  void CreateFilter(const Slice* keys, int n,
                            std::string* dst) const override {
    // Compute bloom filter size (in both bits and bytes)
    size_t bits = n * bits_per_key_;

    // For small n, we can see a very high false positive rate.  Fix it
    // by enforcing a minimum bloom filter length.
    bits = std::max<size_t>(bits, 128);

    size_t bytes = (bits + 7) / 8;
    bits = bytes * 8;

    const size_t init_size = dst->size();
    dst->resize(init_size + bytes, 0);
    dst->push_back(static_cast<char>(num_probes_));  // Remember # of probes
    char* array = &(*dst)[init_size];
    for (size_t i = 0; i < (size_t)n; i++) {
      // Use double-hashing to generate a sequence of hash values.
      // See analysis in [Kirsch,Mitzenmacher 2006].
      uint32_t h = hash_func_(keys[i]);
      const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
      for (size_t j = 0; j < num_probes_; j++) {
        const uint32_t bitpos = h % bits;
        array[bitpos / 8] |= (1 << (bitpos % 8));
        h += delta;
      }
    }
  }

  bool KeyMayMatch(const Slice& key,
                           const Slice& bloom_filter) const override {
    const size_t len = bloom_filter.size();
    if (len < 2) return false;

    const char* array = bloom_filter.cdata();
    const size_t bits = (len - 1) * 8;

    // Use the encoded k so that we can read filters generated by
    // bloom filters created using different parameters.
    const size_t k = array[len-1];
    if (k > 30) {
      // Reserved for potentially new encodings for short bloom filters.
      // Consider it a match.
      return true;
    }

    uint32_t h = hash_func_(key);
    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
    for (size_t j = 0; j < k; j++) {
      const uint32_t bitpos = h % bits;
      if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) return false;
      h += delta;
    }
    return true;
  }

  FilterBitsBuilder* GetFilterBitsBuilder() const override {
    if (use_block_based_builder_) {
      return nullptr;
    }

    return new FullFilterBitsBuilder(bits_per_key_, num_probes_);
  }

  FilterBitsReader* GetFilterBitsReader(const Slice& contents)
      const override {
    return new FullFilterBitsReader(contents, nullptr);
  }

  // If choose to use block based builder
  bool UseBlockBasedBuilder() { return use_block_based_builder_; }

 private:
  size_t bits_per_key_;
  size_t num_probes_;
  uint32_t (*hash_func_)(const Slice& key);

  const bool use_block_based_builder_;

  void initialize() {
    // We intentionally round down to reduce probing cost a little bit
    num_probes_ = static_cast<size_t>(bits_per_key_ * 0.69);  // 0.69 =~ ln(2)
    if (num_probes_ < 1) num_probes_ = 1;
    if (num_probes_ > 30) num_probes_ = 30;
  }
};

// A fixed size filter bits builder will build (with memory allocation)
// and return a Bloom filter of given size and expected false positive rate.
//
// The fixed size Bloom filter has the following encoding:
// For a given number of total bits M and the error rate p,
// we will return a block of M + 40 bits, with 40 bits for metadata
// and M bits for filter data.
// For compliance with FullFilter, the metadata will be encoded
// the same way as in FullFilter.
//
// For detailed proofs on the optimal number of keys and hash functions
// please refer to https://en.wikipedia.org/wiki/Bloom_filter.
//
// The number of hash function given error rate p is -ln p / ln 2.
// The maximum number of keys that can be inserted in a Bloom filter of m bits
// so that one maintains the false positive error rate p is -m (ln 2)^2 / ln p.
class FixedSizeFilterBitsBuilder : public FilterBitsBuilder {
 public:
  FixedSizeFilterBitsBuilder(const FixedSizeFilterBitsBuilder&) = delete;
  void operator=(const FixedSizeFilterBitsBuilder&) = delete;

  FixedSizeFilterBitsBuilder(size_t total_bits, double error_rate)
      : error_rate_(error_rate) {
    DCHECK_GT(error_rate, 0);
    DCHECK_GT(total_bits, 0);
    num_lines_ = yb::ceil_div<size_t>(total_bits, CACHE_LINE_SIZE * 8);
    // AddHash implementation gives much higher false positive rate when num_lines_ is even, so
    // make sure it is odd.
    if (num_lines_ % 2 == 0) {
      // For small filter blocks - add one line, so we can have enough keys in block.
      // For bigger filter block - remove one line, so filter block will fit desired size.
      if (num_lines_ * CACHE_LINE_SIZE < 4096) {
        num_lines_++;
      } else {
        num_lines_--;
      }
    }
    total_bits_ = num_lines_ * CACHE_LINE_SIZE * 8;

    const double minus_log_error_rate = -log(error_rate_);
    DCHECK_GT(minus_log_error_rate, 0);
    num_probes_ = static_cast<size_t> (minus_log_error_rate / LOG2);
    num_probes_ = std::max<size_t>(num_probes_, 1);
    num_probes_ = std::min<size_t>(num_probes_, 255);
    const double max_keys = total_bits_ * LOG2 * LOG2 / minus_log_error_rate;
    DCHECK_LT(max_keys, std::numeric_limits<size_t>::max());
    max_keys_ = static_cast<size_t> (max_keys);
    keys_added_ = 0;

    // TODO - add tests verifying that after inserting max_keys we will have required error rate

    data_.reset(new char[FilterSize()]);
    memset(data_.get(), 0, FilterSize());
  }

  virtual void AddKey(const Slice& key) override {
    ++keys_added_;
    uint32_t hash = BloomHash(key);
    AddHash(hash, data_.get(), num_lines_, total_bits_, num_probes_);
  }

  virtual bool IsFull() const override { return keys_added_ >= max_keys_; }

  virtual Slice Finish(std::unique_ptr<const char[]>* buf) override {
    data_[total_bits_ / 8] = static_cast<char>(num_probes_);
    EncodeFixed32(data_.get() + total_bits_ / 8 + 1, static_cast<uint32_t>(num_lines_));
    buf->reset(data_.release());
    return Slice(buf->get(), FilterSize());
  }

  // Serialization format is the same as for FullFilter.
  static constexpr size_t kMetaDataSize = FullFilterBitsBuilder::kMetaDataSize;

 private:

  inline size_t FilterSize() { return total_bits_ / 8 + kMetaDataSize; }

  std::unique_ptr<char[]> data_;
  size_t max_keys_;
  size_t keys_added_;
  size_t total_bits_; // total number of bits used for filter (excluding metadata)
  size_t num_lines_;
  double error_rate_;
  size_t num_probes_; // number of hash functions
};

class FixedSizeFilterBitsReader : public FullFilterBitsReader {
 public:
  FixedSizeFilterBitsReader(const FixedSizeFilterBitsReader&) = delete;
  void operator=(const FixedSizeFilterBitsReader&) = delete;

  explicit FixedSizeFilterBitsReader(const Slice& contents, Logger* logger)
      : FullFilterBitsReader(contents, logger) {}
};

class FixedSizeFilterPolicy : public FilterPolicy {
 public:
  explicit FixedSizeFilterPolicy(size_t total_bits, double error_rate, Logger* logger)
      : total_bits_(total_bits),
        error_rate_(error_rate),
        logger_(logger) {
    DCHECK_GT(error_rate, 0);
    // Make sure num_probes > 0.
    DCHECK_GT(static_cast<int64_t> (-log(error_rate) / LOG2), 0);
  }

  virtual FilterType GetFilterType() const override { return FilterType::kFixedSizeFilter; }

  virtual const char* Name() const override {
    return "rocksdb.FixedSizeBloomFilter";
  }

  // Not used in FixedSizeFilter. GetFilterBitsBuilder/Reader interface should be used.
  virtual void CreateFilter(const Slice* keys, int n,
                            std::string* dst) const override {
    assert(!"FixedSizeFilterPolicy::CreateFilter is not supported");
  }

  virtual bool KeyMayMatch(const Slice& key, const Slice& filter) const override {
    assert(!"FixedSizeFilterPolicy::KeyMayMatch is not supported");
    return true;
  }

  virtual FilterBitsBuilder* GetFilterBitsBuilder() const override {
    return new FixedSizeFilterBitsBuilder(total_bits_, error_rate_);
  }

  virtual FilterBitsReader* GetFilterBitsReader(const Slice& contents) const override {
    return new FixedSizeFilterBitsReader(contents, logger_);
  }


 private:
  size_t total_bits_;
  double error_rate_;
  Logger* logger_;
};

}  // namespace

const FilterPolicy* NewBloomFilterPolicy(int bits_per_key,
                                         bool use_block_based_builder) {
  return new BloomFilterPolicy(bits_per_key, use_block_based_builder);
  // TODO - replace by NewFixedSizeFilterPolicy and check tests.
}

const FilterPolicy* NewFixedSizeFilterPolicy(size_t total_bits,
                                             double error_rate,
                                             Logger* logger) {
  return new FixedSizeFilterPolicy(total_bits, error_rate, logger);
}

}  // namespace rocksdb
