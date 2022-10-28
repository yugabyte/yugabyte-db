package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.YBClient;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class RebootNodeInUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  public void setUp(boolean withMaster, int numNodes, int replicationFactor) {
    super.setUp();

    Region region = Region.create(defaultProvider, "test-region", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    // Create default universe.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = numNodes;
    userIntent.provider = defaultProvider.uuid.toString();
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = replicationFactor;
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(
        defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, withMaster /* setMasters */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.universeUUID);

    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.List)) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                if (params.nodeUuid == null) {
                  listResponse.message = "{\"universe_uuid\":\"" + params.universeUUID + "\"}";
                } else {
                  listResponse.message =
                      "{\"universe_uuid\":\""
                          + params.universeUUID
                          + "\", "
                          + "\"node_uuid\": \""
                          + params.nodeUuid
                          + "\"}";
                }
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            });

    YBClient mockClient = mock(YBClient.class);
    try {
      doNothing().when(mockClient).waitForMasterLeader(anyLong());
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    } catch (Exception ignored) {
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
  }

  private List<TaskType> rebootNodeTaskSequence(boolean isHardReboot) {
    return ImmutableList.of(
        TaskType.SetNodeState,
        TaskType.AnsibleClusterServerCtl,
        isHardReboot ? TaskType.HardRebootServer : TaskType.RebootServer,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForServer,
        TaskType.WaitForServerReady,
        TaskType.SetNodeState,
        TaskType.UniverseUpdateSucceeded);
  }

  private List<JsonNode> rebootNodeTaskExpectedResults(boolean isHardReboot) {
    String state = isHardReboot ? "HardRebooting" : "Rebooting";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of("state", state)),
        Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", "Live")),
        Json.toJson(ImmutableMap.of()));
  }

  private List<TaskType> rebootNodeWithMaster(boolean isHardReboot) {
    return ImmutableList.of(
        TaskType.SetNodeState,
        TaskType.AnsibleClusterServerCtl,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForMasterLeader,
        isHardReboot ? TaskType.HardRebootServer : TaskType.RebootServer,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForServer,
        TaskType.WaitForServerReady,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForServer,
        TaskType.WaitForServerReady,
        TaskType.SetNodeState,
        TaskType.UniverseUpdateSucceeded);
  }

  private List<JsonNode> rebootNodeWithMasterResults(boolean isHardReboot) {
    String state = isHardReboot ? "HardRebooting" : "Rebooting";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of("state", state)),
        Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
        Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", "Live")),
        Json.toJson(ImmutableMap.of()));
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
    taskParams.nodeName = nodeName;
    try {
      UUID taskUUID = commissioner.submit(TaskType.RebootNodeInUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private enum RebootType {
    WITH_MASTER,
    ONLY_TSERVER
  }

  private void assertRebootNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, RebootType type, boolean isHardReboot) {
    int position = 0;
    int taskPosition = 0;
    switch (type) {
      case WITH_MASTER:
        for (TaskType taskType : rebootNodeWithMaster(isHardReboot)) {
          List<TaskInfo> tasks = subTasksByPosition.get(taskPosition);
          assertEquals(1, tasks.size());
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults = rebootNodeWithMasterResults(isHardReboot).get(position);
          List<JsonNode> taskDetails =
              tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
          taskPosition++;
        }
        break;
      case ONLY_TSERVER:
        for (TaskType taskType : rebootNodeTaskSequence(isHardReboot)) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          assertEquals(1, tasks.size());
          assertEquals(taskType, tasks.get(0).getTaskType());
          JsonNode expectedResults = rebootNodeTaskExpectedResults(isHardReboot).get(position);
          List<JsonNode> taskDetails =
              tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
          assertJsonEqual(expectedResults, taskDetails.get(0));
          position++;
          taskPosition++;
        }
        break;
    }
  }

  @Test
  @Parameters({"false", "true"})
  public void testRebootNodeWithNoMaster(boolean isHardReboot) {
    setUp(true, 6, 3);
    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = isHardReboot;

    TaskInfo taskInfo = submitTask(taskParams, "host-n4"); // Node with no master process.
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRebootNodeSequence(subTasksByPosition, RebootType.ONLY_TSERVER, isHardReboot);
  }

  @Test
  @Parameters({"false", "true"})
  public void testRebootNodeWithMaster(boolean isHardReboot) {
    setUp(true, 4, 3);
    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = isHardReboot;

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRebootNodeSequence(subTasksByPosition, RebootType.WITH_MASTER, isHardReboot);
  }
}
