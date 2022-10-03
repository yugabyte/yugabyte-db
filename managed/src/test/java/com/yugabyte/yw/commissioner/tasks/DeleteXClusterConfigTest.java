// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

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
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class DeleteXClusterConfigTest extends CommissionerBaseTest {

  private String configName;
  private String sourceUniverseName;
  private UUID sourceUniverseUUID;
  private Universe sourceUniverse;
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
          TaskType.XClusterConfigSetStatus,
          TaskType.DeleteReplication,
          TaskType.DeleteBootstrapIds,
          TaskType.DeleteXClusterConfigEntry,
          TaskType.UniverseUpdateSucceeded,
          TaskType.UniverseUpdateSucceeded);

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("DeleteXClusterConfig-test-customer");

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
  }

  private TaskInfo submitTask(XClusterConfig xClusterConfig) {
    XClusterConfigTaskParams taskParams = new XClusterConfigTaskParams(xClusterConfig);
    try {
      UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, taskParams);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.universeUUID,
          taskUUID,
          TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.name);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  public void initTargetUniverseClusterConfig(String replicationGroupName) {
    CdcConsumer.ProducerEntryPB.Builder fakeProducerEntry =
        CdcConsumer.ProducerEntryPB.newBuilder();
    CdcConsumer.StreamEntryPB.Builder fakeStreamEntry2 =
        CdcConsumer.StreamEntryPB.newBuilder().setProducerTableId(exampleTableID2);
    fakeProducerEntry.putStreamMap(exampleStreamID2, fakeStreamEntry2.build());
    CdcConsumer.StreamEntryPB.Builder fakeStreamEntry1 =
        CdcConsumer.StreamEntryPB.newBuilder().setProducerTableId(exampleTableID1);
    fakeProducerEntry.putStreamMap(exampleStreamID1, fakeStreamEntry1.build());

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

  @Test
  public void testDelete() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName());

    try {
      DeleteUniverseReplicationResponse mockDeleteResponse =
          new DeleteUniverseReplicationResponse(0, "", null, null);
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), true))
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

    assertFalse(XClusterConfig.maybeGet(xClusterConfig.uuid).isPresent());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);
  }

  @Test
  public void testDeleteHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName());
    HighAvailabilityConfig.create("test-cluster-key");

    try {
      DeleteUniverseReplicationResponse mockDeleteResponse =
          new DeleteUniverseReplicationResponse(0, "", null, null);
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), true))
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

    assertFalse(XClusterConfig.maybeGet(xClusterConfig.uuid).isPresent());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.version);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);
  }

  @Test
  public void testDeleteXClusterFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName());
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
      when(mockClient.deleteUniverseReplication(xClusterConfig.getReplicationGroupName(), true))
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
    String taskErrMsg = taskInfo.getSubTasks().get(1).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to delete replication for XClusterConfig(%s): %s",
            xClusterConfig.uuid, deleteErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.DeletionFailed, xClusterConfig.status);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }
}
