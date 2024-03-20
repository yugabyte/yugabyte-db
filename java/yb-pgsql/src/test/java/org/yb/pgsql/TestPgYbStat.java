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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.BuildTypeUtil;
import org.yb.util.MiscUtil;
import org.yb.util.ThrowingRunnable;
import org.yb.util.ProcessUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.yugabyte.util.PSQLException;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgYbStat extends BasePgSQLTest {
  private static final Integer MAX_PG_STAT_RETRIES = 20;
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);
  private static final int YB_TERMINATED_QUERIES_MAX_SIZE = 1000;

  private void executeQueryAndExpectNoResults(final String query,
    final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      statement.executeQuery(query);
    } catch (PSQLException exception) {
      assertEquals("No results were returned by the query.", exception.getMessage());
    }
  }

  private void executeQueryAndExpectTempFileLimitExceeded(final String query,
    final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      statement.executeQuery(query);
    } catch (PSQLException exception) {
      assertEquals("ERROR: temporary file size exceeds temp_file_limit (0kB)",
                   exception.getMessage());
    }
  }

  private void executeQueryAndSendSignal(final String query,
    final Connection inputConnection, final String signal) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      final int pid = getPid(inputConnection);

      final CountDownLatch startSignal = new CountDownLatch(1);
      final List<ThrowingRunnable> cmds = new ArrayList<>();
      cmds.add(() -> {
        startSignal.countDown();
        startSignal.await();
        try {
          statement.execute(query);
        } catch (Throwable throwable) {
          if(isTestRunningWithConnectionManager())
            assertTrue(throwable.getMessage().contains("remote server read/write error"));
          else
            assertEquals("An I/O error occurred while sending to the backend.",
                       throwable.getMessage());
        }
      });
      cmds.add(() -> {
        startSignal.countDown();
        startSignal.await();
        Thread.sleep(100); // Allow the query execution a headstart before killing
        ProcessUtil.signalProcess(pid, signal);
      });
      MiscUtil.runInParallel(cmds, startSignal, 60);
    } catch (Throwable exception) {
      exception.printStackTrace();
      fail("Unexpected exception thrown");
    }
    assertTrue(inputConnection.isClosed());
  }

  private void setupMinTempFileConfigs(final Connection inputConnection) throws Exception {
    executeQueryAndExpectNoResults("SET work_mem TO 64", inputConnection);
    executeQueryAndExpectNoResults("SET temp_file_limit TO 0", inputConnection);
  }

  private int getPid(final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      ResultSet result = statement.executeQuery("SELECT pg_backend_pid() AS pid");
      assertTrue(result.next());
      return result.getInt("pid");
    }
  }

  private int getCurrentDatabaseOid(final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      ResultSet result = statement.executeQuery(
        "SELECT oid FROM pg_database WHERE datname = current_database()");
      assertTrue(result.next());
      return result.getInt("oid");
    }
  }

  private ArrayList<Connection> createConnections(int numConnections) throws Exception {
    final ArrayList<Connection> connections = new ArrayList<Connection>(numConnections);
    // Create numConnections postgres connections
    for (int i = 0; i < numConnections; ++i) {
      ConnectionBuilder b = getConnectionBuilder();
      b.withTServer(0);
      connections.add(b.connect());
    }
    return connections;
  }

  private interface CheckResultSetInterface {
    public boolean checkResultSet(ResultSet set) throws Exception;
  };

  private boolean waitUntilConditionSatisfiedOrTimeout(String query,
    Connection inputConnection, CheckResultSetInterface checkResultSetCallback) throws Exception {
    int retries = 0;
    while (retries++ < MAX_PG_STAT_RETRIES) {
      try (Statement statement = inputConnection.createStatement()) {
        ResultSet resultSet = statement.executeQuery(query);
        if (checkResultSetCallback.checkResultSet(resultSet))
          return true;
      }
      Thread.sleep(200);
    }
    return false;
  }

  @Test
  public void testYbTerminatedQueriesMultipleCauses() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();

    final String oversized_query = "SELECT a FROM generate_series(0, 1000000) a ORDER BY a";

    // We run three queries, each fail for a different reason.
    // 1: OOM (mocked by sending kill -KILL)
    executeQueryAndSendSignal(oversized_query, connection, "KILL");

    // Mocking a segfault results in ASAN build failing because it detects the
    // SIGSEGV. So skip this query if we're in ASAN.
    if (!BuildTypeUtil.isASAN()) {
      // 2: seg fault (mocked by sending kill -SIGSEGV)
      connection = getConnectionBuilder().connect();
      executeQueryAndSendSignal(oversized_query, connection, "SIGSEGV");
    }

    // 3: temp file limit exceeded (mocked by setting file limit to super small)
    connection = getConnectionBuilder().connect();
    setupMinTempFileConfigs(connection);
    executeQueryAndExpectTempFileLimitExceeded(oversized_query, connection);

    try (Statement statement = connection.createStatement()) {
      // By current implementation, we expect that the queries will overflow from the end
      // of the array and start overwiting the oldest entries stored at the beginning of
      // the array. Consider this a circular buffer.
      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT query_text, termination_reason FROM yb_terminated_queries", connection,
        (ResultSet resultSet) -> {
          if (!resultSet.next()
           || !oversized_query.equals(resultSet.getString("query_text"))
           || !resultSet.getString("termination_reason")
                        .equals("Terminated by SIGKILL"))
            return false;

          // We did not mock a segfault in ASAN build, so do not look for it
          if (!BuildTypeUtil.isASAN()) {
            if (!resultSet.next()
            || !oversized_query.equals(resultSet.getString("query_text"))
            || !resultSet.getString("termination_reason")
                        .equals("Terminated by SIGSEGV"))
              return false;
          }

          if (!resultSet.next()
           || !oversized_query.equals(resultSet.getString("query_text"))
           || !resultSet.getString("termination_reason")
                        .startsWith("temporary file size exceeds temp_file_limit"))
            return false;

          assertFalse("Expected only 3 queries in this test", resultSet.next());
          return true;
        }));
    }
  }

  @Test
  public void testYbTerminatedQueriesOverflow() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();
    setupMinTempFileConfigs(connection);

    final int num_queries = 1006;

    try (Statement statement = connection.createStatement()) {
      for (int i = 0; i < num_queries; i++) {
        final String query = String.format("SELECT * FROM generate_series(0, 1000000 + %d)", i);
        executeQueryAndExpectTempFileLimitExceeded(query, connection);
      }

      executeQueryAndExpectNoResults("SET work_mem TO 2048", connection);

      // By current implementation, we expect that the queries will overflow from the end
      // of the array and start overwiting the oldest entries stored at the beginning of
      // the array. Consider this a circular buffer.
      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT query_text FROM yb_terminated_queries", connection,
        (ResultSet resultSet) -> {
          for (int i = 0; i < YB_TERMINATED_QUERIES_MAX_SIZE; i++) {
            // expected_token = n, n + 1, n + 2, n + 3, n + 4, n + 5, 6, 7, 8, ... n - 1
            // where n = YB_TERMINATED_QUERIES_MAX_SIZE
            int expected_token = i + YB_TERMINATED_QUERIES_MAX_SIZE < num_queries
                ? i + YB_TERMINATED_QUERIES_MAX_SIZE
                : i;
            String expected_query = String.format("SELECT * FROM generate_series(0, 1000000 + %d)",
                                                  expected_token);
            if (!resultSet.next() || !expected_query.equals(resultSet.getString("query_text")))
              return false;
          }
          assertFalse("The size of yb_terminated_queries is greater than the max size",
              resultSet.next());
          return true;
        }));
    }
  }

  @Test
  public void testYBMultipleConnections() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();

    final ArrayList<Connection> connections = createConnections(2);
    final Connection connection1 = connections.get(0);
    final Connection connection2 = connections.get(1);

    setupMinTempFileConfigs(connection1);
    setupMinTempFileConfigs(connection2);

    final String statement1 = "SELECT * FROM generate_series(0, 1000000)";
    final String statement2 = "SELECT * FROM generate_series(6, 1234567)";
    executeQueryAndExpectTempFileLimitExceeded(statement1, connection1);
    executeQueryAndExpectTempFileLimitExceeded(statement2, connection2);

    assertTrue(waitUntilConditionSatisfiedOrTimeout(
      "SELECT backend_pid, query_text FROM yb_terminated_queries", connection1,
      (ResultSet result) -> {
        if (!result.next()) return false;
        if (!statement1.equals(result.getString("query_text"))
            || getPid(connection1) != result.getInt("backend_pid"))
            return false;
        if (!result.next()) return false;
        if (!statement2.equals(result.getString("query_text")) ||
            getPid(connection2) != result.getInt("backend_pid"))
            return false;
        return true;
      }));
  }

  @Test
  public void testYBDBFiltering() throws Exception {
    // We need to restart the cluster to wipe the state currently contained in yb_terminated_queries
    // that can potentially be leftover from another test in this class. This would let us start
    // with a clean slate.
    restartCluster();

    final String databaseName = "db";
    final String database2Name = "db2";

    executeQueryAndExpectNoResults("CREATE DATABASE " + databaseName, connection);

    final String statement1 = "SELECT * FROM generate_series(0, 1000000)";
    final String statement2 = "SELECT * FROM generate_series(0, 9999999)";
    final String statement3 = "SELECT * FROM generate_series(9, 1234567)";
    final String statement4 = "SELECT * FROM generate_series(80, 1791283)";

    try (Connection connection2 = getConnectionBuilder().withDatabase(databaseName).connect()) {
      setupMinTempFileConfigs(connection2);

      executeQueryAndExpectTempFileLimitExceeded(statement1, connection2);
      executeQueryAndExpectTempFileLimitExceeded(statement2, connection2);
    }

    executeQueryAndExpectNoResults("CREATE DATABASE " + database2Name, connection);

    try (Connection connection2 = getConnectionBuilder().withDatabase(database2Name).connect()) {
      setupMinTempFileConfigs(connection2);

      executeQueryAndExpectTempFileLimitExceeded(statement3, connection2);
      executeQueryAndExpectTempFileLimitExceeded(statement4, connection2);

      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT S.query_text FROM yb_pg_stat_get_queries(" +
        getCurrentDatabaseOid(connection2) + "::Oid) as S", connection2,
        (ResultSet result) -> {
          if (!result.next()) return false;
          if (!statement3.equals(result.getString("query_text"))) return false;
          if (!result.next()) return false;
          if (!statement4.equals(result.getString("query_text"))) return false;
          return !result.next();
        }));
    }

    try (Connection connection2 = getConnectionBuilder().withDatabase(databaseName).connect()) {
      assertTrue(waitUntilConditionSatisfiedOrTimeout(
        "SELECT S.query_text FROM yb_pg_stat_get_queries("
        + getCurrentDatabaseOid(connection2) + "::Oid) as S", connection2,
        (ResultSet result) -> {
          if (!result.next()) return false;
          if (!statement1.equals(result.getString("query_text"))) return false;
          if (!result.next()) return false;
          if (!statement2.equals(result.getString("query_text"))) return false;
          return !result.next();
        }));
    }
  }
}
