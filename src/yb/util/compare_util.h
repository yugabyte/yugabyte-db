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

#include <unordered_map>
#include <vector>

#include <boost/preprocessor/config/config.hpp>
#include <boost/unordered_map.hpp>

namespace yb {
namespace util {

template<typename T>
int CompareUsingLessThan(const T& a, const T& b) {
  if (a < b) return -1;
  if (b < a) return 1;
  return 0;
}

// If the vectors are not equal size, we assume the smaller vector is padded with the last element
// An empty vector will always compare less than a bigger vector.
template<typename T>
int CompareVectors(const std::vector<T>& a, const std::vector<T>& b) {
  auto a_iter = a.begin();
  auto b_iter = b.begin();
  while (a_iter != a.end() && b_iter != b.end()) {
    int result = a_iter->CompareTo(*b_iter);
    if (result != 0) {
      return result;
    }
    ++a_iter;
    ++b_iter;
  }
  if (a_iter == a.end()) {
    return b_iter == b.end() ? 0 : -1;
  }
  return 1;
}

// http://stackoverflow.com/questions/1903954/is-there-a-standard-sign-function-signum-sgn-in-c-c
template<typename T>
inline int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

// Whether dereferencables (pointers, optionals, etc.) are equal by value
// using the given equality function.
template<class T, class EqualityFn>
[[nodiscard]]
bool DereferencedEqual(const T& t1, const T& t2, EqualityFn equality_fn) {
  return !t1 ? !t2 : t2 && equality_fn(*t1, *t2);
}

template<class... Args>
struct is_unordered_map : std::false_type {};

template<class... Args>
struct is_unordered_map<std::unordered_map<Args...>> : std::true_type {};

template<class... Args>
struct is_unordered_map<boost::unordered_map<Args...>> : std::true_type {};

template<class Map> concept UnorderedMap = is_unordered_map<Map>::value;

// Whether unordered maps contains elements that are equal by value, order of elements is ignored.
template<UnorderedMap UM, class EqualityFn>
[[nodiscard]]
bool MapsEqual(const UM& map1, const UM& map2, EqualityFn equality_fn) {
  if (map1.size() != map2.size()) {
    return false;
  }
  for (const auto& e1 : map1) {
    const auto e2iter = map2.find(e1.first);
    if (e2iter == map2.end() || !equality_fn(e1.second, e2iter->second)) {
      return false;
    }
  }
  return true;
}

#if BOOST_PP_VARIADICS

#define YB_FIELD_EQUALS(r, data, elem) \
    && lhs.BOOST_PP_CAT(elem, BOOST_PP_APPLY(data)) == rhs.BOOST_PP_CAT(elem, BOOST_PP_APPLY(data))
#define YB_FIELDS_EQUALS(data, ...) \
    BOOST_PP_SEQ_FOR_EACH(YB_FIELD_EQUALS, data(), BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define YB_STRUCT_EQUALS(...) \
    true YB_FIELDS_EQUALS(BOOST_PP_NIL, __VA_ARGS__)

#define YB_CLASS_EQUALS(...) \
    true YB_FIELDS_TO_STRING((BOOST_PP_IDENTITY(_)), __VA_ARGS__)

#else
#error "Compiler not supported -- BOOST_PP_VARIADICS is not set. See https://bit.ly/2ZF7rTu."
#endif

}  // namespace util
}  // namespace yb
