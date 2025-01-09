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
#pragma once

#include <set>
#include <sstream>
#include <string>

#include <boost/preprocessor/cat.hpp>

#include <gtest/gtest.h> // For SUCCEED/FAIL

#include "yb/gutil/stl_util.h"  // For VectorToSet

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/string_trim.h"
#include "yb/util/tostring.h"

namespace yb {
namespace util {

template<typename T>
std::string TEST_SetDifferenceStr(const std::set<T>& expected, const std::set<T>& actual) {
  std::set<T> only_in_expected, only_in_actual;
  for (const auto& expected_item : expected) {
    if (!actual.count(expected_item)) {
      only_in_expected.insert(expected_item);
    }
  }

  for (const auto& actual_item : actual) {
    if (!expected.count(actual_item)) {
      only_in_actual.insert(actual_item);
    }
  }

  std::ostringstream result;
  if (!only_in_expected.empty()) {
    result << "only in the expected set: " << yb::ToString(only_in_expected);
  }
  if (!only_in_actual.empty()) {
    if (result.tellp() > 0) {
      result << "; ";
    }
    result << "only in the actual set: " << yb::ToString(only_in_actual);
  }
  if (result.tellp() == 0) {
    return "no differences";
  }
  return result.str();
}

}  // namespace util
}  // namespace yb

// ASSERT_NO_FATAL_FAILURE is just too long to type.
#define NO_FATALS(expr) \
  ASSERT_NO_FATAL_FAILURE(expr)

// Detect fatals in the surrounding scope. NO_FATALS() only checks for fatals
// in the expression passed to it.
#define NO_PENDING_FATALS() \
  if (testing::Test::HasFatalFailure()) { return; }

// ASSERT_NO_FATAL_FAILURE is just too long to type.
#define ASSERT_NO_FATALS ASSERT_NO_FATAL_FAILURE

// We are using "const auto" for storing the status so that the same macros work for both YB and
// RocksDB's Status types.

#define ASSERT_OK(status) do { \
    auto&& _assert_status = (status); \
    if (_assert_status.ok()) { \
      SUCCEED(); \
    } else { \
      FAIL() << "Bad status: " << StatusToString(_assert_status);  \
    } \
  } while (0)

#define ASSERT_NOK(s) ASSERT_FALSE((s).ok())

#define ASSERT_OK_PREPEND(status, msg) do { \
  auto&& _assert_status = (status); \
  if (_assert_status.ok()) { \
    SUCCEED(); \
  } else { \
    FAIL() << (msg) << " - status: " << StatusToString(_assert_status);  \
  } \
} while (0)

#ifdef EXPECT_OK
#undef EXPECT_OK
#endif

#define EXPECT_OK(status) do { \
    auto&& _s = (status); \
    if (_s.ok()) { \
      SUCCEED(); \
    } else { \
      ADD_FAILURE() << "Bad status: " << StatusToString(_s);  \
    } \
  } while (0)

#define EXPECT_NOK(s) EXPECT_FALSE((s).ok())

// Like the above, but doesn't record successful
// tests.
#define ASSERT_OK_FAST(status) do {      \
    auto&& _assert_status = (status); \
    if (!_assert_status.ok()) { \
      FAIL() << "Bad status: " << StatusToString(_assert_status);  \
    } \
  } while (0)

#define ASSERT_NO_ERROR(ec) \
  do { \
    auto&& _ec = (ec); \
    if (!_ec) { \
      SUCCEED(); \
    } else { \
      FAIL() << "Unexpected error: " << ec.message(); \
    } \
  } while (false)

#define EXPECT_NO_ERROR(ec) \
  do { \
    auto&& _ec = (ec); \
    if (!_ec) { \
      SUCCEED(); \
    } else { \
      ADD_FAILURE() << "Unexpected error: " << ec.message(); \
    } \
  } while (false)

// Asserts that result is ok, extracts result value is case of success.
#define ASSERT_RESULT(expr) \
  RESULT_CHECKER_HELPER(expr, ASSERT_OK(__result))

// Expects that result is ok, extracts result value is case of success.
#define EXPECT_RESULT(expr) \
  RESULT_CHECKER_HELPER(expr, EXPECT_OK(__result))

// Asserts that result is ok, extracts result value is case of success.
#define ASSERT_RESULT_FAST(expr) \
  RESULT_CHECKER_HELPER(expr, ASSERT_OK_FAST(__result))

#ifdef THREAD_SANITIZER
#define ASSERT_PERF_LE(lhs, rhs) do { (void)(lhs); (void)(rhs); } while(false)
#define EXPECT_PERF_LE(lhs, rhs) do { (void)(lhs); (void)(rhs); } while(false)
#else
#define ASSERT_PERF_LE(lhs, rhs) ASSERT_LE(lhs, rhs)
#define EXPECT_PERF_LE(lhs, rhs) EXPECT_LE(lhs, rhs)
#endif

#define ASSERT_STR_CONTAINS(str, substr) do { \
  std::string _s = (str); \
  if (_s.find((substr)) == std::string::npos) { \
    FAIL() << "Expected to find substring '" << (substr) \
    << "'. Got: '" << _s << "'"; \
  } \
  } while (0)

#define ASSERT_STR_NOT_CONTAINS(str, substr) do { \
  std::string _s = (str); \
  if (_s.find((substr)) != std::string::npos) { \
    FAIL() << "Expected not to find substring '" << (substr) \
    << "'. Got: '" << _s << "'"; \
  } \
  } while (0)

inline std::string FindFirstDiff(const std::string& lhs, const std::string& rhs) {
  size_t min_len = std::min(lhs.size(), rhs.size());
  size_t i = 0;
  for (; i != min_len; ++i) {
    if (lhs[i] != rhs[i]) {
      break;
    }
  }
  return lhs.substr(i, std::min<size_t>(lhs.size() - i, 32)) + " vs " +
         rhs.substr(i, std::min<size_t>(rhs.size() - i, 32));
}

#define ASSERT_STR_EQ(lhs, rhs) do { \
    std::string _lhs = (lhs); \
    std::string _rhs = (rhs); \
    ASSERT_EQ(lhs, rhs) << "First diff: " << FindFirstDiff(lhs, rhs); \
  } while (0)

#define ASSERT_FILE_EXISTS(env, path) do { \
  std::string _s = (path); \
  ASSERT_TRUE(env->FileExists(_s)) \
    << "Expected file to exist: " << _s; \
  } while (0)

#define ASSERT_FILE_NOT_EXISTS(env, path) do { \
  std::string _s = (path); \
  ASSERT_FALSE(env->FileExists(_s)) \
    << "Expected file not to exist: " << _s; \
  } while (0)

// Wrappers around ASSERT_EQ and EXPECT_EQ that trim expected and actual strings and outputs
// expected and actual values without any escaping. We're also printing a stack trace to allow
// easier debugging.
#define _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_SETUP(expected, actual) \
    const auto _expected_tmp = ::yb::util::TrimStr(yb::util::LeftShiftTextBlock(expected)); \
    const auto _actual_tmp = ::yb::util::TrimStr(yb::util::LeftShiftTextBlock(actual));

#define _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_MSG \
    "\nActual (trimmed):\n" << _actual_tmp \
        << "\n\nExpected (trimmed):\n" << _expected_tmp;

#define ASSERT_STR_EQ_VERBOSE_TRIMMED(expected, actual) \
  do { \
    _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_SETUP(expected, actual) \
    ASSERT_EQ(_expected_tmp, _actual_tmp) << _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_MSG; \
  } while(0)

#define ASSERT_SETS_EQ(expected_set, actual_set) \
  do { \
    auto&& _expected_set_computed = (expected_set); \
    auto&& _actual_set_computed = (actual_set); \
    if (_expected_set_computed != _actual_set_computed) { \
      FAIL() << "Expected " \
             << BOOST_PP_STRINGIZE(actual_set) << " to be equal to " \
             << BOOST_PP_STRINGIZE(expected_set) << ". Differences: " \
             << ::yb::util::TEST_SetDifferenceStr(_expected_set_computed, _actual_set_computed); \
    } \
  } while (0)

// Compare vectors of "comparable" values that can be put inside an std::set. Because they are
// comparable, we also show the differences between the set of elements in vectors, which is
// sometimes the only way to see what is different.
//
// Using a simple name ASSERT_VECTORS_EQ for this macro rather than a more verbose and precise but
// more confusing such as ASSERT_VECTORS_OF_COMPARABLE_EQ.
//
// Google Test says "do not use this in your code" about GTEST_PRED_FORMAT2_, but we need to use it
// to correctly propagate the raw text of expressions to the error message. We pass the string
// representation of the actual macro parameters but the ..._computed values as values (to avoid
// multiple evaluations).
//
// Here are macros from gtest that were used to construct the implementation below:
//
// ASSERT_EQ -> GTEST_ASSERT_EQ -> ASSERT_PRED_FORMAT2 -> GTEST_PRED_FORMAT2_ -> GTEST_ASSERT_
#define ASSERT_VECTORS_EQ(expected_vector, actual_vector) \
  do { \
    auto&& _expected_vector_computed = (expected_vector); \
    auto&& _actual_vector_computed = (actual_vector); \
    auto expected_set = ::yb::VectorToSet(_expected_vector_computed); \
    auto actual_set = ::yb::VectorToSet(_actual_vector_computed); \
    GTEST_ASSERT_( \
        ::testing::internal::EqHelper::Compare( \
            BOOST_PP_STRINGIZE(expected_vector), \
            BOOST_PP_STRINGIZE(actual_vector), \
            _expected_vector_computed, \
            _actual_vector_computed), \
        GTEST_FATAL_FAILURE_) \
        << "Differences (as sets): " \
        << ::yb::util::TEST_SetDifferenceStr(expected_set, actual_set); \
  } while (0)

// A wrapper around EXPECT_EQ that trims expected and actual strings and outputs expected and actual
// values without any escaping.
#define EXPECT_STR_EQ_VERBOSE_TRIMMED(expected, actual) \
  do { \
    _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_SETUP(expected, actual) \
    EXPECT_EQ(_expected_tmp, _actual_tmp) << _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_MSG; \
  } while(0)

#define YB_ASSERT_TRUE(condition) \
  GTEST_TEST_BOOLEAN_((condition) ? true : false, #condition, false, true, \
                      GTEST_FATAL_FAILURE_)

#define VERIFY_EQ(expected_expr, actual_expr) \
  do { \
    auto&& _expected = (expected_expr); \
    auto&& _actual = (actual_expr); \
    if (_expected != _actual) { \
      return ::testing::internal::EqFailure( \
          BOOST_PP_STRINGIZE(expected_expr), \
          BOOST_PP_STRINGIZE(actual_expr), \
          ::testing::internal::FormatForComparisonFailureMessage(_expected, _actual), \
          ::testing::internal::FormatForComparisonFailureMessage(_actual, _expected), \
          false); \
    } \
  } while (false) \
  /**/

#define ASSERT_VERIFY(expr) \
  do { \
    auto&& _result = (expr); \
    if (!_result) { \
      FAIL() << _result.message(); \
    } \
  } while (false) \
  /**/

// Asserts that expr is not null, returns expr in case of success.
#define ASSERT_NOTNULL(expr) \
  __extension__ ({ \
    auto&& _result = (expr); \
    if (_result == nullptr) { \
      FAIL() << "Unexpected nullptr"; \
    } \
    std::move(_result); \
  }) \
  /**/

// Similar to ASSERT_NOTNULL but does not return anything.
#define ASSERT_ONLY_NOTNULL(expr) \
  do { \
    auto&& _result = (expr); \
    if (_result == nullptr) { \
      FAIL() << "Unexpected nullptr"; \
    } \
  } while (false)
  /**/

#define ASSERT_NOK_PG_ERROR_CODE(expr, pg_error_code) \
  do { \
    auto&& _status = (expr); \
    ASSERT_NOK(_status); \
    ASSERT_EQ(PgsqlError(_status), pg_error_code); \
  } while (false)

#define ASSERT_NOK_STR_CONTAINS(expr, expected_failure_substr) \
  do { \
    auto&& _result = (expr); \
    ASSERT_NOK(_result); \
    ASSERT_STR_CONTAINS(StatusToString(_result), expected_failure_substr); \
  } while (false)

#define CURRENT_TEST_NAME() \
  ::testing::UnitTest::GetInstance()->current_test_info()->name()

#define CURRENT_TEST_CASE_NAME() \
  ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name()

#define CURRENT_TEST_CASE_AND_TEST_NAME_STR() \
  (std::string(CURRENT_TEST_CASE_NAME()) + '.' + CURRENT_TEST_NAME())

// Macros to disable tests in certain build types. Cannot be used in a parameterized test with
// TEST_P or extended test fixtures with TEST_F_EX. For these, please use GTEST_SKIP or
// YB_SKIP_TEST_IN_TSAN macros.

#define YB_DISABLE_TEST(test_name) BOOST_PP_CAT(DISABLED_, test_name)

#ifdef __APPLE__
#define YB_DISABLE_TEST_ON_MACOS(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_ON_MACOS(test_name) test_name
#endif

#ifdef THREAD_SANITIZER
#define YB_DISABLE_TEST_IN_TSAN(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_IN_TSAN(test_name) test_name
#endif

#ifdef ADDRESS_SANITIZER
#define YB_DISABLE_TEST_IN_ASAN(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_IN_ASAN(test_name) test_name
#endif

#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
#define YB_DISABLE_TEST_IN_SANITIZERS(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_IN_SANITIZERS(test_name) test_name
#endif

#ifdef FASTDEBUG
#define YB_DISABLE_TEST_IN_FASTDEBUG(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_IN_FASTDEBUG(test_name) test_name
#endif

#if defined(__APPLE__) || defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
#define YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(test_name) test_name
#endif

#if !defined(NDEBUG) || defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
#define YB_DISABLE_TEST_EXCEPT_RELEASE(test_name) YB_DISABLE_TEST(test_name)
#else
#define YB_DISABLE_TEST_EXCEPT_RELEASE(test_name) test_name
#endif

#ifdef __linux__
#define YB_LINUX_ONLY_TEST(test_name) test_name
#else
#define YB_LINUX_ONLY_TEST(test_name) YB_DISABLE_TEST(test_name)
#endif

// Can be used in individual test cases or in the SetUp() method to skip all tests for a fixture.
#define YB_SKIP_TEST_IN_TSAN() \
  do { \
    if (::yb::IsTsan()) { \
      GTEST_SKIP() << "Skipping test in TSAN"; \
      return; \
    } \
  } while (false)

#define YB_SKIP_TEST_IN_SANITIZERS() \
  do { \
    if (::yb::IsSanitizer()) { \
      GTEST_SKIP() << "Skipping test in sanitizers"; \
      return; \
    } \
  } while (false)
