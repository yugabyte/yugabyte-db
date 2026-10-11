package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClientApi;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class RebootNodeInUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  public void setUp(boolean withMaster, int numNodes, int replicationFactor) {
    setUp(withMaster, numNodes, replicationFactor, false);
  }

  public void setUp(boolean withMaster, int numNodes, int replicationFactor, boolean enableYbc) {
    super.setUpBase();

    Region region = Region.create(defaultProvider, "test-region", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    // Create default universe.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = numNodes;
    userIntent.ybSoftwareVersion = "2.21.1.1-b1";
    userIntent.replicationFactor = replicationFactor;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    TestUtils.initUserIntent(
        userIntent,
        defaultProvider,
        ApiUtils.UTIL_INST_TYPE,
        ApiUtils.getDummyDeviceInfo(1, 100),
        "demo-access");

    defaultUniverse = createUniverse(defaultCustomer.getId());
    if (enableYbc) {
      PlacementInfo placementInfo =
          PlacementInfoUtil.getPlacementInfo(
              ClusterType.PRIMARY,
              userIntent,
              userIntent.replicationFactor,
              null,
              Collections.emptyList());
      Universe.saveDetails(
          defaultUniverse.getUniverseUUID(),
          ApiUtils.mockUniverseUpdater(
              userIntent,
              "host",
              withMaster /* setMasters */,
              false /* updateInProgress */,
              placementInfo,
              true /* enableYbc */));
    } else {
      Universe.saveDetails(
          defaultUniverse.getUniverseUUID(),
          ApiUtils.mockUniverseUpdater(userIntent, withMaster /* setMasters */));
    }
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
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

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockMasterChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1112, "", null);
    GetLoadMovePercentResponse mockGetLoadMovePercentResponse =
        new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null);

    YBClientApi mockClient = mock(YBClientApi.class);
    try {
      doNothing().when(mockClient).waitForMasterLeader(anyLong());
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
      IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean())).thenReturn(okReadyResp);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.getLeaderBlacklistCompletion()).thenReturn(mockGetLoadMovePercentResponse);
    } catch (Exception e) {
      fail();
    }
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    setLeaderlessTabletsMock();
    setUnderReplicatedTabletsMock();
    setCheckNodesAreSafeToTakeDown(mockClient);
    setFollowerLagMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));

    // Comprehensive prechecks add CheckNodeCommandExecution; this class asserts exact sequences.
    factory
        .globalRuntimeConf()
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
  }

  private void mockListWithNoInstance() {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                if (params.nodeUuid != null) {
                  listResponse.message = "";
                } else {
                  listResponse.message = "{\"universe_uuid\":\"" + params.getUniverseUUID() + "\"}";
                }
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
  }

  private void mockListWithInstanceNotRunning() {
    AtomicBoolean hardRebooted = new AtomicBoolean(false);
    doAnswer(
            invocation -> {
              NodeManager.NodeCommandType commandType = invocation.getArgument(0);
              if (NodeManager.NodeCommandType.Hard_Reboot.equals(commandType)) {
                hardRebooted.set(true);
                return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
              }
              if (NodeManager.NodeCommandType.List.equals(commandType)) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                if (params.nodeUuid == null) {
                  listResponse.message = "{\"universe_uuid\":\"" + params.getUniverseUUID() + "\"}";
                } else {
                  boolean isRunning = hardRebooted.get();
                  listResponse.message =
                      "{\"universe_uuid\":\""
                          + params.getUniverseUUID()
                          + "\", "
                          + "\"node_uuid\": \""
                          + params.nodeUuid
                          + "\", "
                          + "\"is_running\": "
                          + isRunning
                          + "}";
                }
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
  }

  private void mockCommandExecutionSuccess() {
    lenient()
        .when(
            mockNodeUniverseManager.runCommand(
                any(), any(), eq(Arrays.asList("echo", "command-execution-test")), any()))
        .thenReturn(ShellResponse.create(0, "Command output:\ncommand-execution-test"));
  }

  private void mockCommandExecutionFailure() {
    doReturn(ShellResponse.create(1, "command failed"))
        .when(mockNodeUniverseManager)
        .runCommand(any(), any(), eq(Arrays.asList("echo", "command-execution-test")), any());
  }

  private List<TaskType> rebootNodeTaskSequence(boolean isHardReboot) {
    return ImmutableList.of(
        TaskType.CheckLeaderlessTablets,
        TaskType.CheckUnderReplicatedTablets,
        TaskType.CheckNodesAreSafeToTakeDown,
        TaskType.UpdateConsistencyCheck,
        TaskType.FreezeUniverse,
        TaskType.SetNodeState,
        TaskType.ModifyBlackList,
        TaskType.WaitForLeaderBlacklistCompletion,
        TaskType.AnsibleClusterServerCtl,
        isHardReboot ? TaskType.HardRebootServer : TaskType.RebootServer,
        TaskType.AnsibleClusterServerCtl,
        TaskType.WaitForServer,
        TaskType.WaitForServerReady,
        TaskType.ModifyBlackList,
        TaskType.SetNodeState,
        TaskType.UniverseUpdateSucceeded);
  }

  private List<JsonNode> rebootNodeTaskExpectedResults(boolean isHardReboot) {
    String state = isHardReboot ? "HardRebooting" : "Rebooting";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", state)),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", "Live")),
        Json.toJson(ImmutableMap.of()));
  }

  private List<TaskType> rebootNodeWithMaster(boolean isHardReboot) {
    return ImmutableList.of(
        TaskType.CheckLeaderlessTablets,
        TaskType.CheckUnderReplicatedTablets,
        TaskType.CheckNodesAreSafeToTakeDown,
        TaskType.UpdateConsistencyCheck,
        TaskType.FreezeUniverse,
        TaskType.SetNodeState,
        TaskType.ModifyBlackList,
        TaskType.WaitForLeaderBlacklistCompletion,
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
        TaskType.ModifyBlackList,
        TaskType.SetNodeState,
        TaskType.UniverseUpdateSucceeded);
  }

  private List<JsonNode> rebootNodeWithMasterResults(boolean isHardReboot) {
    String state = isHardReboot ? "HardRebooting" : "Rebooting";
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", state)),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
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
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("state", "Live")),
        Json.toJson(ImmutableMap.of()));
  }

  private List<TaskType> rebootNodeWithOnlyMaster(boolean isHardReboot) {
    return ImmutableList.of(
        TaskType.CheckLeaderlessTablets,
        TaskType.CheckNodesAreSafeToTakeDown,
        TaskType.UpdateConsistencyCheck,
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
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
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
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
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
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }

  @Test
  public void testRebootFailsWhenInstanceMissing() {
    setUp(true, 6, 3);
    mockListWithNoInstance();

    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Failure, taskInfo.getTaskState());
    assertFalse(
        taskInfo.getSubTasks().stream().anyMatch(t -> t.getTaskType() == TaskType.FreezeUniverse));
    assertTrue(
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .updateSucceeded);
  }

  @Test
  public void testSoftRebootRunsCheckNodeCommandExecutionWhenFlagEnabled() {
    setUp(true, 6, 3);
    mockCommandExecutionSuccess();
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "true");

    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = false;

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckNodeCommandExecution));
  }

  @Test
  public void testSoftRebootFailsWhenNodeUnreachable() {
    setUp(true, 6, 3);
    mockCommandExecutionFailure();
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "true");

    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = false;

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Failure, taskInfo.getTaskState());
    assertFalse(
        taskInfo.getSubTasks().stream().anyMatch(t -> t.getTaskType() == TaskType.RebootServer));
    NodeDetails node =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNode("host-n4");
    assertEquals(NodeDetails.NodeState.Live, node.state);
  }

  @Test
  public void testHardRebootSkipsCheckNodeCommandExecution() {
    setUp(true, 6, 3);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "true");

    RebootNodeInUniverse.Params taskParams = new RebootNodeInUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = true;

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckNodeCommandExecution));
  }

  @Test
  public void testHardRebootSkipsYbcStopOnStoppedVm() throws Exception {
    setUp(true, 6, 3, true);
    mockListWithInstanceNotRunning();
    doNothing().when(mockYbcManager).waitForYbc(any(), any(), anyInt());

    RebootNodeInUniverse.Params taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), RebootNodeInUniverse.Params.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.isHardReboot = true;

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Success, taskInfo.getTaskState());

    TaskInfo controllerStop =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.AnsibleClusterServerCtl)
            .filter(
                t -> {
                  JsonNode params = t.getTaskParams();
                  return params.has("process")
                      && "controller".equals(params.get("process").asText())
                      && params.has("command")
                      && "stop".equals(params.get("command").asText());
                })
            .findFirst()
            .orElseThrow(() -> new AssertionError("controller stop task not found"));
    assertTrue(controllerStop.getTaskParams().get("skipStopForPausedVM").asBoolean());
  }
}
