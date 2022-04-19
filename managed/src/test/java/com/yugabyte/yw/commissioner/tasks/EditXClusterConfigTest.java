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
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

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
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClient;
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
  private HashSet<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

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
          CustomerTask.TaskType.EditXClusterConfig,
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
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
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigRename, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format("Failed to rename XClusterConfig(%s): %s", xClusterConfig.uuid, renameErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Paused, xClusterConfig.status);

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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Paused, xClusterConfig.status);

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
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Paused);

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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);

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
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Paused);

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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);

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

    String pauseResumeErrMsg = "failed to set status";

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
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigSetStatus, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to set XClusterConfig(%s) status: %s", xClusterConfig.uuid, pauseResumeErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testAddTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID3)))
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
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
  public void testAddTablesHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");

    try {
      AlterUniverseReplicationResponse mockEditResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID3)))
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());

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
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID3)))
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
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigModifyTables, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to add tables to XClusterConfig(%s): %s", xClusterConfig.uuid, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);
    assertEquals(exampleTables, xClusterConfig.getTables());

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
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID3)))
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
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigModifyTables, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) operation failed: code: %s\nmessage: \"%s\"",
            xClusterConfig.uuid, ErrorCode.UNKNOWN_ERROR, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);
    assertEquals(exampleTables, xClusterConfig.getTables());

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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
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
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigModifyTables, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "Failed to remove tables from XClusterConfig(%s): %s",
            xClusterConfig.uuid, alterErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
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

    try {
      AlterUniverseReplicationResponse mockAddResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID3)))
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
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
  public void testAddRemoveTablesHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    HighAvailabilityConfig.create("test-cluster-key");

    try {
      AlterUniverseReplicationResponse mockAddResponse =
          new AlterUniverseReplicationResponse(0, "", null);
      when(mockClient.alterUniverseReplicationAddTables(
              xClusterConfig.getReplicationGroupName(), Collections.singleton(exampleTableID3)))
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
    assertEquals(Success, taskInfo.getTaskState());

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Running, xClusterConfig.status);
    assertEquals(newTables, xClusterConfig.getTables());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(4, targetUniverse.version);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testEditXClusterStateInit() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Init);

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.sourceUniverseUUID + "_" + newName;

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertEquals(Failure, taskInfo.getTaskState());

    String taskErrMsg = taskInfo.getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) must be in `Running` or `Paused` state to edit.",
            xClusterConfig.uuid);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testEditXClusterStateUpdating() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Updating);

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.sourceUniverseUUID + "_" + newName;

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertEquals(Failure, taskInfo.getTaskState());

    String taskErrMsg = taskInfo.getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) must be in `Running` or `Paused` state to edit.",
            xClusterConfig.uuid);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }

  @Test
  public void testEditXClusterStateFailed() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Failed);

    String newName = configName + "-renamed";
    String newFullName = xClusterConfig.sourceUniverseUUID + "_" + newName;

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.name = newName;
    TaskInfo taskInfo = submitTask(xClusterConfig, editFormData);
    assertEquals(Failure, taskInfo.getTaskState());

    String taskErrMsg = taskInfo.getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) must be in `Running` or `Paused` state to edit.",
            xClusterConfig.uuid);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    xClusterConfig.refresh();
    assertEquals(XClusterConfigStatusType.Failed, xClusterConfig.status);

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);

    xClusterConfig.delete();
  }
}
