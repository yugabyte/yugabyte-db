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

#include "yb/util/disk_space_checker.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(reject_writes_when_disk_full);
DECLARE_uint64(reject_writes_min_disk_space_mb);
DECLARE_uint32(reject_writes_min_disk_space_pct);
DECLARE_uint32(max_disk_throughput_mbps);
DECLARE_int64(TEST_simulate_free_space_bytes);

namespace yb {

class DiskSpaceCheckerTest : public YBTest {
 protected:
  // Free space reported for dir_, so that the thresholds under test decide the outcome instead of
  // the state of the machine running the test.
  static constexpr uint64_t kFreeSpaceMb = 1024;

  void SetUp() override {
    YBTest::SetUp();
    // The flag defaults to false in ASAN builds, which disables every check below.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_when_disk_full) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_free_space_bytes) = kFreeSpaceMb * 1_MB;
    dir_ = GetTestPath("data");
    ASSERT_OK(env_->CreateDir(dir_));
  }

  // Every check uses a fresh checker so that the cached result of the previous check is not used.
  bool HasSufficientDiskSpace() {
    return DiskSpaceChecker(env_.get(), dir_).HasSufficientDiskSpace();
  }

  std::string dir_;
};

TEST_F(DiskSpaceCheckerTest, Thresholds) {
  // Leave only the MB-based threshold in play.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_pct) = 0;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = kFreeSpaceMb / 2;
  ASSERT_TRUE(HasSufficientDiskSpace());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = kFreeSpaceMb * 2;
  ASSERT_FALSE(HasSufficientDiskSpace());

  // A zero value derives the threshold from --max_disk_throughput_mbps: 10s worth of writes, which
  // at 1MBps fits in the free space.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_disk_throughput_mbps) = 1;
  ASSERT_TRUE(HasSufficientDiskSpace());

  // The same 10s of writes no longer fit once the throughput is as large as the free space.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_disk_throughput_mbps) = kFreeSpaceMb;
  ASSERT_FALSE(HasSufficientDiskSpace());

  // Requiring the entire disk capacity to be free always fails, since the free space above is far
  // below the capacity of any disk this test can run on.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_disk_throughput_mbps) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_pct) = 100;
  ASSERT_FALSE(HasSufficientDiskSpace());

  // A zero value disables the percentage-based threshold.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_pct) = 0;
  ASSERT_TRUE(HasSufficientDiskSpace());
}

TEST_F(DiskSpaceCheckerTest, AlwaysCheckDisk) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_pct) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = kFreeSpaceMb / 2;

  DiskSpaceChecker cached_checker(env_.get(), dir_);
  DiskSpaceChecker always_checker(env_.get(), dir_, /* always_check_disk = */ true);

  ASSERT_TRUE(cached_checker.HasSufficientDiskSpace());
  ASSERT_TRUE(always_checker.HasSufficientDiskSpace());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = kFreeSpaceMb * 2;

  // The cached checker keeps returning the previous result until the check interval elapses, while
  // the always-check one picks up the new threshold right away.
  ASSERT_TRUE(cached_checker.HasSufficientDiskSpace());
  ASSERT_FALSE(always_checker.HasSufficientDiskSpace());
}

}  // namespace yb
