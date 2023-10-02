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
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.BuildTypeUtil;
import org.yb.util.SystemUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestYbPgStatActivity extends BasePgSQLTest {
  private static final boolean BUILDTYPE_SUPPORTS_TCMALLOC =
    SystemUtil.IS_LINUX && !BuildTypeUtil.isASAN();
  private static final int N_YB_HEAP_STATS_COLUMNS = 7;

  private static final String RETURNED_VALID_VALUE =
    " returned a valid value when tracking was disabled";
  private static final String RETURNED_INVALID_VALUE =
    " wasn't retrieved or returned incorrect value.";
  private static final String ALLOCATED_MEM_BYTES_NAME =
    "yb_pg_stat_get_backend_allocated_mem_bytes";
  private static final String RSS_MEM_BYTES_NAME =
    "yb_pg_stat_get_backend_rss_mem_bytes";

  private void consumeMem(Statement stmt) throws Exception {
    stmt.execute("SELECT * FROM pg_proc;");
    stmt.execute("SELECT * FROM pg_stat_activity;");
    stmt.execute("SELECT * FROM pg_attribute;");
    stmt.execute("CREATE TABLE test (id int);");
    stmt.execute("DROP TABLE test;");
  }

  private Long getAllocatedMem(Statement stmt, int beid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT %s(%d)", ALLOCATED_MEM_BYTES_NAME, beid))
      .getLong(0);
  }

  private Long getRssMem(Statement stmt, int beid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT %s(%d)", RSS_MEM_BYTES_NAME, beid))
      .getLong(0);
  }

  private int getPgBackendBeid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT s.backendid FROM " +
        "(SELECT pg_stat_get_backend_idset() AS backendid) AS s " +
        "WHERE pg_backend_pid() = pg_stat_get_backend_pid(s.backendid)").getInt(0);
  }

  private void setMemoryTracking(Statement stmt, boolean enabled) throws Exception {
    stmt.execute(String.format("SET yb_enable_memory_tracking = %b", enabled));
  }

  private void assertValidAllocatedMemBytes(long allocatedMem) throws Exception {
    if (BUILDTYPE_SUPPORTS_TCMALLOC)
      assertGreaterThan(ALLOCATED_MEM_BYTES_NAME + RETURNED_INVALID_VALUE,
                        allocatedMem, 0L);
  }

  private void assertValidRssMemBytes(long rssMem) throws Exception {
    assertGreaterThan(RSS_MEM_BYTES_NAME + RETURNED_INVALID_VALUE, rssMem, 0L);
  }

  private void assertYbHeapStats(Statement stmt, boolean shouldBeValid) throws Exception {
    if (BUILDTYPE_SUPPORTS_TCMALLOC) {
      Row yb_heap_stats = getSingleRow(stmt, "SELECT * FROM yb_heap_stats()");
      for (int i = 0; i < N_YB_HEAP_STATS_COLUMNS; i++)
      {
        if (shouldBeValid)
          assertGreaterThan(String.format("Field %s of yb_heap_stats %s",
                                        yb_heap_stats.getColumnName(i), RETURNED_INVALID_VALUE),
                            yb_heap_stats.getLong(i).longValue(), 0L);
        else
          assertEquals(String.format("Field %s of yb_heap_stats %s",
                                    yb_heap_stats.getColumnName(i), RETURNED_VALID_VALUE),
                      yb_heap_stats.getLong(i), null);
      }
    }
  }

  private void forceCatalogCacheRefresh() throws Exception {
    Connection connection = getConnectionBuilder().withTServer(0).connect();
    Statement stmt = connection.createStatement();
    stmt.execute("ALTER ROLE yugabyte SUPERUSER");
    waitForTServerHeartbeat();
  }

  @Test
  public void testSetMemoryTracking() throws Exception {
    forceCatalogCacheRefresh();
    try (Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the allocated_mem_bytes and rss_mem_bytes are successfully retrieved
      Long allocatedMem = getAllocatedMem(stmt, beid);
      Long rssMem = getRssMem(stmt, beid);

      assertValidAllocatedMemBytes(allocatedMem);
      assertValidRssMemBytes(rssMem);
      assertYbHeapStats(stmt, true);

      // Verify that the allocated_mem_bytes and rss_mem_bytes are not retrieved
      // when tracking is off
      setMemoryTracking(stmt, false);
      allocatedMem = getAllocatedMem(stmt, beid);
      rssMem = getRssMem(stmt, beid);

      assertEquals(RSS_MEM_BYTES_NAME + RETURNED_VALID_VALUE, rssMem, null);
      assertEquals(ALLOCATED_MEM_BYTES_NAME + RETURNED_VALID_VALUE, allocatedMem, null);
      assertYbHeapStats(stmt, false);

      // Verify that the allocated_mem_bytes and rss_mem_bytes are successfully retrieved
      // when tracking is turned back on
      setMemoryTracking(stmt, true);

      allocatedMem = getAllocatedMem(stmt, beid);
      rssMem = getRssMem(stmt, beid);

      assertValidAllocatedMemBytes(allocatedMem);
      assertValidRssMemBytes(rssMem);
      assertYbHeapStats(stmt, true);
    }
  }

  private long getAllocatedMemFromPgStatActivity(Statement stmt, int pid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT allocated_mem_bytes " +
        " FROM pg_stat_activity WHERE pid = %d;", pid)).getLong(0);
  }

  private long getRssMemFromPgStatActivity(Statement stmt, int pid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT rss_mem_bytes " +
        " FROM pg_stat_activity WHERE pid = %d;", pid)).getLong(0);
  }

  private int getPgBackendPid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT pg_backend_pid()").getInt(0);
  }

  @Test
  public void testMemUsageFuncsReturnValues() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the allocated_mem_bytes and rss_mem_bytes are successfully retrieved
      long allocatedMem = getAllocatedMem(stmt, beid);
      long rssMem = getRssMem(stmt, beid);

      assertValidAllocatedMemBytes(allocatedMem);
      assertValidRssMemBytes(rssMem);
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
      Long c1AllocatedMem1 = getAllocatedMem(stmt3, beid1);
      Long c2AllocatedMem1 = getAllocatedMem(stmt3, beid2);
      Long c1RssMem1 = getRssMem(stmt3, beid1);

      // Verify that after memory consumption in c1, the functions' return values changed.
      consumeMem(stmt1);
      Long c1AllocatedMem2 = getAllocatedMem(stmt3, beid1);
      Long c1RssMem2 = getRssMem(stmt3, beid1);

      if (BUILDTYPE_SUPPORTS_TCMALLOC)
        assertNotEquals("Allocated mem usage wasn't reflected. ",
            c1AllocatedMem2, c1AllocatedMem1);
      assertNotEquals("Rss mem usage wasn't correctly reflected. ", c1RssMem2, c1RssMem1);

      // Verify that memory consumption in connection1 does not affect connection2's allocated
      // memory. Since there could be other memory allocated & reported to TcMalloc in connection2
      // that's not related to connection1, we expect the returned value within ~10% of change.
      Long c2AllocatedMem2 = getAllocatedMem(stmt3, beid2);
      if (BUILDTYPE_SUPPORTS_TCMALLOC)
        assertTrue("Allocating memomry in connection1 changed allocated memory in connection2.",
          c2AllocatedMem1 * 0.9 <= c2AllocatedMem2 && c2AllocatedMem2 <= c2AllocatedMem1 * 1.1);
    }
  }

  @Test
  public void testMemUsageFuncsWithExplicitTransactions() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the mem usage values don't change during an explicit transaction.
      connection.setAutoCommit(false);
      long allocatedMem1 = getAllocatedMem(stmt, beid);
      long rssMem1 = getRssMem(stmt, beid);
      consumeMem(stmt);
      long allocatedMem2 = getAllocatedMem(stmt, beid);
      long rssMem2 = getRssMem(stmt, beid);
      connection.commit();

      if (BUILDTYPE_SUPPORTS_TCMALLOC)
        assertEquals("Allocated mem usage changed during explicit transactions.",
          allocatedMem1, allocatedMem2);
      assertEquals("Rss mem usage changed during explicit transactions.",
          rssMem1, rssMem2);
    }
  }

  @Test
  public void testInitialMemValuesFromPgStatActivity() throws Exception {
    // Skip test if the current yb instance is a sanitized build.
    // as the test checks o/p of columns in pg_stat_activity and not the
    // called function.
    if (BuildTypeUtil.isSanitizerBuild())
      return;

    try (Connection connection = getConnectionBuilder().withTServer(0).connect();
        Statement stmt = connection.createStatement()) {
      int pid = getPgBackendPid(stmt);

      // Verify that the allocated_mem_bytes and rss_mem_bytes are successfully
      // retrieved
      assertNotEquals("pg_stat_activity.allocated_mem_bytes wasn't retrieved.",
          getAllocatedMemFromPgStatActivity(stmt, pid), 0);

      assertNotEquals("pg_stat_activity.rss_mem_bytes wasn't retrieved or returned " +
          "incorrect value.", getRssMemFromPgStatActivity(stmt, pid), -1);
    }
  }

  @Test
  public void testMemUsageOfQueryFromPgStatActivity() throws Exception {
    // Skip test if the current yb instance is a sanitized build.
    // as the test checks o/p of columns in pg_stat_activity and not the
    // called function.
    if (BuildTypeUtil.isSanitizerBuild())
      return;

    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Connection connection2 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt1 = connection1.createStatement();
         Statement stmt2 = connection2.createStatement();) {
      int pid = getPgBackendPid(stmt1);

      // Use connection2 to query the memory usage of connection1
      long c1AllocatedMem1 = getAllocatedMemFromPgStatActivity(stmt2, pid);
      long c1RssMem1 = getRssMemFromPgStatActivity(stmt2, pid);

      // Verify that after memory consumption in connection1, the functions' return values changed.
      consumeMem(stmt1);
      long c1AllocatedMem2 = getAllocatedMemFromPgStatActivity(stmt2, pid);
      long c1RssMem2 = getRssMemFromPgStatActivity(stmt2, pid);

      assertNotEquals("Allocated mem usage didn't change.",
          c1AllocatedMem2, c1AllocatedMem1);
      assertNotEquals("Rss mem usage didn't change.", c1RssMem2, c1RssMem1);
    }
  }
}
