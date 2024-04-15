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
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;

import org.junit.Test;
import org.junit.Assume;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.ProcessUtil;
import org.yb.YBTestRunner;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunner.class)
public class TestPgConnection extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);
  private static final int MAX_CONNECTIONS = 15;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_max_connections", String.valueOf(MAX_CONNECTIONS));
    return flagMap;
  }

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumMasters() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  private void assertAlreadyAtMaxConnections() throws Exception {
    try {
      createConnection();
      fail("Expected connection to fail because we have exceeded the maximum");
    } catch (PSQLException e) {
      assertEquals( "FATAL: sorry, too many clients already", e.getMessage());
    }
  }

  private int getRemainingAvailableConnections() throws Exception {
    try (Connection conn = createConnection()) {
      Statement stmt = conn.createStatement();
      ResultSet result = stmt.executeQuery("SELECT COUNT(*) FROM pg_stat_activity");
      result.next();
      int count = result.getInt("count");
      conn.close();
      // +2: one for the connection we have just closed, one for the checkpointer process,
      // which appears in the pg_stat_activity, but is not counted toward ysql_max_connections.
      return MAX_CONNECTIONS - count + 2;
    }
  }

  private Connection createConnection() throws Exception {
    ConnectionBuilder b = getConnectionBuilder();
    b.withTServer(0);
    return b.connect();
  }

  private Connection[] createConnections(int numConnections) throws Exception {
    final Connection[] connections = new Connection[numConnections];
    for (int i = 0; i < numConnections; ++i) {
      connections[i] = createConnection();
    }
    return connections;
  }

  private List<Integer> getConnectionPids(Connection[] connections) throws Exception {
    List<Integer> backendPids = new ArrayList<>();
    for (Connection connection : connections) {
      try (Statement stmt = connection.createStatement()) {
        ResultSet result = stmt.executeQuery("SELECT pg_backend_pid()");
        while (result.next()) {
          backendPids.add(Integer.parseInt(result.getString("pg_backend_pid")));
        }
      }
    }
    return backendPids;
  }

  @Test
  public void testConnectionKills() throws Exception {
    Assume.assumeFalse(BasePgSQLTest.LESSER_PHYSICAL_CONNS,
      isTestRunningWithConnectionManager());

    final int NUM_CONNECTIONS = getRemainingAvailableConnections();

    final Connection[] connections = createConnections(NUM_CONNECTIONS);
    final List<Integer> backendPids = getConnectionPids(connections);

    assertAlreadyAtMaxConnections();

    // Pick a random postgres connection and issue a SIGKILL to it
    final Random random = new Random(System.currentTimeMillis());
    final int killedPidIdx = random.nextInt(NUM_CONNECTIONS);
    final Integer killedPid = backendPids.get(killedPidIdx);
    ProcessUtil.signalProcess(killedPid, "KILL");

    // Check for liveness of all the postgres connections except the one
    // that is killed.
    for (int i = 0; i < NUM_CONNECTIONS; ++i) {
      try (Statement stmt = connections[i].createStatement()) {
        stmt.execute(String.format("SELECT %d AS text", i));
        assertNotEquals(backendPids.get(i), killedPid);
      } catch (PSQLException e) {
        assertEquals(backendPids.get(i), killedPid);
      }
    }

    // Addding new connections also work
    final Connection newConnection = createConnection();
    try (Statement stmt = newConnection.createStatement()) {
      ResultSet result = stmt.executeQuery("SELECT pg_backend_pid()");
      while (result.next()) {
        LOG.info("add new connection pg_backend_pid: " +
            Integer.parseInt(result.getString("pg_backend_pid")));
      }
    }
  }

  @Test
  public void testConnectionKillsAndRestarts() throws Exception {
    Assume.assumeFalse(BasePgSQLTest.LESSER_PHYSICAL_CONNS,
      isTestRunningWithConnectionManager());

    final int NUM_CONNECTIONS = getRemainingAvailableConnections();

    // Create N connections, kill them all. Repeat this process a few times
    // to verify that we don't eventually run out
    for (int repetition = 0; repetition < 3; ++repetition) {
      final Connection[] connections = createConnections(NUM_CONNECTIONS);
      final List<Integer> backendPids = getConnectionPids(connections);

      assertAlreadyAtMaxConnections();

      // Force kill all processes
      for (int pid : backendPids) {
        ProcessUtil.signalProcess(pid, "KILL");
      }

      // Verify that all the connections are invalid
      for (int i = 0; i < NUM_CONNECTIONS; ++i) {
        try (Statement stmt = connections[i].createStatement()) {
          stmt.execute(String.format("SELECT %d AS text", i));
          fail("Expected statement to fail because the connection is invalid");
        } catch (PSQLException e) {
        }
      }
    }
  }
}
