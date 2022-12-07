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

#include <vector>

#include <boost/preprocessor/cat.hpp>

// A "strongly-typed boolean" tool. This is needed to prevent passing the wrong boolean as a
// function parameter, and to make callsites more readable by enforcing that MyBooleanType::kTrue or
// MyBooleanType::kFalse is specified instead of kTrue, kFalse. Conversion from strongly-typed bools
// to regular bools is automatic, but the reverse conversion is always explicit.
#define YB_STRONGLY_TYPED_BOOL(TypeName) \
  class BOOST_PP_CAT(TypeName, _Tag); \
  typedef ::yb::StronglyTypedBool<BOOST_PP_CAT(TypeName, _Tag)> TypeName;

namespace yb {

template <class Tag>
struct StronglyTypedBoolProxy {
  bool value;

  operator bool() const { return value; }
  StronglyTypedBoolProxy<Tag> operator!() const { return StronglyTypedBoolProxy<Tag>{!value}; }
};

template <class Tag>
class StronglyTypedBool {
 public:
  static constexpr StronglyTypedBoolProxy<Tag> kTrue{true};
  static constexpr StronglyTypedBoolProxy<Tag> kFalse{false};
  static constexpr std::initializer_list<StronglyTypedBool<Tag>> kValues = {
    StronglyTypedBool<Tag>(false), StronglyTypedBool<Tag>(true)
  };

  StronglyTypedBool(StronglyTypedBoolProxy<Tag> proxy) : value_(proxy.value) {} // NOLINT

  // This is public so that we can construct a strongly-typed boolean value out of a regular one if
  // needed. In that case we'll have to spell out the class name, which will enforce readability.
  explicit constexpr StronglyTypedBool(bool value) : value_(value) {}

  // These operators return regular bools so that it is easy to use strongly-typed bools in logical
  // expressions.
  operator bool() const { return value_; }
  StronglyTypedBool<Tag> operator!() const { return StronglyTypedBool<Tag>(!value_); }
  bool get() const { return value_; }

 private:
  bool value_;
};

}  // namespace yb
