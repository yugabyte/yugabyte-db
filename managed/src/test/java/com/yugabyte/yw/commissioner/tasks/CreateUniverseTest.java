// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateUniverseTest extends UniverseModifyBaseTest {

  private static final List<TaskType> UNIVERSE_CREATE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.WaitForMasterLeader,
          TaskType.AnsibleConfigureServers,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForTServerHeartBeats,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.CreateAlertDefinitions,
          TaskType.CreateTable,
          TaskType.ChangeAdminPassword,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> UNIVERSE_CREATE_TASK_RETRY_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.WaitForMasterLeader,
          TaskType.AnsibleConfigureServers,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForTServerHeartBeats,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.CreateAlertDefinitions,
          TaskType.CreateTable,
          TaskType.ChangeAdminPassword,
          TaskType.UniverseUpdateSucceeded);

  private void assertTaskSequence(
      List<TaskType> sequence, Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    assertEquals(sequence.size(), subTasksByPosition.size());
    for (TaskType taskType : sequence) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertTrue(tasks.size() > 0);
      assertEquals(taskType, tasks.get(0).getTaskType());
      position++;
    }
  }

  @Override
  @Before
  public void setUp() {
    super.setUp();

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockMasterChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1111, "", null);
    // ChangeConfigResponse mockChangeConfigResponse = mock(ChangeConfigResponse.class);
    ListTabletServersResponse mockListTabletServersResponse = mock(ListTabletServersResponse.class);
    when(mockListTabletServersResponse.getTabletServersCount()).thenReturn(10);

    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.listTabletServers()).thenReturn(mockListTabletServersResponse);
    } catch (Exception e) {
      fail();
    }
    mockWaits(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    doAnswer(
            inv -> {
              return null;
            })
        .when(mockYcqlQueryExecutor)
        .validateAdminPassword(any(), any());
    doAnswer(
            inv -> {
              return null;
            })
        .when(mockYsqlQueryExecutor)
        .validateAdminPassword(any(), any());
    ShellResponse successResponse = new ShellResponse();
    successResponse.message = "Command output:\nCREATE TABLE";
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), (any())))
        .thenReturn(successResponse);
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private UniverseDefinitionTaskParams getTaskParams(boolean enableAuth) {
    Universe result =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            u -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              Cluster primaryCluster = universeDetails.getPrimaryCluster();
              primaryCluster.userIntent.enableYCQL = enableAuth;
              primaryCluster.userIntent.enableYCQLAuth = enableAuth;
              primaryCluster.userIntent.ycqlPassword = "Admin@123";
              primaryCluster.userIntent.enableYSQL = enableAuth;
              primaryCluster.userIntent.enableYSQLAuth = enableAuth;
              primaryCluster.userIntent.ysqlPassword = "Admin@123";
              primaryCluster.userIntent.enableYEDIS = false;
              for (NodeDetails node : universeDetails.nodeDetailsSet) {
                // Reset for creation.
                node.state = NodeDetails.NodeState.ToBeAdded;
                node.isMaster = false;
                node.nodeName = null;
              }
            });
    UniverseDefinitionTaskParams universeDetails = result.getUniverseDetails();
    universeDetails.universeUUID = defaultUniverse.universeUUID;
    universeDetails.firstTry = true;
    universeDetails.previousTaskUUID = null;
    return universeDetails;
  }

  @Test
  public void testCreateUniverseSuccess() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_SEQUENCE, subTasksByPosition);
  }

  @Test
  public void testCreateUniverseRetrySuccess() {
    // Fail the task for the first run.
    when(mockClient.waitForServer(any(), anyLong()))
        .thenThrow(new RuntimeException())
        .thenReturn(true);
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_SEQUENCE, subTasksByPosition);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getTaskUUID());
    taskParams = Json.fromJson(taskInfo.getTaskDetails(), UniverseDefinitionTaskParams.class);
    taskParams.previousTaskUUID = taskInfo.getTaskUUID();
    taskParams.firstTry = false;
    // Retry the task.
    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    subTasks = taskInfo.getSubTasks();
    subTasksByPosition = subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_RETRY_SEQUENCE, subTasksByPosition);
  }

  @Test
  public void testCreateUniverseRetryFailure() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_SEQUENCE, subTasksByPosition);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getTaskUUID());
    taskParams = Json.fromJson(taskInfo.getTaskDetails(), UniverseDefinitionTaskParams.class);
    taskParams.previousTaskUUID = taskInfo.getTaskUUID();
    taskParams.firstTry = false;
    primaryCluster.userIntent.enableYCQL = true;
    primaryCluster.userIntent.enableYCQLAuth = true;
    primaryCluster.userIntent.ycqlPassword = "Admin@123";
    primaryCluster.userIntent.enableYSQL = true;
    primaryCluster.userIntent.enableYSQLAuth = true;
    primaryCluster.userIntent.ysqlPassword = "Admin@123";
    taskInfo = submitTask(taskParams);
    // Task is already successful, so the passwords must have been cleared.
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
