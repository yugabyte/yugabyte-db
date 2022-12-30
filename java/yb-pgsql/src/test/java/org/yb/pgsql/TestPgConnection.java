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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.concurrent.TimeUnit;

import com.yugabyte.util.PSQLException;

import java.sql.*;
import java.util.*;

import static org.yb.AssertionWrappers.*;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgConnection extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
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

  private Connection[] createConnections(int numConnections) throws Exception {
    final Connection[] connections = new Connection[numConnections];
    // Create numConnections postgres connections
    for (int i = 0; i < numConnections; ++i) {
      ConnectionBuilder b = getConnectionBuilder();
      b.withTServer(0);
      connections[i] = b.connect();
    }
    return connections;
  }

  @Test
  public void testConnectionKills() throws Exception {
    final int NUM_CONNECTIONS = 8;
    final Connection[] connections = createConnections(NUM_CONNECTIONS);
    List<Integer> backendPids = new ArrayList<>();

    // Create postgres connections and store their pids in a list
    for (int i = 0; i < NUM_CONNECTIONS; ++i) {
      try (Statement stmt = connections[i].createStatement()) {
        ResultSet result = stmt.executeQuery(String.format("SELECT pg_backend_pid()"));
        while (result.next()) {
          backendPids.add(Integer.parseInt(result.getString("pg_backend_pid")));
        }
      }
    }

    // pick a random postgres connection and issue a SIGKILL to it
    Random random = new Random(System.currentTimeMillis());
    int killedPidIdx = random.nextInt(NUM_CONNECTIONS);
    Integer killedPid = backendPids.get(killedPidIdx);
    Runtime rt = Runtime.getRuntime();
    rt.exec("kill -9 " + killedPid);

    // Check for liveness of all the postgres connections except the one
    // that is killed.
    for (int i = 0; i < NUM_CONNECTIONS; ++i) {
      try (Statement stmt = connections[i].createStatement()) {
        stmt.execute(String.format("SELECT " + i + " AS text"));
        assertNotEquals(backendPids.get(i), killedPid);
      } catch (PSQLException e) {
        assertEquals(backendPids.get(i), killedPid);
      }
    }

    // Addding new connections also work
    ConnectionBuilder b = getConnectionBuilder();
    b.withTServer(0);
    final Connection newConnection = b.connect();
    try (Statement stmt = newConnection.createStatement()) {
      ResultSet result = stmt.executeQuery(String.format("SELECT pg_backend_pid()"));
      while (result.next()) {
        LOG.info("add new connection pg_backend_pid: " +
          Integer.parseInt(result.getString("pg_backend_pid")));
      }
    }
  }
}
