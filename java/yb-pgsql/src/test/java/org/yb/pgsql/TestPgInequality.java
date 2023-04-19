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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgInequality extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgInequality.class);

  private void createTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      createTable(statement, tableName);
    }
  }

  private void createTable(Statement statement, String tableName) throws SQLException {
    // Schema:
    //
    //      Table "public.test_inequality"
    //   Column |  Type   | Collation | Nullable | Default
    //  --------+---------+-----------+----------+---------
    //   h      | text    |           | not null |
    //   r1     | integer |           | not null |
    //   r2     | text    |           | not null |
    //   val    | text    |           |          |
    //  Indexes:
    //      "test_inequality_pkey" PRIMARY KEY, lsm (h HASH, r1, r2 DESC)
    //

    String sql = "CREATE TABLE IF NOT EXISTS " + tableName +
      "(h text, r1 integer, r2 text, val text, PRIMARY KEY(h, r1 ASC, r2 DESC))";
    LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
    statement.execute(sql);
    LOG.info("Table creation finished: " + tableName);
  }

  private List<Row> setupTable(String tableName) throws SQLException {
    List<Row> allRows = new ArrayList<>();
    try (Statement statement = connection.createStatement()) {
      createTable(tableName);
      String insertTemplate = "INSERT INTO %s(h, r1, r2, val) VALUES ('%s', %d, '%s', '%s')";

      // NOTE: val = h-r1-r2

      for (int h = 0; h < 10; h++) {
        for (int r1 = 0; r1 < 5; r1++) {
          for (char r2 = 'z'; r2 >= 's'; r2--) {
            statement.execute(String.format(insertTemplate, tableName, "" + h, r1, "" + r2,
                  h + "-" + r1 + "-" + r2));
            allRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }
      }
    }

    // Sort inserted rows and return.
    Collections.sort(allRows);
    return allRows;
  }

  @Test
  public void testWhereClauseInequality() throws Exception {
    String tableName = "test_inequality";

    List<Row> allRows = setupTable(tableName);
    try (Statement statement = connection.createStatement()) {
      // Test no where clause -- select all rows.
      String query = "SELECT val FROM " + tableName;
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> sortedRs = getSortedRowList(rs);
        assertEquals(allRows, sortedRs);
      }

      int h = 1;

      // Test where clause with inequality: A1 < r1 < {B1, B2}.

      int A = 2, B1 = 4, B2 = 5;
      query = "SELECT val FROM " + tableName + " WHERE h = '" + h + "' and r1 > " + A +
        " and r1 < " + B1 + " and r1 < " + B2;
      try (ResultSet rs = statement.executeQuery(query)) {
        // Check the result.
        List<Row> someRows = new ArrayList<>();
        for (int r1 = A + 1; r1 < B1 && r1 < B2; r1++) {
          for (char r2 = 'z'; r2 >= 's'; r2--) {
            someRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }

        Collections.sort(someRows);
        assertEquals(someRows, getSortedRowList(rs));
      }

      // Test where clause with inequality: {C1, C2} < r2 < D1.

      char C1 = 's', C2 = 't', D = 'z';
      query = "SELECT val FROM " + tableName + " WHERE h = '" + h + "' and r2 > '" + C1 +
        "' and r2 > '" + C2 + "' and r2 < '" + D + "'";
      try (ResultSet rs = statement.executeQuery(query)) {
        // Check the result.
        List<Row> someRows = new ArrayList<>();
        for (int r1 = 0; r1 < 5; r1++) {
          for (char r2 = (char)(D - 1); r2 > C1 && r2 > C2; r2 = (char)(r2 - 1)) {
            someRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }

        Collections.sort(someRows);
        assertEquals(someRows, getSortedRowList(rs));
      }

      // Test where clause with inequality: A1 < r1 < {B1, B2} \and {C1, C2} < r2 < D.

      query = "SELECT val FROM " + tableName + " WHERE h = '" + h + "' and r1 > " + A +
        " and r1 < " + B1 + " and r1 < " + B2 + " and r2 > '" + C1 + "' and r2 > '" + C2 +
        "' and r2 < '" + D + "'";
      try (ResultSet rs = statement.executeQuery(query)) {
        // Check the result.
        List<Row> someRows = new ArrayList<>();
        for (int r1 = A + 1; r1 < B1 && r1 < B2; r1++) {
          for (char r2 = (char)(D - 1); r2 > C1 && r2 > C2; r2 = (char)(r2 - 1)) {
            someRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }

        Collections.sort(someRows);
        assertEquals(someRows, getSortedRowList(rs));
      }
    }
  }
}
