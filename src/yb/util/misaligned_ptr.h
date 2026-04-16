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

#include <iterator>
#include <type_traits>

#include "yb/gutil/endian.h"

namespace yb {

// Utility class to operate on data that could be stored with wrong alignment.
template <class T>
class MisalignedPtr {
 public:
  using RawPtr = std::conditional_t<std::is_const<T>::value, const std::byte, std::byte>*;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using reference = T;
  using iterator_category = std::random_access_iterator_tag;

  explicit MisalignedPtr(RawPtr ptr) : ptr_(ptr) {}

  RawPtr raw() const {
    return ptr_;
  }

  MisalignedPtr& operator++() {
    ptr_ += sizeof(value_type);
    return *this;
  }

  reference operator*() const {
    return Load<std::remove_const_t<T>, LittleEndian>(ptr_);
  }

  template <class Field>
  RawPtr FieldRawPtr() {
    static_assert(std::is_standard_layout_v<Field>);
    return ptr_ + offsetof(value_type, Field);
  }

 private:
  RawPtr ptr_;
};

template <class T>
bool operator!=(const MisalignedPtr<T>& lhs, const MisalignedPtr<T>& rhs) {
  return lhs.raw() != rhs.raw();
}

template <class T>
MisalignedPtr<T> operator+(MisalignedPtr<T> lhs, ptrdiff_t rhs) {
  return MisalignedPtr<T>(lhs.raw() + rhs * sizeof(T));
}

template <class T>
MisalignedPtr<T> operator-(MisalignedPtr<T> lhs, ptrdiff_t rhs) {
  return MisalignedPtr<T>(lhs.raw() - rhs * sizeof(T));
}

template <class T>
ptrdiff_t operator-(MisalignedPtr<T> lhs, MisalignedPtr<T> rhs) {
  return (lhs.raw() - rhs.raw()) / sizeof(T);
}

} // namespace yb
