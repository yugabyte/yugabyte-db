// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <gtest/gtest.h>

#include "yb/util/operation_counter.h"
#include "yb/util/test_macros.h"

namespace yb {

TEST(TestOperationCounter, TestCounter) {
  RWOperationCounter rw_counter("test_counter");
  ASSERT_EQ(0, rw_counter.Get());
  ASSERT_EQ(0, rw_counter.GetOpCounter());

  // Test increment
  ASSERT_TRUE(rw_counter.Increment());
  ASSERT_EQ(1, rw_counter.Get());

  // Test decrement
  rw_counter.Decrement();
  ASSERT_EQ(0, rw_counter.Get());

  // Test Disable
  ASSERT_OK(rw_counter.DisableAndWaitForOps(CoarseTimePoint::max(), Stop::kFalse));
  rw_counter.UnlockExclusiveOpMutex();
  ASSERT_EQ(1, rw_counter.TEST_GetDisableCount());
  ASSERT_OK(rw_counter.DisableAndWaitForOps(CoarseTimePoint::max(), Stop::kFalse));
  rw_counter.UnlockExclusiveOpMutex();
  ASSERT_EQ(2, rw_counter.TEST_GetDisableCount());
  ASSERT_EQ(0, rw_counter.GetOpCounter());

  ASSERT_FALSE(rw_counter.Increment());
  ASSERT_EQ(0, rw_counter.GetOpCounter());

  rw_counter.Enable(Unlock::kFalse, Stop::kFalse);
  ASSERT_EQ(1, rw_counter.TEST_GetDisableCount());
  rw_counter.Enable(Unlock::kFalse, Stop::kFalse);
  ASSERT_EQ(0, rw_counter.TEST_GetDisableCount());
  ASSERT_EQ(0, rw_counter.GetOpCounter());

  // Test Stop
  ASSERT_OK(rw_counter.DisableAndWaitForOps(CoarseTimePoint::max(), Stop::kTrue));
  rw_counter.UnlockExclusiveOpMutex();
  ASSERT_TRUE(rw_counter.TEST_IsStopped());
  ASSERT_EQ(0, rw_counter.GetOpCounter());

  ASSERT_FALSE(rw_counter.Increment());
  ASSERT_EQ(0, rw_counter.GetOpCounter());

  rw_counter.Enable(Unlock::kFalse, Stop::kTrue);
  ASSERT_FALSE(rw_counter.TEST_IsStopped());

  // Test increment again
  ASSERT_TRUE(rw_counter.Increment());
  ASSERT_EQ(1, rw_counter.Get());
}

}  // namespace yb
