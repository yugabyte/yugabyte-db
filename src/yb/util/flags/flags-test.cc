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

DEFINE_UNKNOWN_int32(flagstest_testflag, 0, "test flag");
bool ValidateTestFlag(const char* flag_name, const int32 new_val) { return new_val >= 0; }
DEFINE_validator(flagstest_testflag, &ValidateTestFlag);
DECLARE_string(vmodule);

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
  FLAGS_flagstest_testflag = 2;
  ASSERT_OK(SET_FLAG_DEFAULT_AND_CURRENT(flagstest_testflag, 1));
  ASSERT_EQ(1, FLAGS_flagstest_testflag);

  // Make sure validator is called. Set to a non valid number.
  ASSERT_NOK(SET_FLAG_DEFAULT_AND_CURRENT(flagstest_testflag, -1));
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
  auto res = SetFlag("vmodule", "BadValue", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::BAD_VALUE);

  res = SetFlag("vmodule", "files=", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::BAD_VALUE);

  res =
      SetFlag("vmodule", "biggerThanInt=2147483648", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::BAD_VALUE);

  res = SetFlag("vmodule", "files=-1b", SetFlagForce::kFalse, &old_value, &output_msg);
  ASSERT_EQ(res, SetFlagResult::BAD_VALUE);

  ASSERT_EQ(FLAGS_vmodule, expected_old);
  ASSERT_FALSE(VLOG_IS_ON(1));

  res = SetFlag("vmodule", "", SetFlagForce::kFalse, &old_value, &output_msg);
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
} // namespace yb
