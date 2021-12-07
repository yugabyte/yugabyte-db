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

#include "yb/util/bytes_formatter.h"

#include <string>
#include <gtest/gtest.h>

using std::string;

namespace yb {
namespace util {

TEST(BytesFormatterTest, TestSingleQuotes) {
  ASSERT_EQ(
      "'foo\\'bar\"baz'",
      FormatBytesAsStr("foo'bar\"baz", QuotesType::kSingleQuotes));
  ASSERT_EQ(
      "'\\x01\\x02\\x03\\xfe\\xff'",
      FormatBytesAsStr("\x01\x02\x03\xfe\xff", QuotesType::kSingleQuotes));
  ASSERT_EQ("'foo\\\\bar'", FormatBytesAsStr("foo\\bar", QuotesType::kSingleQuotes));
}

TEST(BytesFormatterTest, TestDoubleQuotes) {
  ASSERT_EQ(
      "\"foo'bar\\\"baz\"",
      FormatBytesAsStr("foo'bar\"baz", QuotesType::kDoubleQuotes));
  ASSERT_EQ(
      "\"\\x01\\x02\\x03\\xfe\\xff\"",
      FormatBytesAsStr("\x01\x02\x03\xfe\xff", QuotesType::kDoubleQuotes));
  ASSERT_EQ("\"foo\\\\bar\"", FormatBytesAsStr("foo\\bar", QuotesType::kDoubleQuotes));
}

TEST(BytesFormatterTest, TestMaxLength) {
  const string input_str("foo'bar\"baz");
  ASSERT_EQ(
      "\"fo<...9 bytes skipped>\"",
      FormatBytesAsStr(input_str, QuotesType::kDoubleQuotes, 3));
  ASSERT_EQ(
      "\"foo'<...7 bytes skipped>\"",
      FormatBytesAsStr(input_str, QuotesType::kDoubleQuotes, 5));
  ASSERT_EQ(
      "\"foo'bar\\\"<...3 bytes skipped>\"",
      FormatBytesAsStr(input_str, QuotesType::kDoubleQuotes, 10));
  ASSERT_EQ(
      "\"foo'bar\\\"baz\"",
      FormatBytesAsStr(input_str, QuotesType::kDoubleQuotes, 20));
}

}  // namespace util
}  // namespace yb
