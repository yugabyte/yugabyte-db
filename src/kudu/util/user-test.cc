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

#include <string>

#include <gtest/gtest.h>

#include "kudu/util/status.h"
#include "kudu/util/test_util.h"
#include "kudu/util/user.h"

namespace kudu {

using std::string;

class TestUser : public KuduTest {
};

// Validate that the current username is non-empty.
TEST_F(TestUser, TestNonEmpty) {
  string username;
  ASSERT_TRUE(username.empty());
  ASSERT_OK(GetLoggedInUser(&username));
  ASSERT_FALSE(username.empty());
  LOG(INFO) << "Name of the current user is: " << username;
}

} // namespace kudu
