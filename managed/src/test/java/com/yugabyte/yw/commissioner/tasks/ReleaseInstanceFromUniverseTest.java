// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReleaseInstanceFromUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private YBClient mockClient;

  private static final String DEFAULT_NODE_NAME = "host-n1";

  private void setDefaultNodeState(final NodeState desiredState) {
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Set<NodeDetails> nodes = universeDetails.nodeDetailsSet;
          for (NodeDetails node : nodes) {
            if (node.nodeName.equals(DEFAULT_NODE_NAME)) {
              node.state = desiredState;
              break;
            }
          }
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.universeUUID, updater);
  }

  @Override
  @Before
  public void setUp() {
    super.setUp();

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent;
    // create default universe
    userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = defaultProvider.uuid.toString();
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(
        defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */));

    setDefaultNodeState(NodeState.Removed);

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(4);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1111, "", null);

    mockClient = mock(YBClient.class);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockChangeConfigResponse);
    } catch (Exception e) {
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(new ShellResponse());
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName, int version) {
    taskParams.expectedUniverseVersion = version;
    taskParams.nodeName = nodeName;
    try {
      UUID taskUUID = commissioner.submit(TaskType.ReleaseInstanceFromUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<TaskType> RELEASE_INSTANCE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.WaitForMasterLeader,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState,
          TaskType.AnsibleDestroyServer,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> RELEASE_INSTANCE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "BeingDecommissioned")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Decommissioned")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertReleaseInstanceSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType : RELEASE_INSTANCE_TASK_SEQUENCE) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(1, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = RELEASE_INSTANCE_TASK_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Test
  public void testReleaseInstanceSuccess() {
    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(3);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);

    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;

    TaskInfo taskInfo = submitTask(taskParams, DEFAULT_NODE_NAME, 3);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(subTasksByPosition.size(), RELEASE_INSTANCE_TASK_SEQUENCE.size());
    assertReleaseInstanceSequence(subTasksByPosition);
  }

  @Test
  public void testReleaseStoppedInstanceSuccess() {
    setDefaultNodeState(NodeState.Removed);
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;

    TaskInfo taskInfo = submitTask(taskParams, DEFAULT_NODE_NAME, 4);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(subTasksByPosition.size(), RELEASE_INSTANCE_TASK_SEQUENCE.size());
    assertReleaseInstanceSequence(subTasksByPosition);
  }

  @Test
  public void testReleaseUnknownNode() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    // Throws at validateParams check.
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "host-n9", 3));
  }

  @Test
  public void testReleaseNodeAllowedState() {
    Set<NodeState> allowedStates = NodeState.allowedStatesForAction(NodeActionType.RELEASE);
    Set<NodeState> expectedStates =
        ImmutableSet.of(
            NodeState.Adding,
            NodeState.BeingDecommissioned,
            NodeState.Removed,
            NodeState.Terminating);
    assertEquals(expectedStates, allowedStates);
  }
}
