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

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgDelete extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDelete.class);

  @Test
  public void testBasicDelete() throws SQLException {
    Set<Row> allRows = setupSimpleTable("test_basic_del");

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_basic_del WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(10, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DELETE FROM test_basic_del WHERE h = 2");
    }

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_basic_del WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }
  }

  @Test
  public void testBasicDelete2() throws SQLException {
    Set<Row> allRows = setupSimpleTable("test_basic_del2");

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_basic_del2 WHERE h > 8";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(10, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DELETE FROM test_basic_del2 WHERE h > 8");
    }

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_basic_del2 WHERE h > 8";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }
  }

  @Test
  public void testDeleteWithSingleColumnKey() throws SQLException {
    Set<Row> allRows = new HashSet<>();
    String tableName = "test_delete_single_column_key";
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
      String query = "SELECT h FROM test_delete_single_column_key WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(1, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DELETE FROM test_delete_single_column_key WHERE h = 2");
    }

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_delete_single_column_key WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }
  }

  @Test
  public void testDeleteWithSingleColumnKey2() throws SQLException {
    Set<Row> allRows = new HashSet<>();
    String tableName = "test_delete_single_column_key2";
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
      String query = "SELECT h FROM test_delete_single_column_key2 WHERE h > 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(7, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DELETE FROM test_delete_single_column_key2 WHERE h > 2");
    }

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_delete_single_column_key2 WHERE h > 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }
  }
}
