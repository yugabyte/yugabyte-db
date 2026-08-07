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

#include "yb/tserver/server_main_util.h"

#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

DECLARE_int64(global_memstore_size_mb_max);

namespace yb {

class ServerMainUtilTest : public YBTest {};

TEST_F(ServerMainUtilTest, GlobalMemstoreDefault) {
  google::FlagSaver flag_saver;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_memstore_size_mb_max) = USE_RECOMMENDED_MEMORY_VALUE;
  internal::AdjustMemoryLimits(/* is_master= */ false, 64_GB - 1);
  ASSERT_EQ(FLAGS_global_memstore_size_mb_max, 2048);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_memstore_size_mb_max) = USE_RECOMMENDED_MEMORY_VALUE;
  internal::AdjustMemoryLimits(/* is_master= */ false, 64_GB);
  ASSERT_EQ(FLAGS_global_memstore_size_mb_max, 4096);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_memstore_size_mb_max) = USE_RECOMMENDED_MEMORY_VALUE;
  internal::AdjustMemoryLimits(/* is_master= */ false, 64_GB + 1);
  ASSERT_EQ(FLAGS_global_memstore_size_mb_max, 4096);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_memstore_size_mb_max) = USE_RECOMMENDED_MEMORY_VALUE;
  internal::AdjustMemoryLimits(/* is_master= */ true, 128_GB);
  ASSERT_EQ(FLAGS_global_memstore_size_mb_max, 2048);
}

TEST_F(ServerMainUtilTest, GlobalMemstoreOverride) {
  google::FlagSaver flag_saver;

  for (const auto value : {0, 2048, 3072}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_memstore_size_mb_max) = value;
    internal::AdjustMemoryLimits(/* is_master= */ false, 64_GB);
    ASSERT_EQ(FLAGS_global_memstore_size_mb_max, value);
  }
}

}  // namespace yb
