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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

import com.google.common.net.HostAndPort;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import org.yb.CommonNet;
import org.yb.CommonNet.ReplicationInfoPB;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.Schema;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.cdc.CdcConsumer.XClusterRole;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.util.Pair;

/**
 * Interface for a synchronous and thread-safe client for YB.
 *
 * <p>Implemented by {@link YBClient}.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public interface YBClientApi extends AutoCloseable {

  interface Condition {
    boolean get() throws Exception;
  }

  void createRedisNamespace() throws Exception;

  boolean createRedisNamespace(boolean ifNotExist) throws Exception;

  YBTable createRedisTable(String name, int numTablets) throws Exception;

  YBTable createRedisTable(String name) throws Exception;

  YBTable createRedisTable(String name, boolean ifNotExist) throws Exception;

  YBTable createRedisTableOnly(String name) throws Exception;

  YBTable createRedisTableOnly(String name, boolean ifNotExist) throws Exception;

  YBTable createTable(String keyspace, String name, Schema schema) throws Exception;

  YBTable createTable(String keyspace, String name, Schema schema, CreateTableOptions builder)
    throws Exception;

  CreateKeyspaceResponse createKeyspace(String keyspace) throws Exception;

  CreateKeyspaceResponse createKeyspace(String keyspace, YQLDatabase databaseType) throws Exception;

  DeleteNamespaceResponse deleteNamespace(String keyspaceName) throws Exception;

  DeleteTableResponse deleteTable(String keyspace, String name) throws Exception;

  AlterTableResponse alterTable(String keyspace, String name, AlterTableOptions ato)
    throws Exception;

  GetMasterHeartbeatDelaysResponse getMasterHeartbeatDelays() throws Exception;

  boolean isAlterTableDone(String keyspace, String name) throws Exception;

  ListTabletServersResponse listTabletServers() throws Exception;

  ListLiveTabletServersResponse listLiveTabletServers() throws Exception;

  ListMastersResponse listMasters() throws Exception;

  ListMasterRaftPeersResponse listMasterRaftPeers() throws Exception;

  GetMasterClusterConfigResponse getMasterClusterConfig() throws Exception;

  ChangeMasterClusterConfigResponse changeMasterClusterConfig(
    CatalogEntityInfo.SysClusterConfigEntryPB config) throws Exception;

  ChangeLoadBalancerStateResponse changeLoadBalancerState(boolean isEnable) throws Exception;

  GetLoadBalancerStateResponse getLoadBalancerState() throws Exception;

  GetLoadMovePercentResponse getLoadMoveCompletion() throws Exception;

  AreNodesSafeToTakeDownResponse areNodesSafeToTakeDown(
    Set<String> masterIps, Set<String> tserverIps, long followerLagBoundMs) throws Exception;

  GetLoadMovePercentResponse getLeaderBlacklistCompletion() throws Exception;

  IsLoadBalancedResponse getIsLoadBalanced(int numServers) throws Exception;

  IsLoadBalancerIdleResponse getIsLoadBalancerIdle() throws Exception;

  AreLeadersOnPreferredOnlyResponse getAreLeadersOnPreferredOnly() throws Exception;

  IsInitDbDoneResponse getIsInitDbDone() throws Exception;

  boolean waitForMaster(HostAndPort hp, long timeoutMS) throws Exception;

  String getLeaderMasterUUID();

  List<GetMasterRegistrationResponse> getMasterRegistrationResponseList();

  HostAndPort getLeaderMasterHostAndPort();

  ChangeConfigResponse changeMasterConfig(String host, int port, boolean isAdd) throws Exception;

  ChangeConfigResponse changeMasterConfig(String host, int port, boolean isAdd, boolean useHost)
    throws Exception;

  ChangeConfigResponse changeMasterConfig(
    String host, int port, boolean isAdd, boolean useHost, String hostAddrToAdd) throws Exception;

  void waitForMasterLeader(long timeoutMs) throws Exception;

  boolean enableEncryptionAtRestInMemory(String versionId) throws Exception;

  boolean disableEncryptionAtRestInMemory() throws Exception;

  boolean enableEncryptionAtRest(String file) throws Exception;

  boolean disableEncryptionAtRest() throws Exception;

  Pair<Boolean, String> isEncryptionEnabled() throws Exception;

  void addUniverseKeys(Map<String, byte[]> universeKeys, HostAndPort hp) throws Exception;

  boolean hasUniverseKeyInMemory(String universeKeyId, HostAndPort hp) throws Exception;

  boolean ping(String host, int port) throws Exception;

  boolean setFlag(HostAndPort hp, String flag, String value) throws Exception;

  boolean setFlag(HostAndPort hp, String flag, String value, boolean force) throws Exception;

  String getFlag(HostAndPort hp, String flag) throws Exception;

  String getMasterAddresses(HostAndPort hp) throws Exception;

  UpgradeYsqlResponse upgradeYsql(HostAndPort hp, boolean useSingleConnection) throws Exception;

  ListTabletsResponse listStatusAndSchemaOfTabletsForTServer(HostAndPort hp) throws Exception;

  ConnectivityStateResponse getConnectivityState(HostAndPort tserverHP) throws Exception;

  GetConsensusStateResponse getTabletConsensusStateFromTS(String tabletId, HostAndPort hp)
    throws Exception;

  GetLatestEntryOpIdResponse getLatestEntryOpIds(HostAndPort hp, List<String> tabletIds)
    throws Exception;

  IsServerReadyResponse isServerReady(HostAndPort hp, boolean isTserver) throws Exception;

  ListTabletsForTabletServerResponse listTabletsForTabletServer(HostAndPort hp) throws Exception;

  boolean reloadCertificates(HostAndPort server) throws RuntimeException, IllegalStateException;

  void injectWaitError();

  boolean waitForReplicaCount(YBTable table, int numReplicas, long timeoutMs);

  boolean waitForServer(HostAndPort hp, long timeoutMs);

  boolean waitForLoadBalance(long timeoutMs, int numServers);

  boolean waitForLoadBalancerActive(long timeoutMs);

  boolean waitForLoadBalancerIdle(long timeoutMs);

  boolean waitForAreLeadersOnPreferredOnlyCondition(long timeoutMs);

  boolean waitForExpectedReplicaMap(
    long timeoutMs, YBTable table, Map<String, List<List<Integer>>> replicaMapExpected);

  boolean waitForMasterHasUniverseKeyInMemory(
    long timeoutMs, String universeKeyId, HostAndPort hp);

  LeaderStepDownResponse masterLeaderStepDown() throws Exception;

  ListTablesResponse getTablesList() throws Exception;

  ListTablesResponse getTablesList(String nameFilter) throws Exception;

  ListTablesResponse getTablesList(
    String nameFilter, boolean excludeSystemTables, String namespace)
    throws Exception;

  ListNamespacesResponse getNamespacesList() throws Exception;

  CreateCDCStreamResponse createCDCStream(
    HostAndPort hp, String tableId, String nameSpaceName, String format, String checkpointType)
    throws Exception;

  CreateCDCStreamResponse createCDCStream(
    YBTable table, String nameSpaceName, String format, String checkpointType) throws Exception;

  CreateCDCStreamResponse createCDCStream(
    YBTable table, String nameSpaceName, String format, String checkpointType, String recordType)
    throws Exception;

  CreateCDCStreamResponse createCDCStream(
    YBTable table,
    String nameSpaceName,
    String format,
    String checkpointType,
    String recordType,
    boolean dbtype,
    boolean consistentSnapshot,
    boolean useSnapshot)
    throws Exception;

  boolean waitForTableRemoval(long timeoutMs, String name);

  boolean tableExists(String keyspace, String name) throws Exception;

  boolean tableExistsByUUID(String tableUUID) throws Exception;

  GetTableSchemaResponse getTableSchema(String keyspace, String name) throws Exception;

  GetTableSchemaResponse getTableSchemaByUUID(String tableUUID) throws Exception;

  YBTable openTable(String keyspace, String name) throws Exception;

  Set<String> getTabletUUIDs(YBTable table) throws Exception;

  Set<String> getTabletUUIDs(String keyspace, String name) throws Exception;

  YBTable openTableByUUID(String tableUUID) throws Exception;

  SetupUniverseReplicationResponse setupUniverseReplication(
    String replicationGroupName,
    Map<String, String> sourceTableIdsBootstrapIdMap,
    Set<CommonNet.HostPortPB> sourceMasterAddresses,
    @Nullable Boolean isTransactional)
    throws Exception;

  SetupUniverseReplicationResponse setupUniverseReplication(
    String replicationGroupName,
    Map<String, String> sourceTableIdsBootstrapIdMap,
    Set<CommonNet.HostPortPB> sourceMasterAddresses)
    throws Exception;

  IsSetupUniverseReplicationDoneResponse isSetupUniverseReplicationDone(
    String replicationGroupName)
    throws Exception;

  SetUniverseReplicationEnabledResponse setUniverseReplicationEnabled(
    String replicationGroupName, boolean active) throws Exception;

  AlterUniverseReplicationResponse alterUniverseReplicationAddTables(
    String replicationGroupName, Map<String, String> sourceTableIdsToAddBootstrapIdMap)
    throws Exception;

  AlterUniverseReplicationResponse alterUniverseReplicationRemoveTables(
    String replicationGroupName, Set<String> sourceTableIdsToRemove) throws Exception;

  AlterUniverseReplicationResponse alterUniverseReplicationRemoveTables(
    String replicationGroupName,
    Set<String> sourceTableIdsToRemove,
    boolean removeTableIgnoreErrors)
    throws Exception;

  AlterUniverseReplicationResponse alterUniverseReplicationSourceMasterAddresses(
    String replicationGroupName, Set<CommonNet.HostPortPB> sourceMasterAddresses)
    throws Exception;

  AlterUniverseReplicationResponse alterUniverseReplicationName(
    String replicationGroupName, String newReplicationGroupName) throws Exception;

  IsSetupUniverseReplicationDoneResponse isAlterUniverseReplicationDone(
    String replicationGroupName)
    throws Exception;

  GetChangesResponse getChangesCDCSDK(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    byte[] key,
    int write_id,
    long time,
    boolean needSchemaInfo)
    throws Exception;

  GetChangesResponse getChangesCDCSDK(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    byte[] key,
    int write_id,
    long time,
    boolean needSchemaInfo,
    CdcSdkCheckpoint explicitCheckpoint)
    throws Exception;

  GetChangesResponse getChangesCDCSDK(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    byte[] key,
    int write_id,
    long time,
    boolean needSchemaInfo,
    CdcSdkCheckpoint explicitCheckpoint,
    long safeHybridTime)
    throws Exception;

  GetChangesResponse getChangesCDCSDK(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    byte[] key,
    int write_id,
    long time,
    boolean needSchemaInfo,
    CdcSdkCheckpoint explicitCheckpoint,
    long safeHybridTime,
    int walSegmentIndex)
    throws Exception;

  GetChangesResponse getChangesCDCSDK(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    byte[] key,
    int write_id,
    long time,
    boolean needSchemaInfo,
    CdcSdkCheckpoint explicitCheckpoint,
    long safeHybridTime,
    int walSegmentIndex,
    Long getchangesRespMaxSizeBytes)
    throws Exception;

  GetChangesResponse getChangesCDCSDK(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    byte[] key,
    int write_id,
    long time,
    boolean needSchemaInfo,
    CdcSdkCheckpoint explicitCheckpoint,
    long safeHybridTime,
    int walSegmentIndex,
    Long getchangesRespMaxSizeBytes,
    long maxIndexInSortWindow)
    throws Exception;

  GetCheckpointResponse getCheckpoint(YBTable table, String streamId, String tabletId)
    throws Exception;

  GetDBStreamInfoResponse getDBStreamInfo(String streamId) throws Exception;

  GetTabletListToPollForCDCResponse getTabletListToPollForCdc(
    YBTable table, String streamId, String tableId, String tabletId) throws Exception;

  GetTabletListToPollForCDCResponse getTabletListToPollForCdc(
    YBTable table, String streamId, String tableId) throws Exception;

  SplitTabletResponse splitTablet(String tabletId) throws Exception;

  FlushTableResponse flushTable(String tableId) throws Exception;

  FlushTabletsResponse flushTablets(String tserverIp, int rpcPort, List<String> tabletIds)
    throws Exception;

  SetCheckpointResponse commitCheckpoint(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    boolean initialCheckpoint)
    throws Exception;

  SetCheckpointResponse commitCheckpoint(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    boolean initialCheckpoint,
    boolean bootstrap,
    Long cdcsdkSafeTime)
    throws Exception;

  GetStatusResponse getStatus(String host, int port) throws Exception;

  GetAutoFlagsConfigResponse autoFlagsConfig() throws Exception;

  PromoteAutoFlagsResponse promoteAutoFlags(
    String maxFlagClass, boolean promoteNonRuntimeFlags, boolean force) throws Exception;

  RollbackAutoFlagsResponse rollbackAutoFlags(int rollbackVersion) throws Exception;

  StartYsqlMajorCatalogUpgradeResponse startYsqlMajorCatalogUpgrade() throws Exception;

  IsYsqlMajorCatalogUpgradeDoneResponse isYsqlMajorCatalogUpgradeDone() throws Exception;

  FinalizeYsqlMajorCatalogUpgradeResponse finalizeYsqlMajorCatalogUpgrade() throws Exception;

  RollbackYsqlMajorCatalogVersionResponse rollbackYsqlMajorCatalogVersion() throws Exception;

  GetYsqlMajorCatalogUpgradeStateResponse getYsqlMajorCatalogUpgradeState() throws Exception;

  SetCheckpointResponse bootstrapTablet(
    YBTable table,
    String streamId,
    String tabletId,
    long term,
    long index,
    boolean initialCheckpoint,
    boolean bootstrap)
    throws Exception;

  DeleteUniverseReplicationResponse deleteUniverseReplication(
    String replicationGroupName, boolean ignoreErrors) throws Exception;

  DeleteUniverseReplicationResponse deleteUniverseReplication(String replicationGroupName)
    throws Exception;

  GetUniverseReplicationResponse getUniverseReplication(String replicationGroupName)
    throws Exception;

  GetUniverseReplicationInfoResponse getUniverseReplicationInfo(String replicationGroupName)
    throws Exception;

  GetXClusterOutboundReplicationGroupInfoResponse getXClusterOutboundReplicationGroupInfo(
    String replicationGroupName) throws Exception;

  GetXClusterOutboundReplicationGroupsResponse getXClusterOutboundReplicationGroups(
    @Nullable String namespaceId) throws Exception;

  BootstrapUniverseResponse bootstrapUniverse(HostAndPort hostAndPort, List<String> tableIds)
    throws Exception;

  ChangeXClusterRoleResponse changeXClusterRole(XClusterRole role) throws Exception;

  IsBootstrapRequiredResponse isBootstrapRequired(Map<String, String> tableIdsStreamIdMap)
    throws Exception;

  List<IsBootstrapRequiredResponse> isBootstrapRequiredParallel(
    Map<String, String> tableIdStreamIdMap, int partitionSize) throws Exception;

  GetReplicationStatusResponse getReplicationStatus(@Nullable String replicationGroupName)
    throws Exception;

  GetXClusterSafeTimeResponse getXClusterSafeTime() throws Exception;

  XClusterFailoverResponse xClusterFailover(String replicationGroupId) throws Exception;

  IsXClusterFailoverDoneResponse isXClusterFailoverDone(String replicationGroupId)
    throws Exception;

  WaitForReplicationDrainResponse waitForReplicationDrain(
    List<String> streamIds, @Nullable Long targetTime) throws Exception;

  WaitForReplicationDrainResponse waitForReplicationDrain(List<String> streamIds) throws Exception;

  XClusterCreateOutboundReplicationGroupResponse xClusterCreateOutboundReplicationGroup(
    String replicationGroupId, Set<String> namespaceIds, boolean automaticDdlMode)
    throws Exception;

  IsXClusterBootstrapRequiredResponse isXClusterBootstrapRequired(
    String replicationGroupId, String namespaceId) throws Exception;

  CreateXClusterReplicationResponse createXClusterReplication(
    String replicationGroupId, Set<CommonNet.HostPortPB> targetMasterAddresses) throws Exception;

  IsCreateXClusterReplicationDoneResponse isCreateXClusterReplicationDone(
    String replicationGroupId, Set<CommonNet.HostPortPB> targetMasterAddresses) throws Exception;

  XClusterDeleteOutboundReplicationGroupResponse xClusterDeleteOutboundReplicationGroup(
    String replicationGroupId) throws Exception;

  XClusterDeleteOutboundReplicationGroupResponse xClusterDeleteOutboundReplicationGroup(
    String replicationGroupId, @Nullable Set<CommonNet.HostPortPB> targetMasterAddresses)
    throws Exception;

  XClusterAddNamespaceToOutboundReplicationGroupResponse
    xClusterAddNamespaceToOutboundReplicationGroup(String replicationGroupId, String namespaceId)
    throws Exception;

  XClusterRemoveNamespaceFromOutboundReplicationGroupResponse
    xClusterRemoveNamespaceFromOutboundReplicationGroup(
    String replicationGroupId, String namespaceId)
    throws Exception;

  AlterUniverseReplicationResponse alterUniverseReplicationRemoveNamespace(
    String replicationGroupId, String namespaceId) throws Exception;

  AddNamespaceToXClusterReplicationResponse addNamespaceToXClusterReplication(
    String replicationGroupId,
    Set<CommonNet.HostPortPB> targetMasterAddresses,
    String namespaceId)
    throws Exception;

  IsAlterXClusterReplicationDoneResponse isAlterXClusterReplicationDone(
    String replicationGroupId, Set<CommonNet.HostPortPB> targetMasterAddresses) throws Exception;

  GetNamespaceInfoResponse getNamespaceInfo(String keyspaceName, YQLDatabase databaseType)
    throws Exception;

  ListCDCStreamsResponse listCDCStreams(
    String tableId, String namespaceId, MasterReplicationOuterClass.IdTypePB idType)
    throws Exception;

  DeleteCDCStreamResponse deleteCDCStream(
    Set<String> streamIds, boolean ignoreErrors, boolean forceDelete) throws Exception;

  GetTabletLocationsResponse getTabletLocations(
    List<String> tabletIds, String tableId, boolean includeHidden, boolean includeDeleted)
    throws Exception;

  CreateSnapshotScheduleResponse createSnapshotSchedule(
    YQLDatabase databaseType, String keyspaceName, long retentionInSecs, long timeIntervalInSecs)
    throws Exception;

  EditSnapshotScheduleResponse editSnapshotSchedule(
    UUID snapshotScheduleUUID, long retentionInSecs, long timeIntervalInSecs) throws Exception;

  CreateSnapshotScheduleResponse createSnapshotSchedule(
    YQLDatabase databaseType,
    String keyspaceName,
    String keyspaceId,
    long retentionInSecs,
    long timeIntervalInSecs)
    throws Exception;

  DeleteSnapshotScheduleResponse deleteSnapshotSchedule(UUID snapshotScheduleUUID)
    throws Exception;

  ListSnapshotSchedulesResponse listSnapshotSchedules(UUID snapshotScheduleUUID) throws Exception;

  RestoreSnapshotScheduleResponse restoreSnapshotSchedule(
    UUID snapshotScheduleUUID, long restoreTimeInMillis) throws Exception;

  ListSnapshotRestorationsResponse listSnapshotRestorations(UUID restorationUUID) throws Exception;

  ListSnapshotsResponse listSnapshots(UUID snapshotUUID, boolean listDeletedSnapshots)
    throws Exception;

  DeleteSnapshotResponse deleteSnapshot(UUID snapshotUUID) throws Exception;

  CloneNamespaceResponse cloneNamespace(
    YQLDatabase databaseType,
    String sourceKeyspaceName,
    String targetKeyspaceName,
    long cloneTimeInMillis)
    throws Exception;

  CloneNamespaceResponse cloneNamespace(
    YQLDatabase databaseType,
    String sourceKeyspaceName,
    String keyspaceId,
    String targetKeyspaceName,
    long cloneTimeInMillis)
    throws Exception;

  ListClonesResponse listClones(String keyspaceId, Integer cloneSeqNo) throws Exception;

  ValidateReplicationInfoResponse validateReplicationInfo(ReplicationInfoPB replicationInfoPB)
    throws Exception;

  SetPreferredZonesResponse setPreferredZones(
    Map<Integer, List<CommonNet.CloudInfoPB>> prioritiesMap)
    throws Exception;

  ValidateFlagValueResponse validateFlagValue(String flagName, String flagValue) throws Exception;

  ValidateFlagValueResponse validateFlagValues(HostAndPort hp, Map<String, String> flags)
    throws Exception;

  void shutdown() throws Exception;

  long getDefaultOperationTimeoutMs();

  long getDefaultAdminOperationTimeoutMs();
}
