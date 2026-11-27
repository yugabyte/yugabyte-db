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

package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.SQLException;
import java.sql.Statement;

@RunWith(value=YBTestRunner.class)
public class TestPgTruncate extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgTruncate.class);

  @Test
  public void testCountAfterTruncate() throws SQLException {
    final int numInsertedRows = 10000;
    final String tableName = "test_truncate";
    final String insertFormat = "INSERT INTO %s(h, r, vi, vs) SELECT generate_series(1, %d), " +
          "0.5, generate_series(1, %d), 'constant-string'";
    final String insertText = String.format(insertFormat, tableName, numInsertedRows,
          numInsertedRows);
    final String countRowsStmt = "SELECT COUNT(*) FROM %s";
    final String countRelTuplesStmt = "SELECT reltuples FROM pg_class WHERE relname = '%s'";

    createSimpleTableWithSingleColumnKey(tableName);

    // Populate the table with rows.
    try (Statement statement = connection.createStatement()) {
      // Create a secondary index on vi.
      statement.execute(String.format("CREATE INDEX on %s (vi)", tableName));
      statement.execute(insertText);
      statement.execute(String.format("ANALYZE %s", tableName));

      assertOneRow(statement, String.format(countRowsStmt, tableName), numInsertedRows);
      assertOneRow(statement, String.format(countRelTuplesStmt, tableName), numInsertedRows);
      assertOneRow(statement, String.format(countRelTuplesStmt,
        String.format("%s_pkey", tableName)), numInsertedRows);
      assertOneRow(statement, String.format(countRelTuplesStmt,
        String.format("%s_%s_idx", tableName, "vi")), numInsertedRows);
    }

    // Truncate the table and assert the new row count.
    try (Statement statement = connection.createStatement()) {
      statement.execute("TRUNCATE " + tableName);

      assertOneRow(statement, String.format(countRowsStmt, tableName), 0);
      assertOneRow(statement, String.format(countRelTuplesStmt, tableName), -1);
      assertOneRow(statement, String.format(countRelTuplesStmt,
        String.format("%s_pkey", tableName)), -1);
      assertOneRow(statement, String.format(countRelTuplesStmt,
        String.format("%s_%s_idx", tableName, "vi")), -1);

    }

    // Ensure that rows can be inserted into the table again.
    try (Statement statement = connection.createStatement()) {
      statement.execute(insertText);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, numInsertedRows);
    }
  }
}
