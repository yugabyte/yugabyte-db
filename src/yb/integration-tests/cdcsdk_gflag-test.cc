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

#include <boost/assign.hpp>
#include <gtest/gtest.h>

#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/util/test_macros.h"

DECLARE_int32(cdc_snapshot_batch_size);
DECLARE_int32(cdc_max_stream_intent_records);

namespace yb {
namespace cdc {
class CDCSDKGFlagValueTest : public CDCSDKTestBase {
};

TEST_F(CDCSDKGFlagValueTest, YB_DISABLE_TEST_IN_TSAN(GFlagsDefaultValue)) {
  // create a cluster
  ASSERT_OK(SetUpWithParams(3, 1, false));

  const uint32_t default_intent_batch_size = 1680;
  const uint32_t default_snapshot_batch_size = 250;

  ASSERT_EQ(default_intent_batch_size, FLAGS_cdc_max_stream_intent_records);
  ASSERT_EQ(default_snapshot_batch_size, FLAGS_cdc_snapshot_batch_size);
}
} // namespace cdc
} // namespace yb
