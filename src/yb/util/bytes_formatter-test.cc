// Copyright (c) YugaByte, Inc.

#include "yb/util/bytes_formatter.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

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
