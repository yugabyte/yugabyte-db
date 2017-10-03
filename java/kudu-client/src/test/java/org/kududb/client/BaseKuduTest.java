// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import static org.junit.Assert.fail;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import com.google.common.collect.ImmutableList;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;
import org.kududb.master.Master;
import org.kududb.util.NetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.common.base.Stopwatch;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;

public class BaseKuduTest {

  private static final Logger LOG = LoggerFactory.getLogger(BaseKuduTest.class);

  private static final String NUM_MASTERS_PROP = "NUM_MASTERS";
  private static final int NUM_TABLET_SERVERS = 3;
  private static final int DEFAULT_NUM_MASTERS = 1;

  // Number of masters that will be started for this test if we're starting
  // a cluster.
  private static final int NUM_MASTERS =
      Integer.getInteger(NUM_MASTERS_PROP, DEFAULT_NUM_MASTERS);

  private static MiniKuduCluster miniCluster;

  // Comma separate describing the master addresses and ports.
  protected static String masterAddresses;
  protected static List<HostAndPort> masterHostPorts;

  protected static final int DEFAULT_SLEEP = 50000;

  // We create both versions of the client for ease of use.
  protected static AsyncKuduClient client;
  protected static KuduClient syncClient;
  protected static Schema basicSchema = getBasicSchema();
  protected static Schema allTypesSchema = getSchemaWithAllTypes();

  private static List<String> tableNames = new ArrayList<>();

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    LOG.info("Setting up before class...");

    miniCluster = new MiniKuduCluster.MiniKuduClusterBuilder()
        .numMasters(NUM_MASTERS)
        .numTservers(NUM_TABLET_SERVERS)
        .defaultTimeoutMs(DEFAULT_SLEEP)
        .build();
    masterAddresses = miniCluster.getMasterAddresses();
    masterHostPorts = miniCluster.getMasterHostPorts();

    LOG.info("Creating new Kudu client...");
    client = new AsyncKuduClient.AsyncKuduClientBuilder(masterAddresses).build();
    syncClient = new KuduClient(client);
    LOG.info("Waiting for tablet servers...");
    if (!miniCluster.waitForTabletServers(NUM_TABLET_SERVERS)) {
      fail("Couldn't get " + NUM_TABLET_SERVERS + " tablet servers running, aborting");
    }
  }

  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    try {
      if (client != null) {
        Deferred<ArrayList<Void>> d = client.shutdown();
        d.addErrback(defaultErrorCB);
        d.join(DEFAULT_SLEEP);
        // No need to explicitly shutdown the sync client,
        // shutting down the async client effectively does that.
      }
    } finally {
      miniCluster.shutdown();
    }
  }

  protected static KuduTable createTable(String tableName, Schema schema,
                                         CreateTableOptions builder) {
    LOG.info("Creating table: {}", tableName);
    Deferred<KuduTable> d = client.createTable(tableName, schema, builder);
    final AtomicBoolean gotError = new AtomicBoolean(false);
    d.addErrback(new Callback<Object, Object>() {
      @Override
      public Object call(Object arg) throws Exception {
        gotError.set(true);
        LOG.error("Error : " + arg);
        return null;
      }
    });
    KuduTable table = null;
    try {
      table = d.join(DEFAULT_SLEEP);
    } catch (Exception e) {
      fail("Timed out");
    }
    if (gotError.get()) {
      fail("Got error during table creation, is the Kudu master running at " +
          masterAddresses + "?");
    }
    tableNames.add(tableName);
    return table;
  }

  /**
   * Counts the rows from the {@code scanner} until exhaustion. It doesn't require the scanner to
   * be new, so it can be used to finish scanning a previously-started scan.
   */
  protected static int countRowsInScan(AsyncKuduScanner scanner)
      throws Exception {
    final AtomicInteger counter = new AtomicInteger();

    Callback<Object, RowResultIterator> cb = new Callback<Object, RowResultIterator>() {
      @Override
      public Object call(RowResultIterator arg) throws Exception {
        if (arg == null) return null;
        counter.addAndGet(arg.getNumRows());
        return null;
      }
    };

    while (scanner.hasMoreRows()) {
      Deferred<RowResultIterator> data = scanner.nextRows();
      data.addCallbacks(cb, defaultErrorCB);
      data.join(DEFAULT_SLEEP);
    }

    Deferred<RowResultIterator> closer = scanner.close();
    closer.addCallbacks(cb, defaultErrorCB);
    closer.join(DEFAULT_SLEEP);
    return counter.get();
  }

  protected List<String> scanTableToStrings(KuduTable table) throws Exception {
    List<String> rowStrings = Lists.newArrayList();
    KuduScanner scanner = syncClient.newScannerBuilder(table).build();
    while (scanner.hasMoreRows()) {
      RowResultIterator rows = scanner.nextRows();
      for (RowResult r : rows) {
        rowStrings.add(r.rowToString());
      }
    }
    Collections.sort(rowStrings);
    return rowStrings;
  }

  private static final int[] KEYS = new int[] {10, 20, 30};
  protected static KuduTable createFourTabletsTableWithNineRows(String tableName) throws
      Exception {
    CreateTableOptions builder = new CreateTableOptions();
    for (int i : KEYS) {
      PartialRow splitRow = basicSchema.newPartialRow();
      splitRow.addInt(0, i);
      builder.addSplitRow(splitRow);
    }
    KuduTable table = createTable(tableName, basicSchema, builder);
    AsyncKuduSession session = client.newSession();

    // create a table with on empty tablet and 3 tablets of 3 rows each
    for (int key1 : KEYS) {
      for (int key2 = 1; key2 <= 3; key2++) {
        Insert insert = table.newInsert();
        PartialRow row = insert.getRow();
        row.addInt(0, key1 + key2);
        row.addInt(1, key1);
        row.addInt(2, key2);
        row.addString(3, "a string");
        row.addBoolean(4, true);
        session.apply(insert).join(DEFAULT_SLEEP);
      }
    }
    session.close().join(DEFAULT_SLEEP);
    return table;
  }

  public static Schema getSchemaWithAllTypes() {
    List<ColumnSchema> columns =
        ImmutableList.of(
            new ColumnSchema.ColumnSchemaBuilder("int8", Type.INT8).key(true).build(),
            new ColumnSchema.ColumnSchemaBuilder("int16", Type.INT16).build(),
            new ColumnSchema.ColumnSchemaBuilder("int32", Type.INT32).build(),
            new ColumnSchema.ColumnSchemaBuilder("int64", Type.INT64).build(),
            new ColumnSchema.ColumnSchemaBuilder("bool", Type.BOOL).build(),
            new ColumnSchema.ColumnSchemaBuilder("float", Type.FLOAT).build(),
            new ColumnSchema.ColumnSchemaBuilder("double", Type.DOUBLE).build(),
            new ColumnSchema.ColumnSchemaBuilder("string", Type.STRING).build(),
            new ColumnSchema.ColumnSchemaBuilder("binary-array", Type.BINARY).build(),
            new ColumnSchema.ColumnSchemaBuilder("binary-bytebuffer", Type.BINARY).build(),
            new ColumnSchema.ColumnSchemaBuilder("null", Type.STRING).nullable(true).build(),
            new ColumnSchema.ColumnSchemaBuilder("timestamp", Type.TIMESTAMP).build());

    return new Schema(columns);
  }

  public static Schema getBasicSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(5);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.INT32).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column1_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column2_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column3_s", Type.STRING)
        .nullable(true)
        .desiredBlockSize(4096)
        .encoding(ColumnSchema.Encoding.DICT_ENCODING)
        .compressionAlgorithm(ColumnSchema.CompressionAlgorithm.LZ4)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column4_b", Type.BOOL).build());
    return new Schema(columns);
  }

  protected Insert createBasicSchemaInsert(KuduTable table, int key) {
    Insert insert = table.newInsert();
    PartialRow row = insert.getRow();
    row.addInt(0, key);
    row.addInt(1, 2);
    row.addInt(2, 3);
    row.addString(3, "a string");
    row.addBoolean(4, true);
    return insert;
  }

  static Callback<Object, Object> defaultErrorCB = new Callback<Object, Object>() {
    @Override
    public Object call(Object arg) throws Exception {
      if (arg == null) return null;
      if (arg instanceof Exception) {
        LOG.warn("Got exception", (Exception) arg);
      } else {
        LOG.warn("Got an error response back " + arg);
      }
      return new Exception("Can't recover from error, see previous WARN");
    }
  };

  /**
   * Helper method to open a table. It sets the default sleep time when joining on the Deferred.
   * @param name Name of the table
   * @return A KuduTable
   * @throws Exception MasterErrorException if the table doesn't exist
   */
  protected static KuduTable openTable(String name) throws Exception {
    Deferred<KuduTable> d = client.openTable(name);
    return d.join(DEFAULT_SLEEP);
  }

  /**
   * Helper method to easily kill a tablet server that serves the given table's only tablet's
   * leader. The currently running test case will be failed if there's more than one tablet,
   * if the tablet has no leader after some retries, or if the tablet server was already killed.
   *
   * This method is thread-safe.
   * @param table a KuduTable which will get its single tablet's leader killed.
   * @throws Exception
   */
  protected static void killTabletLeader(KuduTable table) throws Exception {
    LocatedTablet.Replica leader = null;
    DeadlineTracker deadlineTracker = new DeadlineTracker();
    deadlineTracker.setDeadline(DEFAULT_SLEEP);
    while (leader == null) {
      if (deadlineTracker.timedOut()) {
        fail("Timed out while trying to find a leader for this table: " + table.getName());
      }
      List<LocatedTablet> tablets = table.getTabletsLocations(DEFAULT_SLEEP);
      if (tablets.isEmpty() || tablets.size() > 1) {
        fail("Currently only support killing leaders for tables containing 1 tablet, table " +
            table.getName() + " has " + tablets.size());
      }
      LocatedTablet tablet = tablets.get(0);
      if (tablet.getReplicas().size() == 1) {
        fail("Table " + table.getName() + " only has 1 tablet, please enable replication");
      }
      leader = tablet.getLeaderReplica();
      if (leader == null) {
        LOG.info("Sleeping while waiting for a tablet LEADER to arise, currently slept " +
            deadlineTracker.getElapsedMillis() + "ms");
        Thread.sleep(50);
      }
    }

    Integer port = leader.getRpcPort();
    miniCluster.killTabletServerOnPort(port);
  }

  /**
   * Helper method to easily kill the leader master.
   *
   * This method is thread-safe.
   * @throws Exception If there is an error finding or killing the leader master.
   */
  protected static void killMasterLeader() throws Exception {
    int leaderPort = findLeaderMasterPort();
    miniCluster.killMasterOnPort(leaderPort);
  }

  /**
   * Find the port of the leader master in order to retrieve it from the port to process map.
   * @return The port of the leader master.
   * @throws Exception If we are unable to find the leader master.
   */
  protected static int findLeaderMasterPort() throws Exception {
    Stopwatch sw = new Stopwatch().start();
    int leaderPort = -1;
    while (leaderPort == -1 && sw.elapsedMillis() < DEFAULT_SLEEP) {
      Deferred<Master.GetTableLocationsResponsePB> masterLocD = client.getMasterTableLocationsPB();
      Master.GetTableLocationsResponsePB r = masterLocD.join(DEFAULT_SLEEP);
      leaderPort = r.getTabletLocations(0)
          .getReplicas(0)
          .getTsInfo()
          .getRpcAddresses(0)
          .getPort();
    }
    if (leaderPort == -1) {
      fail("No leader master found after " + DEFAULT_SLEEP + " ms.");
    }
    return leaderPort;
  }

  /**
   * Return the comma-separated list of "host:port" pairs that describes the master
   * config for this cluster.
   * @return The master config string.
   */
  protected static String getMasterAddresses() {
    return masterAddresses;
  }
}
