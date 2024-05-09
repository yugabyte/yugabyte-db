// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.contains;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Iterables;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.pekko.japi.function.Predicate;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class EditUniverseTest extends UniverseModifyBaseTest {

  private static final List<TaskType> UNIVERSE_EXPAND_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeStatus, // ToBeAdded to Adding
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.AnsibleSetupServer,
          TaskType.RunHooks,
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.ModifyBlackList,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServer, // check if postgres is up
          TaskType.ModifyBlackList,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForLeadersOnPreferredOnly,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.ChangeMasterConfig, // Add
          TaskType.CheckFollowerLag, // Add
          TaskType.WaitForMasterLeader,
          TaskType.AnsibleConfigureServers, // Tservers
          TaskType.AnsibleConfigureServers, // Masters
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.ChangeMasterConfig, // Remove
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers, // Tservers
          TaskType.AnsibleConfigureServers, // Masters
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.AnsibleClusterServerCtl, // Stop master
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UpdateUniverseIntent,
          TaskType.WaitForTServerHeartBeats,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> UNIVERSE_EXPAND_TASK_SEQUENCE_ON_PREM =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.PreflightNodeCheck,
          TaskType.FreezeUniverse,
          TaskType.SetNodeStatus, // ToBeAdded to Adding
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.AnsibleSetupServer,
          TaskType.RunHooks,
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.ModifyBlackList,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServer, // check if postgres is up
          TaskType.ModifyBlackList,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForLeadersOnPreferredOnly,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.ChangeMasterConfig, // Add
          TaskType.CheckFollowerLag, // Add
          TaskType.WaitForMasterLeader,
          TaskType.AnsibleConfigureServers, // Tservers
          TaskType.AnsibleConfigureServers, // Masters
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.ChangeMasterConfig, // Remove
          TaskType.WaitForMasterLeader,
          TaskType.UpdateNodeProcess,
          TaskType.AnsibleConfigureServers, // Tservers
          TaskType.AnsibleConfigureServers, // Masters
          TaskType.SetFlagInMemory,
          TaskType.SetFlagInMemory,
          TaskType.AnsibleClusterServerCtl, // Stop master
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UpdateUniverseIntent,
          TaskType.WaitForTServerHeartBeats,
          TaskType.UniverseUpdateSucceeded);

  private void assertTaskSequence(
      List<TaskType> sequence, Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    assertEquals(sequence.size(), subTasksByPosition.size());
    for (TaskType taskType : sequence) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertTrue(tasks.size() > 0);
      assertEquals(taskType, tasks.get(0).getTaskType());
      position++;
    }
  }

  @Override
  @Before
  public void setUp() {
    super.setUp();

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockMasterChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1111, "", null);
    ChangeConfigResponse mockChangeConfigResponse = mock(ChangeConfigResponse.class);
    ListTabletServersResponse mockListTabletServersResponse = mock(ListTabletServersResponse.class);
    when(mockListTabletServersResponse.getTabletServersCount()).thenReturn(10);

    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.changeMasterConfig(
              anyString(), anyInt(), anyBoolean(), anyBoolean(), anyString()))
          .thenReturn(mockChangeConfigResponse);
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
          .thenReturn(Boolean.TRUE);
      when(mockClient.listTabletServers()).thenReturn(mockListTabletServersResponse);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
      when(mockClient.waitForAreLeadersOnPreferredOnlyCondition(anyLong())).thenReturn(true);
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

      mockLocaleCheckResponse(mockNodeUniverseManager);

      when(mockClient.getLoadMoveCompletion())
          .thenReturn(new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null));
    } catch (Exception e) {
      fail();
    }
    mockWaits(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    setFollowerLagMock();
    setLeaderlessTabletsMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testEditTags() throws JsonProcessingException {
    Universe universe = defaultUniverse;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  ImmutableMap.of("q", "v", "q1", "v1", "q3", "v3");
            });
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    Map<String, String> newTags = ImmutableMap.of("q", "vq", "q2", "v2");
    taskParams.getPrimaryCluster().userIntent.instanceTags = newTags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    List<TaskInfo> instanceActions = subTasksByPosition.get(2);
    assertEquals(
        new ArrayList<>(
            Arrays.asList(
                TaskType.InstanceActions, TaskType.InstanceActions, TaskType.InstanceActions)),
        instanceActions.stream()
            .map(t -> t.getTaskType())
            .collect(Collectors.toCollection(ArrayList::new)));
    JsonNode details = instanceActions.get(0).getDetails();
    assertEquals(Json.toJson(newTags), details.get("tags"));
    assertEquals("q1,q3", details.get("deleteTags").asText());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        new HashMap<>(newTags),
        new HashMap<>(universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags));
  }

  @Test
  public void testEditTagsUnsupportedProvider() {
    Universe universe = defaultUniverse;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.providerType =
                  Common.CloudType.onprem;
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  ImmutableMap.of("q", "v");
            });
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    Map<String, String> newTags = ImmutableMap.of("q1", "v1");
    taskParams.getPrimaryCluster().userIntent.instanceTags = newTags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(
        0, subTasks.stream().filter(t -> t.getTaskType() == TaskType.InstanceActions).count());
  }

  @Test
  public void testExpandSuccess() {
    Universe universe = defaultUniverse;
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_EXPAND_TASK_SEQUENCE, subTasksByPosition);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(5, universe.getUniverseDetails().nodeDetailsSet.size());
  }

  @Test
  public void testExpandOnPremSuccess() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);
    Universe universe = onPremUniverse;
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_EXPAND_TASK_SEQUENCE_ON_PREM, subTasksByPosition);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(5, universe.getUniverseDetails().nodeDetailsSet.size());
  }

  @Test
  public void testExpandOnPremFailNoNodes() {
    Universe universe = onPremUniverse;
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    List<NodeInstance> added = new ArrayList<>();
    added.add(createOnpremInstance(zone));
    added.add(createOnpremInstance(zone));
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    added.forEach(
        nodeInstance -> {
          nodeInstance.setState(NodeInstance.State.USED);
          nodeInstance.save();
        });
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testExpandOnPremFailProvision() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);
    Universe universe = onPremUniverse;
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    preflightResponse.message = "{\"test\": false}";
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testEditUniverseRetries() {
    Universe universe = defaultUniverse;
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Edit,
        CustomerTask.TargetType.Universe,
        taskParams.getUniverseUUID(),
        TaskType.EditUniverse,
        taskParams);
    universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    setDumpEntitiesMock(universe, "", false);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromHost(defaultUniverse.getMasters().get(0).cloudInfo.private_ip));
    taskParams = performShrink(universe);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Edit,
        CustomerTask.TargetType.Universe,
        taskParams.getUniverseUUID(),
        TaskType.EditUniverse,
        taskParams);
  }

  @Test
  public void testVolumeSizeValidationIncNum() {
    Universe universe = defaultUniverse;
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performFullMove(universe);
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize--;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.numVolumes++;
    setDumpEntitiesMock(defaultUniverse, "", false);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCloudShrinkNodesInvalidDiskSize() {
    UniverseDefinitionTaskParams taskParams = getTaskParamsForDiskSizeValidation(defaultUniverse);
    // 80GB used per node on average with a total of 400GB for 5 nodes. Distribute the additional
    // 160GB (2 nodes removed) into 3 nodes with each getting 53.33GB on average.
    mockMetrics(
        taskParams,
        taskParams.nodeDetailsSet,
        n -> n.state != NodeState.ToBeAdded,
        80.0 /* Used */,
        20.0 /* Free */);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.ValidateNodeDiskSize)
            .findFirst()
            .get();
    String expectedMsg =
        "Additional disk size of 160.00 GB is needed, but only 60.00 GB is available";
    assertThat(subTaskInfo.getErrorMessage(), containsString(expectedMsg));
  }

  @Test
  public void testCloudShrinkNodesValidDiskSize() {
    UniverseDefinitionTaskParams taskParams = getTaskParamsForDiskSizeValidation(defaultUniverse);
    // 60GB used per node on average with a total of 300GB for 5 nodes. Distribute the additional
    // 120GB (2 nodes removed) into 3 nodes with each getting 40GB on average.
    mockMetrics(
        taskParams,
        taskParams.nodeDetailsSet,
        n -> n.state != NodeState.ToBeAdded,
        60.0 /* Used */,
        40.0 /* Free */);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testOnpremShrinkNodesInvalidDiskSize() {
    UniverseDefinitionTaskParams taskParams = getTaskParamsForDiskSizeValidation(onPremUniverse);
    // 90GB used per node on average with a total of 450GB for 5 nodes. Distribute the additional
    // 180GB (2 nodes removed) into 3 nodes with each getting 60GB on average.
    mockMetrics(
        taskParams,
        taskParams.nodeDetailsSet,
        n -> n.state != NodeState.ToBeAdded,
        90.0 /* Used */,
        59.0 /* Free */);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.ValidateNodeDiskSize)
            .findFirst()
            .get();
    String expectedMsg =
        "Additional disk size of 180.00 GB is needed, but only 177.00 GB is available";
    assertThat(subTaskInfo.getErrorMessage(), containsString(expectedMsg));
  }

  @Test
  public void testOnpremShrinkNodesValidDiskSize() {
    UniverseDefinitionTaskParams taskParams = getTaskParamsForDiskSizeValidation(onPremUniverse);
    // 90GB used per node on average with a total of 450GB for 5 nodes. Distribute the additional
    // 180GB (2 nodes removed) into 3 nodes with each getting 60GB on average.
    mockMetrics(
        taskParams,
        taskParams.nodeDetailsSet,
        n -> n.state != NodeState.ToBeAdded,
        90.0 /* Used */,
        61.0 /* Free */);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  private UniverseDefinitionTaskParams getTaskParamsForDiskSizeValidation(Universe universe) {
    Cluster primayCluster = universe.getUniverseDetails().getPrimaryCluster();
    if (primayCluster.userIntent.providerType == CloudType.onprem) {
      NodeDetails firstNode = Iterables.get(universe.getNodesInCluster(primayCluster.uuid), 0);
      AvailabilityZone zone = AvailabilityZone.getOrBadRequest(firstNode.getAzUuid());
      // Create two more nods.
      createOnpremInstance(zone);
      createOnpremInstance(zone);
    }
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "100");
    UniverseDefinitionTaskParams taskParams = editClusterSize(universe, ApiUtils.UTIL_INST_TYPE, 5);
    setDumpEntitiesMock(universe, "", false);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromHost(universe.getMasters().get(0).cloudInfo.private_ip));
    return editClusterSize(universe, ApiUtils.UTIL_INST_TYPE, 3);
  }

  private UniverseDefinitionTaskParams performFullMove(Universe universe) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodePrefix = universe.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.creatingUser = defaultUser;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    newUserIntent.instanceType = "c10.large";
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);

    int iter = 1;
    List<String> newIps = new ArrayList<>();
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      if (node.state == NodeDetails.NodeState.ToBeAdded) {
        newIps.add(node.cloudInfo.private_ip);
      }
      node.tserverRpcPort = 3333;
      iter++;
    }

    UniverseModifyBaseTest.mockMasterAndPeerRoles(mockClient, newIps);

    return taskParams;
  }

  private UniverseDefinitionTaskParams performExpand(Universe universe) {
    UniverseDefinitionTaskParams taskParams = editClusterSize(universe, ApiUtils.UTIL_INST_TYPE, 5);
    taskParams.expectedUniverseVersion = 2;
    return taskParams;
  }

  private UniverseDefinitionTaskParams performShrink(Universe universe) {
    UniverseDefinitionTaskParams taskParams = editClusterSize(universe, "m4.medium", 3);
    return taskParams;
  }

  private UniverseDefinitionTaskParams editClusterSize(
      Universe universe, String instanceType, int numNodes) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodePrefix = universe.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.creatingUser = defaultUser;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = numNodes;
    newUserIntent.numNodes = numNodes;
    newUserIntent.instanceType = instanceType;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);

    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    return taskParams;
  }

  private void mockMetrics(
      UniverseDefinitionTaskParams taskParams,
      Set<NodeDetails> nodeDetails,
      Predicate<NodeDetails> predicate,
      double usedSizeGb,
      double freeSizeGb) {
    List<MetricQueryResponse.Entry> sizeResponseList = new ArrayList<>();
    nodeDetails.stream()
        .filter(n -> predicate.test(n))
        .forEach(
            n -> {
              MetricQueryResponse.Entry entry = new MetricQueryResponse.Entry();
              entry.labels = new HashMap<>();
              entry.values = new ArrayList<>();
              entry.values.add(ImmutablePair.of(0.1, usedSizeGb));
              sizeResponseList.add(entry);
            });
    doReturn(sizeResponseList)
        .when(mockMetricQueryHelper)
        .queryDirect(contains("rocksdb_current_version_sst_files_size"));
    List<MetricQueryResponse.Entry> freeResponseList = new ArrayList<>();
    nodeDetails.stream()
        .filter(n -> predicate.test(n))
        .forEach(
            n -> {
              MetricQueryResponse.Entry entry = new MetricQueryResponse.Entry();
              entry.labels = new HashMap<>();
              entry.labels.put("exported_instance", n.getNodeName());
              entry.labels.put("mountpoint", "/mnt/d0");
              entry.values = new ArrayList<>();
              entry.values.add(ImmutablePair.of(0.1, freeSizeGb));
              freeResponseList.add(entry);
            });
    doReturn(freeResponseList)
        .when(mockMetricQueryHelper)
        .queryDirect(contains("node_filesystem_free_bytes"));
  }
}
