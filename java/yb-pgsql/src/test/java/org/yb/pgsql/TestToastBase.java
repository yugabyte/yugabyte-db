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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.google.common.net.HostAndPort;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public abstract class TestToastBase extends BasePgSQLTest {
  static final String TABLE_NAME = "t";
  static final int NUM_ROWS = 10;
  static final int NUM_OBJECTS = 10;
  static final int KB = 1024;
  static final int MB = 1024 * KB;
  private static final Logger LOG = LoggerFactory.getLogger(TestToastBase.class);
  String object_template;

  /** For compression tests. */
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_pg_conf_csv", "max_stack_depth='7680kB'");
    return flags;
  }

  protected void setEnableToastFlag(boolean enable) {
    int flagValue = enable ? 2048 : -1;
    miniCluster
        .getTabletServers()
        .keySet()
        .forEach(
            ts -> {
              try {
                setServerFlag(ts, "ysql_yb_toast_catcache_threshold",
                  Integer.toString(flagValue));
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });
  }

  @Before
  public void createTables() throws SQLException {
    try (Statement statement = connection.createStatement()) {
      statement.execute(
          "CREATE TABLE "
              + TABLE_NAME
              + " AS SELECT i AS a, i AS b, i AS c FROM generate_series(0, "
              + (NUM_ROWS - 1)
              + ") i");
    }
  }

  void executeNonBreakingDdl(Connection conn) throws SQLException {
    // Do a non-breaking catalog change on the main connection so that the next DML on the other
    // connections will force a catalog refresh upon their next query (but in-flight transactions
    // will not be aborted).
    try (Statement statement = conn.createStatement()) {
      statement.execute("ALTER TABLE " + TABLE_NAME + " ADD COLUMN d INT");
      statement.execute("ALTER TABLE " + TABLE_NAME + " DROP COLUMN d");
    }
  }

  Connection rebuildRelcacheInitFile() throws Exception {
    executeNonBreakingDdl(connection);
    // Create a dummy connection that will build the relcache init file.
    return getConnectionBuilder().connect();
  }

  /**
   * Verifies that the result set contains the expected rows for the test query
   * SELECT a FROM TABLE_NAME ORDER BY a.
   * <p>
   * If originalValues is true, then the expected values are 0, 1, 2, ..., NUM_ROWS - 1.
   * Otherwise, the expected values are all 0.
   */
  void verifyResults(ResultSet rs, boolean originalValues) throws SQLException {
      for (int j = 0; j < NUM_ROWS; j++) {
        assertTrue(rs.next());
        int expectedValue = originalValues ? j : 0;
        assertEquals(expectedValue, rs.getInt(1));
      }
      assertFalse(rs.next());
  }

  void selectObjectAndCheckResults(int index, Connection conn,
                                   boolean originalValues) throws SQLException {
    try (Statement statement = conn.createStatement()) {
      statement.execute(selectObject(index));

      // Verify that the results are as expected.
      try (ResultSet rs = statement.getResultSet()) {
        verifyResults(rs, originalValues);
      }
    }
  }


  void refreshCatalog(Connection conn) throws SQLException {
    try (Statement statement = conn.createStatement()) {
      statement.execute("SELECT a FROM " + TABLE_NAME);
    }
  }

  void selectObjectsAndCheckResults(Connection conn, boolean originalValues) throws SQLException {
    for (int i = 0; i < NUM_OBJECTS; i++) {
      selectObjectAndCheckResults(i, conn, originalValues);
    }
  }

  abstract void createObject(int index, int size, boolean originalTemplate) throws SQLException;

  void createObjects() throws SQLException {
    for (int i = 0; i < NUM_OBJECTS; i++) {
      createObject(i, 1000, true);
    }
  }

  abstract String selectObject(int index);

  @Test
  public void correctnessTest() throws SQLException {
    for (int i = 0; i < 10; i++) {
      createObject(1, 1000, true);
      selectObjectAndCheckResults(1, connection, true);
    }

    // Now replace the object body, and check that it still works.
    for (int i = 0; i < 10; i++) {
      createObject(1, 1000, false);
      selectObjectAndCheckResults(1, connection, false);
    }
  }

  /** Same as correctnessTest, but with force_parallel_mode enabled. */
  @Test
  public void parallelCorrectnessTest() throws SQLException {
    try (Statement statement = connection.createStatement()) {
      statement.execute("SET force_parallel_mode = TRUE");
    }
    correctnessTest();
    try (Statement statement = connection.createStatement()) {
      statement.execute("RESET force_parallel_mode");
    }
  }

  CacheMemoryContextTracker buildRelcacheInitFileMemoryUsage() throws Exception {
    object_template = "relcache_test_%d";
    // Create some large objects
    createObjects();

    Connection conn = rebuildRelcacheInitFile();
    CacheMemoryContextTracker tracker = new CacheMemoryContextTracker(conn, 0);
    return tracker;
  }

  CacheMemoryContextTracker simpleTest() throws SQLException {
    object_template = "simple_test_%d";
    CacheMemoryContextTracker cxt = new CacheMemoryContextTracker(connection);

    createObjects();

    selectObjectsAndCheckResults(connection, true);
    cxt.measureFinalBytes();
    return cxt;
  }

  List<CacheMemoryContextTracker> catalogRefreshMemoryUsageTest() throws Exception {
    object_template = "refresh_test_%d";
    executeNonBreakingDdl(connection);

    // Create a connection to build the relcache init file (if needed).
    getConnectionBuilder().connect();

    // Create some connections.
    int numConnections = 10;
    List<Connection> connections = new ArrayList<>(numConnections);
    List<CacheMemoryContextTracker> trackers = new ArrayList<>(numConnections);
    for (int i = 0; i < numConnections; ++i) {
      ConnectionBuilder b = getConnectionBuilder();
      connections.add(b.connect());
      trackers.add(new CacheMemoryContextTracker(connections.get(i)));
    }

    // Create some large objects.
    createObjects();

    // Wait for the breaking catalog change to be propagated.
    Thread.sleep(5000);

    // Do a DML on each of the connections to force a catalog refresh.
    for (CacheMemoryContextTracker tracker : trackers) {
      refreshCatalog(tracker.getConnection());
      tracker.measureFinalBytes();
    }
    return trackers;
  }

  List<CacheMemoryContextTracker> adHocMemoryUsageTest() throws Exception {
    object_template = "ad_hoc_test_%d";
    rebuildRelcacheInitFile();

    // Create some connections.
    int numConnections = 1;
    List<CacheMemoryContextTracker> trackers = new ArrayList<>(numConnections);
    for (int i = 0; i < numConnections; i++) {
        trackers.add(new CacheMemoryContextTracker(getConnectionBuilder().connect()));
    }

    // Create some large objects.
    LOG.info("Creating large objects");
    createObjects();

    LOG.info("Selecting from objects");
    // Invoke the objects on each connection so that they will do an ad-hoc catalog lookup
    // for their info.
    for (CacheMemoryContextTracker tracker : trackers) {
      for (int i = 0; i < NUM_OBJECTS; i++) {
        LOG.info(String.format("Selecting from object %d", i));
        selectObjectsAndCheckResults(tracker.getConnection(), true);
      }
      tracker.measureFinalBytes();
    }
    return trackers;
  }
}
