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

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "yb/util/flags.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_string(vmodule);

using std::string;
using std::vector;

// LoggingTest is not actually an integration test, but we put it here, so it will be linked with
// all libraries used for YB cluster processes, because static object initialization in thse
// libraries could cause logging issues.
// For example: https://github.com/yugabyte/yugabyte-db/issues/3176

namespace yb {

// Test the YB_LOG_EVERY_N_SECS(...) macro.
TEST(LoggingTest, TestThrottledLogging) {
  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  for (int i = 0; i < 10000; i++) {
    YB_LOG_EVERY_N_SECS(INFO, 1) << "test" << THROTTLE_MSG;
    SleepFor(MonoDelta::FromMilliseconds(1));
    if (sink.logged_msgs().size() >= 2) break;
  }
  const vector<string>& msgs = sink.logged_msgs();
  ASSERT_GE(msgs.size(), 2);

  // The first log line shouldn't have a suppression count.
  EXPECT_THAT(msgs[0], testing::ContainsRegex("test$"));
  // The second one should have suppressed at least three digits worth of log messages.
  EXPECT_THAT(msgs[1], testing::ContainsRegex("\\[suppressed [0-9]{3,} similar messages\\]"));

  // Just compilation check.
  YB_LOG_EVERY_N_SECS(INFO, 1) << "test" << THROTTLE_MSG;
  YB_LOG_EVERY_N_SECS(INFO, 1) << "test" << THROTTLE_MSG;
}

TEST(LoggingTest, VModule) {
  google::FlagSaver flag_saver;

  google::SetVLOGLevel("logging-test", 3);

  ASSERT_TRUE(VLOG_IS_ON(1));
  ASSERT_TRUE(VLOG_IS_ON(2));
  ASSERT_TRUE(VLOG_IS_ON(3));
  ASSERT_FALSE(VLOG_IS_ON(4));

  constexpr auto kPattern = "vmodule-test";
  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  VLOG(1) << kPattern;
  VLOG_IF(3, true) << kPattern;
  VLOG(5) << kPattern;

  const vector<string>& msgs = sink.logged_msgs();

  ASSERT_GE(msgs.size(), 2);

  EXPECT_THAT(msgs[0], testing::HasSubstr(kPattern));
  EXPECT_THAT(msgs[0], testing::HasSubstr("vlog1: "));
  EXPECT_THAT(msgs[1], testing::HasSubstr("vlog3: "));
}

} // namespace yb
