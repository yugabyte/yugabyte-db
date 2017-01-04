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
package org.yb.client;

import com.google.common.net.HostAndPort;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.Common;
import org.yb.Common.TableType;
import org.yb.Schema;
import org.yb.Type;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.consensus.Metadata;
import org.yb.master.Master;

import com.stumbleupon.async.Deferred;

/**
 * A synchronous and thread-safe client for YB.
 * <p>
 * This class acts as a wrapper around {@link AsyncYBClient}. The {@link Deferred} objects are
 * joined against using the default admin operation timeout
 * (see {@link org.yb.client.YBClient.YBClientBuilder#defaultAdminOperationTimeoutMs(long)} (long)}).
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class YBClient implements AutoCloseable {

  public static final Logger LOG = LoggerFactory.getLogger(YBClient.class);

  private final AsyncYBClient asyncClient;

  // Number of retries on retriable errors, could make it time based as needed.
  private static final int MAX_NUM_RETRIES = 25;

  // Redis default table name.
  public static final String REDIS_DEFAULT_TABLE_NAME = ".redis";

  // Redis key column name.
  public static final String REDIS_KEY_COLUMN_NAME = "key";

  // 2 power 16
  public static final int TWO_POWER_SIXTEEN = (int)Math.pow(2, 16);

  public YBClient(AsyncYBClient asyncClient) {
    this.asyncClient = asyncClient;
  }

  /**
   * Create redis table options object.
   * @param numTablets number of pre-split tablets.
   * @return an options object to be used during table creation.
   */
  public static CreateTableOptions getRedisTableOptions(int numTablets) {
    CreateTableOptions cto = new CreateTableOptions();

    if (numTablets == 0) {
      LOG.info("Number of tablets cannot be zero, defaulting it to 1.");
      numTablets = 1;
    }

    cto.setTableType(TableType.REDIS_TABLE_TYPE)
       .setNumTablets(numTablets);

    return cto;
  }

  /**
   * Create a redis table on the cluster with the specified name.
   * @param name Tables name
   * @param numTablets number of pre-split tablets
   * @return an object to communicate with the created table.
   */
  public YBTable createRedisTable(String name, int numTablets) throws Exception {
    return createTable(name, getRedisSchema(), getRedisTableOptions(numTablets));
  }

  /**
   * Create a table on the cluster with the specified name and schema. Default table
   * configurations are used, mainly the table will have one tablet.
   * @param name Table's name
   * @param schema Table's schema
   * @return an object to communicate with the created table
   */
  public YBTable createTable(String name, Schema schema) throws Exception {
    return createTable(name, schema, new CreateTableOptions());
  }

  /**
   * Create a table on the cluster with the specified name, schema, and table configurations.
   * @param name the table's name
   * @param schema the table's schema
   * @param builder a builder containing the table's configurations
   * @return an object to communicate with the created table
   */
  public YBTable createTable(String name, Schema schema, CreateTableOptions builder)
      throws Exception {
    Deferred<YBTable> d = asyncClient.createTable(name, schema, builder);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Delete a table on the cluster with the specified name.
   * @param name the table's name
   * @return an rpc response object
   */
  public DeleteTableResponse deleteTable(String name) throws Exception {
    Deferred<DeleteTableResponse> d = asyncClient.deleteTable(name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Alter a table on the cluster as specified by the builder.
   *
   * When the method returns it only indicates that the master accepted the alter
   * command, use {@link YBClient#isAlterTableDone(String)} to know when the alter finishes.
   * @param name the table's name, if this is a table rename then the old table name must be passed
   * @param ato the alter table builder
   * @return an rpc response object
   */
  public AlterTableResponse alterTable(String name, AlterTableOptions ato) throws Exception {
    Deferred<AlterTableResponse> d = asyncClient.alterTable(name, ato);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Helper method that checks and waits until the completion of an alter command.
   * It will block until the alter command is done or the timeout is reached.
   * @param name Table's name, if the table was renamed then that name must be checked against
   * @return a boolean indicating if the table is done being altered
   */
  public boolean isAlterTableDone(String name) throws Exception {
    long totalSleepTime = 0;
    while (totalSleepTime < getDefaultAdminOperationTimeoutMs()) {
      long start = System.currentTimeMillis();

      Deferred<IsAlterTableDoneResponse> d = asyncClient.isAlterTableDone(name);
      IsAlterTableDoneResponse response;
      try {
        response = d.join(AsyncYBClient.SLEEP_TIME);
      } catch (Exception ex) {
        throw ex;
      }

      if (response.isDone()) {
        return true;
      }

      // Count time that was slept and see if we need to wait a little more.
      long elapsed = System.currentTimeMillis() - start;
      // Don't oversleep the deadline.
      if (totalSleepTime + AsyncYBClient.SLEEP_TIME > getDefaultAdminOperationTimeoutMs()) {
        return false;
      }
      // elapsed can be bigger if we slept about 500ms
      if (elapsed <= AsyncYBClient.SLEEP_TIME) {
        LOG.debug("Alter not done, sleep " + (AsyncYBClient.SLEEP_TIME - elapsed) +
            " and slept " + totalSleepTime);
        Thread.sleep(AsyncYBClient.SLEEP_TIME - elapsed);
        totalSleepTime += AsyncYBClient.SLEEP_TIME;
      } else {
        totalSleepTime += elapsed;
      }
    }
    return false;
  }

  /**
   * Get the list of running tablet servers.
   * @return a list of tablet servers
   */
  public ListTabletServersResponse listTabletServers() throws Exception {
    Deferred<ListTabletServersResponse> d = asyncClient.listTabletServers();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of all the masters.
   * @return a list of masters
   */
  public ListMastersResponse listMasters() throws Exception {
    Deferred<ListMastersResponse> d = asyncClient.listMasters();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the current cluster configuration.
   * @return the configuration
   */
  public GetMasterClusterConfigResponse getMasterClusterConfig() throws Exception {
    Deferred<GetMasterClusterConfigResponse> d = asyncClient.getMasterClusterConfig();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Change the current cluster configuration.
   * @param config the new config to set on the cluster.
   * @return the configuration
   */
  public ChangeMasterClusterConfigResponse changeMasterClusterConfig(
      Master.SysClusterConfigEntryPB config) throws Exception {
    Deferred<ChangeMasterClusterConfigResponse> d = asyncClient.changeMasterClusterConfig(config);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the tablet load move completion percentage for blacklisted nodes, if any.
   * @return the response with percent load completed.
   */
  public GetLoadMovePercentResponse getLoadMoveCompletion() throws Exception {
    Deferred<GetLoadMovePercentResponse> d;
    GetLoadMovePercentResponse resp;
    int numTries = 0;
    do {
      d = asyncClient.getLoadMoveCompletion();
      resp = d.join(getDefaultAdminOperationTimeoutMs());
    } while (resp.hasRetriableError() && numTries++ < MAX_NUM_RETRIES);
    return resp;
  }

  /**
   * Check if the tablet load is balanced as per the master leader.
   * @return a deferred object that yields if the load is balanced.
   */
  public IsLoadBalancedResponse getIsLoadBalanced() throws Exception {
    Deferred<IsLoadBalancedResponse> d = asyncClient.getIsLoadBalanced();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Find the uuid of a master using its host/port.
   * @return The uuid of the master, or null if not found.
   * @throws Nothing.
   */
  String getMasterUUID(String host, int port) {
    HostAndPort hostAndPort = HostAndPort.fromParts(host, port);
    Deferred<GetMasterRegistrationResponse> d;
    TabletClient clientForHostAndPort = asyncClient.newMasterClient(hostAndPort);
    if (clientForHostAndPort == null) {
      String message = "Couldn't resolve master's address at " + hostAndPort.toString();
      LOG.warn(message);
    } else {
      d = asyncClient.getMasterRegistration(clientForHostAndPort);
      try {
        GetMasterRegistrationResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
        return resp.getInstanceId().getPermanentUuid().toStringUtf8();
      } catch (Exception e) {
        LOG.warn("Couldn't get registration info for master " + hostAndPort.toString());
      }
    }

    return null;
  }

  /**
   * Find the uuid of the leader master.
   * @return The uuid of the leader master, or null if no leader found.
   * @throws Nothing.
   */
  public String getLeaderMasterUUID() {
    for (HostAndPort hostAndPort : asyncClient.getMasterAddresses()) {
      Deferred<GetMasterRegistrationResponse> d;
      TabletClient clientForHostAndPort = asyncClient.newMasterClient(hostAndPort);
      if (clientForHostAndPort == null) {
        String message = "Couldn't resolve this master's address " + hostAndPort.toString();
        LOG.warn(message);
      } else {
        d = asyncClient.getMasterRegistration(clientForHostAndPort);
        try {
          GetMasterRegistrationResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
          if (resp.getRole() == Metadata.RaftPeerPB.Role.LEADER) {
            return resp.getInstanceId().getPermanentUuid().toStringUtf8();
          }
        } catch (Exception e) {
          LOG.warn("Couldn't get registration info for master " + hostAndPort.toString());
        }
      }
    }

    return null;
  }

  /**
   * Find the host/port of the leader master.
   * @return The host and port of the leader master, or null if no leader found.
   * @throws Nothing.
   */
  public HostAndPort getLeaderMasterHostAndPort() {
    for (HostAndPort hostAndPort : asyncClient.getMasterAddresses()) {
      Deferred<GetMasterRegistrationResponse> d;
      TabletClient clientForHostAndPort = asyncClient.newMasterClient(hostAndPort);
      if (clientForHostAndPort == null) {
        String message = "Couldn't resolve this master's host/port " + hostAndPort.toString();
        LOG.warn(message);
      } else {
        d = asyncClient.getMasterRegistration(clientForHostAndPort);
        try {
          GetMasterRegistrationResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
          if (resp.getRole() == Metadata.RaftPeerPB.Role.LEADER) {
            return hostAndPort;
          }
        } catch (Exception e) {
          LOG.warn("Couldn't get registration info for master " + hostAndPort.toString());
        }
      }
    }

    return null;
  }

  /**
   * Helper API to wait and get current leader's UUID. This takes care of waiting for any election
   * in progress.
   *
   * @param timeout Amount of time to try getting the leader uuid.
   * @return Master leader uuid on success, null otherwise.
   */
  private String waitAndGetLeaderMasterUUID(long timeoutMs) throws Exception {
    long start = System.currentTimeMillis();

    // Retry till we get a valid UUID (or timeout) for the new leader.
    do {
      String leaderUuid = getLeaderMasterUUID();

      // Done if we got a valid one.
      if (leaderUuid != null) {
        return leaderUuid;
      }

      Thread.sleep(asyncClient.SLEEP_TIME);
    } while (System.currentTimeMillis() < start + timeoutMs);

    LOG.error("Timed out getting leader uuid.");

    return null;
  }

 /**
   * Change master server configuration.
   *
   * @param  leaderUuid Current master leader's uuid.
   * @return The new leader uuid, on success.
   * @exception if the new leader is the same as the old leader after certain number of tries.
   */
  private String stepDownMasterLeaderAndWaitForNewLeader(String leaderUuid) throws Exception {
    String tabletId = getMasterTabletId();
    String newLeader = leaderUuid;

    int numIters = 0;
    int maxNumIters = 25;
    String errorMsg = null;
    try {
      // TODO: This while loop will not be needed once JIRA ENG-49 is fixed.
      do {
        Deferred<LeaderStepDownResponse> d = asyncClient.masterLeaderStepDown(leaderUuid,
                                                                              tabletId);
        LeaderStepDownResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
        if (resp.hasError()) {
          errorMsg = "Master leader step down hit error " + resp.errorMessage();
          break;
        }
        newLeader = waitAndGetLeaderMasterUUID(getDefaultAdminOperationTimeoutMs());
        if (newLeader == null) {
          errorMsg = "Timed out as we could not find a valid new leader. Old leader is " +
                     leaderUuid;
          break;
        }

        // Done if we found a new leader.
        if (!newLeader.equals(leaderUuid)) {
          break;
        }

        numIters++;
        LOG.info("Try step down {}, new master {}, iter {}.", leaderUuid, newLeader, numIters);
        if (numIters >= maxNumIters) {
          errorMsg = "Maximum iterations reached trying to step down the " +
                     "leader master with uuid " + leaderUuid;
          break;
        }

        Thread.sleep(asyncClient.SLEEP_TIME);
      } while (true);
    } catch (Exception e) {
     // TODO: Ideally we need an error code here, but this is come another layer which
     // eats up the NOT_THE_LEADER code.
     if (e.getMessage().contains("Wrong destination UUID requested")) {
        LOG.info("Got wrong destination for {} stepdown.", leaderUuid);
        newLeader = waitAndGetLeaderMasterUUID(getDefaultAdminOperationTimeoutMs());
        if (newLeader == null || newLeader.equals(leaderUuid)) {
          errorMsg = "Could not find a valid new leader. Old leader is " + leaderUuid +
                     ", new leader is " + newLeader;
        }
      } else {
        LOG.error("Error trying to step down {}. Error: .", leaderUuid, e);
        throw new RuntimeException("Could not step down leader master " + leaderUuid, e);
      }
    }

    if (errorMsg != null) {
      LOG.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }

    LOG.info("Step down of {} done, new master uuid={}.", leaderUuid, newLeader);

    return newLeader;
  }

  /**
   * Change master configuration.
   *
   * @param host Master host that is being added or removed.
   * @param port RPC port of the host being added or removed.
   * @param isAdd If we are adding or removing the master to the configuration.
   *
   * @return The change config response object.
   */
  public ChangeConfigResponse changeMasterConfig(
      String host, int port, boolean isAdd) throws Exception {
    String changeUuid = getMasterUUID(host, port);
    if (changeUuid == null) {
      throw new IllegalArgumentException("Invalid master host/port of " + host + "/" +
                                          port + " - could not get it's uuid.");
    }
    String leaderUuid = getLeaderMasterUUID();
    if (leaderUuid == null) {
      throw new IllegalStateException("Invalid setup - could not find the leader master in " +
                                      asyncClient.getMasterAddresses());
    }

    boolean didStepDown = false;
    // If caller is trying to remove the leader, then step it down first and wait for a new one to
    // be elected.
    if (!isAdd && leaderUuid.equals(changeUuid)) {
      // We just set the leader in this case for tracing purposes, but change config call will
      // not use it.
      leaderUuid = stepDownMasterLeaderAndWaitForNewLeader(leaderUuid);
      didStepDown = true;
    }

    LOG.info("Sending changeConfig to leader {}: Target host/port={}/{} at uuid={}, add={}, " +
             "stepdown={}.", leaderUuid, host, port, changeUuid, isAdd, didStepDown);

    // For a new leader after stepdown, the sendRpcToTablet will retry to get the correct leader.
    // Seemed very intrusive to change request's uuid contents during rpc retry.
    // didStepDown can be removed once a "proxy" like concept is added to java client.
    Deferred<ChangeConfigResponse> d = asyncClient.changeMasterConfig(
        didStepDown ? "" : leaderUuid, host, port, changeUuid, isAdd);
    ChangeConfigResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
    if (!resp.hasError()) {
      asyncClient.updateMasterAdresses(host, port, isAdd);
    }
    return resp;
  }

  /**
   * Get the basic redis schema.
   * @return redis schema
   */
  public static Schema getRedisSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(1);
    columns.add(new ColumnSchema.ColumnSchemaBuilder(REDIS_KEY_COLUMN_NAME, Type.BINARY)
                                .hashKey(true)
                                .nullable(false)
                                .build());
    return new Schema(columns);
  }

  /**
   * Get the master's tablet id.
   * @return master tablet id.
   */
  public static String getMasterTabletId() {
    return AsyncYBClient.getMasterTabletId();
  }

 /**
  * Wait for the cluster to have successfully elected a Master Leader.
  * @param timeoutMs the amount of time, in MS, to wait until a Leader is present
  */
  public void waitForMasterLeader(long timeoutMs) throws Exception {
    String leaderUuid = waitAndGetLeaderMasterUUID(timeoutMs);

    if (leaderUuid == null) {
      throw new RuntimeException("Timed out waiting for Master Leader.");
    }
  }

  /**
  * Ping a certain server to see if it is responding to RPC requests.
  * @param host the hostname or IP of the server
  * @param port the port number of the server
  * @return true if the server responded to ping
  */
  public boolean ping(final HostAndPort hp) throws Exception {
    Deferred<PingResponse> d = asyncClient.ping(hp);
    PingResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
    return true;
  }

  /**
  * Wait for the specific server to come online.
  * @param hp the HostAndPort of the server
  * @param timeoutMs the amount of time, in MS, to wait
  * @return true if the server responded to pings in the given time, false otherwise
  */
  public boolean waitForServer(final HostAndPort hp, final long timeoutMs) {
    Exception finalException = null;
    long start = System.currentTimeMillis();
    do {
      try {
        if (ping(hp)) {
          return true;
        }
      } catch (Exception e) {
        // We will get exceptions if we cannot connect to the other end. Catch them and save for
        // final debug if we never succeed.
        finalException = e;
      }
      // Need to wait even when ping has an exception, so the sleep is outside the above try block.
      try {
        Thread.sleep(AsyncYBClient.SLEEP_TIME);
      } catch (Exception e) {}
    } while (System.currentTimeMillis() < start + timeoutMs);

    LOG.warn("Timed out waiting for server {} to come online. Final exception was {}.",
             hp.toString(), finalException != null ? finalException.toString() : "none");

    return false;
  }

  /**
  * Wait for the tablet load to be balanced by master leader.
  * @param timeoutMs the amount of time, in MS, to wait
  * @return true if the master leader does not return any error balance check.
  */
  public boolean waitForLoadBalance(final long timeoutMs) {
    Exception finalException = null;
    long start = System.currentTimeMillis();
    do {
      try {
        IsLoadBalancedResponse resp = getIsLoadBalanced();
        if (!resp.hasError()) {
          return true;
        }
      } catch (Exception e) {
        // We will get exceptions if we cannot connect to the other end. Catch them and save for
        // final debug if we never succeed.
        finalException = e;
      }
      // Need to wait even when check has an exception, so sleep is outside the above try block.
      try {
        Thread.sleep(AsyncYBClient.SLEEP_TIME);
      } catch (Exception e) {}
    } while ((timeoutMs == Long.MAX_VALUE) || System.currentTimeMillis() < start + timeoutMs);

    LOG.warn("Timed out waiting for load to balance. Final exception was {}.",
             finalException != null ? finalException.toString() : "none");
    return false;
  }

  /**
   * Change master server configuration.
   * @return status of the step down via a response.
   */
  public LeaderStepDownResponse masterLeaderStepDown() throws Exception {
    String leader_uuid = getLeaderMasterUUID();
    String tablet_id = getMasterTabletId();
    Deferred<LeaderStepDownResponse> d = asyncClient.masterLeaderStepDown(leader_uuid, tablet_id);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of all the tables.
   * @return a list of all the tables
   */
  public ListTablesResponse getTablesList() throws Exception {
    return getTablesList(null);
  }

  /**
   * Get a list of table names. Passing a null filter returns all the tables. When a filter is
   * specified, it only returns tables that satisfy a substring match.
   * @param nameFilter an optional table name filter
   * @return a deferred that contains the list of table names
   */
  public ListTablesResponse getTablesList(String nameFilter) throws Exception {
    Deferred<ListTablesResponse> d = asyncClient.getTablesList(nameFilter);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Test if a table exists.
   * @param name a non-null table name
   * @return true if the table exists, else false
   */
  public boolean tableExists(String name) throws Exception {
    Deferred<Boolean> d = asyncClient.tableExists(name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Open the table with the given name. If the table was just created, this method will block until
   * all its tablets have also been created.
   * @param name table to open
   * @return a YBTable if the table exists, else a MasterErrorException
   */
  public YBTable openTable(final String name) throws Exception {
    Deferred<YBTable> d = asyncClient.openTable(name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Create a new session for interacting with the cluster.
   * User is responsible for destroying the session object.
   * This is a fully local operation (no RPCs or blocking).
   * @return a synchronous wrapper around YBSession.
   */
  public YBSession newSession() {
    AsyncYBSession session = asyncClient.newSession();
    return new YBSession(session);
  }

  /**
   * Creates a new {@link YBScanner.YBScannerBuilder} for a particular table.
   * @param table the name of the table you intend to scan.
   * The string is assumed to use the platform's default charset.
   * @return a new scanner builder for this table
   */
  public YBScanner.YBScannerBuilder newScannerBuilder(YBTable table) {
    return new YBScanner.YBScannerBuilder(asyncClient, table);
  }

  /**
   * Analogous to {@link #shutdown()}.
   * @throws Exception if an error happens while closing the connections
   */
  @Override
  public void close() throws Exception {
    asyncClient.close();
  }

  /**
   * Performs a graceful shutdown of this instance.
   * @throws Exception
   */
  public void shutdown() throws Exception {
    Deferred<ArrayList<Void>> d = asyncClient.shutdown();
    d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the timeout used for operations on sessions and scanners.
   * @return a timeout in milliseconds
   */
  public long getDefaultOperationTimeoutMs() {
    return asyncClient.getDefaultOperationTimeoutMs();
  }

  /**
   * Get the timeout used for admin operations.
   * @return a timeout in milliseconds
   */
  public long getDefaultAdminOperationTimeoutMs() {
    return asyncClient.getDefaultAdminOperationTimeoutMs();
  }

  /**
   * Builder class to use in order to connect to YB.
   * All the parameters beyond those in the constructors are optional.
   */
  public final static class YBClientBuilder {
    private AsyncYBClient.AsyncYBClientBuilder clientBuilder;

    /**
     * Creates a new builder for a client that will connect to the specified masters.
     * @param masterAddresses comma-separated list of "host:port" pairs of the masters
     */
    public YBClientBuilder(String masterAddresses) {
      clientBuilder = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses);
    }

    /**
     * Creates a new builder for a client that will connect to the specified masters.
     *
     * <p>Here are some examples of recognized formats:
     * <ul>
     *   <li>example.com
     *   <li>example.com:80
     *   <li>192.0.2.1
     *   <li>192.0.2.1:80
     *   <li>[2001:db8::1]
     *   <li>[2001:db8::1]:80
     *   <li>2001:db8::1
     * </ul>
     *
     * @param masterAddresses list of master addresses
     */
    public YBClientBuilder(List<String> masterAddresses) {
      clientBuilder = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses);
    }

    /**
     * Sets the default timeout used for administrative operations (e.g. createTable, deleteTable,
     * etc).
     * Optional.
     * If not provided, defaults to 10s.
     * A value of 0 disables the timeout.
     * @param timeoutMs a timeout in milliseconds
     * @return this builder
     */
    public YBClientBuilder defaultAdminOperationTimeoutMs(long timeoutMs) {
      clientBuilder.defaultAdminOperationTimeoutMs(timeoutMs);
      return this;
    }

    /**
     * Sets the default timeout used for user operations (using sessions and scanners).
     * Optional.
     * If not provided, defaults to 10s.
     * A value of 0 disables the timeout.
     * @param timeoutMs a timeout in milliseconds
     * @return this builder
     */
    public YBClientBuilder defaultOperationTimeoutMs(long timeoutMs) {
      clientBuilder.defaultOperationTimeoutMs(timeoutMs);
      return this;
    }

    /**
     * Sets the default timeout to use when waiting on data from a socket.
     * Optional.
     * If not provided, defaults to 5s.
     * A value of 0 disables the timeout.
     * @param timeoutMs a timeout in milliseconds
     * @return this builder
     */
    public YBClientBuilder defaultSocketReadTimeoutMs(long timeoutMs) {
      clientBuilder.defaultSocketReadTimeoutMs(timeoutMs);
      return this;
    }

    /**
     * Set the executors which will be used for the embedded Netty boss and workers.
     * Optional.
     * If not provided, uses a simple cached threadpool. If either argument is null,
     * then such a thread pool will be used in place of that argument.
     * Note: executor's max thread number must be greater or equal to corresponding
     * worker count, or netty cannot start enough threads, and client will get stuck.
     * If not sure, please just use CachedThreadPool.
     */
    public YBClientBuilder nioExecutors(Executor bossExecutor, Executor workerExecutor) {
      clientBuilder.nioExecutors(bossExecutor, workerExecutor);
      return this;
    }

    /**
     * Set the maximum number of boss threads.
     * Optional.
     * If not provided, 1 is used.
     */
    public YBClientBuilder bossCount(int bossCount) {
      clientBuilder.bossCount(bossCount);
      return this;
    }

    /**
     * Set the maximum number of worker threads.
     * Optional.
     * If not provided, (2 * the number of available processors) is used.
     */
    public YBClientBuilder workerCount(int workerCount) {
      clientBuilder.workerCount(workerCount);
      return this;
    }

    /**
     * Creates a new client that connects to the masters.
     * Doesn't block and won't throw an exception if the masters don't exist.
     * @return a new asynchronous YB client
     */
    public YBClient build() {
      AsyncYBClient client = clientBuilder.build();
      return new YBClient(client);
    }

  }
}
