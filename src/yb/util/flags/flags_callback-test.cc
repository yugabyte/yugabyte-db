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

#include "yb/util/flags/flags_callback.h"
#include "yb/util/flags.h"
#include "yb/util/test_util.h"

DEFINE_RUNTIME_bool(cb_test, false, "test flag");
static bool global1_called = false;
REGISTER_CALLBACK(cb_test, "Global callback", []() { global1_called = true; });

DEFINE_RUNTIME_bool(cb_test2, false, "test flag");
static bool global2_set = false;
REGISTER_CALLBACK(cb_test2, "Global callback", []() { global2_set = FLAGS_cb_test2; });

namespace yb {

class FlagsCallbackTest : public YBTest {};

TEST(FlagsCallbackTest, TestCallback) {
  ASSERT_TRUE(global1_called);
  ASSERT_FALSE(global2_set);

  bool local_called1 = false;
  bool local_called2 = false;
  auto callback_info1 = ASSERT_RESULT(
      RegisterFlagUpdateCallback(&FLAGS_cb_test, "test_cb", [&]() { local_called1 = true; }));
  ASSERT_FALSE(local_called1);

  // Register different callback function with same name.
  ASSERT_NOK(RegisterFlagUpdateCallback(
      &FLAGS_cb_test, "test_cb", [&]() { local_called1 = local_called1; }));

  auto callback_info2 = ASSERT_RESULT(
      RegisterFlagUpdateCallback(&FLAGS_cb_test2, "test_cb", [&]() { local_called2 = true; }));
  ASSERT_FALSE(local_called2);

  global1_called = false;
  ASSERT_OK(SET_FLAG(cb_test, true));
  ASSERT_TRUE(local_called1);
  ASSERT_FALSE(local_called2);
  ASSERT_TRUE(global1_called);
  ASSERT_FALSE(global2_set);

  ASSERT_OK(SET_FLAG(cb_test2, false));
  ASSERT_TRUE(local_called2);
  ASSERT_FALSE(global2_set);

  ASSERT_OK(SET_FLAG(cb_test2, true));
  ASSERT_TRUE(global2_set);

  ASSERT_NO_FATALS(callback_info1.Deregister());
  ASSERT_NO_FATALS(callback_info2.Deregister());
  // Double deregister is No op and ok.
  ASSERT_NO_FATALS(callback_info1.Deregister());
}

}  // namespace yb
