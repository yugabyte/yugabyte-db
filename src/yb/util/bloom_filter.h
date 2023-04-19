// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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
#pragma once

#include "yb/gutil/hash/city.h"
#include "yb/gutil/macros.h"
#include "yb/util/bitmap.h"
#include "yb/util/slice.h"

namespace yb {

// Probe calculated from a given key. This caches the calculated
// hash values which are necessary for probing into a Bloom Filter,
// so that when many bloom filters have to be consulted for a given
// key, we only need to calculate the hashes once.
//
// This is implemented based on the idea of double-hashing from the following paper:
//   "Less Hashing, Same Performance: Building a Better Bloom Filter"
//   Kirsch and Mitzenmacher, ESA 2006
//   http://www.eecs.harvard.edu/~kirsch/pubs/bbbf/esa06.pdf
//
// Currently, the implementation uses the 64-bit City Hash.
// TODO: an SSE CRC32 hash is probably ~20% faster. Come back to this
// at some point.
class BloomKeyProbe {
 public:
  // Default constructor - this is only used to instantiate an object
  // and later reassign by assignment from another instance
  BloomKeyProbe() {}

  // Construct a probe from the given key.
  //
  // NOTE: proper operation requires that the referenced memory remain
  // valid for the lifetime of this object.
  explicit BloomKeyProbe(const Slice &key) : key_(key) {
    uint64_t h = util_hash::CityHash64(
      reinterpret_cast<const char *>(key.data()),
      key.size());

    // Use the top and bottom halves of the 64-bit hash
    // as the two independent hash functions for mixing.
    h_1_ = static_cast<uint32>(h);
    h_2_ = static_cast<uint32>(h >> 32);
  }

  const Slice &key() const { return key_; }

  // The initial hash value. See MixHash() for usage example.
  uint32_t initial_hash() const {
    return h_1_;
  }

  // Mix the given hash function with the second calculated hash
  // value. A sequence of independent hashes can be calculated
  // by repeatedly calling MixHash() on its previous result.
  uint32_t MixHash(uint32_t h) const {
    return h + h_2_;
  }

 private:
  Slice key_;

  // The two hashes.
  uint32_t h_1_;
  uint32_t h_2_;
};

// Sizing parameters for the constructor to BloomFilterBuilder.
// This is simply to provide a nicer API than a bunch of overloaded
// constructors.
class BloomFilterSizing {
 public:
  // Size the bloom filter by a fixed size and false positive rate.
  //
  // Picks the number of entries to achieve the above.
  static BloomFilterSizing BySizeAndFPRate(size_t n_bytes, double fp_rate);

  // Size the bloom filer by an expected count and false positive rate.
  //
  // Picks the number of bytes to achieve the above.
  static BloomFilterSizing ByCountAndFPRate(size_t expected_count, double fp_rate);

  size_t n_bytes() const { return n_bytes_; }
  size_t expected_count() const { return expected_count_; }

 private:
  BloomFilterSizing(size_t n_bytes, size_t expected_count) :
    n_bytes_(n_bytes),
    expected_count_(expected_count)
  {}

  size_t n_bytes_;
  size_t expected_count_;
};


// Builder for a BloomFilter structure.
class BloomFilterBuilder {
 public:
  // Create a bloom filter.
  // See BloomFilterSizing static methods to specify this argument.
  explicit BloomFilterBuilder(const BloomFilterSizing &sizing);

  // Clear all entries, reset insertion count.
  void Clear();

  // Add the given key to the bloom filter.
  void AddKey(const BloomKeyProbe &probe);

  // Return an estimate of the false positive rate.
  double false_positive_rate() const;

  size_t n_bytes() const {
    return n_bits_ / 8;
  }

  size_t n_bits() const {
    return n_bits_;
  }

  // Return a slice view into this Bloom Filter, suitable for
  // writing out to a file.
  const Slice slice() const {
    return Slice(&bitmap_[0], n_bytes());
  }

  // Return the number of hashes that are calculated for each entry
  // in the bloom filter.
  size_t n_hashes() const { return n_hashes_; }

  size_t expected_count() const { return expected_count_; }

  // Return the number of keys inserted.
  size_t count() const { return n_inserted_; }

 private:
  size_t n_bits_;
  std::unique_ptr<uint8_t[]> bitmap_;

  // The number of hash functions to compute.
  size_t n_hashes_;

  // The expected number of elements, for which the bloom is optimized.
  size_t expected_count_;

  // The number of elements inserted so far since the last Reset.
  size_t n_inserted_;

  DISALLOW_COPY_AND_ASSIGN(BloomFilterBuilder);
};

// Wrapper around a byte array for reading it as a bloom filter.
class BloomFilter {
 public:
  BloomFilter(const Slice &data, size_t n_hashes);

  // Return true if the filter may contain the given key.
  bool MayContainKey(const BloomKeyProbe &probe) const;

 private:
  friend class BloomFilterBuilder;
  static uint32_t PickBit(uint32_t hash, size_t n_bits);

  size_t n_bits_;
  const uint8_t *bitmap_;

  size_t n_hashes_;
};


////////////////////////////////////////////////////////////
// Inline implementations
////////////////////////////////////////////////////////////

inline uint32_t BloomFilter::PickBit(uint32_t hash, size_t n_bits) {
  switch (n_bits) {
    // Fast path for the default bloom filter block size. Bitwise math
    // is much faster than division.
    case 4096 * 8:
      return hash & (n_bits - 1);

    default:
      return hash % n_bits;
  }
}

inline void BloomFilterBuilder::AddKey(const BloomKeyProbe &probe) {
  uint32_t h = probe.initial_hash();
  for (size_t i = 0; i < n_hashes_; i++) {
    uint32_t bitpos = BloomFilter::PickBit(h, n_bits_);
    BitmapSet(&bitmap_[0], bitpos);
    h = probe.MixHash(h);
  }
  n_inserted_++;
}

inline bool BloomFilter::MayContainKey(const BloomKeyProbe &probe) const {
  uint32_t h = probe.initial_hash();

  // Basic unrolling by 2s gives a small benefit here since the two bit positions
  // can be calculated in parallel -- it's a 50% chance that the first will be
  // set even if it's a bloom miss, in which case we can parallelize the load.
  auto rem_hashes = n_hashes_;
  while (rem_hashes >= 2) {
    uint32_t bitpos1 = PickBit(h, n_bits_);
    h = probe.MixHash(h);
    uint32_t bitpos2 = PickBit(h, n_bits_);
    h = probe.MixHash(h);

    if (!BitmapTest(&bitmap_[0], bitpos1) ||
        !BitmapTest(&bitmap_[0], bitpos2)) {
      return false;
    }

    rem_hashes -= 2;
  }

  while (rem_hashes) {
    uint32_t bitpos = PickBit(h, n_bits_);
    if (!BitmapTest(&bitmap_[0], bitpos)) {
      return false;
    }
    h = probe.MixHash(h);
    rem_hashes--;
  }
  return true;
}

} // namespace yb
