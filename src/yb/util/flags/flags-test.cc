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
#include "yb/util/logging_test_util.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
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
DEFINE_NEW_INSTALL_VALUE(flagstest_testflag, 50);

DECLARE_string(vmodule);

DECLARE_string(allowed_preview_flags_csv);
DEFINE_RUNTIME_PREVIEW_bool(preview_flag, false, "preview flag");

DEFINE_NON_RUNTIME_string(flagstest_secret_flag, "", "This is a secret");
TAG_FLAG(flagstest_secret_flag, sensitive_info);

const auto kBadSecretValue = "yb_rox";
bool ValidateSecretFlag(const char* flag_name, const std::string& new_val) {
  if (new_val == kBadSecretValue) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, new_val) << "This is no secret. Everyone knows it!";
    return false;
  }
  return true;
}
DEFINE_validator(flagstest_secret_flag, &ValidateSecretFlag);

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
  ASSERT_EQ(
      SetFlag("vmodule", "BadValue", SetFlagForce::kFalse, &old_value, &output_msg),
      SetFlagResult::BAD_VALUE);
  ASSERT_STR_CONTAINS(output_msg, "'BadValue' is not valid");

  ASSERT_EQ(
      SetFlag("vmodule", "files=", SetFlagForce::kFalse, &old_value, &output_msg),
      SetFlagResult::BAD_VALUE);
  ASSERT_STR_CONTAINS(output_msg, "'files=' is not valid");

  ASSERT_EQ(
      SetFlag("vmodule", "biggerThanInt=2147483648", SetFlagForce::kFalse, &old_value, &output_msg),
      SetFlagResult::BAD_VALUE);
  ASSERT_STR_CONTAINS(
      output_msg,
      "Invalid logging level '2147483648' for module 'biggerThanInt'. Only integer values between "
      "-2147483648 and 2147483647 are allowed");

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

TEST_F(FlagsTest, ValidateFlagValue) {
  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  // Test a valid value and make sure the flag is not changed.
  ASSERT_OK(flags_internal::ValidateFlagValue("flagstest_testflag", "1"));
  ASSERT_EQ(FLAGS_flagstest_testflag, 0);

  // Test an invalid value.
  const auto kBadValueMessage = "Must be >= 0";
  ASSERT_NOK_STR_CONTAINS(
      flags_internal::ValidateFlagValue("flagstest_testflag", "-1"), kBadValueMessage);
  ASSERT_EQ(FLAGS_flagstest_testflag, 0);

  // We should have logged the error to the logs as well.
  ASSERT_STR_CONTAINS(AsString(sink.logged_msgs()), kBadValueMessage);

  // Test validating a flag with tag sensitive_info.
  const auto kSecretValue = "$secr3t#";
  ASSERT_OK(flags_internal::ValidateFlagValue("flagstest_secret_flag", kSecretValue));
  ASSERT_EQ(FLAGS_flagstest_secret_flag, "");

  // Test an invalid value and make sure the status does not contain the secret value.
  auto status = flags_internal::ValidateFlagValue("flagstest_secret_flag", kBadSecretValue);
  ASSERT_NOK(status);
  ASSERT_STR_NOT_CONTAINS(status.ToString(), kBadSecretValue);
  ASSERT_EQ(FLAGS_flagstest_secret_flag, "");

  // We should not have logged the secret values to the logs.
  ASSERT_STR_NOT_CONTAINS(AsString(sink.logged_msgs()), kSecretValue);
  ASSERT_STR_NOT_CONTAINS(AsString(sink.logged_msgs()), kBadSecretValue);

  // Test the vmodule flag.
  const auto kVmoduleValue = "file=1";
  ASSERT_OK(SET_FLAG(vmodule, kVmoduleValue));
  ASSERT_EQ(FLAGS_vmodule, kVmoduleValue);
  ASSERT_OK(flags_internal::ValidateFlagValue("vmodule", "files=1"));
  ASSERT_NOK(flags_internal::ValidateFlagValue("vmodule", "files="));
  ASSERT_EQ(FLAGS_vmodule, kVmoduleValue);

  // Test Preview flags.
  // When not in allow list we should only be able to set to the default value.
  ASSERT_OK(flags_internal::ValidateFlagValue("preview_flag", "false"));
  constexpr auto kPreviewFlagMissingError =
      "Flag 'preview_flag' protects a feature that is currently in preview. In order for it to be "
      "modified, you must acknowledge the risks by adding 'preview_flag' to the flag "
      "'allowed_preview_flags_csv'";
  ASSERT_NOK_STR_CONTAINS(
      flags_internal::ValidateFlagValue("preview_flag", "true"), kPreviewFlagMissingError);

  // Test the allowed_preview_flags_csv flag.
  ASSERT_OK(flags_internal::ValidateFlagValue("allowed_preview_flags_csv", "na_flag"));
  ASSERT_OK(flags_internal::ValidateFlagValue("allowed_preview_flags_csv", "preview_flag"));
  std::string old_value, output_msg;
  ASSERT_EQ(
      SetFlag(
          "allowed_preview_flags_csv", "preview_flag", flags_internal::SetFlagForce::kFalse,
          &old_value, &output_msg),
      flags_internal::SetFlagResult::SUCCESS);
  ASSERT_OK(flags_internal::ValidateFlagValue("preview_flag", "true"));

  ASSERT_EQ(
      SetFlag(
          "preview_flag", "true", flags_internal::SetFlagForce::kFalse, &old_value, &output_msg),
      flags_internal::SetFlagResult::SUCCESS);
  ASSERT_NOK_STR_CONTAINS(
      flags_internal::ValidateFlagValue("allowed_preview_flags_csv", ""), kPreviewFlagMissingError);
}

namespace flags_internal {
std::optional<std::string> GetFlagNewInstallValue(const std::string& flag_name);
}  // namespace flags_internal

TEST_F(FlagsTest, NewInstallValue) {
  auto new_install_value = flags_internal::GetFlagNewInstallValue("flagstest_testflag");
  ASSERT_TRUE(new_install_value.has_value());
  ASSERT_EQ(*new_install_value, "50");

  new_install_value = flags_internal::GetFlagNewInstallValue("flagstest_secret_flag");
  ASSERT_FALSE(new_install_value.has_value());
}

} // namespace yb
