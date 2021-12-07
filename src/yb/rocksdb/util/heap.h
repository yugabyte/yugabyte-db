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

#ifndef YB_ROCKSDB_UTIL_HEAP_H
#define YB_ROCKSDB_UTIL_HEAP_H

#pragma once

#include <algorithm>

#include <boost/container/small_vector.hpp>

namespace rocksdb {

// Binary heap implementation optimized for use in multi-way merge sort.
// Comparison to std::priority_queue:
// - In libstdc++, std::priority_queue::pop() usually performs just over logN
//   comparisons but never fewer.
// - std::priority_queue does not have a replace-top operation, requiring a
//   pop+push.  If the replacement element is the new top, this requires
//   around 2logN comparisons.
// - This heap's pop() uses a "schoolbook" downheap which requires up to ~2logN
//   comparisons.
// - This heap provides a replace_top() operation which requires [1, 2logN]
//   comparisons.  When the replacement element is also the new top, this
//   takes just 1 or 2 comparisons.
//
// The last property can yield an order-of-magnitude performance improvement
// when merge-sorting real-world non-random data.  If the merge operation is
// likely to take chunks of elements from the same input stream, only 1
// comparison per element is needed.  In RocksDB-land, this happens when
// compacting a database where keys are not randomly distributed across L0
// files but nearby keys are likely to be in the same L0 file.
//
// The container uses the same counterintuitive ordering as
// std::priority_queue: the comparison operator is expected to provide the
// less-than relation, but top() will return the maximum.

template<typename T, typename Compare = std::less<T>>
class BinaryHeap {
 public:
  BinaryHeap() { }
  explicit BinaryHeap(Compare cmp) : cmp_(std::move(cmp)) { }

  void push(const T& value) {
    data_.push_back(std::move(value));
    std::push_heap(data_.begin(), data_.end(), cmp_);
  }

  void push(T&& value) {
    data_.push_back(std::move(value));
    std::push_heap(data_.begin(), data_.end(), cmp_);
  }

  const T& top() const {
    assert(!empty());
    return data_.front();
  }

  void replace_top(const T& value) {
    assert(!empty());
    data_.front() = value;
    downheap(get_root());
  }

  void replace_top(T&& value) {
    assert(!empty());
    data_.front() = std::move(value);
    downheap(get_root());
  }

  void pop() {
    assert(!empty());
    std::pop_heap(data_.begin(), data_.end(), cmp_);
    data_.pop_back();
  }

  void swap(BinaryHeap &other) {
    std::swap(cmp_, other.cmp_);
    data_.swap(other.data_);
  }

  void clear() {
    data_.clear();
  }

  bool empty() const {
    return data_.empty();
  }

  size_t size() const {
    return data_.size();
  }

 private:
  static inline size_t get_root() { return 0; }
  static inline size_t get_left(size_t index) { return 2 * index + 1; }
  static inline size_t get_right(size_t index) { return 2 * index + 2; }

  void downheap(size_t index) {
    T* data = data_.data();
    T v = std::move(data[index]);
    size_t size = data_.size();
    for(;;) {
      const size_t left_child = get_left(index);
      if (left_child >= size) {
        break;
      }
      const size_t right_child = left_child + 1;
      DCHECK_EQ(right_child, get_right(index));
      T* picked_child = data + left_child;
      if (right_child < size) {
        T* right_ptr = data + right_child;
        if (cmp_(*picked_child, *right_ptr)) {
          picked_child = right_ptr;
        }
      }
      if (!cmp_(v, *picked_child)) {
        break;
      }
      data[index] = std::move(*picked_child);
      index = picked_child - data;
    }
    data[index] = std::move(v);
  }

  Compare cmp_;
  boost::container::small_vector<T, 8> data_;
};

}  // namespace rocksdb

#endif // YB_ROCKSDB_UTIL_HEAP_H
