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

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.CommonNet;
import org.yb.CommonTypes;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.Schema;
import org.yb.Type;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.cdc.CdcConsumer.XClusterRole;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.CatalogEntityInfo.ReplicationInfoPB;
import org.yb.tserver.TserverTypes;
import org.yb.util.Pair;

import com.google.common.net.HostAndPort;
import com.stumbleupon.async.Callback;
import com.stumbleupon.async.Deferred;

/**
 * A synchronous and thread-safe client for YB.
 * <p>
 * This class acts as a wrapper around {@link AsyncYBClient}. The {@link Deferred} objects are
 * joined against using the default admin operation timeout
 * (see {@link org.yb.client.YBClient.YBClientBuilder#defaultAdminOperationTimeoutMs(long)}(long)}).
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class YBClient implements AutoCloseable {

  public static final Logger LOG = LoggerFactory.getLogger(YBClient.class);

  private final AsyncYBClient asyncClient;

  // Number of retries on retriable errors, could make it time based as needed.
  private static final int MAX_NUM_RETRIES = 25;

  // Redis default table name.
  public static final String REDIS_DEFAULT_TABLE_NAME = "redis";

  // Redis keyspace name.
  public static final String REDIS_KEYSPACE_NAME = "system_redis";

  // Redis key column name.
  public static final String REDIS_KEY_COLUMN_NAME = "key";

  // Log errors every so many errors.
  private static final int LOG_ERRORS_EVERY_NUM_ITERS = 100;

  // Log info after these many iterations.
  private static final int LOG_EVERY_NUM_ITERS = 200;

  // Simple way to inject an error on Wait based APIs. If enabled, after first inject,
  // it will be turned off. We can enhance it to use more options like every-N etc.
  private boolean injectWaitError = false;

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
    if (numTablets <= 0) {
      String msg = "Number of tablets " + numTablets + " cannot be less than one.";
      LOG.error(msg);
      throw new RuntimeException(msg);
    }

    cto.setTableType(TableType.REDIS_TABLE_TYPE)
       .setNumTablets(numTablets);
    return cto;
  }

  /**
   * Creates the redis namespace "system_redis".
   *
   * @throws Exception if the namespace already exists or creation fails.
   */
  public void createRedisNamespace() throws Exception {
    createRedisNamespace(false);
  }

  /**
   * Creates the redis namespace "system_redis".
   *
   * @param ifNotExist if true, error is ignored if the namespace already exists.
   * @return true if created, false if already exists.
   * @throws Exception throws exception on creation failure.
   */
  public boolean createRedisNamespace(boolean ifNotExist) throws Exception {
    Exception exception = null;
    try {
      // TODO As there is no way to get an existing namespace or create if it does not exist,
      // the RPC call is made first that may fail.
      // Change this when we have better support for namespace.
      CreateKeyspaceResponse resp = this.createKeyspace(REDIS_KEYSPACE_NAME,
          YQLDatabase.YQL_DATABASE_REDIS);
      if (resp.hasError()) {
        exception = new RuntimeException("Could not create keyspace " + REDIS_KEYSPACE_NAME +
            ". Error: " + resp.errorMessage());
      }
    } catch (YBServerException e) {
      // The RPC call also throws exception on conflict.
      exception = e;
    }
    if (exception != null) {
      String errMsg = exception.getMessage();
      if (ifNotExist && errMsg != null && errMsg.contains("ALREADY_PRESENT")) {
        LOG.info("Redis namespace {} already exists.", REDIS_KEYSPACE_NAME);
        return false;
      }
      throw exception;
    }
    return true;
  }

  /**
   * Create a redis table on the cluster with the specified name and tablet count.
   * It also creates the redis namespace 'system_redis'.
   *
   * @param name the table name
   * @param numTablets number of pre-split tablets
   * @return an object to communicate with the created table.
   */
  public YBTable createRedisTable(String name, int numTablets) throws Exception {
    createRedisNamespace();
    return createTable(REDIS_KEYSPACE_NAME, name, getRedisSchema(),
                       getRedisTableOptions(numTablets));
  }

  /**
   * Create a redis table on the cluster with the specified name.
   * It also creates the redis namespace 'system_redis'.
   *
   * @param name the table name
   * @return an object to communicate with the created table.
   */
  public YBTable createRedisTable(String name) throws Exception {
    return createRedisTable(name, false);
  }

  /**
   * Create a redis table on the cluster with the specified name.
   * It also creates the redis namespace 'system_redis'.
   *
   * @param name the table name.
   * @param ifNotExist if true, table is not created if it already exists.
   * @return an object to communicate with the created table if it is created.
   * @throws Exception throws exception on creation failure.
   */
  public YBTable createRedisTable(String name, boolean ifNotExist) throws Exception {
    if (createRedisNamespace(ifNotExist)) {
      // Namespace is just created, so there is no redis table.
      return createRedisTableOnly(name, false);
    }
    return createRedisTableOnly(name, ifNotExist);
  }

  /**
   * Create a redis table on the cluster with the specified name.
   * The redis namespace 'system_redis' must already be existing.
   *
   * @param name the table name.
   * @return an object to communicate with the created table if it is created.
   * @throws Exception throws exception on creation failure.
   */
  public YBTable createRedisTableOnly(String name) throws Exception {
    return createRedisTableOnly(name, false);
  }

  /**
   * Create a redis table on the cluster with the specified name.
   * The redis namespace 'system_redis' must already be existing.
   *
   * @param name the table name.
   * @param ifNotExist if true, table is not created if it already exists.
   * @return an object to communicate with the created table if it is created.
   * @throws Exception throws exception on creation failure.
   */
  public YBTable createRedisTableOnly(String name, boolean ifNotExist) throws Exception {
    if (ifNotExist) {
      ListTablesResponse response = getTablesList(name);
      if (response.getTablesList().stream().anyMatch(n -> n.equals(name))) {
        return openTable(REDIS_KEYSPACE_NAME, name);
      }
    }
    return createTable(REDIS_KEYSPACE_NAME, name, getRedisSchema(),
        new CreateTableOptions().setTableType(TableType.REDIS_TABLE_TYPE));
  }

  /**
   * Create a table on the cluster with the specified name and schema. Default table
   * configurations are used, mainly the table will have one tablet.
   * @param keyspace CQL keyspace to which this table belongs
   * @param name Table's name
   * @param schema Table's schema
   * @return an object to communicate with the created table
   */
  public YBTable createTable(String keyspace, String name, Schema schema) throws Exception {
    return createTable(keyspace, name, schema, new CreateTableOptions());
  }

  /**
   * Create a table on the cluster with the specified name, schema, and table configurations.
   * @param keyspace CQL keyspace to which this table belongs
   * @param name the table's name
   * @param schema the table's schema
   * @param builder a builder containing the table's configurations
   * @return an object to communicate with the created table
   */
  public YBTable createTable(String keyspace, String name, Schema schema,
                             CreateTableOptions builder)
      throws Exception {
    Deferred<YBTable> d = asyncClient.createTable(keyspace, name, schema, builder);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /*
   * Create a CQL keyspace.
   * @param non-null name of the keyspace.
   */
  public CreateKeyspaceResponse createKeyspace(String keyspace)
      throws Exception {
    Deferred<CreateKeyspaceResponse> d = asyncClient.createKeyspace(keyspace);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /*
   * Create a keyspace (namespace) for the specified database type.
   * @param non-null name of the keyspace.
   */
  public CreateKeyspaceResponse createKeyspace(String keyspace, YQLDatabase databaseType)
      throws Exception {
    Deferred<CreateKeyspaceResponse> d = asyncClient.createKeyspace(keyspace, databaseType);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Delete a keyspace(or namespace) on the cluster with the specified name.
   * @param keyspaceName CQL keyspace to delete
   * @return an rpc response object
   */
  public DeleteNamespaceResponse deleteNamespace(String keyspaceName)
      throws Exception {
    Deferred<DeleteNamespaceResponse> d = asyncClient.deleteNamespace(keyspaceName);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Delete a table on the cluster with the specified name.
   * @param keyspace CQL keyspace to which this table belongs
   * @param name the table's name
   * @return an rpc response object
   */
  public DeleteTableResponse deleteTable(final String keyspace, final String name)
      throws Exception {
    Deferred<DeleteTableResponse> d = asyncClient.deleteTable(keyspace, name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Alter a table on the cluster as specified by the builder.
   *
   * When the method returns it only indicates that the master accepted the alter
   * command, use {@link YBClient#isAlterTableDone(String)} to know when the alter finishes.
   * @param keyspace CQL keyspace to which this table belongs
   * @param name the table's name, if this is a table rename then the old table name must be passed
   * @param ato the alter table builder
   * @return an rpc response object
   */
  public AlterTableResponse alterTable(String keyspace, String name, AlterTableOptions ato)
      throws Exception {
    Deferred<AlterTableResponse> d = asyncClient.alterTable(keyspace, name, ato);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get information about master heartbeat delays as seen by the master leader.
   *
   * @return rpc response object containing the master heartbeat delays and rpc error if any
   * @throws Exception when the rpc fails
   */
  public GetMasterHeartbeatDelaysResponse getMasterHeartbeatDelays() throws Exception {
    Deferred<GetMasterHeartbeatDelaysResponse> d = asyncClient.getMasterHeartbeatDelays();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Helper method that checks and waits until the completion of an alter command.
   * It will block until the alter command is done or the timeout is reached.
   * @param keyspace CQL keyspace to which this table belongs
   * @param name Table's name, if the table was renamed then that name must be checked against
   * @return a boolean indicating if the table is done being altered
   */
  public boolean isAlterTableDone(String keyspace, String name) throws Exception {
    long totalSleepTime = 0;
    while (totalSleepTime < getDefaultAdminOperationTimeoutMs()) {
      long start = System.currentTimeMillis();

      Deferred<IsAlterTableDoneResponse> d = asyncClient.isAlterTableDone(keyspace, name);
      IsAlterTableDoneResponse response;
      try {
        response = d.join(AsyncYBClient.sleepTime);
      } catch (Exception ex) {
        throw ex;
      }

      if (response.isDone()) {
        return true;
      }

      // Count time that was slept and see if we need to wait a little more.
      long elapsed = System.currentTimeMillis() - start;
      // Don't oversleep the deadline.
      if (totalSleepTime + AsyncYBClient.sleepTime > getDefaultAdminOperationTimeoutMs()) {
        return false;
      }
      // elapsed can be bigger if we slept about 500ms
      if (elapsed <= AsyncYBClient.sleepTime) {
        LOG.debug("Alter not done, sleep " + (AsyncYBClient.sleepTime - elapsed) +
            " and slept " + totalSleepTime);
        Thread.sleep(AsyncYBClient.sleepTime - elapsed);
        totalSleepTime += AsyncYBClient.sleepTime;
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

  public ListLiveTabletServersResponse listLiveTabletServers() throws Exception {
    Deferred<ListLiveTabletServersResponse> d = asyncClient.listLiveTabletServers();
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
      CatalogEntityInfo.SysClusterConfigEntryPB config) throws Exception {
    Deferred<ChangeMasterClusterConfigResponse> d = asyncClient.changeMasterClusterConfig(config);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Change the load balancer state.
   * @param isEnable if true, load balancer is enabled on master, else is disabled.
   * @return the response of the operation.
   */
  public ChangeLoadBalancerStateResponse changeLoadBalancerState(
      boolean isEnable) throws Exception {
    Deferred<ChangeLoadBalancerStateResponse> d = asyncClient.changeLoadBalancerState(isEnable);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the tablet load move completion percentage for blacklisted nodes, if any.
   * @return the response with percent load completed.
   */
  public GetLoadMovePercentResponse getLoadMoveCompletion()
      throws Exception {
    Deferred<GetLoadMovePercentResponse> d;
    GetLoadMovePercentResponse resp;
    int numTries = 0;
    do {
      d = asyncClient.getLoadMoveCompletion();
      resp = d.join(getDefaultAdminOperationTimeoutMs());
    } while (resp.hasRetriableError() && numTries++ < MAX_NUM_RETRIES);
    return resp;
  }

  public AreNodesSafeToTakeDownResponse areNodesSafeToTakeDown(Set<String> masterIps,
                                                               Set<String> tserverIps,
                                                               long followerLagBoundMs)
      throws Exception {
    ListTabletServersResponse tabletServers = listTabletServers();
    Collection<String> tserverUUIDs = tabletServers.getTabletServersList().stream()
        .filter(ts -> tserverIps.contains(ts.getHost()))
        .map(ts -> ts.getUuid())
        .collect(Collectors.toSet());

    ListMastersResponse masters = listMasters();
    Collection<String> masterUUIDs = masters.getMasters().stream()
        .filter(m -> masterIps.contains(m.getHost()))
        .map(m -> m.getUuid())
        .collect(Collectors.toSet());

    Deferred<AreNodesSafeToTakeDownResponse> d = asyncClient.areNodesSafeToTakeDown(
        masterUUIDs, tserverUUIDs, followerLagBoundMs
    );
    return d.join(getDefaultAdminOperationTimeoutMs());
  }


  /**
   * Get the tablet load move completion percentage for blacklisted nodes, if any.
   * @return the response with percent load completed.
   */
  public GetLoadMovePercentResponse getLeaderBlacklistCompletion()
      throws Exception {
    Deferred<GetLoadMovePercentResponse> d;
    GetLoadMovePercentResponse resp;
    int numTries = 0;
    do {
      d = asyncClient.getLeaderBlacklistCompletion();
      resp = d.join(getDefaultAdminOperationTimeoutMs());
    } while (resp.hasRetriableError() && numTries++ < MAX_NUM_RETRIES);
    return resp;
  }

  /**
   * Check if the tablet load is balanced as per the master leader.
   * @param numServers expected number of servers across which the load needs to balanced.
   *                   Zero implies load distribution can be checked across all servers
   *                   which the master leader knows about.
   * @return a deferred object that yields if the load is balanced.
   */
  public IsLoadBalancedResponse getIsLoadBalanced(int numServers) throws Exception {
    Deferred<IsLoadBalancedResponse> d = asyncClient.getIsLoadBalanced(numServers);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Check if the load balancer is idle as per the master leader.
   * @return a deferred object that yields if the load is balanced.
   */
  public IsLoadBalancerIdleResponse getIsLoadBalancerIdle() throws Exception {
    Deferred<IsLoadBalancerIdleResponse> d = asyncClient.getIsLoadBalancerIdle();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Check if the tablet leader load is balanced as per the master leader.
   * @return a deferred object that yields if the load is balanced.
   */
  public AreLeadersOnPreferredOnlyResponse getAreLeadersOnPreferredOnly() throws Exception {
    Deferred<AreLeadersOnPreferredOnlyResponse> d = asyncClient.getAreLeadersOnPreferredOnly();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Check if initdb executed by the master is done running.
   */
  public IsInitDbDoneResponse getIsInitDbDone() throws Exception {
    Deferred<IsInitDbDoneResponse> d = asyncClient.getIsInitDbDone();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Wait for the master server to be running and initialized.
   * @param hp the host and port for the master.
   * @param timeoutMS timeout in milliseconds to wait for.
   * @return returns true if the master is properly initialized, false otherwise.
   */
  public boolean waitForMaster(HostAndPort hp, long timeoutMS) throws Exception {
    if (!waitForServer(hp, timeoutMS)) {
      return false;
    }

    long start = System.currentTimeMillis();
    while (System.currentTimeMillis() - start < timeoutMS &&
      getMasterUUID(hp.getHost(), hp.getPort()) == null) {
      Thread.sleep(AsyncYBClient.sleepTime);
    }
    return getMasterUUID(hp.getHost(), hp.getPort()) != null;
  }

  /**
   * Find the uuid of a master using the given host/port.
   * @param host Master host that is being queried.
   * @param port RPC port of the host being queried.
   * @return The uuid of the master, or null if not found.
   */
  String getMasterUUID(String host, int port) {
    HostAndPort hostAndPort = HostAndPort.fromParts(host, port);
    return getMasterRegistrationResponse(hostAndPort).map(
            resp -> resp.getInstanceId().getPermanentUuid().toStringUtf8()
    ).orElse(null);
  }

  /**
   * Find the uuid of the leader master.
   * @return The uuid of the leader master, or null if no leader found.
   */
  public String getLeaderMasterUUID() {
    for (HostAndPort hostAndPort : asyncClient.getMasterAddresses()) {
      Optional<GetMasterRegistrationResponse> resp = getMasterRegistrationResponse(hostAndPort);
      if (resp.isPresent() && resp.get().getRole() == CommonTypes.PeerRole.LEADER) {
        return resp.get().getInstanceId().getPermanentUuid().toStringUtf8();
      }
    }

    return null;
  }

  public List<GetMasterRegistrationResponse> getMasterRegistrationResponseList() {
    List<GetMasterRegistrationResponse> result = new ArrayList<>();
    for (HostAndPort hostAndPort : asyncClient.getMasterAddresses()) {
      Optional<GetMasterRegistrationResponse> resp = getMasterRegistrationResponse(hostAndPort);
      if (resp.isPresent()) {
        result.add(resp.get());
      }
    }
    return result;
  }

  private Optional<GetMasterRegistrationResponse> getMasterRegistrationResponse(
          HostAndPort hostAndPort) {
    Deferred<GetMasterRegistrationResponse> d;
    TabletClient clientForHostAndPort = asyncClient.newMasterClient(hostAndPort);
    if (clientForHostAndPort == null) {
      String message = "Couldn't resolve this master's address " + hostAndPort.toString();
      LOG.warn(message);
    } else {
      d = asyncClient.getMasterRegistration(clientForHostAndPort);
      try {
        return Optional.of(d.join(getDefaultAdminOperationTimeoutMs()));
      } catch (Exception e) {
        LOG.warn("Couldn't get registration info for master {} due to error '{}'.",
                hostAndPort.toString(), e.getMessage());
      }
    }
    return Optional.empty();
  }

  /**
   * Find the host/port of the leader master.
   * @return The host and port of the leader master, or null if no leader found.
   */
  public HostAndPort getLeaderMasterHostAndPort() {
    List<HostAndPort> masterAddresses = asyncClient.getMasterAddresses();
    Map<HostAndPort, TabletClient> clients = new HashMap<>();
    for (HostAndPort hostAndPort : masterAddresses) {
      TabletClient clientForHostAndPort = asyncClient.newMasterClient(hostAndPort);
      if (clientForHostAndPort == null) {
        String message = "Couldn't resolve this master's host/port " + hostAndPort.toString();
        LOG.warn(message);
      }
      clients.put(hostAndPort, clientForHostAndPort);
    }

    CountDownLatch finished = new CountDownLatch(1);
    AtomicInteger workersLeft = new AtomicInteger(clients.entrySet().size());

    AtomicReference<HostAndPort> result = new AtomicReference<>(null);
    for (Entry<HostAndPort, TabletClient> entry : clients.entrySet()) {
      asyncClient.getMasterRegistration(entry.getValue())
          .addCallback(new Callback<Object, GetMasterRegistrationResponse>() {
            @Override
            public Object call(GetMasterRegistrationResponse response) throws Exception {
              if (response.getRole() == CommonTypes.PeerRole.LEADER) {
                boolean wasNullResult = result.compareAndSet(null, entry.getKey());
                if (!wasNullResult) {
                  LOG.warn(
                      "More than one node reported they are master-leaders ({} and {})",
                      result.get().toString(), entry.getKey().toString());
                }
                finished.countDown();
              } else if (workersLeft.decrementAndGet() == 0) {
                finished.countDown();
              }
              return null;
            }
          }).addErrback(new Callback<Exception, Exception>() {
            @Override
            public Exception call(final Exception e) {
              // If finished == 0 then we are here because of closeClient() and the master is
              // already found.
              if (finished.getCount() != 0) {
                LOG.warn("Couldn't get registration info for master " + entry.getKey().toString());
                if (workersLeft.decrementAndGet() == 0) {
                  finished.countDown();
                }
              }
              return null;
            }
          });
    }

    try {
      finished.await(getDefaultAdminOperationTimeoutMs(), TimeUnit.MILLISECONDS);
    } catch (InterruptedException e) {
    }
    return result.get();
  }

  /**
   * Helper API to wait and get current leader's UUID. This takes care of waiting for any election
   * in progress.
   *
   * @param timeoutMs Amount of time to try getting the leader uuid.
   * @return Master leader uuid on success, null otherwise.
   */
  private String waitAndGetLeaderMasterUUID(long timeoutMs) throws Exception {
    LOG.info("Waiting for master leader (timeout: " + timeoutMs + " ms)");
    long start = System.currentTimeMillis();
    // Retry till we get a valid UUID (or timeout) for the new leader.
    do {
      String leaderUuid = getLeaderMasterUUID();

      long elapsedTimeMs = System.currentTimeMillis() - start;
      // Done if we got a valid one.
      if (leaderUuid != null) {
        LOG.info("Fininshed waiting for master leader in " + elapsedTimeMs + " ms. Leader UUID: " +
                 leaderUuid);
        return leaderUuid;
      }

      Thread.sleep(asyncClient.sleepTime);
    } while (System.currentTimeMillis() - start < timeoutMs);

    LOG.error("Timed out getting leader uuid.");

    return null;
  }

 /**
   * Step down the current master leader and wait for a new leader to be elected.
   *
   * @param  leaderUuid Current master leader's uuid.
   * @exception if the new leader is the same as the old leader after certain number of tries.
   */
  private void stepDownMasterLeaderAndWaitForNewLeader(String leaderUuid) throws Exception {
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

        LOG.info("Tried step down of {}, new leader {}, iter {}.",
                 leaderUuid, newLeader, numIters);

        // Done if we found a new leader.
        if (!newLeader.equals(leaderUuid)) {
          break;
        }

        numIters++;
        if (numIters >= maxNumIters) {
          errorMsg = "Maximum iterations reached trying to step down the " +
                     "leader master with uuid " + leaderUuid;
          break;
        }

        Thread.sleep(asyncClient.sleepTime);
      } while (true);
    } catch (Exception e) {
     // TODO: Ideally we need an error code here, but this is come another layer which
     // eats up the NOT_THE_LEADER code.
     if (e.getMessage().contains("Wrong destination UUID requested")) {
        LOG.info("Got wrong destination for {} stepdown with error '{}'.",
                 leaderUuid, e.getMessage());
        newLeader = waitAndGetLeaderMasterUUID(getDefaultAdminOperationTimeoutMs());
        LOG.info("After wait for leader uuid. Old leader was {}, new leader is {}.",
                 leaderUuid, newLeader);
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
  }

  /**
   * Change master configuration.
   *
   * @param host Master host that is being added or removed.
   * @param port RPC port of the host being added or removed.
   * @param isAdd true if we are adding a server to the master configuration, false if removing.
   * @param useHost if caller wants to use host/port instead of uuid of server being removed.
   * @param hostAddrToAdd if caller wants to use a different address for the master
   *                      for the config update
   *
   * @return The change config response object.
   */
  public ChangeConfigResponse changeMasterConfig(
      String host, int port, boolean isAdd) throws Exception {
    return changeMasterConfig(host, port, isAdd, false /* useHost */);
  }

  public ChangeConfigResponse changeMasterConfig(
      String host, int port, boolean isAdd, boolean useHost) throws Exception {
    return changeMasterConfig(host, port, isAdd, useHost, null /* hostAddrToAdd */);
  }

  public ChangeConfigResponse changeMasterConfig(String host, int port,
                                                 boolean isAdd, boolean useHost,
                                                 String hostAddrToAdd) throws Exception {
    String masterUuid = null;
    int tries = 0;

    if (isAdd || !useHost) {
      // In case the master UUID is returned as null, retry a few times
      do {
          masterUuid = getMasterUUID(host, port);
          Thread.sleep(AsyncYBClient.sleepTime);
          tries++;
      } while (tries < MAX_NUM_RETRIES && masterUuid == null);

      if (masterUuid == null) {
        throw new IllegalArgumentException("Invalid master host/port of " + host + "/" +
                                            port + " - could not get it's uuid.");
      }
    }

    LOG.info("Sending changeConfig : Target host:port={}:{} at uuid={}, add={}, useHost={}.",
             host, port, masterUuid, isAdd, useHost);
    long timeout = getDefaultAdminOperationTimeoutMs();
    ChangeConfigResponse resp = null;
    boolean changeConfigDone;

    // It is possible that we might need different addresses for reaching from the client
    // than the one the DB nodes use to communicate. If the client provides an address
    // to add to the config explicitly, use that.
    String hostAddr = hostAddrToAdd == null ? host : hostAddrToAdd;
    do {
      changeConfigDone = true;
      try {
        Deferred<ChangeConfigResponse> d =
            asyncClient.changeMasterConfig(hostAddr, port, masterUuid, isAdd, useHost);
        resp = d.join(timeout);
        if (!resp.hasError()) {
          asyncClient.updateMasterAdresses(host, port, isAdd);
        }
      } catch (TabletServerErrorException tsee) {
        String leaderUuid = waitAndGetLeaderMasterUUID(timeout);
        LOG.info("Hit tserver error {}, leader is {}.",
                 tsee.getTServerError().toString(), leaderUuid);
        if (tsee.getTServerError().getCode() ==
            TserverTypes.TabletServerErrorPB.Code.LEADER_NEEDS_STEP_DOWN) {
          stepDownMasterLeaderAndWaitForNewLeader(leaderUuid);
          changeConfigDone = false;
          LOG.info("Retrying changeConfig because it received LEADER_NEEDS_STEP_DOWN error code.");
        } else {
          throw tsee;
        }
      }
    } while (!changeConfigDone);

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
      throw new RuntimeException(
          "Timed out waiting for Master Leader after " + timeoutMs + " ms");
    }
  }

  /**
   * Enable encryption at rest using the key file specified
   */
  public boolean enableEncryptionAtRestInMemory(final String versionId) throws Exception {
    Deferred<ChangeEncryptionInfoInMemoryResponse> d;
    d = asyncClient.enableEncryptionAtRestInMemory(versionId);
    d.join(getDefaultAdminOperationTimeoutMs());
    return d.join(getDefaultAdminOperationTimeoutMs()).hasError();
  }

  /**
   * Disable encryption at rest
   */
  public boolean disableEncryptionAtRestInMemory() throws Exception {
    Deferred<ChangeEncryptionInfoInMemoryResponse> d;
    d = asyncClient.disableEncryptionAtRestInMemory();
    return !d.join(getDefaultAdminOperationTimeoutMs()).hasError();
  }

  /**
  * Enable encryption at rest using the key file specified
  */
  public boolean enableEncryptionAtRest(final String file) throws Exception {
    Deferred<ChangeEncryptionInfoResponse> d;
    d = asyncClient.enableEncryptionAtRest(file);
    return !d.join(getDefaultAdminOperationTimeoutMs()).hasError();
  }

  /**
   * Disable encryption at rest
   */
  public boolean disableEncryptionAtRest() throws Exception {
    Deferred<ChangeEncryptionInfoResponse> d;
    d = asyncClient.disableEncryptionAtRest();
    return !d.join(getDefaultAdminOperationTimeoutMs()).hasError();
  }

  public Pair<Boolean, String> isEncryptionEnabled() throws Exception {
    Deferred<IsEncryptionEnabledResponse> d = asyncClient.isEncryptionEnabled();
    IsEncryptionEnabledResponse resp = d.join(getDefaultAdminOperationTimeoutMs());
    if (resp.getServerError() != null) {
      throw new RuntimeException("Could not check isEnabledEncryption with error: " +
                                 resp.getServerError().getStatus().getMessage());
    }
    return new Pair(resp.getIsEnabled(), resp.getUniverseKeyId());
  }

  /**
  * Add universe keys in memory to a master at hp, with universe keys in format <id -> key>
  */
  public void addUniverseKeys(Map<String, byte[]> universeKeys, HostAndPort hp) throws Exception {
    Deferred<AddUniverseKeysResponse> d = asyncClient.addUniverseKeys(universeKeys, hp);
    AddUniverseKeysResponse resp = d.join();
    if (resp.getServerError() != null) {
      throw new RuntimeException("Could not add universe keys to " + hp.toString() +
                                 " with error: " + resp.getServerError().getStatus().getMessage());
    }
  }

  /**
   * Check if a master at hp has universe key universeKeyId. Returns true if it does.
   */
  public boolean hasUniverseKeyInMemory(String universeKeyId, HostAndPort hp) throws Exception {
    Deferred<HasUniverseKeyInMemoryResponse> d =
            asyncClient.hasUniverseKeyInMemory(universeKeyId, hp);
    HasUniverseKeyInMemoryResponse resp = d.join();
    if (resp.getServerError() != null) {
      throw new RuntimeException("Could not add universe keys to " + hp.toString() +
                                 " with error: " + resp.getServerError().getStatus().getMessage());
    }
    return resp.hasKey();
  }

  /**
  * Ping a certain server to see if it is responding to RPC requests.
  * @param host the hostname or IP of the server
  * @param port the port number of the server
  * @return true if the server responded to ping
  */
  public boolean ping(final String host, int port) throws Exception {
    Deferred<PingResponse> d = asyncClient.ping(HostAndPort.fromParts(host, port));
    d.join(getDefaultAdminOperationTimeoutMs());
    return true;
  }

  /**
   * Set a gflag of a given server.
   * @param hp the host and port of the server
   * @param flag the flag to be set.
   * @param value the value to set the flag to
   * @return true if the server successfully set the flag
   */
  public boolean setFlag(HostAndPort hp, String flag, String value) throws Exception {
    return setFlag(hp, flag, value, false);
  }

  /**
   * Set a gflag of a given server.
   * @param hp the host and port of the server
   * @param flag the flag to be set.
   * @param value the value to set the flag to
   * @param force if the flag needs to be set even if it is not marked runtime safe
   * @return true if the server successfully set the flag
   */
  public boolean setFlag(HostAndPort hp, String flag, String value,
                         boolean force) throws Exception {
    if (flag == null || flag.isEmpty() || value == null || value.isEmpty() || hp == null) {
      LOG.warn("Invalid arguments for hp: {}, flag {}, or value: {}", hp.toString(), flag, value);
      return false;
    }
    Deferred<SetFlagResponse> d = asyncClient.setFlag(hp, flag, value, force);
    return !d.join(getDefaultAdminOperationTimeoutMs()).hasError();
  }

  /**
   * Get a gflag's value from a given server.
   * @param hp the host and port of the server
   * @param flag the flag to get.
   * @return string value of flag if valid, else empty string
   */
  public String getFlag(HostAndPort hp, String flag) throws Exception {
    if (flag == null || hp == null) {
      LOG.warn("Invalid arguments for hp: {}, flag {}", hp.toString(), flag);
      return "";
    }
    Deferred<GetFlagResponse> d = asyncClient.getFlag(hp, flag);
    GetFlagResponse result = d.join(getDefaultAdminOperationTimeoutMs());
    if (result.getValid()) {
      LOG.warn("Invalid flag {}", flag);
      return result.getValue();
    }
    return "";
  }

  /**
   *  Get the list of master addresses from a given tserver.
   * @param hp the host and port of the server
   * @return a comma separated string containing the list of master addresses
   */
  public String getMasterAddresses(HostAndPort hp) throws Exception {
    Deferred<GetMasterAddressesResponse> d = asyncClient.getMasterAddresses(hp);
    return d.join(getDefaultAdminOperationTimeoutMs()).getMasterAddresses();
  }

  /**
   * @see AsyncYBClient#upgradeYsql(HostAndPort, boolean)
   */
  public UpgradeYsqlResponse upgradeYsql(HostAndPort hp, boolean useSingleConnection)
    throws Exception {
    Deferred<UpgradeYsqlResponse> d = asyncClient.upgradeYsql(hp, useSingleConnection);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Check if the server is ready to serve requests.
   * @param hp the host and port of the server.
   * @param isTserver true if host/port is for tserver, else its master.
   * @return server readiness response.
   */
  public IsServerReadyResponse isServerReady(HostAndPort hp, boolean isTserver)
     throws Exception {
    Deferred<IsServerReadyResponse> d = asyncClient.isServerReady(hp, isTserver);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Gets the list of tablets for a TServer.
   * @param hp host and port of the TServer.
   * @return response containing the list of tablet ids that exist on a TServer.
   */
  public ListTabletsForTabletServerResponse listTabletsForTabletServer(HostAndPort hp)
      throws Exception {
    Deferred<ListTabletsForTabletServerResponse> d = asyncClient.listTabletsForTabletServer(hp);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   *
   * @param servers string array of node addresses (master and/or tservers) in the
   *                format host:port
   * @return 'true' when all certificates are reloaded
   * @throws RuntimeException      when the operation fails
   * @throws IllegalStateException when the input 'servers' array is empty or null
   */
  public boolean reloadCertificates(HostAndPort server)
      throws RuntimeException, IllegalStateException {
    if (server == null || server.getHost() == null || "".equals(server.getHost().trim()))
      throw new IllegalStateException("No servers to act upon");

    LOG.debug("attempting to reload certificates for {}", (Object) server);

    Deferred<ReloadCertificateResponse> deferred = asyncClient.reloadCertificates(server);
    try {
      ReloadCertificateResponse response = deferred.join(getDefaultAdminOperationTimeoutMs());
      LOG.debug("received certificate reload response from {}", response.getNodeAddress());
      return true;

    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public interface Condition {
    boolean get() throws Exception;
  }

  private class ReplicaCountCondition implements Condition {
    private int numReplicas;
    private YBTable table;
    public ReplicaCountCondition(YBTable table, int numReplicas) {
      this.numReplicas = numReplicas;
      this.table = table;
    }
    @Override
    public boolean get() throws Exception {
      for (LocatedTablet tablet : table.getTabletsLocations(getDefaultAdminOperationTimeoutMs())) {
        if (tablet.getReplicas().size() != numReplicas) {
          return false;
        }
      }
      return true;
    }
  }

  private class TableDoesNotExistCondition implements Condition {
    private String nameFilter;
    public TableDoesNotExistCondition(String nameFilter) {
      this.nameFilter = nameFilter;
    }
    @Override
    public boolean get() throws Exception {
      ListTablesResponse tl = getTablesList(nameFilter);
      return tl.getTablesList().isEmpty();
    }
  }

  /**
   * Checks the ping of the given ip and port.
   */
  private class ServerCondition implements Condition {
    private HostAndPort hp;
    public ServerCondition(HostAndPort hp) {
      this.hp = hp;
    }
    @Override
    public boolean get() throws Exception {
      return ping(hp.getHost(), hp.getPort());
    }
  }

  /**
   * Checks whether the IsLoadBalancedResponse has no error.
   */
  private class LoadBalanceCondition implements Condition {
    private int numServers;
    public LoadBalanceCondition(int numServers) {
      this.numServers = numServers;
    }
    @Override
    public boolean get() throws Exception {
      IsLoadBalancedResponse resp = getIsLoadBalanced(numServers);
      return !resp.hasError();
    }
  }

  /**
   * Checks whether the IsLoadBalancerIdleResponse has no error.
   */
  private class LoadBalancerIdleCondition implements Condition {
    public LoadBalancerIdleCondition() {
    }
    @Override
    public boolean get() throws Exception {
      IsLoadBalancerIdleResponse resp = getIsLoadBalancerIdle();
      return !resp.hasError();
    }
  }

  /**
   * Checks whether the LoadBalancer is currently running.
   */
  private class LoadBalancerActiveCondition implements Condition {
    public LoadBalancerActiveCondition() {
    }
    @Override
    public boolean get() throws Exception {
      try {
        IsLoadBalancerIdleResponse resp = getIsLoadBalancerIdle();
      } catch (MasterErrorException e) {
        // TODO (deepthi.srinivasan) Instead of writing if-else
        // with Exceptions, find a way to receive the error code
        // neatly.
        return e.toString().contains("LOAD_BALANCER_RECENTLY_ACTIVE");
      }
      return false;
    }
  }


  private class AreLeadersOnPreferredOnlyCondition implements Condition {
    @Override
    public boolean get() throws Exception {
      AreLeadersOnPreferredOnlyResponse resp = getAreLeadersOnPreferredOnly();
      return !resp.hasError();
    }
  }

  private class ReplicaMapCondition implements Condition {
    private YBTable table;
    Map<String, List<List<Integer>>> replicaMapExpected;
    private long deadline;
    public ReplicaMapCondition(YBTable table,
                               Map<String, List<List<Integer>>> replicaMapExpected,
                               long deadline) {
      this.table = table;
      this.replicaMapExpected = replicaMapExpected;
      this.deadline = deadline;
    }
    @Override
    public boolean get() throws Exception {
      Map<String, List<List<Integer>>> replicaMap =
          table.getMemberTypeCountsForEachTSType(deadline);
      return replicaMap.equals(replicaMapExpected);
    }
  }

  private class MasterHasUniverseKeyInMemoryCondition implements Condition {
    private String universeKeyId;
    private HostAndPort hp;
    public MasterHasUniverseKeyInMemoryCondition(String universeKeyId, HostAndPort hp) {
      this.universeKeyId = universeKeyId;
      this.hp = hp;
    }

    @Override
    public boolean get() throws Exception {
      return hasUniverseKeyInMemory(universeKeyId, hp);
    }
  }

  /**
   * Quick and dirty error injection on Wait based API's.
   * After every use, for now, will get automatically disabled.
   */
  public void injectWaitError() {
    injectWaitError = true;
  }

  /**
   * Helper method that loops on a condition every 500ms until it returns true or the
   * operation times out.
   * @param condition the Condition which implements a boolean get() method.
   * @param timeoutMs the amount of time, in MS, to wait.
   * @return true if the condition is true within the time frame, false otherwise.
   */
  private boolean waitForCondition(Condition condition, final long timeoutMs) {
    int numErrors = 0;
    int numIters = 0;
    Exception exception = null;
    long start = System.currentTimeMillis();
    do {
      exception = null;
      try {
        if (injectWaitError) {
          Thread.sleep(AsyncYBClient.sleepTime);
          injectWaitError = false;
          String msg = "Simulated expection due to injected error.";
          LOG.info(msg);
          throw new RuntimeException(msg);
        }
        if (condition.get()) {
          return true;
        }
      } catch (Exception e) {
        numErrors++;
        if (numErrors % LOG_ERRORS_EVERY_NUM_ITERS == 0) {
          LOG.warn("Hit {} errors so far. Latest is : {}.", numErrors, e.getMessage());
        }
        exception = e;
      }
      numIters++;
      if (numIters % LOG_EVERY_NUM_ITERS == 0) {
        LOG.info("Tried operation {} times so far.", numIters);
      }
      // Sleep before next retry.
      try {
        Thread.sleep(AsyncYBClient.sleepTime);
      } catch (Exception e) {}
    } while(System.currentTimeMillis() - start < timeoutMs);
    if (exception == null) {
      LOG.error("Timed out waiting for condition");
    } else {
      LOG.error("Hit too many errors, final exception is {}.", exception.getMessage());
    }
    LOG.error("Returning failure after {} iterations, num errors = {}.", numIters, numErrors);
    return false;
  }

  /**
  * Wait for the table to have a specific number of replicas.
  * @param table the table to check the condition on
  * @param numReplicas the number of replicas we expect the table to have
  * @param timeoutMs the amount of time, in MS, to wait
  * @return true if the table the expected number of replicas, false otherwise
  */
  public boolean waitForReplicaCount(final YBTable table, final int numReplicas,
                                     final long timeoutMs) {
    Condition replicaCountCondition = new ReplicaCountCondition(table, numReplicas);
    return waitForCondition(replicaCountCondition, timeoutMs);
  }

  /**
  * Wait for the specific server to come online.
  * @param hp the HostAndPort of the server
  * @param timeoutMs the amount of time, in MS, to wait
  * @return true if the server responded to pings in the given time, false otherwise
  */
  public boolean waitForServer(final HostAndPort hp, final long timeoutMs) {
    Condition serverCondition = new ServerCondition(hp);
    return waitForCondition(serverCondition, timeoutMs);
  }

  /**
  * Wait for the tablet load to be balanced by master leader.
  * @param timeoutMs the amount of time, in MS, to wait
  * @param numServers expected number of servers which need to balanced.
  * @return true if the master leader does not return any error balance check.
  */
  public boolean waitForLoadBalance(final long timeoutMs, int numServers) {
    Condition loadBalanceCondition = new LoadBalanceCondition(numServers);
    return waitForCondition(loadBalanceCondition, timeoutMs);
  }

  /**
  * Wait for the Load Balancer to become active.
  * @param timeoutMs the amount of time, in MS, to wait
  * @return true if the load balancer is currently running.
  */
  public boolean waitForLoadBalancerActive(final long timeoutMs) {
    Condition loadBalancerActiveCondition = new LoadBalancerActiveCondition();
    return waitForCondition(loadBalancerActiveCondition, timeoutMs);
  }

  /**
  * Wait for the tablet load to be balanced by master leader.
  * @param timeoutMs the amount of time, in MS, to wait
  * @return true if the master leader does not return any error balance check.
  */
  public boolean waitForLoadBalancerIdle(final long timeoutMs) {
    Condition loadBalancerIdleCondition = new LoadBalancerIdleCondition();
    return waitForCondition(loadBalancerIdleCondition, timeoutMs);
  }

  /**
   * Wait for the leader load to be balanced by master leader.
   * @param timeoutMs the amount of time, in MS, to wait.
   * @return true iff the leader count is balanced within timeoutMs.
   */
  public boolean waitForAreLeadersOnPreferredOnlyCondition(final long timeoutMs) {
    Condition areLeadersOnPreferredOnlyCondition =
        new AreLeadersOnPreferredOnlyCondition();
    return waitForCondition(areLeadersOnPreferredOnlyCondition, timeoutMs);
  }

  /**
   * Check if the replica count per ts matches the expected, returns true if it does within
   * timeoutMs, false otherwise.
   * TODO(Rahul): follow similar style for this type of function will do with affinitized
   * leaders tests.
   * @param timeoutMs number of milliseconds before timing out.
   * @param table the table to wait for load balancing.
   * @param replicaMapExpected the expected map between cluster uuid and live, read replica count.
   * @return true if the read only replica count for the table matches the expected within the
   * expected time frame, false otherwise.
   */
  public boolean waitForExpectedReplicaMap(final long timeoutMs, YBTable table,
                                            Map<String, List<List<Integer>>> replicaMapExpected) {
    Condition replicaMapCondition = new ReplicaMapCondition(table, replicaMapExpected, timeoutMs);
    return waitForCondition(replicaMapCondition, timeoutMs);
  }

  public boolean waitForMasterHasUniverseKeyInMemory(
          final long timeoutMs, String universeKeyId, HostAndPort hp) {
    Condition universeKeyCondition = new MasterHasUniverseKeyInMemoryCondition(universeKeyId, hp);
    return waitForCondition(universeKeyCondition, timeoutMs);
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
   * Get the list of all YSQL, YCQL, and YEDIS non-system tables.
   * @return a list of all the non-system tables
   */
  public ListTablesResponse getTablesList() throws Exception {
    // YEDIS tables are stored as system tables, so they have to be separated.
    ListTablesResponse nonSystemTables = getTablesList(null, true, null);
    ListTablesResponse yedisTables;
    // If YEDIS is not enabled, getTablesList will error out on this call.
    try {
      yedisTables  = getTablesList(null, false, REDIS_KEYSPACE_NAME);
    } catch (MasterErrorException e) {
      yedisTables = null;
    }
    return nonSystemTables.mergeWith(yedisTables);
  }

  /**
   * Get a list of table names. Passing a null filter returns all the tables. When a filter is
   * specified, it only returns tables that satisfy a substring match.
   * @param nameFilter an optional table name filter
   * @return a deferred that contains the list of table names
   */
  public ListTablesResponse getTablesList(String nameFilter) throws Exception {
    return getTablesList(nameFilter, false, null);
  }

  /**
   * Get a list of table names. Passing a null filter returns all the tables. When a filter is
   * specified, it only returns tables that satisfy a substring match. Passing excludeSysfilters
   * will return only non-system tables (index and user tables).
   * @param nameFilter an optional table name filter
   * @param excludeSystemTables an optional filter to search only non-system tables
   * @param namespace an optional filter to search tables in specific namespace
   * @return a deferred that contains the list of table names
   */
  public ListTablesResponse getTablesList(
      String nameFilter, boolean excludeSystemTables, String namespace)
  throws Exception {
    Deferred<ListTablesResponse> d = asyncClient.getTablesList(
        nameFilter, excludeSystemTables, namespace);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of all YSQL, YCQL, and YEDIS namespaces.
   * @return a list of all the namespaces
   */
  public ListNamespacesResponse getNamespacesList() throws Exception {

    // Fetch the namespaces of YSQL
    ListNamespacesResponse namespacesList = null;
    try {
      Deferred<ListNamespacesResponse> d =
          asyncClient.getNamespacesList(YQLDatabase.YQL_DATABASE_PGSQL);
      namespacesList = d.join(getDefaultAdminOperationTimeoutMs());
    } catch (MasterErrorException e) {
    }

    // Fetch the namespaces of YCQL
    try {
      Deferred<ListNamespacesResponse> d =
          asyncClient.getNamespacesList(YQLDatabase.YQL_DATABASE_CQL);
      ListNamespacesResponse response = d.join(getDefaultAdminOperationTimeoutMs());
      namespacesList = namespacesList == null ? response : namespacesList.mergeWith(response);
    } catch (MasterErrorException e) {
    }

    // Fetch the namespaces of YEDIS
    try {
      Deferred<ListNamespacesResponse> d =
          asyncClient.getNamespacesList(YQLDatabase.YQL_DATABASE_REDIS);
      ListNamespacesResponse response = d.join(getDefaultAdminOperationTimeoutMs());
      namespacesList = namespacesList == null ? response : namespacesList.mergeWith(response);
    } catch (MasterErrorException e) {
    }

    return namespacesList;
  }

  /**
   * Create for a given tablet and stream.
   * @param hp host port of the server.
   * @param tableId the table id to subscribe to.
   * @return a deferred object for the response from server.
   */
  public CreateCDCStreamResponse createCDCStream(
          final HostAndPort hp, String tableId,
          String nameSpaceName, String format,
          String checkpointType) throws Exception{
    Deferred<CreateCDCStreamResponse> d = asyncClient.createCDCStream(hp,
      tableId,
      nameSpaceName,
      format,
      checkpointType);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public CreateCDCStreamResponse createCDCStream(YBTable table,
                                                  String nameSpaceName,
                                                  String format,
                                                  String checkpointType) throws Exception {
    return createCDCStream(table, nameSpaceName, format, checkpointType, "");
  }

  public CreateCDCStreamResponse createCDCStream(YBTable table,
                                                  String nameSpaceName,
                                                  String format,
                                                  String checkpointType,
                                                  String recordType) throws Exception {
    Deferred<CreateCDCStreamResponse> d = asyncClient.createCDCStream(table,
      nameSpaceName, format, checkpointType, recordType, null);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }
  public CreateCDCStreamResponse createCDCStream(YBTable table,
                                                  String nameSpaceName,
                                                  String format,
                                                  String checkpointType,
                                                  String recordType,
                                                  boolean dbtype,
                                                  boolean consistentSnapshot,
                                                  boolean useSnapshot) throws Exception {
    Deferred<CreateCDCStreamResponse> d;
    if (dbtype) {
      d = asyncClient.createCDCStream(table,
        nameSpaceName, format, checkpointType, recordType,
        CommonTypes.YQLDatabase.YQL_DATABASE_CQL,
        consistentSnapshot
            ? (useSnapshot ? CommonTypes.CDCSDKSnapshotOption.USE_SNAPSHOT
                : CommonTypes.CDCSDKSnapshotOption.NOEXPORT_SNAPSHOT)
            : null);
    } else {
      d = asyncClient.createCDCStream(table,
        nameSpaceName, format, checkpointType, recordType,
        consistentSnapshot
            ? (useSnapshot ? CommonTypes.CDCSDKSnapshotOption.USE_SNAPSHOT
                : CommonTypes.CDCSDKSnapshotOption.NOEXPORT_SNAPSHOT)
            : null);
    }
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public boolean waitForTableRemoval(final long timeoutMs, String name) {
    Condition TableDoesNotExistCondition = new TableDoesNotExistCondition(name);
    return waitForCondition(TableDoesNotExistCondition, timeoutMs);
  }

  /**
   * Test if a table exists.
   * @param keyspace the keyspace name to which this table belongs.
   * @param name a non-null table name
   * @return true if the table exists, else false
   */
  public boolean tableExists(String keyspace, String name) throws Exception {
    Deferred<Boolean> d = asyncClient.tableExists(keyspace, name);
    try {
      return d.join(getDefaultAdminOperationTimeoutMs());
    } catch (MasterErrorException e) {
      return false;
    }
  }

  /**
   * Test if a table exists based on its UUID.
   * @param tableUUID a non-null table UUID
   * @return true if the table exists, else false
   */
  public boolean tableExistsByUUID(String tableUUID) throws Exception {
    Deferred<Boolean> d = asyncClient.tableExistsByUUID(tableUUID);
    try {
      return d.join(getDefaultAdminOperationTimeoutMs());
    } catch (MasterErrorException e) {
      return false;
    }
  }

  /**
   * Get the schema for a table based on the table's name.
   * @param keyspace the keyspace name to which this table belongs.
   * @param name a non-null table name
   * @return a deferred that contains the schema for the specified table
   */
  public GetTableSchemaResponse getTableSchema(final String keyspace, final String name)
      throws Exception {
    Deferred<GetTableSchemaResponse> d = asyncClient.getTableSchema(keyspace, name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the schema for a table based on the table's UUID.
   * @param tableUUID a non-null table uuid
   * @return a deferred that contains the schema for the specified table
   */
  public GetTableSchemaResponse getTableSchemaByUUID(final String tableUUID)
      throws Exception {
    Deferred<GetTableSchemaResponse> d = asyncClient.getTableSchemaByUUID(tableUUID);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Open the table with the given name. If the table was just created, this method will block until
   * all its tablets have also been created.
   * @param keyspace the keyspace name to which the table belongs.
   * @param name table to open
   * @return a YBTable if the table exists, else a MasterErrorException
   */
  public YBTable openTable(final String keyspace, final String name) throws Exception {
    Deferred<YBTable> d = asyncClient.openTable(keyspace, name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of tablet UUIDs of a table with the given name.
   * @param table table info.
   * @return the set of tablet UUIDs of the table.
   */
  public Set<String> getTabletUUIDs(final YBTable table) throws Exception {
    Set<String> ids = new HashSet<>();
    for (LocatedTablet tablet : table.getTabletsLocations(getDefaultAdminOperationTimeoutMs())) {
      ids.add(new String(tablet.getTabletId()));
    }
    return ids;
  }

  /**
   * Get the list of tablet UUIDs of a table with the given name.
   * @param keyspace the keyspace name to which the table belongs.
   * @param name the table name
   * @return the set of tablet UUIDs of the table.
   */
  public Set<String> getTabletUUIDs(final String keyspace, final String name) throws Exception {
    return getTabletUUIDs(openTable(keyspace, name));
  }

  /**
   * Open the table with the given UUID. If the table was just created, this method will block until
   * all its tablets have also been created.
   * @param tableUUID table to open
   * @return a YBTable if the table exists, else a MasterErrorException
   */
  public YBTable openTableByUUID(final String tableUUID) throws Exception {
    Deferred<YBTable> d = asyncClient.openTableByUUID(tableUUID);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * It is the same as {@link AsyncYBClient#setupUniverseReplication(String, Map, Set, Boolean)}
   * except that it is synchronous.
   *
   * @see AsyncYBClient#setupUniverseReplication(String, Map, Set, Boolean)
   */
  public SetupUniverseReplicationResponse setupUniverseReplication(
    String replicationGroupName,
    Map<String, String> sourceTableIdsBootstrapIdMap,
    Set<CommonNet.HostPortPB> sourceMasterAddresses,
    @Nullable Boolean isTransactional) throws Exception {
    Deferred<SetupUniverseReplicationResponse> d =
      asyncClient.setupUniverseReplication(
        replicationGroupName,
        sourceTableIdsBootstrapIdMap,
        sourceMasterAddresses,
        isTransactional);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public SetupUniverseReplicationResponse setupUniverseReplication(
    String replicationGroupName,
    Map<String, String> sourceTableIdsBootstrapIdMap,
    Set<CommonNet.HostPortPB> sourceMasterAddresses) throws Exception {
    return setupUniverseReplication(
      replicationGroupName,
      sourceTableIdsBootstrapIdMap,
      sourceMasterAddresses,
      null /* isTransactional */);
  }

  public IsSetupUniverseReplicationDoneResponse isSetupUniverseReplicationDone(
    String replicationGroupName) throws Exception {
    Deferred<IsSetupUniverseReplicationDoneResponse> d =
      asyncClient.isSetupUniverseReplicationDone(replicationGroupName);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public SetUniverseReplicationEnabledResponse setUniverseReplicationEnabled(
    String replicationGroupName, boolean active) throws Exception {
    Deferred<SetUniverseReplicationEnabledResponse> d =
      asyncClient.setUniverseReplicationEnabled(replicationGroupName, active);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public AlterUniverseReplicationResponse alterUniverseReplicationAddTables(
    String replicationGroupName,
    Map<String, String> sourceTableIdsToAddBootstrapIdMap) throws Exception {
    Deferred<AlterUniverseReplicationResponse> d =
      asyncClient.alterUniverseReplicationAddTables(
        replicationGroupName, sourceTableIdsToAddBootstrapIdMap);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public AlterUniverseReplicationResponse alterUniverseReplicationRemoveTables(
    String replicationGroupName,
    Set<String> sourceTableIdsToRemove) throws Exception {
    Deferred<AlterUniverseReplicationResponse> d =
      asyncClient.alterUniverseReplicationRemoveTables(
        replicationGroupName, sourceTableIdsToRemove);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public AlterUniverseReplicationResponse alterUniverseReplicationRemoveTables(
    String replicationGroupName,
    Set<String> sourceTableIdsToRemove,
    boolean removeTableIgnoreErrors) throws Exception {
    Deferred<AlterUniverseReplicationResponse> d =
      asyncClient.alterUniverseReplicationRemoveTables(
        replicationGroupName, sourceTableIdsToRemove, removeTableIgnoreErrors);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public AlterUniverseReplicationResponse alterUniverseReplicationSourceMasterAddresses(
    String replicationGroupName,
    Set<CommonNet.HostPortPB> sourceMasterAddresses) throws Exception {
    Deferred<AlterUniverseReplicationResponse> d =
      asyncClient.alterUniverseReplicationSourceMasterAddresses(
        replicationGroupName, sourceMasterAddresses);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public AlterUniverseReplicationResponse alterUniverseReplicationName(
    String replicationGroupName,
    String newReplicationGroupName) throws Exception {
    Deferred<AlterUniverseReplicationResponse> d =
      asyncClient.alterUniverseReplicationName(
        replicationGroupName, newReplicationGroupName);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public IsSetupUniverseReplicationDoneResponse isAlterUniverseReplicationDone(
    String replicationGroupName) throws Exception {
    Deferred<IsSetupUniverseReplicationDoneResponse> d =
      asyncClient.isSetupUniverseReplicationDone(replicationGroupName + ".ALTER");
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public GetChangesResponse getChangesCDCSDK(YBTable table, String streamId,
                                             String tabletId, long term,
                                             long index, byte[] key,
                                             int write_id, long time,
                                             boolean needSchemaInfo) throws Exception {
    Deferred<GetChangesResponse> d = asyncClient.getChangesCDCSDK(
      table, streamId, tabletId, term, index, key, write_id, time, needSchemaInfo);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  public GetChangesResponse getChangesCDCSDK(YBTable table, String streamId,
                                             String tabletId, long term,
                                             long index, byte[] key,
                                             int write_id, long time,
                                             boolean needSchemaInfo,
                                             CdcSdkCheckpoint explicitCheckpoint) throws Exception {
    Deferred<GetChangesResponse> d = asyncClient.getChangesCDCSDK(
      table, streamId, tabletId, term, index, key, write_id, time, needSchemaInfo,
      explicitCheckpoint);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  public GetChangesResponse getChangesCDCSDK(YBTable table, String streamId,
                                             String tabletId, long term,
                                             long index, byte[] key,
                                             int write_id, long time,
                                             boolean needSchemaInfo,
                                             CdcSdkCheckpoint explicitCheckpoint,
                                             long safeHybridTime) throws Exception {
    Deferred<GetChangesResponse> d = asyncClient.getChangesCDCSDK(
      table, streamId, tabletId, term, index, key, write_id, time, needSchemaInfo,
      explicitCheckpoint, safeHybridTime);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  public GetChangesResponse getChangesCDCSDK(YBTable table, String streamId, String tabletId,
      long term, long index, byte[] key, int write_id, long time, boolean needSchemaInfo,
      CdcSdkCheckpoint explicitCheckpoint, long safeHybridTime, int walSegmentIndex)
      throws Exception {
    Deferred<GetChangesResponse> d =
        asyncClient.getChangesCDCSDK(table, streamId, tabletId, term, index, key, write_id, time,
            needSchemaInfo, explicitCheckpoint, safeHybridTime, walSegmentIndex);
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  public GetCheckpointResponse getCheckpoint(YBTable table, String streamId,
                                             String tabletId) throws Exception {
    Deferred<GetCheckpointResponse> d = asyncClient
      .getCheckpoint(table, streamId, tabletId);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  public GetDBStreamInfoResponse getDBStreamInfo(String streamId) throws Exception {
    Deferred<GetDBStreamInfoResponse> d = asyncClient
      .getDBStreamInfo(streamId);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of child tablets for a given parent tablet.
   *
   * @param table    the {@link YBTable} instance of the table
   * @param streamId the DB stream ID to read from in the cdc_state table
   * @param tableId  the UUID of the table to which the parent tablet belongs
   * @param tabletId the UUID of the parent tablet
   * @return an RPC response containing the list of child tablets
   * @throws Exception
   */
  public GetTabletListToPollForCDCResponse getTabletListToPollForCdc(
      YBTable table, String streamId, String tableId, String tabletId) throws Exception {
    Deferred<GetTabletListToPollForCDCResponse> d = asyncClient
      .getTabletListToPollForCdc(table, streamId, tableId, tabletId);
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of tablets by reading the entries in the cdc_state table for a given table and
   * DB stream ID.
   * @param table the {@link YBTable} instance of the table
   * @param streamId the DB stream ID to read from in the cdc_state table
   * @param tableId the UUID of the table for which we need the tablets to poll for
   * @return an RPC response containing the list of tablets to poll for
   * @throws Exception
   */
  public GetTabletListToPollForCDCResponse getTabletListToPollForCdc(
    YBTable table, String streamId, String tableId) throws Exception {
    return getTabletListToPollForCdc(table, streamId, tableId, "");
  }

  /**
   * [Test purposes only] Split the provided tablet.
   * @param tabletId the UUID of the tablet to split
   * @return {@link SplitTabletResponse}
   * @throws Exception
   */
  public SplitTabletResponse splitTablet(String tabletId) throws Exception {
    Deferred<SplitTabletResponse> d = asyncClient.splitTablet(tabletId);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  /**
   * [Test purposes only] Flush and compact the provided table. Note that you will need to wait
   * accordingly for the table to get flushed and the SST files to get created.
   * @param tableId the UUID of the table to compact
   * @return an RPC response of type {@link FlushTableResponse} containing the flush request ID
   * @throws Exception
   */
  public FlushTableResponse flushTable(String tableId) throws Exception {
    Deferred<FlushTableResponse> d = asyncClient.flushTable(tableId);
    return d.join(2*getDefaultAdminOperationTimeoutMs());
  }

  public SetCheckpointResponse commitCheckpoint(YBTable table, String streamId,
                                                String tabletId,
                                                long term,
                                                long index,
                                                boolean initialCheckpoint) throws Exception {
    return commitCheckpoint(table, streamId, tabletId, term, index, initialCheckpoint,
                            false /* bootstrap */ , null /* cdcsdkSafeTime */);
  }

  public SetCheckpointResponse commitCheckpoint(YBTable table, String streamId,
                                                String tabletId,
                                                long term,
                                                long index,
                                                boolean initialCheckpoint,
                                                boolean bootstrap,
                                                Long cdcsdkSafeTime) throws Exception {
    Deferred<SetCheckpointResponse> d = asyncClient.setCheckpoint(table, streamId, tabletId, term,
      index, initialCheckpoint, bootstrap, cdcsdkSafeTime);
    d.addErrback(new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception o) throws Exception {
        o.printStackTrace();
        throw o;
      }
    });
    d.addCallback(setCheckpointResponse -> {
      return setCheckpointResponse;
    });
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the status of current server.
   * @param host the address to bind to.
   * @param port the port to bind to (0 means any free port).
   * @return an object containing the status details of the server.
   * @throws Exception
   */
  public GetStatusResponse getStatus(final String host, int port) throws Exception {
    Deferred<GetStatusResponse> d = asyncClient.getStatus(HostAndPort.fromParts(host, port));
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

    /**
   * Get the auto flag config for servers.
   * @return auto flag config for each server if exists, else a MasterErrorException.
   */
  public GetAutoFlagsConfigResponse autoFlagsConfig() throws Exception {
    Deferred<GetAutoFlagsConfigResponse> d = asyncClient.autoFlagsConfig();
    d.addErrback(new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception o) throws Exception {
        LOG.error("Error: ", o);
        throw o;
      }
    });
    d.addCallback(getAutoFlagsConfigResponse -> {
      return getAutoFlagsConfigResponse;
    });
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Promotes the auto flag config for each servers.
   * @param maxFlagClass class category up to which auto flag should be promoted.
   * @param promoteNonRuntimeFlags promotes auto flag non-runtime flags if true.
   * @param force promotes auto flag forcefully if true.
   * @return response from the server for promoting auto flag config, else a MasterErrorException.
   */
  public PromoteAutoFlagsResponse promoteAutoFlags(String maxFlagClass,
                                                   boolean promoteNonRuntimeFlags,
                                                   boolean force) throws Exception {
    Deferred<PromoteAutoFlagsResponse> d = asyncClient.getPromoteAutoFlagsResponse(
        maxFlagClass,
        promoteNonRuntimeFlags,
        force);
    d.addErrback(new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception o) throws Exception {
        LOG.error("Error: ", o);
        throw o;
      }
    });
    d.addCallback(promoteAutoFlagsResponse -> {
      return promoteAutoFlagsResponse;
    });
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Rollbacks the auto flag config for each servers.
   * @param rollbackVersion auto flags version to which rollback is desired.
   * @return response from the server for rolling back auto flag config,
   *         else a MasterErrorException.
   */
  public RollbackAutoFlagsResponse rollbackAutoFlags(int rollbackVersion) throws Exception {
    Deferred<RollbackAutoFlagsResponse> d = asyncClient.getRollbackAutoFlagsResponse(
        rollbackVersion);
    d.addErrback(new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception o) throws Exception {
        LOG.error("Error: ", o);
        throw o;
      }
    });
    d.addCallback(rollbackAutoFlagsResponse -> {
      return rollbackAutoFlagsResponse;
    });
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  public SetCheckpointResponse bootstrapTablet(YBTable table, String streamId,
                                               String tabletId,
                                               long term,
                                               long index,
                                               boolean initialCheckpoint,
                                               boolean bootstrap) throws Exception {
    Deferred<SetCheckpointResponse> d = asyncClient.setCheckpointWithBootstrap(table, streamId,
        tabletId, term, index, initialCheckpoint, bootstrap);
    d.addErrback(new Callback<Exception, Exception>() {
      @Override
      public Exception call(Exception o) throws Exception {
        o.printStackTrace();
        throw o;
      }
    });
    d.addCallback(setCheckpointResponse -> {
      return setCheckpointResponse;
    });
    return d.join(2 * getDefaultAdminOperationTimeoutMs());
  }

  public DeleteUniverseReplicationResponse deleteUniverseReplication(
    String replicationGroupName, boolean ignoreErrors) throws Exception {
    Deferred<DeleteUniverseReplicationResponse> d =
        asyncClient.deleteUniverseReplication(replicationGroupName, ignoreErrors);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public DeleteUniverseReplicationResponse deleteUniverseReplication(
    String replicationGroupName) throws Exception {
    return deleteUniverseReplication(replicationGroupName, false /* ignoreErrors */);
  }

  public GetUniverseReplicationResponse getUniverseReplication(
    String replicationGrouopName) throws Exception {
    Deferred<GetUniverseReplicationResponse> d =
      asyncClient.getUniverseReplication(replicationGrouopName);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public BootstrapUniverseResponse bootstrapUniverse(
    final HostAndPort hostAndPort, List<String> tableIds) throws Exception {
    Deferred<BootstrapUniverseResponse> d =
      asyncClient.bootstrapUniverse(hostAndPort, tableIds);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /** See {@link AsyncYBClient#changeXClusterRole(org.yb.cdc.CdcConsumer.XClusterRole)} */
  public ChangeXClusterRoleResponse changeXClusterRole(XClusterRole role) throws Exception {
    Deferred<ChangeXClusterRoleResponse> d = asyncClient.changeXClusterRole(role);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * It checks whether a bootstrap flow is required to set up replication or in case an existing
   * stream has fallen far behind.
   *
   * @param tableIdsStreamIdMap A map of table ids to their corresponding stream id if any
   * @return A deferred object that yields a {@link IsBootstrapRequiredResponse} which contains
   *         a map of each table id to a boolean showing whether bootstrap is required for that
   *         table
   */
  public IsBootstrapRequiredResponse isBootstrapRequired(
      Map<String, String> tableIdsStreamIdMap) throws Exception {
    Deferred<IsBootstrapRequiredResponse> d =
        asyncClient.isBootstrapRequired(tableIdsStreamIdMap);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * It makes parallel calls to {@link AsyncYBClient#isBootstrapRequired(java.util.Map)} method with
   * batches of {@code partitionSize} tables.
   *
   * @see YBClient#isBootstrapRequired(java.util.Map)
   */
  public List<IsBootstrapRequiredResponse> isBootstrapRequiredParallel(
      Map<String, String> tableIdStreamIdMap, int partitionSize) throws Exception {
    // Partition the tableIdStreamIdMap.
    List<Map<String, String>> tableIdStreamIdMapList = new ArrayList<>();
    Iterator<Entry<String, String>> iter = tableIdStreamIdMap.entrySet().iterator();
    while (iter.hasNext()) {
      Map<String, String> partition = new HashMap<>();
      tableIdStreamIdMapList.add(partition);

      while (partition.size() < partitionSize && iter.hasNext()) {
        Entry<String, String> entry = iter.next();
        partition.put(entry.getKey(), entry.getValue());
      }
    }

    // Make the requests asynchronously.
    List<Deferred<IsBootstrapRequiredResponse>> ds = new ArrayList<>();
    for (Map<String, String> tableIdStreamId : tableIdStreamIdMapList) {
      ds.add(asyncClient.isBootstrapRequired(tableIdStreamId));
    }

    // Wait for all the request to join.
    List<IsBootstrapRequiredResponse> isBootstrapRequiredList = new ArrayList<>();
    for (Deferred<IsBootstrapRequiredResponse> d : ds) {
      isBootstrapRequiredList.add(d.join(getDefaultAdminOperationTimeoutMs()));
    }

    return isBootstrapRequiredList;
  }

  public GetReplicationStatusResponse getReplicationStatus(
      @Nullable String replicationGroupName) throws Exception {
    Deferred<GetReplicationStatusResponse> d =
        asyncClient.getReplicationStatus(replicationGroupName);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public GetXClusterSafeTimeResponse getXClusterSafeTime() throws Exception {
    Deferred<GetXClusterSafeTimeResponse> d = asyncClient.getXClusterSafeTime();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public WaitForReplicationDrainResponse waitForReplicationDrain(
      List<String> streamIds,
      @Nullable Long targetTime) throws Exception {
    Deferred<WaitForReplicationDrainResponse> d =
        asyncClient.waitForReplicationDrain(streamIds, targetTime);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public WaitForReplicationDrainResponse waitForReplicationDrain(
      List<String> streamIds) throws Exception {
    return waitForReplicationDrain(streamIds, null /* targetTime */);
  }

  // DB Scoped replication methods.
  // --------------------------------------------------------------------------------

  /**
   * @see AsyncYBClient#xClusterCreateOutboundReplicationGroup(String, Set<String>)
   */
  public XClusterCreateOutboundReplicationGroupResponse xClusterCreateOutboundReplicationGroup(
      String replicationGroupId, Set<String> namespaceIds) throws Exception {
    Deferred<XClusterCreateOutboundReplicationGroupResponse> d =
        asyncClient.xClusterCreateOutboundReplicationGroup(replicationGroupId, namespaceIds);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#isXClusterBootstrapRequired(String, String)
   */
  public IsXClusterBootstrapRequiredResponse isXClusterBootstrapRequired(
      String replicationGroupId, String namespaceId) throws Exception {
    Deferred<IsXClusterBootstrapRequiredResponse> d =
        asyncClient.isXClusterBootstrapRequired(replicationGroupId, namespaceId);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#createXClusterReplication(String, Set<CommonNet.HostPortPB>)
   */
  public CreateXClusterReplicationResponse createXClusterReplication(
      String replicationGroupId, Set<CommonNet.HostPortPB> targetMasterAddresses) throws Exception {
    Deferred<CreateXClusterReplicationResponse> d =
        asyncClient.createXClusterReplication(replicationGroupId, targetMasterAddresses);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#isCreateXClusterReplicationDone(String, Set<CommonNet.HostPortPB>)
   */
  public IsCreateXClusterReplicationDoneResponse isCreateXClusterReplicationDone(
      String replicationGroupId, Set<CommonNet.HostPortPB> targetMasterAddresses) throws Exception {
    Deferred<IsCreateXClusterReplicationDoneResponse> d =
        asyncClient.isCreateXClusterReplicationDone(replicationGroupId, targetMasterAddresses);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  // Used by YBA to delete outbound replication from the source universe,
  public XClusterDeleteOutboundReplicationGroupResponse xClusterDeleteOutboundReplicationGroup(
      String replicationGroupId) throws Exception {
    Deferred<XClusterDeleteOutboundReplicationGroupResponse> d =
        asyncClient.xClusterDeleteOutboundReplicationGroup(replicationGroupId, null);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#xClusterDeleteOutboundReplicationGroup(String, Set<CommonNet.HostPortPB>)
   */
  public XClusterDeleteOutboundReplicationGroupResponse xClusterDeleteOutboundReplicationGroup(
    String replicationGroupId, @Nullable Set<CommonNet.HostPortPB> targetMasterAddresses)
    throws Exception {
  Deferred<XClusterDeleteOutboundReplicationGroupResponse> d =
      asyncClient.xClusterDeleteOutboundReplicationGroup(replicationGroupId, targetMasterAddresses);
  return d.join(getDefaultAdminOperationTimeoutMs());
  }

  // --------------------------------------------------------------------------------
  // End of DB Scoped replication methods.

  /**
   * Get information about the namespace/database.
   * @param keyspaceName namespace name to get details about.
   * @param databaseType database type the database belongs to.
   * @return details for the namespace.
   * @throws Exception
   */
  public GetNamespaceInfoResponse getNamespaceInfo(String keyspaceName,
      YQLDatabase databaseType) throws Exception {
    Deferred<GetNamespaceInfoResponse> d = asyncClient.getNamespaceInfo(keyspaceName, databaseType);
    return d.join(getDefaultOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#listCDCStreams(String, String, MasterReplicationOuterClass.IdTypePB)
   */
  public ListCDCStreamsResponse listCDCStreams(
      String tableId,
      String namespaceId,
      MasterReplicationOuterClass.IdTypePB idType) throws Exception {
    Deferred<ListCDCStreamsResponse> d =
        asyncClient.listCDCStreams(tableId, namespaceId, idType);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#deleteCDCStream(Set, boolean, boolean)
   */
  public DeleteCDCStreamResponse deleteCDCStream(Set<String> streamIds,
                                                 boolean ignoreErrors,
                                                 boolean forceDelete) throws Exception {
    Deferred<DeleteCDCStreamResponse> d =
      asyncClient.deleteCDCStream(streamIds, ignoreErrors, forceDelete);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * @see AsyncYBClient#getTabletLocations(List<String>, String, boolean, boolean)
   */
  public GetTabletLocationsResponse getTabletLocations(List<String> tabletIds,
                                                               String tableId,
                                                               boolean includeInactive,
                                                               boolean includeDeleted)
                                                               throws Exception {
    Deferred<GetTabletLocationsResponse> d =
      asyncClient.getTabletLocations(tabletIds, tableId, includeInactive, includeDeleted);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public CreateSnapshotScheduleResponse createSnapshotSchedule(
    YQLDatabase databaseType,
    String keyspaceName,
    long retentionInSecs,
    long timeIntervalInSecs) throws Exception {
    Deferred<CreateSnapshotScheduleResponse> d =
      asyncClient.createSnapshotSchedule(databaseType, keyspaceName,
          retentionInSecs, timeIntervalInSecs);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public CreateSnapshotScheduleResponse createSnapshotSchedule(
      YQLDatabase databaseType,
      String keyspaceName,
      String keyspaceId,
      long retentionInSecs,
      long timeIntervalInSecs) throws Exception {
    Deferred<CreateSnapshotScheduleResponse> d =
        asyncClient.createSnapshotSchedule(
            databaseType,
            keyspaceName,
            keyspaceId,
            retentionInSecs,
            timeIntervalInSecs);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public DeleteSnapshotScheduleResponse deleteSnapshotSchedule(
      UUID snapshotScheduleUUID) throws Exception {
    Deferred<DeleteSnapshotScheduleResponse> d =
      asyncClient.deleteSnapshotSchedule(snapshotScheduleUUID);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public ListSnapshotSchedulesResponse listSnapshotSchedules(
      UUID snapshotScheduleUUID) throws Exception {
    Deferred<ListSnapshotSchedulesResponse> d =
      asyncClient.listSnapshotSchedules(snapshotScheduleUUID);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public RestoreSnapshotScheduleResponse restoreSnapshotSchedule(UUID snapshotScheduleUUID,
                                                 long restoreTimeInMillis) throws Exception {
    Deferred<RestoreSnapshotScheduleResponse> d =
      asyncClient.restoreSnapshotSchedule(snapshotScheduleUUID, restoreTimeInMillis);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public ListSnapshotRestorationsResponse listSnapshotRestorations(
      UUID restorationUUID) throws Exception {
    Deferred<ListSnapshotRestorationsResponse> d =
      asyncClient.listSnapshotRestorations(restorationUUID);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public ListSnapshotsResponse listSnapshots(UUID snapshotUUID,
                                             boolean listDeletedSnapshots) throws Exception {
    Deferred<ListSnapshotsResponse> d =
      asyncClient.listSnapshots(snapshotUUID, listDeletedSnapshots);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public DeleteSnapshotResponse deleteSnapshot(
      UUID snapshotUUID) throws Exception {
    Deferred<DeleteSnapshotResponse> d =
      asyncClient.deleteSnapshot(snapshotUUID);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  public ValidateReplicationInfoResponse validateReplicationInfo(
    ReplicationInfoPB replicationInfoPB) throws Exception {
    Deferred<ValidateReplicationInfoResponse> d =
      asyncClient.validateReplicationInfo(replicationInfoPB);
    return d.join(getDefaultAdminOperationTimeoutMs());
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
    asyncClient.shutdown();
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
     * Sets the certificate file in case SSL is enabled.
     * Optional.
     * If not provided, defaults to null.
     * A value of null disables an SSL connection.
     * @param certFile the path to the certificate.
     * @return this builder
     */
    public YBClientBuilder sslCertFile(String certFile) {
      clientBuilder.sslCertFile(certFile);
      return this;
    }

    /**
     * Sets the client cert and key files if mutual auth is enabled.
     * Optional.
     * If not provided, defaults to null.
     * A value of null disables an mTLS connection.
     * @param certFile the path to the client certificate.
     * @param keyFile the path to the client private key file.
     * @return this builder
     */
    public YBClientBuilder sslClientCertFiles(String certFile, String keyFile) {
      clientBuilder.sslClientCertFiles(certFile, keyFile);
      return this;
    }

    /**
     * Sets the outbound client host:port on which the socket binds.
     * Optional.
     * If not provided, defaults to null.
     * @param host the address to bind to.
     * @param port the port to bind to (0 means any free port).
     * @return this builder
     */
    public YBClientBuilder bindHostAddress(String host, int port) {
      clientBuilder.bindHostAddress(host, port);
      return this;
    }

    /**
     * Single thread pool is used in netty4 to handle client IO
     */
    @Deprecated
    @SuppressWarnings("unused")
    public YBClientBuilder nioExecutors(Executor bossExecutor, Executor workerExecutor) {
      return executor(workerExecutor);
    }

    /**
     * Set the executors which will be used for the embedded Netty workers.
     * Optional.
     * If not provided, uses a simple cached threadpool. If either argument is null,
     * then such a thread pool will be used in place of that argument.
     * Note: executor's max thread number must be greater or equal to corresponding
     * thread count, or netty cannot start enough threads, and client will get stuck.
     * If not sure, please just use CachedThreadPool.
     */
    public YBClientBuilder executor(Executor executor) {
      clientBuilder.executor(executor);
      return this;
    }

    /**
     * Single thread pool is used in netty4 to handle client IO
     */
    @Deprecated
    @SuppressWarnings("unused")
    public YBClientBuilder bossCount(int workerCount) {
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
