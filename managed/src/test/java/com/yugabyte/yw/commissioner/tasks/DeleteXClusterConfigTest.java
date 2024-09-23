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
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
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
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
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
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class DeleteXClusterConfigTest extends CommissionerBaseTest {

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
  private Set<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  List<TaskType> DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.FreezeUniverse,
          TaskType.XClusterConfigSetStatus,
          TaskType.DeleteReplication,
          TaskType.DeleteBootstrapIds,
          TaskType.DeleteXClusterConfigEntry,
          TaskType.PromoteAutoFlags,
          TaskType.PromoteAutoFlags,
          TaskType.UniverseUpdateSucceeded,
          TaskType.UniverseUpdateSucceeded);

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("DeleteXClusterConfig-test-customer");
    defaultUser = ModelFactory.testUser(defaultCustomer);

    configName = "DeleteXClusterConfigTest-test-config";

    sourceUniverseName = "DeleteXClusterConfig-test-universe-1";
    sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse(sourceUniverseName, sourceUniverseUUID);

    targetUniverseName = "DeleteXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";

    exampleStreamID1 = "ec10532900ef42a29a6899c82dd7404f";
    exampleStreamID2 = "ec10532900ef42a29a6899c82dd7404d";

    exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);
    exampleTables.add(exampleTableID2);

    createFormData = new XClusterConfigCreateFormData();
    createFormData.name = configName;
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverseUUID;
    createFormData.tables = exampleTables;

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);
    try {
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer.autoFlagDetails = new ArrayList<>();
      when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(autoFlagsPerServer);
      when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0,
                  "uuid",
                  MasterClusterOuterClass.PromoteAutoFlagsResponsePB.getDefaultInstance()));
    } catch (Exception ignored) {
      fail();
    }
  }

  private TaskInfo submitTask(XClusterConfig xClusterConfig) {
    XClusterConfigTaskParams taskParams = new XClusterConfigTaskParams(xClusterConfig);
    try {
      UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, taskParams);

      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
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
  public void testDelete() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    try {
      DeleteUniverseReplicationResponse mockDeleteResponse =
          new DeleteUniverseReplicationResponse(0, "", null, null);
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockDeleteResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.get(i), subtaskGroup.getTaskType());
    }

    assertFalse(XClusterConfig.maybeGet(xClusterConfig.getUuid()).isPresent());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);
  }

  @Test
  public void testDeleteWithPromoteAutoFlagsOnSelectiveUniverse() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);
    TestHelper.updateUniverseVersion(targetUniverse, "2.14.0.0-b1");

    try {
      DeleteUniverseReplicationResponse mockDeleteResponse =
          new DeleteUniverseReplicationResponse(0, "", null, null);
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockDeleteResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskType> taskSequence = new ArrayList<>(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE);
    taskSequence.remove(taskSequence.indexOf(TaskType.PromoteAutoFlags));
    assertEquals(taskSequence.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < taskSequence.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(taskSequence.get(i), subtaskGroup.getTaskType());
    }

    assertFalse(XClusterConfig.maybeGet(xClusterConfig.getUuid()).isPresent());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);
  }

  @Test
  public void testDeleteHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    HighAvailabilityConfig.create("test-cluster-key");

    try {
      DeleteUniverseReplicationResponse mockDeleteResponse =
          new DeleteUniverseReplicationResponse(0, "", null, null);
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockDeleteResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.get(i), subtaskGroup.getTaskType());
    }

    assertFalse(XClusterConfig.maybeGet(xClusterConfig.getUuid()).isPresent());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);
  }

  @Test
  public void testDeleteXClusterFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    String deleteErrMsg = "failed to run delete rpc";
    try {
      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(deleteErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      DeleteUniverseReplicationResponse mockSetupResponse =
          new DeleteUniverseReplicationResponse(0, "", masterErrorBuilder.build(), null);
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), false))
          .thenReturn(mockSetupResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(DELETE_XCLUSTER_CONFIG_TASK_SEQUENCE.get(i), subtaskGroup.getTaskType());
    }
    String taskErrMsg = taskInfo.getSubTasks().get(3).getErrorMessage();
    String expectedErrMsg =
        String.format(
            "Failed to delete replication for XClusterConfig(%s): %s",
            xClusterConfig.getUuid(), deleteErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.DeletionFailed, xClusterConfig.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }
}
