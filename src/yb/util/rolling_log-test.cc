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

#include "yb/util/logging.h"
#include <glog/stl_logging.h>

#include "yb/gutil/strings/util.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/rolling_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using strings::Substitute;

namespace yb {

class RollingLogTest : public YBTest {
 public:
  RollingLogTest()
    : log_dir_(GetTestPath("log_dir")) {
  }

  void SetUp() override {
    ASSERT_OK(env_->CreateDir(log_dir_));
  }

 protected:
  void AssertLogCount(int expected_count, vector<string>* children) {
    vector<string> dir_entries;
    ASSERT_OK(env_->GetChildren(log_dir_, &dir_entries));
    children->clear();

    for (const string& child : dir_entries) {
      if (child == "." || child == "..") continue;
      children->push_back(child);
      ASSERT_TRUE(HasPrefixString(child, "rolling_log-test."));
      ASSERT_STR_CONTAINS(child, ".mylog.");

      string pid_suffix = Substitute("$0", getpid());
      ASSERT_TRUE(HasSuffixString(child, pid_suffix) ||
                  HasSuffixString(child, pid_suffix + ".gz")) << "bad child: " << child;
    }
    ASSERT_EQ(children->size(), expected_count) << *children;
  }

  const string log_dir_;
};

// Test with compression off.
TEST_F(RollingLogTest, TestLog) {
  RollingLog log(env_.get(), log_dir_, "mylog");
  log.SetCompressionEnabled(false);
  log.SetSizeLimitBytes(100);

  // Before writing anything, we shouldn't open a log file.
  vector<string> children;
  ASSERT_NO_FATALS(AssertLogCount(0, &children));

  // Appending some data should write a new segment.
  ASSERT_OK(log.Append("Hello world\n"));
  ASSERT_NO_FATALS(AssertLogCount(1, &children));

  for (int i = 0; i < 10; i++) {
    ASSERT_OK(log.Append("Hello world\n"));
  }
  ASSERT_NO_FATALS(AssertLogCount(2, &children));

  faststring data;
  string path = JoinPathSegments(log_dir_, children[0]);
  ASSERT_OK(ReadFileToString(env_.get(), path, &data));
  ASSERT_TRUE(HasPrefixString(data.ToString(), "Hello world\n"))
    << "Data missing";
  ASSERT_LE(data.size(), 100) << "Size limit not respected";
}

// Test with compression on.
TEST_F(RollingLogTest, TestCompression) {
  RollingLog log(env_.get(), log_dir_, "mylog");
  ASSERT_OK(log.Open());

  GStringPiece data = "Hello world\n";
  int raw_size = 0;
  for (int i = 0; i < 1000; i++) {
    ASSERT_OK(log.Append(data));
    raw_size += data.size();
  }
  ASSERT_OK(log.Close());

  vector<string> children;
  ASSERT_NO_FATALS(AssertLogCount(1, &children));
  ASSERT_TRUE(HasSuffixString(children[0], ".gz"));

  // Ensure that the output is actually gzipped.
  uint64_t size = ASSERT_RESULT(env_->GetFileSize(JoinPathSegments(log_dir_, children[0])));
  ASSERT_LT(size, raw_size / 10);
  ASSERT_GT(size, 0);
}

} // namespace yb
