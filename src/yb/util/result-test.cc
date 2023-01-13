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

Result<std::unique_ptr<int>> ReturnVariable() {
  std::unique_ptr<int> result;
  result = std::make_unique<int>(42);
  return result;
}

// Test that we could return non-copyable variable of type T from function with return type
// Result<T>.
TEST_F(ResultTest, ReturnVariable) {
  auto ptr = ASSERT_RESULT(ReturnVariable());
  ASSERT_EQ(42, *ptr);
}

Result<std::unique_ptr<int>> ReturnBadVariable() {
  return STATUS(RuntimeError, "Just for test");
}

Status test_function(int* num_func_calls, std::unique_ptr<int> ptr) {
  ++(*CHECK_NOTNULL(num_func_calls));
  LOG(INFO) << "Called test_function() (call number " << *num_func_calls << ") with arg=" << *ptr;
  EXPECT_EQ(42, *ptr);
  return *num_func_calls == 1 ?
      Status::OK() : STATUS(InternalError, "test_function() returned a error");
}

template<typename F, typename GetResult>
Status GetStatus(int* num_func_calls, F f, GetResult get_result) {
  RETURN_NOT_OK(f(num_func_calls, VERIFY_RESULT(get_result())));
  return Status::OK();
}

TEST_F(ResultTest, VerifyResultMacro) {
  int num_func_calls = 0;
  // Good way - no returns from the macros.
  Status s  = GetStatus(&num_func_calls, test_function, ReturnVariable);
  LOG(INFO) << "GetStatus() returned status=" << s;
  ASSERT_EQ(1, num_func_calls);
  ASSERT_TRUE(s.ok());

  // Bad way 1 - exit from VERIFY_RESULT().
  s  = GetStatus(&num_func_calls, test_function, ReturnBadVariable);
  LOG(INFO) << "GetStatus() returned status=" << s;
  ASSERT_EQ(1, num_func_calls);
  ASSERT_TRUE(s.IsRuntimeError());

  // Bad way 2 - exit from RETURN_NOT_OK().
  s  = GetStatus(&num_func_calls, test_function, ReturnVariable);
  LOG(INFO) << "GetStatus() returned status=" << s;
  ASSERT_EQ(2, num_func_calls);
  ASSERT_TRUE(s.IsInternalError());
}

struct NonCopyableNonMovable {
  NonCopyableNonMovable() = default;
  NonCopyableNonMovable(const NonCopyableNonMovable&) = delete;
  NonCopyableNonMovable& operator=(const NonCopyableNonMovable&) = delete;
};

class MoveCounter {
 public:
  MoveCounter() = default;
  MoveCounter(const MoveCounter&) = delete;

  MoveCounter(const MoveCounter&&) {
    ++counter_;
  }

  MoveCounter& operator=(const MoveCounter&) = delete;

  MoveCounter& operator=(MoveCounter&&) {
    ++counter_;
    return *this;
  }

  static size_t counter() {
    return counter_;
  }

 private:
  static size_t counter_;
};

size_t MoveCounter::counter_ = 0;

// Next function won't compile in case VERIFY_RESULT will try to create copy of Result's data
Status VerifyResultMacroReferenceNoCopyHelper() {
  static const NonCopyableNonMovable data;
  const auto& r ATTRIBUTE_UNUSED = VERIFY_RESULT_REF(Result<const NonCopyableNonMovable&>(data));
  return Status::OK();
}

Status VerifyResultMacroMoveCountHelper() {
  const auto result ATTRIBUTE_UNUSED = VERIFY_RESULT(Result<MoveCounter>(MoveCounter()));
  return Status::OK();
}

TEST_F(ResultTest, VerifyResultMacroReferenceNoCopy) {
  ASSERT_OK(VerifyResultMacroReferenceNoCopyHelper());
}

TEST_F(ResultTest, VerifyResultMacroMoveCount) {
  ASSERT_OK(VerifyResultMacroMoveCountHelper());
  ASSERT_EQ(2, MoveCounter::counter());
}

namespace {

template<typename T>
void TestNotOk(T t) {
  const auto LogPrefix = []() -> std::string { return "prefix"; };
  WARN_NOT_OK(t, "boo");
  WARN_WITH_PREFIX_NOT_OK(t, "foo");
  ERROR_NOT_OK(t, "moo");
}

} // namespace

TEST_F(ResultTest, NotOk) {
  Result<std::string> good_result = "good";
  Result<std::string> bad_result = STATUS(InternalError, "bad");

  LOG(INFO) << "Checking OK because it's mandatory: " << bad_result.ok();

  // Result
  TestNotOk<Result<std::string>>(good_result);
  TestNotOk<Result<std::string>>(bad_result);

  // Status
  TestNotOk<Status>(Status::OK());
  TestNotOk<Status>(bad_result.status());
}

} // namespace test
} // namespace yb
