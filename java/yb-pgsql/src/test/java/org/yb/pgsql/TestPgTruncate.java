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
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgTruncate extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgTruncate.class);

  @Test
  public void testBasicTruncate() throws SQLException {
    String tableName = "test_truncate";
    List<Row> allRows = setupSimpleTable(tableName);

    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT count(*) FROM %s", tableName);
      try (ResultSet rs = statement.executeQuery(query)) {
        assertEquals(true, rs.next());
        assertEquals(allRows.size(), rs.getLong(1));
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("TRUNCATE %s", tableName));
    }

    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT count(*) FROM %s", tableName);
      try (ResultSet rs = statement.executeQuery(query)) {
        assertEquals(true, rs.next());
        assertEquals(0, rs.getLong(1));
      }
    }
  }

  @Test
  public void testBasicTruncateMultiTables() throws SQLException {
    String tableName1 = "test_truncate1";
    String tableName2 = "test_truncate2";
    String tableName3 = "test_truncate3";
    List<Row> allRows1 = setupSimpleTable(tableName1);
    List<Row> allRows2 = setupSimpleTable(tableName2);
    List<Row> allRows3 = setupSimpleTable(tableName3);

    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT (SELECT count(*) FROM %s)," +
                                   "       (SELECT count(*) FROM %s)," +
                                   "       (SELECT count(*) FROM %s)",
                                   tableName1, tableName2, tableName3);
      try (ResultSet rs = statement.executeQuery(query)) {
        assertEquals(true, rs.next());
        assertEquals(allRows1.size(), rs.getLong(1));
        assertEquals(allRows2.size(), rs.getLong(2));
        assertEquals(allRows3.size(), rs.getLong(3));
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("TRUNCATE %s, %s, %s", tableName1, tableName2, tableName3));
    }

    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT (SELECT count(*) FROM %s)," +
                                   "       (SELECT count(*) FROM %s)," +
                                   "       (SELECT count(*) FROM %s)",
                                   tableName1, tableName2, tableName3);
      try (ResultSet rs = statement.executeQuery(query)) {
        assertEquals(true, rs.next());
        assertEquals(0, rs.getLong(1));
        assertEquals(0, rs.getLong(2));
        assertEquals(0, rs.getLong(3));
      }
    }
  }
}
