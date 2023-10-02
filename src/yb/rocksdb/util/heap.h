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
// - This heap's pop() uses a "schoolbook" down_root which requires up to ~2logN
//   comparisons.
// - down_root has rich interface allowing to cache the best child if heap was not
//   modified. The external caching is used because it provided better performance in real
//   application testing.
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

// The container is optimized for small types, that could be freely copied.
// So only scalar types are allowed.
template<typename T, typename Compare = std::less<T>> requires (std::is_scalar_v<T>)
class BinaryHeap {
 public:
  BinaryHeap() { }
  explicit BinaryHeap(const Compare& cmp) : cmp_(cmp) { }
  explicit BinaryHeap(Compare&& cmp) : cmp_(std::move(cmp)) { }

  void push(const T& value) {
    data_.push_back(value);
    std::push_heap(data_.begin(), data_.end(), cmp_);
  }

  const T& top() const {
    DCHECK(!empty());
    return data_.front();
  }

  void replace_top(const T& value) {
    DCHECK(!empty());
    data_.front() = value;
    down_root();
  }

  void pop() {
    DCHECK(!empty());
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

  const T& second_top() const {
    LOG_IF(DFATAL, empty()) << "Heap is empty.";
    const size_t size = data_.size();
    LOG_IF(DFATAL, size < 2) << "Heap contains less than 2 elements - " << size;

    constexpr size_t left_child = 1;
    constexpr size_t right_child = 2;
    DCHECK_EQ(left_child, get_left(get_root()));
    DCHECK_EQ(right_child, get_right(get_root()));

    if (size > 2 && cmp_(data_.data()[1], data_.data()[2])) {
      return data_.data()[2];
    }

    return data_.data()[1];
  }

  // Returns the best root child index and pointer if root was not pushed down.
  // User could specify the best child index returned by the previous call to down_root if it is
  // better than current root. In this case root will be unconditionally pushed down to this child
  // subheap.
  std::pair<size_t, const T*> down_root(size_t best_child = 0) {
    size_t index = 0;
    T* data = data_.data();
    T v = data[index];
    size_t size = data_.size();

    if (best_child) {
      data[index] = data[best_child];
      index = best_child;
    }

    for(;;) {
      const size_t left_child = get_left(index);
      if (left_child >= size) {
        break;
      }
      const size_t right_child = left_child + 1;
      DCHECK_EQ(right_child, get_right(index));
      auto left_ptr = data + left_child;
      if (right_child < size) {
        T* right_ptr = data + right_child;
        if (cmp_(*left_ptr, *right_ptr)) {
          if (cmp_(v, *right_ptr)) {
            data[index] = *right_ptr;
            index = right_child;
            continue;
          }
          if (index == 0) {
            return {right_child, right_ptr};
          }
          break;
        }
      }
      if (!cmp_(v, *left_ptr)) {
        if (index == 0) {
          return {left_child, left_ptr};
        }
        break;
      }
      data[index] = *left_ptr;
      index = left_child;
    }
    data[index] = v;
    return {0, nullptr};
  }

 private:
  static inline size_t get_root() { return 0; }
  static inline size_t get_left(size_t index) { return 2 * index + 1; }
  static inline size_t get_right(size_t index) { return 2 * index + 2; }

  Compare cmp_;
  boost::container::small_vector<T, 8> data_;
};

}  // namespace rocksdb
