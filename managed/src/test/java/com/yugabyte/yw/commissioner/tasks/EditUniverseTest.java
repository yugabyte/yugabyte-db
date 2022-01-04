// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.master.CatalogEntityInfo;

@RunWith(MockitoJUnitRunner.class)
public class EditUniverseTest extends UniverseModifyBaseTest {

  private static final List<TaskType> UNIVERSE_EXPAND_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeStatus, // ToBeAdded to Adding
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.ModifyBlackList,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.UpdatePlacementInfo,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.WaitForLoadBalance,
          TaskType.ChangeMasterConfig, // Add
          TaskType.ChangeMasterConfig, // Remove
          TaskType.AnsibleClusterServerCtl, // Stop master
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers, // Tservers
          TaskType.SetFlagInMemory,
          TaskType.AnsibleConfigureServers, // Masters
          TaskType.SetFlagInMemory,
          TaskType.ModifyBlackList,
          TaskType.WaitForTServerHeartBeats,
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
    ChangeConfigResponse mockChangeConfigResponse = mock(ChangeConfigResponse.class);
    ListTabletServersResponse mockListTabletServersResponse = mock(ListTabletServersResponse.class);
    when(mockListTabletServersResponse.getTabletServersCount()).thenReturn(10);

    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.changeMasterConfig(anyString(), anyInt(), anyBoolean(), anyBoolean()))
          .thenReturn(mockChangeConfigResponse);
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
          .thenReturn(Boolean.TRUE);
      when(mockClient.listTabletServers()).thenReturn(mockListTabletServersResponse);
    } catch (Exception e) {
    }
    mockWaits(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockClient.waitForLoadBalance(anyLong(), anyInt())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testExpandSuccess() {
    Universe universe = defaultUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_EXPAND_TASK_SEQUENCE, subTasksByPosition);
    universe = Universe.getOrBadRequest(universe.universeUUID);
    assertEquals(5, universe.getUniverseDetails().nodeDetailsSet.size());
  }

  @Test
  public void testExpandOnPremSuccess() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);

    Universe universe = onPremUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_EXPAND_TASK_SEQUENCE, subTasksByPosition);
    universe = Universe.getOrBadRequest(universe.universeUUID);
    assertEquals(5, universe.getUniverseDetails().nodeDetailsSet.size());
  }

  @Test
  public void testExpandOnPremFailNoNodes() {
    Universe universe = onPremUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testExpandOnPremFailProvision() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);
    Universe universe = onPremUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    preflightResponse.message = "{\"test\": false}";

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  private UniverseDefinitionTaskParams performExpand(Universe universe) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodePrefix = universe.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 5;
    newUserIntent.numNodes = 5;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getCustomerId(), primaryCluster.uuid, EDIT);

    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    return taskParams;
  }
}
