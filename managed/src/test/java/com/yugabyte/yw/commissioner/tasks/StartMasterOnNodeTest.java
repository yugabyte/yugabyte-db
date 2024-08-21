// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.lenient;
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
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class StartMasterOnNodeTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private YBClient mockClient;

  @Override
  @Before
  public void setUp() {
    super.setUp();

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
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

    mockClient = mock(YBClient.class);

    try {
      lenient().when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
      lenient().when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.setFlag(any(HostAndPort.class), anyString(), anyString(), anyBoolean()))
          .thenReturn(true);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      lenient().when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
      mockClockSyncResponse(mockNodeUniverseManager);
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
    } catch (Exception e) {
      fail();
    }

    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    taskParams.clusters.addAll(universe.getUniverseDetails().clusters);
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeName = nodeName;
    if (universe.getNode(nodeName) != null) {
      NodeDetails node = universe.getNode(nodeName);
      if (node.isInPlacement(universe.getUniverseDetails().getPrimaryCluster().uuid)) {
        List<String> masterIps =
            universe.getUniverseDetails().nodeDetailsSet.stream()
                .filter(n -> n.isMaster)
                .map(n -> n.cloudInfo.private_ip)
                .collect(Collectors.toList());
        masterIps.add(node.cloudInfo.private_ip);
        UniverseModifyBaseTest.mockMasterAndPeerRoles(mockClient, masterIps);
      }
    }
    try {
      UUID taskUUID = commissioner.submit(TaskType.StartMasterOnNode, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  // @formatter:off
  private static final List<TaskType> START_MASTER_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.SetNodeState,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          TaskType.ChangeMasterConfig,
          TaskType.CheckFollowerLag,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> START_MASTER_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Starting")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  // @formatter:on

  private void assertStartMasterSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType : START_MASTER_TASK_SEQUENCE) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      // assertEquals(1, tasks.size());
      assertEquals("At position: " + position, taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = START_MASTER_TASK_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Test
  public void testStartMasterOnNodeIfUnderReplicatedMasterAndNodeIsLive() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams, "host-n2");
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeManager, times(10)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(START_MASTER_TASK_SEQUENCE.size(), subTasksByPosition.size());
    assertStartMasterSequence(subTasksByPosition);
  }

  @Test
  public void testStartMasterOnNodeIfNodeIsUnknown() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "host-n9"));
  }

  @Test
  public void testStartMasterOnNodeIfAlreadyMaster() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "host-n1"));
  }

  @Test
  public void testStartMasterOnNodeIfUnderReplicatedMasterAndNodeIsRemoved() {
    Universe universe = createUniverse("DemoX");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    // Node "host-n4" is in Removed state already.
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "host-n4"));
  }

  @Test
  public void testStartMasterOnNodeIfNodeInReadOnlyCluster() {
    Universe universe = createUniverse("DemoX");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(false, 3));

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    // Node "yb-tserver-0" is in Read Only cluster.
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "yb-tserver-0"));
  }

  @Test
  public void testStartMasterInUniverseRetries() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.clusters.addAll(universe.getUniverseDetails().clusters);
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeName = "host-n2";
    NodeDetails node = universe.getNode(taskParams.nodeName);
    List<String> masterIps =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.isMaster)
            .map(n -> n.cloudInfo.private_ip)
            .collect(Collectors.toList());
    masterIps.add(node.cloudInfo.private_ip);
    UniverseModifyBaseTest.mockMasterAndPeerRoles(mockClient, masterIps);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.StartMaster,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.StartMasterOnNode,
        taskParams);
  }
}
