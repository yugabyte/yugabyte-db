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

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

namespace yb {

// The cache that stores at most specified number of recently added entries.
template <class Value, class KeyIndex = boost::multi_index::identity<Value>>
class LRUCache {
 private:
  class IdTag;

  using Impl = boost::multi_index_container<
      Value,
      boost::multi_index::indexed_by<
        boost::multi_index::sequenced<>,
        boost::multi_index::hashed_unique<
          boost::multi_index::tag<IdTag>,
          KeyIndex
        >
      >
  >;

 public:
  using const_iterator = typename Impl::const_iterator;
  using iterator = typename Impl::const_iterator;

  explicit LRUCache(size_t capacity) : capacity_(capacity) {}

  // Insert entry in cache.
  template<class V>
  iterator insert(V&& value) {
    return FinalizeInsertion(impl_.push_front(std::forward<V>(value)));
  }

  iterator Insert(const Value& value) {
    return insert(value);
  }

  iterator Insert(Value&& value) {
    return insert(std::move(value));
  }

  template<class... Args>
  iterator emplace(Args&&... args) {
    return FinalizeInsertion(impl_.emplace_front(std::forward<Args>(args)...));
  }

  // Erase entry from cache. Returns number of removed entries.
  template <class Key>
  size_t erase(const Key& key) {
    return impl_.template get<IdTag>().erase(key);
  }

  // Erase entry from cache. Returns number of removed entries.
  template <class Key>
  size_t Erase(const Key& key) {
    return erase(key);
  }

  // Erase by usage order iterator
  const_iterator erase(const_iterator pos) {
    return impl_.erase(pos);
  }

  // Erase by usage order iterators
  const_iterator erase(const_iterator first, const_iterator last) {
    return impl_.erase(first, last);
  }

  // Begin of usage order
  const_iterator begin() const {
    return impl_.begin();
  }

  // End of usage order
  const_iterator end() const {
    return impl_.end();
  }

 private:
  iterator FinalizeInsertion(const std::pair<iterator, bool>& insertion_result) {
    if (!insertion_result.second) {
      impl_.relocate(impl_.begin(), insertion_result.first);
    } else if (impl_.size() > capacity_) {
      impl_.pop_back();
    }
    return insertion_result.first;
  }

  const size_t capacity_;
  Impl impl_;
};

} // namespace yb
