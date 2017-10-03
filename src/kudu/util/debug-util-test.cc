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

#include <glog/stl_logging.h>
#include <signal.h>
#include <string>
#include <vector>

#include "kudu/gutil/ref_counted.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/debug-util.h"
#include "kudu/util/test_util.h"
#include "kudu/util/thread.h"

using std::string;
using std::vector;

namespace kudu {

class DebugUtilTest : public KuduTest {
};

TEST_F(DebugUtilTest, TestStackTrace) {
  StackTrace t;
  t.Collect(1);
  string trace = t.Symbolize();
  ASSERT_STR_CONTAINS(trace, "kudu::DebugUtilTest_TestStackTrace_Test::TestBody");
}

// DumpThreadStack is only supported on Linux, since the implementation relies
// on the tgkill syscall which is not portable.
#if defined(__linux__)

namespace {
void SleeperThread(CountDownLatch* l) {
  // We use an infinite loop around WaitFor() instead of a normal Wait()
  // so that this test passes in TSAN. Without this, we run into this TSAN
  // bug which prevents the sleeping thread from handling signals:
  // https://code.google.com/p/thread-sanitizer/issues/detail?id=91
  while (!l->WaitFor(MonoDelta::FromMilliseconds(10))) {
  }
}

void fake_signal_handler(int signum) {}

bool IsSignalHandlerRegistered(int signum) {
  struct sigaction cur_action;
  CHECK_EQ(0, sigaction(signum, nullptr, &cur_action));
  return cur_action.sa_handler != SIG_DFL;
}
} // anonymous namespace

TEST_F(DebugUtilTest, TestStackTraceInvalidTid) {
  string s = DumpThreadStack(1);
  ASSERT_STR_CONTAINS(s, "unable to deliver signal");
}

TEST_F(DebugUtilTest, TestStackTraceSelf) {
  string s = DumpThreadStack(Thread::CurrentThreadId());
  ASSERT_STR_CONTAINS(s, "kudu::DebugUtilTest_TestStackTraceSelf_Test::TestBody()");
}

TEST_F(DebugUtilTest, TestStackTraceMainThread) {
  string s = DumpThreadStack(getpid());
  ASSERT_STR_CONTAINS(s, "kudu::DebugUtilTest_TestStackTraceMainThread_Test::TestBody()");
}

TEST_F(DebugUtilTest, TestSignalStackTrace) {
  CountDownLatch l(1);
  scoped_refptr<Thread> t;
  ASSERT_OK(Thread::Create("test", "test thread", &SleeperThread, &l, &t));

  // We have to loop a little bit because it takes a little while for the thread
  // to start up and actually call our function.
  string stack;
  for (int i = 0; i < 10000; i++) {
    stack = DumpThreadStack(t->tid());
    if (stack.find("SleeperThread") != string::npos) break;
    SleepFor(MonoDelta::FromMicroseconds(100));
  }
  ASSERT_STR_CONTAINS(stack, "SleeperThread");

  // Test that we can change the signal and that the stack traces still work,
  // on the new signal.
  ASSERT_FALSE(IsSignalHandlerRegistered(SIGUSR1));
  ASSERT_OK(SetStackTraceSignal(SIGUSR1));

  // Should now be registered.
  ASSERT_TRUE(IsSignalHandlerRegistered(SIGUSR1));

  // SIGUSR2 should be relinquished.
  ASSERT_FALSE(IsSignalHandlerRegistered(SIGUSR2));

  // Stack traces should work using the new handler.
  ASSERT_STR_CONTAINS(DumpThreadStack(t->tid()), "SleeperThread");

  // Switch back to SIGUSR2 and ensure it changes back.
  ASSERT_OK(SetStackTraceSignal(SIGUSR2));
  ASSERT_TRUE(IsSignalHandlerRegistered(SIGUSR2));
  ASSERT_FALSE(IsSignalHandlerRegistered(SIGUSR1));

  // Stack traces should work using the new handler.
  ASSERT_STR_CONTAINS(DumpThreadStack(t->tid()), "SleeperThread");

  // Register our own signal handler on SIGUSR1, and ensure that
  // we get a bad Status if we try to use it.
  signal(SIGUSR1, &fake_signal_handler);
  ASSERT_STR_CONTAINS(SetStackTraceSignal(SIGUSR1).ToString(),
                      "unable to install signal handler");
  signal(SIGUSR1, SIG_IGN);

  // Stack traces should be disabled
  ASSERT_STR_CONTAINS(DumpThreadStack(t->tid()), "unable to take thread stack");

  // Re-enable so that other tests pass.
  ASSERT_OK(SetStackTraceSignal(SIGUSR2));

  // Allow the thread to finish.
  l.CountDown();
  t->Join();
}

// Test which dumps all known threads within this process.
// We don't validate the results in any way -- but this verifies that we can
// dump library threads such as the libc timer_thread and properly time out.
TEST_F(DebugUtilTest, TestDumpAllThreads) {
  vector<pid_t> tids;
  ASSERT_OK(ListThreads(&tids));
  for (pid_t tid : tids) {
    LOG(INFO) << DumpThreadStack(tid);
  }
}
#endif

} // namespace kudu
