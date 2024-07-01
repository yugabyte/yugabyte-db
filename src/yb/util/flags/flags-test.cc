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

#include <filesystem>

#include "yb/util/flags.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

using std::string;

DEFINE_NON_RUNTIME_int32(flagstest_testflag, 0, "test flag");
bool ValidateTestFlag(const char* flag_name, const int32 new_val) {
  if (new_val >= 0) {
    return true;
  }

  LOG_FLAG_VALIDATION_ERROR(flag_name, new_val) << "Must be >= 0";
  return false;
}
DEFINE_validator(flagstest_testflag, &ValidateTestFlag);
DECLARE_string(vmodule);

DECLARE_string(allowed_preview_flags_csv);
DEFINE_RUNTIME_PREVIEW_bool(preview_flag, false, "preview flag");

namespace yb {

class FlagsTest : public YBTest {
};

TEST_F(FlagsTest, TestRefreshFlagsFile) {
  ASSERT_EQ(0, FLAGS_flagstest_testflag);
  std::string flagsfile = GetTestPath("flagsfile");
  CHECK_OK(WriteStringToFile(env_.get(), "--flagstest_testflag=100", flagsfile));
  RefreshFlagsFile(flagsfile);
  ASSERT_EQ(100, FLAGS_flagstest_testflag);
}

TEST_F(FlagsTest, TestSetFlagDefault) {
  ASSERT_EQ(0, FLAGS_flagstest_testflag);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flagstest_testflag) = 2;
  ASSERT_OK(SET_FLAG_DEFAULT_AND_CURRENT(flagstest_testflag, 1));
  ASSERT_EQ(1, FLAGS_flagstest_testflag);

  // Make sure validator is called. Set to a non valid number.
  ASSERT_NOK(SET_FLAG_DEFAULT_AND_CURRENT(flagstest_testflag, -1));
}

TEST_F(FlagsTest, TestSetFlag) {
  ASSERT_EQ(FLAGS_flagstest_testflag, 0);

  std::string old_value, output_msg;
  auto res =
      SetFlag("flag_not_exist", "1", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::NO_SUCH_FLAG);

  // Flag is not runtime so it should fail to set without the Force flag.
  res = SetFlag(
      "flagstest_testflag", "1", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::NOT_SAFE);

  res = SetFlag(
      "flagstest_testflag", "-1", flags_internal::SetFlagForce::kTrue, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::BAD_VALUE);
  ASSERT_STR_CONTAINS(output_msg, "flagstest_testflag");
  ASSERT_STR_CONTAINS(output_msg, "-1");
  ASSERT_STR_CONTAINS(output_msg, "Must be >= 0");

  ASSERT_EQ(0, FLAGS_flagstest_testflag);

  res = SetFlag(
      "flagstest_testflag", "1", flags_internal::SetFlagForce::kTrue, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);

  ASSERT_EQ(1, FLAGS_flagstest_testflag);
}

TEST_F(FlagsTest, TestVmodule) {
  using flags_internal::SetFlagForce;
  using flags_internal::SetFlagResult;
  const string file_name = std::filesystem::path(__FILE__).stem();
  ASSERT_EQ(FLAGS_vmodule, "");
  ASSERT_FALSE(VLOG_IS_ON(1));
  string expected_old = FLAGS_vmodule;

  // Set to invalid value
  string old_value, output_msg;
  ASSERT_DEATH(
      SetFlag("vmodule", "BadValue", SetFlagForce::kFalse, &old_value, &output_msg),
      "'BadValue' is not valid");

  ASSERT_DEATH(
      SetFlag("vmodule", "files=", SetFlagForce::kFalse, &old_value, &output_msg),
      "'files=' is not valid");

  ASSERT_DEATH(
      SetFlag("vmodule", "biggerThanInt=2147483648", SetFlagForce::kFalse, &old_value, &output_msg),
      "'2147483648' is not a valid integer number");

  ASSERT_EQ(FLAGS_vmodule, expected_old);
  ASSERT_FALSE(VLOG_IS_ON(1));

  auto res = SetFlag("vmodule", "", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old);
  ASSERT_EQ(FLAGS_vmodule, "");
  expected_old = FLAGS_vmodule;

  // Add a new module to the list
  res = SetFlag("vmodule", file_name + "=1", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old);
  ASSERT_EQ(FLAGS_vmodule, file_name + "=1");
  ASSERT_TRUE(VLOG_IS_ON(1));
  ASSERT_FALSE(VLOG_IS_ON(2));
  expected_old = FLAGS_vmodule;

  // Add another module. modules not set should reset to 0
  res = SetFlag("vmodule", "file_not_exist=1", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old);
  ASSERT_EQ(FLAGS_vmodule, file_name + "=0,file_not_exist=1");
  ASSERT_FALSE(VLOG_IS_ON(1));
  expected_old = FLAGS_vmodule;

  // Update an existing module
  res = SetFlag("vmodule", file_name + "=3", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old);
  ASSERT_EQ(FLAGS_vmodule, file_name + "=3,file_not_exist=0");
  ASSERT_TRUE(VLOG_IS_ON(3));
  expected_old = FLAGS_vmodule;

  res = SetFlag("vmodule", "", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old);
  ASSERT_EQ(FLAGS_vmodule, file_name + "=0,file_not_exist=0");
  expected_old = FLAGS_vmodule;
}

TEST_F(FlagsTest, TestPreviewFlagsAllowList) {
  ASSERT_EQ(FLAGS_allowed_preview_flags_csv, "");
  ASSERT_EQ(FLAGS_preview_flag, false);

  string expected_old_allow_list = FLAGS_allowed_preview_flags_csv;
  string expected_old_preview_flag = "false";

  // Should be able to set default value.
  string old_value, output_msg;
  auto res = SetFlag(
      "preview_flag", "false", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old_preview_flag);
  ASSERT_EQ(FLAGS_preview_flag, false);

  // Should not be able to set flag without adding in allow list.
  res = SetFlag(
      "preview_flag", "true", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::BAD_VALUE);

  // Add flags to allow list.
  res = SetFlag(
      "allowed_preview_flags_csv", "some_flag,preview_flag", flags_internal::SetFlagForce::kFalse,
      &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old_allow_list);
  ASSERT_EQ(FLAGS_allowed_preview_flags_csv, "some_flag,preview_flag");
  expected_old_allow_list = FLAGS_allowed_preview_flags_csv;

  // Should be able to modify preview flag now.
  res = SetFlag(
      "preview_flag", "true", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old_preview_flag);
  ASSERT_EQ(FLAGS_preview_flag, true);
  expected_old_preview_flag = "true";

  // Should not be able to remove preview_flag from allow list.
  res = SetFlag(
      "allowed_preview_flags_csv", "some_flag", flags_internal::SetFlagForce::kFalse, &old_value,
      &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::BAD_VALUE);

  // Should be able to remove some_flag from allow list.
  res = SetFlag(
      "allowed_preview_flags_csv", "preview_flag", flags_internal::SetFlagForce::kFalse, &old_value,
      &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old_allow_list);
  ASSERT_EQ(FLAGS_allowed_preview_flags_csv, "preview_flag");
  expected_old_allow_list = FLAGS_allowed_preview_flags_csv;

  // Should be able to reset preview flag to default value and remove from allow list.
  res = SetFlag(
      "preview_flag", "false", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old_preview_flag);
  ASSERT_EQ(FLAGS_preview_flag, false);
  expected_old_preview_flag = "false";

  res = SetFlag(
      "allowed_preview_flags_csv", "", flags_internal::SetFlagForce::kFalse, &old_value,
      &output_msg);
  ASSERT_EQ(res, flags_internal::SetFlagResult::SUCCESS);
  ASSERT_EQ(old_value, expected_old_allow_list);
  ASSERT_EQ(FLAGS_allowed_preview_flags_csv, "");
  expected_old_allow_list = FLAGS_allowed_preview_flags_csv;
}
} // namespace yb
