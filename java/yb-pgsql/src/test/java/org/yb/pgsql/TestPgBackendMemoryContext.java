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

import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Map.Entry;
import java.util.List;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

import com.google.common.net.HostAndPort;

/**
 * Test the pg_log_backend_memory_contexts function
 */
@RunWith(value=YBTestRunner.class)
public class TestPgBackendMemoryContext extends BasePgSQLTest {
  private final List<String> logBackendMemoryContextsResults =
      Collections.synchronizedList(new ArrayList<>());

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

      boolean foundBackendPid = false;
      boolean foundTopMemoryContext = false;
      final String EXPECTED_LOG_START_STRING = String.format("logging memory contexts of PID %s",
                                                            getPgBackendPid(connection));
      final String EXPECTED_TOP_MEMORY_CONTEXT_STRING = "level: 0; TopMemoryContext";

      for (String result : logBackendMemoryContextsResults) {
        if (result.contains(EXPECTED_LOG_START_STRING)) {
          foundBackendPid = true;
        } else if (result.contains(EXPECTED_TOP_MEMORY_CONTEXT_STRING)) {
          foundTopMemoryContext = true;
          break; // TopMemoryContext is logged after backend PID
        }
      }

      assertTrue(String.format("pg_log_backend_memory_contexts should log the contexts " +
                              "of a specific process with PID %s",
                              getPgBackendPid(connection)),
                foundBackendPid);
      assertTrue("pg_log_backend_memory_contexts should log TopMemoryContext",
                foundTopMemoryContext);
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
