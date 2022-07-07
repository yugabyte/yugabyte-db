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
#include <stdio.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/env.h"
#include "yb/util/errno.h"
#include "yb/util/pstack_watcher.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"

using std::shared_ptr;
using std::string;
using strings::Substitute;

namespace yb {

TEST(TestPstackWatcher, TestPstackWatcherCancellation) {
  PstackWatcher watcher(MonoDelta::FromSeconds(1000000));
  watcher.Shutdown();
}

TEST(TestPstackWatcher, TestWait) {
  PstackWatcher watcher(MonoDelta::FromMilliseconds(10));
  watcher.Wait();
}

// Disable TestDumpStacks and TestPstackWatcherRunning on macOS because neither gdb or lldb ways
// of obtaining stack traces there appear to work. gdb requires codesigning, which is a manual
// procedure we have to go through on every build host, and it is not high priority as of 12/2018.
TEST(TestPstackWatcher, YB_DISABLE_TEST_ON_MACOS(TestDumpStacks)) {
  ASSERT_OK(PstackWatcher::DumpStacks());
}

static shared_ptr<FILE> RedirectStdout(string *temp_path) {
  string temp_dir;
  CHECK_OK(Env::Default()->GetTestDirectory(&temp_dir));
  *temp_path = Substitute("$0/pstack_watcher-dump.$1.txt",
                      temp_dir, getpid());
  return shared_ptr<FILE>(
      freopen(temp_path->c_str(), "w", stdout), fclose);
}

TEST(TestPstackWatcher, YB_DISABLE_TEST_ON_MACOS(TestPstackWatcherRunning)) {
  string stdout_file;
  int old_stdout;
  CHECK_ERR(old_stdout = dup(STDOUT_FILENO));
  {
    shared_ptr<FILE> out_fp = RedirectStdout(&stdout_file);
    PCHECK(out_fp.get());
    PstackWatcher watcher(MonoDelta::FromMilliseconds(500));
    while (watcher.IsRunning()) {
      SleepFor(MonoDelta::FromMilliseconds(1));
    }
  }
  CHECK_ERR(dup2(old_stdout, STDOUT_FILENO));
  PCHECK(stdout = fdopen(STDOUT_FILENO, "w"));

  faststring contents;
  CHECK_OK(ReadFileToString(Env::Default(), stdout_file, &contents));
  ASSERT_STR_CONTAINS(contents.ToString(), "BEGIN STACKS");
  CHECK_ERR(unlink(stdout_file.c_str()));
  ASSERT_GE(fprintf(stdout, "%s\n", contents.ToString().c_str()), 0)
      << "errno=" << errno << ": " << ErrnoToString(errno);
}

} // namespace yb
