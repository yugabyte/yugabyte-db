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
import org.junit.Assume;
import org.junit.runner.RunWith;
import org.yb.util.BuildTypeUtil;
import org.yb.util.SystemUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestYbPgStatActivity extends BasePgSQLTest {
  private final class MemoryStats {
    final Long allocatedMem;
    final Long rssMem;
    final Long pssMem;

    public MemoryStats(Long allocatedMem, Long rssMem, Long pssMem) {
      this.allocatedMem = allocatedMem;
      this.rssMem = rssMem;
      this.pssMem = pssMem;
    }

    public void assertValid() throws Exception {
      if (BUILDTYPE_SUPPORTS_TCMALLOC)
        assertGreaterThan(ALLOCATED_MEM_BYTES_NAME + RETURNED_INVALID_VALUE,
                          this.allocatedMem, 0L);

      assertGreaterThan(RSS_MEM_BYTES_NAME + RETURNED_INVALID_VALUE, this.rssMem, 0L);

      if (!SystemUtil.IS_MAC)
        assertGreaterThan(PSS_MEM_BYTES_NAME + RETURNED_INVALID_VALUE, this.pssMem, 0L);
    }

    public void assertAllValuesDiffer(MemoryStats other) throws Exception {
      if (BUILDTYPE_SUPPORTS_TCMALLOC)
        assertNotEquals("Allocated mem usage wasn't correctly reflected",
                        this.allocatedMem, other.allocatedMem);

      assertNotEquals("Rss mem usage wasn't correctly reflected", this.rssMem, other.rssMem);

      if (!SystemUtil.IS_MAC)
        assertNotEquals("Pss mem usage wasn't correctly reflected", this.pssMem, other.pssMem);
    }

    @Override
    public boolean equals(Object obj) {
      MemoryStats other = (MemoryStats) obj;
      if (BUILDTYPE_SUPPORTS_TCMALLOC && !this.allocatedMem.equals(other.allocatedMem))
          return false;
      if (!this.rssMem.equals(other.rssMem))
        return false;
      if (!SystemUtil.IS_MAC && !this.pssMem.equals(other.pssMem))
        return false;
      return true;
    }

    public String toString() {
      return String.format("MemoryStats(allocatedMem=%s, rssMem=%s, pssMem=%s)",
          allocatedMem, rssMem, pssMem);
    }
  }

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
  private static final String PSS_MEM_BYTES_NAME =
    "yb_pg_stat_get_backend_pss_mem_bytes";

  private void consumeMem(Statement stmt) throws Exception {
    stmt.execute("SELECT * FROM pg_proc;");
    stmt.execute("SELECT * FROM pg_stat_activity;");
    stmt.execute("SELECT * FROM pg_attribute;");
    stmt.execute("CREATE TABLE test (id int);");
    stmt.execute("DROP TABLE test;");
  }

  private MemoryStats getMemoryStats(Statement stmt, int beid) throws Exception {
    Row result = getSingleRow(stmt, String.format("SELECT %s(%d), %s(%d), %s(%d)",
                                                  ALLOCATED_MEM_BYTES_NAME, beid,
                                                  RSS_MEM_BYTES_NAME, beid,
                                                  PSS_MEM_BYTES_NAME, beid));

    return new MemoryStats(result.getLong(0), result.getLong(1), result.getLong(2));
  }

  private int getPgBackendBeid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT s.backendid FROM " +
        "(SELECT pg_stat_get_backend_idset() AS backendid) AS s " +
        "WHERE pg_backend_pid() = pg_stat_get_backend_pid(s.backendid)").getInt(0);
  }

  private void setMemoryTracking(Statement stmt, boolean enabled) throws Exception {
    stmt.execute(String.format("SET yb_enable_memory_tracking = %b", enabled));
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
    getSystemTableRowsList(stmt, "SELECT yb_increment_all_db_catalog_versions(true)");
    waitForTServerHeartbeat();
  }

  @Test
  public void testSetMemoryTracking() throws Exception {
    Assume.assumeFalse(CATALOG_CACHE_MISS_NEED_UNIQUE_PHYSICAL_CONN,
          isTestRunningWithConnectionManager());
    forceCatalogCacheRefresh();
    try (Statement stmt = connection.createStatement()) {
      consumeMem(stmt);
      int beid = getPgBackendBeid(stmt);

      // Verify that the memory stats are successfully retrieved
      getMemoryStats(stmt, beid).assertValid();

      assertYbHeapStats(stmt, true);

      // Verify that the memory stats are not retrieved when tracking is off
      setMemoryTracking(stmt, false);
      MemoryStats memStats = getMemoryStats(stmt, beid);

      assertEquals(RSS_MEM_BYTES_NAME + RETURNED_VALID_VALUE, memStats.rssMem, null);
      assertEquals(PSS_MEM_BYTES_NAME + RETURNED_VALID_VALUE, memStats.pssMem, null);
      assertEquals(ALLOCATED_MEM_BYTES_NAME + RETURNED_VALID_VALUE, memStats.allocatedMem, null);
      assertYbHeapStats(stmt, false);

      // Verify that the memory stats are successfully retrieved when tracking is turned back on
      setMemoryTracking(stmt, true);

      getMemoryStats(stmt, beid).assertValid();
      assertYbHeapStats(stmt, true);
    }
  }

  private long getAllocatedMemFromPgStatActivity(Statement stmt, int pid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT allocated_mem_bytes " +
        " FROM pg_stat_activity WHERE pid = %d;", pid)).getLong(0);
  }

  private long getPssMemFromPgStatActivity(Statement stmt, int pid) throws Exception {
    return getSingleRow(stmt, String.format("SELECT pss_mem_bytes " +
        " FROM pg_stat_activity WHERE pid = %d;", pid)).getLong(0);
  }

  private int getPgBackendPid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT pg_backend_pid()").getInt(0);
  }

  @Test
  public void testMemUsageFuncsReturnValues() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the memory stats are successfully retrieved
      getMemoryStats(stmt, beid).assertValid();
    }
  }

  @Test
  public void testMemUsageFuncsWithMultipleBackends() throws Exception {
    skipYsqlConnMgr(BasePgSQLTest.SAME_PHYSICAL_CONN_AFFECTING_DIFF_LOGICAL_CONNS_MEM,
         isTestRunningWithConnectionManager());
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
      MemoryStats c1MemStats1 = getMemoryStats(stmt3, beid1);
      MemoryStats c2MemStats1 = getMemoryStats(stmt3, beid2);

      // Verify that after memory consumption in c1, the functions' return values changed.
      consumeMem(stmt1);
      MemoryStats c1MemStats2 = getMemoryStats(stmt3, beid1);

      c1MemStats1.assertAllValuesDiffer(c1MemStats2);

      // Verify that memory consumption in connection1 does not affect connection2's allocated
      // memory. Since there could be other memory allocated & reported to TcMalloc in connection2
      // that's not related to connection1, we expect the returned value within ~10% of change.
      MemoryStats c2MemStats2 = getMemoryStats(stmt3, beid2);
      if (BUILDTYPE_SUPPORTS_TCMALLOC)
        assertTrue("Allocating memomry in connection1 changed allocated memory in connection2.",
          c2MemStats1.allocatedMem * 0.9 <= c2MemStats2.allocatedMem &&
          c2MemStats2.allocatedMem <= c2MemStats1.allocatedMem * 1.1);
    }
  }

  @Test
  public void testMemUsageFuncsWithExplicitTransactions() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      int beid = getPgBackendBeid(stmt);

      // Verify that the mem usage values don't change during an explicit transaction.
      connection.setAutoCommit(false);
      MemoryStats memStats1 = getMemoryStats(stmt, beid);
      consumeMem(stmt);
      MemoryStats memStats2 = getMemoryStats(stmt, beid);
      connection.commit();

      assertEquals("Memory usage changed during explicit transaction.",
          memStats1, memStats2);
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

      // Verify that the allocatedMem and pss_mem_bytes are successfully
      // retrieved
      assertNotEquals("pg_stat_activity.allocatedMem wasn't retrieved.",
          getAllocatedMemFromPgStatActivity(stmt, pid), 0);

      if (!SystemUtil.IS_MAC)
        assertNotEquals("pg_stat_activity.pss_mem_bytes wasn't retrieved or returned " +
            "incorrect value.", getPssMemFromPgStatActivity(stmt, pid), -1);
    }
  }

  @Test
  public void testMemUsageOfQueryFromPgStatActivity() throws Exception {
    skipYsqlConnMgr(BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED,
        isTestRunningWithConnectionManager());

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
      long c1PssMem1 = getPssMemFromPgStatActivity(stmt2, pid);

      // Verify that after memory consumption in connection1, the functions' return values changed.
      consumeMem(stmt1);
      long c1AllocatedMem2 = getAllocatedMemFromPgStatActivity(stmt2, pid);
      long c1PssMem2 = getPssMemFromPgStatActivity(stmt2, pid);

      assertNotEquals("Allocated mem usage didn't change.",
          c1AllocatedMem2, c1AllocatedMem1);
      if (!SystemUtil.IS_MAC)
        assertNotEquals("Pss mem usage didn't change.", c1PssMem2, c1PssMem1);
    }
  }
}
