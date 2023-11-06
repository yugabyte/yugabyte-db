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
#include <stdint.h>

#include <memory>

#include <glog/logging.h>

#include "yb/rocksdb/comparator.h"
#include "yb/util/slice.h"

namespace rocksdb {

Comparator::~Comparator() { }

namespace {
class BytewiseComparatorImpl : public Comparator {
 public:
  BytewiseComparatorImpl() { }

  const char* Name() const override {
    return "leveldb.BytewiseComparator";
  }

  int Compare(Slice a, Slice b) const override {
    return a.compare(b);
  }

  bool Equal(Slice a, Slice b) const override {
    return a == b;
  }

  virtual void FindShortestSeparator(std::string* start, const Slice& limit) const override {
    const uint8_t* start_bytes = pointer_cast<const uint8_t*>(start->data());
    const uint8_t* limit_bytes = limit.data();
    // Find length of common prefix
    size_t min_length = std::min(start->size(), limit.size());
    size_t diff_index = 0;
    while ((diff_index < min_length) &&
           (start_bytes[diff_index] == limit_bytes[diff_index])) {
      diff_index++;
    }

    if (diff_index >= min_length) {
      // Do not shorten if one string is a prefix of the other
    } else {
      uint8_t start_byte = start_bytes[diff_index];
      uint8_t limit_byte = limit_bytes[diff_index];
      if (start_byte > limit_byte) {
        // Cannot shorten since limit is smaller than start.
        return;
      }
      DCHECK_LT(start_byte, limit_byte);

      if (diff_index == limit.size() - 1 && start_byte + 1 == limit_byte) {
        //     v
        // A A 1 A A A
        // A A 2
        //
        // Incrementing the current byte will make start bigger than limit, we
        // will skip this byte, and find the first non 0xFF byte in start and
        // increment it.
        ++diff_index;
        while (diff_index < start->size() && start_bytes[diff_index] == 0xffU) { ++diff_index; }
        if (diff_index == start->size()) {
          return;
        }
      }
      (*start)[diff_index]++;
      start->resize(diff_index + 1);
      DCHECK_LT(Compare(*start, limit), 0);
    }
  }

  void FindShortSuccessor(std::string* key) const override {
    // Find first character that can be incremented
    size_t n = key->size();
    for (size_t i = 0; i < n; i++) {
      const uint8_t byte = (*key)[i];
      if (byte != static_cast<uint8_t>(0xff)) {
        (*key)[i] = byte + 1;
        key->resize(i+1);
        return;
      }
    }
    // *key is a run of 0xffs.  Leave it alone.
  }
};

class ReverseBytewiseComparatorImpl : public BytewiseComparatorImpl {
 public:
  ReverseBytewiseComparatorImpl() { }

  const char* Name() const override {
    return "rocksdb.ReverseBytewiseComparator";
  }

  int Compare(Slice a, Slice b) const override {
    return -a.compare(b);
  }
};

}  // namespace

const ComparatorPtr& SharedBytewiseComparator() {
  // Comparator should be shared ptr, because we use shared_from_this to store it in index reader.
  static ComparatorPtr bytewise = std::make_shared<BytewiseComparatorImpl>();
  return bytewise;
}

const Comparator* BytewiseComparator() {
  // Comparator should be shared ptr, because we use shared_from_this to store it in index reader.
  return SharedBytewiseComparator().get();
}

const Comparator* ReverseBytewiseComparator() {
  static ComparatorPtr rbytewise = std::make_shared<ReverseBytewiseComparatorImpl>();
  return rbytewise.get();
}

class Uint64ComparatorImpl : public Comparator {
 public:
  Uint64ComparatorImpl() { }

  const char* Name() const override {
    return "rocksdb.Uint64Comparator";
  }

  int Compare(Slice a, Slice b) const override {
    assert(a.size() == sizeof(uint64_t) && b.size() == sizeof(uint64_t));
    const uint64_t* left = reinterpret_cast<const uint64_t*>(a.data());
    const uint64_t* right = reinterpret_cast<const uint64_t*>(b.data());
    if (*left == *right) {
      return 0;
    } else if (*left < *right) {
      return -1;
    } else {
      return 1;
    }
  }

  virtual void FindShortestSeparator(std::string* start,
      const Slice& limit) const override {
    return;
  }

  void FindShortSuccessor(std::string* key) const override {
    return;
  }
};

const Comparator* Uint64Comparator() {
  static ComparatorPtr uint64comparator = std::make_shared<Uint64ComparatorImpl>();
  return uint64comparator.get();
}

}  // namespace rocksdb
