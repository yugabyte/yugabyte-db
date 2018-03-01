// Copyright (c) YugaByte, Inc.
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

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "yb/client/client.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"

#include "yb/util/test_util.h"

using namespace std::literals; // NOLINT

namespace yb {
namespace pgserver {

class TestPgServer : public YBTest {
 public:
  TestPgServer() {
  }
  virtual ~TestPgServer() {
  }

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  virtual void SetUp() override {
    YBTest::SetUp();
  }
  virtual void TearDown() override {
    YBTest::TearDown();
  }

 private:
};

TEST_F(TestPgServer, TestPgEndToEnd) {
}

}  // namespace pgserver
}  // namespace yb
