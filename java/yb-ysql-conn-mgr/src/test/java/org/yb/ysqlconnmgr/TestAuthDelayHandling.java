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
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestAuthDelayHandling extends BaseYsqlConnMgr {
  // The idea here is to test that connection manager preemptively aborts
  // authentication attempts where the client has already timed out, instead
  // of first spinning up a backend and then discovering that the client
  // has alread left.
  private final int AUTH_DELAY_MS = 5000;
  private final int MAX_CONTROL_BACKENDS = 1;
  private final int NUM_WORKERS = 10; // Queue size is num_workers * 16. Set to 10 to be safe.
  private final String USERNAME = "yugabyte";
  private final String PASSWORD = "yugabyte";
  // Keep a multiple of 10 as control pool size is set to ysql_max_connections/10,
  // so this makes for cleaner calculations.
  private final int MAX_PHYSICAL_CONNECTIONS = 20;
  // Connection manager seems to allocate 19 physical backends to the control pool,
  // despite setting ysql_max_connections to 20. Not entirely sure why, but this
  // behaviour is stable with this configuration. Thus, we set total threads to
  // 2 * 19 + 1 (1 successful login expected). Refer to the test explanation for
  // more details.
  private final int numThreads = 1 + 38;

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder.withUser("yugabyte").withPassword("yugabyte");
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_max_connections", Integer.toString(MAX_PHYSICAL_CONNECTIONS));
        put("ysql_conn_mgr_num_workers", Integer.toString(NUM_WORKERS));
        put("ysql_enable_auth", "true");
        put("TEST_ysql_conn_mgr_auth_delay_ms", Integer.toString(AUTH_DELAY_MS));
        put("ysql_conn_mgr_log_settings", "log_debug,log_query");
        put("ysql_conn_mgr_control_connection_pool_size", Integer.toString(MAX_CONTROL_BACKENDS));
      }
    };
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Test
  public void testAuthDelayHandling() throws Exception {
    // This test ensures that the fix added for #28080 and #28231 is working as
    // intended. The fix catches clients who have timed out before authentication
    // has begun and abandons the auth attempt for these clients, skipping
    // expensive steps like backend spin-up and catalogue cache refresh.

    // For this test, conn mgr has a delay added during authentication of 5 sec.
    // The test launches 2N + 1 thread attempting to connect to conn mgr.
    // The first 2N of these are intended to timeout after 1 sec of no messages
    // from conn mgr, while the last one will timeout after 7.5 seconds (and
    // sleeps for 2.5 seconds before attempting a connection). (Here, N = 19)

    // The first N threads will occupy the available physical connections and 'hang up'
    // during auth (because of the auth delay flag). Technically, they will see the password
    // request from the server and close the socket. But because conn mgr is stuck in
    // a machine_sleep call due to the flag, it will not see the closed socket until
    // it wakes up from sleep.

    // The next N threads will timeout while waiting  in the routing queue. Upon hitting
    // the socket timeout, they will close the socket (hence the custom socket factory) and exit.
    // We expect conn mgr to check this and handle the closed sockets by abandoning
    // auth for them, allowing the final thread to complete authentication successfully.

    // If conn mgr does not catch the timed out queued clients, it will proceed to
    // attempt authentication for already timed out clients, causing further clients
    // to also time out (here, the 1 expectSuccess thread), causing a
    // 'cascade of timeouts'.

    getConnectionBuilder().withUser("yugabyte").withPassword("yugabyte").connect();

    Runnable[] runnables = new Runnable[numThreads];
    for (int i = 0; i < numThreads; i++) {
      runnables[i] = new AuthAttemptRunnable(i, (i == numThreads - 1));
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
      assertFalse("Unexpected exception. Refer to logs for error.",
          ((AuthAttemptRunnable) runnable).caughtException);
    }
  }

  private class AuthAttemptRunnable implements Runnable {
    private final String[] ALLOWED_ERROR_MSGS = {"The connection attempt failed",
        "Connection attempt timed out", "sorry, too many clients already"};

    private final Boolean expectSuccess;
    private final int threadId;
    public boolean caughtException;
    private final int socketTimeout;

    public AuthAttemptRunnable(int threadId, Boolean expectSuccess) {
      this.expectSuccess = expectSuccess;
      this.threadId = threadId;
      this.caughtException = false;
      this.socketTimeout = (expectSuccess) ? (2 * AUTH_DELAY_MS / 1000) : 1;
    }

    @Override
    public void run() {
      // Create a connection to the Ysql Connection Manager and try to execute
      // queries on the connection.
      Properties props = new Properties();
      props.setProperty("user", USERNAME);
      props.setProperty("password", PASSWORD);

      // loginTimeout is set so that successful thread enforces timeout correctly
      // socketTimeout is set so that failure threads enforce socket closure correctly
      props.setProperty("loginTimeout", String.valueOf(socketTimeout));
      props.setProperty("socketTimeout", String.valueOf(socketTimeout));
      if (this.expectSuccess) {
        // We want to delay the thread that is expected to succeed so that it is necessarily last in
        // the queue
        try {
          Thread.sleep(AUTH_DELAY_MS / 2);
        } catch (InterruptedException e) {
          fail("Sleep interrupted for thread " + threadId + " with expectSuccess. "
              + e.getMessage());
        }
      } else {
        // Custom socketFactory order to abandon auth and close socket correctly
        props.setProperty("socketFactory", "org.yb.ysqlconnmgr.CloseAfterAuthRequestSocketFactory");
      }

      LOG.info("Now connecting for thread " + threadId);

      try (Connection connection = getConnectionBuilder().connect(props);) {
        LOG.info("Connection successful for thread " + threadId);
        if (!this.expectSuccess) {
          LOG.error("Unexpectedly created connection for thread number " + this.threadId
              + ". Expected to fail connection attempt.");
          this.caughtException = true;
        }
      } catch (Exception e) {
        LOG.info("Connection failed for thread " + threadId);
        if (this.expectSuccess) {
          LOG.error("Unable to create the connection for thread number " + this.threadId
                  + ". Expected to succeed.", e);
          this.caughtException = true;
          return;
        }
        // Got exception during authentication.
        for (String allowed_msg : ALLOWED_ERROR_MSGS) {
          if (e.getMessage().contains(allowed_msg)) {
            return;
          }
        }
        LOG.error("Unable to create the connection for thread number " + this.threadId
                + " Unexpected error message:", e);
        this.caughtException = true;
      }
    }
  }
}
