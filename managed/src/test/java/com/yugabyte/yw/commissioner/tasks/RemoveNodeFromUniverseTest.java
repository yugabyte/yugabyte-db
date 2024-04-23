// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.*;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.TabletServerInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class RemoveNodeFromUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  private YBClient mockClient;

  public void setUp(boolean withMaster, int numNodes, int replicationFactor, boolean multiZone) {
    super.setUp();

    Region region = Region.create(defaultProvider, "test-region", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    if (multiZone) {
      AvailabilityZone.createOrThrow(region, "az-2", "az-2", "subnet-2");
      AvailabilityZone.createOrThrow(region, "az-3", "az-3", "subnet-3");
    }
    // create default universe
    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = numNodes;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.ybSoftwareVersion = "2.8.1.0-b1";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = replicationFactor;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, withMaster /* setMasters */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Set<NodeDetails> nodes = universeDetails.nodeDetailsSet;
          int count = 0;
          int numZones = 1;
          if (multiZone) {
            numZones = 3;
          }
          for (NodeDetails node : nodes) {
            node.cloudInfo.az = "az-" + (count % numZones + 1);
            nodes.add(node);
          }
          universeDetails.nodeDetailsSet = nodes;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    mockClient = mock(YBClient.class);
    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.List)) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                if (params.nodeUuid == null) {
                  listResponse.message = "{\"universe_uuid\":\"" + params.getUniverseUUID() + "\"}";
                } else {
                  listResponse.message =
                      "{\"universe_uuid\":\""
                          + params.getUniverseUUID()
                          + "\", "
                          + "\"node_uuid\": \""
                          + params.nodeUuid
                          + "\"}";
                }
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            });
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);

    ChangeMasterClusterConfigResponse ccr = new ChangeMasterClusterConfigResponse(1111, "", null);
    GetLoadMovePercentResponse gpr = new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null);
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      // WaitForTServerHeartBeats mock.
      doNothing().when(mockClient).waitForMasterLeader(anyLong());
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(ccr);
      when(mockClient.getLoadMoveCompletion()).thenReturn(gpr);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
      when(mockClient.setFlag(any(), any(), any(), anyBoolean())).thenReturn(true);
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getLeaderMasterHostAndPort())
          .thenReturn(HostAndPort.fromParts("1.1.1.1", 1234));
      setMockLiveTabletServers();
    } catch (Exception e) {
      fail();
    }

    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    mockWaits(mockClient, 3);
    UniverseModifyBaseTest.mockGetMasterRegistrationResponse(
        mockClient, ImmutableList.of("10.0.0.4", "10.0.0.6"), Collections.emptyList());

    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
    taskParams.nodeName = nodeName;
    try {
      UUID taskUUID = commissioner.submit(TaskType.RemoveNodeFromUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<TaskType> REMOVE_NODE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForDataMove,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> REMOVE_NODE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Removing")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
          Json.toJson(ImmutableMap.of("state", "Removed")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> REMOVE_NODE_WITH_MASTER_REPLACE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForDataMove,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          TaskType.ChangeMasterConfig,
          TaskType.CheckFollowerLag,
          TaskType.ChangeMasterConfig,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState,
          TaskType.SetNodeStatus,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> REMOVE_NODE_WITH_MASTER_REPLACE_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Removing")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of("isAdd", false, "processType", "TSERVER")),
          Json.toJson(ImmutableMap.of("state", "Starting")),
          Json.toJson(ImmutableMap.of("type", "Software")),
          Json.toJson(ImmutableMap.of("type", "GFlags")),
          Json.toJson(ImmutableMap.of("type", "GFlags")),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of("isAdd", true, "processType", "MASTER")),
          Json.toJson(ImmutableMap.of("serverType", "MASTER")),
          Json.toJson(ImmutableMap.of("opType", "AddMaster")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("opType", "RemoveMaster")),
          Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
          // Wait for master leader.
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("isAdd", false, "processType", "MASTER")),
          // Tserver.
          Json.toJson(ImmutableMap.of("type", "GFlags")),
          // Master.
          Json.toJson(ImmutableMap.of("type", "GFlags")),
          Json.toJson(ImmutableMap.of("serverType", "TSERVER")),
          Json.toJson(ImmutableMap.of("serverType", "MASTER")),
          Json.toJson(ImmutableMap.of("state", "Live")),
          // Clear master state.
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Removed")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> REMOVE_NODE_WITH_MASTER =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForDataMove,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.ChangeMasterConfig,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeStatus,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> REMOVE_NODE_WITH_MASTER_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Removing")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of("isAdd", false, "processType", "TSERVER")),
          Json.toJson(ImmutableMap.of("opType", "RemoveMaster")),
          Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("isAdd", false, "processType", "MASTER")),
          // Tserver.
          Json.toJson(ImmutableMap.of("type", "GFlags")),
          // Master.
          Json.toJson(ImmutableMap.of("type", "GFlags")),
          Json.toJson(ImmutableMap.of("serverType", "TSERVER")),
          Json.toJson(ImmutableMap.of("serverType", "MASTER")),
          // Clear master state.
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Removed")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private enum RemoveType {
    WITH_MASTER,
    ONLY_TSERVER
  }

  private void assertRemoveNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      RemoveType type,
      boolean moveData,
      boolean replaceMaster) {
    int position = 0;
    int taskPosition = 0;
    switch (type) {
      case WITH_MASTER:
        List<TaskType> taskSequence =
            replaceMaster ? REMOVE_NODE_WITH_MASTER_REPLACE : REMOVE_NODE_WITH_MASTER;
        List<JsonNode> taskParams =
            replaceMaster
                ? REMOVE_NODE_WITH_MASTER_REPLACE_RESULTS
                : REMOVE_NODE_WITH_MASTER_RESULTS;
        for (TaskType taskType : taskSequence) {
          if (taskType.equals(TaskType.WaitForDataMove) && !moveData) {
            position++;
            continue;
          }

          List<TaskInfo> tasks = subTasksByPosition.get(taskPosition);
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults = taskParams.get(position);
          List<JsonNode> taskDetails =
              tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
          taskPosition++;
        }
        break;
      case ONLY_TSERVER:
        for (TaskType taskType : REMOVE_NODE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          assertEquals(1, tasks.size());
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults = REMOVE_NODE_TASK_EXPECTED_RESULTS.get(position);
          List<JsonNode> taskDetails =
              tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
          taskPosition++;
        }
        break;
    }
  }

  @Test
  public void testRemoveNodeSuccess() {
    setUp(false, 4, 3, false);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.ONLY_TSERVER, true, false);
  }

  @Test
  public void testRemoveNodeWithMaster() {
    setUp(true, 4, 3, false);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    List<String> masters =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.nodeName.equals("host-n1"))
            .filter(n -> n.isMaster || n.nodeName.equals("host-n4"))
            .map(n -> n.cloudInfo.private_ip)
            .collect(Collectors.toList());

    UniverseModifyBaseTest.mockMasterAndPeerRoles(mockClient, masters);

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.WITH_MASTER, true, true);
  }

  @Test
  public void testRemoveUnknownNode() {
    setUp(false, 4, 3, false);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    TaskInfo taskInfo = submitTask(taskParams, "host-n9");
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testRemoveNonExistentNode() {
    setUp(false, 4, 3, false);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = null;
    doReturn(dummyShellResponse).when(mockNodeManager).nodeCommand(any(), any());

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.ONLY_TSERVER, true, false);
  }

  @Test
  /*
   * Do not allow removal of a healthy node with tablets assigned to it
   *   when there is no place to move tablets to.
   */
  public void testRemoveNodeHealthyNode() {
    setUp(true, 3, 3, false);
    String nodeToRemove = "host-n1";
    NodeDetails node = defaultUniverse.getNode(nodeToRemove);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort));
    setDumpEntitiesMock(defaultUniverse, nodeToRemove, true);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    TaskInfo taskInfo = submitTask(taskParams, nodeToRemove);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  /*
   * Similar to testRemoveNodeHealthyNode test.
   */
  public void testRemoveNodeHealthyNodeRF5() { // This should fail now.
    setUp(true, 5, 5, true);
    String nodeToRemove = "host-n1";
    NodeDetails node = defaultUniverse.getNode(nodeToRemove);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort));
    setDumpEntitiesMock(defaultUniverse, nodeToRemove, true);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    TaskInfo taskInfo = submitTask(taskParams, nodeToRemove);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  /*
   * We allow removing a node when there is no place to move the tablets
   *  (ex: number of node equals RF) if the node is unhealhy (no tablets assigned to it)
   */
  public void testRemoveNodeUnhealthyNode() {
    setUp(true, 3, 3, true);
    String nodeToRemove = "host-n1";
    NodeDetails node = defaultUniverse.getNode(nodeToRemove);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort));
    setDumpEntitiesMock(defaultUniverse, nodeToRemove, false);

    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    TaskInfo taskInfo = submitTask(taskParams, nodeToRemove);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.WITH_MASTER, true, false);
  }

  @Test
  public void testRemoveNodeRF5() {
    setUp(true, 6, 5, true);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;

    List<String> masters =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.nodeName.equals("host-n1"))
            .filter(n -> n.isMaster || n.nodeName.equals("host-n6"))
            .map(n -> n.cloudInfo.private_ip)
            .collect(Collectors.toList());

    UniverseModifyBaseTest.mockMasterAndPeerRoles(mockClient, masters);
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.WITH_MASTER, true, true);
  }

  @Test
  public void testRemoveNodeAllowedState() {
    Set<NodeState> allowedStates = NodeState.allowedStatesForAction(NodeActionType.REMOVE);
    Set<NodeState> expectedStates =
        ImmutableSet.of(
            NodeState.Adding,
            NodeState.Live,
            NodeState.ToBeRemoved,
            NodeState.ToJoinCluster,
            NodeState.Stopped,
            NodeState.Starting,
            NodeState.Stopping,
            NodeState.Removing);
    assertEquals(expectedStates, allowedStates);
  }

  @Test
  public void testRemoveNodeRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    setUp(true, 4, 3, false);
    String nodeToRemove = "host-n1";
    NodeDetails node = defaultUniverse.getNode(nodeToRemove);
    lenient()
        .when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort));
    setDumpEntitiesMock(defaultUniverse, nodeToRemove, true);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;
    taskParams.nodeName = nodeToRemove;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Remove,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RemoveNodeFromUniverse,
        taskParams);
  }

  public void setMockLiveTabletServers() throws Exception {
    List<TabletServerInfo> tabletServerInfoList = new ArrayList<>();

    // Loop through both Primary cluster and RR nodes.
    for (Cluster cluster : defaultUniverse.getUniverseDetails().clusters) {
      UUID clusterUuid = cluster.uuid;
      Set<NodeDetails> nodesInCluster =
          new HashSet<>(defaultUniverse.getNodesByCluster(cluster.uuid));
      for (NodeDetails curNode : nodesInCluster) {
        TabletServerInfo.CloudInfo cloudInfo = new TabletServerInfo.CloudInfo();
        cloudInfo.setCloud(curNode.cloudInfo.cloud);
        cloudInfo.setRegion(curNode.cloudInfo.region);
        cloudInfo.setZone(curNode.cloudInfo.az);

        TabletServerInfo tserverInfo = new TabletServerInfo();
        tserverInfo.setCloudInfo(cloudInfo);
        tserverInfo.setUuid(UUID.randomUUID());
        tserverInfo.setInPrimaryCluster(cluster.clusterType.equals(ClusterType.PRIMARY));
        tserverInfo.setPlacementUuid(clusterUuid);
        tserverInfo.setPrivateRpcAddress(
            HostAndPort.fromParts(curNode.cloudInfo.private_ip, curNode.tserverRpcPort));

        tabletServerInfoList.add(tserverInfo);
      }
    }
    ListLiveTabletServersResponse listLiveTabletServersResponse =
        mock(ListLiveTabletServersResponse.class);
    when(listLiveTabletServersResponse.getTabletServers()).thenReturn(tabletServerInfoList);
    when(mockClient.listLiveTabletServers()).thenReturn(listLiveTabletServersResponse);
  }
}
