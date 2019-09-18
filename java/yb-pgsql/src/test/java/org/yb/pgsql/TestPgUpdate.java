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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import static java.lang.Math.toIntExact;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgUpdate extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgUpdate.class);

  @Test
  public void testBasicUpdate() throws SQLException {
    String tableName = "test_basic_update";
    List<Row> allRows = setupSimpleTable(tableName);

    // UPDATE with condition on partition columns.
    String query = String.format("SELECT h FROM %s WHERE h = 2 AND vi = 1000", tableName);
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      String update_txt = String.format("UPDATE %s SET vi = 1000 WHERE h = 2", tableName);
      statement.execute(update_txt);

      // Not allowing update primary key columns.
      update_txt = String.format("UPDATE %s SET r = 1000 WHERE h = 2", tableName);
      runInvalidQuery(statement, update_txt);
      update_txt = String.format("UPDATE %s SET h = h + 1 WHERE vi = 2", tableName);
      runInvalidQuery(statement, update_txt);
    }

    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(10, rcount);
      }
    }

    // UPDATE with condition on regular columns.
    query = String.format("SELECT h FROM %s WHERE vi = 2000", tableName);
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      String update_txt = String.format("UPDATE %s SET vi = 2*vi WHERE vi = 1000", tableName);
      statement.execute(update_txt);
    }

    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(10, rcount);
      }
    }
  }

  @Test
  public void testUpdateWithSingleColumnKey() throws SQLException {
    List<Row> allRows = new ArrayList<>();
    String tableName = "test_update_single_column_key";
    try (Statement statement = connection.createStatement()) {
      createSimpleTableWithSingleColumnKey(tableName);
      String insertTemplate = "INSERT INTO %s(h, r, vi, vs) VALUES (%d, %f, %d, '%s')";

      for (int h = 0; h < 10; h++) {
        int r = h + 100;
        statement.execute(String.format(insertTemplate, tableName,
                                        h, r + 0.5, h * 10 + r, "v" + h + r));
        allRows.add(new Row((long) h,
                            r + 0.5,
                            h * 10 + r,
                            "v" + h + r));
      }
    }

    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT h FROM %s WHERE h > 5 AND vi = 1000", tableName);
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      String update_stmt = String.format("UPDATE %s SET vi = 1000 WHERE h > 5", tableName);
      statement.execute(update_stmt);

      // Not allowing update primary key columns.
      update_stmt = String.format("UPDATE %s SET h = h + 100 WHERE vi = 2", tableName);
      runInvalidQuery(statement, update_stmt);
    }

    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT h FROM %s WHERE h > 5 AND vi = 1000", tableName);
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(4, rcount);
      }
    }
  }

  @Test
  public void testUpdateReturn() throws SQLException {
    String tableName = "test_update_return";
    createSimpleTable(tableName);

    List<Row> expectedRows = new ArrayList<>();
    try (Statement insert_stmt = connection.createStatement()) {
      String insert_format = "INSERT INTO %s(h, r, vi, vs) VALUES(%d, %f, %d, '%s')";
      for (long h = 0; h < 5; h++) {
        for (int r = 0; r < 5; r++) {
          String insert_text = String.format(insert_format, tableName,
                                             h, r + 0.5, h * 10 + r, "v" + h + r);
          if (h == 2 || h == 3) {
            // Constructring rows to be returned by UPDATE.
            expectedRows.add(new Row(h + 100L, r + 0.5 + 100, toIntExact(h * 10 + r + 2000)));
          }
          insert_stmt.execute(insert_text);
        }
      }
    }

    // Sort expected rows to match with result set.
    Collections.sort(expectedRows);

    try (Statement update_stmt = connection.createStatement()) {
        // Update with RETURNING clause.
      String update_text = String.format("UPDATE %s SET vi = vi + 1000 WHERE h = 2 OR h = 3 " +
                                         "RETURNING h + 100, r + 100, vi + 1000", tableName);
      update_stmt.execute(update_text);

      // Verify RETURNING clause.
      ResultSet returning = update_stmt.getResultSet();
      assertEquals(expectedRows, getSortedRowList(returning));
    }
  }

  @Test
  public void testUpdateEnforceConstraints() throws SQLException {
    String tableName = "test_update_enforce_constraints";

    List<Row> expectedRows = new ArrayList<>();

    try (Statement stmt = connection.createStatement()) {
      String create_table_format = "CREATE TABLE %s(a INT PRIMARY KEY, b INT CHECK (b > 0))";
      stmt.execute(String.format(create_table_format, tableName));

      String insert_format = "INSERT INTO %s(a, b) VALUES(%d, %d) RETURNING a, b";

      // INSERT with invalid value will fail.
      String insert_text = String.format(insert_format, tableName, 1, -1);
      runInvalidQuery(stmt, insert_text);

      ResultSet returning;
      // INSERT with valid value will succeed.
      for (int i = 1; i <= 5; ++i) {
        insert_text = String.format(insert_format, tableName, i, i);
        stmt.execute(insert_text);
        expectedRows.add(new Row(i, i));
        returning = stmt.getResultSet();
        assertEquals(expectedRows.subList(i - 1, i), getSortedRowList(returning));
      }

      // UPDATE with invalid value will fail.
      runInvalidQuery(stmt, String.format("UPDATE %s SET b = -1 WHERE a = 1", tableName));
      stmt.execute(String.format("SELECT * FROM %s", tableName));
      returning = stmt.getResultSet();
      assertEquals(expectedRows, getSortedRowList(returning));

      // UPDATE multiple rows where some row will be invalid.
      runInvalidQuery(stmt, String.format("Update %s SET b = b - 2 WHERE a < 5", tableName));
      stmt.execute(String.format("SELECT * FROM %s", tableName));
      returning = stmt.getResultSet();
      assertEquals(expectedRows, getSortedRowList(returning));

      // UPDATE with valid value will succeed.
      stmt.execute(String.format("UPDATE %s SET b = b + 1", tableName));
      expectedRows.clear();
      for (int i = 1; i <= 5; ++i) {
        expectedRows.add(new Row(i, i + 1));
      }
      stmt.execute(String.format("SELECT * FROM %s", tableName));
      returning = stmt.getResultSet();
      assertEquals(expectedRows, getSortedRowList(returning));
    }
  }
}
