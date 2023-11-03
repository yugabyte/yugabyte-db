// Copyright (c) YugaByte, Inc.

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
  }

  void TearDown() override {
    YBTest::TearDown();
  }
};

// Test num_cpus defaults to hardware_concurrency value
// and is equal to gutil's original NumCPUs function.
TEST_F(SysInfoTest, NumCpusZeroTest) {
  ASSERT_EQ(base::NumCPUs(), std::thread::hardware_concurrency());
  ASSERT_EQ(base::NumCPUs(), base::RawNumCPUs());
}

// Test gflag value changes are reflected in NumCPUs function
TEST_F(SysInfoTest, NumCpusChangedTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 20;
  ASSERT_EQ(base::NumCPUs(), 20);
}

}   // namespace gutil
}   // namespace yb
