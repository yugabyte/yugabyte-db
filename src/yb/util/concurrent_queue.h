// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/locks.h"

namespace yb {

// RWQueue implementation from [1998] Maged Michael, Michael Scott
// "Simple, fast, and practical non-blocking and blocking concurrent queue algorithms"
template <typename T>
class RWQueue {
 public:
  using value_type = T;

  RWQueue() {
    head_.node = tail_.node = new Node;
  }

  ~RWQueue() {
    clear();
    DCHECK_EQ(head_.node, tail_.node);
    delete head_.node;
  }

  template <class... Args>
  void Push(Args&&... value) {
    auto node = new Node(std::forward<Args>(value)...);
    std::lock_guard lock(tail_.mutex);
    tail_.node->next.store(node, std::memory_order_release);
    tail_.node = node;
  }

  template <class... Args>
  bool push(Args&&... value) {
    Push(std::forward<Args>(value)...);
    return true;
  }

  bool Pop(value_type& value) {
    return DoPop(value);
  }

  bool pop(value_type& value) {
    return Pop(value);
  }

  std::optional<value_type> Pop() {
    std::optional<value_type> result;
    DoPop(result);
    return result;
  }

  std::optional<value_type> pop() {
    return Pop();
  }

  void Clear() {
    Node* head;
    Node* tail;
    {
      std::lock_guard lock_head(head_.mutex);
      std::lock_guard lock_tail(tail_.mutex);
      head = head_.node;
      tail = tail_.node;
      head_.node = tail;
    }
    while (head != tail) {
      auto* next = head->next.load(std::memory_order_relaxed);
      if (!next) {
        break;
      }
      delete head;
      head = next;
    }
  }

  void clear() {
    Clear();
  }

  bool empty() const {
    std::lock_guard lock(head_.mutex);
    return head_.node->next.load(std::memory_order_relaxed) == nullptr;
  }

 private:
  template <class Out>
  bool DoPop(Out& value) {
    Node* node;
    {
      std::lock_guard lock(head_.mutex);
      node = head_.node;
      auto new_head = node->next.load(std::memory_order_acquire);
      if (!new_head) {
        return false;
      }
      value = std::move(new_head->value);
      head_.node = new_head;
    }
    delete node;
    return true;
  }

  struct Node {
    std::atomic<Node*> next{nullptr};
    value_type value;

    template <typename... Args>
    explicit Node(Args&&... args)
        : value(std::forward<Args>(args)...) {}
  };

  struct EndType {
    mutable TrivialSpinlock mutex;
    Node* node;
  };

  alignas(CACHELINE_SIZE) EndType head_;
  alignas(CACHELINE_SIZE) EndType tail_;
};

}  // namespace yb
