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

#include <stdlib.h>

#include <string>

#include <gtest/gtest.h>

#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using namespace std::literals;

namespace yb {

class SubprocessTest : public YBTest {};

TEST_F(SubprocessTest, TestSimplePipe) {
  vector<string> argv;
  argv.push_back("tr");
  argv.push_back("a-z");
  argv.push_back("A-Z");
  Subprocess p("/usr/bin/tr", argv);
  p.PipeParentStdout();
  ASSERT_OK(p.Start());

  FILE* out = fdopen(p.ReleaseChildStdinFd(), "w");
  PCHECK(out);
  FILE* in = fdopen(p.from_child_stdout_fd(), "r");
  PCHECK(in);

  fprintf(out, "hello world\n");
  // We have to close 'out' or else tr won't write any output, since
  // it enters a buffered mode if it detects that its input is a FIFO.
  fclose(out);

  char buf[1024];
  ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
  ASSERT_STREQ("HELLO WORLD\n", &buf[0]);

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

TEST_F(SubprocessTest, TestErrPipe) {
  vector<string> argv;
  argv.push_back("tee");
  argv.push_back("/dev/stderr");
  Subprocess p("/usr/bin/tee", argv);
  p.PipeParentStderr();
  ASSERT_OK(p.Start());

  FILE* out = fdopen(p.ReleaseChildStdinFd(), "w");
  PCHECK(out);

  fprintf(out, "Hello, World\n");
  fclose(out); // same reasoning as above, flush to prevent tee buffering

  FILE* in = fdopen(p.from_child_stderr_fd(), "r");
  PCHECK(in);

  char buf[1024];
  ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
  ASSERT_STREQ("Hello, World\n", &buf[0]);

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

TEST_F(SubprocessTest, TestOutPipe) {
  vector<string> argv;
  argv.push_back("tee");
  argv.push_back("/dev/stdout");
  Subprocess p("/usr/bin/tee", argv);
  p.PipeParentStdout();
  ASSERT_OK(p.Start());

  FILE* out = fdopen(p.ReleaseChildStdinFd(), "w");
  PCHECK(out);

  fprintf(out, "Hello, World (stdout)\n");
  fclose(out); // same reasoning as above, flush to prevent tee buffering

  FILE* in = fdopen(p.from_child_stdout_fd(), "r");
  PCHECK(in);

  char buf[1024];
  ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
  ASSERT_STREQ("Hello, World (stdout)\n", &buf[0]);

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

TEST_F(SubprocessTest, TestKill) {
  vector<string> argv;
  argv.push_back("cat");
  Subprocess p("/bin/cat", argv);
  ASSERT_OK(p.Start());

  ASSERT_OK(p.Kill(SIGKILL));

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_EQ(SIGKILL, WTERMSIG(wait_status));

  // Test that calling Wait() a second time returns the same
  // cached value instead of trying to wait on some other process
  // that was assigned the same pid.
  wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_EQ(SIGKILL, WTERMSIG(wait_status));
}

TEST_F(SubprocessTest, TestEnvVars) {
  auto kEnvVarFromParentName = "YB_VAR_FROM_PARENT_PROCESS"s;
  auto kEnvVarFromParentValue = "value_from_parent_process"s;
  auto kEnvVarForChildOnlyName = "YB_VAR_SET_FOR_CHILD_ONLY"s;
  auto kEnvVarForChildOnlyValue = "value_set_for_child_only"s;

  setenv(kEnvVarFromParentName.c_str(), kEnvVarFromParentValue.c_str(), /* replace= */ true);
  vector<string> argv {"env" };
  Subprocess p("env", argv);
  p.PipeParentStdout();
  p.SetEnv(kEnvVarForChildOnlyName, kEnvVarForChildOnlyValue);
  ASSERT_OK(p.Start());

  FILE* in = fdopen(p.from_child_stdout_fd(), "r");
  PCHECK(in);

  char buf[1024];
  std::map<std::string, std::string> subprocess_env;
  while (fgets(buf, sizeof(buf), in) != nullptr) {
    const char* equal_sign_ptr = strchr(buf, '=');
    if (equal_sign_ptr != nullptr) {
      size_t name_length = equal_sign_ptr - buf;
      subprocess_env[std::string(buf, name_length)] =
          std::string(equal_sign_ptr + 1, strlen(buf) - name_length - 1);
    }
  }

  auto it = subprocess_env.find(kEnvVarFromParentName);
  ASSERT_TRUE(it != subprocess_env.end());
  ASSERT_EQ(kEnvVarFromParentValue + "\n", it->second);

  it = subprocess_env.find(kEnvVarForChildOnlyName);
  ASSERT_TRUE(it != subprocess_env.end());
  ASSERT_EQ(kEnvVarForChildOnlyValue + "\n", it->second);

  int wait_status = 0;
  ASSERT_OK(p.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

} // namespace yb
