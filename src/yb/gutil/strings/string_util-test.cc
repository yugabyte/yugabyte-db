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
// Some portions Copyright 2013 The Chromium Authors. All rights reserved.

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/util.h"
#include "yb/util/string_util.h"
#include "yb/util/test_util.h"

#include <gtest/gtest.h>

using std::string;

namespace yb {

class StringUtilTest : public YBTest {
};

TEST_F(StringUtilTest, MatchPatternTest) {
  EXPECT_TRUE(MatchPattern("www.google.com", "*.com"));
  EXPECT_TRUE(MatchPattern("www.google.com", "*"));
  EXPECT_FALSE(MatchPattern("www.google.com", "www*.g*.org"));
  EXPECT_TRUE(MatchPattern("Hello", "H?l?o"));
  EXPECT_FALSE(MatchPattern("www.google.com", "http://*)"));
  EXPECT_FALSE(MatchPattern("www.msn.com", "*.COM"));
  EXPECT_TRUE(MatchPattern("Hello*1234", "He??o\\*1*"));
  EXPECT_FALSE(MatchPattern("", "*.*"));
  EXPECT_TRUE(MatchPattern("", "*"));
  EXPECT_TRUE(MatchPattern("", "?"));
  EXPECT_TRUE(MatchPattern("", ""));
  EXPECT_FALSE(MatchPattern("Hello", ""));
  EXPECT_TRUE(MatchPattern("Hello*", "Hello*"));
  // Stop after a certain recursion depth.
  EXPECT_FALSE(MatchPattern("123456789012345678", "?????????????????*"));

  // Test UTF8 matching.
  EXPECT_TRUE(MatchPattern("heart: \xe2\x99\xa0", "*\xe2\x99\xa0"));
  EXPECT_TRUE(MatchPattern("heart: \xe2\x99\xa0.", "heart: ?."));
  EXPECT_TRUE(MatchPattern("hearts: \xe2\x99\xa0\xe2\x99\xa0", "*"));
  // Invalid sequences should be handled as a single invalid character.
  EXPECT_TRUE(MatchPattern("invalid: \xef\xbf\xbe", "invalid: ?"));
  // If the pattern has invalid characters, it shouldn't match anything.
  EXPECT_FALSE(MatchPattern("\xf4\x90\x80\x80", "\xf4\x90\x80\x80"));

  // This test verifies that consecutive wild cards are collapsed into 1
  // wildcard (when this doesn't occur, MatchPattern reaches it's maximum
  // recursion depth).
  EXPECT_TRUE(MatchPattern("Hello" ,
                           "He********************************o"));
}

TEST_F(StringUtilTest, TestIsBigInteger) {
  ASSERT_TRUE(IsBigInteger("0"));
  ASSERT_TRUE(IsBigInteger("1234"));
  ASSERT_TRUE(IsBigInteger("-1234"));
  ASSERT_TRUE(IsBigInteger("+1234"));
  ASSERT_TRUE(IsBigInteger("0000"));
  ASSERT_TRUE(IsBigInteger("00001234"));
  ASSERT_TRUE(IsBigInteger("-00001234"));
  ASSERT_TRUE(IsBigInteger("+00001234"));
  ASSERT_TRUE(IsBigInteger("111222333444555666777888999888777666555444333222111"));
  ASSERT_FALSE(IsBigInteger(""));
  ASSERT_FALSE(IsBigInteger("."));
  ASSERT_FALSE(IsBigInteger("0."));
  ASSERT_FALSE(IsBigInteger(".0"));
  ASSERT_FALSE(IsBigInteger("0.0"));
  ASSERT_FALSE(IsBigInteger("0,0"));
  ASSERT_FALSE(IsBigInteger(" 0"));
  ASSERT_FALSE(IsBigInteger("0 "));
}

TEST_F(StringUtilTest, TestIsDecimal) {
  // Integer cases
  ASSERT_TRUE(IsDecimal("0"));
  ASSERT_TRUE(IsDecimal("1234"));
  ASSERT_TRUE(IsDecimal("-1234"));
  ASSERT_TRUE(IsDecimal("+1234"));
  ASSERT_TRUE(IsDecimal("0000"));
  ASSERT_TRUE(IsDecimal("00001234"));
  ASSERT_TRUE(IsDecimal("-00001234"));
  ASSERT_TRUE(IsDecimal("+00001234"));
  ASSERT_TRUE(IsDecimal("111222333444555666777888999888777666555444333222111"));
  // Decimal separator - regular
  ASSERT_TRUE(IsDecimal("0.0"));
  ASSERT_TRUE(IsDecimal("1234.1234"));
  ASSERT_TRUE(IsDecimal("-1234.1234"));
  ASSERT_TRUE(IsDecimal("+1234.1234"));
  ASSERT_TRUE(IsDecimal("0000.0000"));
  ASSERT_TRUE(IsDecimal("00001234.12340000"));
  ASSERT_TRUE(IsDecimal("-00001234.12340000"));
  ASSERT_TRUE(IsDecimal("+00001234.12340000"));
  ASSERT_TRUE(IsDecimal(string("111222333444555666777888999888777666555444333222111")
                            + ".111222333444555666777888999888777666555444333222111"));
  // Decimal separator - irregular
  ASSERT_TRUE(IsDecimal("0."));
  ASSERT_TRUE(IsDecimal(".0"));
  ASSERT_TRUE(IsDecimal("0.0"));
  // Exponent
  ASSERT_TRUE(IsDecimal("0e0"));
  ASSERT_TRUE(IsDecimal("1e2"));
  ASSERT_TRUE(IsDecimal("123E456"));
  ASSERT_TRUE(IsDecimal("-123e-456"));
  ASSERT_TRUE(IsDecimal("+123e+456"));
  ASSERT_TRUE(IsDecimal("000123e000456"));
  ASSERT_TRUE(IsDecimal(string("111222333444555666777888999888777666555444333222111")
                            + "e111222333444555666777888999888777666555444333222111"));
  // Both exponent and decimal separator
  ASSERT_TRUE(IsDecimal("1.2e345"));
  ASSERT_TRUE(IsDecimal(".123e1"));
  ASSERT_TRUE(IsDecimal("-.123e1"));
  ASSERT_TRUE(IsDecimal("+.123e1"));
  ASSERT_TRUE(IsDecimal("123.e1"));
  ASSERT_TRUE(IsDecimal("-123.e1"));
  ASSERT_TRUE(IsDecimal("+123.e1"));
  ASSERT_TRUE(IsDecimal(string("111222333444555666777888999888777666555444333222111")
                            + ".111222333444555666777888999888777666555444333222111"
                            + "e111222333444555666777888999888777666555444333222111"));
  // Non-decimals
  ASSERT_FALSE(IsDecimal(" 0"));
  ASSERT_FALSE(IsDecimal("0 "));
  ASSERT_FALSE(IsDecimal(""));
  ASSERT_FALSE(IsDecimal("."));
  ASSERT_FALSE(IsDecimal("0,0"));
  ASSERT_FALSE(IsDecimal("0e"));
  ASSERT_FALSE(IsDecimal("0e0.0"));
  ASSERT_FALSE(IsDecimal("0e.0"));
  ASSERT_FALSE(IsDecimal("0e0."));
}

TEST_F(StringUtilTest, TestIsBoolean) {
  ASSERT_TRUE(IsBoolean("true"));
  ASSERT_TRUE(IsBoolean("TRUE"));
  ASSERT_TRUE(IsBoolean("fAlSe"));
  ASSERT_TRUE(IsBoolean("falsE"));
  ASSERT_FALSE(IsBoolean(""));
  ASSERT_FALSE(IsBoolean("0"));
  ASSERT_FALSE(IsBoolean("1"));
  ASSERT_FALSE(IsBoolean(" true"));
  ASSERT_FALSE(IsBoolean("false "));
}

TEST_F(StringUtilTest, TestAppendWithSeparator) {
  string s;
  AppendWithSeparator("foo", &s);
  ASSERT_EQ(s, "foo");
  AppendWithSeparator("bar", &s);
  ASSERT_EQ(s, "foo, bar");
  AppendWithSeparator("foo", &s, " -- ");
  ASSERT_EQ(s, "foo, bar -- foo");

  s = "";
  AppendWithSeparator(string("foo"), &s);
  ASSERT_EQ(s, "foo");
  AppendWithSeparator(string("bar"), &s);
  ASSERT_EQ(s, "foo, bar");
  AppendWithSeparator(string("foo"), &s, " -- ");
  ASSERT_EQ(s, "foo, bar -- foo");
}

TEST_F(StringUtilTest, TestCollectionToString) {
  std::vector<std::string> v{"foo", "123", "bar", ""};
  ASSERT_EQ("[foo, 123, bar, ]", VectorToString(v));
  ASSERT_EQ("[foo, 123, bar, ]", RangeToString(v.begin(), v.end()));
  ASSERT_EQ("[]", RangeToString(v.begin(), v.begin()));
  ASSERT_EQ("[foo]", RangeToString(v.begin(), v.begin() + 1));
}

TEST_F(StringUtilTest, TestStringStartsWithOrEquals) {
  ASSERT_TRUE(StringStartsWithOrEquals("", ""));
  ASSERT_TRUE(StringStartsWithOrEquals("abc", ""));
  ASSERT_TRUE(StringStartsWithOrEquals("abc", "ab"));
  ASSERT_TRUE(StringStartsWithOrEquals("abc", "abc"));
  ASSERT_FALSE(StringStartsWithOrEquals("abc", "abd"));
  ASSERT_FALSE(StringStartsWithOrEquals("abc", "abcd"));
}

TEST_F(StringUtilTest, TestSplitAndFlatten) {
  ASSERT_EQ("[foo, bar, baz]", VectorToString(SplitAndFlatten(
      {"foo,bar", "baz"})));
  ASSERT_EQ("[foo, bar, baz, foo]", VectorToString(SplitAndFlatten(
      {"foo", "bar:baz", "foo"}, ":")));
}

TEST_F(StringUtilTest, JoinStringsLimitCount) {
  // Test empty vector and string.
  ASSERT_EQ("", JoinStringsLimitCount(std::vector<std::string>{}, ",", 10));
  ASSERT_EQ("", JoinStringsLimitCount(std::vector<std::string>{""}, ",", 10));

  // Test different limit counts and delimiters.
  ASSERT_EQ("foo,bar", JoinStringsLimitCount(std::vector<std::string>{"foo", "bar"}, ",", 10));

  std::vector<std::string> strings = {"foo",  "bar",  "baz",  "foo2", "bar2",
                                      "baz2", "foo3", "bar3", "baz3"};
  ASSERT_EQ("foo and 8 others", JoinStringsLimitCount(strings, ",", 1));
  ASSERT_EQ("foo;bar;baz;foo2 and 5 others", JoinStringsLimitCount(strings, ";", 4));
  ASSERT_EQ("foobarbazfoo2 and 5 others", JoinStringsLimitCount(strings, "", 4));
  ASSERT_EQ("foo,bar,baz,foo2,bar2,baz2,foo3,bar3,baz3", JoinStringsLimitCount(strings, ",", 20));

  // Make sure intput result is cleared.
  std::string result = "test";
  JoinStringsLimitCount(std::vector<std::string>{"foo"}, ",", 1, &result);
  ASSERT_EQ(result, "foo");
}

} // namespace yb
