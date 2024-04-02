// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class StartNodeInUniverseTest extends CommissionerBaseTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Universe defaultUniverse;

  private YBClient mockClient;

  private Region region;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    mockClient = mock(YBClient.class);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
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
    } catch (Exception e) {
      fail();
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

    Map<String, String> gflags = new HashMap<>();
    gflags.put("foo", "bar");
    defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags = gflags;

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
    UniverseModifyBaseTest.mockGetMasterRegistrationResponse(
        mockClient, ImmutableList.of("10.0.0.1"), Collections.emptyList());

    setFollowerLagMock();
    setLeaderlessTabletsMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
    return submitTask(taskParams, nodeName, 3);
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName, int expectedVersion) {
    taskParams.clusters.addAll(
        Universe.getOrBadRequest(taskParams.getUniverseUUID()).getUniverseDetails().clusters);
    taskParams.expectedUniverseVersion = expectedVersion;
    taskParams.nodeName = nodeName;
    try {
      UUID taskUUID = commissioner.submit(TaskType.StartNodeInUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private Universe setMasters(Universe universe, String... nodeNames) {
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        univ -> {
          Arrays.stream(nodeNames)
              .map(name -> univ.getNode(name))
              .forEach(node -> node.isMaster = true);
        });
  }

  List<TaskType> START_NODE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  List<JsonNode> START_NODE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Starting")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  List<TaskType> WITH_MASTER_UNDER_REPLICATED =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          // Start master.
          TaskType.AnsibleClusterServerCtl,
          // Set master to true.
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          TaskType.ChangeMasterConfig,
          TaskType.CheckFollowerLag,
          // Start of master address update subtasks from MasterInfoUpdateTask.
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          // End of master address update subtasks.
          // Update master address in config file for tserver.
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  List<JsonNode> WITH_MASTER_UNDER_REPLICATED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Starting")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", true)),
          Json.toJson(ImmutableMap.of("serverType", "MASTER")),
          Json.toJson(ImmutableMap.of("opType", "AddMaster")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("serverType", "TSERVER")),
          Json.toJson(ImmutableMap.of("serverType", "MASTER")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of("serverType", "TSERVER")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertStartNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, boolean masterStartExpected) {
    int position = 0;
    if (masterStartExpected) {
      for (TaskType taskType : WITH_MASTER_UNDER_REPLICATED) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        assertEquals("At position: " + position, taskType, tasks.get(0).getTaskType());
        JsonNode expectedResults = WITH_MASTER_UNDER_REPLICATED_RESULTS.get(position);
        List<JsonNode> taskDetails =
            tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
        assertJsonEqual(expectedResults, taskDetails.get(0));
        position++;
      }
    } else {
      for (TaskType taskType : START_NODE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        assertEquals(1, tasks.size());
        assertEquals("At position: " + position, taskType, tasks.get(0).getTaskType());
        JsonNode expectedResults = START_NODE_TASK_EXPECTED_RESULTS.get(position);
        List<JsonNode> taskDetails =
            tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
        assertJsonEqual(expectedResults, taskDetails.get(0));
        position++;
      }
    }
  }

  @Test
  public void testAddNodeSuccess() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    // Set one master atleast for master addresses to be populated.
    setMasters(defaultUniverse, "host-n2");
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(START_NODE_TASK_SEQUENCE.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, false);
  }

  @Test
  public void testStartNodeWithUnderReplicatedMaster_WithoutReadOnlyCluster_NodeFromPrimary() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    // Set one master atleast for master addresses to be populated.
    setMasters(universe, "host-n2");
    setExpectedMasters(universe, "host-n1", "host-n2");
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(13)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(WITH_MASTER_UNDER_REPLICATED.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, true);
  }

  @Test
  public void testStartUnknownNode() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    // Throws at validateParams check.
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "host-n9"));
  }

  @Test
  public void testStartNodeWithUnderReplicatedMaster_WithReadOnlyCluster_NodeFromPrimary() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(false, 3));
    // Set one master atleast for master addresses to be populated.
    setMasters(universe, "host-n2");
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    setExpectedMasters(universe, "host-n1", "host-n2");
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(16)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(WITH_MASTER_UNDER_REPLICATED.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, true /* Master start is expected */);
  }

  @Test
  public void testStartNodeWithUnderReplicatedMaster_WithReadOnlyCluster_NodeFromReadReplica() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(false, 3));
    // Set one master atleast for master addresses to be populated.
    setMasters(universe, "host-n2");
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams, "yb-tserver-0");
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(START_NODE_TASK_SEQUENCE.size(), subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, false /* Master start is unexpected */);
  }

  @Test
  // @formatter:off
  @Parameters({"false, region-1, true", "true, region-1, true", "true, test-region, false"})
  // @formatter:on
  public void testStartNodeWithUnderReplicatedMaster_WithDefaultRegion(
      boolean isDefaultRegion, String nodeRegionCode, boolean isMasterStart) {
    // 'test-region' for automatically created nodes.
    Region testRegion = Region.create(defaultProvider, "test-region", "Region 2", "yb-image-1");

    Universe universe = createUniverse("Demo");

    // This adds nodes in test-region.
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes(false));

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            univ -> {
              univ.getNode("host-n1").cloudInfo.region = nodeRegionCode;
              Cluster cluster = univ.getUniverseDetails().clusters.get(0);
              cluster.userIntent.regionList =
                  ImmutableList.of(region.getUuid(), testRegion.getUuid());
              cluster.placementInfo =
                  PlacementInfoUtil.getPlacementInfo(
                      ClusterType.PRIMARY,
                      cluster.userIntent,
                      cluster.userIntent.replicationFactor,
                      region.getUuid(),
                      Collections.emptyList());
              if (isDefaultRegion) {
                cluster.placementInfo.cloudList.get(0).defaultRegion = region.getUuid();
              }
            });
    // Set one master atleast for master addresses to be populated.
    universe = setMasters(universe, "host-n2");
    setExpectedMasters(universe, "host-n1", "host-n2");
    TaskInfo taskInfo = submitTask(taskParams, "host-n1", 4);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(isMasterStart ? 13 : 3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(
        isMasterStart ? WITH_MASTER_UNDER_REPLICATED.size() : START_NODE_TASK_SEQUENCE.size(),
        subTasksByPosition.size());
    assertStartNodeSequence(subTasksByPosition, isMasterStart);
  }

  private void setExpectedMasters(Universe universe, String... names) {
    UniverseModifyBaseTest.mockMasterAndPeerRoles(
        mockClient,
        Arrays.stream(names)
            .map(name -> universe.getNode(name))
            .map(n -> n.cloudInfo.private_ip)
            .collect(Collectors.toList()));
  }

  @Test
  public void testStartNodeInUniverseRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(false, 3));
    // Set one master atleast for master addresses to be populated.
    setMasters(universe, "host-n2");
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.clusters.addAll(
        Universe.getOrBadRequest(taskParams.getUniverseUUID()).getUniverseDetails().clusters);
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeName = "host-n1";
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Start,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.StartNodeInUniverse,
        taskParams);
  }
}
