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

#include <vector>

#include "yb/util/logging.h"

namespace yb {

// Light-weight hashtable implementation for mapping a small number of
// integers to other integers.
// This is used by Schema to map from Column ID to column index.
//
// The implementation is an open-addressed hash table with linear probing.
// The probing is limited to look only in the initial position and a single
// position following. If neither position is free, the hashtable is doubled.
// Therefore, the fill rate of the hashtable could be fairly bad, in the worst
// case. However, in practice, we expect that most tables will have nearly
// sequential column IDs (with only the occasional gap if a column has been removed).
// Therefore, we expect to have very few collisions.
//
// The implementation takes care to only use power-of-2 sized bucket arrays so that
// modulo can be calculated using bit masking. This improves performance substantially
// since the 'div' instruction can take many cycles.
//
// NOTE: this map restricts that keys and values are positive. '-1' is used
// as a special identifier indicating that the slot is unused or that the key
// was not found.
class IdMapping {
 private:
  enum {
    kInitialCapacity = 64,
    kNumProbes = 2
  };
  typedef std::pair<int, int> value_type;

 public:
  static const int kNoEntry;

  IdMapping() :
    mask_(kInitialCapacity - 1),
    entries_(kInitialCapacity) {
    clear();
  }

  explicit IdMapping(const IdMapping& other)
  : mask_(other.mask_),
    entries_(other.entries_) {
  }

  ~IdMapping() {}

  void clear() {
    ClearMap(&entries_);
  }

  // NOLINT on this function definition because it thinks we're calling
  // std::swap instead of defining it.
  void swap(IdMapping& other) { // NOLINT(*)
    uint64_t tmp = other.mask_;
    other.mask_ = mask_;
    mask_ = tmp;
    other.entries_.swap(entries_);
  }

  IdMapping& operator=(const IdMapping& other) {
    mask_ = other.mask_;
    entries_ = other.entries_;
    return *this;
  }

  int operator[](int key) const {
    return get(key);
  }

  int get(int key) const {
    DCHECK_GE(key, 0);
    for (int i = 0; i < kNumProbes; i++) {
      int s = slot(key + i);
      if (entries_[s].first == key || entries_[s].first == kNoEntry) {
        return entries_[s].second;
      }
    }
    return kNoEntry;
  }

  void set(int key, int val) {
    DCHECK_GE(key, 0);
    DCHECK_GE(val, 0);
    while (true) {
      for (int i = 0; i < kNumProbes; i++) {
        int s = slot(key + i);
        CHECK_NE(entries_[s].first, key) << "Cannot insert duplicate keys";
        if (entries_[s].first == kNoEntry) {
          entries_[s].first = key;
          entries_[s].second = val;
          return;
        }
      }
      // Didn't find a spot.
      DoubleCapacity();
    }
  }

  size_t capacity() const {
    return mask_ + 1;
  }

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

  friend bool operator==(const IdMapping& lhs, const IdMapping& rhs);

 private:
  int slot(int key) const {
    return key & mask_;
  }

  void DoubleCapacity() {
    auto new_capacity = capacity() * 2;
    std::vector<value_type> entries(new_capacity);
    ClearMap(&entries);
    mask_ = new_capacity - 1;
    entries.swap(entries_);

    for (const auto& entry : entries) {
      if (entry.first != kNoEntry) {
        set(entry.first, entry.second);
      }
    }
  }

  static void ClearMap(std::vector<value_type>* v) {
    for (auto& entry : *v) {
      entry = std::make_pair(kNoEntry, kNoEntry);
    }
  }

  uint64_t mask_;
  std::vector<value_type> entries_;
};

} // namespace yb
