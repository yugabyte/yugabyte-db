// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;

import play.libs.Json;

import java.util.*;
import java.util.stream.Collectors;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo.ModifyUniverseConfig;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyLong;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class RemoveNodeFromUniverseTest extends CommissionerBaseTest {
  public static final Logger LOG = LoggerFactory.getLogger(AddNodeToUniverseTest.class);

  UniverseDefinitionTaskParams.UserIntent userIntent;

  @InjectMocks
  Commissioner commissioner;
  Universe defaultUniverse;
  YBClient mockClient;
  ShellProcessHandler.ShellResponse dummyShellResponse;

  @Before
  public void setUp() {
    super.setUp();
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */));

    mockClient = mock(YBClient.class);

    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    mockWaits(mockClient);
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

  List<TaskType> REMOVE_NODE_TASK_SEQUENCE = ImmutableList.of(
    TaskType.SetNodeState,
    TaskType.UpdatePlacementInfo,
    TaskType.WaitForDataMove,
    TaskType.AnsibleClusterServerCtl,
    TaskType.UpdateNodeProcess,
    TaskType.UpdateNodeProcess,
    TaskType.SetNodeState,
    TaskType.UniverseUpdateSucceeded
  );

  List<JsonNode> REMOVE_NODE_TASK_EXPECTED_RESULTS = ImmutableList.of(
    Json.toJson(ImmutableMap.of("state", "Removing")),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
    Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", false)),
    Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
    Json.toJson(ImmutableMap.of("state", "Removed")),
    Json.toJson(ImmutableMap.of())
  );

  List<TaskType> REMOVE_NODE_WITH_MASTER = ImmutableList.of(
    TaskType.SetNodeState,
    TaskType.WaitForMasterLeader,
    TaskType.ChangeMasterConfig,
    TaskType.AnsibleClusterServerCtl,
    TaskType.WaitForMasterLeader,
    TaskType.UpdatePlacementInfo,
    TaskType.WaitForDataMove,
    TaskType.AnsibleClusterServerCtl,
    TaskType.UpdateNodeProcess,
    TaskType.UpdateNodeProcess,
    TaskType.SetNodeState,
    TaskType.UniverseUpdateSucceeded
  );

  List<JsonNode> REMOVE_NODE_WITH_MASTER_RESULTS = ImmutableList.of(
    Json.toJson(ImmutableMap.of("state", "Removing")),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
    Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", false)),
    Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
    Json.toJson(ImmutableMap.of("state", "Removed")),
    Json.toJson(ImmutableMap.of())
  );

  List<TaskType> REMOVE_NOT_EXISTS_NODE_TASK_SEQUENCE = ImmutableList.of(
    TaskType.SetNodeState,
    TaskType.UpdatePlacementInfo,
    TaskType.UpdateNodeProcess,
    TaskType.UpdateNodeProcess,
    TaskType.SetNodeState,
    TaskType.UniverseUpdateSucceeded
  );

  List<JsonNode> REMOVE_NOT_EXISTS_NODE_TASK_EXPECTED_RESULTS = ImmutableList.of(
    Json.toJson(ImmutableMap.of("state", "Removing")),
    Json.toJson(ImmutableMap.of()),
    Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", false)),
    Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
    Json.toJson(ImmutableMap.of("state", "Removed")),
    Json.toJson(ImmutableMap.of())
  );

  private enum RemoveType {
    WITH_MASTER,
    ONLY_TSERVER,
    NOT_EXISTS
  };

  private void assertRemoveNodeSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                        RemoveType type) {
    int position = 0;
    switch (type) {
      case WITH_MASTER:
        for (TaskType taskType: REMOVE_NODE_WITH_MASTER) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          assertEquals(1, tasks.size());
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults =
              REMOVE_NODE_WITH_MASTER_RESULTS.get(position);
          List<JsonNode> taskDetails = tasks.stream()
              .map(t -> t.getTaskDetails())
              .collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
        }
      break;
      case ONLY_TSERVER:
        for (TaskType taskType: REMOVE_NODE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          assertEquals(1, tasks.size());
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults =
              REMOVE_NODE_TASK_EXPECTED_RESULTS.get(position);
          List<JsonNode> taskDetails = tasks.stream()
              .map(t -> t.getTaskDetails())
              .collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
        }
        break;
      case NOT_EXISTS:
        for (TaskType taskType: REMOVE_NOT_EXISTS_NODE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          assertEquals(1, tasks.size());
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults =
              REMOVE_NOT_EXISTS_NODE_TASK_EXPECTED_RESULTS.get(position);
          List<JsonNode> taskDetails = tasks.stream()
              .map(t -> t.getTaskDetails())
              .collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
        }
        break;
    }
  }

  @Test
  public void testRemoveNodeSuccess() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.ONLY_TSERVER);
  }

  @Test
  public void testRemoveNodeWithMaster() {
    Universe universe = createUniverse("RemNodeWithMaster");
    universe = Universe.saveDetails(universe.universeUUID,
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    // Change one of the nodes' state to removed.
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        Set<NodeDetails> nodes = universeDetails.nodeDetailsSet;
        for (NodeDetails node : nodes) {
          if (node.nodeName.equals("host-n1")) {
            node.isMaster = true;
            nodes.add(node);
            break;
          }
        }
        universeDetails.nodeDetailsSet = nodes;
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(universe.universeUUID, updater);
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = 3;
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.WITH_MASTER);
  }

  @Test
  public void testRemoveUnknownNode() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    TaskInfo taskInfo = submitTask(taskParams, "host-n9");
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
  }

  @Test
  public void testRemoveNonExistentNode() {
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    dummyShellResponse = new ShellProcessHandler.ShellResponse();
    dummyShellResponse.message = null;
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertRemoveNodeSequence(subTasksByPosition, RemoveType.NOT_EXISTS);
  }
}
