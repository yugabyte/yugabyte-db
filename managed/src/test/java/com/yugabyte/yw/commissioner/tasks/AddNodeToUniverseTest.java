// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.VersionCheckMode.HA_ONLY;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.ListTabletServersResponse;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class AddNodeToUniverseTest extends UniverseModifyBaseTest {
  public static final Logger LOG = LoggerFactory.getLogger(AddNodeToUniverseTest.class);

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String DEFAULT_NODE_NAME = "host-n1";

  @Override
  @Before
  public void setUp() {
    super.setUp();

    ChangeMasterClusterConfigResponse ccr = new ChangeMasterClusterConfigResponse(1111, "", null);

    // Change one of the nodes' state to removed.
    setDefaultNodeState(defaultUniverse, NodeState.Removed, DEFAULT_NODE_NAME);
    setDefaultNodeState(onPremUniverse, NodeState.Removed, DEFAULT_NODE_NAME);

    // WaitForTServerHeartBeats mock.
    ListTabletServersResponse mockResponse = mock(ListTabletServersResponse.class);
    when(mockResponse.getTabletServersCount()).thenReturn(7);

    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(ccr);
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
      when(mockClient.listTabletServers()).thenReturn(mockResponse);
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
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
    } catch (Exception e) {
      fail();
    }

    mockWaits(mockClient, 4);
    when(mockClient.waitForLoadBalance(anyLong(), anyInt())).thenReturn(true);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  // Updates one of the nodes using a passed consumer.
  private Universe.UniverseUpdater getNodeUpdater(String nodeName, Consumer<NodeDetails> consumer) {
    return universe -> {
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      Set<NodeDetails> nodes = universeDetails.nodeDetailsSet;
      for (NodeDetails node : nodes) {
        if (node.nodeName.equals(nodeName)) {
          consumer.accept(node);
          break;
        }
      }
      universe.setUniverseDetails(universeDetails);
    };
  }

  private void setDefaultNodeState(
      Universe universe, final NodeState desiredState, String nodeName) {
    Universe.saveDetails(
        universe.getUniverseUUID(), getNodeUpdater(nodeName, node -> node.state = desiredState));
  }

  private void decomissionOnPremNode(String nodeName) {
    Universe.saveDetails(
        onPremUniverse.getUniverseUUID(),
        u -> {
          NodeDetails node = u.getNode(nodeName);
          node.state = NodeState.Decommissioned;
          NodeInstance.maybeGetByName(nodeName)
              .ifPresent(
                  nodeInstance -> {
                    nodeInstance.setInUse(false);
                    nodeInstance.setNodeName("");
                    nodeInstance.save();
                  });
        });
  }

  private TaskInfo submitTask(UUID universeUUID, String nodeName, int version) {
    return submitTask(universeUUID, defaultProvider, nodeName, version);
  }

  private TaskInfo submitTask(UUID universeUUID, Provider provider, String nodeName, int version) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.clusters.addAll(universe.getUniverseDetails().clusters);

    taskParams.expectedUniverseVersion = version;
    taskParams.nodeName = nodeName;
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.azUuid = AvailabilityZone.getByCode(provider, AZ_CODE).getUuid();
    taskParams.creatingUser = defaultUser;
    try {
      UUID taskUUID = commissioner.submit(TaskType.AddNodeToUniverse, taskParams);
      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);

      CustomerTask.create(
          defaultCustomer,
          universe.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.Add,
          DEFAULT_NODE_NAME);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<TaskType> ADD_NODE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.InstanceExistCheck, // only if it wasn't decommissioned.
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState, // to Adding
          TaskType.SetNodeStatus, // to Adding for 'To Be Added'
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.AnsibleSetupServer, // end provisioning
          TaskType.RunHooks,
          TaskType.AnsibleConfigureServers, // Gflags - master
          TaskType.AnsibleConfigureServers, // Gflags - tServer
          TaskType.SetNodeStatus, // ToJoinCluster
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl, // start process
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer,
          TaskType.WaitForServer, // wait for postgres to be up
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ModifyBlackList,
          TaskType.WaitForTServerHeartBeats,
          TaskType.SetNodeState,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> ADD_NODE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Adding")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()), // provisioned
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> ADD_NODE_TASK_DECOMISSIONED_NODE_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.AnsibleSetupServer,
          TaskType.RunHooks,
          TaskType.AnsibleConfigureServers, // Gflags - master
          TaskType.AnsibleConfigureServers, // Gflags - tServer
          TaskType.SetNodeStatus, // ToJoinCluster
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl, // start process (master/tServer)
          TaskType.UpdateNodeProcess, // update process name in DB
          TaskType.WaitForServer, // wait for process to come up
          TaskType.WaitForServer, // wait for postgres to be up
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ModifyBlackList,
          TaskType.WaitForTServerHeartBeats,
          TaskType.SetNodeState, // Live
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> ADD_NODE_TASK_DECOMISSIONED_NODE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> WITH_MASTER_UNDER_REPLICATED =
      ImmutableList.of(
          TaskType.InstanceExistCheck,
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.AnsibleSetupServer, // provisioned
          TaskType.RunHooks,
          TaskType.AnsibleConfigureServers, // install software, under-replicated
          TaskType.AnsibleConfigureServers, // GFlags master
          TaskType.AnsibleConfigureServers, // Gflags tServer
          TaskType.SetNodeStatus, // To join cluster
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.ChangeMasterConfig, // master done
          TaskType.CheckFollowerLag, // master done
          TaskType.WaitForServer,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.WaitForServer, // tServer
          TaskType.WaitForServer, // wait for postgres to be up
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ModifyBlackList,
          TaskType.WaitForTServerHeartBeats,
          TaskType.AnsibleConfigureServers, // add Master
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> WITH_MASTER_UNDER_REPLICATED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Adding")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", true)),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", true)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Live")),
          Json.toJson(ImmutableMap.of()));

  private void assertAddNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      boolean isNodeDecomissioned,
      boolean masterUnderReplicated) {
    int position = 0;
    List<TaskType> expectedTaskSequence = ADD_NODE_TASK_SEQUENCE;
    List<JsonNode> taskExpectedResults = ADD_NODE_TASK_EXPECTED_RESULTS;
    if (isNodeDecomissioned) {
      expectedTaskSequence = ADD_NODE_TASK_DECOMISSIONED_NODE_SEQUENCE;
      taskExpectedResults = ADD_NODE_TASK_DECOMISSIONED_NODE_EXPECTED_RESULTS;
    } else if (masterUnderReplicated) {
      expectedTaskSequence = WITH_MASTER_UNDER_REPLICATED;
      taskExpectedResults = WITH_MASTER_UNDER_REPLICATED_RESULTS;
    }
    for (TaskType expectedTaskType : expectedTaskSequence) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      // For some weird reason, the error interchanges expected and actual if I do not put expected
      // parameter later.
      assertEquals("At position: " + position, tasks.get(0).getTaskType(), expectedTaskType);
      JsonNode expectedResults = taskExpectedResults.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      assertJsonEqual(
          "At position: " + position + " taskType " + expectedTaskType,
          expectedResults,
          taskDetails.get(0));
      position++;
    }
  }

  @Test
  @Parameters({"true", "false"})
  public void testAddNodeSuccess(boolean isHAConfig) throws Exception {

    if (isHAConfig) {
      SettableRuntimeConfigFactory factory =
          app.injector().instanceOf(SettableRuntimeConfigFactory.class);
      factory.globalRuntimeConf().setValue("yb.universe_version_check_mode", HA_ONLY.name());
      HighAvailabilityConfig.create("clusterKey");
    }
    mockWaits(mockClient, 3);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    TaskInfo taskInfo = submitTask(defaultUniverse.getUniverseUUID(), DEFAULT_NODE_NAME, 3);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeManager, times(12)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertAddNodeSequence(subTasksByPosition, false, false);

    if (isHAConfig) {
      // In HA config mode, we expect any save of universe details to result in
      // a bump on the cluster config version. The actual number depends on the
      // number of invocations of saveUniverseDetails, so it can vary but the
      // important thing is that it is much more than the other case.
      // 7 version increments + 1 modify blacklist.
      verify(mockClient, times(13)).changeMasterClusterConfig(any());
    } else {
      verify(mockClient, times(1)).changeMasterClusterConfig(any());
    }
  }

  @Test
  public void testAddNodeOnPremSuccess() throws Exception {
    mockWaits(mockClient, 3);
    TaskInfo taskInfo =
        submitTask(onPremUniverse.getUniverseUUID(), onPremProvider, DEFAULT_NODE_NAME, 3);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeManager, times(12)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertAddNodeSequence(subTasksByPosition, false, false);
  }

  @Test
  public void testAddNodeOnPremSuccessForDecommissionedNode() throws Exception {
    mockWaits(mockClient, 4);
    decomissionOnPremNode(DEFAULT_NODE_NAME);
    TaskInfo taskInfo =
        submitTask(onPremUniverse.getUniverseUUID(), onPremProvider, DEFAULT_NODE_NAME, 4);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeManager, times(11)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertAddNodeSequence(subTasksByPosition, true, false);
  }

  @Test
  public void testAddNodeOnPrem_FailedPreflightCheck() throws Exception {
    mockWaits(mockClient, 4);
    preflightResponse.message = "{\"test\": false}";
    decomissionOnPremNode(DEFAULT_NODE_NAME);
    TaskInfo taskInfo =
        submitTask(onPremUniverse.getUniverseUUID(), onPremProvider, DEFAULT_NODE_NAME, 4);
    assertEquals(Failure, taskInfo.getTaskState());

    verify(mockNodeManager, times(1)).nodeCommand(any(), any());
    assertThat(
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.FreezeUniverse)
            .findFirst()
            .get()
            .getErrorMessage(),
        containsString(
            "Failed preflight checks for node host-n1. Code: 1. Output: {\"test\": false}"));

    // Node must not be reserved on failure.
    assertFalse(NodeInstance.maybeGetByName(DEFAULT_NODE_NAME).isPresent());
  }

  @Test
  public void testAddNodeWithUnderReplicatedMaster() {
    UniverseModifyBaseTest.mockMasterAndPeerRoles(
        mockClient, ImmutableList.of("10.0.0.1", "10.0.0.2", "10.0.0.3"));

    verify(mockNodeManager, never()).nodeCommand(any(), any());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        getNodeUpdater(DEFAULT_NODE_NAME, node -> node.isMaster = false));

    TaskInfo taskInfo = submitTask(defaultUniverse.getUniverseUUID(), DEFAULT_NODE_NAME, 4);
    assertEquals(Success, taskInfo.getTaskState());

    // 5 calls for setting up the server and then 6 calls for setting the conf files.
    verify(mockNodeManager, times(20)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertAddNodeSequence(subTasksByPosition, false, true);
  }

  @Test
  public void testAddUnknownNode() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> submitTask(defaultUniverse.getUniverseUUID(), "host-n9", 3));
    assertEquals("No node host-n9 is found in universe Test Universe", exception.getMessage());
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void testAddNodeWithUnderReplicatedMaster_WithReadOnlyCluster_NodeFromPrimary() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(false, 1));
    setDefaultGFlags(universe);

    // Change one of the nodes' state to removed.
    setDefaultNodeState(universe, NodeState.Removed, DEFAULT_NODE_NAME);

    TaskInfo taskInfo = submitTask(universe.getUniverseUUID(), DEFAULT_NODE_NAME, 4);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertAddNodeSequence(subTasksByPosition, false, true /* Master start is expected */);
  }

  @Test
  public void testAddNodeWithUnderReplicatedMaster_WithReadOnlyCluster_NodeFromReadReplica() {
    Universe universe = createUniverse("Demo");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor = 5;
            });
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(true, 1));
    setDefaultGFlags(universe);

    // Change one of the nodes' state to removed.
    setDefaultNodeState(universe, NodeState.Removed, "yb-tserver-0");

    TaskInfo taskInfo = submitTask(universe.getUniverseUUID(), "yb-tserver-0", 5);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertAddNodeSequence(subTasksByPosition, false, false /* Master start is unexpected */);
  }

  private void setDefaultGFlags(Universe universe) {
    Universe.UniverseUpdater updater =
        universe1 -> {
          UniverseDefinitionTaskParams universeDetails = universe1.getUniverseDetails();
          Map<String, String> gflags = new HashMap<>();
          gflags.put("foo", "bar");

          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          primaryCluster.userIntent.masterGFlags = gflags;
          primaryCluster.userIntent.tserverGFlags = gflags;

          List<Cluster> readOnlyClusters = universeDetails.getReadOnlyClusters();
          if (readOnlyClusters.size() > 0) {
            readOnlyClusters.get(0).userIntent.masterGFlags = gflags;
            readOnlyClusters.get(0).userIntent.tserverGFlags = gflags;
          }
        };
    Universe.saveDetails(universe.getUniverseUUID(), updater);
  }

  @Test
  public void testAddNodeAllowedState() {
    Set<NodeState> allowedStates = NodeState.allowedStatesForAction(NodeActionType.ADD);
    Set<NodeState> expectedStates =
        ImmutableSet.of(
            NodeState.Removed,
            NodeState.Decommissioned,
            NodeState.ToBeAdded,
            NodeState.InstanceCreated,
            NodeState.ServerSetup,
            NodeState.ToJoinCluster,
            NodeState.Provisioned,
            NodeState.SoftwareInstalled,
            NodeState.Adding);
    assertEquals(expectedStates, allowedStates);
  }

  @Test
  public void testAddNodeRetries() {
    // This is set up with under-replicated master to execute master addition flow.
    UniverseModifyBaseTest.mockMasterAndPeerRoles(
        mockClient, ImmutableList.of("10.0.0.1", "10.0.0.2", "10.0.0.3"));
    verify(mockNodeManager, never()).nodeCommand(any(), any());
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            getNodeUpdater(DEFAULT_NODE_NAME, node -> node.isMaster = false));

    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.clusters.addAll(universe.getUniverseDetails().clusters);
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.nodeName = DEFAULT_NODE_NAME;
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.azUuid = AvailabilityZone.getByCode(defaultProvider, AZ_CODE).getUuid();
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Add,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.AddNodeToUniverse,
        taskParams);
  }
}
