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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/time.h>

#include "kudu/util/pstack_watcher.h"
#include "kudu/util/flags.h"
#include "kudu/util/status.h"

DEFINE_int32(test_timeout_after, 0,
             "Maximum total seconds allowed for all unit tests in the suite. Default: disabled");

// Start timer that kills the process if --test_timeout_after is exceeded before
// the tests complete.
static void CreateAndStartTimer();

// Gracefully kill the process.
static void KillTestOnTimeout(int signum);

int main(int argc, char **argv) {
  google::InstallFailureSignalHandler();
  // InitGoogleTest() must precede ParseCommandLineFlags(), as the former
  // removes gtest-related flags from argv that would trip up the latter.
  ::testing::InitGoogleTest(&argc, argv);
  kudu::ParseCommandLineFlags(&argc, &argv, true);

  // Create the test-timeout timer.
  CreateAndStartTimer();

  int ret = RUN_ALL_TESTS();

  return ret;
}

static void CreateAndStartTimer() {
  struct sigaction action;
  struct itimerval timer;

  // Create the test-timeout timer.
  memset(&action, 0, sizeof(action));
  action.sa_handler = &KillTestOnTimeout;
  CHECK_ERR(sigaction(SIGALRM, &action, nullptr)) << "Unable to set timeout action";

  timer.it_interval.tv_sec = 0;                      // No repeat.
  timer.it_interval.tv_usec = 0;
  timer.it_value.tv_sec = FLAGS_test_timeout_after;  // Fire in timeout seconds.
  timer.it_value.tv_usec = 0;

  CHECK_ERR(setitimer(ITIMER_REAL, &timer, nullptr)) << "Unable to set timeout timer";
}

static void KillTestOnTimeout(int signum) {
  // Dump a pstack to stdout.
  WARN_NOT_OK(kudu::PstackWatcher::DumpStacks(), "Unable to print pstack");

  // ...and abort.
  LOG(FATAL) << "Maximum unit test time exceeded (" << FLAGS_test_timeout_after << " sec)";
}
