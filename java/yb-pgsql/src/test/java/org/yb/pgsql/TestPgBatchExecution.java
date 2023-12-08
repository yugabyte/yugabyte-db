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

import java.sql.Statement;

import static org.yb.AssertionWrappers.fail;

import java.sql.BatchUpdateException;
import java.sql.PreparedStatement;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.junit.runners.Parameterized;
import org.yb.YBParameterizedTestRunner;

import java.util.Arrays;
import java.util.List;

@RunWith(value = YBParameterizedTestRunner.class)
public class TestPgBatchExecution  extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgBatchExecution.class);
  private static final String TABLE_NAME = "t";
  private final String gucBatchExecutionsHandlingOptions;

  public TestPgBatchExecution(String batchExecutionsHandlingOptions) {
    gucBatchExecutionsHandlingOptions = batchExecutionsHandlingOptions;
  }

  // Run each test with peeking to detect batch execution and assuming all executions are batched.
  @Parameterized.Parameters
  public static List<String> batchExecutionHandlingOptions() {
    return Arrays.asList("detect_by_peeking",
                         "assume_all_batch_executions");
  }

  private void insertValues(int count) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < count; ++i) {
        stmt.execute(String.format("INSERT INTO %s VALUES (%d)", TABLE_NAME, i));
      }
    }
  }

  private void expectRowCount(String table_name, int expectation) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      assertOneRow(stmt, "SELECT COUNT(*) FROM " + table_name, expectation);
    }
  }

  private void expectRowCount(int expectation) throws Exception {
    expectRowCount(TABLE_NAME, expectation);
  }

  private void expectRowCountWithinRange(int range_start,
                                         int range_end,
                                         int expectation)  throws Exception {
    try (Statement stmt = connection.createStatement()) {
      assertOneRow(stmt,
                   String.format("SELECT COUNT(*) FROM %s WHERE i >= %d and i <= %d",
                                 TABLE_NAME, range_start, range_end),
                   expectation);
    }
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("SET yb_pg_batch_detection_mechanism=%s",
                                 gucBatchExecutionsHandlingOptions));
      stmt.execute(String.format("CREATE TABLE %s (i INT PRIMARY KEY)", TABLE_NAME));
    }
  }

  @Test
  public void testBatchDetectionMethod() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      assertOneRow(stmt,
                   "SHOW yb_pg_batch_detection_mechanism",
                   gucBatchExecutionsHandlingOptions);
    }
  }

  @Test
  public void testBatchInsert() throws Exception {
    // Batch Insert
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < 5; ++i) {
        stmt.addBatch(String.format("INSERT INTO %s VALUES (%d)", TABLE_NAME, i));
      }
      stmt.executeBatch();
    }
    expectRowCount(5);

    // Batch Insert using Prepared Statements
    try (PreparedStatement pstmt = connection.prepareStatement(
        String.format("INSERT INTO %s VALUES (?)", TABLE_NAME))) {
      for (int i = 5; i < 10; ++i) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
    }
    expectRowCount(10);
  }

  @Test
  public void testBatchInsertWithError() throws Exception {
    // Batch insert leading to UniqueContraintViolation exception and rollback
    try (Statement stmt = connection.createStatement()) {
      insertValues(5);

      for (int i : new int[]{5, 6, 7, 7, 8, 9}) {
        stmt.addBatch(String.format("INSERT INTO %s VALUES (%d)", TABLE_NAME, i));
      }
      stmt.executeBatch();
      fail("Batch operation expected to fail");
    } catch(BatchUpdateException e) {
    }
    // Entire batch should be reverted.
    expectRowCount(5);
  }

  @Test
  public void testBatchPreparedInsertWithError() throws Exception {
    // Batch insert using Prepared Statement leading to
    // UniqueContraintViolation exception and rollback
    try {
      insertValues(5);

      PreparedStatement pstmt = connection.prepareStatement(
          String.format("INSERT INTO %s VALUES (?)", TABLE_NAME));
      for (int i : new int[]{5, 6, 7, 7, 8, 9}) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
      fail("Batch operation expected to fail");
    } catch(BatchUpdateException e) {
    }
    // Entire batch should be reverted.
    expectRowCount(5);
  }

  @Test
  public void testBatchUpdate() throws Exception {
    // Batch Update
    insertValues(5);
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < 5; ++i) {
        stmt.addBatch(String.format("UPDATE %s SET i = i + 5 WHERE i = %d", TABLE_NAME, i));
      }
      stmt.executeBatch();
    }
    expectRowCountWithinRange(5, 9, 5);

    // Batch Update using Prepared Statements
    try (PreparedStatement pstmt = connection.prepareStatement(
        String.format("UPDATE %s SET i = i + 5 WHERE i = ?", TABLE_NAME))) {
      for (int i = 5; i < 10; ++i) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
    }
    expectRowCountWithinRange(10, 14, 5);
  }

  @Test
  public void testBatchUpdateWithError() throws Exception {
    // Batch update leading to UniqueContraintViolation exception and rollback
    try (Statement stmt = connection.createStatement()) {
      insertValues(5);

      for (int i = 3; i >= 0; i--) {
        stmt.addBatch(String.format("UPDATE %s SET i = i + 2 WHERE i = $d", TABLE_NAME, i));
      }
      stmt.executeBatch();
      fail("Batch operation expected to fail");
    } catch(BatchUpdateException e) {
    }
    // Entire batch should be reverted.
    expectRowCountWithinRange(0, 4, 5);
  }

  @Test
  public void testBatchPreparedUpdateWithError() throws Exception {
    // Batch update using Prepared Statement leading to
    // UniqueContraintViolation exception and rollback
    try {
      insertValues(5);

      PreparedStatement pstmt = connection.prepareStatement(
          String.format("UPDATE %s SET i = i + 2 WHERE i = ?", TABLE_NAME));
      for (int i = 3; i >= 0; i--) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
      fail("Batch operation expected to fail");
    } catch(BatchUpdateException e) {
    }
    // Entire batch should be reverted.
    expectRowCountWithinRange(0, 4, 5);
  }

  @Test
  public void testBatchDelete() throws Exception {
    // Batch Delete
    insertValues(5);
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < 5; ++i) {
        stmt.addBatch(String.format("DELETE FROM %s WHERE i=%d", TABLE_NAME, i));
      }
      stmt.executeBatch();
    }
    expectRowCount(0);

    // Batch Delete using Prepared Statements
    insertValues(5);
    try (PreparedStatement pstmt = connection.prepareStatement(
        String.format("DELETE FROM %s WHERE i=?", TABLE_NAME))) {
      for (int i = 0; i < 5; ++i) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
    }
    expectRowCount(0);

    // Batch delete, making reuse of a prepared statement.
    insertValues(5);
    try (PreparedStatement pstmt = connection.prepareStatement(
        String.format("DELETE FROM %s WHERE i=?", TABLE_NAME))) {
      for (int i = 0; i < 2; ++i) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
      expectRowCount(3);

      for (int i = 2; i < 5; ++i) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
    }
    expectRowCount(0);
  }

  @Test
  public void testBatchDeleteWithErrors() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (id int PRIMARY KEY)");
      stmt.execute(
          "CREATE TABLE t2 (id  int, t1_id int, PRIMARY KEY (id), FOREIGN KEY " +
          "(t1_id) REFERENCES t1(id))");
      for (int i = 0; i < 5; ++i) {
        stmt.execute(String.format("INSERT INTO t1 VALUES (%d)", i));
      }
      for (int i = 2; i < 5; ++i) {
        stmt.execute(String.format("INSERT INTO t2 VALUES (%d, %d)", i, i));
      }
    }

    // Batch Delete from t1 causing Foreiign Key Constraint Violation
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < 5; ++i) {
        stmt.addBatch(String.format("DELETE FROM t1 WHERE i=%d", i));
      }
      stmt.executeBatch();
      fail("Batch operation expected to fail");
    } catch(BatchUpdateException e) {
    }
    // Entire batch should be reverted.
    try (Statement stmt = connection.createStatement()) {
      expectRowCount("t1", 5);
      expectRowCount("t2", 3);
    }
  }

  @Test
  public void testBatchPreparedDeleteWithErrors() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (id int PRIMARY KEY)");
      stmt.execute("CREATE TABLE t2 (id  int, t1_id int, PRIMARY KEY (id), FOREIGN KEY " +
          "(t1_id) REFERENCES t1(id))");
      for (int i = 0; i < 5; ++i) {
        stmt.execute(String.format("INSERT INTO t1 VALUES (%d)", i));
      }
      for (int i = 2; i < 5; ++i) {
        stmt.execute(String.format("INSERT INTO t2 VALUES (%d, %d)", i, i));
      }
    }

    // Batch Delete using Prepared Statements
    // from t1 causing Foreign Key Constraint Violation
    try (PreparedStatement pstmt = connection.prepareStatement(
        "DELETE FROM t1 WHERE i=?")) {
      for (int i = 0; i < 5; ++i) {
        pstmt.setInt(1, i);
        pstmt.addBatch();
      }
      pstmt.executeBatch();
      fail("Batch operation expected to fail");
    } catch(BatchUpdateException e) {
    }
    try (Statement stmt = connection.createStatement()) {
      expectRowCount("t1", 5);
      expectRowCount("t2", 3);
    }
  }

  @Test
  public void testBatchPreparedDeleteAndUpdate() throws Exception {
    insertValues(5);
    try {
      PreparedStatement pstmtDelete = connection.prepareStatement(
          String.format("DELETE FROM %s WHERE i=?", TABLE_NAME));
      for (int i = 0; i < 2; ++i) {
        pstmtDelete.setInt(1, i);
        pstmtDelete.addBatch();
      }
      pstmtDelete.executeBatch();

      PreparedStatement pstmtUpdate = connection.prepareStatement(
          String.format("UPDATE %s SET i=i+3 WHERE i=?", TABLE_NAME));
      for (int i = 2; i < 5; ++i) {
        pstmtUpdate.setInt(1, i);
        pstmtUpdate.addBatch();
      }
      pstmtUpdate.executeBatch();

      for (int i = 5; i < 8; ++i) {
        pstmtDelete.setInt(1, i);
        pstmtDelete.addBatch();
      }
      pstmtDelete.executeBatch();
    } catch (Exception e) {
      LOG.error("Unexpected exception", e);
    }
    expectRowCount(0);
  }
}
