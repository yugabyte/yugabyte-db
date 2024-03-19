// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class PauseUniverseTest extends CommissionerBaseTest {
  private Universe defaultUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
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
  }

  private void setupUniverse(boolean updateInProgress) {
    Region r = Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            defaultProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, defaultProvider, i, 1);
    userIntent.replicationFactor = 1;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";

    defaultUniverse = createUniverse(defaultCustomer.getId());
    String nodePrefix = "demo-universe";
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(
            userIntent, nodePrefix, true /* setMasters */, updateInProgress));
  }

  private static final List<TaskType> PAUSE_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.PauseServer,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ManageAlertDefinitions,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> PAUSE_UNIVERSE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertTaskSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType : PAUSE_UNIVERSE_TASKS) {
      JsonNode expectedResults = PAUSE_UNIVERSE_EXPECTED_RESULTS.get(position);
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(taskType, tasks.get(0).getTaskType());
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(PauseUniverse.Params taskParams) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    try {
      UUID taskUUID = commissioner.submit(TaskType.PauseUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testPauseUniverseSuccess() {
    setupUniverse(false);
    PauseUniverse.Params taskParams = new PauseUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition);
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testPauseUniverseWithUpdateInProgress() {
    setupUniverse(true);
    PauseUniverse.Params taskParams = new PauseUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    PlatformServiceException thrown =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));
    assertThat(thrown.getMessage(), containsString("is already being updated"));
  }

  @Test
  public void testPauseUniverseSuccessWithNodesAlreadyStopped() {
    setupUniverse(false);
    PauseUniverse.Params taskParams = new PauseUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    for (NodeDetails node : defaultUniverse.getNodes()) {
      node.state = NodeDetails.NodeState.Stopped;
      defaultUniverse.save();
    }
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition);
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }
}
