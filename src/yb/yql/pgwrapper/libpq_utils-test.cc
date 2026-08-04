// Copyright (c) YugabyteDB, Inc.
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

#include <gtest/gtest.h>

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pgwrapper {

TEST(PqEscapeTest, EscapeLiteral) {
  EXPECT_EQ(PqEscapeLiteral("hello"), "'hello'");
  EXPECT_EQ(PqEscapeLiteral("it's"), "'it''s'");
  EXPECT_EQ(PqEscapeLiteral("back\\slash"), "E'back\\\\slash'");
  EXPECT_EQ(PqEscapeLiteral("it's a back\\slash"), "E'it''s a back\\\\slash'");
  EXPECT_EQ(PqEscapeLiteral("E'literal'"), "'E''literal'''");
  EXPECT_EQ(PqEscapeLiteral("has\"quote"), "'has\"quote'");
  EXPECT_EQ(PqEscapeLiteral(""), "''");
}

TEST(PqEscapeTest, EscapeIdentifier) {
  EXPECT_EQ(PqEscapeIdentifier("hello"), "\"hello\"");
  EXPECT_EQ(PqEscapeIdentifier("has\"quote"), "\"has\"\"quote\"");
  EXPECT_EQ(PqEscapeIdentifier("back\\slash"), "\"back\\slash\"");
  EXPECT_EQ(PqEscapeIdentifier("it's a back\\slash"), "\"it's a back\\slash\"");
  EXPECT_EQ(PqEscapeIdentifier(""), "\"\"");
}

TEST(PqEscapeTest, EscapeStringConn) {
  EXPECT_EQ(PqEscapeStringConn("hello"), "'hello'");
  EXPECT_EQ(PqEscapeStringConn("it's"), "'it\\'s'");
  EXPECT_EQ(PqEscapeStringConn("back\\slash"), "'back\\\\slash'");
  EXPECT_EQ(PqEscapeStringConn("it's a back\\slash"), "'it\\'s a back\\\\slash'");
  EXPECT_EQ(PqEscapeStringConn("has\"quote"), "'has\"quote'");
  EXPECT_EQ(PqEscapeStringConn(""), "''");
}

} // namespace yb::pgwrapper
