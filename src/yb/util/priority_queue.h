// Copyright (c) YugaByte, Inc.
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

namespace yb {

// Priority queue that supports pop with returning result.
// Useful for non copyable but moveable types.
// It is max priority queue like std::priority_queue.
template <class T,
          class Container = std::vector<T>,
          class Compare = std::less<typename Container::value_type>>
class PriorityQueue {
 public:
  typedef typename Container::const_iterator const_iterator;

  T Pop() {
    auto result = std::move(queue_.front());
    std::pop_heap(queue_.begin(), queue_.end(), compare_);
    queue_.pop_back();
    return result;
  }

  void Push(const T& task) {
    queue_.push_back(task);
    std::push_heap(queue_.begin(), queue_.end(), compare_);
  }

  void Push(T&& task) {
    queue_.push_back(std::move(task));
    std::push_heap(queue_.begin(), queue_.end(), compare_);
  }

  template <class Predicate>
  void RemoveIf(const Predicate& predicate) {
    auto end = queue_.end();
    auto it = queue_.begin();
    while (it < end) {
      if (predicate(&*it)) {
        --end;
        *it = std::move(*end);
      } else {
        ++it;
      }
    }
    if (end == queue_.end()) {
      return;
    }
    queue_.erase(end, queue_.end());
    std::make_heap(queue_.begin(), queue_.end(), compare_);
  }

  void Swap(Container* out) {
    out->swap(queue_);
    std::make_heap(queue_.begin(), queue_.end(), compare_);
  }

  bool empty() const {
    return queue_.empty();
  }

  size_t size() const {
    return queue_.size();
  }

  const T& top() const {
    DCHECK(!queue_.empty());
    return queue_.front();
  }

  const_iterator begin() const {
    return queue_.begin();
  }

  const_iterator end() const {
    return queue_.end();
  }

 private:
  Container queue_;
  Compare compare_;
};

} // namespace yb
