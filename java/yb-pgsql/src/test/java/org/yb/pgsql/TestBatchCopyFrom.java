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

import static org.junit.Assert.fail;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.sql.SQLException;
import java.sql.Statement;

import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.postgresql.copy.CopyManager;
import org.postgresql.core.BaseConnection;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestBatchCopyFrom extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestBatchCopyFrom.class);

  private static final String INVALID_USAGE_ERROR_MSSG =
      "ROWS_PER_TRANSACTION option is not supported";
  private static final String INVALID_ARGUMENT_ERROR_MSSG =
      "argument to option \"rows_per_transaction\" must be a positive integer";

  private CopyManager copyManager = getCopyAPI();

  private CopyManager getCopyAPI() {
    if (copyManager == null) {
      try {
        copyManager = new CopyManager((BaseConnection) connection);
      } catch (Exception e) {
        LOG.info("CopyManager instance failed to get created.", e);
      }
    }
    return copyManager;
  }

  private String getAbsFilePath(String fileName) {
    return TestUtils.getBaseTmpDir() + "/" + fileName;
  }

  private void createFileInTmpDir(String absFilePath, int totalLines) throws IOException {
    File myObj = new File(absFilePath);
    myObj.createNewFile();

    BufferedWriter writer = new BufferedWriter(new FileWriter(myObj));
    writer.write("a,b,c,d\n");

    int totalValues = totalLines * 4;
    for (int i = 0; i < totalValues; i++) {
      if (i != 0 && (i + 1) % 4 == 0)
        writer.write("\n");
      else
        writer.write(i+",");
    }
    writer.close();
  }

  @Test
  public void testWithSmallBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-large.txt");
    String tableName = "smallBatchSize";
    int totalLines = 100000;
    int batchSize = 100;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
          tableName, absFilePath, batchSize));

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testWithLargeBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-large.txt");
    String tableName = "smallBatchSize";
    int totalLines = 100;
    int batchSize = 1000;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
          tableName, absFilePath, batchSize));

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testStatementLevelAfterTriggerWithBatchTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batchSize10-copyfrom.txt");
    String tableName = "testAfterTrigger";
    int totalLines = 20;
    int batchSize = 10;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format(
          "CREATE FUNCTION trigger_fn() RETURNS TRIGGER AS $$ BEGIN " +
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s); " +
          "RETURN NEW; END; $$ LANGUAGE 'plpgsql';",
          tableName, absFilePath, batchSize));
      statement.execute(String.format(
          "CREATE TRIGGER trig AFTER INSERT ON %s " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn();",
          tableName));
      runInvalidQuery(statement,
          String.format("INSERT INTO %s (a, b) VALUES (1, 2)", tableName),
          INVALID_USAGE_ERROR_MSSG);

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testStatementLevelBeforeTriggerWithBatchTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batchSize10-copyfrom.txt");
    String tableName = "testBeforeTrigger";
    int totalLines = 20;
    int batchSize = 10;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format(
          "CREATE FUNCTION trigger_fn() RETURNS TRIGGER AS $$ BEGIN " +
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s); " +
          "RETURN NEW; END; $$ LANGUAGE 'plpgsql';",
          tableName, absFilePath, batchSize));
      statement.execute(String.format(
          "CREATE TRIGGER trig BEFORE INSERT ON %s " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn();",
          tableName));
      runInvalidQuery(statement,
          String.format("INSERT INTO %s (a, b) VALUES (1, 2)", tableName),
          INVALID_USAGE_ERROR_MSSG);

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testNoBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-small.txt");
    String tableName = "noBatchSizeTable";
    int totalLines = 2;
    int batchSize = 0;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchSize),
          INVALID_ARGUMENT_ERROR_MSSG);

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testTempTableWithBatchTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batchSize5-copyfrom.txt");
    String tableName = "tempTable";
    int totalLines = 20;
    int batchSize = 5;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TEMPORARY TABLE %s (a int, b int, c int, d int)", tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchSize),
          INVALID_USAGE_ERROR_MSSG);

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testInNestedTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batchSize5-copyfrom.txt");
    String copyFromTableName = "batchedTable";
    int totalLines = 20;
    int batchSize = 5;
    String dummyTableName = "sampleTable";

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(
          String.format("CREATE TABLE %s (a int, b int, c int, d int)", dummyTableName));
      statement.execute(
          String.format("CREATE TABLE %s (a int, b int, c int, d int)", copyFromTableName));
      statement.execute("BEGIN TRANSACTION;");
      statement.execute(String.format("INSERT INTO %s (a, b) VALUES (1, 2)", dummyTableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              copyFromTableName, absFilePath, batchSize),
          INVALID_USAGE_ERROR_MSSG);
      statement.execute("END;");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + copyFromTableName, 0);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 0);
    }
  }

  @Test
  public void testStdinCopy() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-stdin.txt");
    String tableName = "stdinBatchSizeTable";
    int totalLines = 5;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      copyManager.copyIn(
          String.format(
              "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, batchSize),
          new BufferedReader(new FileReader(absFilePath))
      );

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void tesStdinCopyInNestedTransactionWithPreviousTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-stdin-nested.txt");
    String tableName = "stdinNestedBatchSizeTable";
    String dummyTableName = "dummyTable";
    int totalLines = 5;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a int)", dummyTableName));
      statement.execute("BEGIN TRANSACTION");
      statement.execute(String.format("INSERT INTO %s (a) VALUES (1)", dummyTableName));
      try {
        copyManager.copyIn(
            String.format(
                "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
                tableName, batchSize),
            new BufferedReader(new FileReader(absFilePath)));
        fail(String.format("Statement did not fail: %s", INVALID_USAGE_ERROR_MSSG));
      } catch (SQLException e) {
          if (StringUtils.containsIgnoreCase(e.getMessage(), INVALID_USAGE_ERROR_MSSG)) {
            LOG.info("Expected exception", e);
          } else {
            fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
                               e.getMessage(), INVALID_USAGE_ERROR_MSSG));
        }
      }
      statement.execute("COMMIT TRANSACTION");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 0);
    }
  }

  @Test
  public void tesStdinCopyInNestedTransactionWithProceedingTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-stdin-nested.txt");
    String tableName = "stdinNestedBatchSizeTable";
    String dummyTableName = "dummyTable";
    int totalLines = 5;
    int batchSize = 2;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a int)", dummyTableName));
      statement.execute("BEGIN TRANSACTION");
      copyManager.copyIn(
          String.format(
              "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, batchSize),
          new BufferedReader(new FileReader(absFilePath))
      );
      statement.execute(String.format("INSERT INTO %s (a) VALUES (1)", dummyTableName));
      statement.execute("COMMIT TRANSACTION");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 1);
    }
  }

  @Test
  public void tesStdinCopyInNestedTransactionWithoutOtherTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-stdin.txt");
    String tableName = "stdinNestedBatchSizeTable";
    int totalLines = 5;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute("BEGIN TRANSACTION");
      copyManager.copyIn(
          String.format(
              "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, batchSize),
          new BufferedReader(new FileReader(absFilePath))
      );
      statement.execute("COMMIT TRANSACTION");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }
}
