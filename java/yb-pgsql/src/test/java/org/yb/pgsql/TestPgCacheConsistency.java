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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgCacheConsistency extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgCacheConsistency.class);

  @Test
  public void testBasicDDLOperations() throws Exception {
    try (Connection connection1 = createConnection(0);
         Connection connection2 = createConnection(1);
         Statement statement1 = connection1.createStatement();
         Statement statement2 = connection2.createStatement()) {
      Set<Row> expectedRows = new HashSet<>();

      // Create a table with connection 1.
      statement1.execute("CREATE TABLE cache_test1(a int)");

      // Ensure table is usable from both connections.
      statement1.execute("INSERT INTO cache_test1(a) VALUES (1)");
      expectedRows.add(new Row(1));
      statement2.execute("INSERT INTO cache_test1(a) VALUES (2)");
      expectedRows.add(new Row(2));

      // Check values.
      try (ResultSet rs = statement1.executeQuery("SELECT * FROM cache_test1")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Drop table from connection 2.
      statement2.execute("DROP TABLE cache_test1");

      // Check that insert now fails on both tables.
      runInvalidQuery(statement1, "INSERT INTO cache_test1(a) VALUES (3)");
      runInvalidQuery(statement2, "INSERT INTO cache_test1(a) VALUES (4)");

      // Create and use a new table on connection 1.
      statement1.execute("CREATE TABLE cache_test2(a int)");
      statement1.execute("INSERT INTO cache_test2(a) VALUES (1)");

      // Drop and create a same-name table on connection 2.
      statement2.execute("DROP TABLE cache_test2");
      statement2.execute("CREATE TABLE cache_test2(a float)");

      // Check that can use new table on both connections.
      statement1.execute("INSERT INTO cache_test2(a) VALUES (1)");
      expectedRows.add(new Row(1.0));
      statement2.execute("INSERT INTO cache_test2(a) VALUES (2)");
      expectedRows.add(new Row(2.0));

      // Check values.
      try (ResultSet rs = statement1.executeQuery("SELECT * FROM cache_test2")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

      // Drop and create a same-name table on connection 1.
      statement1.execute("DROP TABLE cache_test2");
      statement1.execute("CREATE TABLE cache_test2(a bool)");

      // Check that we cannot still insert a float (but that bool will work).
      runInvalidQuery(statement2, "INSERT INTO cache_test2(a) VALUES (1.0)");
      statement2.execute("INSERT INTO cache_test2(a) VALUES (true)");
      expectedRows.add(new Row(true));
      statement1.execute("INSERT INTO cache_test2(a) VALUES (false)");
      expectedRows.add(new Row(false));

      // Check values.
      try (ResultSet rs = statement2.executeQuery("SELECT * FROM cache_test2")) {
        assertEquals(expectedRows, getRowSet(rs));
      }
      expectedRows.clear();

    }
  }
}
