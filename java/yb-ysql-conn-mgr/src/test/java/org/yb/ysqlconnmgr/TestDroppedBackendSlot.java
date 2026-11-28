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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestDroppedBackendSlot extends BaseYsqlConnMgr {
  private static final int YSQL_MAX_CONNECTIONS = 20;
  private static final int RESERVE_INTERNAL_CONNS = 8;
  private static final int NUM_SLEEPERS = 10;
  private static final String USER = "slot_user";
  private static final String SLEEP_QUERY = "SELECT pg_sleep(600)";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_max_connections",
        Integer.toString(YSQL_MAX_CONNECTIONS));
    builder.addCommonTServerFlag("ysql_conn_mgr_reserve_internal_conns",
        Integer.toString(RESERVE_INTERNAL_CONNS));
    builder.addCommonTServerFlag("ysql_conn_mgr_enable_multi_route_pool", "true");
  }

  @Override
  protected boolean shouldRestartMiniClusterBetweenTests() {
    return true;
  }

  private static class LogLineCounter implements LogErrorListener {
    final AtomicInteger count = new AtomicInteger();
    private final String needle;

    LogLineCounter(String needle) {
      this.needle = needle;
    }

    @Override
    public void handleLine(String line) {
      if (line.contains(needle)) {
        count.incrementAndGet();
      }
    }

    @Override
    public void reportErrorsAtEnd() {}
  }

  private MiniYBDaemon connMgrTServer() {
    String host = getPgHost(TSERVER_IDX);
    return miniCluster.getTabletServers().values().stream()
        .filter(daemon -> host.equals(daemon.getLocalhostIP())).findFirst().get();
  }

  private Connection connectAsUser() throws Exception {
    return connectAs(USER);
  }

  private Connection connectAs(String user) throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(user)
        .connect();
  }

  private List<Integer> idleBackendPids(Statement stmt, String user) throws Exception {
    List<Integer> pids = new ArrayList<>();
    try (ResultSet rs = stmt.executeQuery(
        "SELECT pid FROM pg_stat_activity WHERE usename = '" + user + "' AND state = 'idle'")) {
      while (rs.next()) {
        pids.add(rs.getInt(1));
      }
    }
    return pids;
  }

  private List<Integer> sleepingBackendPids(Statement stmt) throws Exception {
    List<Integer> pids = new ArrayList<>();
    try (ResultSet rs = stmt.executeQuery(
        "SELECT pid FROM pg_stat_activity WHERE query = '" + SLEEP_QUERY + "'")) {
      while (rs.next()) {
        pids.add(rs.getInt(1));
      }
    }
    return pids;
  }

  private static void signal(String sig, List<Integer> pids) throws Exception {
    for (int pid : pids) {
      Process kill = new ProcessBuilder("kill", sig, String.valueOf(pid)).start();
      assertEquals(0, kill.waitFor());
    }
  }

  // Run pg_sleep on 10 of the 12 Connection Manager slots, freeze those backends with SIGSTOP and
  // drop their clients. Connection Manager gives up on them, but they still hold their Postgres
  // slots. 10 new clients must wait instead of failing with "too many clients", and must get
  // through once the frozen backends are resumed and exit.
  @Test
  public void testNewClientsWaitForDroppedBackendsToExit() throws Exception {
    try (Connection conn = connectToPostgres();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE " + USER + " LOGIN");
    }

    ExecutorService pool = Executors.newCachedThreadPool();
    List<Integer> pids = new ArrayList<>();
    try (Connection pgConn = connectToPostgres();
         Statement pgStmt = pgConn.createStatement()) {
      List<Connection> sleepers = new ArrayList<>();
      for (int i = 0; i < NUM_SLEEPERS; i++) {
        Connection conn = connectAsUser();
        sleepers.add(conn);
        pool.submit(() -> conn.createStatement().execute(SLEEP_QUERY));
      }
      TestUtils.waitFor(() -> sleepingBackendPids(pgStmt).size() == NUM_SLEEPERS, 60000);
      pids.addAll(sleepingBackendPids(pgStmt));

      LogLineCounter cancelLimits = new LogLineCounter("server cancel limit reached");
      connMgrTServer().getLogPrinter().addErrorListener(cancelLimits);
      signal("-STOP", pids);
      for (Connection conn : sleepers) {
        conn.abort(Runnable::run);
      }

      TestUtils.waitFor(() -> cancelLimits.count.get() == NUM_SLEEPERS, 90000);

      List<Future<?>> newClients = new ArrayList<>();
      for (int i = 0; i < NUM_SLEEPERS; i++) {
        newClients.add(pool.submit(() -> {
          try (Connection conn = connectAsUser();
               Statement stmt = conn.createStatement()) {
            stmt.execute("SELECT 1");
          }
          return null;
        }));
      }

      Thread.sleep(10000);
      for (Future<?> f : newClients) {
        if (f.isDone()) {
          f.get();
        }
      }

      signal("-CONT", pids);
      pids.clear();
      for (Future<?> f : newClients) {
        f.get(60, TimeUnit.SECONDS);
      }
    } finally {
      if (!pids.isEmpty()) {
        signal("-CONT", pids);
      }
      pool.shutdownNow();
    }
  }

  // Leave 10 idle servers on one route, freeze their backends with SIGSTOP, then connect 10
  // clients as another user. Those clients evict the idle servers, whose frozen backends cannot
  // exit, so the evicted slots must stay held instead of going to new backends that Postgres
  // would refuse with "too many clients". The clients must get through once the backends resume.
  @Test
  public void testEvictedIdleServerSlotHeldUntilBackendExits() throws Exception {
    final String donor = "evict_donor";
    final String recipient = "evict_recipient";
    final int numDonors = 10;
    final int numRecipients = 10;
    try (Connection conn = connectToPostgres();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE " + donor + " LOGIN");
      stmt.execute("CREATE ROLE " + recipient + " LOGIN");
    }

    List<Connection> donors = new ArrayList<>();
    for (int i = 0; i < numDonors; i++) {
      Connection conn = connectAs(donor);
      donors.add(conn);
      Statement stmt = conn.createStatement();
      stmt.execute("BEGIN");
      stmt.execute("SELECT 1");
    }
    for (Connection conn : donors) {
      conn.createStatement().execute("COMMIT");
      conn.close();
    }

    ExecutorService pool = Executors.newCachedThreadPool();
    List<Integer> pids = new ArrayList<>();
    try (Connection pgConn = connectToPostgres();
         Statement pgStmt = pgConn.createStatement()) {
      TestUtils.waitFor(() -> idleBackendPids(pgStmt, donor).size() == numDonors, 30000);
      pids.addAll(idleBackendPids(pgStmt, donor));

      LogLineCounter heldSlots = new LogLineCounter("holding its connection slot until it does");
      connMgrTServer().getLogPrinter().addErrorListener(heldSlots);
      signal("-STOP", pids);

      List<Future<?>> recipients = new ArrayList<>();
      for (int i = 0; i < numRecipients; i++) {
        recipients.add(pool.submit(() -> {
          try (Connection conn = connectAs(recipient);
               Statement stmt = conn.createStatement()) {
            stmt.execute("SELECT 1");
          }
          return null;
        }));
      }

      Thread.sleep(10000);
      for (Future<?> f : recipients) {
        if (f.isDone()) {
          f.get();
        }
      }
      assertGreaterThan(heldSlots.count.get(), 0);

      signal("-CONT", pids);
      pids.clear();
      for (Future<?> f : recipients) {
        f.get(60, TimeUnit.SECONDS);
      }
    } finally {
      if (!pids.isEmpty()) {
        signal("-CONT", pids);
      }
      pool.shutdownNow();
    }
  }
}
