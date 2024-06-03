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

#include <signal.h>
#include <chrono>
#include <thread>

#include "yb/util/logging.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace integration_tests {

/**
 * This class tests different kind of failures.
 */
class TestFailuresTest: public YBTest {
};

class CustomListener: public ::testing::EmptyTestEventListener {
 public:
  enum class Type {
    RAISE_SIGSEGV,
    RETURN_EXIT_CODE_1
  };

  explicit CustomListener(const Type &type) : type_(type) {}

  void OnTestProgramEnd(const testing::UnitTest &test) override {
    switch (type_) {
      case Type::RAISE_SIGSEGV:
        raise(SIGSEGV);
        break;
      case Type::RETURN_EXIT_CODE_1:
        exit(1);
        break;
    }
  }

 private:
  Type type_;
};

TEST_F(TestFailuresTest, Exit1WithoutXml) {
  // Exit before gtest writes XML for testing test results reporting.
  exit(1);
}

TEST_F(TestFailuresTest, SigSegvWithoutXml) {
  // Raise SIGSEGV before gtest writes XML for testing test results reporting.
  raise(SIGSEGV);
}

TEST_F(TestFailuresTest, Exit1WithPassXml) {
  // Make gtest produce XML with PASS result, but return non-zero exit code.
  auto &listeners = ::testing::UnitTest::GetInstance()->listeners();
  listeners.Append(new CustomListener(CustomListener::Type::RETURN_EXIT_CODE_1));
}

TEST_F(TestFailuresTest, SigSegvWithPassXml) {
  // Make gtest produce XML with PASS result and after that terminate due to SIGSEGV.
  ::testing::UnitTest::GetInstance()->listeners().Append(new CustomListener(
      CustomListener::Type::RAISE_SIGSEGV));
}

TEST_F(TestFailuresTest, AsanFailure) {
  // Force memory leak.
  std::string* const s = new std::string();
  s->append("TEST");
}

} // namespace integration_tests
} // namespace yb
