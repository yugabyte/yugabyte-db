// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.fail;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThan;

import java.sql.Connection;
import java.sql.Statement;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.UUID;

import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestSchemaVersionMismatch extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSchemaVersionMismatch.class);
  private static final String uuid = "00c10076-6ee5-4381-98f9-d2570408905b";
  private final AtomicBoolean testFailed = new AtomicBoolean(false);
  private AtomicInteger numExpectedErrors = new AtomicInteger(0);

  // Use pg_sleep(1) to increase the chances of hitting a schema version mismatch.
  // This increases the likelihood of the DDL thread bumping the schema version
  // before the DML threads reads from the table.
  private static final String updateSqlTemplate =
      "WITH updated AS (" +
      "  UPDATE todo " +
      "  SET status = false " +
      "  WHERE id = %s " +
      "  RETURNING * " +
      ") " +
      "SELECT pg_sleep(1), * FROM updated;";
  
  private static final String updateSqlWithPlaceholder = String.format(updateSqlTemplate, "?");
  private static final String updateSqlWithUuid = String.format(updateSqlTemplate, "'" + uuid + "'");

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // The test expects schema version mismatch errors admist processing of DDLs, most of which
    // are eliminated with object locking. Hence, disable the feature for the test.
    flagMap.put("enable_object_locking_for_table_locks", "false");
    return flagMap;
  }

  /**
   * Helper method to setup the test table with initial data.
   */
  private void setupTestTable(Statement stmt) throws SQLException {
    stmt.execute("DROP TABLE IF EXISTS todo;");
    stmt.execute("CREATE TABLE todo (" +
        "    id         uuid PRIMARY KEY," +
        "    task       VARCHAR(255)," +
        "    status     boolean," +
        "    created_at timestamptz default now()" +
        ");");

    // Insert test data
    StringBuilder insertBuilder = new StringBuilder();
    insertBuilder.append("INSERT INTO todo (id, task, status) VALUES ");
    
    // Add the row that will be updated in the test first
    insertBuilder.append(String.format("('%s', 'task 1', false)", uuid));
    
    // Add additional random test data
    for (int i = 0; i < 100; i++) {
      insertBuilder.append(",");
      insertBuilder.append(String.format("('%s', 'task %d', %b)", UUID.randomUUID().toString(),
          i + 2, i % 2 == 0));
    }
    insertBuilder.append(";");
    stmt.execute(insertBuilder.toString());
  }

  @Test
  public void testBasic() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      setupTestTable(stmt);
    }

    // Set the preferred query mode to simple so that the simple protocol is used
    // for one of the DML threads. We don't care which protocol is used for the
    // DDL thread.
    try (Connection simpleModeConn = getConnectionBuilder().withPreferQueryMode("simple").connect();
        Connection extendedModeConn = getConnectionBuilder().connect();
        Connection ddlConn = getConnectionBuilder().connect();
        Statement simpleStmt = simpleModeConn.createStatement();
        Statement ddlStmt = ddlConn.createStatement()) {

      final long duration_ms = 30000;

      Thread simpleDmlThread = createDmlThread(simpleStmt, updateSqlWithUuid, duration_ms);
      Thread extendedDmlThread =
          createExtendedDmlThread(extendedModeConn, updateSqlWithPlaceholder, duration_ms, false);
      Thread extendedDmlThreadWithRetry =
          createExtendedDmlThreadWithRetry(extendedModeConn, updateSqlWithPlaceholder, duration_ms);
      Thread ddlThread = createDdlThread(ddlStmt, duration_ms);

      simpleDmlThread.start();
      extendedDmlThread.start();
      extendedDmlThreadWithRetry.start();
      ddlThread.start();

      simpleDmlThread.join();
      extendedDmlThread.join();
      extendedDmlThreadWithRetry.join();
      ddlThread.join();

      assertFalse("Test failed due to unexpected error code", testFailed.get());
      assertGreaterThan("Expected at least one schema version mismatch error", numExpectedErrors.get(), 0);
    }
  }

  @Test
  public void testWithExplicitTxn() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      setupTestTable(stmt);
    }

    try (Connection simpleModeConn = getConnectionBuilder().withPreferQueryMode("simple").connect();
        Connection extendedModeConn = getConnectionBuilder().connect();
        Connection ddlConn = getConnectionBuilder().connect();
        Statement simpleStmt = simpleModeConn.createStatement();
        Statement ddlStmt = ddlConn.createStatement()) {

      final long duration_ms = 30000;

      Thread simpleDmlThread = createDmlThread(simpleStmt, updateSqlWithUuid, duration_ms);
      Thread extendedDmlThread =
          createExtendedDmlThread(extendedModeConn, updateSqlWithPlaceholder, duration_ms, true);
      Thread extendedDmlThreadWithRetry =
          createExtendedDmlThreadWithRetry(extendedModeConn, updateSqlWithPlaceholder, duration_ms);
      Thread ddlThread = createDdlThread(ddlStmt, duration_ms);

      simpleDmlThread.start();
      extendedDmlThread.start();
      extendedDmlThreadWithRetry.start();
      ddlThread.start();

      simpleDmlThread.join();
      extendedDmlThread.join();
      extendedDmlThreadWithRetry.join();
      ddlThread.join();

      assertFalse("Test failed due to unexpected error code", testFailed.get());
      assertGreaterThan("Expected at least one schema version mismatch error", numExpectedErrors.get(), 0);
    }
  }

  /**
   * Creates a DML thread that runs the given DML query (using the simple protocol) for the given
   * duration.
   */
  private Thread createDmlThread(Statement stmt, String sql, long duration) {
    return new Thread(() -> {
      long endTime = System.currentTimeMillis() + duration;
      while (System.currentTimeMillis() < endTime) {
        try {
          stmt.execute(String.format(sql, "'" + uuid + "'"));
        } catch (SQLException e) {
          processError("DML", e);
        }
      }
    });
  }

  /**
   * Same as createDmlThread, but uses the extended protocol (PreparedStatement) instead of the
   * simple protocol (Statement.execute()).
   */
  private Thread createExtendedDmlThread(Connection conn, String sqlWithPlaceholder, long duration_ms,
      boolean use_txn_block) {
    return new Thread(() -> {
      long endTime = System.currentTimeMillis() + duration_ms;
      while (System.currentTimeMillis() < endTime) {
        try (PreparedStatement pstmt = conn.prepareStatement(sqlWithPlaceholder)) {
          if (use_txn_block)
            conn.createStatement().execute("BEGIN");

          pstmt.setObject(1, UUID.fromString(uuid));
          pstmt.execute();

          if (use_txn_block)
            conn.createStatement().execute("COMMIT");
        } catch (SQLException e) {
          if (use_txn_block) {
            try {
              conn.createStatement().execute("ROLLBACK");
            } catch (SQLException e2) {
              // We don't really expect this to happen, but we still
              // catch the exception here just to be safe (and to make
              // the compiler happy).
              processError("Extended DML", e2);
            }
          }
          processError("Extended DML", e);
        }
      }
    });
  }

  /**
   * Same as createDmlThread, but uses the extended protocol (PreparedStatement) instead of the
   * simple protocol (Statement.execute()). This thread only retries the execute phase rather than
   * retrying the entire parse, bind, and execute cycle.
   */
  private Thread createExtendedDmlThreadWithRetry(Connection conn, String sqlWithPlaceholder, long duration_ms) {
    return new Thread(() -> {
      long endTime = System.currentTimeMillis() + duration_ms;
      while (System.currentTimeMillis() < endTime) {

        // Prepare the statement once at the beginning of the loop.
        try (PreparedStatement pstmt = conn.prepareStatement(sqlWithPlaceholder)) {
          pstmt.setObject(1, UUID.fromString(uuid));

          // Keep retrying the statement until it succeeds or we time out.
          while (System.currentTimeMillis() < endTime) {
            try {
              pstmt.execute();
              break;
            } catch (SQLException e) {
              if ("40001".equals(e.getSQLState())) {
                LOG.info("Caught expected serialization failure. Retrying...");
                continue;
              } else {
                throw e;
              }
            }
          }

        } catch (SQLException e) {
          processError("Extended DML", e);
        }
      }
    });
  }

  /**
   * Creates a DDL thread that runs the given DDL statements for the given duration.
   */
  private Thread createDdlThread(Statement stmt, long duration_ms) {
    return new Thread(() -> {
      long endTime = System.currentTimeMillis() + duration_ms;
      while (System.currentTimeMillis() < endTime) {
        try {
          stmt.execute("ALTER TABLE todo ADD COLUMN updt date;");
          stmt.execute("ALTER TABLE todo DROP COLUMN updt;");
        } catch (SQLException e) {
          processError("DDL", e);
        }
      }
    });
  }

  /**
   * Logs the error and checks that the error code is 40001. If the error code is not 40001, sets
   * the testFailed flag to true.
   */
  private void processError(String threadType, SQLException e) {
    LOG.info("Error in " + threadType + " thread. Error code: " + e.getSQLState(), e);

    // We expect the error code 40001.
    // The "Invalid column number" error is a known issue (#20327) and can be
    // ignored. The "column with id X is marked for deletion" error is also expected
    // when the column is being dropped.
    // The 25P02 error code ("current transaction is aborted") is expected in
    // testWithExplicitTxn when we hit a schema version mismatch and the transaction
    // is aborted.
    // The 0A000 error code ("cached plan must not change result type") is expected
    // in testSchemaVersionMismatch when the result tuple descriptor of the prepared
    // statement changes.
    if ("40001".equals(e.getSQLState()) ||
        "25P02".equals(e.getSQLState()) ||
        "0A000".equals(e.getSQLState()) ||
        e.getMessage().contains("Invalid column number") ||
        e.getMessage().contains("marked for deletion")) {
      numExpectedErrors.incrementAndGet();
    } else {
      LOG.error("Unexpected error code: " + e.getSQLState() + ", message: " + e.getMessage());
      testFailed.set(true);
    }
  }
}
