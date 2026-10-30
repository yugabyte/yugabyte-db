// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Aborted;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.clearInvocations;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.ListTabletServersResponse;
import play.libs.Json;

/**
 * Commissioner-level tests for {@link RollbackAddNodeToUniverse}. Computer/API coverage lives in
 * {@code AddNodeToUniverseRollbackComputerTest} / {@code CustomerTaskManagerTest}.
 */
public class RollbackAddNodeToUniverseTest extends UniverseModifyBaseTest {

  private static final String DEFAULT_NODE_NAME = "host-n1";

  private SettableRuntimeConfigFactory factory;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    factory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    ChangeMasterClusterConfigResponse ccr = new ChangeMasterClusterConfigResponse(1111, "", null);
    ListTabletServersResponse mockResponse = mock(ListTabletServersResponse.class);
    when(mockResponse.getTabletServersCount()).thenReturn(7);
    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(ccr);
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
      when(mockClient.listTabletServers()).thenReturn(mockResponse);
      ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
      when(listMastersResponse.getPeersList()).thenReturn(Collections.emptyList());
      when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
      mockClockSyncResponse(mockNodeUniverseManager);
      mockLocaleCheckResponse(mockNodeUniverseManager);
      mockDbNodePortConnectivityResponse(mockNodeUniverseManager);
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
      when(mockClient.waitForLoadBalance(anyLong(), anyInt())).thenReturn(true);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    mockWaits(mockClient, 4);
    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  private void enableAddNodeRollback(Universe universe) {
    factory.globalRuntimeConf().setValue("yb.task.allow_add_node_rollback", "true");
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
  }

  private void setNodeState(Universe universe, NodeState state, String nodeName) {
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          NodeDetails node = u.getNode(nodeName);
          node.state = state;
        });
  }

  private void decomissionOnPremNode(String nodeName) {
    Universe.saveDetails(
        onPremUniverse.getUniverseUUID(),
        u -> {
          NodeDetails node = u.getNode(nodeName);
          node.state = NodeState.Decommissioned;
          NodeInstance.maybeGetByName(nodeName, node.nodeUuid)
              .ifPresent(
                  nodeInstance -> {
                    nodeInstance.setState(NodeInstance.State.FREE);
                    nodeInstance.setNodeName("");
                    nodeInstance.save();
                  });
        });
  }

  private NodeTaskParams buildAddParams(Universe universe, Provider provider, String nodeName) {
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodeName = nodeName;
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.azUuid = AvailabilityZone.getByCode(provider, AZ_CODE).getUuid();
    taskParams.creatingUser = defaultUser;
    return taskParams;
  }

  private TaskInfo submitAddAndAbort(
      Universe universe, Provider provider, String nodeName, int offsetFromMark)
      throws InterruptedException {
    TestUtils.setFakeHttpContext(defaultUser);
    setPausePosition(0);
    try {
      UUID taskUUID =
          commissioner.submit(
              TaskType.AddNodeToUniverse, buildAddParams(universe, provider, nodeName));
      CustomerTask.create(
          defaultCustomer,
          universe.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.Add,
          nodeName);
      waitForTaskPaused(taskUUID);
      // Subtasks after FreezeUniverse are only created once the freeze has run.
      setPausePosition(firstPosition(taskUUID, TaskType.FreezeUniverse) + 1);
      commissioner.resumeTask(taskUUID);
      waitForTaskPaused(taskUUID);
      setAbortPosition(firstPosition(taskUUID, TaskType.MarkRollbackUnsafe) + offsetFromMark);
      commissioner.resumeTask(taskUUID);
      return waitForTask(taskUUID);
    } finally {
      clearAbortOrPausePositions();
    }
  }

  private int firstPosition(UUID taskUUID, TaskType taskType) {
    return TaskInfo.getOrBadRequest(taskUUID).getSubTasks().stream()
        .filter(t -> t.getTaskType() == taskType)
        .mapToInt(TaskInfo::getPosition)
        .min()
        .orElseThrow(() -> new AssertionError("no " + taskType + " subtask"));
  }

  private TaskInfo submitRollback(TaskInfo failedAdd, Universe universe)
      throws InterruptedException {
    NodeTaskParams rollbackParams = Json.fromJson(failedAdd.getTaskParams(), NodeTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    UUID rollbackUuid = commissioner.submit(TaskType.RollbackAddNodeToUniverse, rollbackParams);
    CustomerTask.create(
        defaultCustomer,
        universe.getUniverseUUID(),
        rollbackUuid,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.RollbackAddNodeToUniverse,
        rollbackParams.nodeName);
    return waitForTask(rollbackUuid);
  }

  private void stubNodeManagerOkWithInstancesPresent() {
    ShellResponse ok = new ShellResponse();
    ok.message = "";
    ShellResponse listWithInstance = new ShellResponse();
    listWithInstance.message = "[{\"id\":\"i-mock\"}]";
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                return listWithInstance;
              }
              return ok;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
  }

  @Test
  public void testRollbackAddNodeNotRollbackable() {
    assertFalse(Commissioner.canTaskTypeRollback(TaskType.RollbackAddNodeToUniverse));
  }

  @Test
  public void testRollbackRemovedHappyPathAfterAbortBeforeCheckpoint() throws Exception {
    setNodeState(defaultUniverse, NodeState.Removed, DEFAULT_NODE_NAME);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    enableAddNodeRollback(universe);

    TaskInfo failedAdd = submitAddAndAbort(universe, defaultProvider, DEFAULT_NODE_NAME, 0);
    assertEquals(Aborted, failedAdd.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertTrue(commissioner.canTaskRollbackDetailed(failedAdd));

    clearInvocations(mockNodeManager);
    stubNodeManagerOkWithInstancesPresent();
    TaskInfo rollbackInfo = submitRollback(failedAdd, universe);
    assertEquals(Success, rollbackInfo.getTaskState());

    List<TaskType> types =
        rollbackInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());
    assertTrue(types.contains(TaskType.AnsibleClusterServerCtl));
    assertFalse(types.contains(TaskType.AnsibleDestroyServer));
    assertTrue(types.contains(TaskType.RestoreUniverseDetailsFromDelta));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    NodeDetails node = universe.getNode(DEFAULT_NODE_NAME);
    assertEquals(NodeState.Removed, node.state);
    assertNull(universe.getStateTransitionDetails());
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    verify(mockNodeManager, never()).nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());
  }

  @Test
  public void testRollbackDecommissionedHappyPathAfterAbortBeforeCheckpoint() throws Exception {
    decomissionOnPremNode(DEFAULT_NODE_NAME);
    Universe universe = Universe.getOrBadRequest(onPremUniverse.getUniverseUUID());
    enableAddNodeRollback(universe);

    TaskInfo failedAdd = submitAddAndAbort(universe, onPremProvider, DEFAULT_NODE_NAME, 0);
    assertEquals(Aborted, failedAdd.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());

    clearInvocations(mockNodeManager);
    stubNodeManagerOkWithInstancesPresent();
    TaskInfo rollbackInfo = submitRollback(failedAdd, universe);
    assertEquals(Success, rollbackInfo.getTaskState());

    List<TaskType> types =
        rollbackInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());
    assertTrue(types.contains(TaskType.AnsibleDestroyServer));
    assertTrue(types.contains(TaskType.RestoreUniverseDetailsFromDelta));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    NodeDetails node = universe.getNode(DEFAULT_NODE_NAME);
    assertEquals(NodeState.Decommissioned, node.state);
    assertNull(universe.getStateTransitionDetails());
    verify(mockNodeAgentClient, atLeastOnce()).runDestroyServer(any(), any(), any());
  }

  @Test
  public void testRollbackRejectedAfterMarkRollbackUnsafe() throws Exception {
    setNodeState(defaultUniverse, NodeState.Removed, DEFAULT_NODE_NAME);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    enableAddNodeRollback(universe);

    TaskInfo failedAdd = submitAddAndAbort(universe, defaultProvider, DEFAULT_NODE_NAME, 1);
    assertEquals(Aborted, failedAdd.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
    assertFalse(commissioner.canTaskRollbackDetailed(failedAdd));

    TaskInfo rollbackInfo = submitRollback(failedAdd, universe);
    assertEquals(Failure, rollbackInfo.getTaskState());
  }

  @Test
  public void testRollbackAddNodeRetriesRemoved() throws Exception {
    setNodeState(defaultUniverse, NodeState.Removed, DEFAULT_NODE_NAME);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    enableAddNodeRollback(universe);

    TaskInfo failedAdd = submitAddAndAbort(universe, defaultProvider, DEFAULT_NODE_NAME, 0);
    assertEquals(Aborted, failedAdd.getTaskState());

    stubNodeManagerOkWithInstancesPresent();
    NodeTaskParams rollbackParams = Json.fromJson(failedAdd.getTaskParams(), NodeTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RollbackAddNodeToUniverse,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.RollbackAddNodeToUniverse,
        rollbackParams);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(NodeState.Removed, universe.getNode(DEFAULT_NODE_NAME).state);
  }

  @Test
  public void testRollbackAddNodeRetriesDecommissioned() throws Exception {
    decomissionOnPremNode(DEFAULT_NODE_NAME);
    Universe universe = Universe.getOrBadRequest(onPremUniverse.getUniverseUUID());
    enableAddNodeRollback(universe);

    TaskInfo failedAdd = submitAddAndAbort(universe, onPremProvider, DEFAULT_NODE_NAME, 0);
    assertEquals(Aborted, failedAdd.getTaskState());

    stubNodeManagerOkWithInstancesPresent();
    NodeTaskParams rollbackParams = Json.fromJson(failedAdd.getTaskParams(), NodeTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RollbackAddNodeToUniverse,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.RollbackAddNodeToUniverse,
        rollbackParams);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(NodeState.Decommissioned, universe.getNode(DEFAULT_NODE_NAME).state);
  }
}
