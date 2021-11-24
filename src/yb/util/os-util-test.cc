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
#include <gtest/gtest.h>

#include "yb/util/os-util.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

using std::string;

namespace yb {

void RunTest(const string& name, int user_ticks, int kernel_ticks, int io_wait) {
  string buf = strings::Substitute(string("0 ($0) S 0 0 0 0 0 0 0") +
                                   " 0 0 0 $1 $2 0 0 0 0 0"         +
                                   " 0 0 0 0 0 0 0 0 0 0 "          +
                                   " 0 0 0 0 0 0 0 0 0 0 "          +
                                   " 0 $3 0 0 0 0 0 0 0 0 "         +
                                   " 0 0",
                                   name, user_ticks, kernel_ticks, io_wait);
  ThreadStats stats;
  string extracted_name;
  ASSERT_OK(ParseStat(buf, &extracted_name, &stats));
  ASSERT_EQ(name, extracted_name);
  ASSERT_EQ(user_ticks * (1e9 / sysconf(_SC_CLK_TCK)), stats.user_ns);
  ASSERT_EQ(kernel_ticks * (1e9 / sysconf(_SC_CLK_TCK)), stats.kernel_ns);
  ASSERT_EQ(io_wait * (1e9 / sysconf(_SC_CLK_TCK)), stats.iowait_ns);
}

TEST(OsUtilTest, TestSelf) {
  RunTest("test", 111, 222, 333);
}

TEST(OsUtilTest, TestSelfNameWithSpace) {
  RunTest("a space", 111, 222, 333);
}

TEST(OsUtilTest, TestSelfNameWithParens) {
  RunTest("a(b(c((d))e)", 111, 222, 333);
}

} // namespace yb
