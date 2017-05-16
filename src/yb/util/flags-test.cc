// Copyright (c) YugaByte, Inc.

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "yb/util/flags.h"
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

} // namespace yb
