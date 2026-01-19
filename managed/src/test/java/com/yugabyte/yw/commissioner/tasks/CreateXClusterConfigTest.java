// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.filterTableInfoListByTableIds;
import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap;
import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getTableIds;
import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getTableIdsWithoutTablesOnTargetInReplication;
import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getTableInfoList;
import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getTableType;
import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.groupByNamespaceId;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static com.yugabyte.yw.models.XClusterNamespaceConfig.Status.Validated;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anySet;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.yb.CommonTypes.YQLDatabase.YQL_DATABASE_PGSQL;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.lang.reflect.Constructor;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.Schema;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.BootstrapUniverseResponse;
import org.yb.client.CreateSnapshotScheduleResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetNamespaceInfoResponse;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.GetXClusterOutboundReplicationGroupsResponse;
import org.yb.client.IsCreateXClusterReplicationDoneResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.IsXClusterBootstrapRequiredResponse;
import org.yb.client.ListCDCStreamsResponse;
import org.yb.client.ListNamespacesResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.SetupUniverseReplicationResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.XClusterCreateOutboundReplicationGroupResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateXClusterConfigTest extends CommissionerBaseTest {

  private Universe sourceUniverse;
  private Users defaultUser;
  private UUID targetUniverseUUID;
  private Universe targetUniverse;
  private String exampleTableID1;
  private String exampleTableID2;
  private String exampleStreamID1;
  private String exampleStreamID2;
  private String exampleTable1Name;
  private String exampleTable2Name;
  private String namespace1Name;
  private String namespace1Id;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  @Before
  public void setUp() throws Exception {
    defaultCustomer = testCustomer("CreateXClusterConfig-test-customer");
    defaultUser = ModelFactory.testUser(defaultCustomer);

    String configName = "CreateXClusterConfigTest-test-config";

    String sourceUniverseName = "CreateXClusterConfig-test-universe-1";
    UUID sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse(sourceUniverseName, sourceUniverseUUID);
    UniverseDefinitionTaskParams sourceUniverseDetails = sourceUniverse.getUniverseDetails();
    NodeDetails sourceUniverseNodeDetails = new NodeDetails();
    sourceUniverseNodeDetails.isMaster = true;
    sourceUniverseNodeDetails.isTserver = true;
    sourceUniverseNodeDetails.state = NodeState.Live;
    sourceUniverseNodeDetails.cloudInfo = new CloudSpecificInfo();
    sourceUniverseNodeDetails.cloudInfo.private_ip = "1.1.1.1";
    sourceUniverseNodeDetails.cloudInfo.secondary_private_ip = "2.2.2.2";
    sourceUniverseNodeDetails.placementUuid =
        sourceUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    sourceUniverseDetails.nodeDetailsSet.add(sourceUniverseNodeDetails);
    sourceUniverse.setUniverseDetails(sourceUniverseDetails);
    sourceUniverse.update();

    String targetUniverseName = "CreateXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);
    UniverseDefinitionTaskParams targetUniverseDetails = targetUniverse.getUniverseDetails();
    NodeDetails targetUniverseNodeDetails = new NodeDetails();
    targetUniverseNodeDetails.nodeName = "target-node";
    targetUniverseNodeDetails.isMaster = true;
    targetUniverseNodeDetails.isTserver = true;
    targetUniverseNodeDetails.state = NodeState.Live;
    targetUniverseNodeDetails.cloudInfo = new CloudSpecificInfo();
    targetUniverseNodeDetails.cloudInfo.private_ip = "3.3.3.3";
    targetUniverseNodeDetails.cloudInfo.secondary_private_ip = "4.4.4.4";
    targetUniverseNodeDetails.placementUuid =
        targetUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    targetUniverseDetails.nodeDetailsSet.add(targetUniverseNodeDetails);
    targetUniverse.setUniverseDetails(targetUniverseDetails);
    targetUniverse.update();

    namespace1Name = "ycql-namespace1";
    namespace1Id = UUID.randomUUID().toString();

    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";

    exampleTable1Name = "exampleTable1";
    exampleTable2Name = "exampleTable2";

    exampleStreamID1 = "fea203ffca1f48349901e0de2b52c416";
    exampleStreamID2 = "ec10532900ef42a29a6899c82dd7404d";

    Set<String> exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);
    exampleTables.add(exampleTableID2);

    createFormData = new XClusterConfigCreateFormData();
    createFormData.name = configName;
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverseUUID;
    createFormData.tables = exampleTables;

    mockClient = mock(YBClient.class);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);

    // Use reflection to access the package-private constructor.
    Constructor<ListCDCStreamsResponse> constructor =
        ListCDCStreamsResponse.class.getDeclaredConstructor(
            long.class, String.class, MasterTypes.MasterErrorPB.class, List.class);
    constructor.setAccessible(true);
    ListCDCStreamsResponse listCDCStreamsResp = constructor.newInstance(0, "", null, List.of());
    when(mockClient.listCDCStreams(null, null, null)).thenReturn(listCDCStreamsResp);

    GetTableSchemaResponse mockTableSchemaResponseTable1 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(List.of()),
            namespace1Name,
            "exampleTableID1",
            exampleTableID1,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            List.of(),
            false);
    GetTableSchemaResponse mockTableSchemaResponseTable2 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(List.of()),
            namespace1Name,
            "exampleTableID2",
            exampleTableID2,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            List.of(),
            false);
    lenient()
        .when(mockClient.getTableSchemaByUUID(exampleTableID1))
        .thenReturn(mockTableSchemaResponseTable1);
    lenient()
        .when(mockClient.getTableSchemaByUUID(exampleTableID2))
        .thenReturn(mockTableSchemaResponseTable2);
    lenient()
        .when(mockAutoFlagUtil.getPromotedAutoFlags(any(), any(), anyInt()))
        .thenReturn(Set.of("FLAG_1"));
    GFlagsValidation.AutoFlagDetails autoFlagDetails = new GFlagsValidation.AutoFlagDetails();
    autoFlagDetails.name = "FLAG_1";
    GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
        new GFlagsValidation.AutoFlagsPerServer();
    autoFlagsPerServer.autoFlagDetails = Collections.singletonList(autoFlagDetails);
    when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
        .thenReturn(autoFlagsPerServer);
  }

  private TaskInfo submitTask(
      XClusterConfig xClusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList) {
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(
            xClusterConfig,
            createFormData.bootstrapParams,
            requestedTableInfoList,
            Collections.emptyMap(),
            Collections.emptyMap());
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);

      // Set http context.
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          TargetType.XClusterConfig,
          CustomerTask.TaskType.CreateXClusterConfig,
          xClusterConfig.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private void initTargetUniverseClusterConfig(String replicationGroupName, int numberOfTables)
      throws Exception {
    CdcConsumer.ProducerEntryPB.Builder fakeProducerEntry =
        CdcConsumer.ProducerEntryPB.newBuilder();
    switch (numberOfTables) {
      case 2:
        CdcConsumer.StreamEntryPB.Builder fakeStreamEntry2 =
            CdcConsumer.StreamEntryPB.newBuilder().setProducerTableId(exampleTableID2);
        fakeProducerEntry.putStreamMap(exampleStreamID2, fakeStreamEntry2.build());
        // Intentional fall-through.
      case 1:
      default:
        CdcConsumer.StreamEntryPB.Builder fakeStreamEntry1 =
            CdcConsumer.StreamEntryPB.newBuilder().setProducerTableId(exampleTableID1);
        fakeProducerEntry.putStreamMap(exampleStreamID1, fakeStreamEntry1.build());
    }

    CdcConsumer.ConsumerRegistryPB.Builder fakeConsumerRegistryBuilder =
        CdcConsumer.ConsumerRegistryPB.newBuilder()
            .putProducerMap(replicationGroupName, fakeProducerEntry.build());

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder fakeClusterConfigBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder()
            .setConsumerRegistry(fakeConsumerRegistryBuilder.build());

    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(0, "", fakeClusterConfigBuilder.build(), null);

    when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
  }

  private void initClientGetTablesList() throws Exception {
    ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList = new ArrayList<>();
    // Adding table 1.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table1TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table1TableInfoBuilder.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    table1TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID1));
    table1TableInfoBuilder.setName(exampleTable1Name);
    table1TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table1TableInfoBuilder.build());
    // Adding table 2.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table2TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table2TableInfoBuilder.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    table2TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID2));
    table2TableInfoBuilder.setName(exampleTable2Name);
    table2TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table2TableInfoBuilder.build());

    when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
        .thenReturn(mockListTablesResponse);
    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
  }

  @Test
  public void testCreate() throws Exception {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initClientGetTablesList();

    BootstrapUniverseResponse mockBootstrapUniverseResponse =
        new BootstrapUniverseResponse(0, "", null, List.of(exampleStreamID2, exampleStreamID1));
    when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
    SetupUniverseReplicationResponse mockSetupResponse =
        new SetupUniverseReplicationResponse(0, "", null);
    when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
        .thenReturn(mockSetupResponse);
    IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
        new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
    when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
        .thenReturn(mockIsSetupDoneResponse);
    when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
        .thenAnswer(
            invocation -> {
              initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
              return mockIsSetupDoneResponse;
            });

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        getRequestedTableInfoListAndVerify(
                mockYBClient,
                createFormData.tables,
                createFormData.bootstrapParams,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    TaskInfo taskInfo = submitTask(xClusterConfig, requestedTableInfoList);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  private XClusterConfig setupDbScopedDrConfig() {
    createFormData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    createFormData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    createFormData.bootstrapParams.backupRequestParams.storageConfigUUID = UUID.randomUUID();
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);
    xClusterConfig.setType(ConfigType.Db);
    xClusterConfig.update();
    XClusterNamespaceConfig nsConfig = new XClusterNamespaceConfig();
    nsConfig.setConfig(xClusterConfig);
    nsConfig.setSourceNamespaceId(namespace1Id);
    nsConfig.setStatus(Validated);
    nsConfig.save();
    xClusterConfig.refresh();
    return xClusterConfig;
  }

  private MasterTypes.NamespaceIdentifierPB getNamespaceInfo() {
    ByteString ns1IdByteStr = ByteString.copyFromUtf8(namespace1Id.replaceAll("-", ""));
    return MasterTypes.NamespaceIdentifierPB.newBuilder()
        .setDatabaseType(YQL_DATABASE_PGSQL)
        .setId(ns1IdByteStr)
        .setName(namespace1Name)
        .build();
  }

  private MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder getTableInfoBuilder(
      MasterTypes.NamespaceIdentifierPB namespaceInfo) {
    return MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder()
        .setTableType(CommonTypes.TableType.PGSQL_TABLE_TYPE)
        .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()))
        .setName("table_1")
        .setRelationType(MasterTypes.RelationType.USER_TABLE_RELATION)
        .setNamespace(namespaceInfo);
  }

  private void runAndAssertDbScopedTask(
      XClusterConfig xClusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfo,
      MasterTypes.NamespaceIdentifierPB namespaceInfo)
      throws Exception {
    XClusterCreateOutboundReplicationGroupResponse outReplGroupResp =
        mock(XClusterCreateOutboundReplicationGroupResponse.class);
    lenient().when(outReplGroupResp.hasError()).thenReturn(false);
    IsXClusterBootstrapRequiredResponse completionResponse =
        mock(IsXClusterBootstrapRequiredResponse.class);
    when(completionResponse.isNotReady()).thenReturn(false);
    GetNamespaceInfoResponse gnir = mock(GetNamespaceInfoResponse.class);
    lenient().when(gnir.getNamespaceInfo()).thenReturn(namespaceInfo);
    GetXClusterOutboundReplicationGroupsResponse getOutReplGroupResp =
        mock(GetXClusterOutboundReplicationGroupsResponse.class);
    when(getOutReplGroupResp.hasError()).thenReturn(false);
    when(getOutReplGroupResp.getReplicationGroupIds()).thenReturn(List.of());
    CreateSnapshotScheduleResponse createPITRResponse = mock(CreateSnapshotScheduleResponse.class);
    UUID sourcePitrUUID = UUID.randomUUID();
    UUID targetPitrUUID = UUID.randomUUID();
    when(createPITRResponse.getSnapshotScheduleUUID())
        .thenReturn(sourcePitrUUID)
        .thenReturn(targetPitrUUID);
    ListSnapshotSchedulesResponse sourceListPITRResponse =
        mock(ListSnapshotSchedulesResponse.class);
    ListSnapshotSchedulesResponse targetListPITRResponse =
        mock(ListSnapshotSchedulesResponse.class);
    UUID snapshotUUID = UUID.randomUUID();
    SnapshotInfo snapshotInfo =
        new SnapshotInfo(snapshotUUID, 0L, 0L, CatalogEntityInfo.SysSnapshotEntryPB.State.COMPLETE);
    when(sourceListPITRResponse.getSnapshotScheduleInfoList())
        .thenReturn(
            List.of(
                new SnapshotScheduleInfo(
                    sourcePitrUUID,
                    0L,
                    0L,
                    List.of(snapshotInfo),
                    null,
                    null,
                    YQLDatabase.YQL_DATABASE_PGSQL)));
    when(targetListPITRResponse.getSnapshotScheduleInfoList())
        .thenReturn(
            List.of(
                new SnapshotScheduleInfo(
                    targetPitrUUID,
                    0L,
                    0L,
                    List.of(snapshotInfo),
                    null,
                    null,
                    YQLDatabase.YQL_DATABASE_PGSQL)));
    ListSnapshotsResponse listSnapshotsResponse = mock(ListSnapshotsResponse.class);
    when(listSnapshotsResponse.getSnapshotInfoList()).thenReturn(List.of(snapshotInfo));
    IsCreateXClusterReplicationDoneResponse isDoneResponse =
        mock(IsCreateXClusterReplicationDoneResponse.class);
    when(isDoneResponse.isDone()).thenReturn(true);

    lenient()
        .when(
            mockClient.xClusterCreateOutboundReplicationGroup(anyString(), anySet(), anyBoolean()))
        .thenReturn(outReplGroupResp);
    when(mockClient.isXClusterBootstrapRequired(anyString(), anyString()))
        .thenReturn(completionResponse);
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(new ShellResponse());
    when(mockClient.getNamespaceInfo(anyString(), eq(YQL_DATABASE_PGSQL))).thenReturn(gnir);
    when(mockClient.getXClusterOutboundReplicationGroups(anyString()))
        .thenReturn(getOutReplGroupResp);
    ShellResponse successShellResponse = new ShellResponse();
    successShellResponse.message = Json.newObject().toString();
    when(mockTableManagerYb.createBackup(any())).thenReturn(successShellResponse);
    when(restoreManagerYb.runCommand(any())).thenReturn(successShellResponse);
    when(mockClient.createSnapshotSchedule(
            eq(YQL_DATABASE_PGSQL), eq(namespace1Name), anyString(), anyLong(), anyLong()))
        .thenReturn(createPITRResponse);
    when(mockClient.listSnapshotSchedules(eq(sourcePitrUUID))).thenReturn(sourceListPITRResponse);
    when(mockClient.listSnapshotSchedules(eq(targetPitrUUID))).thenReturn(targetListPITRResponse);
    when(mockClient.listSnapshotSchedules(null)).thenReturn(sourceListPITRResponse);
    when(mockClient.listSnapshots(snapshotUUID, true)).thenReturn(listSnapshotsResponse);
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(successShellResponse);
    when(mockClient.isCreateXClusterReplicationDone(anyString(), anySet()))
        .thenReturn(isDoneResponse);

    TaskInfo taskInfo = submitTask(xClusterConfig, sourceTableInfo);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(
        1,
        taskInfo.getSubTasks().stream()
            .filter(ti -> ti.getTaskType() == TaskType.BackupTableYb)
            .count());
    assertEquals(
        1,
        taskInfo.getSubTasks().stream()
            .filter(ti -> ti.getTaskType() == TaskType.RestoreBackupYb)
            .count());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testCreateDbScopedWithSourceTableNotOnTarget() throws Exception {
    XClusterConfig xClusterConfig = setupDbScopedDrConfig();

    MasterTypes.NamespaceIdentifierPB namespaceInfo = getNamespaceInfo();
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder tiBuilder =
        getTableInfoBuilder(namespaceInfo);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfo = new ArrayList<>();
    sourceTableInfo.add(tiBuilder.build());

    ListTablesResponse sourceTableListResponse = mock(ListTablesResponse.class);
    when(sourceTableListResponse.getTableInfoList()).thenReturn(sourceTableInfo);
    ListTablesResponse targetTableListResponse = mock(ListTablesResponse.class);
    when(targetTableListResponse.getTableInfoList()).thenReturn(List.of());
    ListNamespacesResponse lnr = mock(ListNamespacesResponse.class);
    when(lnr.getNamespacesList()).thenReturn(List.of(namespaceInfo));
    when(mockClient.getNamespacesList()).thenReturn(lnr);
    when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
        .thenReturn(sourceTableListResponse)
        .thenReturn(targetTableListResponse)
        .thenReturn(sourceTableListResponse);

    runAndAssertDbScopedTask(xClusterConfig, sourceTableInfo, namespaceInfo);
  }

  @Test
  public void testCreateDbScopedWithTargetTableNotOnSource() throws Exception {
    XClusterConfig xClusterConfig = setupDbScopedDrConfig();

    MasterTypes.NamespaceIdentifierPB namespaceInfo = getNamespaceInfo();
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder tiBuilder =
        getTableInfoBuilder(namespaceInfo);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfo = new ArrayList<>();
    sourceTableInfo.add(tiBuilder.build());

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfo =
        new ArrayList<>(sourceTableInfo);
    tiBuilder.setName("table_2");
    targetTableInfo.add(tiBuilder.build());

    ListTablesResponse sourceTableListResponse = mock(ListTablesResponse.class);
    when(sourceTableListResponse.getTableInfoList()).thenReturn(sourceTableInfo);
    ListTablesResponse targetTableListResponse = mock(ListTablesResponse.class);
    when(targetTableListResponse.getTableInfoList()).thenReturn(targetTableInfo);
    ListNamespacesResponse lnr = mock(ListNamespacesResponse.class);
    when(lnr.getNamespacesList()).thenReturn(List.of(namespaceInfo));
    when(mockClient.getNamespacesList()).thenReturn(lnr);
    when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
        .thenReturn(sourceTableListResponse)
        .thenReturn(targetTableListResponse)
        .thenReturn(sourceTableListResponse);

    runAndAssertDbScopedTask(xClusterConfig, sourceTableInfo, namespaceInfo);
  }

  @Test
  public void testCreateHAEnabled() throws Exception {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initClientGetTablesList();
    HighAvailabilityConfig.create("test-cluster-key");

    BootstrapUniverseResponse mockBootstrapUniverseResponse =
        new BootstrapUniverseResponse(0, "", null, List.of(exampleStreamID2, exampleStreamID1));
    when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
    SetupUniverseReplicationResponse mockSetupResponse =
        new SetupUniverseReplicationResponse(0, "", null);
    when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
        .thenReturn(mockSetupResponse);
    IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
        new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
    when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
        .thenAnswer(
            invocation -> {
              initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
              return mockIsSetupDoneResponse;
            });

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        getRequestedTableInfoListAndVerify(
                mockYBClient,
                createFormData.tables,
                createFormData.bootstrapParams,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    TaskInfo taskInfo = submitTask(xClusterConfig, requestedTableInfoList);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testCreateXClusterSetupFailure() throws Exception {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initClientGetTablesList();

    String setupErrMsg = "failed to run setup rpc";

    BootstrapUniverseResponse mockBootstrapUniverseResponse =
        new BootstrapUniverseResponse(0, "", null, List.of(exampleStreamID2, exampleStreamID1));
    when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
    AppStatusPB.Builder appStatusBuilder =
        AppStatusPB.newBuilder().setMessage(setupErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
    MasterErrorPB.Builder masterErrorBuilder =
        MasterErrorPB.newBuilder().setStatus(appStatusBuilder.build()).setCode(Code.UNKNOWN_ERROR);
    SetupUniverseReplicationResponse mockSetupResponse =
        new SetupUniverseReplicationResponse(0, "", masterErrorBuilder.build());
    when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
        .thenAnswer(
            invocation -> {
              initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
              return mockSetupResponse;
            });

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        getRequestedTableInfoListAndVerify(
                mockYBClient,
                createFormData.tables,
                createFormData.bootstrapParams,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    TaskInfo taskInfo = submitTask(xClusterConfig, requestedTableInfoList);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    // Two FreeUniverse subtasks for source and target.
    assertEquals(TaskType.XClusterConfigSetup, taskInfo.getSubTasks().get(9).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(9).getErrorMessage();
    assertThat(taskErrMsg, containsString(setupErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testCreateXClusterIsSetupDoneFailure() throws Exception {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initClientGetTablesList();

    String isSetupDoneErrMsg = "failed to run setup rpc";

    BootstrapUniverseResponse mockBootstrapUniverseResponse =
        new BootstrapUniverseResponse(0, "", null, List.of(exampleStreamID2, exampleStreamID1));
    when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
    SetupUniverseReplicationResponse mockSetupResponse =
        new SetupUniverseReplicationResponse(0, "", null);
    when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
        .thenReturn(mockSetupResponse);

    AppStatusPB.Builder appStatusBuilder =
        AppStatusPB.newBuilder().setMessage(isSetupDoneErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
    IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
        new IsSetupUniverseReplicationDoneResponse(0, "", null, true, appStatusBuilder.build());
    when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
        .thenAnswer(
            invocation -> {
              initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
              return mockIsSetupDoneResponse;
            });

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        getRequestedTableInfoListAndVerify(
                mockYBClient,
                createFormData.tables,
                createFormData.bootstrapParams,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    TaskInfo taskInfo = submitTask(xClusterConfig, requestedTableInfoList);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    // Two FreeUniverse subtasks for source and target.
    assertEquals(TaskType.XClusterConfigSetup, taskInfo.getSubTasks().get(9).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(9).getErrorMessage();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) operation failed: code: %s\nmessage: \"%s\"",
            xClusterConfig.getUuid(), ErrorCode.UNKNOWN_ERROR, isSetupDoneErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  public static Pair<List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>, Set<String>>
      getRequestedTableInfoListAndVerify(
          YBClientService ybService,
          Set<String> requestedTableIds,
          @Nullable XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
          Universe sourceUniverse,
          Universe targetUniverse,
          @Nullable String currentReplicationGroupName,
          XClusterConfig.ConfigType configType) {
    // Ensure at least one table exists to verify.
    if (requestedTableIds.isEmpty()) {
      throw new IllegalArgumentException("requestedTableIds cannot be empty");
    }
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        getTableInfoList(ybService, sourceUniverse);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        filterTableInfoListByTableIds(sourceTableInfoList, requestedTableIds);

    CommonTypes.TableType tableType = getTableType(requestedTableInfoList);

    // Txn xCluster is supported only for YSQL tables.
    if (configType.equals(ConfigType.Txn)
        && !tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "Transaction xCluster is supported only for YSQL tables. Table type %s is selected",
              tableType));
    }

    // XCluster replication can be set up only for YCQL and YSQL tables.
    if (!tableType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
        && !tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "XCluster replication can be set up only for YCQL and YSQL tables: "
                  + "type %s requested",
              tableType));
    }

    // Make sure all the tables on the source universe have a corresponding table on the target
    // universe.
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTablesInfoList =
        getTableInfoList(ybService, targetUniverse);
    Map<String, String> sourceTableIdTargetTableIdMap =
        getSourceTableIdTargetTableIdMap(requestedTableInfoList, targetTablesInfoList);

    // If some tables do not exist on the target universe, bootstrapping is required.
    Set<String> sourceTableIdsWithNoTableOnTargetUniverse =
        sourceTableIdTargetTableIdMap.entrySet().stream()
            .filter(entry -> Objects.isNull(entry.getValue()))
            .map(Map.Entry::getKey)
            .collect(Collectors.toSet());
    if (!sourceTableIdsWithNoTableOnTargetUniverse.isEmpty()) {
      if (Objects.isNull(bootstrapParams)) {
        throw new IllegalArgumentException(
            String.format(
                "Table ids %s do not have corresponding tables on the target universe and "
                    + "they must be bootstrapped but bootstrapParams is null",
                sourceTableIdsWithNoTableOnTargetUniverse));
      }
      if (Objects.isNull(bootstrapParams.tables)
          || !bootstrapParams.tables.containsAll(sourceTableIdsWithNoTableOnTargetUniverse)) {
        throw new IllegalArgumentException(
            String.format(
                "Table ids %s do not have corresponding tables on the target universe and "
                    + "they must be bootstrapped but the set of tables in bootstrapParams (%s) "
                    + "does not contain all of them",
                sourceTableIdsWithNoTableOnTargetUniverse, bootstrapParams.tables));
      }
    }

    if (bootstrapParams != null && bootstrapParams.tables != null) {
      // Ensure tables in bootstrapParams is a subset of requestedTableIds.
      if (!bootstrapParams.allowBootstrap
          && !requestedTableIds.containsAll(bootstrapParams.tables)) {
        throw new IllegalArgumentException(
            String.format(
                "The set of tables in bootstrapParams (%s) is not a subset of "
                    + "requestedTableIds (%s)",
                bootstrapParams.tables, requestedTableIds));
      }

      // Bootstrapping must not be done for tables whose corresponding target table is in
      // replication. It also includes tables that are in reverse replication between the same
      // universes.
      Map<String, String> sourceTableIdTargetTableIdWithBootstrapMap =
          sourceTableIdTargetTableIdMap.entrySet().stream()
              .filter(entry -> bootstrapParams.tables.contains(entry.getKey()))
              .collect(
                  HashMap::new,
                  (map, entry) -> map.put(entry.getKey(), entry.getValue()),
                  HashMap::putAll);

      if (!bootstrapParams.allowBootstrap) {
        bootstrapParams.tables =
            getTableIdsWithoutTablesOnTargetInReplication(
                ybService,
                requestedTableInfoList,
                sourceTableIdTargetTableIdWithBootstrapMap,
                targetUniverse,
                currentReplicationGroupName);
      }

      // If table type is YSQL and bootstrap is requested, all tables in that keyspace are selected.
      if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
        groupByNamespaceId(requestedTableInfoList)
            .forEach(
                (namespaceId, tablesInfoList) -> {
                  Set<String> selectedTableIdsInNamespaceToBootstrap =
                      getTableIds(tablesInfoList).stream()
                          .filter(bootstrapParams.tables::contains)
                          .collect(Collectors.toSet());
                  if (!selectedTableIdsInNamespaceToBootstrap.isEmpty()) {
                    Set<String> tableIdsInNamespace =
                        sourceTableInfoList.stream()
                            .filter(
                                tableInfo ->
                                    !tableInfo
                                            .getRelationType()
                                            .equals(MasterTypes.RelationType.SYSTEM_TABLE_RELATION)
                                        && tableInfo
                                            .getNamespace()
                                            .getId()
                                            .toStringUtf8()
                                            .equals(namespaceId))
                            .map(tableInfo -> tableInfo.getId().toStringUtf8())
                            .collect(Collectors.toSet());
                    if (!bootstrapParams.allowBootstrap
                        && tableIdsInNamespace.size()
                            != selectedTableIdsInNamespaceToBootstrap.size()) {
                      throw new IllegalArgumentException(
                          String.format(
                              "For YSQL tables, all the tables in a keyspace must be selected: "
                                  + "selected: %s, tables in the keyspace: %s",
                              selectedTableIdsInNamespaceToBootstrap, tableIdsInNamespace));
                    }
                  }
                });
      }
    }

    return new Pair<>(requestedTableInfoList, sourceTableIdsWithNoTableOnTargetUniverse);
  }
}
