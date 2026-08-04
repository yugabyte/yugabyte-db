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

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include "yb/util/flags.h"
#include "yb/util/status.h"

// Helper macros for common flag validation functions.
//
// When validation depends on other flags, a delayed validator should be used:
//
//     // Independent of other flags, not delayed.
//     DEFINE_validator(my_flag, FLAG_COND_VALIDATOR(!_value.empty(), "Must not be empty"));
//
//     // Dependent on the value of other_flag, delayed.
//     DEFINE_validator(my_flag,
//         FLAG_DELAYED_COND_VALIDATOR(_value.starts_with(FLAGS_other_flag),
//                                     Format("Must start with other_flag: $0", FLAGS_other_flag)));
//     // Inverse direction.
//     DEFINE_validator(other_flag,
//         FLAG_DELAYED_COND_VALIDATOR(FLAGS_my_flag.starts_with(_value),
//                                     Format("Must be a prefix of my_flag: $0", FLAGS_my_flag)));
//
// For simple comparisons with other flags, FLAG_<cmp>_FLAG_VALIDATOR can be used:
//
//     // Validate that my_flag < other_flag.
//     DEFINE_validator(my_flag, FLAG_LT_FLAG_VALIDATOR(other_flag));
//     // Inverse direction, checking other_flag > my_flag.
//     DEFINE_validator(other_flag, FLAG_GT_FLAG_VALIDATOR(my_flag));
//
// As in the above examples, if you have a validator for my_flag depending on the value of
// other_flag, you must also add the inverse validator to other_flag checking the value of my_flag.
//
// FLAG_COND_VALIDATOR(cond, message)
// FLAG_DELAYED_COND_VALIDATOR(cond, message)
// - Check that `cond` is true, and fails validation with `message` if not. The value of
//   the flag is available in `_value`.
//
// FLAG_OK_VALIDATOR(expr)
// FLAG_DELAYED_OK_VALIDATOR(expr)
// - Check that `expr.ok()` is true, and fails validation with `message` if not. The value of
//   the flag is available in `_value`.
//
// FLAG_EQ_FLAG_VALIDATOR(other_flag)
// FLAG_NE_FLAG_VALIDATOR(other_flag)
// FLAG_LT_FLAG_VALIDATOR(other_flag)
// FLAG_LE_FLAG_VALIDATOR(other_flag)
// FLAG_GT_FLAG_VALIDATOR(other_flag)
// FLAG_GE_FLAG_VALIDATOR(other_flag)
// - Perform a comparison of the flag value with the value of a different flag `other_flag`.
//
// FLAG_EQ_VALUE_VALIDATOR(cmp_value)
// FLAG_NE_VALUE_VALIDATOR(cmp_value)
// FLAG_LT_VALUE_VALIDATOR(cmp_value)
// FLAG_LE_VALUE_VALIDATOR(cmp_value)
// FLAG_GT_VALUE_VALIDATOR(cmp_value)
// FLAG_GE_VALUE_VALIDATOR(cmp_value)
// - Perform a comparison of the flag value with a different value `cmp_value`. This value must
//   not depend on the value of another flag.
//
// FLAG_IN_SET_VALIDATOR(value1, value2, ...)
// - Check that the flag value is one of `value1`, `value2`, ...
//
// FLAG_RANGE_VALIDATOR(min_value_inclusive, max_value_inclusive)
// - Check that `min_value_inclusive` <= flag value <=  `max_value_inclusive`.
//
// Examples:
//
// DEFINE_validator(my_flag, FLAG_LT_FLAG_VALIDATOR(other_flag));
// - Validates that my_flag < other_flag.
//
// DEFINE_validator(my_flag,
//     FLAG_GT_FLAG_VALIDATOR(other_flag),
//     FLAG_LT_VALUE_VALIDATOR(512_MB));
// - Validates that other_flag < my_flag < 512MB.
//
// DEFINE_validator(my_flag, FLAG_IN_SET_VALIDATOR("foo", "bar"));
// - Validates my_flag is either "foo", or "bar".
//
namespace yb::flags_internal {

#define FLAG_COND_VALIDATOR_HELPER(_cond, _message, _delayed) \
    [](const char* _flag_name, auto _value) -> bool { \
      if constexpr (_delayed) { DELAY_FLAG_VALIDATION_ON_STARTUP(_flag_name); } \
      if (_cond) { \
        return true; \
      } \
      LOG_FLAG_VALIDATION_ERROR(_flag_name, _value) << _message; \
      return false; \
    }

#define FLAG_COND_VALIDATOR(cond, message) FLAG_COND_VALIDATOR_HELPER(cond, message, false)
#define FLAG_DELAYED_COND_VALIDATOR(cond, message) \
    FLAG_COND_VALIDATOR_HELPER(cond, message, true)

#define FLAG_OK_VALIDATOR_HELPER(_expr, _delayed) \
    [](const char* _flag_name, auto _value) -> bool { \
      if constexpr (_delayed) { DELAY_FLAG_VALIDATION_ON_STARTUP(_flag_name); } \
      auto _res = (_expr); \
      if (_res.ok()) { \
        return true; \
      } \
      LOG_FLAG_VALIDATION_ERROR(_flag_name, _value) << yb::MoveStatus(_res); \
      return false; \
    }

#define FLAG_OK_VALIDATOR(expr) FLAG_OK_VALIDATOR_HELPER(expr, false)
#define FLAG_DELAYED_OK_VALIDATOR(expr) FLAG_OK_VALIDATOR_HELPER(expr, true)

#define FLAG_CMP_FLAG_VALIDATOR_HELPER(other_flag, cmp, cmp_desc) \
    FLAG_DELAYED_COND_VALIDATOR( \
      cmp(_value, BOOST_PP_CAT(FLAGS_, other_flag)), \
      "Must be " cmp_desc " " #other_flag ": " << BOOST_PP_CAT(FLAGS_, other_flag))

#define FLAG_EQ_FLAG_VALIDATOR(other_flag) \
    FLAG_CMP_FLAG_VALIDATOR_HELPER(other_flag, std::equal_to{}, "equal to")
#define FLAG_NE_FLAG_VALIDATOR(other_flag) \
    FLAG_CMP_FLAG_VALIDATOR_HELPER(other_flag, std::not_equal_to{}, "not equal to")

bool compare_less(std::integral auto x, std::integral auto y) {
  return std::cmp_less(x, y);
}
bool compare_less(std::floating_point auto x, std::floating_point auto y) {
  return x < y;
}

bool compare_less_equal(std::integral auto x, std::integral auto y) {
  return std::cmp_less_equal(x, y);
}
bool compare_less_equal(std::floating_point auto x, std::floating_point auto y) {
  return x <= y;
}

bool compare_greater(std::integral auto x, std::integral auto y) {
  return std::cmp_greater(x, y);
}
bool compare_greater(std::floating_point auto x, std::floating_point auto y) {
  return x > y;
}

bool compare_greater_equal(std::integral auto x, std::integral auto y) {
  return std::cmp_greater_equal(x, y);
}
bool compare_greater_equal(std::floating_point auto x, std::floating_point auto y) {
  return x >= y;
}

#define FLAG_LT_FLAG_VALIDATOR(other_flag) \
    FLAG_CMP_FLAG_VALIDATOR_HELPER(other_flag, ::yb::flags_internal::compare_less, "less than")
#define FLAG_LE_FLAG_VALIDATOR(other_flag) \
    FLAG_CMP_FLAG_VALIDATOR_HELPER( \
        other_flag, ::yb::flags_internal::compare_less_equal, "less than or equal to")
#define FLAG_GT_FLAG_VALIDATOR(other_flag) \
    FLAG_CMP_FLAG_VALIDATOR_HELPER( \
        other_flag, ::yb::flags_internal::compare_greater, "greater than")
#define FLAG_GE_FLAG_VALIDATOR(other_flag) \
    FLAG_CMP_FLAG_VALIDATOR_HELPER( \
        other_flag, ::yb::flags_internal::compare_greater_equal, "greater than or equal to")

#define FLAG_REQUIRES_FLAG_VALIDATOR(required_flag) \
    FLAG_DELAYED_COND_VALIDATOR( \
      !_value || BOOST_PP_CAT(FLAGS_, required_flag), \
      "Requires " #required_flag " to be true")

#define FLAG_REQUIRES_NONZERO_FLAG_VALIDATOR(required_flag) \
    FLAG_DELAYED_COND_VALIDATOR( \
      !_value || BOOST_PP_CAT(FLAGS_, required_flag), \
      "Requires " #required_flag " to be non-zero")

#define FLAG_REQUIRED_BY_FLAG_VALIDATOR(required_by_flag) \
    FLAG_DELAYED_COND_VALIDATOR( \
      _value || !BOOST_PP_CAT(FLAGS_, required_by_flag), \
      "Required by " #required_by_flag " to be true")

#define FLAG_REQUIRED_NONZERO_BY_FLAG_VALIDATOR(required_by_flag) \
    FLAG_DELAYED_COND_VALIDATOR( \
      _value || !BOOST_PP_CAT(FLAGS_, required_by_flag), \
      "Required to be non-zero when " #required_by_flag " is true")

#define FLAG_CMP_VALUE_VALIDATOR_HELPER(cmp_value, cmp, cmp_desc) \
    FLAG_COND_VALIDATOR(cmp(_value, (cmp_value)), "Must be " cmp_desc " " #cmp_value)

#define FLAG_EQ_VALUE_VALIDATOR(cmp_value) \
    FLAG_CMP_VALUE_VALIDATOR_HELPER(cmp_value, std::equal_to{}, "equal to")
#define FLAG_NE_VALUE_VALIDATOR(cmp_value) \
    FLAG_CMP_VALUE_VALIDATOR_HELPER(cmp_value, std::not_equal_to{}, "not equal to")
#define FLAG_LT_VALUE_VALIDATOR(cmp_value) \
    FLAG_CMP_VALUE_VALIDATOR_HELPER(cmp_value, ::yb::flags_internal::compare_less, "less than")
#define FLAG_LE_VALUE_VALIDATOR(cmp_value) \
    FLAG_CMP_VALUE_VALIDATOR_HELPER( \
        cmp_value, ::yb::flags_internal::compare_less_equal, "less than or equal to")
#define FLAG_GT_VALUE_VALIDATOR(cmp_value) \
    FLAG_CMP_VALUE_VALIDATOR_HELPER( \
        cmp_value, ::yb::flags_internal::compare_greater, "greater than")
#define FLAG_GE_VALUE_VALIDATOR(cmp_value) \
    FLAG_CMP_VALUE_VALIDATOR_HELPER( \
        cmp_value, ::yb::flags_internal::compare_greater_equal, "greater than or equal to")

#define VALIDATOR_OR_EQ_HELPER(r, value, v) || ((value) == (v))
#define VALIDATOR_COMMA_HELPER(r, unused, i, v) BOOST_PP_IF(i, ", ", ) BOOST_PP_STRINGIZE(v)

#define FLAG_IN_SET_VALIDATOR(...) \
    FLAG_COND_VALIDATOR( \
        false BOOST_PP_SEQ_FOR_EACH(VALIDATOR_OR_EQ_HELPER, _value,  \
                                    BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)), \
        "Must be one of: " BOOST_PP_SEQ_FOR_EACH_I(VALIDATOR_COMMA_HELPER, _, \
                                                   BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))

#define FLAG_RANGE_VALIDATOR(min_value_inclusive, max_value_inclusive) \
    FLAG_COND_VALIDATOR( \
        ::yb::flags_internal::compare_less_equal((min_value_inclusive), _value) && \
            ::yb::flags_internal::compare_less_equal(_value, (max_value_inclusive)), \
        "Must be in the range [" #min_value_inclusive "-" #max_value_inclusive "]")

} // namespace yb::flags_internal
