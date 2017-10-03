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

#include "kudu/twitter-demo/parser.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "kudu/gutil/strings/split.h"
#include "kudu/util/env.h"
#include "kudu/util/path_util.h"
#include "kudu/util/test_util.h"
#include "kudu/util/status.h"

namespace kudu {
namespace twitter_demo {

// Return the directory of the currently-running executable.
static string GetExecutableDir() {
  string exec;
  CHECK_OK(Env::Default()->GetExecutablePath(&exec));
  return DirName(exec);
}

static Status LoadFile(const string& name, vector<string>* lines) {
  string path = JoinPathSegments(GetExecutableDir(), name);
  faststring data;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), path, &data));

  *lines = strings::Split(data.ToString(), "\n");
  return Status::OK();
}

static void EnsureFileParses(const char* file, TwitterEventType expected_type) {
  TwitterEventParser p;
  TwitterEvent event;

  SCOPED_TRACE(file);
  vector<string> jsons;
  CHECK_OK(LoadFile(file, &jsons));

  int line_number = 1;
  for (const string& json : jsons) {
    if (json.empty()) continue;
    SCOPED_TRACE(json);
    SCOPED_TRACE(line_number);
    ASSERT_OK(p.Parse(json, &event));
    ASSERT_EQ(expected_type, event.type);
    line_number++;
  }
}

// example-tweets.txt includes a few hundred tweets collected
// from the sample hose.
TEST(ParserTest, TestParseTweets) {
  EnsureFileParses("example-tweets.txt", TWEET);
}

// example-deletes.txt includes a few hundred deletes collected
// from the sample hose.
TEST(ParserTest, TestParseDeletes) {
  EnsureFileParses("example-deletes.txt", DELETE_TWEET);
}

TEST(ParserTest, TestReformatTime) {
  ASSERT_EQ("20130814063107", TwitterEventParser::ReformatTime("Wed Aug 14 06:31:07 +0000 2013"));
}

} // namespace twitter_demo
} // namespace kudu
