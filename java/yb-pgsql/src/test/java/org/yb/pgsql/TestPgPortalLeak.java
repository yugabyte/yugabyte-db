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
import org.yb.YBTestRunner;
import static org.yb.AssertionWrappers.*;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgPortalLeak extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgPortalLeak.class);

  private final int rowCount = 4000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    // TODO(#21956): In RC isolation, testNoPortalLeak fails because there actually seems to be a
    // memory leak.
    flags.put("yb_enable_read_committed_isolation", "false");
    return flags;
  }

  private void createTable(String tableName) throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute(
        "CREATE TABLE " + tableName +
        "  (id text, ename text, age int, city text, about_me text, PRIMARY KEY(id, ename))");

      String insertFormat = "INSERT INTO " + tableName + " VALUES ('%s', '%s', %d, '%s', '%s')";
      for (int iter = 0; iter < rowCount; iter++) {
        String id = String.format("user-%04096d", iter);
        String ename = String.format("name-%d", iter);
        int age = 20 + iter%50;
        String city = String.format("city-%d", iter%1000);
        String aboutMe = String.format("about_me-%d", iter);

        statement.execute(String.format(insertFormat, id, ename, age, city, aboutMe));
      }
    }
  }

  private void selectBatches(String tableName, boolean expectNoLeak) throws Exception {
    // Set auto commit to false so that query can be read in small batches.
    connection.setAutoCommit(false);

    try (Statement statement = connection.createStatement()) {
      // Start transaction.
      statement.execute("BEGIN");

      // Read data from table in small batches.
      int fetchSize = 100;
      String selectTxt = "select yb_mem_usage_sql_b(), id, ename, age, city from " + tableName;
      Statement selectStmt = connection.createStatement();

      selectStmt.setFetchSize(fetchSize);
      long expectedUsage = 0;
      long currentUsage = 0;
      try (ResultSet rs = selectStmt.executeQuery(selectTxt)) {
        int rowCount = 0;
        while (rs.next()) {
          rowCount++;

          // Memory usage for each batch should be the same as other batches, but the usage will
          // fluctuate for the first few rows in each batch.
          if (expectNoLeak) {
            if (rowCount % fetchSize > 2) {
              if (expectedUsage == 0) {
                expectedUsage = rs.getLong(1);
              }
              currentUsage = rs.getLong(1);
              assertEquals(expectedUsage, currentUsage);
            }

          } else if (rowCount % fetchSize == 7) {
            // Expecting leaking from batch to batch.
            currentUsage = rs.getLong(1);
            assertLessThan(expectedUsage, currentUsage);
            expectedUsage = currentUsage;
          }

          // Print result every 500.
          if (rowCount % 500 == 0) {
            LOG.info(String.format("Row %d: usage = %d bytes," +
                                   " ename = '%s', age = '%s', city = '%s'",
                                   rowCount, rs.getLong(1),
                                   rs.getString(3).trim(), rs.getString(4), rs.getString(5)));
          }
        }
      } catch (Exception e) {
        statement.execute("ABORT");
      }

      // Start transaction.
      statement.execute("END");
    }
  }

  // Test that there is no leak.
  @Test
  public void testNoPortalLeak() throws Exception {
    String tableName = "tableExpectPgPortalHasNoLeak";
    createTable(tableName);
    selectBatches(tableName, true /* expectNoLeak */);
  }
}
