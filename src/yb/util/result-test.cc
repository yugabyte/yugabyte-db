//
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
//

#include "yb/util/result.h"
#include "yb/util/test_util.h"

namespace yb {
namespace test {

class ResultTest : public YBTest {
};

namespace {

template<class TValue>
void CheckResultIsStatus(const yb::Result<TValue>& result) {
  ASSERT_FALSE(result.ok());
  ASSERT_FALSE(result);
  ASSERT_TRUE(!result);
  ASSERT_TRUE(result.status().IsRuntimeError());
}

void CheckResultIsStatus(const yb::Result<bool>& result) {
  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.status().IsRuntimeError());
}

template<class TValue>
void CheckResultIsValue(const yb::Result<TValue>& result, const TValue& value) {
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result ? true : false);
  ASSERT_FALSE(!result);
  CHECK_EQ(*result, value);
}

void CheckResultIsValue(const yb::Result<bool>& result, bool value) {
  ASSERT_TRUE(result.ok());
  CHECK_EQ(result.get(), value);
}

template<class TValue, class Checker>
void CheckFromResult(const yb::Result<TValue>& result, const Checker& checker) {
  typedef yb::Result<TValue> Result;

  Result result3(result); // Copy from result
  checker(result3);

  Result result4(std::move(result)); // Move from result
  checker(result4);

  result3 = result4; // Assign from result
  checker(result3);

  result3 = std::move(result4); // Move assign from result
  checker(result3);
}

template<class TValue>
void CheckStatus() {
  typedef yb::Result<TValue> Result;
  Status status = STATUS(RuntimeError, "");
  Status status2 = status;
  Status status3 = status;

  Result result(status); // Copy from status
  CheckResultIsStatus(result);

  Result result2(std::move(status2)); // Move from status
  CheckResultIsStatus(result2);

  result = status; // Assign from status
  CheckResultIsStatus(result);

  result = std::move(status3); // Move assign from status
  CheckResultIsStatus(result);

  CheckFromResult(result, [](const auto& result) { CheckResultIsStatus(result); });
}

template<class TValue>
void CheckValue(TValue value) {
  typedef yb::Result<TValue> Result;

  static_assert(sizeof(Result) == sizeof(void*) + std::max(sizeof(TValue), sizeof(void*)),
                "Invalid Result size");

  auto value2 = value;
  auto value3 = value;

  Result result(value); // Copy from value
  CheckResultIsValue(result, value);

  Result result2(std::move(value2)); // Move from value
  CheckResultIsValue(result2, value);

  result = value; // Assign from value
  CheckResultIsValue(result, value);

  result = std::move(value3); // Move assign from value
  CheckResultIsValue(result, value);

  CheckFromResult(result, [value](const auto& res) { CheckResultIsValue(res, value); });
}

Result<std::string&> GetStringReference() {
  static std::string result = "shared string";
  return result;
}

Result<std::string&> GetBadStringReference() {
  return STATUS(RuntimeError, "Just for test");
}

Result<const std::string&> GetConstStringReference() {
  return GetStringReference();
}

} // namespace

TEST_F(ResultTest, Status) {
  CheckStatus<int>();
  CheckStatus<std::string>();
  CheckStatus<bool>();
}

TEST_F(ResultTest, Result) {
  const std::string kString = "Hello World!";
  CheckValue(42);
  CheckValue(std::string(kString));
  CheckValue(true);
  yb::Result<std::string> result(kString);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->size(), kString.size()); // Check of operator->
}

TEST_F(ResultTest, NonCopyable) {
  Result<std::unique_ptr<int>> ptr = std::make_unique<int>(42);
  Result<std::unique_ptr<int>> ptr2(std::move(ptr));
  ASSERT_OK(ptr2);

  std::unique_ptr<int>& unique = *ptr2;
  ASSERT_EQ(42, *unique);
}

TEST_F(ResultTest, Reference) {
  LOG(INFO) << GetStringReference();
  LOG(INFO) << GetConstStringReference();
  auto result = GetStringReference();
  auto const_result = GetConstStringReference();

  static_assert(sizeof(result) == 2 * sizeof(void*), "Invalid Result size");

  ASSERT_OK(result);
  Result<std::string&> result2(result);
  const_result = result;
  const_result = std::move(result);
  result = GetBadStringReference();
  ASSERT_NOK(result);
  auto status = std::move(result.status());
  ASSERT_NOK(status);
  result = GetStringReference();
  ASSERT_OK(result);
}

} // namespace test
} // namespace yb
