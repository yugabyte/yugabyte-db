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

package org.yb.pgsql;

import static org.junit.Assume.assumeTrue;
import static org.yb.AssertionWrappers.assertEquals;

import java.sql.Connection;
import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.BuildTypeUtil;

@RunWith(value=YBTestRunner.class)
public class TestPgJsonLexer extends BasePgSQLTest {
  private static void assertParse(Statement stmt, String json, boolean valid) throws Exception {
    String literal = "'" + json.replace("'", "''") + "'";
    assertEquals(json, Boolean.valueOf(valid),
        getSingleRow(stmt, "SELECT yb_test_json_parse_exact(" + literal + ")").getBoolean(0));
  }

  // The lexer must not read past the end of the input, e.g. when a string is unterminated.
  @Test
  public void testNoReadPastEndOfInput() throws Exception {
    assumeTrue("Requires an ASAN build", BuildTypeUtil.isASAN());
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS yb_test_memory_errors");
      assertParse(stmt, "\"a", false);
      assertParse(stmt, "\"ab", false);
      assertParse(stmt, "\"abc", false);
      assertParse(stmt, "[\"abc", false);
      assertParse(stmt, "{\"a\": \"bc", false);
      assertParse(stmt, "\"ab\\", false);
      assertParse(stmt, "\"ab\\u00", false);
      assertParse(stmt, "\"abc\"", true);
      assertParse(stmt, "{\"a\": [\"bc\", 1]}", true);
    }
  }
}
