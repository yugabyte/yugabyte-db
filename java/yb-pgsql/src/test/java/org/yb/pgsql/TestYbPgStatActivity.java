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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.Statement;
import java.sql.ResultSet;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.util.BuildTypeUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value = YBTestRunnerNonTsanOnly.class)

public class TestYbPgStatActivity extends BasePgSQLTest {

  private void consumeMem(Statement stmt) throws Exception {
    stmt.execute("SELECT * FROM pg_proc;");
    stmt.execute("SELECT * FROM pg_stat_activity;");
    stmt.execute("SELECT * FROM pg_attribute;");
    stmt.execute("CREATE TABLE test (id int);");
    stmt.execute("DROP TABLE test;");
  }

  private long getAllocatedMem(Statement stmt, int beid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT " +
        "yb_pg_stat_get_backend_allocated_mem_bytes(%d);", beid)).getLong(0);
  }

  private long getRssMem(Statement stmt, int beid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT " +
        "yb_pg_stat_get_backend_rss_mem_bytes(%d);", beid)).getLong(0);
  }

  private int getPgBackendBeid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT s.backendid FROM " +
        "(SELECT pg_stat_get_backend_idset() AS backendid) AS s " +
        "WHERE pg_backend_pid() = pg_stat_get_backend_pid(s.backendid);").getInt(0);
  }

  @Test
  public void testMemUsageFuncsReturnValues() throws Exception{
    try (Connection connection = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the allocated_mem_bytes and rss_mem_bytes are successfully retrieved
      long allocatedMem = getAllocatedMem(stmt, beid);
      long rssMem = getRssMem(stmt, beid);

      // TcMalloc is not enabled in macOS and ASAN build, so don't look for it.
      if (!BuildTypeUtil.isASAN())
        assertNotEquals("yb_pg_stat_get_backend_allocated_mem_bytes wasn't retrieved.",
            allocatedMem, 0);
      assertNotEquals("yb_pg_stat_get_backend_rss_mem_bytes wasn't retrieved or returned " +
          "incorrect value.", rssMem, -1);
    }
  }

  @Test
  public void testMemUsageFuncsWithMultipleBackends() throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(0).connect();
         Connection connection3 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt1 = connection1.createStatement();
         Statement stmt2 = connection2.createStatement();
         Statement stmt3 = connection3.createStatement();) {
      int beid1 = getPgBackendBeid(stmt1);
      int beid2 = getPgBackendBeid(stmt2);
      int beid3 = getPgBackendBeid(stmt3);

      // Use connection3 to query the memory usage of c1 and c2
      long c1AllocatedMem1 = getAllocatedMem(stmt3, beid1);
      long c2AllocatedMem1 = getAllocatedMem(stmt3, beid2);
      long c1RssMem1 = getRssMem(stmt3, beid1);

      // Verify that after memory consumption in c1, the functions' return values changed.
      consumeMem(stmt1);
      long c1AllocatedMem2 = getAllocatedMem(stmt3, beid1);
      long c1RssMem2 = getRssMem(stmt3, beid1);

      // TcMalloc is not enabled in ASAN, so don't look for it.
      if (!BuildTypeUtil.isASAN())
        assertNotEquals("Allocated mem usage wasn't reflected. ",
            c1AllocatedMem2, c1AllocatedMem1);
      assertNotEquals("Rss mem usage wasn't correctly reflected. ", c1RssMem2, c1RssMem1);

      // Verify that memory consumption in connection1 does not affect connection2's allocated
      // memory. Since there could be other memory allocated & reported to TcMalloc in connection2
      // that's not related to connection1, we expect the returned value within ~10% of change.
      long c2AllocatedMem2 = getAllocatedMem(stmt3, beid2);
      if (!BuildTypeUtil.isASAN())
        assertTrue("Allocating memomry in connection1 changed allocated memory in connection2.",
          c2AllocatedMem1 * 0.9 <= c2AllocatedMem2 && c2AllocatedMem2 <= c2AllocatedMem1 * 1.1);
    }
  }

  @Test
  public void testMemUsageFuncsWithExplicitTransactions() throws Exception {
    try (Connection connection = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the mem usage values don't change during an explicit transaction.
      connection.setAutoCommit(false);
      long allocatedMem1 = getAllocatedMem(stmt, beid);
      long rssMem1 = getRssMem(stmt, beid);
      consumeMem(stmt);
      long allocatedMem2 = getAllocatedMem(stmt, beid);
      long rssMem2 = getRssMem(stmt, beid);
      connection.commit();

      // TcMalloc is not enabled in ASAN, so don't look for it.
      if (!BuildTypeUtil.isASAN())
        assertEquals("Allocated mem usage changed during explicit transactions.",
          allocatedMem1, allocatedMem2);
      assertEquals("Rss mem usage changed during explicit transactions.",
          rssMem1, rssMem2);
    }
  }
}
