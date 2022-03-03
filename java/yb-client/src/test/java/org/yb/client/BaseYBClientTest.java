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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;
import org.junit.AfterClass;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.Schema;
import org.yb.Type;
import org.yb.minicluster.BaseMiniClusterTest;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.yb.AssertionWrappers.fail;

/**
 * A base class for tests using the the Java YB client.
 */
public class BaseYBClientTest extends BaseMiniClusterTest {

    // Default keyspace name.
  public static final String DEFAULT_KEYSPACE_NAME = "default_keyspace";

  private static final Logger LOG = LoggerFactory.getLogger(BaseYBClientTest.class);

  // We create both versions of the client (synchronous and asynchronous) for ease of use.
  protected static AsyncYBClient client;
  protected static YBClient syncClient;
  protected static Schema basicSchema = getBasicSchema();
  protected static Schema hashKeySchema = getHashKeySchema();
  protected static Schema allTypesSchema = getSchemaWithAllTypes();
  protected static Schema redisSchema = getRedisSchema();

  @Override
  protected void afterStartingMiniCluster() throws Exception {
    super.afterStartingMiniCluster();
    LOG.info("Creating new YB client...");
    client = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses)
        .defaultAdminOperationTimeoutMs(DEFAULT_SLEEP)
        .defaultOperationTimeoutMs(DEFAULT_SLEEP)
        .defaultSocketReadTimeoutMs(DEFAULT_SLEEP)
        .sslCertFile(certFile)
        .sslClientCertFiles(clientCertFile, clientKeyFile)
        .bindHostAddress(clientHost, clientPort)
        .build();

    syncClient = new YBClient(client);
    syncClient.createKeyspace(DEFAULT_KEYSPACE_NAME);
  }

  private static void destroyClient() throws Exception {
    if (client != null) {
      client.shutdown();
      // No need to explicitly shutdown the sync client,
      // shutting down the async client effectively does that.
      client = null;
    }

    // TODO: do we need to shutdown / close syncClient here?
  }

  public static void destroyClientAndMiniCluster() throws Exception {
    try {
      destroyClient();
    } finally {
      destroyMiniCluster();
    }
  }

  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    destroyClientAndMiniCluster();
  }

  protected static YBTable createTable(String tableName, Schema schema,
                                       CreateTableOptions builder) {
    LOG.info("Creating table: {}", tableName);
    Deferred<YBTable> d = client.createTable(DEFAULT_KEYSPACE_NAME, tableName, schema, builder);
    final AtomicBoolean gotError = new AtomicBoolean(false);
    d.addErrback(arg -> {
      gotError.set(true);
      LOG.error("Error : " + arg);
      return null;
    });
    YBTable table = null;
    try {
      table = d.join(DEFAULT_SLEEP);
    } catch (Exception e) {
      fail("Timed out");
    }
    if (gotError.get()) {
      fail("Got error during table creation, is the YB master running at " + masterAddresses + "?");
    }
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

  public static Schema getRedisSchema() {
    return YBClient.getRedisSchema();
  }

  public static Schema getBasicSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(5);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.INT32).key(true).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column1_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column2_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column3_s", Type.STRING)
        .nullable(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column4_b", Type.BOOL).build());
    return new Schema(columns);
  }

  public static Schema getHashKeySchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(5);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.INT32).key(true).hashKey(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column1_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column2_i", Type.INT32).build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column3_s", Type.STRING)
        .nullable(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column4_b", Type.BOOL).build());
    return new Schema(columns);
  }

  static Callback<Object, Object> defaultErrorCB = arg -> {
    if (arg == null) return null;
    if (arg instanceof Exception) {
      Exception e = (Exception) arg;
      LOG.warn("Got exception", e);
      return new Exception("Can't recover from error: ", e);
    } else {
      LOG.warn("Got an error response back " + arg);
      return new Exception("Can't recover from error, see previous WARN : " + arg);
    }
  };

  /**
   * Helper method to open a table. It sets the default sleep time when joining on the Deferred.
   * @param name Name of the table
   * @return A YBTable
   * @throws Exception MasterErrorException if the table doesn't exist
   */
  protected static YBTable openTable(String name) throws Exception {
    Deferred<YBTable> d = client.openTable(DEFAULT_KEYSPACE_NAME, name);
    return d.join(DEFAULT_SLEEP);
  }

  /**
   * Helper method to easily kill a tablet server that serves the given table's only tablet's
   * leader. The currently running test case will be failed if there's more than one tablet,
   * if the tablet has no leader after some retries, or if the tablet server was already killed.
   *
   * This method is thread-safe.
   * @param table a YBTable which will get its single tablet's leader killed.
   * @throws Exception
   */
  protected static void killTabletLeader(YBTable table) throws Exception {
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

    HostAndPort leaderHostPort = HostAndPort.fromParts(leader.getRpcHost(), leader.getRpcPort());
    LOG.info("The previous host port is " + leaderHostPort);
    miniCluster.killTabletServerOnHostPort(leaderHostPort);
  }

  protected static void killAllTabletLeader(YBTable table) throws Exception {
    List<LocatedTablet> tablets = table.getTabletsLocations(DEFAULT_SLEEP);
    for (LocatedTablet tablet : tablets) {
      LocatedTablet.Replica leader = null;
      DeadlineTracker deadlineTracker = new DeadlineTracker();
      deadlineTracker.setDeadline(DEFAULT_SLEEP);
      while (leader == null) {
        if (deadlineTracker.timedOut()) {
          fail("Timed out while trying to find a leader for this table: " + table.getName());
        }

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
      HostAndPort leaderHostPort = HostAndPort.fromParts(leader.getRpcHost(), leader.getRpcPort());
      miniCluster.killTabletServerOnHostPort(leaderHostPort);
    }
  }

  /**
   * Find the host/port of the leader master.
   * @return The host/port of the leader master.
   * @throws Exception If we are unable to find the leader master.
   */
  public static HostAndPort findLeaderMasterHostPort() throws Exception {
    return syncClient.getLeaderMasterHostAndPort();
  }

  /**
   * Helper method to easily kill the leader master.
   *
   * This method is thread-safe.
   * @throws Exception If there is an error finding or killing the leader master.
   */
  protected static void killMasterLeader() throws Exception {
    HostAndPort leaderHostPort = findLeaderMasterHostPort();
    miniCluster.killMasterOnHostPort(leaderHostPort);
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
