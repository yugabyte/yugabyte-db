// Copyright (c) Yugabyte, Inc.
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

import java.sql.Connection;
import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunner.class)
public class TestYbTracing extends BasePgSQLTest {
    
  private int getPgBackendPid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT pg_backend_pid()").getInt(0);
  }

  private boolean ybPgEnableTracing(Statement stmt, int pid, Long query_id) throws Exception {
    if (query_id == null) {
      return getSingleRow(stmt, String.format("SELECT yb_pg_enable_tracing(%d, NULL)", pid)).getBoolean(0);
    }
    return getSingleRow(stmt, String.format("SELECT yb_pg_enable_tracing(%d, %d)", pid, query_id)).getBoolean(0);
  }

  private boolean ybPgDisableTracing(Statement stmt, int pid, Long query_id) throws Exception {
    if (query_id == null) {
      return getSingleRow(stmt, String.format("SELECT yb_pg_disable_tracing(%d, NULL)", pid)).getBoolean(0);
    }
    return getSingleRow(stmt, String.format("SELECT yb_pg_disable_tracing(%d, %d)", pid, query_id)).getBoolean(0);
  }

  private boolean isYbPgTracingEnabled(Statement stmt, int pid, Long query_id) throws Exception {
    if (query_id == null) {
      return getSingleRow(stmt, String.format("SELECT is_yb_pg_tracing_enabled(%d, NULL)", pid)).getBoolean(0);
    }
    return getSingleRow(stmt, String.format("SELECT is_yb_pg_tracing_enabled(%d, %d)", pid, query_id)).getBoolean(0);
  }

  @Test
  public void testPgRegressTracingMultipleBackendsWithoutQueryId() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(0).connect();
         Connection connection3 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt1 = connection1.createStatement();
         Statement stmt2 = connection2.createStatement();
         Statement stmt3 = connection3.createStatement();) {
      int pid1 = getPgBackendPid(stmt1);
      int pid2 = getPgBackendPid(stmt2);
      int pid3 = getPgBackendPid(stmt3);

      // Use connection2 to enable tracing for connection1
      boolean res1 = ybPgEnableTracing(stmt2, pid1, null);
      boolean res2 = isYbPgTracingEnabled(stmt1, pid1, null);
      
      assertEquals("Tracing is not enabled by second connection", res1, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res2, true);

      // Use connection1 to disable tracing for connection1 and check from connection2
      boolean res3 = ybPgDisableTracing(stmt1, pid1, null);
      boolean res4 = isYbPgTracingEnabled(stmt2, pid1, null);
      
      assertEquals("Tracing is not disabled by second connection", res3, true);
      assertEquals("Tracing is disabled but is_yb_pg_tracing_enabled returned false", res4, false);

      // Use connection1 to enable tracing for all connections
      boolean res5 = ybPgEnableTracing(stmt1, 0, null);
      boolean res6 = isYbPgTracingEnabled(stmt1, pid1, null);
      boolean res7 = isYbPgTracingEnabled(stmt2, pid2, null);
      boolean res8 = isYbPgTracingEnabled(stmt3, pid3, null);
      boolean res9 = ybPgDisableTracing(stmt3, 0, null);

      assertEquals("Tracing is not enabled for all connections", res5, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res6, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res7, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res8, true);
      assertEquals("Tracing is not disabled for all connections", res9, true);
    }
  }

  @Test
  public void testPgRegressTracingMultipleBackendsWithQueryId() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(0).connect();
         Connection connection3 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt1 = connection1.createStatement();
         Statement stmt2 = connection2.createStatement();
         Statement stmt3 = connection3.createStatement();) {
      int pid1 = getPgBackendPid(stmt1);
      int pid2 = getPgBackendPid(stmt2);
      int pid3 = getPgBackendPid(stmt3);

      Long query_id = 1234L;

      // Use connection2 to enable tracing for connection1
      boolean res1 = ybPgEnableTracing(stmt2, pid1, query_id);
      boolean res2 = isYbPgTracingEnabled(stmt1, pid1, query_id);
      
      assertEquals("Tracing is not enabled by second connection", res1, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res2, true);

      // Use connection1 to disable tracing for connection1 and check from connection2
      boolean res3 = ybPgDisableTracing(stmt1, pid1, query_id);
      boolean res4 = isYbPgTracingEnabled(stmt2, pid1, query_id);
      
      assertEquals("Tracing is not disabled by second connection", res3, true);
      assertEquals("Tracing is disabled but is_yb_pg_tracing_enabled returned false", res4, false);

      // Use connection1 to enable tracing for all connections
      boolean res5 = ybPgEnableTracing(stmt1, 0, query_id);
      boolean res6 = isYbPgTracingEnabled(stmt1, pid1, query_id);
      boolean res7 = isYbPgTracingEnabled(stmt2, pid2, query_id);
      boolean res8 = isYbPgTracingEnabled(stmt3, pid3, query_id);
      boolean res9 = ybPgDisableTracing(stmt3, 0, query_id);

      assertEquals("Tracing is not enabled for all connections", res5, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res6, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res7, true);
      assertEquals("Tracing is enabled but is_yb_pg_tracing_enabled returned false", res8, true);
      assertEquals("Tracing is not disabled for all connections", res9, true);
    }
  }
}
