// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
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
          TaskType.FreezeUniverse,
          TaskType.InstanceExistCheck,
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks, // PreNodeProvision
          TaskType.AnsibleSetupServer,
          TaskType.RunHooks, // PostNodeProvision
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.WaitForServer, // wait for postgres
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
          TaskType.FreezeUniverse,
          TaskType.InstanceExistCheck,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.WaitForServer, // wait for postgres
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
      when(mockNodeUniverseManager.runCommand(any(), any(), any()))
          .thenReturn(
              ShellResponse.create(
                  ShellResponse.ERROR_CODE_SUCCESS,
                  ShellResponse.RUN_COMMAND_OUTPUT_PREFIX
                      + "Reference ID    : A9FEA9FE (metadata.google.internal)\n"
                      + "    Stratum         : 3\n"
                      + "    Ref time (UTC)  : Mon Jun 12 16:18:24 2023\n"
                      + "    System time     : 0.000000003 seconds slow of NTP time\n"
                      + "    Last offset     : +0.000019514 seconds\n"
                      + "    RMS offset      : 0.000011283 seconds\n"
                      + "    Frequency       : 99.154 ppm slow\n"
                      + "    Residual freq   : +0.009 ppm\n"
                      + "    Skew            : 0.106 ppm\n"
                      + "    Root delay      : 0.000162946 seconds\n"
                      + "    Root dispersion : 0.000101734 seconds\n"
                      + "    Update interval : 32.3 seconds\n"
                      + "    Leap status     : Normal"));

      mockLocaleCheckResponse(mockNodeUniverseManager);
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
            defaultUniverse.getUniverseUUID(),
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
    universeDetails.creatingUser = defaultUser;
    universeDetails.setUniverseUUID(defaultUniverse.getUniverseUUID());
    universeDetails.setPreviousTaskUUID(null);
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
    taskParams = Json.fromJson(taskInfo.getDetails(), UniverseDefinitionTaskParams.class);
    taskParams.setPreviousTaskUUID(taskInfo.getTaskUUID());
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
    taskParams = Json.fromJson(taskInfo.getDetails(), UniverseDefinitionTaskParams.class);
    taskParams.setPreviousTaskUUID(taskInfo.getTaskUUID());
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

  @Test
  public void testCreateDedicatedUniverseSuccess() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    taskParams.getPrimaryCluster().userIntent.dedicatedNodes = true;
    PlacementInfoUtil.SelectMastersResult selectMastersResult =
        PlacementInfoUtil.selectMasters(
            null, taskParams.nodeDetailsSet, null, true, taskParams.clusters);
    selectMastersResult.addedMasters.forEach(taskParams.nodeDetailsSet::add);
    PlacementInfoUtil.dedicateNodes(taskParams.nodeDetailsSet);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Map<UniverseTaskBase.ServerType, List<NodeDetails>> byDedicatedType =
        defaultUniverse.getNodes().stream().collect(Collectors.groupingBy(n -> n.dedicatedTo));
    List<NodeDetails> masterNodes = byDedicatedType.get(UniverseTaskBase.ServerType.MASTER);
    List<NodeDetails> tserverNodes = byDedicatedType.get(UniverseTaskBase.ServerType.TSERVER);
    assertEquals(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor,
        masterNodes.size());
    assertEquals(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes,
        tserverNodes.size());
    for (NodeDetails masterNode : masterNodes) {
      assertTrue(masterNode.isMaster);
      assertFalse(masterNode.isTserver);
    }
    for (NodeDetails tserverNode : tserverNodes) {
      assertFalse(tserverNode.isMaster);
      assertTrue(tserverNode.isTserver);
    }
  }

  @Test
  public void testCreateUniverseWithReadReplicaSuccess() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    UniverseDefinitionTaskParams.UserIntent intent =
        taskParams.getPrimaryCluster().userIntent.clone();
    intent.replicationFactor = 1;
    intent.numNodes = 1;
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            UniverseDefinitionTaskParams.ClusterType.ASYNC,
            intent,
            1,
            null,
            Collections.emptyList());
    Universe updated =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(intent, placementInfo));
    taskParams.clusters.add(updated.getUniverseDetails().getReadOnlyClusters().get(0));
    taskParams.nodeDetailsSet = updated.getUniverseDetails().nodeDetailsSet;
    taskParams.nodeDetailsSet.forEach(
        node -> {
          node.nodeName = null;
          node.state = NodeDetails.NodeState.ToBeAdded;
        });
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    int tserversStarted =
        (int)
            taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskType() == TaskType.AnsibleClusterServerCtl)
                .map(t -> t.getDetails())
                .filter(t -> t.has("process") && t.get("process").asText().equals("tserver"))
                .filter(t -> t.has("command") && t.get("command").asText().equals("start"))
                .count();
    assertEquals(taskParams.getPrimaryCluster().userIntent.numNodes + 1, tserversStarted);
  }

  @Test
  public void testCreateUniverseRetries() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    UniverseDefinitionTaskParams.UserIntent intent =
        taskParams.getPrimaryCluster().userIntent.clone();
    intent.replicationFactor = 1;
    intent.numNodes = 1;
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            UniverseDefinitionTaskParams.ClusterType.ASYNC,
            intent,
            1,
            null,
            Collections.emptyList());
    Universe updated =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(intent, placementInfo));
    taskParams.clusters.add(updated.getUniverseDetails().getReadOnlyClusters().get(0));
    taskParams.nodeDetailsSet = updated.getUniverseDetails().nodeDetailsSet;
    taskParams.nodeDetailsSet.forEach(
        node -> {
          node.nodeName = null;
          node.state = NodeDetails.NodeState.ToBeAdded;
        });
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Create,
        CustomerTask.TargetType.Universe,
        updated.getUniverseUUID(),
        TaskType.CreateUniverse,
        taskParams);
  }
}
