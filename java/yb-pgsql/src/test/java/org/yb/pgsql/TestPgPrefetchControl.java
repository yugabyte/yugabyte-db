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
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;

// In this test module we adjust the number of rows to be prefetched by PgGate and make sure that
// the result for the query are correct.
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgPrefetchControl extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgPrefetchControl.class);

  @Override
  protected String pgPrefetchLimit() {
    // Set the prefetch limit to 100 for this test.
    return "100";
  }

  protected void createPrefetchTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(h int, r text, vi int, vs text, " +
                                 "PRIMARY KEY (h, r))", tableName);
      LOG.info("Execute: " + sql);
      statement.execute(sql);
      LOG.info("Created: " + tableName);
    }
  }

  @Test
  public void testSimplePrefetch() throws SQLException {
    String tableName = "TestPrefetch";
    createPrefetchTable(tableName);

    int tableRowCount = 5000;
    try (Statement statement = connection.createStatement()) {
      List<Row> insertedRows = new ArrayList<>();

      for (int i = 0; i < tableRowCount; i++) {
        int h = i;
        String r = String.format("range_%d", h);
        int vi = i + 10000;
        String vs = String.format("value_%d", vi);
        String stmt = String.format("INSERT INTO %s VALUES (%d, '%s', %d, '%s')",
                                    tableName, h, r, vi, vs);
        statement.execute(stmt);
        insertedRows.add(new Row(h, r, vi, vs));
      }

      // Check rows.
      String stmt = String.format("SELECT * FROM %s ORDER BY h", tableName);
      try (ResultSet rs = statement.executeQuery(stmt)) {
        assertEquals(insertedRows, getRowList(rs));
      }
    }
  }
}
