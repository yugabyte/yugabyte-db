// Copyright (c) YugabyteDB, Inc.

#include <thread>

#include "yb/gutil/sysinfo.h"

#include "yb/util/logging.h"
#include "yb/util/test_util.h"

DECLARE_int32(num_cpus);

namespace yb {
namespace gutil {

class SysInfoTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    InitGoogleLoggingSafe("SysInfoTest");
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 0;
  }

  void TearDown() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 0;
    YBTest::TearDown();
  }
};

// Test that with default flags, NumCPUs returns the hardware CPU count.
TEST_F(SysInfoTest, NumCpusDefaultMatchesHardware) {
  ASSERT_EQ(base::NumCPUs(), std::thread::hardware_concurrency());
  ASSERT_EQ(base::NumCPUs(), base::RawNumCPUs());
}

// Test that --num_cpus flag overrides auto-detected value.
TEST_F(SysInfoTest, NumCpusFlagOverride) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 20;
  ASSERT_EQ(base::NumCPUs(), 20);
}

// Test that --num_cpus flag takes priority in both directions relative to auto-detected.
// RawNumCPUs is never affected by the flag.
TEST_F(SysInfoTest, NumCpusFlagBothDirections) {
  int raw = base::RawNumCPUs();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = raw + 10;
  ASSERT_EQ(base::NumCPUs(), raw + 10);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 1;
  ASSERT_EQ(base::NumCPUs(), 1);
  ASSERT_EQ(base::RawNumCPUs(), raw);
}

}   // namespace gutil
}   // namespace yb
