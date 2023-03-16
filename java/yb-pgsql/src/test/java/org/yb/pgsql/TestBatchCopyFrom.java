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

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;

import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import com.yugabyte.copy.CopyManager;
import com.yugabyte.core.BaseConnection;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestBatchCopyFrom extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestBatchCopyFrom.class);

  private static final String INVALID_BATCH_OPTION_USAGE_WARNING_MSG =
      "Batched COPY is not supported";
  private static final String INVALID_BATCH_SIZE_ERROR_MSG =
      "argument to option \"rows_per_transaction\" must be a positive integer";
  private static final String INVALID_NUM_SKIPPED_ROWS_ERROR_MSG =
      "argument to option \"skip\" must be a nonnegative integer";
  private static final String INVALID_COPY_INPUT_ERROR_MSG =
      "invalid input syntax for integer";
  private static final String INVALID_FOREIGN_KEY_ERROR_MSG =
      "violates foreign key constraint";
  private static final String BATCH_TXN_SESSION_VARIABLE_NAME =
      "yb_default_copy_from_rows_per_transaction";
  private static final int BATCH_TXN_SESSION_VARIABLE_DEFAULT_ROWS = 20000;
  private static final String DISABLE_TXN_WRITES_SESSION_VARIABLE_NAME =
      "yb_disable_transactional_writes";
  private static final String YB_ENABLE_UPSERT_MODE_VARIABLE_NAME =
      "yb_enable_upsert_mode";

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

  private void writeToFileInTmpDir(String absFilePath, String line) throws IOException {
    File myObj = new File(absFilePath);
    BufferedWriter writer = new BufferedWriter(new FileWriter(myObj, true));
    writer.write(line);
    writer.close();
  }

  private void appendInvalidLines(String absFilePath, int totalLines) throws IOException {
    FileWriter fileWriter = new FileWriter(absFilePath,true);
    BufferedWriter bufferedWriter = new BufferedWriter(fileWriter);
    for (int i = 0; i < totalLines; i++) {
      bufferedWriter.write("#,#,#,#\n");
    }
    bufferedWriter.close();
  }

  private void appendValidLines(String absFilePath, int totalLines, int offset)
      throws IOException {
    FileWriter fileWriter = new FileWriter(absFilePath,true);
    BufferedWriter bufferedWriter = new BufferedWriter(fileWriter);

    int startValue = offset * 4;
    int endValue = startValue + totalLines * 4;
    for (int i = startValue; i < endValue; i++) {
      if ((i + 1) % 4 == 0) {
        bufferedWriter.write("\n");
      } else {
        bufferedWriter.write(i+",");
      }
    }
    bufferedWriter.close();
  }

  @Test
  public void testZeroBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("zero-row-batch-copyfrom.txt");
    String tableName = "noBatchSizeTable";
    int totalValidLines = 2500;
    int totalInvalidLines = 500;

    createFileInTmpDir(absFilePath, totalValidLines);
    appendInvalidLines(absFilePath, totalInvalidLines);

    try (Statement statement = connection.createStatement()) {
      // With batch size of 0, it will attempt to copy all rows in a single transaction.
      // But with invalid entries, it will rollback to 0 rows in the table.
      statement.execute(String.format("CREATE TABLE %s (a text, b text, c int, d int)", tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION 0)",
              tableName, absFilePath),
          INVALID_COPY_INPUT_ERROR_MSG);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);

      // Set the batch size to 1 which will copy all valid rows of 2500.
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION 1)",
              tableName, absFilePath),
          INVALID_COPY_INPUT_ERROR_MSG);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalValidLines);
    }
  }

  @Test
  public void testNegativeBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("negative-rows-batch-copyfrom.txt");
    String tableName = "tempTable";
    int totalLines = 20;
    int batchSize = -1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchSize),
          INVALID_BATCH_SIZE_ERROR_MSG);

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testSmallBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-small.txt");
    String tableName = "smallBatchSize";
    int totalLines = 100000;
    int batchSize = 100;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c text, d int)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
          tableName, absFilePath, batchSize));

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testLargeBatchSize() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-large.txt");
    String tableName = "smallBatchSize";
    int totalLines = 100;
    int batchSize = 10000;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a serial primary key, b text, c text, d varchar)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
          tableName, absFilePath, batchSize));

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testStatementLevelTriggersWithBatchCopy() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-with-statement-triggers.txt");
    String tableName = "testAfterTrigger";
    String dummyTableName = "dummyTable";
    int totalLines = 7;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b text, c int, d int)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a int)", dummyTableName));
      statement.execute(String.format(
          "CREATE FUNCTION trigger_fn_on_dummy_table() RETURNS TRIGGER AS $$ BEGIN " +
          "INSERT INTO %s VALUES (1);" +
          "RETURN NEW; END; $$ LANGUAGE 'plpgsql';",
          dummyTableName));

      // test after-statement trigger
      statement.execute(String.format(
          "CREATE TRIGGER trig AFTER INSERT ON %s " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn_on_dummy_table();",
          tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
          tableName, absFilePath, batchSize));
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 1);

      // test before-statement trigger
      statement.execute("DROP TRIGGER trig ON " + tableName);
      statement.execute(String.format(
          "CREATE TRIGGER trig BEFORE INSERT ON %s " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn_on_dummy_table();",
          tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
          tableName, absFilePath, batchSize));
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines * 2);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 2);
    }
  }

  @Test
  public void testStatementLevelTriggersWithBatchCopyFailure() throws Exception {
    String absFilePath = getAbsFilePath("fail-batch-copyfrom-with-statement-triggers.txt");
    String tableName = "testAfterTrigger";
    String dummyTableName = "dummyTable";
    int totalValidLines = 7;
    int totalInvalidLines = 1;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalValidLines);
    appendInvalidLines(absFilePath, totalInvalidLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a serial, b text, c int, d int)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a int)", dummyTableName));
      statement.execute(String.format(
          "CREATE FUNCTION trigger_fn_on_dummy_table() RETURNS TRIGGER AS $$ BEGIN " +
          "INSERT INTO %s VALUES (1);" +
          "RETURN NEW; END; $$ LANGUAGE 'plpgsql';",
          dummyTableName));

      // test after-statement trigger
      statement.execute(String.format(
          "CREATE TRIGGER trig AFTER INSERT ON %s " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn_on_dummy_table();",
          tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchSize),
          INVALID_COPY_INPUT_ERROR_MSG);
      // The copy will happen in one batch, hence none of the rows will be copied
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 0);

      // test before-statement trigger
      statement.execute("DROP TRIGGER trig ON " + tableName);
      statement.execute(String.format(
          "CREATE TRIGGER trig BEFORE INSERT ON %s " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn_on_dummy_table();",
          tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchSize),
          INVALID_COPY_INPUT_ERROR_MSG);
      // The copy will happen in one batch, hence none of the rows will be copied
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 0);
    }
  }

  @Test
  public void testOnTempTable() throws Exception {
    String absFilePath = getAbsFilePath("temp-table-batch-copyfrom.txt");
    String tableName = "tempTable";
    int totalLines = 20;
    int batchSize = 5;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TEMPORARY TABLE %s (a int, b text, c int, d int)", tableName));
      verifyStatementWarning(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchSize),
          INVALID_BATCH_OPTION_USAGE_WARNING_MSG);

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testInsideTransactionBlock() throws Exception {
    String absFilePath = getAbsFilePath("txn-block-batch-copyfrom.txt");
    String copyFromTableName = "batchedTable";
    int totalLines = 20;
    int batchSize = 5;
    String dummyTableName = "sampleTable";

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a int primary key, b text, c int, d text)", dummyTableName));
      statement.execute(String.format(
          "CREATE TABLE %s (a serial, b text primary key, c text, d varchar)", copyFromTableName));
      statement.execute("BEGIN TRANSACTION;");
      statement.execute(String.format("INSERT INTO %s (a, b) VALUES (1, 'val')", dummyTableName));
      verifyStatementWarning(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              copyFromTableName, absFilePath, batchSize),
          INVALID_BATCH_OPTION_USAGE_WARNING_MSG);
      statement.execute("END;");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + copyFromTableName, totalLines);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 1);
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
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d text)", tableName));
      CopyManager copyManager = new CopyManager((BaseConnection) connection);
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
  public void tesStdinCopyInTransactionBlockWithPreviousTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-stdin-txn-block-1.txt");
    String tableName = "stdinTxnBlockBatchSizeTable";
    String dummyTableName = "dummyTable";
    int totalLines = 5;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a text, b int, c int, d int)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a text)", dummyTableName));
      statement.execute("BEGIN TRANSACTION");
      statement.execute(String.format("INSERT INTO %s (a) VALUES ('abc')", dummyTableName));

      CopyManager copyManager = new CopyManager((BaseConnection) connection);
      // falls back to copying all rows in single transaction
      copyManager.copyIn(
          String.format(
              "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, batchSize),
          new BufferedReader(new FileReader(absFilePath)));
      statement.execute("COMMIT TRANSACTION");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 1);
    }
  }

  @Test
  public void tesStdinCopyFailureInTransactionBlockWithPreviousTransaction() throws Exception {
    String absFilePath = getAbsFilePath("fail-batch-copyfrom-stdin-txn-block.txt");
    String tableName = "stdinTxnBlockBatchSizeTable";
    String dummyTableName = "dummyTable";
    int totalValidLines = 7;
    int totalInvalidLines = 1;
    int batchSize = 1;

    createFileInTmpDir(absFilePath, totalValidLines);
    appendInvalidLines(absFilePath, totalInvalidLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d text)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a int)", dummyTableName));
      statement.execute("BEGIN TRANSACTION");
      statement.execute(String.format("INSERT INTO %s (a) VALUES (1)", dummyTableName));

      try {
        CopyManager copyManager = new CopyManager((BaseConnection) connection);
        copyManager.copyIn(
            String.format(
                "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
                tableName, batchSize),
            new BufferedReader(new FileReader(absFilePath)));
        fail(String.format("Statement did not fail with error: %s", INVALID_COPY_INPUT_ERROR_MSG));
      } catch (SQLException e) {
        if (StringUtils.containsIgnoreCase(e.getMessage(), INVALID_COPY_INPUT_ERROR_MSG)) {
          LOG.info("Expected exception", e);
        } else {
          fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
              e.getMessage(), INVALID_COPY_INPUT_ERROR_MSG));
        }
      }
      statement.execute("COMMIT TRANSACTION");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 0);
    }
  }

  @Test
  public void tesStdinCopyInTransactionBlockWithProceedingTransaction() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-stdin-txn-block-2.txt");
    String tableName = "stdinTxnBlockBatchSizeTable";
    String dummyTableName = "dummyTable";
    int totalLines = 5;
    int batchSize = 2;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int, b int, c int, d int)", tableName));
      statement.execute(String.format("CREATE TABLE %s (a int)", dummyTableName));
      statement.execute("BEGIN TRANSACTION");
      CopyManager copyManager = new CopyManager((BaseConnection) connection);
      copyManager.copyIn(
          String.format(
              "COPY %s FROM STDIN WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, batchSize),
          new BufferedReader(new FileReader(absFilePath)));
      statement.execute(String.format("INSERT INTO %s (a) VALUES (1)", dummyTableName));
      statement.execute("COMMIT TRANSACTION");

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + dummyTableName, 1);
    }
  }

  @Test
  public void testBatchTxnSessionVariable() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-sessionvar.txt");
    String tableName = "batchSessionTable";
    int totalValidLines = 7;
    int totalInvalidLines = 1;
    int batchSessionSize = 2;
    // The first three batches of 2 rows will be successfully copied over,
    // since starting from the 8th row, entry is invalid.
    int expectedCopiedLines = batchSessionSize * 3;

    createFileInTmpDir(absFilePath, totalValidLines);
    appendInvalidLines(absFilePath, totalInvalidLines);

    try (Statement statement = connection.createStatement()) {
      // set session variable
      statement.execute("SET " + BATCH_TXN_SESSION_VARIABLE_NAME + "=" + batchSessionSize);
      statement.execute(String.format(
          "CREATE TABLE %s (a Integer, b serial, c varchar, d int)", tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, absFilePath),
          INVALID_COPY_INPUT_ERROR_MSG);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);

      // reset session variable
      statement.execute("RESET " + BATCH_TXN_SESSION_VARIABLE_NAME);
      assertOneRow(statement, "SHOW " + BATCH_TXN_SESSION_VARIABLE_NAME,
          String.valueOf(BATCH_TXN_SESSION_VARIABLE_DEFAULT_ROWS));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, absFilePath),
          INVALID_COPY_INPUT_ERROR_MSG);
      // total rows remain the same since none of the rows copied over
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);

      // set session variable to "default"
      statement.execute("SET " + BATCH_TXN_SESSION_VARIABLE_NAME + " = DEFAULT");
      assertOneRow(statement, "SHOW " + BATCH_TXN_SESSION_VARIABLE_NAME,
          String.valueOf(BATCH_TXN_SESSION_VARIABLE_DEFAULT_ROWS));
    }
  }

  @Test
  public void testNonTxnWriteSessionVariable() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-nontxn-sessionvar.txt");
    String tableName = "nontxnSessionVarTable";
    int totalValidLines = 5;
    int expectedCopiedLines = totalValidLines;

    createFileInTmpDir(absFilePath, totalValidLines);

    try (Statement statement = connection.createStatement()) {
      // ensure non-txn session variable is off by default
      assertOneRow(statement, "SHOW " + DISABLE_TXN_WRITES_SESSION_VARIABLE_NAME, "off");
      // set non-txn session variable
      statement.execute("SET " + DISABLE_TXN_WRITES_SESSION_VARIABLE_NAME + "=true");
      statement.execute(String.format(
          "CREATE TABLE %s (a Integer, b serial, c varchar, d int)", tableName));
      statement.execute(String.format("CREATE INDEX ON %s (d)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, absFilePath));

      // ensure no discrepency in result
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);
      // ensure DDL's, DML's execute without error
      statement.execute("ALTER TABLE " + tableName + " ADD COLUMN e INT");
      statement.execute("ALTER TABLE " + tableName + " DROP COLUMN e");
      statement.execute("INSERT INTO " + tableName + " VALUES (100)");
      statement.execute("TRUNCATE TABLE " + tableName);
      statement.execute("DROP TABLE " + tableName);
      statement.execute("CREATE TABLE " + tableName + " (a int primary key, b text)");
    }
  }

  @Test
  public void testEnableUpsertModeSessionVariable() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-upsertmode-sessionvar.txt");
    String tableName = "upsertModeSessionVarTable";
    int totalValidLines = 5;
    int expectedCopiedLines = totalValidLines;

    createFileInTmpDir(absFilePath, totalValidLines);

    try (Statement statement = connection.createStatement()) {
      // check that yb_enable_upsert_mode session is off by default
      assertOneRow(statement, "SHOW " + YB_ENABLE_UPSERT_MODE_VARIABLE_NAME, "off");

      // set enable-upsert session variable
      statement.execute("SET " + YB_ENABLE_UPSERT_MODE_VARIABLE_NAME + "=true");

      // create and populate table with COPY command
      statement.execute(String.format(
        "CREATE TABLE %s (a INT, b INT, c TEXT, d TEXT)", tableName));
      statement.execute(String.format(
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, absFilePath));

      // check every row was properly inserted into the table
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);

      // verify DDL and DML statements work
      statement.execute("ALTER TABLE " + tableName + " ADD COLUMN e INT");
      statement.execute("ALTER TABLE " + tableName + " DROP COLUMN c");
      statement.execute("INSERT INTO " + tableName + " VALUES (0, 1, 2, 3)");
      statement.execute("TRUNCATE TABLE " + tableName);
      statement.execute("DROP TABLE " + tableName);
      statement.execute("CREATE TABLE " + tableName + " (a INT PRIMARY KEY, b TEXT)");
    }
  }

  @Test
  public void testEnableUpsertModeSessionVariableIndex() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-upsertmode-index-sessionvar.txt");
    String tableName = "upsertModeSessionVarTableIndex";
    int totalValidLines = 5;
    int expectedCopiedLines = totalValidLines;

    createFileInTmpDir(absFilePath, totalValidLines);

    // add CSV line that shares the same primary key value as
    // another row but updates the indexed column b
    writeToFileInTmpDir(absFilePath, "0,5,2,3\n");

    try (Statement statement = connection.createStatement()) {

      // set enable-upsert session variable
      statement.execute("SET " + YB_ENABLE_UPSERT_MODE_VARIABLE_NAME + "=true");

      // create and populate table with COPY command
      // COPY command should not throw error even with the duplicate primary key
      // since upsert mode is ON
      statement.execute(String.format(
        "CREATE TABLE %s (a INT PRIMARY KEY, b INT, c TEXT, d TEXT)", tableName));
      statement.execute(String.format("CREATE INDEX ON %s (b)", tableName));
      statement.execute(String.format(
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, absFilePath));

      // check every row was properly inserted into the table
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);

      // check that row with shared primary key was overwritten
      assertOneRow(statement, String.format("SELECT COUNT(*) FROM %s WHERE b=1", tableName), 1);

      // verify index exists, is being used, and is properly updated
      String explainOutput = getExplainAnalyzeOutput(statement, String.format(
        "SELECT * from %s WHERE b=5;", tableName));
      assertTrue("SELECT query should be an index scan", explainOutput.contains(
        "Index Scan using upsertmodesessionvartableindex_b_idx on upsertmodesessionvartableindex"));
      explainOutput = getExplainAnalyzeOutput(statement, String.format(
        "SELECT * from %s WHERE b=5;", tableName));
      assertTrue("Expect to fetch 2 rows from index scan",
                 explainOutput.contains("rows=2 loops=1)"));
    }
  }

  @Test
  public void testEnableUpsertModeSessionVariableForeignKey() throws Exception {
    String foreignKeyFilePath = getAbsFilePath("batch-copyfrom-upsertmode-fk-sessionvar.txt");
    String primaryKeyTableName = "upsertModeSessionVarTablePk";
    String foreignKeyTableName = "upsertModeSessionVarTableFk";
    int expectedPrimaryKeyLines = 20;
    int expectedForeignKeyLines = 5;

    // create tmp CSV file with header
    createFileInTmpDir(foreignKeyFilePath, expectedForeignKeyLines);

    // add CSV line that shares the same primary key as
    // another row but updates the foreign key column
    writeToFileInTmpDir(foreignKeyFilePath, "0,2,3,4\n");

    try (Statement statement = connection.createStatement()) {

      // set enable-upsert session variable
      statement.execute("SET " + YB_ENABLE_UPSERT_MODE_VARIABLE_NAME + "=true");

      // create and populate primary key table with COPY command
      statement.execute(String.format(
        "CREATE TABLE %s (a INT PRIMARY KEY, b text)", primaryKeyTableName));
      for (int i = 0; i < expectedPrimaryKeyLines; i++) {
        statement.execute(String.format(
          "INSERT INTO %s VALUES (%d, %s)", primaryKeyTableName, i, i+1));
      }
      // check every row was properly inserted into the table
      assertOneRow(statement, "SELECT COUNT(*) FROM " + primaryKeyTableName,
        expectedPrimaryKeyLines);

      // create and bulk load the foreign key table
      statement.execute(String.format(
        "CREATE TABLE %s (a INT PRIMARY KEY, b INT REFERENCES %s, c TEXT, d TEXT)",
          foreignKeyTableName, primaryKeyTableName));
      statement.execute(String.format(
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", foreignKeyTableName, foreignKeyFilePath));
      // check every row was properly inserted into the table
      assertOneRow(statement, "SELECT COUNT(*) FROM " + foreignKeyTableName,
        expectedForeignKeyLines);

      // verify foreign key was updated
      assertOneRow(statement, String.format(
        "SELECT COUNT(*) FROM %s WHERE b=2", foreignKeyTableName), 1);

      // clear table to test scenario where FK constraint is violated
      statement.execute(String.format("TRUNCATE TABLE %s", foreignKeyTableName));

      // add CSV row with foreign key that points to a non-existent ID
      writeToFileInTmpDir(foreignKeyFilePath, "0,-1,3,4\n");

      // validate invalid FK is caught
      runInvalidQuery(statement,
        String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", foreignKeyTableName, foreignKeyFilePath),
          INVALID_FOREIGN_KEY_ERROR_MSG);

      // no rows should have been copied
      assertOneRow(statement, String.format("SELECT COUNT(*) FROM %s", foreignKeyTableName), 0);
    }
  }

  @Test
  public void testEnableUpsertModeSessionVariableTrigger() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-upsertmode-sessionvar.txt");
    String tableName = "upsertModeSessionVarTable";
    int totalValidLines = 5;
    int expectedCopiedLines = totalValidLines;

    createFileInTmpDir(absFilePath, totalValidLines);

    try (Statement statement = connection.createStatement()) {
      // create and populate table with COPY command
      statement.execute(String.format(
        "CREATE TABLE %s (a INT, b INT, c TEXT, d TEXT)", tableName));

      // create before and after triggers on table
      statement.execute(
        "CREATE FUNCTION trigger_func() RETURNS trigger LANGUAGE plpgsql AS \'" +
        "BEGIN " +
        " RAISE NOTICE \'\'trigger_func(%) called: action = %, when = %, level = %\'\', " +
        " TG_ARGV[0], TG_OP, TG_WHEN, TG_LEVEL; " +
        "RETURN NULL; " +
        "END;\';");
      statement.execute(String.format(
        "CREATE TRIGGER before_ins_stmt_trig BEFORE INSERT ON %s " +
        "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_func(\'before_ins_stmt\');", tableName));
      statement.execute(String.format(
        "CREATE TRIGGER after_ins_stmt_trig AFTER INSERT ON %s " +
        "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_func(\'after_ins_stmt\');", tableName));

      // verify batching is disabled if the table contains a non-FK trigger
      verifyStatementWarnings(
        statement,
        String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, absFilePath),
        Arrays.asList(
          "Batched COPY is not supported on table with non RI trigger. " +
            "Defaulting to using one transaction for the entire copy.",
          "trigger_func(before_ins_stmt) called: action = INSERT, when = BEFORE, level = STATEMENT",
          "trigger_func(after_ins_stmt) called: action = INSERT, when = AFTER, level = STATEMENT"));

      // check every row was properly inserted into the table
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);
    }
  }

  @Test
  public void testSessionVariableWithRowsPerTransactionOption() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-sessionvar-with-query-option.txt");
    String tableName = "batchSessionAndOptionTable";
    int totalValidLines = 7;
    int totalInvalidLines = 3;
    int batchSessionSize = 1;
    int batchOptionSize = 4;
    // Only the first batch of 4 rows will be successfully copied over
    // since starting from the 8th row, entry is invalid.
    int expectedCopiedLines = batchOptionSize;

    createFileInTmpDir(absFilePath, totalValidLines);
    appendInvalidLines(absFilePath, totalInvalidLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute("SET " + BATCH_TXN_SESSION_VARIABLE_NAME + "=" + batchSessionSize);
      statement.execute(String.format(
          "CREATE TABLE %s (a serial, b text, c varchar, d Integer)", tableName));
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)",
              tableName, absFilePath, batchOptionSize),
          INVALID_COPY_INPUT_ERROR_MSG);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, expectedCopiedLines);
    }
  }

  @Test
  public void testBatchedCopyForPartitionedTables() throws Exception {
    String absFilePath = getAbsFilePath("batch-copyfrom-parts.txt");
    String tableName = "parts";
    int totalLines = 100000;
    int batchSize = 100;

    createFileInTmpDir(absFilePath, totalLines);

    final String createStmt = "CREATE TABLE %s (a int, b int, c text, d int) PARTITION BY %s(a)";
    final String createDefaultStmt = "CREATE TABLE %s PARTITION OF %s DEFAULT";
    final String copyStmt =
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %s)";

    try (Statement statement = connection.createStatement()) {
      String[] partTypes = {"HASH", "RANGE", "LIST"};
      for (final String partType : partTypes) {
        final String parentName = tableName + partType;
        // Create partitioned table.
        statement.execute(String.format(createStmt, parentName, partType));

        // Create partitions for hash partitioned table, and a default partition for range/list.
        if (partType.equals("HASH")) {
          statement.execute(String.format("CREATE TABLE %s PARTITION OF %s FOR VALUES WITH " +
              "(modulus 2, remainder 0)", parentName + "_part1", parentName));
          statement.execute(String.format("CREATE TABLE %s PARTITION OF %s FOR VALUES WITH " +
              "(modulus 2, remainder 1)", parentName + "_part2", parentName));
        } else {
          statement.execute(String.format(createDefaultStmt, parentName + "_default", parentName));
        }

        // Copy rows over to the partitioned table
        statement.execute(String.format(copyStmt, parentName, absFilePath, batchSize));

        // Verify the copy went through.
        assertOneRow(statement, "SELECT COUNT(*) FROM " + parentName, totalLines);
      }
    }
  }

  @Test
  public void testBatchedCopyValidForeignKeyCheck() throws Exception {
    String absFilePath = getAbsFilePath("fk-copyfrom.txt");
    String refTableName = "reftable_ok";
    String tableName = "maintable_ok";

    int totalLines = 100;
    int batchSize = totalLines;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      // Both reference table and main table have the same key set from 0 to totalLines - 1.
      statement.execute(String.format("CREATE TABLE %s (a INT PRIMARY KEY)", refTableName));
      statement.execute(String.format(
          "INSERT INTO %s (a) SELECT s * 4 FROM GENERATE_SERIES (0, %d) AS s",
          refTableName, totalLines - 1));

      statement.execute(
          String.format("CREATE TABLE %s (a INT REFERENCES %s, b INT, c INT, d INT)",
                        tableName, refTableName));
      statement.execute(
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %d)",
                        tableName, absFilePath, batchSize));

      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testBatchedCopyFailedOnForeignKeyCheck() throws Exception {
    String absFilePath = getAbsFilePath("fk-copyfrom-all-failure.txt");
    String refTableName = "reftable_failed";
    String tableName = "maintable_failed";

    int totalLines = 100;
    int batchSize = totalLines;
    String referenceKey = "a_fkey";

    createFileInTmpDir(absFilePath, totalLines);

    String INVALID_FOREIGN_KEY_CHECK_ERROR_MSG =
        String.format("insert or update on table \"%s\" violates foreign key constraint \"%s_%s\"",
                      tableName, tableName, referenceKey);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a INT PRIMARY KEY)", refTableName));
      // Create reference table without the (a = 0) line.
      statement.execute(String.format(
          "INSERT INTO %s (a) SELECT s * 4 FROM GENERATE_SERIES (1, %d) AS s",
          refTableName, totalLines - 1));

      statement.execute(
          String.format("CREATE TABLE %s (a INT REFERENCES %s, b INT, c INT, d INT)",
                        tableName, refTableName));

      // The execution will fail since the (a = 0) key is not present in the reference table.
      runInvalidQuery(statement,
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %d)",
                        tableName, absFilePath, batchSize),
          INVALID_FOREIGN_KEY_CHECK_ERROR_MSG);

      // No rows should be copied.
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testBatchedCopyPartialFailureOnForeignKeyCheck() throws Exception {
    String absFilePath = getAbsFilePath("fk-copyfrom-partial-failure.txt");
    String refTableName = "reftable_partial";
    String tableName = "maintable_partial";

    int totalLines = 100;
    int batchSize = 1;
    String referenceKey = "a_fkey";

    createFileInTmpDir(absFilePath, totalLines);

    String INVALID_FOREIGN_KEY_CHECK_ERROR_MSG =
        String.format("insert or update on table \"%s\" violates foreign key constraint \"%s_%s\"",
                      tableName, tableName, referenceKey);

    try (Statement statement = connection.createStatement()) {
      // Create reference table with half of the lines.
      statement.execute(String.format("CREATE TABLE %s (a INT PRIMARY KEY)", refTableName));
      statement.execute(String.format(
          "INSERT INTO %s (a) SELECT s * 4 FROM GENERATE_SERIES (0, %d) AS s",
          refTableName, totalLines/2 - 1));

      statement.execute(
          String.format("CREATE TABLE %s (a INT REFERENCES %s, b INT, c INT, d INT)",
                        tableName, refTableName));

      // The execution will throw error since the later half is not present in the reference table.
      runInvalidQuery(statement,
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %d)",
                        tableName, absFilePath, batchSize),
          INVALID_FOREIGN_KEY_CHECK_ERROR_MSG);

      // However, we should be able to copy up to totalLines / 2 lines
      // that are present in the reference table.
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines / 2);
    }
  }

  @Test
  public void testBatchedCopyManualTrigger() throws Exception {
    String absFilePath = getAbsFilePath("manual-trigger.txt");
    String tableName = "manual_trigger_table";

    int totalLines = 100;

    // The batch size will be ignored, since there is a manually created trigger.
    int batchSize = 5;
    String INVALID_PRIMARY_KEY_TRIGGER_ERROR_MSG = "Primary key too large";

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(
          String.format("CREATE TABLE %s (a INT PRIMARY KEY, b INT, c INT, d INT)", tableName));

      // This trigger will fire since the row will eventually exceed the limit.
      statement.execute(
          String.format(
              "CREATE FUNCTION log_a() RETURNS TRIGGER AS $$ " +
              "BEGIN " +
              "IF (NEW.a > %d) THEN RAISE EXCEPTION '%s'; " +
              "END IF; " +
              "RETURN NEW; " +
              "END; " +
              "$$ LANGUAGE plpgsql;", totalLines / 2, INVALID_PRIMARY_KEY_TRIGGER_ERROR_MSG));

      statement.execute(
          String.format(
              "CREATE TRIGGER mytrigger BEFORE INSERT OR UPDATE ON %s " +
              "FOR EACH ROW EXECUTE PROCEDURE log_a()", tableName));

      // The execution will throw error on the primary key trigger.
      runInvalidQuery(statement,
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %d)",
                        tableName, absFilePath, batchSize),
          INVALID_PRIMARY_KEY_TRIGGER_ERROR_MSG);

      // We should roll back all the changes.
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);
    }
  }

  @Test
  public void testBatchCopySkipSomeRows() throws Exception {
    String absFilePath = getAbsFilePath("skip-rows-valid-some.txt");
    String tableName = "skiprows_some";
    int totalLines = 100;
    int skippedLines = 10;
    int expectedCopiedLines = totalLines - skippedLines;

    createFileInTmpDir(absFilePath, totalLines);
    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a Integer, b serial, c varchar, d int)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, SKIP %s)",
          tableName, absFilePath, skippedLines));

      // Expecting totalLines - skippedLines will be copied.
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName),
                   expectedCopiedLines);
    }
  }

  @Test
  public void testBatchCopySkipAllRows() throws Exception {
    String absFilePath = getAbsFilePath("skip-rows-valid-all.txt");
    String tableName = "skiprows_all";
    int totalLines = 100;
    int skippedLines = 1000;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a Integer, b serial, c varchar, d int)", tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, SKIP %s)",
          tableName, absFilePath, skippedLines));

      // Since skippedLines > totalLines, we expect 0 rows will be copied.
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), 0);
    }
  }

  @Test
  public void testBatchCopyInvalidSkipRowsParameter() throws Exception {
    String absFilePath = getAbsFilePath("skip-rows-invalid.txt");
    String tableName = "skiprow_invalid";
    int totalLines = 100;
    int skippedLines = -1;

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a Integer, b serial, c varchar, d int)", tableName));
      // Copy would fail since skippedLines is an invalid value.
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, SKIP %s)",
              tableName, absFilePath, skippedLines),
          INVALID_NUM_SKIPPED_ROWS_ERROR_MSG);

      // No rows will be copied.
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), 0);
    }
  }

  @Test
  public void testBatchCopyInvalidLinesAmongSkipped() throws Exception {
    // If invalid lines appear among the rows being skipped, we should skip them.
    String absFilePath = getAbsFilePath("skip-rows-invalid-lines-among-skipped.txt");
    String tableName = "skiprow_invalid_lines";
    int totalInvalidLines = 10;
    int totalValidLines = 5;
    int skippedLines = totalInvalidLines;

    createFileInTmpDir(absFilePath, 0);
    appendInvalidLines(absFilePath, totalInvalidLines);
    appendValidLines(absFilePath, totalValidLines, 0);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
          "CREATE TABLE %s (a Integer, b serial, c varchar, d int)", tableName));

      // If we don't skip invalid lines, error will be thrown upon reading through such lines.
      runInvalidQuery(statement,
          String.format(
              "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)",
              tableName, absFilePath, skippedLines),
          INVALID_COPY_INPUT_ERROR_MSG);
      // No rows will be copied.
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), 0);

      // If we skip through the invalid lines, the copy will succeed.
      statement.execute(String.format(
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, SKIP %s)",
        tableName, absFilePath, skippedLines));

      // Only the valid lines will be copied.
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), totalValidLines);
    }
  }

  @Test
  public void testBatchCopySkipRICheck() throws Exception {
    // One will not execute Before/After row trigger for skipped lines. In particular,
    // we will not do Referential Integrity check for such lines.
    String absFilePath = getAbsFilePath("skip-rows-ri-check.txt");
    String tableName = "skiprow_ri_check";
    String refTableName = "skiprow_ri_check_maintable";

    int totalLines = 100;
    int skippedLines = 10;

    String referenceKey = "a_fkey";
    String INVALID_FOREIGN_KEY_CHECK_ERROR_MSG =
        String.format("insert or update on table \"%s\" violates foreign key constraint \"%s_%s\"",
                      tableName, tableName, referenceKey);

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      // Create reference table without the keys among the skipped rows.
      statement.execute(String.format("CREATE TABLE %s (a INT PRIMARY KEY)", refTableName));
      statement.execute(String.format(
          "INSERT INTO %s (a) SELECT s * 4 FROM GENERATE_SERIES (%d, %d) AS s",
          refTableName, skippedLines, totalLines - 1));

      statement.execute(
          String.format("CREATE TABLE %s (a INT REFERENCES %s, b INT, c INT, d INT)",
                        tableName, refTableName));

      // Copying all lines would fail as some of the rows fail the RI check.
      runInvalidQuery(statement,
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)",
                        tableName, absFilePath),
          INVALID_FOREIGN_KEY_CHECK_ERROR_MSG);
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), 0);

      // But after skipping such lines, the copy would succeed.
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, SKIP %s)",
          tableName, absFilePath, skippedLines));
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), totalLines - skippedLines);
    }
  }

  @Test
  public void testBatchCopyMixSkipLinesAndRowsPerTransaction() throws Exception {
    // In this case, we create a reference table containing keys 2, 3, 4, and attempt
    // to copy from a file containing keys from 0 to 5. We specify SKIP = 2
    // and ROWS_PER_TRANSACTION = 3. The copy logic will skip the first two rows, then
    // batch the next three rows in a write batch, then try to copy the last row but fail,
    // resulting to the resulting table containing three rows.
    String absFilePath = getAbsFilePath("skip-rows-mix-skip-lines-and-rps.txt");
    String tableName = "skiprow_mixed";
    String refTableName = "skiprow_mixed_maintable";

    int totalLines = 6;
    int skippedLines = 2;
    int batchSize = 3;

    String referenceKey = "a_fkey";
    String INVALID_FOREIGN_KEY_CHECK_ERROR_MSG =
        String.format("insert or update on table \"%s\" violates foreign key constraint \"%s_%s\"",
                      tableName, tableName, referenceKey);

    createFileInTmpDir(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      // Create reference table without the keys among the skipped tuples, and without the last key
      // of the input file. This is to trigger the failure when copying the file to the table.
      statement.execute(String.format("CREATE TABLE %s (a INT PRIMARY KEY)", refTableName));
      statement.execute(String.format(
          "INSERT INTO %s (a) SELECT s * 4 FROM GENERATE_SERIES (%d, %d) AS s",
          refTableName, skippedLines, totalLines - 2));

      statement.execute(
          String.format("CREATE TABLE %s (a INT REFERENCES %s, b INT, c INT, d INT)",
                        tableName, refTableName));

      // Issue a copy with both SKIP and ROW_PER_TRANSACTION option. The copy would fail as the
      // last row fails the RI check.
      runInvalidQuery(statement,
          String.format(
            "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION %d, SKIP %d)",
            tableName, absFilePath, batchSize, skippedLines),
          INVALID_FOREIGN_KEY_CHECK_ERROR_MSG);
      // But batchSize rows would remain in the table.
      assertOneRow(statement,
                   String.format("SELECT COUNT(*) FROM %s", tableName), batchSize);
    }
  }

  @Test
  public void testBatchedCopyDisableFKCheck() throws Exception {
    String absFilePath = getAbsFilePath("disable-fk-check.txt");
    String tableName = "maintable_disable_fk";
    String refTableName = "reftable_disable_fk";

    int totalLines = 100;
    String referenceKey = "a_fkey";

    createFileInTmpDir(absFilePath, totalLines);

    String INVALID_FOREIGN_KEY_CHECK_ERROR_MSG =
        String.format("insert or update on table \"%s\" violates foreign key constraint \"%s_%s\"",
                      tableName, tableName, referenceKey);

    try (Statement statement = connection.createStatement()) {
      // Create reference table without any data.
      statement.execute(String.format("CREATE TABLE %s (a INT PRIMARY KEY)", refTableName));

      statement.execute(
          String.format("CREATE TABLE %s (a INT REFERENCES %s, b INT, c INT, d INT)",
                        tableName, refTableName));

      // The execution will fail since the none of the key is present in the reference table.
      runInvalidQuery(statement,
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)",
                        tableName, absFilePath),
          INVALID_FOREIGN_KEY_CHECK_ERROR_MSG);

      // No rows should be copied.
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, 0);

      // Executing the copy with DISABLE_FK_CHECK should succeed.
      statement.execute(
          String.format("COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, DISABLE_FK_CHECK)",
          tableName, absFilePath));

      // All the rows will be copied.
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);
    }
  }

  @Test
  public void testBatchedCopWithReplaceOptions() throws Exception {
    String oldFilePath = getAbsFilePath("copy-with-replace-old.txt");
    String newFilePath = getAbsFilePath("copy-with-replace-new.txt");

    String tableName = "copy_with_replace";
    int totalLines = 100;
    createFileInTmpDir(oldFilePath, totalLines / 2);
    createFileInTmpDir(newFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(
          String.format("CREATE TABLE %s (a INT PRIMARY KEY, b INT, c INT, d INT)", tableName));

      // Copy the old file's content to the table.
      statement.execute(String.format(
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)", tableName, oldFilePath));

      assertOneRow(statement, String.format("SELECT COUNT(*) FROM %s", tableName), totalLines / 2);

      // Now copy the new file's content to the table with the REPLACE option.
      statement.execute(String.format(
        "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER, REPLACE)", tableName, newFilePath));

      assertOneRow(statement, String.format("SELECT COUNT(*) FROM %s", tableName), totalLines);
    }
  }
}
