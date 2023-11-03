// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <gtest/gtest.h>

#include "yb/consensus/log_util.h"
#include "yb/util/test_macros.h"

namespace yb {

#if defined(__linux__)
TEST(TestLogUtil, TestModifyDurableWriteFlagIfNotDirectNegative) {
  gflags::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) = "/var/run";

  // Test that the method does not crash and durable_wal_write is flipped
  // when directory is not O_DIRECT writable.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_durable_wal_write) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_require_durable_wal_write) = false;
  ASSERT_OK(log::ModifyDurableWriteFlagIfNotODirect());
  ASSERT_EQ(FLAGS_durable_wal_write, false);

  // Test that the method crashes when durable_wal_write is set to true.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_durable_wal_write) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_require_durable_wal_write) = true;
  ASSERT_NOK(log::ModifyDurableWriteFlagIfNotODirect());
}
#endif

TEST(TestLogUtil, TestModifyDurableWriteFlagIfNotODirectPositive) {
  gflags::FlagSaver flag_saver;
  // Test that the method does not crash when durable_wal_write is set true and paths are O_DIRECT
  // Writable.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_durable_wal_write) = true;
  ASSERT_OK(log::ModifyDurableWriteFlagIfNotODirect());
  // Test that the method does not crash when durable_wal_write is set to false.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_durable_wal_write) = false;
  ASSERT_OK(log::ModifyDurableWriteFlagIfNotODirect());
}
} // namespace yb
