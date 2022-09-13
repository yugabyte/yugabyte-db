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

#include <gflags/gflags.h>

#include "yb/util/flags.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

DEFINE_int32(flagstest_testflag, 0, "test flag");

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
  ASSERT_OK(SetFlagDefaultAndCurrent("flagstest_testflag", "1"));
  ASSERT_EQ(1, FLAGS_flagstest_testflag);

  ASSERT_NOK(SetFlagDefaultAndCurrent("flagstest_testflag", "NA"));
}
} // namespace yb
