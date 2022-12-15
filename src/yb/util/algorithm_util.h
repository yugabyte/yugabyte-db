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

#include <algorithm>
#include <bitset>
#include <type_traits>

#include <boost/container/small_vector.hpp>

namespace yb {

enum class SortOrder : uint8_t {
  kAscending = 0,
  kDescending
};

template<typename Iterator, typename Functor>
void SortByKey(Iterator begin,
               Iterator end,
               const Functor& f,
               SortOrder sort_order = SortOrder::kAscending) {
  using Value = typename Iterator::value_type;
  const bool invert_order = sort_order == SortOrder::kDescending;
  std::sort(begin, end, [invert_order, &f](const Value& a, const Value& b){
    return (f(a) < f(b)) != invert_order;
  });
}

// Returns an iterator pointing to last element that is <= k.
// Returns map.end() if there are no such elements.
//
template <class Map, class Key>
typename Map::const_iterator GetLastLessOrEqual(const Map& map, const Key& k) {
  auto iter = map.upper_bound(k);
  // iter is the first element > k.
  if (iter == map.begin()) {
    // All elements are > k => there are no elements that are <= k.
    return map.end();
  } else {
    // Element previous to iter is the last element that <= k.
    iter--;
    return iter;
  }
}

template <class Col, class Extractor>
bool IsMonotonic(const Col& collection, const Extractor& extractor) {
  auto it = collection.begin();
  auto end = collection.end();
  if (it == end) {
    return true;
  }
  auto prev = extractor(*it);
  while (++it != end) {
    auto next = extractor(*it);
    if (next < prev) {
      return false;
    }
    prev = next;
  }
  return true;
}

// Returns small vector of key and index pairs, sorted by extracted key.
template <class Col, class Extractor>
auto StableSorted(const Col& collection, const Extractor& extractor) {
  struct Entry {
    decltype(extractor(*collection.begin())) key;
    decltype(collection.size()) original_index;
    const std::remove_reference_t<decltype(*collection.begin())>* pointer;
  };

  boost::container::small_vector<Entry, 0x10> order;
  order.reserve(collection.size());
  decltype(collection.size()) index = 0;
  for (const auto& value : collection) {
    order.push_back(Entry {
      .key = extractor(value),
      .original_index = index++,
      .pointer = &value,
    });
  }
  std::sort(order.begin(), order.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.key < rhs.key || (lhs.key == rhs.key && lhs.original_index < rhs.original_index);
  });
  return order;
}

};  // namespace yb
