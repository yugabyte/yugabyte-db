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

package org.yb.pgsql;

import static org.junit.Assume.assumeTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.concurrent.atomic.AtomicReference;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.ExternalDaemonLogErrorListener;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.BuildTypeUtil;

/**
 * Injects memory errors into a backend and checks that the test harness would fail a test on
 * them.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgMemoryErrorDetection extends BasePgSQLTest {
  private static final long LOG_WAIT_TIMEOUT_MS = 60000;

  private void createExtension(Statement stmt) throws Exception {
    stmt.execute("CREATE EXTENSION IF NOT EXISTS yb_test_memory_errors");
  }

  /**
   * Waits until a tserver's log listener has matched a line containing {@code text}, then clears
   * it, since the report was expected.
   */
  private String waitForAndClearErrorLogLine(String text) throws Exception {
    AtomicReference<String> found = new AtomicReference<>();
    try {
      TestUtils.waitFor(() -> {
        for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
          String line = ts.getLogErrorListener().getErrorLogLine();
          if (line != null && line.contains(text)) {
            found.set(line);
            return true;
          }
        }
        return false;
      }, LOG_WAIT_TIMEOUT_MS);
    } catch (Exception e) {
      fail("No tserver log listener matched a line containing '" + text + "'");
    }
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      ExternalDaemonLogErrorListener listener = ts.getLogErrorListener();
      String line = listener.getErrorLogLine();
      if (line != null && line.contains(text)) {
        listener.clearErrorLogLine();
      }
    }
    return found.get();
  }

  @Test
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testAddressSanitizerReportIsDetected() throws Exception {
    assumeTrue("Requires an ASAN build", BuildTypeUtil.isASAN());
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      createExtension(stmt);
      try {
        stmt.execute("SELECT yb_test_heap_buffer_overflow()");
        fail("Expected the backend to die");
      } catch (SQLException e) {
        // ASAN kills the backend, which drops the connection.
      }
    }
    waitForAndClearErrorLogLine("AddressSanitizer: heap-buffer-overflow");
  }

  @Test
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testUseAfterPfreeIsDetected() throws Exception {
    assumeTrue("Requires an ASAN build", BuildTypeUtil.isASAN());
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      createExtension(stmt);
      try {
        stmt.execute("SELECT yb_test_use_after_pfree()");
        fail("Expected the backend to die");
      } catch (SQLException e) {
        // ASAN kills the backend, which drops the connection.
      }
    }
    waitForAndClearErrorLogLine("AddressSanitizer: heap-use-after-free");
  }

  @Test
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testLeakSanitizerReportIsDetected() throws Exception {
    assumeTrue("Requires an ASAN build", BuildTypeUtil.isASAN());
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      createExtension(stmt);
      stmt.execute("SELECT yb_test_leak_malloc()");
    }
    // LeakSanitizer reports when the backend exits, after the client has disconnected.
    waitForAndClearErrorLogLine("LeakSanitizer: detected memory leaks");
  }

  /**
   * MEMORY_CONTEXT_CHECKING reports corruption as a WARNING and carries on, so the query succeeds
   * and only the log listener can fail the test.
   */
  @Test
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testMemoryContextCorruptionIsDetected() throws Exception {
    // Context kind, whether to free the chunk, and the start of the expected WARNING.
    String[][] cases = {
        {"aset", "true", "detected write past chunk end in yb_test_aset "},
        {"aset", "false", "problem in alloc set yb_test_aset: detected write past chunk end"},
        {"generation", "true", "detected write past chunk end in yb_test_generation "},
        {"generation", "false",
         "problem in Generation yb_test_generation: detected write past chunk end"},
        {"slab", "true", "detected write past chunk end in yb_test_slab "},
        {"slab", "false", "problem in slab yb_test_slab: detected write past chunk end"},
    };
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      assumeTrue("Requires a build with PG assertions (and so MEMORY_CONTEXT_CHECKING)",
                 getSingleRow(stmt, "SHOW debug_assertions").getString(0).equals("on"));
      createExtension(stmt);
      for (String[] c : cases) {
        stmt.execute(String.format("SELECT yb_test_write_past_chunk_end('%s', %s)", c[0], c[1]));
        waitForAndClearErrorLogLine(c[2]);
      }
    }
  }
}
