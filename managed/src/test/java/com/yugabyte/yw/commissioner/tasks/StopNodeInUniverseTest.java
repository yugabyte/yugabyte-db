// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class StopNodeInUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

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
    } catch (Exception e) {
      fail();
    }

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockMasterChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1112, "", null);
    GetLoadMovePercentResponse mockGetLoadMovePercentResponse =
        new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null);
    ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
    try {
      when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.getLeaderBlacklistCompletion()).thenReturn(mockGetLoadMovePercentResponse);
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.setFlag(any(), any(), any(), anyBoolean())).thenReturn(true);
    } catch (Exception e) {
      fail();
    }

    setUnderReplicatedTabletsMock();
    setLeaderlessTabletsMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeName = nodeName;
    try {
      UUID taskUUID = commissioner.submit(TaskType.StopNodeInUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<TaskType> STOP_NODE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ModifyBlackList,
          TaskType.UpdateNodeProcess,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> STOP_NODE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopping")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
          Json.toJson(ImmutableMap.of("state", "Stopped")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> STOP_NODE_WITH_YBC_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ModifyBlackList,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> STOP_NODE_WITH_YBC_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopping")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "controller", "command", "stop")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
          Json.toJson(ImmutableMap.of("state", "Stopped")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> STOP_NODE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ModifyBlackList,
          TaskType.UpdateNodeProcess,
          TaskType.ChangeMasterConfig,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeStatus,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> STOP_NODE_TASK_SEQUENCE_MASTER_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopping")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
          Json.toJson(ImmutableMap.of("opType", "RemoveMaster")),
          Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", false)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopped")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> STOP_NODE_WITH_YBC_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ModifyBlackList,
          TaskType.AnsibleClusterServerCtl,
          TaskType.UpdateNodeProcess,
          TaskType.ChangeMasterConfig,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeStatus,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> STOP_NODE_WITH_YBC_TASK_SEQUENCE_MASTER_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopping")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "controller", "command", "stop")),
          Json.toJson(ImmutableMap.of("processType", "TSERVER", "isAdd", false)),
          Json.toJson(ImmutableMap.of("opType", "RemoveMaster")),
          Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", false)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopped")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<TaskType> STOP_NODE_TASK_SEQUENCE_DEDICATED_MASTER =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeState,
          TaskType.ChangeMasterConfig,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeStatus,
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> STOP_NODE_DEDICATED_MASTER_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopping")),
          Json.toJson(ImmutableMap.of("opType", "RemoveMaster")),
          Json.toJson(ImmutableMap.of("process", "master", "command", "stop")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("processType", "MASTER", "isAdd", false)),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("state", "Stopped")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertStopNodeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, boolean isMaster, boolean isYbcConfigured) {
    if (!isYbcConfigured) {
      if (isMaster) {
        assertTasks(
            STOP_NODE_TASK_SEQUENCE_MASTER,
            STOP_NODE_TASK_SEQUENCE_MASTER_RESULTS,
            subTasksByPosition);
      } else {
        assertTasks(STOP_NODE_TASK_SEQUENCE, STOP_NODE_TASK_EXPECTED_RESULTS, subTasksByPosition);
      }
    } else {
      if (isMaster) {
        assertTasks(
            STOP_NODE_WITH_YBC_TASK_SEQUENCE_MASTER,
            STOP_NODE_WITH_YBC_TASK_SEQUENCE_MASTER_RESULTS,
            subTasksByPosition);
      } else {
        assertTasks(
            STOP_NODE_WITH_YBC_TASK_SEQUENCE,
            STOP_NODE_WITH_YBC_TASK_EXPECTED_RESULTS,
            subTasksByPosition);
      }
    }
  }

  private void assertTasks(
      List<TaskType> sequence,
      List<JsonNode> details,
      Map<Integer, List<TaskInfo>> subTasksByPosition) {
    if (subTasksByPosition.size() != sequence.size()) {
      log.debug("Expected:");
      for (TaskType taskType : sequence) {
        log.debug("" + taskType);
      }
      log.debug("Actual:");
      subTasksByPosition.keySet().stream()
          .sorted()
          .forEach(
              position -> {
                log.debug("" + subTasksByPosition.get(position).get(0).getTaskType());
              });
    }
    assertEquals(subTasksByPosition.size(), sequence.size());
    int position = 0;
    for (TaskType taskType : sequence) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = details.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Test
  public void testStopMasterNode() {
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    NodeDetails node =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNode("host-n1");
    assertFalse(node.isTserver);
    assertFalse(node.isMaster);

    verify(mockNodeManager, times(7)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertStopNodeSequence(subTasksByPosition, true, false);
  }

  @Test
  public void testStopMasterNodeWithYbc() {
    Customer customer = ModelFactory.testCustomer("tc3", "Test Customer 3");
    Universe universe =
        createUniverse(
            "Test Universe 2",
            UUID.randomUUID(),
            customer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.16.7.0-b1";
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY,
            userIntent,
            userIntent.replicationFactor,
            null,
            Collections.emptyList());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent,
                "host",
                true /* setMasters */,
                false /* updateInProgress */,
                placementInfo,
                true /* enableYbc */));
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    TaskInfo taskInfo = submitTask(taskParams, "host-n1");
    assertEquals(Success, taskInfo.getTaskState());

    NodeDetails node = Universe.getOrBadRequest(universe.getUniverseUUID()).getNode("host-n1");
    assertFalse(node.isTserver);
    assertFalse(node.isMaster);

    verify(mockNodeManager, times(8)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertStopNodeSequence(subTasksByPosition, true, true);
  }

  @Test
  public void testStopNonMasterNode() {
    Customer customer = ModelFactory.testCustomer("tc4", "Test Customer 4");
    Universe universe = createUniverse(customer.getId());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 5;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.16.7.0-b1";
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Success, taskInfo.getTaskState());

    NodeDetails node = Universe.getOrBadRequest(universe.getUniverseUUID()).getNode("host-n4");
    assertFalse(node.isTserver);
    assertFalse(node.isMaster);

    verify(mockNodeManager, times(2)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertStopNodeSequence(subTasksByPosition, false, false);
  }

  @Test
  public void testStopNonMasterNodeWithYBC() {
    Customer customer = ModelFactory.testCustomer("tc2", "Test Customer 2");
    Universe universe =
        createUniverse(
            "Test Universe", UUID.randomUUID(), customer.getId(), CloudType.aws, null, null, true);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 5;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.16.7.0-b1";
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY,
            userIntent,
            userIntent.replicationFactor,
            null,
            Collections.emptyList());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent,
                "host",
                true /* setMasters */,
                false /* updateInProgress */,
                placementInfo,
                true /* enableYbc */));

    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    TaskInfo taskInfo = submitTask(taskParams, "host-n4");
    assertEquals(Success, taskInfo.getTaskState());

    NodeDetails node = Universe.getOrBadRequest(universe.getUniverseUUID()).getNode("host-n4");
    assertFalse(node.isTserver);
    assertFalse(node.isMaster);

    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertStopNodeSequence(subTasksByPosition, false, true);
  }

  @Test
  public void testStopUnknownNode() {
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    // Throws at validateParams check.
    assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, "host-n9"));
  }

  @Test
  public void testStopDedicatedMasterNode() {
    AtomicReference<String> nodeName = new AtomicReference<>();
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
                  "2.16.7.0-b1";
              NodeDetails node =
                  u.getUniverseDetails().nodeDetailsSet.stream()
                      .filter(n -> n.isMaster)
                      .findFirst()
                      .get();
              node.dedicatedTo = UniverseDefinitionTaskBase.ServerType.MASTER;
              node.isMaster = true;
              node.isTserver = false;
              nodeName.set(node.getNodeName());
            },
            false);

    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());

    TaskInfo taskInfo = submitTask(taskParams, nodeName.get());
    assertEquals(Success, taskInfo.getTaskState());

    NodeDetails node =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNode(nodeName.get());
    assertFalse(node.isTserver);
    assertFalse(node.isMaster);

    verify(mockNodeManager, times(6)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTasks(
        STOP_NODE_TASK_SEQUENCE_DEDICATED_MASTER,
        STOP_NODE_DEDICATED_MASTER_EXPECTED_RESULTS,
        subTasksByPosition);
  }

  @Test
  public void testStopNodeInUniverseRetries() {
    Customer customer = ModelFactory.testCustomer("tc3", "Test Customer 3");
    Universe universe =
        createUniverse(
            "Test Universe 2",
            UUID.randomUUID(),
            customer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.16.7.0-b1";
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY,
            userIntent,
            userIntent.replicationFactor,
            null,
            Collections.emptyList());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent,
                "host",
                true /* setMasters */,
                false /* updateInProgress */,
                placementInfo,
                true /* enableYbc */));
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.nodeName = "host-n1";
    super.verifyTaskRetries(
        customer,
        CustomerTask.TaskType.Stop,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.StopNodeInUniverse,
        taskParams);
  }
}
