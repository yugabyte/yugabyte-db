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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

namespace yb {

// Test the YB_LOG_EVERY_N_SECS(...) macro.
TEST(LoggingTest, TestThrottledLogging) {
  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  for (int i = 0; i < 10000; i++) {
    YB_LOG_EVERY_N_SECS(INFO, 1) << "test";
    SleepFor(MonoDelta::FromMilliseconds(1));
    if (sink.logged_msgs().size() >= 2) break;
  }
  const vector<string>& msgs = sink.logged_msgs();
  ASSERT_GE(msgs.size(), 2);

  // The first log line shouldn't have a suppression count.
  EXPECT_THAT(msgs[0], testing::ContainsRegex("test$"));
  // The second one should have suppressed at least two digits worth of log messages.
  EXPECT_THAT(msgs[1], testing::ContainsRegex("\\[suppressed [0-9]{2,} similar messages\\]"));

  // Just compilation check.
  YB_LOG_EVERY_N_SECS(INFO, 1) << "test";
  YB_LOG_EVERY_N_SECS(INFO, 1) << "test";
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

// Test the YB_LOG_EVERY_N_SECS_OR_VLOG(...) macro when vlog is off.
TEST(LoggingTest, TestThrottledOrVlogWithoutVlog) {
  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  // Log 5000 messages over a period of 5 seconds.
  for (int i = 0; i < 5000; i++) {
    YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 1, 1) << "test";
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  const vector<string>& msgs = sink.logged_msgs();
  ASSERT_GE(msgs.size(), 2);
  ASSERT_LT(msgs.size(), 50);

  // The first log line shouldn't have a suppression count.
  EXPECT_THAT(msgs[0], testing::ContainsRegex("] test$"));
  // The second one should have suppressed at least two digits worth of log messages.
  EXPECT_THAT(
      msgs[1], testing::ContainsRegex("\\] test [suppressed [0-9]{2,} similar messages\\]$"));

  // Just compilation check.
  YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 1, 2) << "test";
  YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 1, 3) << "test";
}

// Test the YB_LOG_EVERY_N_SECS_OR_VLOG(...) macro when vlog is on.
TEST(LoggingTest, TestThrottledOrVlogWithVlog) {
  google::SetVLOGLevel("logging-test", 1);

  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  // Log 2000 messages over a period of 2 seconds.
  for (int i = 0; i < 2000; i++) {
    YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 1, 1) << "test";
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  const vector<string>& msgs = sink.logged_msgs();
  ASSERT_EQ(msgs.size(), 2000);

  // The first log line shouldn't have a suppression count.
  EXPECT_THAT(msgs[0], testing::ContainsRegex("] vlog1: test$"));
}

// Test macros for YB-severity log levels.
TEST(LoggingTest, TestYBLogSeverity) {
  StringVectorSink sink;
  ScopedRegisterSink srs(&sink);

  constexpr auto kPattern = "test ";

  class PrefixLogger {
   public:
    const std::string& LogPrefix() const {
      static std::string prefix = "PREFIX:";
      return prefix;
    }

    void Log(const std::string& msg) {
      LOG_WITH_PREFIX_AND_FUNC(DETAIL) << kPattern << msg;
    }

    void LogIf(bool condition, const std::string& msg) {
      LOG_IF_WITH_PREFIX_AND_FUNC(DETAIL, condition) << kPattern << msg;
    }
  } prefix_logger;

  LOG(DETAIL) << kPattern << "a";
  LOG(DETAIL) << kPattern << "b";
  LOG_WITH_FUNC(DETAIL) << kPattern << "c";
  prefix_logger.Log("d");
  prefix_logger.LogIf(/* condition = */ true, "e");
  prefix_logger.LogIf(/* condition = */ false, "e (shouldn't be logged)");

  const vector<string>& msgs = sink.logged_msgs();

  ASSERT_EQ(msgs.size(), 5);

  EXPECT_THAT(msgs[0], testing::EndsWith("] DETAIL: test a"));
  EXPECT_THAT(msgs[1], testing::EndsWith("] DETAIL: test b"));
  EXPECT_THAT(msgs[2], testing::EndsWith("] DETAIL: TestBody: test c"));
  EXPECT_THAT(msgs[3], testing::EndsWith("] DETAIL: PREFIX:Log: test d"));
  EXPECT_THAT(msgs[4], testing::EndsWith("] DETAIL: PREFIX:LogIf: test e"));
}

}  // namespace yb
