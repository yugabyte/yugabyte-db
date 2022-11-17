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
  explicit StronglyTypedString(const std::string& value) : value_(value) {}

  bool operator==(const StronglyTypedString<Tag>& other) const {
    return value_ == other.value_;
  }

  bool operator!=(const StronglyTypedString<Tag>& other) const {
    return !(*this == other);
  }

  const std::string& ToString() const { return value_; }

 private:
  std::string value_;
};

}  // namespace yb
