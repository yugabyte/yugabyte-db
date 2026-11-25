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

import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Statement;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * Checks that cluster shutdown fails the test when a postgres process exceeds its memory budget.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgMemReport extends BasePgSQLTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    // Every backend's peak RSS is above 1 kB.
    flags.put("TEST_ysql_pg_mem_backend_peak_kb", "1");
    flags.put("TEST_ysql_pg_mem_enforce", "true");
    return flags;
  }

  @Test
  public void testShutdownReportsBackendOverBudget() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Allocates a 20 MB string, which raises the backend's peak RSS.
      stmt.execute("SELECT length(repeat('x', 20000000))");
    }

    AssertionError error = null;
    try {
      destroyMiniCluster();
    } catch (AssertionError e) {
      error = e;
    }
    assertNotNull("Cluster shutdown did not report the backend over budget", error);
    String message = error.getMessage();
    assertTrue(message, message.contains("PG memory budget exceeded (enforce: on)"));
    assertTrue(message,
        message.contains("peak grew after: SELECT length(repeat('x', 20000000))"));
  }
}
