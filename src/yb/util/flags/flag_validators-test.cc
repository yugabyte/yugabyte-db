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

#include <cstddef>
#include <filesystem>

#include <boost/regex.hpp>

#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using std::string;

DEFINE_RUNTIME_int32(flag_validators_test_even, 0, "Even numbers only");
DEFINE_validator(flag_validators_test_even,
    FLAG_COND_VALIDATOR(_value % 2 == 0, "Must be even"));

DEFINE_RUNTIME_int32(flag_validators_test_plus_one, 1, "flag_validators_test_even + 1");
DEFINE_validator(flag_validators_test_plus_one,
    FLAG_DELAYED_COND_VALIDATOR(
       _value == FLAGS_flag_validators_test_even + 1,
       yb::Format("Must be 1 greater than flag_validators_test_even: $0",
                  FLAGS_flag_validators_test_even)));

DEFINE_RUNTIME_uint64(flag_base_defaults_to_zero, 0, "Base flag that defaults to 0.");
DEFINE_RUNTIME_bool(flag_dependent_on_base_flag, false,
    "When true, requires base flag to be non-zero.");
DEFINE_validator(flag_dependent_on_base_flag,
    FLAG_REQUIRES_NONZERO_FLAG_VALIDATOR(flag_base_defaults_to_zero));
DEFINE_validator(flag_base_defaults_to_zero,
    FLAG_REQUIRED_NONZERO_BY_FLAG_VALIDATOR(flag_dependent_on_base_flag));
namespace yb {
namespace {

Status CheckOdd(int n) {
  SCHECK_FORMAT(n % 2 != 0, InvalidArgument, "$0 is not odd", n);
  return Status::OK();
}

// Test that things work with Result as well.
Result<std::nullptr_t> CheckMinusOne(int n, const char* base_name, int base) {
  SCHECK_FORMAT(n == base - 1, InvalidArgument, "$0 is not $1 - 1 ($1 = $2)", n, base_name, base);
  return nullptr;
}

} // namespace
} // namespace yb

DEFINE_RUNTIME_int32(flag_validators_test_odd, 1, "Odd numbers only");
DEFINE_validator(flag_validators_test_odd, FLAG_OK_VALIDATOR(yb::CheckOdd(_value)));

DEFINE_RUNTIME_int32(flag_validators_test_minus_one, 0, "flag_validators_test_odd - 1");
DEFINE_validator(flag_validators_test_minus_one,
    FLAG_OK_VALIDATOR(yb::CheckMinusOne(
        _value, "flag_validators_test_odd", FLAGS_flag_validators_test_odd)));

DEFINE_RUNTIME_int32(flag_validators_test_lt_lhs, 0, "Flag for LHS of < check");
DEFINE_RUNTIME_int32(flag_validators_test_lt_rhs, 9, "Flag for RHS of < check");
DEFINE_RUNTIME_int32(flag_validators_test_le_lhs, 0, "Flag for LHS of <= check");
DEFINE_RUNTIME_int32(flag_validators_test_le_rhs, 9, "Flag for RHS of <= check");
DEFINE_RUNTIME_int32(flag_validators_test_gt_lhs, 9, "Flag for LHS of > check");
DEFINE_RUNTIME_int32(flag_validators_test_gt_rhs, 0, "Flag for RHS of > check");
DEFINE_RUNTIME_int32(flag_validators_test_ge_lhs, 9, "Flag for LHS of >= check");
DEFINE_RUNTIME_int32(flag_validators_test_ge_rhs, 0, "Flag for RHS of >= check");

DEFINE_RUNTIME_double(flag_validators_test_lt_lhs_double, 0.5, "Flag for LHS of < check (double)");
DEFINE_RUNTIME_double(flag_validators_test_lt_rhs_double, 9.5, "Flag for RHS of < check (double)");
DEFINE_RUNTIME_double(flag_validators_test_le_lhs_double, 0.5, "Flag for LHS of <= check (double)");
DEFINE_RUNTIME_double(flag_validators_test_le_rhs_double, 9.5, "Flag for RHS of <= check (double)");
DEFINE_RUNTIME_double(flag_validators_test_gt_lhs_double, 9.5, "Flag for LHS of > check (double)");
DEFINE_RUNTIME_double(flag_validators_test_gt_rhs_double, 0.5, "Flag for RHS of > check (double)");
DEFINE_RUNTIME_double(flag_validators_test_ge_lhs_double, 9.5, "Flag for LHS of >= check (double)");
DEFINE_RUNTIME_double(flag_validators_test_ge_rhs_double, 0.5, "Flag for RHS of >= check (double)");

// Normally RHS flag validators are also needed, but we skip them here because we're interested
// in checking the output of specific validators.
DEFINE_validator(flag_validators_test_lt_lhs,
    FLAG_LT_FLAG_VALIDATOR(flag_validators_test_lt_rhs),
    FLAG_LT_VALUE_VALIDATOR(5));
DEFINE_validator(flag_validators_test_le_lhs,
    FLAG_LE_FLAG_VALIDATOR(flag_validators_test_le_rhs),
    FLAG_LE_VALUE_VALIDATOR(5));
DEFINE_validator(flag_validators_test_gt_lhs,
    FLAG_GT_FLAG_VALIDATOR(flag_validators_test_gt_rhs),
    FLAG_GT_VALUE_VALIDATOR(5));
DEFINE_validator(flag_validators_test_ge_lhs,
    FLAG_GE_FLAG_VALIDATOR(flag_validators_test_ge_rhs),
    FLAG_GE_VALUE_VALIDATOR(5));

DEFINE_validator(flag_validators_test_lt_lhs_double,
    FLAG_LT_FLAG_VALIDATOR(flag_validators_test_lt_rhs_double),
    FLAG_LT_VALUE_VALIDATOR(5.0));
DEFINE_validator(flag_validators_test_le_lhs_double,
    FLAG_LE_FLAG_VALIDATOR(flag_validators_test_le_rhs_double),
    FLAG_LE_VALUE_VALIDATOR(5.0));
DEFINE_validator(flag_validators_test_gt_lhs_double,
    FLAG_GT_FLAG_VALIDATOR(flag_validators_test_gt_rhs_double),
    FLAG_GT_VALUE_VALIDATOR(5.0));
DEFINE_validator(flag_validators_test_ge_lhs_double,
    FLAG_GE_FLAG_VALIDATOR(flag_validators_test_ge_rhs_double),
    FLAG_GE_VALUE_VALIDATOR(5.0));

DEFINE_RUNTIME_string(flag_validators_test_eq_lhs, "foo", "Flag for LHS of == check");
DEFINE_RUNTIME_string(flag_validators_test_eq_rhs, "foo", "Flag for RHS of == check");
DEFINE_RUNTIME_string(flag_validators_test_eq_value, "foo", "Flag for value == check");
DEFINE_RUNTIME_string(flag_validators_test_ne_lhs, "foo", "Flag for LHS of != check");
DEFINE_RUNTIME_string(flag_validators_test_ne_rhs, "bar", "Flag for RHS of != check");
DEFINE_RUNTIME_string(flag_validators_test_ne_value, "foo", "Flag for value != check");

DEFINE_validator(flag_validators_test_eq_lhs, FLAG_EQ_FLAG_VALIDATOR(flag_validators_test_eq_rhs));
DEFINE_validator(flag_validators_test_eq_value, FLAG_EQ_VALUE_VALIDATOR("foo"));
DEFINE_validator(flag_validators_test_ne_lhs, FLAG_NE_FLAG_VALIDATOR(flag_validators_test_ne_rhs));
DEFINE_validator(flag_validators_test_ne_value, FLAG_NE_VALUE_VALIDATOR("bar"));

DEFINE_RUNTIME_string(flag_validators_test_set, "foo", "One of 'foo' or 'bar'");
DEFINE_validator(flag_validators_test_set, FLAG_IN_SET_VALIDATOR("foo", "bar"));

DEFINE_RUNTIME_int32(flag_validators_test_range, 0, "Between 0 and 9");
DEFINE_validator(flag_validators_test_range, FLAG_RANGE_VALIDATOR(0, 9));

DEFINE_RUNTIME_bool(flag_validators_test_required_by_1, false, "Flag for require check");
DEFINE_RUNTIME_bool(flag_validators_test_requires_1, false, "Flag for require check");
DEFINE_validator(flag_validators_test_requires_1,
    FLAG_REQUIRES_FLAG_VALIDATOR(flag_validators_test_required_by_1));

DEFINE_RUNTIME_bool(flag_validators_test_required_by_2, true, "Flag for require check");
DEFINE_RUNTIME_bool(flag_validators_test_requires_2, true, "Flag for require check");
DEFINE_validator(flag_validators_test_required_by_2,
    FLAG_REQUIRED_BY_FLAG_VALIDATOR(flag_validators_test_requires_2));

namespace yb {

class FlagValidatorsTest : public YBTest {
 protected:
  void TestSetFlag(
      const std::string& flag_name, const std::string& new_value, const std::string& error = "") {
    std::string old_value;
    std::string output_msg;

    auto result = SetFlag(
        flag_name, new_value, flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
    if (error.empty()) {
      ASSERT_EQ(result, flags_internal::SetFlagResult::SUCCESS);
    } else {
      ASSERT_NE(result, flags_internal::SetFlagResult::SUCCESS);
      ASSERT_TRUE(boost::regex_search(output_msg, boost::regex(error)));
    }
  }
};

TEST_F(FlagValidatorsTest, TestValidators) {
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_even", "2"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_even", "1", "Must be even"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_plus_one", "3"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_plus_one", "2",
                               "Must be 1 greater than flag_validators_test_even: 2"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_odd", "5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_odd", "6",
                               "Invalid argument .* 6 is not odd"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_minus_one", "4"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_minus_one", "3",
                               "Invalid argument .* 3 is not flag_validators_test_odd - 1 "
                               "\\(flag_validators_test_odd = 5\\)"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs", "4"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs", "5", "Must be less than 5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_rhs", "4"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs", "3"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs", "4",
                               "Must be less than flag_validators_test_lt_rhs: 4"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs", "5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs", "6",
                               "Must be less than or equal to 5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_rhs", "4"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs", "4"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs", "5",
                               "Must be less than or equal to flag_validators_test_le_rhs: 4"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs", "6"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs", "5", "Must be greater than 5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_rhs", "6"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs", "7"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs", "6",
                               "Must be greater than flag_validators_test_gt_rhs: 6"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs", "5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs", "4",
                               "Must be greater than or equal to 5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_rhs", "6"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs", "6"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs", "5",
                               "Must be greater than or equal to flag_validators_test_ge_rhs: 6"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs_double", "4.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs_double", "5.0", "Must be less than 5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_rhs_double", "4.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs_double", "3.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_lt_lhs_double", "4.0",
                               "Must be less than flag_validators_test_lt_rhs_double: 4"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs_double", "5.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs_double", "6.0",
                               "Must be less than or equal to 5.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_rhs_double", "4.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs_double", "4.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_le_lhs_double", "5.0",
                               "Must be less than or equal to flag_validators_test_le_rhs_double: "
                               "4"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs_double", "6.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs_double", "5",
                               "Must be greater than 5.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_rhs_double", "6.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs_double", "7.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_gt_lhs_double", "6.0",
                               "Must be greater than flag_validators_test_gt_rhs_double: 6"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs_double", "5.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs_double", "4.0",
                               "Must be greater than or equal to 5.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_rhs_double", "6.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs_double", "6.0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ge_lhs_double", "5.0",
                               "Must be greater than or equal to "
                               "flag_validators_test_ge_rhs_double: 6"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_value", "foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_value", "bar", "Must be equal to \"foo\""));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_lhs", "foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_lhs", "bar",
                               "Must be equal to flag_validators_test_eq_rhs: foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_rhs", "bar"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_lhs", "foo",
                               "Must be equal to flag_validators_test_eq_rhs: bar"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_eq_lhs", "bar"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_value", "foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_value", "bar",
                               "Must be not equal to \"bar\""));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_lhs", "foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_lhs", "bar",
                               "Must be not equal to flag_validators_test_ne_rhs: bar"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_rhs", "foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_lhs", "foo",
                               "Must be not equal to flag_validators_test_ne_rhs: foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_ne_lhs", "bar"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_set", "foo"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_set", "bar"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_set", "baz",
                               "Must be one of: \"foo\", \"bar\""));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_range", "-1",
                               "Must be in the range \\[0-9\\]"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_range", "0"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_range", "5"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_range", "9"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_range", "10",
                               "Must be in the range \\[0-9\\]"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_requires_1", "true",
                               "Requires flag_validators_test_required_by_1 to be true"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_required_by_1", "true"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_requires_1", "true"));

  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_required_by_2", "false",
                               "Required by flag_validators_test_requires_2 to be true"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_requires_2", "false"));
  ASSERT_NO_FATALS(TestSetFlag("flag_validators_test_required_by_1", "false"));

  ASSERT_NO_FATALS(TestSetFlag("flag_dependent_on_base_flag", "true",
                               "Requires flag_base_defaults_to_zero to be non-zero"));
  ASSERT_NO_FATALS(TestSetFlag("flag_base_defaults_to_zero", "1"));
  ASSERT_NO_FATALS(TestSetFlag("flag_dependent_on_base_flag", "true"));
}

} // namespace yb
