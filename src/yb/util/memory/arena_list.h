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

#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include "yb/gutil/casts.h"

#include "yb/util/memory/arena.h"

namespace yb {

template <class Entry>
struct ArenaListNode : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<
    boost::intrusive::normal_link>> {
  Entry* value_ptr = nullptr;
};

template <class Entry>
struct ArenaListNodeWithValue : ArenaListNode<Entry> {
  template<class... Args>
  explicit ArenaListNodeWithValue(Args&&... args) : value(std::forward<Args>(args)...) {
    this->value_ptr = &value;
  }

  Entry value;
};

template <class Entry>
struct ExtractArenaListValue {
  using type = Entry&;

  template<class E>
  type operator()(const ArenaListNode<E>& node) const {
    return *node.value_ptr;
  }
};

template <class Entry>
class ArenaList {
 public:
  using List = boost::intrusive::list<ArenaListNode<Entry>>;
  using Extractor = ExtractArenaListValue<Entry>;
  using ConstExtractor = ExtractArenaListValue<const Entry>;
  using iterator = boost::transform_iterator<Extractor, typename List::iterator>;
  using const_iterator = boost::transform_iterator<ConstExtractor, typename List::const_iterator>;
  using reverse_iterator = boost::transform_iterator<Extractor, typename List::reverse_iterator>;
  using const_reverse_iterator = boost::transform_iterator<
      ConstExtractor, typename List::const_reverse_iterator>;
  using value_type = Entry;

  explicit ArenaList(ThreadSafeArena* arena) : arena_(arena) {}

  ArenaList(ThreadSafeArena* arena, const ArenaList<Entry>& rhs) : arena_(arena) {
    for (const auto& entry : rhs) {
      emplace_back(entry);
    }
  }

  void operator=(const ArenaList<Entry>& rhs) {
    clear();
    for (const auto& entry : rhs) {
      emplace_back(entry);
    }
  }

  template <class... Args>
  Entry& emplace_back(Args&&... args) {
    auto node = arena_->NewArenaObject<ArenaListNodeWithValue<Entry>>(std::forward<Args>(args)...);
    list_.push_back(*node);
    return *node->value_ptr;
  }

  Entry& push_back_ref(Entry* entry) {
    auto node = arena_->NewObject<ArenaListNode<Entry>>();
    node->value_ptr = entry;
    list_.push_back(*node);
    return *entry;
  }

  Entry& front() {
    return *list_.front().value_ptr;
  }

  const Entry& front() const {
    return *list_.front().value_ptr;
  }

  Entry& back() {
    return *list_.back().value_ptr;
  }

  const Entry& back() const {
    return *list_.back().value_ptr;
  }

  void clear() {
    list_.clear();
  }

  void pop_back() {
    list_.pop_back();
  }

  void pop_front() {
    list_.pop_front();
  }

  iterator erase(iterator it) {
    return iterator(list_.erase(it.base()));
  }

  iterator erase(iterator it, iterator stop) {
    return iterator(list_.erase(it.base(), stop.base()));
  }

  const_iterator erase(const_iterator it, const_iterator stop) {
    return const_iterator(list_.erase(it.base(), stop.base()));
  }

  bool empty() const {
    return list_.empty();
  }

  size_t size() const {
    return list_.size();
  }

  const_iterator cbegin() const {
    return const_iterator(list_.cbegin());
  }

  const_iterator cend() const {
    return const_iterator(list_.cend());
  }

  const_iterator begin() const {
    return const_iterator(list_.begin());
  }

  const_iterator end() const {
    return const_iterator(list_.end());
  }

  iterator begin() {
    return iterator(list_.begin());
  }

  iterator end() {
    return iterator(list_.end());
  }

  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(list_.crbegin());
  }

  const_reverse_iterator crrend() const {
    return const_reverse_iterator(list_.crend());
  }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(list_.rbegin());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(list_.rend());
  }

  reverse_iterator rbegin() {
    return reverse_iterator(list_.rbegin());
  }

  reverse_iterator rend() {
    return reverse_iterator(list_.rend());
  }

  template <class PB>
  void ToGoogleProtobuf(PB* out) const {
    out->Clear();
    out->Reserve(narrow_cast<int>(list_.size()));
    for (const auto& entry : *this) {
      entry.ToGoogleProtobuf(out->Add());
    }
  }

  template <class It>
  void assign(It begin, const It& end) {
    clear();
    for (; begin != end; ++begin) {
      emplace_back(*begin);
    }
  }

  template <class Collection>
  void assign(const Collection& collection) {
    assign(collection.begin(), collection.end());
  }

  void swap(ArenaList* rhs) {
    std::swap(arena_, rhs->arena_);
    list_.swap(rhs->list_);
  }

  ThreadSafeArena& arena() const {
    return *arena_;
  }

  // RepeatedPtrField compatibility
  void Clear() {
    clear();
  }

  Entry* Add() {
    return &emplace_back();
  }

  void RemoveLast() {
    pop_back();
  }

 private:
  ThreadSafeArena* arena_;
  List list_;
};

} // namespace yb
