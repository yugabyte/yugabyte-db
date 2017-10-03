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

#include "kudu/util/pstack_watcher.h"

#include <gtest/gtest.h>
#include <memory>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/bitmap.h"
#include "kudu/util/env.h"
#include "kudu/util/errno.h"
#include "kudu/util/test_macros.h"

using std::shared_ptr;
using std::string;
using strings::Substitute;

namespace kudu {

TEST(TestPstackWatcher, TestPstackWatcherCancellation) {
  PstackWatcher watcher(MonoDelta::FromSeconds(1000000));
  watcher.Shutdown();
}

TEST(TestPstackWatcher, TestWait) {
  PstackWatcher watcher(MonoDelta::FromMilliseconds(10));
  watcher.Wait();
}

TEST(TestPstackWatcher, TestDumpStacks) {
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

TEST(TestPstackWatcher, TestPstackWatcherRunning) {
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

} // namespace kudu
