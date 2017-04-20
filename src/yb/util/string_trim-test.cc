// Copyright (c) YugaByte, Inc.

#include "yb/util/string_trim.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

TEST(StringTrimTest, LeftTrimStr) {
  ASSERT_EQ("foo ", LeftTrimStr("  \t \f \n \r \v foo "));
  ASSERT_EQ("oobar", LeftTrimStr("foobar", "fr"));
}

TEST(StringTrimTest, RightTrimStr) {
  ASSERT_EQ(" foo", RightTrimStr(" foo\t \f \n \r \v "));
  ASSERT_EQ("fooba", RightTrimStr("foobar", "fr"));
}

TEST(StringTrimTest, TrimStr) {
  ASSERT_EQ("foo", TrimStr(" \t \f \n \r \v foo \t \f \n \r \v "));
  ASSERT_EQ("ooba", TrimStr("foobar", "fr"));
}

TEST(StringTrimTest, TestApplyEagerLineContinuation) {
  ASSERT_EQ("  foo bar\n  baz  ", ApplyEagerLineContinuation("  foo \\\n      bar\n  baz  "));
  ASSERT_EQ("  foo bar\n  baz  ", ApplyEagerLineContinuation("  foo \\\n   \\\n    bar\n  baz  "));
}

TEST(StringTrimTest, TestTrimLeadingSpaces) {
  ASSERT_EQ(
      R"#(
This is my

  Text block
)#",
      LeftShiftTextBlock(R"#(
      This is my

        Text block
      )#"));
}

}  // namespace util
}  // namespace yb
