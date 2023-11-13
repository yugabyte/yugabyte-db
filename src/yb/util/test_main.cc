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

#include <signal.h> // For sigaction
#include <sys/time.h>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/util/pstack_watcher.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/debug-util.h"

using testing::EmptyTestEventListener;
using testing::TestPartResult;
using testing::UnitTest;

using yb::GetStackTrace;
using yb::StackTraceLineFormat;

using std::string;

DEFINE_UNKNOWN_int32(test_timeout_after, 0,
             "Maximum total seconds allowed for all unit tests in the suite. Default: disabled");
DECLARE_bool(TEST_promote_all_auto_flags);

// Start timer that kills the process if --test_timeout_after is exceeded before
// the tests complete.
static void CreateAndStartTimer();

// Gracefully kill the process.
static void KillTestOnTimeout(int signum);

class MinimalistPrinter : public EmptyTestEventListener{
  // Called after a failed assertion or a SUCCEED() invocation.
  virtual void OnTestPartResult(const TestPartResult& test_part_result) {
    if (test_part_result.failed()) {
      string stack_trace = GetStackTrace(StackTraceLineFormat::CLION_CLICKABLE, 4);

      // Remove the common part of all Google Test tests from the stack trace.
      const char* kDontShowLinesStartingWith =
          "void testing::internal::HandleSehExceptionsInMethodIfSupported<testing::Test, void>("
          "testing::Test*, void (testing::Test::*)(), char const*)";
      size_t cutoff_pos = stack_trace.find(kDontShowLinesStartingWith, 0);
      if (cutoff_pos != string::npos) {
        cutoff_pos = stack_trace.find_last_of('\n', cutoff_pos);
        if (cutoff_pos != string::npos) {
          stack_trace = stack_trace.substr(0, cutoff_pos);
        }
      }

      std::cerr << "Test failure stack trace:\n" << stack_trace << std::endl;
    }
  }
};

int main(int argc, char **argv) {
  google::InstallFailureSignalHandler();
  // InitGoogleTest() must precede ParseCommandLineFlags(), as the former
  // removes gtest-related flags from argv that would trip up the latter.
  ::testing::InitGoogleTest(&argc, argv);

  // Set before ParseCommandLineFlags so that user provided override takes precedence.
  FLAGS_TEST_promote_all_auto_flags = yb::ShouldTestPromoteAllAutoFlags();

  yb::ParseCommandLineFlags(&argc, &argv, /* remove_flags */ true);

  // Create the test-timeout timer.
  CreateAndStartTimer();

  // Gets hold of the event listener list.
  auto& listeners = UnitTest::GetInstance()->listeners();

  // Adds a listener to the end.  Google Test takes the ownership.
  listeners.Append(new MinimalistPrinter());

  int ret = RUN_ALL_TESTS();

  return ret;
}

static void CreateAndStartTimer() {
  // Create the test-timeout timer.
  CHECK_OK(yb::InstallSignalHandler(SIGALRM, &KillTestOnTimeout));

  struct itimerval timer;
  timer.it_interval.tv_sec = 0;                      // No repeat.
  timer.it_interval.tv_usec = 0;
  timer.it_value.tv_sec = FLAGS_test_timeout_after;  // Fire in timeout seconds.
  timer.it_value.tv_usec = 0;

  CHECK_ERR(setitimer(ITIMER_REAL, &timer, nullptr)) << "Unable to set timeout timer";
}

static void KillTestOnTimeout(int signum) {
  // Dump a pstack to stdout.
  WARN_NOT_OK(yb::PstackWatcher::DumpStacks(), "Unable to print pstack");

  // ...and abort.
  LOG(FATAL) << "Maximum unit test time exceeded (" << FLAGS_test_timeout_after << " sec)";
}
