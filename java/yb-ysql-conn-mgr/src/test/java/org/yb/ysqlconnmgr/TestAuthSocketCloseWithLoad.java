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

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.ResultSet;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;

// This test runs multiple threads that connect as different users and execute simple queries,
// while also simulating clients that abruptly close the socket during authentication, to ensure
// the connection manager handles authentication failures and connection drops gracefully.
//
// We test that all the queries execute correctly: a query failing would indicate that connection
// manager crashed. We ideally want to see if connection manager crashes or not, but we don't really
// have a way to do that, so we just use the workload running successfully as a proxy for that
//
// See the bug https://github.com/yugabyte/yugabyte-db/issues/28174, where we were facing the issue
// that whenever a client would connect to a cluster while some workload was running, Connection
// Manager would crash. This is also seen by writing an empty password on ysqlsh's password prompt.
// In this case, ysqlsh just returns an error directly and closes the socket to backend. We are
// simulating the aforementioned scenario in this test.

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestAuthSocketCloseWithLoad extends BaseYsqlConnMgr {

  private static final int NUM_THREADS_PER_USER = 5;
  private static final int NUM_ROWS = 1000;
  private static final int EMPTY_PASSWORD_ATTEMPTS = 10;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("enable_ysql_conn_mgr", "true");
        put("enable_ysql_conn_mgr_stats", "true");
        put("ysql_conn_mgr_max_client_connections", "6000");
        put("ysql_conn_mgr_enable_multi_route_pool", "false");
        put("ysql_conn_mgr_num_workers", "4");
        put("ysql_conn_mgr_pool_timeout", "10");
        put("ysql_conn_mgr_server_lifetime", "600");
        put("ysql_conn_mgr_use_auth_backend", "false");
        put("ysql_conn_mgr_superuser_sticky", "false");
        put("ysql_enable_auth", "true");
        put("ysql_hba_conf_csv", "host all all all password");
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder.withUser("yugabyte").withPassword("yugabyte");
  }

  @Test
  public void testAuthSocketCloseWithLoad() throws Exception {
    setupTestData();

    // Run concurrent queries with user1 and user2
    AtomicBoolean queryFailed = new AtomicBoolean(false);
    AtomicBoolean stopThreads = new AtomicBoolean(false);
    Thread[] threads = new Thread[NUM_THREADS_PER_USER * 2];

    // Create and start threads for user1
    for (int i = 0; i < NUM_THREADS_PER_USER; i++) {
      threads[i] = new Thread(new QueryRunnable("user1", "password1", queryFailed, stopThreads));
      threads[i].start();
    }

    // Create and start threads for user2
    for (int i = 0; i < NUM_THREADS_PER_USER; i++) {
      threads[NUM_THREADS_PER_USER + i] =
          new Thread(new QueryRunnable("user2", "password2", queryFailed, stopThreads));
      threads[NUM_THREADS_PER_USER + i].start();
    }

    // Sleep for 2 secs so that background threads get a chance to start up
    Thread.sleep(2_000);

    // On main thread, try to connect with empty password
    for (int i = 0; i < EMPTY_PASSWORD_ATTEMPTS; i++) {
      try {
        Properties props = new Properties();
        props.setProperty("socketFactory", "org.yb.ysqlconnmgr.CloseAfterAuthRequestSocketFactory");
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte").withPassword("").connect(props);
        // If connection succeeds, it's unexpected - we should fail
        fail("Expected connection with empty password to fail");
      } catch (SQLException e) {
        // Expected to fail with empty password
        LOG.info("Empty password connection attempt " + (i + 1) + " failed as expected: "
            + e.getMessage());
      }
      // Sleep for some time so other threads get a chance to proceed
      Thread.sleep(1);
    }

    // Signal all threads to stop
    LOG.info("Empty password attempts completed, signaling threads to stop...");
    stopThreads.set(true);

    // Wait for threads to complete
    LOG.info("Waiting for threads to complete...");
    for (int i = 0; i < threads.length; i++) {
      try {
        threads[i].join(30000); // Wait up to 30 seconds for each thread
        LOG.info("Thread " + (i + 1) + " completed");
      } catch (InterruptedException e) {
        LOG.error("Interrupted while waiting for thread " + (i + 1), e);
      }
    }
    LOG.info("All threads completed with queryFailed value " + queryFailed.get());

    // Assert that no query failed
    assertFalse("One or more queries failed during empty password test", queryFailed.get());
  }

  private void setupTestData() throws Exception {
    LOG.info("setupTestData: Creating connection...");
    try (
        Connection connection =
            getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .withUser("yugabyte").withPassword("yugabyte").connect();
        Statement statement = connection.createStatement()) {
      LOG.info("setupTestData: Connection created successfully");

      // Create user1
      statement.execute("CREATE USER user1 WITH PASSWORD 'password1'");

      // Create user2
      statement.execute("CREATE USER user2 WITH PASSWORD 'password2'");

      // Grant necessary permissions to user1 and user2
      grantPermissions(statement, "user1", "yugabyte");
      grantPermissions(statement, "user2", "yugabyte");

      LOG.info("Created users user1 and user2 with permissions");

      // Create table
      statement.execute(
          "CREATE TABLE a (" + "id int PRIMARY KEY," + "name VARCHAR(100)," + "value INTEGER,"
              + "description TEXT," + "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP" + ")");

      // Insert sample data in batches
      int batchSize = 100;
      for (int i = 0; i < NUM_ROWS; i += batchSize) {
        int batchCount = Math.min(batchSize, NUM_ROWS - i);
        StringBuilder insertSql = new StringBuilder();
        insertSql.append("INSERT INTO a (id, name, value, description, created_at) VALUES ");

        for (int j = 0; j < batchCount; j++) {
          int rowNum = i + j + 1;
          if (j > 0)
            insertSql.append(", ");
          insertSql.append(
              String.format("(%d, 'Test User %d', %d, 'Description for row %d', CURRENT_TIMESTAMP)",
                  rowNum, rowNum, rowNum * 10, rowNum));
        }

        statement.execute(insertSql.toString());
      }

      LOG.info("Created table 'a' and inserted " + NUM_ROWS + " rows of data");
      LOG.info("setupTestData: All setup completed successfully");
    }
  }

  private void grantPermissions(Statement statement, String username, String database)
      throws SQLException {
    try {
      // Grant connect permission to database
      statement.execute(String.format("GRANT CONNECT ON DATABASE %s TO %s", database, username));
      LOG.info(
          String.format("Granted CONNECT permission on database '%s' to '%s'", database, username));

      // Grant usage on schema
      statement.execute(String.format("GRANT USAGE ON SCHEMA public TO %s", username));
      LOG.info(String.format("Granted USAGE permission on schema 'public' to '%s'", username));

      // Grant permissions on existing tables
      ResultSet tables =
          statement.executeQuery("SELECT tablename FROM pg_tables WHERE schemaname = 'public'");

      while (tables.next()) {
        String tableName = tables.getString("tablename");
        statement.execute(String.format("GRANT SELECT, INSERT, UPDATE, DELETE ON TABLE %s TO %s",
            tableName, username));
        LOG.info(String.format(
            "Granted SELECT, INSERT, UPDATE, DELETE permissions on table '%s' to '%s'", tableName,
            username));
      }

      // Grant permissions on future tables (default privileges)
      statement.execute(String.format("ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT "
          + "SELECT, INSERT, UPDATE, DELETE ON TABLES TO %s", username));
      LOG.info(String.format("Set default privileges for future tables to '%s'", username));

      // Grant sequence permissions
      statement.execute(String.format(
          "ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT USAGE, SELECT ON SEQUENCES TO %s",
          username));
      LOG.info(String.format("Set default privileges for future sequences to '%s'", username));

    } catch (SQLException e) {
      LOG.error(String.format("Error granting permissions to '%s': %s", username, e.getMessage()));
      throw e;
    }
  }

  private class QueryRunnable implements Runnable {
    private final String username;
    private final String password;
    private final AtomicBoolean queryFailed;
    private final AtomicBoolean stopThreads;

    public QueryRunnable(String username, String password, AtomicBoolean queryFailed,
        AtomicBoolean stopThreads) {
      LOG.info("Creating QueryRunnable for user: " + username);
      this.username = username;
      this.password = password;
      this.queryFailed = queryFailed;
      this.stopThreads = stopThreads;
      LOG.info("QueryRunnable created for user: " + username);
    }

    @Override
    public void run() {
      for (int iteration = 0; true; iteration++) {
        // Check if we should stop
        if (stopThreads.get()) {
          LOG.info("Thread for user " + username + " stopping after " + iteration + " iterations");
          break;
        }

        try (
            Connection connection =
                getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser(username).withPassword(password).connect();
            Statement statement = connection.createStatement()) {
          try {
            // Query 1: Get first 100 rows
            statement.executeQuery("SELECT * FROM a LIMIT 100");

            // Query 2: Get next 100 rows (offset by 100)
            statement.executeQuery("SELECT * FROM a LIMIT 100 OFFSET 100");

            // Query 3: Get another 100 rows (offset by 200)
            statement.executeQuery("SELECT * FROM a LIMIT 100 OFFSET 200");
          } catch (Exception e) {
            LOG.error("Query failed iteration " + iteration + " for user " + username, e);
            queryFailed.set(true);
          }
        } catch (Exception e) {
          LOG.error("Failed to create connection or statement for user " + username, e);
          queryFailed.set(true);
        }
      }
    }
  }
}
