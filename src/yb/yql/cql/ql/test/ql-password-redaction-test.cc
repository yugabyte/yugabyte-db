//--------------------------------------------------------------------------------------------------
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
//
//--------------------------------------------------------------------------------------------------

#include <string>
#include <vector>

#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/yql/cql/ql/util/password_redaction.h"

namespace yb {
namespace ql {

using std::string;

class PasswordRedactionTest : public YBTest {};

TEST_F(PasswordRedactionTest, RedactsEveryPasswordValue) {
  const std::vector<std::pair<string, string>> cases = {
    {"CREATE ROLE r WITH login = true AND pAsSWorD=  'hide me!'",
     "CREATE ROLE r WITH login = true AND pAsSWorD=  <REDACTED>"},
    {"CREATE ROLE r WITH HASHED PASSWORD = '$2a$10$abc'",
     "CREATE ROLE r WITH HASHED PASSWORD = <REDACTED>"},
    {"CREATE ROLE r WITH PASSWORD = 'a' AND PASSWORD = 'b'",
     "CREATE ROLE r WITH PASSWORD = <REDACTED> AND PASSWORD = <REDACTED>"},
    {"CREATE ROLE r WITH PASSWORD = 'it''s' AND login = true",
     "CREATE ROLE r WITH PASSWORD = <REDACTED> AND login = true"},
    {"CREATE ROLE r WITH PASSWORD = E'a\\'b' AND login = true",
     "CREATE ROLE r WITH PASSWORD = <REDACTED> AND login = true"},
    {"CREATE ROLE r WITH PASSWORD = U&'secret' AND login = true",
     "CREATE ROLE r WITH PASSWORD = <REDACTED> AND login = true"},
    {"CREATE ROLE r WITH PASSWORD = $tag$se$$cret$tag$ AND login = true",
     "CREATE ROLE r WITH PASSWORD = <REDACTED> AND login = true"},
    {"CREATE ROLE r WITH PASSWORD = 'abc' -- c\n  'def' AND login = true",
     "CREATE ROLE r WITH PASSWORD = <REDACTED> AND login = true"},
    {"CREATE ROLE r WITH PASSWORD = 'unterminated", "CREATE ROLE r WITH PASSWORD = <REDACTED>"},
    // A ' that is not a string delimiter doesn't shift the PASSWORD clause out of alignment.
    {"/* don't */ CREATE ROLE \"o'brien\" WITH PASSWORD = 'x'",
     "/* don't */ CREATE ROLE \"o'brien\" WITH PASSWORD = <REDACTED>"},
    // A $ inside an unquoted identifier doesn't open a dollar-quoted string.
    {"CREATE ROLE a$b$ WITH LOGIN = true AND PASSWORD = 'secret'",
     "CREATE ROLE a$b$ WITH LOGIN = true AND PASSWORD = <REDACTED>"},
    // Text that only looks like a password clause, inside a string constant, is left alone.
    {"SELECT * FROM t WHERE v = 'password = x'", "SELECT * FROM t WHERE v = 'password = x'"},
  };
  for (const auto& [input, expected] : cases) {
    ASSERT_EQ(RedactPasswordLiterals(input), expected);
  }
}

TEST_F(PasswordRedactionTest, ReportsRedactedRanges) {
  const string stmt = "CREATE ROLE r WITH PASSWORD = 'ab' AND PASSWORD = $$cd$$";
  std::vector<RedactedRange> ranges;
  RedactPasswordLiterals(stmt, &ranges);
  ASSERT_EQ(ranges.size(), 2);
  ASSERT_EQ(stmt.substr(ranges[0].begin, ranges[0].end - ranges[0].begin), "'ab'");
  ASSERT_EQ(stmt.substr(ranges[1].begin, ranges[1].end - ranges[1].begin), "$$cd$$");

  ranges.clear();
  RedactPasswordLiterals("SELECT * FROM t", &ranges);
  ASSERT_TRUE(ranges.empty());
}

// ProcessContextBase::Error echoes the statement into the error message, which reaches the tserver
// log, the client, and the audit record. The echo must carry the redacted statement, with the '^'
// marker still under the error token.
class QLTestErrorEchoRedaction : public QLTestBase {
 protected:
  // Returns the error message of a statement that fails to parse.
  string ParseError(const string& stmt) {
    Status s = TestParser(stmt);
    EXPECT_FALSE(s.ok());
    return s.ToString();
  }
};

TEST_F(QLTestErrorEchoRedaction, MarkerAfterPasswordIsShifted) {
  const string error = ParseError("CREATE ROLE r WITH PASSWORD = 'hunter22_secret' GARBAGE");
  ASSERT_EQ(error.find("hunter22_secret"), string::npos) << error;
  const string echo = "CREATE ROLE r WITH PASSWORD = <REDACTED> GARBAGE";
  const string marker = string(echo.find("GARBAGE"), ' ') + "^^^^^^^";
  ASSERT_NE(error.find(echo + "\n" + marker + "\n"), string::npos) << error;
}

TEST_F(QLTestErrorEchoRedaction, MarkerBeforePasswordIsUnchanged) {
  const string error =
      ParseError("CREATE ROLE r WITH BOGUS = true AND PASSWORD = 'hunter22_secret'");
  ASSERT_EQ(error.find("hunter22_secret"), string::npos) << error;
  const string echo = "CREATE ROLE r WITH BOGUS = true AND PASSWORD = <REDACTED>";
  const string marker = string(echo.find("BOGUS"), ' ') + "^^^^^";
  ASSERT_NE(error.find(echo + "\n" + marker + "\n"), string::npos) << error;
}

TEST_F(QLTestErrorEchoRedaction, MultiLinePasswordCollapsesLines) {
  // The redacted value spans a line break, so the marker line index shifts as well as its column.
  const string error = ParseError("CREATE ROLE r WITH PASSWORD = 'hunter22\nsecret'\nGARBAGE");
  ASSERT_EQ(error.find("hunter22"), string::npos) << error;
  ASSERT_NE(error.find("CREATE ROLE r WITH PASSWORD = <REDACTED>\nGARBAGE\n^^^^^^^\n"),
            string::npos) << error;
}

TEST_F(QLTestErrorEchoRedaction, NoPasswordLeavesEchoUnchanged) {
  const string error = ParseError("SELECT * FROM t WHERE v = 'x' GARBAGE");
  const string echo = "SELECT * FROM t WHERE v = 'x' GARBAGE";
  const string marker = string(echo.find("GARBAGE"), ' ') + "^^^^^^^";
  ASSERT_NE(error.find(echo + "\n" + marker + "\n"), string::npos) << error;
}

}  // namespace ql
}  // namespace yb
