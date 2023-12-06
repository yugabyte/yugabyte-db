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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
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
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.ybSoftwareVersion = "2.16.7.0-b1";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = replicationFactor;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, withMaster /* setMasters */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

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
        TaskType.FreezeUniverse,
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
        Json.toJson(ImmutableMap.of()),
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
        TaskType.FreezeUniverse,
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
        Json.toJson(ImmutableMap.of()),
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

  private List<TaskType> rebootNodeWithOnlyMaster(boolean isHardReboot) {
    return ImmutableList.of(
        TaskType.FreezeUniverse,
        TaskType.SetNodeState,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForMasterLeader,
        isHardReboot ? TaskType.HardRebootServer : TaskType.RebootServer,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForServer,
        TaskType.WaitForServerReady,
        TaskType.SetNodeState,
        TaskType.UniverseUpdateSucceeded);
  }

  private List<JsonNode> rebootNodeWithOnlyMasterResults(boolean isHardReboot) {
    String state = isHardReboot ? "HardRebooting" : "Rebooting";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", state)),
        Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
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
    WITH_MASTER_NO_TSERVER,
    WITH_MASTER,
    ONLY_TSERVER
  }

  private void assertRebootNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, RebootType type, boolean isHardReboot) {
    switch (type) {
      case WITH_MASTER_NO_TSERVER:
        assertTaskSequence(
            rebootNodeWithOnlyMaster(isHardReboot),
            rebootNodeWithOnlyMasterResults(isHardReboot),
            subTasksByPosition);
        break;
      case WITH_MASTER:
        assertTaskSequence(
            rebootNodeWithMaster(isHardReboot),
            rebootNodeWithMasterResults(isHardReboot),
            subTasksByPosition);
        break;
      case ONLY_TSERVER:
        assertTaskSequence(
            rebootNodeTaskSequence(isHardReboot),
            rebootNodeTaskExpectedResults(isHardReboot),
            subTasksByPosition);
        break;
    }
  }

  private void assertTaskSequence(
      List<TaskType> taskTypes,
      List<JsonNode> jsonNodes,
      Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    int taskPosition = 0;
    for (TaskType taskType : taskTypes) {
      List<TaskInfo> tasks = subTasksByPosition.get(taskPosition);
      assertEquals(1, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = jsonNodes.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
      taskPosition++;
    }
  }

  @Test
  @Parameters({"false", "true"})
  public void testRebootNodeWithNoMaster(boolean isHardReboot) {
    setUp(true, 6, 3);
    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
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
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = isHardReboot;

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRebootNodeSequence(subTasksByPosition, RebootType.WITH_MASTER, isHardReboot);
  }

  @Test
  @Parameters({"false", "true"})
  public void testRebootNodeWithMasterAndNoTserver(boolean isHardReboot) {
    setUp(true, 4, 3);
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          universe.getUniverseDetails().nodeDetailsSet.stream()
              .filter(n -> n.nodeName.equals("host-n1"))
              .forEach(n -> n.isTserver = false);
        });
    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 3;
    taskParams.isHardReboot = isHardReboot;

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertRebootNodeSequence(subTasksByPosition, RebootType.WITH_MASTER_NO_TSERVER, isHardReboot);
  }

  @Test
  @Parameters({"false", "true"})
  public void testRebootNodeRetries(boolean isHardReboot) {
    // Set up with master.
    setUp(true, 4, 3);
    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = isHardReboot;
    taskParams.nodeName = "host-n1";
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Reboot,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RebootNodeInUniverse,
        taskParams);
  }
}
