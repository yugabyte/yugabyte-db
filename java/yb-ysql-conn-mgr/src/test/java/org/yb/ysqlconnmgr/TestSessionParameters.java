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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;

// TODO (janand) #18889 Add more tests for session parameter support
//   - RESET query
//   - SET/RESET in nested transaction
//   - SET LOCAL query

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestSessionParameters extends BaseYsqlConnMgr {
  // Pool to be divided between 1 control connection pool and 1 global pool.
  // TODO: Revert to 2 connections after bug fix DB-7395 lands.
  private final int POOL_SIZE = 3;

  private final int TEST_NUM_CONNECTIONS = 20;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_max_conns_per_db", Integer.toString(POOL_SIZE));
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Test
  public void testSessionParameters() throws Exception {
    Runnable[] runnables = new Runnable[TEST_NUM_CONNECTIONS];
    for (int i = 0; i < TEST_NUM_CONNECTIONS; i++) {
      runnables[i] = new SessionParameterTestRunnable(i);
    }

    // Create the threads.
    Thread[] threads = new Thread[TEST_NUM_CONNECTIONS];
    for (int i = 0; i < TEST_NUM_CONNECTIONS; i++) {
      threads[i] = new Thread(runnables[i]);
    }

    // Start the threads.
    for (Thread thread : threads) {
      thread.start();
    }

    // Wait for the threads to finish.
    for (Thread thread : threads) {
      thread.join();
    }

    // Check for exceptions.
    for (Runnable runnable : runnables) {
      assertFalse(((SessionParameterTestRunnable) runnable).gotException);
    }
  }

  private class SessionParameterTestRunnable implements Runnable {
    private long THREAD_ID;
    private final String[] RETRYABLE_ERROR_MESSAGES =
        {"could not serialize access due to concurrent update",
          "Catalog Version Mismatch",
          "Restart read required"
        };

    public boolean gotException;

    public SessionParameterTestRunnable(long i) {
      this.gotException = false;
      this.THREAD_ID = i;
    }

    private Connection setStartupParametersAndConnect() throws Exception {
      // If auto-commit is enabled then each statement will be treated as a "single query
      // transaction", in the case it's disabled it will be a normal transaction.
      // It is required to test both the scenarios, "single query transaction" and a "normal
      // transaction"
      // i.e. auto-commit as enabled and disabled.
      return getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .withAutoCommit(THREAD_ID % 2 == 0 ? AutoCommit.DISABLED : AutoCommit.ENABLED).connect();
    }

    private boolean isRetryable(Exception e) {
      for (String allowed_msg : RETRYABLE_ERROR_MESSAGES) {
        if (e.getMessage().contains(allowed_msg)) {
          return true;
        }
      }
      return false;
    }

    @Override
    public void run() {
      for (int retryCount = 0; retryCount < 5; retryCount++) {
        try (Connection connection = setStartupParametersAndConnect();
            Statement statement = connection.createStatement()) {

          for (int i = 0; i < 10; i++) {
            long expected_value = THREAD_ID * 10 + i;
            statement.execute(String.format("SET search_path = %d", expected_value));

            if (THREAD_ID % 2 == 0)
              connection.commit();

            // Verify the session parameter value.
            ResultSet resultSet = statement.executeQuery("SHOW search_path");
            if (resultSet.next()) {
              int result = resultSet.getInt(1);
              if (result != expected_value) {
                gotException = true;
                return;
              }
            } else {
              LOG.error("Got empty result set after exectuing `SHOW search_path`");
              gotException = true;
              return;
            }

            if (THREAD_ID % 2 == 0)
              connection.commit();
          }

          connection.close();
          return;

        } catch (SQLException e) {
          // Got exception during executing the transaction.
          LOG.error("Unable to execute the query", e);

          if (!isRetryable(e)) {
            gotException = true;
            return;
          }

        } catch (Exception e) {
          // Got exception during creating the connection.
          LOG.error("Unable to create the connection", e);
          gotException = true;
          return;
        }
      }

      LOG.error("Ran out of retries");
      gotException = true;
    }
  }
}
