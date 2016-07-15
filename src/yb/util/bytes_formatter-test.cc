// Copyright (c) YugaByte, Inc.

#include "yb/util/bytes_formatter.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

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

}
}
