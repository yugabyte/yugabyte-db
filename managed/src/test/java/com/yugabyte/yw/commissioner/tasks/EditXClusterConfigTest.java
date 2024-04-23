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
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
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
import com.yugabyte.yw.models.XClusterTableConfig;
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
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes;
import org.yb.Schema;
import org.yb.WireProtocol;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.BootstrapUniverseResponse;
import org.yb.client.GetAutoFlagsConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class EditXClusterConfigTest extends CommissionerBaseTest {

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
  private String exampleTableID3;
  private String exampleStreamID1;
  private String exampleStreamID2;
  private String exampleStreamID3;
  private String exampleTable1Name;
  private String exampleTable2Name;
  private String exampleTable3Name;
  private String namespace1Name;
  private String namespace1Id;
  private Set<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  List<TaskType> RENAME_FAILURE_TASK_SEQUENCE =
      ImmutableList.of(
          // Freeze for source.
          TaskType.FreezeUniverse,
          // Freeze for target.
          TaskType.FreezeUniverse,
          TaskType.XClusterConfigSetStatus,
          TaskType.XClusterConfigRename,
          TaskType.XClusterConfigSetStatus,
          TaskType.UniverseUpdateSucceeded,
          TaskType.UniverseUpdateSucceeded);

  List<TaskType> ADD_TABLE_IS_ALTER_DONE_FAILURE =
      ImmutableList.of(
          // Freeze for source.
          TaskType.FreezeUniverse,
          // Freeze for target.
          TaskType.FreezeUniverse,
          TaskType.XClusterConfigSetStatus,
          TaskType.XClusterConfigSetStatusForTables,
          TaskType.BootstrapProducer,
          TaskType.XClusterConfigModifyTables,
          TaskType.XClusterConfigSetStatus,
          TaskType.UniverseUpdateSucceeded,
          TaskType.UniverseUpdateSucceeded);

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("EditXClusterConfig-test-customer");
    defaultUser = ModelFactory.testUser(defaultCustomer);
    configName = "EditXClusterConfigTest-test-config";

    sourceUniverseName = "EditXClusterConfig-test-universe-1";
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

    targetUniverseName = "EditXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    namespace1Name = "ycql-namespace1";
    namespace1Id = UUID.randomUUID().toString();
    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";
    exampleTableID3 = "000030af000030008000000000004002";

    exampleTable1Name = "exampleTable1";
    exampleTable2Name = "exampleTable2";
    exampleTable3Name = "exampleTable3";

    exampleStreamID1 = "ec10532900ef42a29a6899c82dd7404f";
    exampleStreamID2 = "ec10532900ef42a29a6899c82dd7404d";
    exampleStreamID3 = "fea203ffca1f48349901e0de2b52c416";

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
            exampleTable1Name,
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
            exampleTable2Name,
            exampleTableID2,
            null,
            true,
            CommonTypes.TableType.YQL_TABLE_TYPE,
            Collections.emptyList(),
            false);
    GetTableSchemaResponse mockTableSchemaResponseTable3 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            exampleTable3Name,
            exampleTableID3,
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
          .when(mockClient.getTableSchemaByUUID(exampleTableID3))
          .thenReturn(mockTableSchemaResponseTable3);
    } catch (Exception ignored) {
    }
  }

  private TaskInfo submitTask(
      XClusterConfig xClusterConfig,
      XClusterConfigEditFormData editFormData,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList,
      Set<String> tableIdsToRemove) {
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(
            xClusterConfig,
            editFormData,
            requestedTableToAddInfoList,
            Collections.emptyMap(),
            XClusterConfigTaskBase.getTableIds(requestedTableToAddInfoList),
            Collections.emptyMap(),
            tableIdsToRemove);
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, taskParams);

      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          TargetType.XClusterConfig,
          CustomerTask.TaskType.Edit,
          xClusterConfig.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
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
    // Adding table 3.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table3TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table3TableInfoBuilder.setTableType(CommonTypes.TableType.YQL_TABLE_TYPE);
    table3TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID3));
    table3TableInfoBuilder.setName(exampleTable3Name);
    table3TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table3TableInfoBuilder.build());

    try {
      when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
      when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
          .thenReturn(mockListTablesResponse);
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

    MetricQueryResponse.Entry entryExampleTableID3 = new MetricQueryResponse.Entry();
    entryExampleTableID3.labels = new HashMap<>();
    entryExampleTableID3.labels.put("table_id", exampleTableID3);
    entryExampleTableID3.values = new ArrayList<>();
    entryExampleTableID3.values.add(ImmutablePair.of(10.0, 0.0));
    metricValues.add(entryExampleTableID3);

    when(mockMetricQueryHelper.queryDirect(any())).thenReturn(metricValues);
  }

  @Test
  public void testRename() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.getSourceUniverseUUID() + "_" + newName;

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(), newFullName))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newName, xClusterConfig.getName());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testRenameHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.getSourceUniverseUUID() + "_" + newName;

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(), newFullName))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newName, xClusterConfig.getName());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testRenameFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.getSourceUniverseUUID() + "_" + newName;
    String renameErrMsg = "failed to run rename rpc";
    try {
      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(renameErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", masterErrorBuilder.build());
      when(mockClient.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(), newFullName))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(RENAME_FAILURE_TASK_SEQUENCE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < RENAME_FAILURE_TASK_SEQUENCE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(RENAME_FAILURE_TASK_SEQUENCE.get(i), subtaskGroup.getTaskType());
    }

    String taskErrMsg = taskInfo.getSubTasks().get(3).getDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to rename XClusterConfig(%s): %s", xClusterConfig.getUuid(), renameErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testPause() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    try {
      SetUniverseReplicationEnabledResponse mockEditResponse =
          new SetUniverseReplicationEnabledResponse(0, "", null);
      when(mockClient.setUniverseReplicationEnabled(
              xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertTrue(xClusterConfig.isPaused());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testPauseHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");

    try {
      SetUniverseReplicationEnabledResponse mockEditResponse =
          new SetUniverseReplicationEnabledResponse(0, "", null);
      when(mockClient.setUniverseReplicationEnabled(
              xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertTrue(xClusterConfig.isPaused());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testResume() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    xClusterConfig.disable();

    try {
      SetUniverseReplicationEnabledResponse mockEditResponse =
          new SetUniverseReplicationEnabledResponse(0, "", null);
      when(mockClient.setUniverseReplicationEnabled(xClusterConfig.getReplicationGroupName(), true))
          .thenReturn(mockEditResponse);
      WireProtocol.PromotedFlagsPerProcessPB masterFlagPB =
          WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
              .addFlags("FLAG_1")
              .setProcessName("yb-master")
              .build();
      WireProtocol.PromotedFlagsPerProcessPB tserverFlagPB =
          WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
              .addFlags("FLAG_1")
              .setProcessName("yb-tserver")
              .build();
      WireProtocol.AutoFlagsConfigPB config =
          MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
              .getConfigBuilder()
              .addPromotedFlags(masterFlagPB)
              .addPromotedFlags(tserverFlagPB)
              .setConfigVersion(1)
              .build();
      MasterClusterOuterClass.GetAutoFlagsConfigResponsePB responsePB =
          MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
              .setConfig(config)
              .build();
      GetAutoFlagsConfigResponse resp = new GetAutoFlagsConfigResponse(0, null, responsePB);
      lenient().when(mockClient.autoFlagsConfig()).thenReturn(resp);
      GFlagsValidation.AutoFlagDetails autoFlagDetails = new GFlagsValidation.AutoFlagDetails();
      autoFlagDetails.name = "FLAG_1";
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer.autoFlagDetails = Collections.singletonList(autoFlagDetails);
      when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(autoFlagsPerServer);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Running";
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertFalse(xClusterConfig.isPaused());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testResumeHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    xClusterConfig.disable();

    HighAvailabilityConfig.create("test-cluster-key");

    try {
      SetUniverseReplicationEnabledResponse mockEditResponse =
          new SetUniverseReplicationEnabledResponse(0, "", null);
      when(mockClient.setUniverseReplicationEnabled(xClusterConfig.getReplicationGroupName(), true))
          .thenReturn(mockEditResponse);
      WireProtocol.PromotedFlagsPerProcessPB masterFlagPB =
          WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
              .addFlags("FLAG_1")
              .setProcessName("yb-master")
              .build();
      WireProtocol.PromotedFlagsPerProcessPB tserverFlagPB =
          WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
              .addFlags("FLAG_1")
              .setProcessName("yb-tserver")
              .build();
      WireProtocol.AutoFlagsConfigPB config =
          MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
              .getConfigBuilder()
              .addPromotedFlags(masterFlagPB)
              .addPromotedFlags(tserverFlagPB)
              .setConfigVersion(1)
              .build();
      MasterClusterOuterClass.GetAutoFlagsConfigResponsePB responsePB =
          MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
              .setConfig(config)
              .build();
      GetAutoFlagsConfigResponse resp = new GetAutoFlagsConfigResponse(0, null, responsePB);
      lenient().when(mockClient.autoFlagsConfig()).thenReturn(resp);
      GFlagsValidation.AutoFlagDetails autoFlagDetails = new GFlagsValidation.AutoFlagDetails();
      autoFlagDetails.name = "FLAG_1";
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer.autoFlagDetails = Collections.singletonList(autoFlagDetails);
      when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(autoFlagsPerServer);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Running";
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertFalse(xClusterConfig.isPaused());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testPauseResumeFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String pauseResumeErrMsg = "failed to pause/enable replication";

    try {
      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(pauseResumeErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      SetUniverseReplicationEnabledResponse mockEditResponse =
          new SetUniverseReplicationEnabledResponse(0, "", masterErrorBuilder.build());
      when(mockClient.setUniverseReplicationEnabled(
              xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    TaskInfo taskInfo =
        submitTask(xClusterConfig, editFormData, Collections.emptyList(), Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.SetReplicationPaused, taskInfo.getSubTasks().get(3).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(3).getDetails().get("errorString").asText();
    assertThat(taskErrMsg, containsString("Failed to pause/enable XClusterConfig"));
    assertThat(taskErrMsg, containsString(pauseResumeErrMsg));
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  public void initTargetUniverseClusterConfig(String replicationGroupName, int numberOfTables) {
    CdcConsumer.ProducerEntryPB.Builder fakeProducerEntry =
        CdcConsumer.ProducerEntryPB.newBuilder();
    switch (numberOfTables) {
      case 3:
        CdcConsumer.StreamEntryPB.Builder fakeStreamEntry3 =
            CdcConsumer.StreamEntryPB.newBuilder().setProducerTableId(exampleTableID3);
        fakeProducerEntry.putStreamMap(exampleStreamID3, fakeStreamEntry3.build());
        // Intentional fall-through.
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
    } catch (Exception ignore) {
    }
  }

  @Test
  public void testAddTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(0, "", null, ImmutableList.of(exampleStreamID3));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);

      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, exampleStreamID3)))
          .thenReturn(mockEditResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    xClusterConfig.addTables(Collections.singleton(exampleTableID3));

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                mockYBClient,
                Collections.singleton(exampleTableID3),
                null,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig, editFormData, requestedTableToAddInfoList, Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());
    xClusterConfig
        .getTables()
        .forEach(tableConfig -> assertTrue(tableConfig.isReplicationSetupDone()));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddTablesHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");
    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(0, "", null, ImmutableList.of(exampleStreamID3));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);

      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, exampleStreamID3)))
          .thenReturn(mockEditResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    xClusterConfig.addTables(Collections.singleton(exampleTableID3));

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                mockYBClient,
                Collections.singleton(exampleTableID3),
                null,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig, editFormData, requestedTableToAddInfoList, Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());
    xClusterConfig
        .getTables()
        .forEach(tableConfig -> assertTrue(tableConfig.isReplicationSetupDone()));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(3, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddTablesAlterFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();
    String alterErrMsg = "failed to modify tables";
    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(0, "", null, ImmutableList.of(exampleStreamID3));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);

      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(alterErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", masterErrorBuilder.build());
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, exampleStreamID3)))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    xClusterConfig.addTables(Collections.singleton(exampleTableID3));

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                mockYBClient,
                Collections.singleton(exampleTableID3),
                null,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig, editFormData, requestedTableToAddInfoList, Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < ADD_TABLE_IS_ALTER_DONE_FAILURE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.get(i), subtaskGroup.getTaskType());
    }

    String taskErrMsg = taskInfo.getSubTasks().get(5).getDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to add tables to XClusterConfig(%s): %s",
            xClusterConfig.getUuid(), alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());
    Optional<XClusterTableConfig> table3Config = xClusterConfig.maybeGetTableById(exampleTableID3);
    assertTrue(table3Config.isPresent());
    assertFalse(table3Config.get().isReplicationSetupDone());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddTablesIsAlterDoneFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();

    String alterErrMsg = "failed to modify tables";
    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(0, "", null, ImmutableList.of(exampleStreamID3));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);

      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, exampleStreamID3)))
          .thenReturn(mockEditResponse);

      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(alterErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, appStatusBuilder.build());
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    xClusterConfig.addTables(Collections.singleton(exampleTableID3));

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                mockYBClient,
                Collections.singleton(exampleTableID3),
                null,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig, editFormData, requestedTableToAddInfoList, Collections.emptySet());
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < ADD_TABLE_IS_ALTER_DONE_FAILURE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.get(i), subtaskGroup.getTaskType());
    }

    String taskErrMsg = taskInfo.getSubTasks().get(5).getDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) operation failed: code: %s\nmessage: \"%s\"",
            xClusterConfig.getUuid(), ErrorCode.UNKNOWN_ERROR, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());
    Optional<XClusterTableConfig> table3Config = xClusterConfig.maybeGetTableById(exampleTableID3);
    assertTrue(table3Config.isPresent());
    assertFalse(table3Config.get().isReplicationSetupDone());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testRemoveTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singleton(exampleTableID2),
              false))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig,
            editFormData,
            Collections.emptyList(),
            Collections.singleton(exampleTableID2));
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testRemoveTablesHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");
    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singleton(exampleTableID2),
              false))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig,
            editFormData,
            Collections.emptyList(),
            Collections.singleton(exampleTableID2));
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testRemoveTablesAlterFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    String alterErrMsg = "failed to modify tables";
    try {
      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(alterErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", masterErrorBuilder.build());
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singleton(exampleTableID2),
              false))
          .thenReturn(mockEditResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig,
            editFormData,
            Collections.emptyList(),
            Collections.singleton(exampleTableID2));
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigModifyTables, taskInfo.getSubTasks().get(4).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(4).getDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to remove tables from XClusterConfig(%s): %s",
            xClusterConfig.getUuid(), alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(exampleTables, xClusterConfig.getTableIds());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddRemoveTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(0, "", null, ImmutableList.of(exampleStreamID3));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);

      AlterUniverseReplicationResponse mockAddResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, exampleStreamID3)))
          .thenReturn(mockAddResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);

      AlterUniverseReplicationResponse mockRemoveResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singleton(exampleTableID2),
              false))
          .thenReturn(mockRemoveResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID3);

    xClusterConfig.addTables(Collections.singleton(exampleTableID3));

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                mockYBClient,
                Collections.singleton(exampleTableID3),
                null,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig,
            editFormData,
            requestedTableToAddInfoList,
            Collections.singleton(exampleTableID2));
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());
    xClusterConfig
        .getTables()
        .forEach(tableConfig -> assertTrue(tableConfig.isReplicationSetupDone()));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddRemoveTablesHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");
    initClientGetTablesList();
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      BootstrapUniverseResponse mockBootstrapUniverseResponse =
          new BootstrapUniverseResponse(0, "", null, ImmutableList.of(exampleStreamID3));
      when(mockClient.bootstrapUniverse(any(), any())).thenReturn(mockBootstrapUniverseResponse);

      AlterUniverseReplicationResponse mockAddResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, exampleStreamID3)))
          .thenReturn(mockAddResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);

      AlterUniverseReplicationResponse mockRemoveResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singleton(exampleTableID2),
              false))
          .thenReturn(mockRemoveResponse);
    } catch (Exception ignore) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID3);

    xClusterConfig.addTables(Collections.singleton(exampleTableID3));

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                mockYBClient,
                Collections.singleton(exampleTableID3),
                null,
                sourceUniverse,
                targetUniverse,
                null,
                ConfigType.Basic)
            .getFirst();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo =
        submitTask(
            xClusterConfig,
            editFormData,
            requestedTableToAddInfoList,
            Collections.singleton(exampleTableID2));
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.getStatus());
    assertEquals(newTables, xClusterConfig.getTableIds());
    xClusterConfig
        .getTables()
        .forEach(tableConfig -> assertTrue(tableConfig.isReplicationSetupDone()));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(4, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }
}
