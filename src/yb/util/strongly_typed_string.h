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

#include <string>

#include <boost/preprocessor/cat.hpp>
#include "yb/util/slice.h"

// A "strongly-typed string" tool. This is needed to prevent passing the wrong string as a
// function parameter. Conversion from strongly-typed strings to std::string is automatic, but the
// reverse conversion is always explicit.
#define YB_STRONGLY_TYPED_STRING(TypeName) \
  class BOOST_PP_CAT(TypeName, _Tag); \
  typedef ::yb::StronglyTypedString<BOOST_PP_CAT(TypeName, _Tag)> TypeName;

namespace yb {

template <class Tag>
class StronglyTypedString {
 public:
  // This is public so that we can construct a strongly-typed string value out of a regular one if
  // needed. In that case we'll have to spell out the class name, which will enforce readability.
  StronglyTypedString() {}
  explicit StronglyTypedString(const std::string& value) : value_(value) {}
  explicit StronglyTypedString(const char* const value) : value_(value) {}
  template <class InputIterator>
  StronglyTypedString(InputIterator first, InputIterator last) : value_(first, last) {}

  StronglyTypedString(const StronglyTypedString<Tag>& rhs) : value_(rhs.value_) {}
  StronglyTypedString(const StronglyTypedString<Tag>&& rhs) : value_(std::move(rhs.value_)) {}

  StronglyTypedString<Tag>& operator=(const StronglyTypedString<Tag>& rhs) {
    value_ = rhs.value_;
    return *this;
  }
  StronglyTypedString<Tag>& operator=(const StronglyTypedString<Tag>&& rhs) {
    value_ = std::move(rhs.value_);
    return *this;
  }

  bool operator==(const StronglyTypedString<Tag>& other) const { return value_ == other.value_; }
  bool operator!=(const StronglyTypedString<Tag>& other) const { return !(*this == other); }

  bool empty() const { return value_.empty(); }

  const std::string& ToString() const { return value_; }

  operator Slice() const { return value_; }

  size_t size() const { return value_.size(); }
  const char* data() const { return value_.data(); }

 private:
  std::string value_;
};

template <class Tag>
std::ostream& operator<<(std::ostream& out, const StronglyTypedString<Tag>& strong_string) {
  return out << strong_string.ToString();
}

}  // namespace yb

namespace std {
template <class Tag>
struct hash<yb::StronglyTypedString<Tag>> {
  size_t operator()(const yb::StronglyTypedString<Tag>& strong_string) const {
    return std::hash<std::string>{}(strong_string.ToString());
  }
};
}  // namespace std
