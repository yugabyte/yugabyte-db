//
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
//

#pragma once

#include <memory>

namespace yb {
namespace util {

// This utility uses C++11 features: variadic template & static assert.
template <typename ... Types>
struct TypeIndexer;

template <>
struct TypeIndexer<> {
 protected:
  static constexpr int num_types() { return 0; }

  template <typename Type>
  static constexpr int index_of_type(int) { return -1; }

  template <class Tuple, const int = 0>
  static void reset_tail(Tuple*) {}
};

template <typename T, typename ... Types>
struct TypeIndexer<T, Types ...> : private TypeIndexer<Types ...> {
  static constexpr int num_types() { return super::num_types() + 1; }

  template <typename Type>
  static constexpr int index_of() { return index_of_type<Type>(0); }

 protected:
  typedef TypeIndexer<Types ...> super;

  template <typename Type>
  static constexpr int index_of_type(int i) {
    return std::is_same<T, Type>::value ? i : super::template index_of_type<Type>(i + 1);
  }

  template <class Tuple, const int i = 0>
  static void reset_tail(Tuple* tuple) {
    std::get<i>(*tuple).reset();
    super::template reset_tail<Tuple, i + 1>(tuple);
  }
};

// The class extends std::tuple<> functionality by ability
// to get the tuple element by type (instead of index).
//
// class A {}; class B {}; class C {};
//
// std::tuple<A, B, C> t; // Usual std::tuple.
// B& b = std::get<1>(t); // Constructed by default, returned by index.
//
// SharedPtrTuple<A, B, C> t;
// std::shared_ptr<B>& b = t.get<B>(); // The shared_ptr is empty. Returned by the type.
// b.reset(new B);
//
// t.reset();  // Reset all internal shared_ptr-s.
//
template<class ... Types>
struct SharedPtrTuple : public TypeIndexer<Types ...> {
  template <class Type>
  std::shared_ptr<Type>& get() {
    return std::get<checked_index_of<Type>()>(storage_);
  }

  template <class Type>
  const std::shared_ptr<Type>& get() const {
    return std::get<checked_index_of<Type>()>(storage_);
  }

  template <class Type>
  bool has_type() const { return super::template index_of<Type>() >= 0; }

  void reset() { super::template reset_tail(&storage_); }

 protected:
  typedef TypeIndexer<Types ...> super;

  template <class Type>
  static constexpr int checked_index_of() {
    constexpr int index = super::template index_of<Type>();
    static_assert(index >= 0, "Unknown type 'Type'");
    static_assert(index < super::num_types(), "Internal error: invalid index");
    return index;
  }

  std::tuple<std::shared_ptr<Types> ...> storage_;
};

} // namespace util
} // namespace yb
