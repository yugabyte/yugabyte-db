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
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashSet;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.SetupUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class CreateXClusterConfigTest extends CommissionerBaseTest {

  private String configName;
  private String sourceUniverseName;
  private UUID sourceUniverseUUID;
  private Universe sourceUniverse;
  private String targetUniverseName;
  private UUID targetUniverseUUID;
  private Universe targetUniverse;
  private String exampleTableID1;
  private String exampleTableID2;
  private HashSet<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("CreateXClusterConfig-test-customer");

    configName = "CreateXClusterConfigTest-test-config";

    sourceUniverseName = "CreateXClusterConfig-test-universe-1";
    sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse(sourceUniverseName, sourceUniverseUUID);
    UniverseDefinitionTaskParams sourceUniverseDetails = sourceUniverse.getUniverseDetails();
    NodeDetails sourceUniverseNodeDetails = new NodeDetails();
    sourceUniverseNodeDetails.isMaster = true;
    sourceUniverseNodeDetails.state = NodeState.Live;
    sourceUniverseNodeDetails.cloudInfo = new CloudSpecificInfo();
    sourceUniverseNodeDetails.cloudInfo.private_ip = "1.1.1.1";
    sourceUniverseNodeDetails.cloudInfo.secondary_private_ip = "2.2.2.2";
    sourceUniverseDetails.nodeDetailsSet.add(sourceUniverseNodeDetails);
    sourceUniverse.setUniverseDetails(sourceUniverseDetails);
    sourceUniverse.update();

    targetUniverseName = "CreateXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";

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
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(xClusterConfig, createFormData);
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.universeUUID,
          taskUUID,
          TargetType.XClusterConfig,
          CustomerTask.TaskType.CreateXClusterConfig,
          xClusterConfig.name);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testCreate() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Init);

    try {
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", null);
      when(mockClient.setupUniverseReplication(any(), any(), any())).thenReturn(mockSetupResponse);
      IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsSetupDoneResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
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
  public void testCreateHAEnabled() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Init);

    HighAvailabilityConfig.create("test-cluster-key");

    try {
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", null);
      when(mockClient.setupUniverseReplication(any(), any(), any())).thenReturn(mockSetupResponse);
      IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, null);
      when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsSetupDoneResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
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
  public void testCreateXClusterStateNotInit() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertEquals(Failure, taskInfo.getTaskState());

    String taskErrMsg = taskInfo.getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format("XClusterConfig(%s) must be in `Init` state to create.", xClusterConfig.uuid);
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
  public void testCreateXClusterSetupFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Init);

    String setupErrMsg = "failed to run setup rpc";

    try {
      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(setupErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      MasterErrorPB.Builder masterErrorBuilder =
          MasterErrorPB.newBuilder()
              .setStatus(appStatusBuilder.build())
              .setCode(Code.UNKNOWN_ERROR);
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", masterErrorBuilder.build());
      when(mockClient.setupUniverseReplication(any(), any(), any())).thenReturn(mockSetupResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigSetup, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format("Failed to create XClusterConfig(%s): %s", xClusterConfig.uuid, setupErrMsg);
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
  public void testCreateXClusterIsSetupDoneFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Init);

    String isSetupDoneErrMsg = "failed to run setup rpc";

    try {
      SetupUniverseReplicationResponse mockSetupResponse =
          new SetupUniverseReplicationResponse(0, "", null);
      when(mockClient.setupUniverseReplication(any(), any(), any())).thenReturn(mockSetupResponse);

      AppStatusPB.Builder appStatusBuilder =
          AppStatusPB.newBuilder().setMessage(isSetupDoneErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
      IsSetupUniverseReplicationDoneResponse mockIsSetupDoneResponse =
          new IsSetupUniverseReplicationDoneResponse(0, "", null, true, appStatusBuilder.build());
      when(mockClient.isSetupUniverseReplicationDone(xClusterConfig.getReplicationGroupName()))
          .thenReturn(mockIsSetupDoneResponse);
    } catch (Exception e) {
    }

    TaskInfo taskInfo = submitTask(xClusterConfig);
    assertEquals(Failure, taskInfo.getTaskState());

    assertEquals(TaskType.XClusterConfigSetup, taskInfo.getSubTasks().get(0).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(0).getTaskDetails().get("errorString").asText();
    String expectedErrMsg =
        String.format(
            "XClusterConfig(%s) operation failed: code: %s\nmessage: \"%s\"",
            xClusterConfig.uuid, ErrorCode.UNKNOWN_ERROR, isSetupDoneErrMsg);
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
