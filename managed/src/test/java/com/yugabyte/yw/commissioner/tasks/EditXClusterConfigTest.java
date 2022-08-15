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
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class EditXClusterConfigTest extends CommissionerBaseTest {

  private String configName;
  private String sourceUniverseName;
  private UUID sourceUniverseUUID;
  private Universe sourceUniverse;
  private String targetUniverseName;
  private UUID targetUniverseUUID;
  private Universe targetUniverse;
  private String exampleTableID1;
  private String exampleTableID2;
  private String exampleTableID3;
  private String exampleStreamID1;
  private String exampleStreamID2;
  private String exampleStreamID3;
  private Set<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  List<TaskType> RENAME_FAILURE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.XClusterConfigSetStatus,
          TaskType.XClusterConfigRename,
          TaskType.XClusterConfigSetStatus,
          TaskType.UniverseUpdateSucceeded,
          TaskType.UniverseUpdateSucceeded);

  List<TaskType> ADD_TABLE_IS_ALTER_DONE_FAILURE =
      ImmutableList.of(
          TaskType.XClusterConfigSetStatus,
          TaskType.XClusterConfigModifyTables,
          TaskType.XClusterConfigSetStatus,
          TaskType.UniverseUpdateSucceeded,
          TaskType.UniverseUpdateSucceeded);

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("EditXClusterConfig-test-customer");

    configName = "EditXClusterConfigTest-test-config";

    sourceUniverseName = "EditXClusterConfig-test-universe-1";
    sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse(sourceUniverseName, sourceUniverseUUID);

    targetUniverseName = "EditXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";
    exampleTableID3 = "000030af000030008000000000004002";

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

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);
  }

  private TaskInfo submitTask(
      XClusterConfig xClusterConfig, XClusterConfigEditFormData editFormData) {
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(xClusterConfig, editFormData);
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, taskParams);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.universeUUID,
          taskUUID,
          TargetType.XClusterConfig,
          CustomerTask.TaskType.Edit,
          xClusterConfig.name);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testRename() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.sourceUniverseUUID + "_" + newName;

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(), newFullName))
          .thenReturn(mockEditResponse);
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newName, xClusterConfig.name);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    String newFullName = xClusterConfig.sourceUniverseUUID + "_" + newName;

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(), newFullName))
          .thenReturn(mockEditResponse);
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newName, xClusterConfig.name);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    String newFullName = xClusterConfig.sourceUniverseUUID + "_" + newName;
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
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(RENAME_FAILURE_TASK_SEQUENCE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < RENAME_FAILURE_TASK_SEQUENCE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(RENAME_FAILURE_TASK_SEQUENCE.get(i), subtaskGroup.getTaskType());
    }

    String taskErrMsg = taskInfo.getSubTasks().get(1).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format("Failed to rename XClusterConfig(%s): %s", xClusterConfig.uuid, renameErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);

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
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertTrue(xClusterConfig.paused);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertTrue(xClusterConfig.paused);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.version);
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
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Running";
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertFalse(xClusterConfig.paused);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Running";
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertFalse(xClusterConfig.paused);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.version);
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
    } catch (Exception e) {
    }

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.SetReplicationPaused, taskInfo.getSubTasks().get(1).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(1).getTaskDetails().get("errorString").asText();
    assertThat(taskErrMsg, containsString("Failed to pause/enable XClusterConfig"));
    assertThat(taskErrMsg, containsString(pauseResumeErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);

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
    } catch (Exception e) {
    }
  }

  @Test
  public void testAddTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, null)))
          .thenReturn(mockEditResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());
    xClusterConfig.tables.forEach(tableConfig -> assertTrue(tableConfig.replicationSetupDone));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, null)))
          .thenReturn(mockEditResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());
    xClusterConfig.tables.forEach(tableConfig -> assertTrue(tableConfig.replicationSetupDone));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(3, targetUniverse.version);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddTablesAlterFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

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
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, null)))
          .thenReturn(mockEditResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < ADD_TABLE_IS_ALTER_DONE_FAILURE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.get(i), subtaskGroup.getTaskType());
    }

    String taskErrMsg = taskInfo.getSubTasks().get(1).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to add tables to XClusterConfig(%s): %s", xClusterConfig.uuid, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());
    Optional<XClusterTableConfig> table3Config = xClusterConfig.maybeGetTableById(exampleTableID3);
    assertTrue(table3Config.isPresent());
    assertFalse(table3Config.get().replicationSetupDone);

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

    String alterErrMsg = "failed to modify tables";

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, null)))
          .thenReturn(mockEditResponse);

      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(alterErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, appStatusBuilder.build());
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID2);
    newTables.add(exampleTableID3);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.size(), taskInfo.getSubTasks().size());
    for (int i = 0; i < ADD_TABLE_IS_ALTER_DONE_FAILURE.size(); i++) {
      TaskInfo subtaskGroup = taskInfo.getSubTasks().get(i);
      assertNotNull(subtaskGroup);
      assertEquals(ADD_TABLE_IS_ALTER_DONE_FAILURE.get(i), subtaskGroup.getTaskType());
    }

    String taskErrMsg = taskInfo.getSubTasks().get(1).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) operation failed: code: %s\nmessage: \"%s\"",
            xClusterConfig.uuid, ErrorCode.UNKNOWN_ERROR, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());
    Optional<XClusterTableConfig> table3Config = xClusterConfig.maybeGetTableById(exampleTableID3);
    assertTrue(table3Config.isPresent());
    assertFalse(table3Config.get().replicationSetupDone);

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

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID2)))
          .thenReturn(mockEditResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 2);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID2)))
          .thenReturn(mockEditResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(2, targetUniverse.version);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testRemoveTablesAlterFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

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
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID2)))
          .thenReturn(mockEditResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigModifyTables, taskInfo.getSubTasks().get(1).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(1).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to remove tables from XClusterConfig(%s): %s",
            xClusterConfig.uuid, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);
    assertEquals(exampleTables, xClusterConfig.getTables());

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

    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      AlterUniverseReplicationResponse mockAddResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, null)))
          .thenReturn(mockAddResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);

      AlterUniverseReplicationResponse mockRemoveResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID2)))
          .thenReturn(mockRemoveResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID3);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());
    xClusterConfig.tables.forEach(tableConfig -> assertTrue(tableConfig.replicationSetupDone));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.version);
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
    initTargetUniverseClusterConfig(xClusterConfig.getReplicationGroupName(), 3);

    try {
      AlterUniverseReplicationResponse mockAddResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(),
              Collections.singletonMap(exampleTableID3, null)))
          .thenReturn(mockAddResponse);

      IsSetupUniverseReplicationDoneResponse mockIsAlterReplicationDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isAlterUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsAlterReplicationDoneResponse);

      AlterUniverseReplicationResponse mockRemoveResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationRemoveTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID2)))
          .thenReturn(mockRemoveResponse);
    } catch (Exception e) {
    }

    Set<String> newTables = new HashSet<>();
    newTables.add(exampleTableID1);
    newTables.add(exampleTableID3);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = newTables;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());
    xClusterConfig.tables.forEach(tableConfig -> assertTrue(tableConfig.replicationSetupDone));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(4, targetUniverse.version);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }
}
