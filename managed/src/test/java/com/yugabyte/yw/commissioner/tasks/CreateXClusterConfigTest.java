// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AlertTemplate.REPLICATION_LAG;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
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
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes;
import org.yb.Schema;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.BootstrapUniverseResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.SetupUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class CreateXClusterConfigTest extends CommissionerBaseTest {

  private String configName;
  private String sourceUniverseName;
  private UUID sourceUniverseUUID;
  private Universe sourceUniverse;
  private Users defaultUser;
  private String targetUniverseName;
  private UUID targetUniverseUUID;
  private Universe targetUniverse;
  private String exampleTableID1;
  private String exampleTableID2;
  private String exampleStreamID1;
  private String exampleStreamID2;
  private String exampleTable1Name;
  private String exampleTable2Name;
  private String namespace1Name;
  private String namespace2Name;
  private String namespace1Id;
  private String namespace2Id;
  private Set<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("CreateXClusterConfig-test-customer");
    defaultUser = ModelFactory.testUser(defaultCustomer);

    configName = "CreateXClusterConfigTest-test-config";

    sourceUniverseName = "CreateXClusterConfig-test-universe-1";
    sourceUniverseUUID = UUID.randomUUID();
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

    targetUniverseName = "CreateXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    namespace1Name = "ycql-namespace1";
    namespace2Name = "ycql-namespace2";
    namespace1Id = UUID.randomUUID().toString();
    namespace2Id = UUID.randomUUID().toString();

    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";

    exampleTable1Name = "exampleTable1";
    exampleTable2Name = "exampleTable2";

    exampleStreamID1 = "fea203ffca1f48349901e0de2b52c416";
    exampleStreamID2 = "ec10532900ef42a29a6899c82dd7404d";

    exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);
    exampleTables.add(exampleTableID2);

    createFormData = new XClusterConfigCreateFormData();
    createFormData.name = configName;
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverseUUID;
    createFormData.tables = exampleTables;

    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    GetTableSchemaResponse mockTableSchemaResponseTable1 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            "exampleTableID1",
            exampleTableID1,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            Collections.emptyList(),
            false);
    GetTableSchemaResponse mockTableSchemaResponseTable2 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            "exampleTableID2",
            exampleTableID2,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            Collections.emptyList(),
            false);
    try {
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
    } catch (Exception ignored) {
    }
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

  public void initTargetUniverseClusterConfig(String replicationGroupName, int numberOfTables) {
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

    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception e) {
    }
  }

  public void initClientGetTablesList() {
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

    try {
      when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
          .thenReturn(mockListTablesResponse);
      when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  public void setupAlertConfigurations() {
    AlertConfiguration alertConfiguration =
        alertConfigurationService
            .createConfigurationTemplate(defaultCustomer, REPLICATION_LAG)
            .getDefaultConfiguration();
    alertConfiguration.setDefaultDestination(true);
    alertConfiguration.setCreateTime(new Date());
    alertConfiguration.generateUUID();
    alertConfiguration.save();

    lenient()
        .doReturn(Collections.singletonList(alertConfiguration))
        .when(alertConfigurationService)
        .list(any());
  }

  public void setupMetricValues() {
    ArrayList<MetricQueryResponse.Entry> metricValues = new ArrayList<>();
    MetricQueryResponse.Entry entryExampleTableID1 = new MetricQueryResponse.Entry();
    entryExampleTableID1.labels = new HashMap<>();
    entryExampleTableID1.labels.put("table_id", exampleTableID1);
    entryExampleTableID1.values = new ArrayList<>();
    entryExampleTableID1.values.add(ImmutablePair.of(10.0, 0.0));
    metricValues.add(entryExampleTableID1);

    MetricQueryResponse.Entry entryExampleTableID2 = new MetricQueryResponse.Entry();
    entryExampleTableID2.labels = new HashMap<>();
    entryExampleTableID2.labels.put("table_id", exampleTableID2);
    entryExampleTableID2.values = new ArrayList<>();
    entryExampleTableID2.values.add(ImmutablePair.of(10.0, 0.0));
    metricValues.add(entryExampleTableID2);

    when(mockMetricQueryHelper.queryDirect(any())).thenReturn(metricValues);
  }

  @Test
  public void testCreate() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
    initClientGetTablesList();

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(
              0, "", null, ImmutableList.of(exampleStreamID2, exampleStreamID1));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", null);
      when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
          .thenReturn(mockSetupResponse);
      IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsSetupDoneResponse);
    } catch (Exception ignore) {
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
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

  @Test
  public void testCreateHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
    initClientGetTablesList();
    HighAvailabilityConfig.create("test-cluster-key");

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(
              0, "", null, ImmutableList.of(exampleStreamID2, exampleStreamID1));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", null);
      when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
          .thenReturn(mockSetupResponse);
      IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsSetupDoneResponse);
    } catch (Exception ignore) {
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
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
  public void testCreateXClusterSetupFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
    initClientGetTablesList();

    String setupErrMsg = "failed to run setup rpc";

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(
              0, "", null, ImmutableList.of(exampleStreamID2, exampleStreamID1));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);
      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(setupErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", masterErrorBuilder.build());
      when(mockClient.setupUniverseReplication(any(), any(), any(), any()))
          .thenReturn(mockSetupResponse);
    } catch (Exception ignore) {
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
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
    assertEquals(TaskType.XClusterConfigSetup, taskInfo.getSubTasks().get(6).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(6).getDetails().get("errorString").asText();
    assertThat(taskErrMsg, containsString(setupErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testCreateXClusterIsSetupDoneFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Initialized);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
    initClientGetTablesList();

    String isSetupDoneErrMsg = "failed to run setup rpc";

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(
              0, "", null, ImmutableList.of(exampleStreamID2, exampleStreamID1));
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
          .thenReturn(mockIsSetupDoneResponse);
    } catch (Exception ignore) {
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
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
    assertEquals(TaskType.XClusterConfigSetup, taskInfo.getSubTasks().get(6).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(6).getDetails().get("errorString").asText();
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
}
