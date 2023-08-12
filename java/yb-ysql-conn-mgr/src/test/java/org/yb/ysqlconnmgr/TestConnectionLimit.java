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
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestConnectionLimit extends BaseYsqlConnMgr {
  // Idea is to test whether Ysql Connection Manager is able to handle multiple connections at a
  // time, as of now 16 (hard coded) `worker` threads are used by Ysql Connection Manager. Thus
  // while testing, number of connections made to the Ysql Connection Manager should be around 100
  // (each thread handling 5 - 10 connections), i.e. MAX_PHYSICAL_CONNECTION should be set around
  // 50.
  private final int MAX_PHYSICAL_CONNECTION = 50;

  private final int MAX_LOGICAL_CONNECTION = 10000;
  private final int POOL_SIZE = 33;
  private final int MAX_ROWS = 10000;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_max_connections", Integer.toString(MAX_PHYSICAL_CONNECTION));
        put("ysql_conn_mgr_max_client_connections", Integer.toString(MAX_LOGICAL_CONNECTION));
        put("ysql_conn_mgr_pool_size", Integer.toString(POOL_SIZE));
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  // Check that at the time when client is unable to make MAX_PHYSICAL_CONNECTION + 1
  // connections directly to the database, client can make 2 * MAX_PHYSICAL_CONNECTION connections
  // to the database via Ysql Connection Manager.
  @Test
  public void testLogicalConnectionLimit() throws Exception {
    // Create the test table.
    getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                          .connect()
                          .createStatement()
                          .execute("CREATE TABLE T1 (c1 int NOT NULL PRIMARY KEY, c2 text)");

    // Try making '2 * MAX_PHYSICAL_CONNECTION' connections to the Ysql Connection Manager.
    final int numThreads = 2 * MAX_PHYSICAL_CONNECTION;
    // Create the Runnable objects.
    int subRangeSize = MAX_ROWS / numThreads;
    Runnable[] runnables = new Runnable[numThreads];
    for (int i = 0; i < numThreads; i++) {
      int start = i * subRangeSize;
      runnables[i] = new TransactionRunnable(start);
    }

    // Create the threads.
    Thread[] threads = new Thread[numThreads];
    for (int i = 0; i < numThreads; i++) {
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
      assertFalse(((TransactionRunnable) runnable).gotException);
    }
  }

  private class TransactionRunnable implements Runnable {
    private final String[] ALLOWED_ERROR_MSGS = {
        "could not serialize access due to concurrent update",
        "Catalog Version Mismatch",
        "Restart read required" // More msgs to be added later on.
      };

    private int start;
    public boolean gotException;

    public TransactionRunnable(int start) {
      this.start = start;
      this.gotException = false;
    }

    @Override
    public void run() {
      // Create a connection to the Ysql Connection Manager and make sure client is able to execute
      // queries on the connection.
      try (
          Connection connection =
              getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                    .withAutoCommit(AutoCommit.DISABLED)
                                    .connect();
          Statement statement = connection.createStatement()) {

        for (int key = start; key < 10 + start; key++) {
          // Insert query
          statement.execute(String.format("INSERT INTO T1 (c1, c2) VALUES (%d, 'value')", key));

          // Select query
          statement.execute(String.format("SELECT c2 FROM T1 WHERE c1 = %d", key));

          connection.commit();
        }
      } catch (SQLException e) {
        // Got exception during executing the transaction.
        LOG.error("Unable to execute the query", e);
        for (String allowed_msg : ALLOWED_ERROR_MSGS) {
          if (e.getMessage().contains(allowed_msg)) {
            return;
          }
        }
        this.gotException = true;
      } catch (Exception e) {
        // Got exception during creating the connection.
        LOG.error("Unable to create the connection", e);
        this.gotException = true;
      }
    }
  }
}
