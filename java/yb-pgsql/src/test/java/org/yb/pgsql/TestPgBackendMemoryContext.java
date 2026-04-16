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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Map.Entry;
import java.util.List;

import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

import com.google.common.net.HostAndPort;

/**
 * Test the pg_log_backend_memory_contexts function
 */
@RunWith(value=YBTestRunner.class)
public class TestPgBackendMemoryContext extends BasePgSQLTest {
  private final List<String> logBackendMemoryContextsResults =
      Collections.synchronizedList(new ArrayList<>());

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    if (isTestRunningWithConnectionManager()) {
      // Disable the random warmup mode of the connection manager. This test
      // calls the getPgBackendPid() function multiple times. To ensure the
      // same PID is returned each time, switch to
      // ConnectionManagerWarmupMode.NONE.
      warmupMode = ConnectionManagerWarmupMode.NONE;
    }
    super.customizeMiniClusterBuilder(builder);
  }

  @Before
  public void setUp() throws Exception {
    // Register log listener
    for (Entry<HostAndPort, MiniYBDaemon> entry : miniCluster.getTabletServers().entrySet()) {
      int port = entry.getKey().getPort();
      MiniYBDaemon tserver = entry.getValue();
      BackendMemoryContextsLogListener listener =
          new BackendMemoryContextsLogListener(logBackendMemoryContextsResults);

      // ErrorListener is a misnomer, we parse postgres backend memory contexts logs here
      tserver.getLogPrinter().addErrorListener(listener);
    }
  }

  @Test
  public void testLogBackendMemoryTexts() throws Exception {
    // The following logs are expected outputs from the pg_log_backend_memory_contexts function
    // These logs detail the PG backend process' PID followed by its memory contexts
    // [69974] LOG:  logging memory contexts of PID 69974
    // [69974] LOG:  YB TCMalloc heap size bytes: 12345
    // [69974] LOG:  YB TCMalloc total physical bytes: 12345
    // [69974] LOG:  ...
    // [69974] LOG:  level: 0; TopMemoryContext: \
    //  389872 total in 8 blocks; 32480 free (6 chunks); 357392 used
    // [69974] LOG:  level: 1; pgstat TabStatusArray lookup hash table:
    //  8192 total in 1 blocks; 1408 free (0 chunks); 6784 used
    // [69974] LOG:  level: 1; TopTransactionContext: \
    //  8192 total in 1 blocks; 7720 free (0 chunks); 472 used
    // [69974] LOG:  level: 1; RowDescriptionContext: \
    //  8192 total in 1 blocks; 6880 free (0 chunks); 1312 used

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SELECT pg_log_backend_memory_contexts(pg_backend_pid());");

      assertTrue("pg_log_backend_memory_contexts should output logs",
                !logBackendMemoryContextsResults.isEmpty());

      // With connection manager, getPgBackendPid can return the PID of any
      // backend process out of pool of physical connections it is maintaining.
      // Therefore running the test in NONE mode of connection manager to return
      // the same PID.

      // Look for the logs in the following order:
      List<String> expectedMessages = new ArrayList<>();
      expectedMessages.add(String.format("logging memory contexts of PID %s",
                                         getPgBackendPid(connection)));
      expectedMessages.add("level: 0; TopMemoryContext");
      expectedMessages.add("Grand total: ");
      expectedMessages.add("YB TCMalloc heap size bytes: ");
      expectedMessages.add("YB TCMalloc total physical bytes: ");
      expectedMessages.add("YB TCMalloc current allocated bytes: ");
      expectedMessages.add("YB TCMalloc pageheap free bytes: ");
      expectedMessages.add("YB TCMalloc pageheap unmapped bytes: ");
      expectedMessages.add("YB PGGate bytes: ");

      for (String result : logBackendMemoryContextsResults) {
        if (expectedMessages.isEmpty()) {
          // If we have already found all expected messages, we can skip further checks.
          break;
        }
        if (result.contains(expectedMessages.get(0))) {
          expectedMessages.remove(0);
        }
      }

      assertTrue(String.format("Expected to find all messages in pg_log_backend_memory_contexts %s",
                               expectedMessages.toString()),
                 expectedMessages.isEmpty());
    }
  }

  /**
   * Listen for pg_log_backend_memory_contexts logs
   * LogErrorListener is a misnomer
   */
  private static class BackendMemoryContextsLogListener implements LogErrorListener {
    private final List<String> logBackendMemoryContextsResults;

    public BackendMemoryContextsLogListener(List<String> logBackendMemoryContextsResults) {
      this.logBackendMemoryContextsResults = logBackendMemoryContextsResults;
    }

    @Override
    public void handleLine(String line) {
      logBackendMemoryContextsResults.add(line);
    }

    @Override
    public void reportErrorsAtEnd() {
      // NOOP
    }
  }
}
