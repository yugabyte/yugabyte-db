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

#ifndef YB_UTIL_STRONGLY_TYPED_BOOL_H
#define YB_UTIL_STRONGLY_TYPED_BOOL_H

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
class StronglyTypedBool {
 public:
  static const StronglyTypedBool<Tag> kTrue;
  static const StronglyTypedBool<Tag> kFalse;
  static const std::initializer_list<StronglyTypedBool<Tag>> kValues;

  // This is public so that we can construct a strongly-typed boolean value out of a regular one if
  // needed. In that case we'll have to spell out the class name, which will enforce readability.
  explicit StronglyTypedBool(bool value) : value_(value) {}

  // These operators return regular bools so that it is easy to use strongly-typed bools in logical
  // expressions.
  operator bool() const { return value_; }
  StronglyTypedBool<Tag> operator!() const { return StronglyTypedBool<Tag>(!value_); }
  bool get() const { return value_; }

 private:
  bool value_;
};

template <class Tag>
const StronglyTypedBool<Tag> StronglyTypedBool<Tag>::kTrue(true);

template <class Tag>
const StronglyTypedBool<Tag> StronglyTypedBool<Tag>::kFalse(false);

template <class Tag>
const std::initializer_list<StronglyTypedBool<Tag>> StronglyTypedBool<Tag>::kValues {
  StronglyTypedBool<Tag>::kFalse,
  StronglyTypedBool<Tag>::kTrue
};

}  // namespace yb

#endif // YB_UTIL_STRONGLY_TYPED_BOOL_H
