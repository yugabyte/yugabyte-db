// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static com.yugabyte.yw.models.TaskInfo.State.Aborted;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.contains;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.clearInvocations;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Iterables;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.DoCapacityReservation;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.NodeDetailsArrayComparator;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.pekko.japi.function.Predicate;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class EditUniverseTest extends UniverseModifyBaseTest {

  private static final List<TaskType> UNIVERSE_EXPAND_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.UpdateConsistencyCheck,
          TaskType.FreezeUniverse,
          TaskType.SetNodeStatus, // ToBeAdded to Adding
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.SetupYNP,
          TaskType.YNPProvisioning,
          TaskType.InstallNodeAgent,
          TaskType.SetNodeStatus,
          TaskType.RunHooks,
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ModifyBlackList,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServer, // check if postgres is up
          TaskType.SwamperTargetsFileUpdate,
          TaskType.MarkRollbackUnsafe,
          TaskType.ModifyBlackList,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForLeadersOnPreferredOnly,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
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
          TaskType.SetNodeState,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UpdateUniverseIntent,
          TaskType.WaitForTServerHeartBeats,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> UNIVERSE_EXPAND_TASK_SEQUENCE_ON_PREM =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.PreflightNodeCheck,
          TaskType.UpdateConsistencyCheck,
          TaskType.FreezeUniverse,
          TaskType.SetNodeStatus, // ToBeAdded to Adding
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.SetupYNP,
          TaskType.YNPProvisioning,
          TaskType.InstallNodeAgent,
          TaskType.SetNodeStatus,
          TaskType.RunHooks,
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ModifyBlackList,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServer, // check if postgres is up
          TaskType.SwamperTargetsFileUpdate,
          TaskType.MarkRollbackUnsafe,
          TaskType.ModifyBlackList,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForLeadersOnPreferredOnly,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
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
          TaskType.SetNodeState,
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
      assertEquals("at position " + position, taskType, tasks.get(0).getTaskType());
      position++;
    }
  }

  @Override
  @Before
  public void setUp() {
    super.setUp();
    // Disable comprehensive prechecks (CheckNodeCommandExecution, CheckServiceLiveness) so task
    // sequence
    // assertions remain valid. Tests verify core EditUniverse behavior.
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory
        .forUniverse(onPremUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");

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
      ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
      lenient().when(listMastersResponse.getPeersList()).thenReturn(Collections.emptyList());
      lenient().when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
      when(mockClient.waitForAreLeadersOnPreferredOnlyCondition(anyLong())).thenReturn(true);
      mockClockSyncResponse(mockNodeUniverseManager);
      mockLocaleCheckResponse(mockNodeUniverseManager);
      mockDbNodePortConnectivityResponse(mockNodeUniverseManager);
      when(mockClient.getLoadMoveCompletion())
          .thenReturn(new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null));
      ListLiveTabletServersResponse mockListLiveTabletServersResponse =
          mock(ListLiveTabletServersResponse.class);
      lenient()
          .when(mockListLiveTabletServersResponse.getTabletServers())
          .thenReturn(new ArrayList<>());
      lenient()
          .when(mockClient.listLiveTabletServers())
          .thenReturn(mockListLiveTabletServersResponse);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    mockWaits(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
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
  public void testComprehensivePrechecksSkippedOnRetryForEditUniverse() {
    Universe universe = defaultUniverse;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  ImmutableMap.of("q", "v", "q1", "v1", "q3", "v3");
            });
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "true");
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenAnswer(
            inv -> {
              List<String> cmd = inv.getArgument(2);
              if (cmd != null && cmd.toString().contains("command-execution-test")) {
                return ShellResponse.create(
                    0, ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + " command-execution-test");
              }
              return ShellResponse.create(0, "Command output:\nLinux x86_64");
            });
    UniverseDefinitionTaskParams taskParams1 = universe.getUniverseDetails();
    taskParams1.setUniverseUUID(universe.getUniverseUUID());
    taskParams1.getPrimaryCluster().userIntent.instanceTags =
        ImmutableMap.of("q", "vq", "q2", "v2");
    taskParams1.setRunOnlyPrechecks(true);

    TaskInfo taskInfo1 = submitTask(taskParams1);
    assertEquals(Success, taskInfo1.getTaskState());
    assertTrue(
        taskInfo1.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckNodeCommandExecution));
    assertTrue(
        taskInfo1.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckServiceLiveness));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams taskParams2 = universe.getUniverseDetails();
    taskParams2.setUniverseUUID(universe.getUniverseUUID());
    taskParams2.getPrimaryCluster().userIntent.instanceTags =
        ImmutableMap.of("q", "vq2", "q2", "v2b");
    taskParams2.setRunOnlyPrechecks(true);
    taskParams2.setPreviousTaskUUID(taskInfo1.getUuid());

    TaskInfo taskInfo2 = submitTask(taskParams2);
    assertEquals(Success, taskInfo2.getTaskState());
    assertFalse(
        taskInfo2.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckNodeCommandExecution));
    assertFalse(
        taskInfo2.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckServiceLiveness));
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
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    Map<String, String> newTags = ImmutableMap.of("q", "vq", "q2", "v2");
    taskParams.getPrimaryCluster().userIntent.instanceTags = newTags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    List<TaskInfo> instanceActions = subTasksByPosition.get(3);
    assertEquals(
        new ArrayList<>(
            Arrays.asList(
                TaskType.InstanceActions, TaskType.InstanceActions, TaskType.InstanceActions)),
        instanceActions.stream()
            .map(t -> t.getTaskType())
            .collect(Collectors.toCollection(ArrayList::new)));
    JsonNode details = instanceActions.get(0).getTaskParams();
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
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
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
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, true /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
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
  public void testExpandWithCapacityReservationAzureSuccess() {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");
    Region region = Region.create(azuProvider, "region-1", "region-1", "yb-image");
    Universe universe = createUniverseForProvider("universe-test", azuProvider);
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.creatingUser = defaultUser;
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.getPrimaryCluster().userIntent.numNodes += 2;
    taskParams.getPrimaryCluster().placementInfo.azStream().findFirst().get().numNodesInAZ += 2;
    taskParams.userAZSelected = true;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), taskParams.getPrimaryCluster().uuid, EDIT);
    updateIPs(taskParams);
    taskParams.expectedUniverseVersion = 2;
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);

    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());

    verifyCapacityReservationAZU(
        universe.getUniverseUUID(),
        AzureReservationGroup.of(
            region,
            Map.of(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", Arrays.asList("host-n4", "host-n5")))));

    verifyNodeInteractionsCapacityReservation(
        14,
        NodeManager.NodeCommandType.Create,
        params -> ((AnsibleCreateServer.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                region.getCode()),
            Arrays.asList("host-n4", "host-n5")));
  }

  @Test
  public void testExpandWithCapacityReservationAwsSuccess() {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationAws.getKey(), "true");
    Region.create(defaultProvider, "region-2", "region-2", "yb-image");
    Universe universe = createUniverseForProvider("universe-test", defaultProvider);
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);

    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());

    verifyCapacityReservationAws(
        universe.getUniverseUUID(),
        Map.of(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
            Map.of("1", new ZoneData("region-1", Arrays.asList("host-n4", "host-n5")))));

    verifyNodeInteractionsCapacityReservation(
        14,
        NodeManager.NodeCommandType.Create,
        params -> ((AnsibleCreateServer.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                "1",
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType),
            Arrays.asList("host-n4", "host-n5")));
  }

  @Test
  public void testExpandWithCapacityReservationGcpSuccess() throws Exception {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationGcp.getKey(), "true");
    Region.create(gcpProvider, "region-2", "region-2", "yb-image");
    Universe universe = createUniverseForProvider("universe-test", gcpProvider);
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);

    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());

    verifyCapacityReservationGcp(
        universe.getUniverseUUID(),
        Map.of(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
            Map.of("1", new ZoneData("region-1", Arrays.asList("host-n4", "host-n5")))));

    // GCP reservation names are random "r-<uuid>"; capture them to map by zone.
    ArgumentCaptor<String> nameCaptor = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> zoneCaptor = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> typeCaptor = ArgumentCaptor.forClass(String.class);
    verify(gcpProjectApiClient, org.mockito.Mockito.atLeast(0))
        .createCapacityReservation(
            nameCaptor.capture(),
            zoneCaptor.capture(),
            typeCaptor.capture(),
            org.mockito.Mockito.anyInt(),
            org.mockito.Mockito.anyMap());

    Map<String, String> zoneToName = new HashMap<>();
    for (int idx = 0; idx < nameCaptor.getAllValues().size(); idx++) {
      zoneToName.put(zoneCaptor.getAllValues().get(idx), nameCaptor.getAllValues().get(idx));
    }

    verifyNodeInteractionsCapacityReservation(
        14,
        NodeManager.NodeCommandType.Create,
        params -> ((AnsibleCreateServer.Params) params).capacityReservation,
        Map.of(zoneToName.get("az-1"), Arrays.asList("host-n4", "host-n5")));
  }

  @Test
  public void testExpandOnPremSuccess() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);
    Universe universe = onPremUniverse;
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, true /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
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
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    List<NodeInstance> added = new ArrayList<>();
    added.add(createOnpremInstance(zone));
    added.add(createOnpremInstance(zone));
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
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
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    preflightResponse.message = "{\"test\": false}";
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testEditUniverseRetries() {
    Universe universe = defaultUniverse;
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, true /* move master */);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Edit,
        CustomerTask.TargetType.Universe,
        taskParams.getUniverseUUID(),
        TaskType.EditUniverse,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
    universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    taskParams = performShrink(universe);
    // It may not be a master but works as long as it in the universe.
    NodeDetails liveNode =
        taskParams.nodeDetailsSet.stream()
            .filter(n -> n.state != NodeState.ToBeRemoved)
            .findFirst()
            .get();
    setDumpEntitiesMock(universe, "", false);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromHost(liveNode.cloudInfo.private_ip));
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Edit,
        CustomerTask.TargetType.Universe,
        taskParams.getUniverseUUID(),
        TaskType.EditUniverse,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }

  @Test
  public void testVolumeSizeValidationIncNum() {
    Universe universe = defaultUniverse;
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.targetNodeDiskUsagePercentage.getKey(), "0");
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

  @Test
  public void testStateTransitionDeltaClearedOnSuccessfulExpand() {
    Universe universe = defaultUniverse;
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testStateTransitionDeltaCapturedOnFailedExpand() {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNotNull(universe.getStateTransitionDetails());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertNotNull(universe.getStateTransitionDetails().getDelta());
    JsonNode nodeDetailsDelta =
        universe.getStateTransitionDetails().getDelta().get("nodeDetailsSet");
    assertNotNull(nodeDetailsDelta);
    boolean hasAdd = false;
    for (JsonNode node : nodeDetailsDelta) {
      if (node.has("$deltaType") && "ADD".equals(node.get("$deltaType").asText())) {
        hasAdd = true;
        assertEquals(NodeState.Live.name(), node.get("$newValue").get("state").asText());
      }
    }
    assertTrue(hasAdd);
    assertNoMasterStateInNodeDetailsDelta(nodeDetailsDelta);
  }

  @Test
  public void testStateTransitionDeltaRollbackUnsafeAfterCheckpoint() {
    Universe universe = defaultUniverse;
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int markRollbackUnsafePosition =
        UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    assertTrue(markRollbackUnsafePosition >= 0);
    // Abort before the next subtask so MarkRollbackUnsafe has already committed.
    setAbortPosition(markRollbackUnsafePosition + 1);
    try {
      TaskInfo taskInfo = submitTask(taskParams);
      assertEquals(Aborted, taskInfo.getTaskState());
      boolean sawMarkRollbackUnsafe =
          taskInfo.getSubTasks().stream()
              .anyMatch(
                  t ->
                      t.getTaskType() == TaskType.MarkRollbackUnsafe
                          && t.getTaskState() == Success);
      assertTrue(sawMarkRollbackUnsafe);
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertNotNull(universe.getStateTransitionDetails());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testStateTransitionDeltaCapturedOnFailedShrink() {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performShrink(universe);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNotNull(universe.getStateTransitionDetails());
    JsonNode nodeDetailsDelta =
        universe.getStateTransitionDetails().getDelta().get("nodeDetailsSet");
    assertNotNull(nodeDetailsDelta);
    boolean hasDelete = false;
    for (JsonNode node : nodeDetailsDelta) {
      if (node.has("$deltaType") && "DELETE".equals(node.get("$deltaType").asText())) {
        hasDelete = true;
        break;
      }
    }
    assertTrue(hasDelete);
    assertNoMasterStateInNodeDetailsDelta(nodeDetailsDelta);
  }

  @Test
  public void testStateTransitionDeltaNotCapturedOnPrecheckFailure() {
    UniverseDefinitionTaskParams taskParams = getTaskParamsForDiskSizeValidation(onPremUniverse);
    mockMetrics(
        taskParams,
        taskParams.nodeDetailsSet,
        n -> n.state != NodeState.ToBeAdded,
        90.0 /* Used */,
        59.0 /* Free */);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testExpandRollback() {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    int nodes = universe.getUniverseDetails().nodeDetailsSet.size();
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "true");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodes, universe.getUniverseDetails().nodeDetailsSet.size());
    assertTrue(universe.getUniverseDetails().autoRollbackPerformed);
    assertNotNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testFullMoveRollback() {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    int nodes = universe.getUniverseDetails().nodeDetailsSet.size();
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "true");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performFullMove(universe);
    assertEquals(nodes * 2, taskParams.nodeDetailsSet.size());
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodes, universe.getUniverseDetails().nodeDetailsSet.size());
    assertTrue(universe.getUniverseDetails().autoRollbackPerformed);
    assertNotNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
  }

  @Test
  public void testFullMoveNoRollback() {
    ShellResponse badShellResponse = new ShellResponse();
    badShellResponse.code = 100;
    badShellResponse.message = "No way!";
    ShellResponse okShellResponse = new ShellResponse();
    okShellResponse.message = "";
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.Create.equals(invocation.getArgument(0))) {
                AnsibleCreateServer.Params params = invocation.getArgument(1);
                if (params.nodeName.contains("n2")) {
                  return badShellResponse;
                }
              }
              return okShellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    int nodes = universe.getUniverseDetails().nodeDetailsSet.size();
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "true");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performFullMove(universe);
    assertEquals(nodes * 2, taskParams.nodeDetailsSet.size());
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNotEquals(nodes, universe.getUniverseDetails().nodeDetailsSet.size());
    assertFalse(universe.getUniverseDetails().autoRollbackPerformed);
    assertNotNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNotNull(universe.getUniverseDetails().placementModificationTaskUuid);
  }

  private UniverseDefinitionTaskParams getTaskParamsForDiskSizeValidation(Universe universe) {
    Cluster primayCluster = universe.getUniverseDetails().getPrimaryCluster();
    if (primayCluster.userIntent.getAllCloudTypes().iterator().next() == CloudType.onprem) {
      NodeDetails firstNode = Iterables.get(universe.getNodesInCluster(primayCluster.uuid), 0);
      AvailabilityZone zone = AvailabilityZone.getOrBadRequest(firstNode.getAzUuid());
      // Create two more nods.
      createOnpremInstance(zone);
      createOnpremInstance(zone);
    }
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(universe)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "100");
    UniverseDefinitionTaskParams taskParams =
        editClusterSize(universe, ApiUtils.UTIL_INST_TYPE, 5, false /* move master */);
    setDumpEntitiesMock(universe, "", false);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromHost(universe.getMasters().get(0).cloudInfo.private_ip));
    return editClusterSize(universe, ApiUtils.UTIL_INST_TYPE, 3, false /* move master */);
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

  /** Primary RF=1 with 3 Live nodes so RF-only increase 1->3 adds no ToBeAdded/ToBeRemoved. */
  private Universe prepareUniverseForRfIncrease() {
    return Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          Cluster primary = u.getUniverseDetails().getPrimaryCluster();
          primary.userIntent.replicationFactor = 1;
          UUID defaultRegion = primary.userIntent.regionList.get(0);
          PlacementInfoUtil.checkAndSetPerAZRF(
              primary.placementInfo, 1, defaultRegion, false /* throwIfIncorrect */);
        });
  }

  private UniverseDefinitionTaskParams performIncreaseRf(Universe universe, int newRf) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodePrefix = universe.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.creatingUser = defaultUser;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    newUserIntent.replicationFactor = newRf;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);
    assertEquals(
        0, taskParams.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeAdded).count());
    assertEquals(
        0,
        taskParams.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeRemoved).count());
    updateIPs(taskParams);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromHost(universe.getMasters().get(0).cloudInfo.private_ip));
    return taskParams;
  }

  private UniverseDefinitionTaskParams performExpand(Universe universe, boolean moveMaster) {
    UniverseDefinitionTaskParams taskParams =
        editClusterSize(universe, ApiUtils.UTIL_INST_TYPE, 5, moveMaster);
    taskParams.expectedUniverseVersion = 2;
    return taskParams;
  }

  private UniverseDefinitionTaskParams performShrink(Universe universe) {
    UniverseDefinitionTaskParams taskParams = editClusterSize(universe, "m4.medium", 3, false);
    return taskParams;
  }

  private UniverseDefinitionTaskParams editClusterSize(
      Universe universe, String instanceType, int numNodes, boolean moveMasters) {
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
    PlacementRegion placementRegion = pi.cloudList.get(0).regionList.get(0);
    if (moveMasters) {
      // Add another AZ to move one master to it.
      Region region = Region.getOrBadRequest(placementRegion.uuid);
      AvailabilityZone zone = AvailabilityZone.createOrThrow(region, AZ_CODE1, "AZ 2", "subnet-1");
      PlacementInfoUtil.addPlacementZone(zone.getUuid(), pi);
      placementRegion.azList.get(0).numNodesInAZ = numNodes - 1;
      placementRegion.azList.get(1).numNodesInAZ = 1;
      if (primaryCluster.userIntent.providerType == CloudType.onprem) {
        createOnpremInstance(zone);
      }
    } else {
      placementRegion.azList.get(0).numNodesInAZ = numNodes;
    }
    newUserIntent.numNodes = numNodes;
    newUserIntent.instanceType = instanceType;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);
    updateIPs(taskParams);
    return taskParams;
  }

  private void updateIPs(UniverseDefinitionTaskParams taskParams) {
    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
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

  private void assertNoMasterStateInNodeDetailsDelta(JsonNode nodeDetailsDelta) {
    for (JsonNode node : nodeDetailsDelta) {
      assertFalse("Delta must not contain masterState", nodeDeltaContainsMasterState(node));
    }
  }

  private boolean nodeDeltaContainsMasterState(JsonNode node) {
    if (node == null || node.isNull()) {
      return false;
    }
    // DELETE entries reflect the pre-task node; transient masterState on old values is expected.
    if (node.has("$deltaType") && "DELETE".equals(node.get("$deltaType").asText())) {
      return false;
    }
    if (node.has("masterState") && !node.get("masterState").isNull()) {
      return true;
    }
    if (node.has("$deltaType")) {
      String deltaType = node.get("$deltaType").asText();
      if ("ADD".equals(deltaType) && node.has("$newValue")) {
        return containsNonNullMasterState(node.get("$newValue"));
      }
    }
    java.util.Iterator<Map.Entry<String, JsonNode>> fields = node.fields();
    while (fields.hasNext()) {
      Map.Entry<String, JsonNode> entry = fields.next();
      if (entry.getKey().startsWith("$")) {
        continue;
      }
      if ("masterState".equals(entry.getKey())) {
        JsonNode masterStateDelta = entry.getValue();
        if (masterStateDelta.has("$deltaType")
            && masterStateDelta.has("$newValue")
            && !masterStateDelta.get("$newValue").isNull()) {
          return true;
        }
        continue;
      }
      if (nodeDeltaContainsMasterState(entry.getValue())) {
        return true;
      }
    }
    return false;
  }

  private boolean containsNonNullMasterState(JsonNode node) {
    return node != null && node.has("masterState") && !node.get("masterState").isNull();
  }

  @Test
  public void testCanTaskRollbackTrueAfterFailedExpandBeforeCheckpoint() {
    clearAbortOrPausePositions();
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    CustomerTask editCustomerTask =
        CustomerTask.create(
            defaultCustomer,
            universe.getUniverseUUID(),
            taskInfo.getUuid(),
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.Edit,
            universe.getName());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertEquals(taskInfo.getUuid(), universe.getUniverseDetails().placementModificationTaskUuid);
    assertTrue(commissioner.canTaskRollbackDetailed(taskInfo));
    // Listing canRollback: ownership still on the failed edit.
    assertTrue(listingCanRollback(editCustomerTask, taskInfo, universe));
  }

  @Test
  public void testCanTaskRollbackFalseAfterCheckpoint() {
    clearAbortOrPausePositions();
    Universe universe = defaultUniverse;
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int markRollbackUnsafePosition =
        UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    setAbortPosition(markRollbackUnsafePosition + 1);
    try {
      TaskInfo taskInfo = submitTask(taskParams);
      assertEquals(Aborted, taskInfo.getTaskState());
      CustomerTask editCustomerTask =
          CustomerTask.create(
              defaultCustomer,
              universe.getUniverseUUID(),
              taskInfo.getUuid(),
              CustomerTask.TargetType.Universe,
              CustomerTask.TaskType.Edit,
              universe.getName());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
      // Listing hint stays optimistic for rollbackSafe; submit gate uses detailed check.
      // Placement still owned by the failed edit, so listing ownership AND stays true.
      assertTrue(commissioner.canTaskRollback(taskInfo));
      assertFalse(commissioner.canTaskRollbackDetailed(taskInfo));
      assertTrue(listingCanRollback(editCustomerTask, taskInfo, universe));
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testCanTaskRollbackFalseAfterRollbackOwnsPlacement() {
    clearAbortOrPausePositions();
    // Once RollbackEditUniverse freezes, placement ownership moves off the failed edit.
    // Submit detailed check and listing canRollback must both become false for the edit.
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    CustomerTask editCustomerTask =
        CustomerTask.create(
            defaultCustomer,
            universe.getUniverseUUID(),
            failedEdit.getUuid(),
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.Edit,
            universe.getName());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
    assertTrue(listingCanRollback(editCustomerTask, failedEdit, universe));

    // Simulate freeze of RollbackEditUniverse: both updating and placement ownership move.
    UUID rollbackUuid = UUID.randomUUID();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        univ -> {
          univ.getUniverseDetails().placementModificationTaskUuid = rollbackUuid;
          univ.getUniverseDetails().updatingTaskUUID = rollbackUuid;
        });
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(rollbackUuid, universe.getUniverseDetails().placementModificationTaskUuid);
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());

    // Cheap hint may still be true; detailed + listing ownership must be false.
    assertTrue(commissioner.canTaskRollback(failedEdit));
    assertFalse(commissioner.canTaskRollbackDetailed(failedEdit));
    assertFalse(listingCanRollback(editCustomerTask, failedEdit, universe));
  }

  @Test
  public void testRollbackEditUniverseNotRollbackable() {
    assertFalse(Commissioner.canTaskTypeRollback(TaskType.RollbackEditUniverse));
  }

  @Test
  public void testRollbackEditUniverseHappyPathAfterFailedExpand() throws InterruptedException {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    Set<String> beforeNames =
        universe.getNodes().stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
    assertTrue(universe.getUniverseDetails().nodeDetailsSet.size() > nodesBefore);
    Set<String> addIps =
        universe.getNodes().stream()
            .filter(n -> n.getNodeName() != null && !beforeNames.contains(n.getNodeName()))
            .map(n -> n.cloudInfo == null ? null : n.cloudInfo.private_ip)
            .filter(ip -> ip != null && !ip.isBlank())
            .collect(Collectors.toSet());
    Set<String> beforeIps =
        universe.getNodes().stream()
            .filter(n -> n.getNodeName() != null && beforeNames.contains(n.getNodeName()))
            .map(n -> n.cloudInfo == null ? null : n.cloudInfo.private_ip)
            .filter(ip -> ip != null && !ip.isBlank())
            .collect(Collectors.toSet());

    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.verifyClusterStateBeforeTask.getKey(), "true");

    // Instances were never created (Create failed); List still empty. Stub Destroy/List to succeed
    // for rollback, and report instances present so destroy is attempted for ADD nodes.
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

    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    UUID rollbackUuid = commissioner.submit(TaskType.RollbackEditUniverse, rollbackParams);
    TaskInfo rollbackInfo = waitForTask(rollbackUuid);
    assertEquals(Success, rollbackInfo.getTaskState());

    // Destroy ADDs before blacklist clear; swamper rewrite must follow details restore.
    List<TaskType> rollbackTypes =
        rollbackInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());
    int consistencyIdx = rollbackTypes.indexOf(TaskType.CheckForClusterServers);
    int destroyIdx = rollbackTypes.indexOf(TaskType.AnsibleDestroyServer);
    int blacklistIdx = rollbackTypes.indexOf(TaskType.ModifyBlackList);
    int membershipIdx = rollbackTypes.indexOf(TaskType.ConfirmEditRollbackMembership);
    int restoreIdx = rollbackTypes.indexOf(TaskType.RestoreUniverseDetailsFromDelta);
    int swamperIdx = rollbackTypes.indexOf(TaskType.SwamperTargetsFileUpdate);
    assertTrue(consistencyIdx >= 0);
    assertTrue(destroyIdx > consistencyIdx);
    assertTrue(blacklistIdx > destroyIdx);
    assertTrue(membershipIdx > blacklistIdx);
    assertTrue(restoreIdx > membershipIdx);
    assertTrue(swamperIdx > restoreIdx);
    Set<String> removeIps = privateIpsFromModifyBlackListRemoveNodes(rollbackInfo);
    assertFalse(addIps.isEmpty());
    assertTrue(removeIps.containsAll(addIps));
    assertTrue(removeIps.containsAll(beforeIps));
    // No CR state left after failed edit catch cleanup => no ReleaseCapacityReservation.
    assertFalse(rollbackTypes.contains(TaskType.DeleteCapacityReservation));
    verify(mockSwamperHelper, atLeastOnce()).writeUniverseTargetJson(universe.getUniverseUUID());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
    assertTrue(universe.getUniverseDetails().updateSucceeded);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
    verify(mockNodeManager, atLeastOnce())
        .nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());
  }

  @Test
  public void testRollbackEditUniverseReleasesCapacityReservationBeforeRestore()
      throws InterruptedException {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());

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

    // Simulate abort/crash leaving CapacityReservationState after the failed edit.
    UUID providerUuid = defaultProvider.getUuid();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams.CapacityReservationState crState =
              new UniverseDefinitionTaskParams.CapacityReservationState();
          crState
              .getAwsReservationInfos()
              .put(providerUuid, new UniverseDefinitionTaskParams.AwsReservationInfo());
          u.getUniverseDetails().setCapacityReservationState(crState);
        });

    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    UUID rollbackUuid = commissioner.submit(TaskType.RollbackEditUniverse, rollbackParams);
    TaskInfo rollbackInfo = waitForTask(rollbackUuid);
    assertEquals(Success, rollbackInfo.getTaskState());

    List<TaskType> rollbackTypes =
        rollbackInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());
    int crIdx = rollbackTypes.indexOf(TaskType.DeleteCapacityReservation);
    int restoreIdx = rollbackTypes.indexOf(TaskType.RestoreUniverseDetailsFromDelta);
    assertTrue(crIdx >= 0);
    assertTrue(restoreIdx > crIdx);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
    assertNull(universe.getUniverseDetails().getCapacityReservationState());
  }

  @Test
  public void testRollbackEditUniverseIdempotentRetry() throws InterruptedException {
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());

    ShellResponse ok = new ShellResponse();
    ok.message = "";
    // First rollback pass: List empty => skip destroy; restore details. Then retry should also
    // succeed with no instances.
    doAnswer(invocation -> ok).when(mockNodeManager).nodeCommand(any(), any());

    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    UUID rollbackUuid = commissioner.submit(TaskType.RollbackEditUniverse, rollbackParams);
    assertEquals(Success, waitForTask(rollbackUuid).getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackEditUniverseAfterMidProvisionAbort() throws InterruptedException {
    // U1: abort after Create succeeded (at SetupYNP) - still pre-checkpoint.
    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int setupYnpPosition = UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.SetupYNP);
    assertTrue(
        setupYnpPosition > UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.AnsibleCreateServer));
    setAbortPosition(setupYnpPosition);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
      assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
      assertTrue(universe.getUniverseDetails().nodeDetailsSet.size() > nodesBefore);

      stubNodeManagerOkWithInstancesPresent();
      // Abort position was set for EditUniverse (SetupYNP). Clear before rollback so the longer
      // rollback graph (tags/blacklist/destroy/restore) is not aborted by the same position.
      clearAbortOrPausePositions();
      assertEquals(Success, submitRollbackEditUniverse(failedEdit, universe).getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
      assertNull(universe.getStateTransitionDetails());
      verify(mockNodeManager, atLeastOnce())
          .nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testRollbackEditUniverseAfterPartialCreateFailure() throws InterruptedException {
    // U4: some Creates succeed, one fails - manual rollback destroys surviving ADD instances.
    ShellResponse bad = new ShellResponse();
    bad.code = 100;
    bad.message = "Nope";
    ShellResponse ok = new ShellResponse();
    ok.message = "";
    AtomicInteger createCount = new AtomicInteger();
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.Create.equals(invocation.getArgument(0))) {
                // Fail the second ADD create so the first ADD may have succeeded.
                if (createCount.incrementAndGet() >= 2) {
                  return bad;
                }
              }
              return ok;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
    // Auto-rollback would refuse after partial Create; manual path must still be eligible.
    assertFalse(universe.getUniverseDetails().autoRollbackPerformed);
    assertTrue(universe.getUniverseDetails().nodeDetailsSet.size() > nodesBefore);

    clearInvocations(mockNodeManager);
    stubNodeManagerOkWithInstancesPresent();
    assertEquals(Success, submitRollbackEditUniverse(failedEdit, universe).getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
    assertNull(universe.getStateTransitionDetails());
    verify(mockNodeManager, atLeastOnce())
        .nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());
  }

  @Test
  public void testCanTaskRollbackFalseWhenDedicatedNodesChanged() throws InterruptedException {
    // Match auto-rollback: dedicatedNodes flips rewrite Live tserver gflags before the checkpoint.
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));

    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          StateTransitionDetails details = u.getStateTransitionDetails();
          UniverseDefinitionTaskParams before = details.getBeforeUniverseDetails();
          UniverseDefinitionTaskParams target = details.getTargetUniverseDetails();
          target.getPrimaryCluster().userIntent.dedicatedNodes =
              !before.getPrimaryCluster().userIntent.dedicatedNodes;
          JsonNode delta =
              DeltaEvaluator.buildDeltaJsonTree(before, target, new NodeDetailsArrayComparator());
          u.setStateTransitionDetails(new StateTransitionDetails(true /* rollbackSafe */, delta));
        });
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isDedicatedNodesChanged());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    assertFalse(commissioner.canTaskRollbackDetailed(failedEdit));

    TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
    assertEquals(Failure, rollbackInfo.getTaskState());
  }

  @Test
  public void testRollbackEditUniverseRejectedWhenUnsafe() throws InterruptedException {
    // U5: past MarkRollbackUnsafe - submit RollbackEditUniverse and expect BAD_REQUEST failure.
    // Failure now happens post-lock in createPrecheckTasks; unlock must leave the universe clean.
    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int markRollbackUnsafePosition =
        UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    setAbortPosition(markRollbackUnsafePosition + 1);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
      assertTrue(commissioner.canTaskRollback(failedEdit));
      assertFalse(commissioner.canTaskRollbackDetailed(failedEdit));

      TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
      assertEquals(Failure, rollbackInfo.getTaskState());
      assertThat(rollbackInfo.getErrorMessage(), containsString("rollback checkpoint was crossed"));
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      // Unlock-on-precheck-failure must not wipe the failed edit's ownership or delta.
      assertFalse(universe.getUniverseDetails().updateInProgress);
      assertEquals(
          failedEdit.getUuid(), universe.getUniverseDetails().placementModificationTaskUuid);
      assertNotNull(universe.getStateTransitionDetails());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testRollbackEditUniverseRejectedWhenDeltaMissing() throws InterruptedException {
    // Missing delta after a failed expand - createPrecheckTasks must reject and unlock cleanly.
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());

    Universe.saveDetails(universe.getUniverseUUID(), univ -> univ.setStateTransitionDetails(null));

    TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
    assertEquals(Failure, rollbackInfo.getTaskState());
    assertThat(rollbackInfo.getErrorMessage(), containsString("delta is missing"));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertFalse(universe.getUniverseDetails().updateInProgress);
    assertEquals(failedEdit.getUuid(), universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackEditUniverseRunOnlyPrechecksSafe() throws InterruptedException {
    // Precheck-only on a rollback-safe universe must succeed without destroying nodes.
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());

    clearInvocations(mockNodeManager);
    stubNodeManagerOkWithInstancesPresent();
    UniverseDefinitionTaskParams precheckParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    precheckParams.setUniverseUUID(universe.getUniverseUUID());
    precheckParams.expectedUniverseVersion = -1;
    precheckParams.setRunOnlyPrechecks(true);
    TaskInfo precheckInfo =
        waitForTask(commissioner.submit(TaskType.RollbackEditUniverse, precheckParams));
    assertEquals(Success, precheckInfo.getTaskState());
    verify(mockNodeManager, never()).nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Precheck-only must not restore details or clear the failed-edit delta.
    assertNotNull(universe.getStateTransitionDetails());
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
  }

  @Test
  public void testRollbackEditUniverseRunOnlyPrechecksUnsafe() throws InterruptedException {
    // Precheck-only must still reject when the rollback checkpoint was crossed.
    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int markRollbackUnsafePosition =
        UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    setAbortPosition(markRollbackUnsafePosition + 1);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());

      UniverseDefinitionTaskParams precheckParams =
          Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
      precheckParams.setUniverseUUID(universe.getUniverseUUID());
      precheckParams.expectedUniverseVersion = -1;
      precheckParams.setRunOnlyPrechecks(true);
      TaskInfo precheckInfo =
          waitForTask(commissioner.submit(TaskType.RollbackEditUniverse, precheckParams));
      assertEquals(Failure, precheckInfo.getTaskState());
      assertThat(precheckInfo.getErrorMessage(), containsString("rollback checkpoint was crossed"));
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertFalse(universe.getUniverseDetails().updateInProgress);
      assertNotNull(universe.getStateTransitionDetails());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testRollbackEditUniverseAfterWaitForServerAbort() throws InterruptedException {
    // U2: abort at WaitForServer on new nodes - still before MarkRollbackUnsafe.
    // Live tservers include ADD IPs (still ToBeAdded in YBA); CCC must skip those names.
    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    enableManualEditRollback(universe);
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.verifyClusterStateBeforeTask.getKey(), "true");
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int waitForServerPosition = UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.WaitForServer);
    int markUnsafePosition = UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    assertTrue(waitForServerPosition > 0 && waitForServerPosition < markUnsafePosition);
    setAbortPosition(waitForServerPosition);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
      assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
      assertTrue(universe.getUniverseDetails().nodeDetailsSet.size() > nodesBefore);

      // Master still reports ADD tservers (not in Live-before). Use ADD IPs only so CCC must
      // skip those names, while membership still matches Live-before + destroyedNodeIps.
      // Full mid-edit setMockLiveTabletServers would also report remapped survivor IPs that
      // differ from before (test UpdateNodeInfo artifact) and fail ConfirmEditRollbackMembership.
      try {
        Set<String> beforeNames =
            universe.getStateTransitionDetails().getBeforeUniverseDetails().nodeDetailsSet.stream()
                .map(NodeDetails::getNodeName)
                .filter(Objects::nonNull)
                .collect(Collectors.toSet());
        ListLiveTabletServersResponse liveTs = mock(ListLiveTabletServersResponse.class);
        List<org.yb.util.TabletServerInfo> addServers = new ArrayList<>();
        for (NodeDetails n : universe.getNodes()) {
          if (n.getNodeName() == null
              || beforeNames.contains(n.getNodeName())
              || n.cloudInfo == null
              || n.cloudInfo.private_ip == null) {
            continue;
          }
          org.yb.util.TabletServerInfo info = new org.yb.util.TabletServerInfo();
          info.setPrivateRpcAddress(
              HostAndPort.fromParts(n.cloudInfo.private_ip, n.tserverRpcPort));
          addServers.add(info);
        }
        assertFalse("expected ADD nodes with IPs after WaitForServer", addServers.isEmpty());
        lenient().when(liveTs.getTabletServers()).thenReturn(addServers);
        lenient().when(mockClient.listLiveTabletServers()).thenReturn(liveTs);
      } catch (Exception e) {
        fail(e.getMessage());
      }

      stubNodeManagerOkWithInstancesPresent();
      clearAbortOrPausePositions();
      TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
      assertEquals(Success, rollbackInfo.getTaskState());
      List<TaskType> rollbackTypes =
          rollbackInfo.getSubTasks().stream()
              .map(TaskInfo::getTaskType)
              .collect(Collectors.toList());
      assertTrue(rollbackTypes.contains(TaskType.CheckForClusterServers));
      assertTrue(rollbackTypes.contains(TaskType.ConfirmEditRollbackMembership));
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
      assertNull(universe.getStateTransitionDetails());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testRollbackEditUniverseAfterModifyBlackListAbort() throws InterruptedException {
    // U3: abort at first ModifyBlackList (new-node blacklist) - still pre-checkpoint.
    // Rollback clears orphaned server_blacklist for Live survivors and ADD nodes.
    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    Set<String> beforeNames =
        universe.getNodes().stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int modifyBlackListPosition = UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.ModifyBlackList);
    int markUnsafePosition = UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    assertTrue(modifyBlackListPosition > 0 && modifyBlackListPosition < markUnsafePosition);
    setAbortPosition(modifyBlackListPosition);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
      assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
      Set<String> addIps =
          universe.getNodes().stream()
              .filter(n -> n.getNodeName() != null && !beforeNames.contains(n.getNodeName()))
              .map(n -> n.cloudInfo == null ? null : n.cloudInfo.private_ip)
              .filter(ip -> ip != null && !ip.isBlank())
              .collect(Collectors.toSet());
      Set<String> beforeIps =
          universe.getNodes().stream()
              .filter(n -> n.getNodeName() != null && beforeNames.contains(n.getNodeName()))
              .map(n -> n.cloudInfo == null ? null : n.cloudInfo.private_ip)
              .filter(ip -> ip != null && !ip.isBlank())
              .collect(Collectors.toSet());
      assertFalse(addIps.isEmpty());

      stubNodeManagerOkWithInstancesPresent();
      clearAbortOrPausePositions();
      clearInvocations(mockClient);
      TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
      assertEquals(Success, rollbackInfo.getTaskState());
      Set<String> removeIps = privateIpsFromModifyBlackListRemoveNodes(rollbackInfo);
      assertTrue(removeIps.containsAll(addIps));
      assertTrue(removeIps.containsAll(beforeIps));
      try {
        // Precheck reads config; clear-blacklist path updates master cluster config.
        verify(mockClient, atLeastOnce()).getMasterClusterConfig();
        verify(mockClient, atLeastOnce()).changeMasterClusterConfig(any());
      } catch (Exception e) {
        fail(e.getMessage());
      }
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
      assertNull(universe.getStateTransitionDetails());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testRollbackEditUniverseRejectedAfterBlacklistSwapAbort()
      throws InterruptedException {
    // Unblacklisting ADDs is past MarkRollbackUnsafe; abort at the swap and expect rejection.
    Universe universe = defaultUniverse;
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    int swapBlackListPosition = UNIVERSE_EXPAND_TASK_SEQUENCE.lastIndexOf(TaskType.ModifyBlackList);
    int markUnsafePosition = UNIVERSE_EXPAND_TASK_SEQUENCE.indexOf(TaskType.MarkRollbackUnsafe);
    assertTrue(markUnsafePosition > 0 && markUnsafePosition < swapBlackListPosition);
    setAbortPosition(swapBlackListPosition + 1);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
      assertTrue(commissioner.canTaskRollback(failedEdit));
      assertFalse(commissioner.canTaskRollbackDetailed(failedEdit));

      TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
      assertEquals(Failure, rollbackInfo.getTaskState());
      assertThat(rollbackInfo.getErrorMessage(), containsString("rollback checkpoint was crossed"));
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertFalse(universe.getUniverseDetails().updateInProgress);
      assertEquals(
          failedEdit.getUuid(), universe.getUniverseDetails().placementModificationTaskUuid);
      assertNotNull(universe.getStateTransitionDetails());
      assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testRollbackEditUniverseRetryAfterDestroyFailure() throws InterruptedException {
    // U6: abort after Destroy (before Restore); retry restores details (idempotent).
    // Force-delete swallows Destroy shell errors, so abort at Restore is the reliable fault.
    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    Universe universe = defaultUniverse;
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    enableManualEditRollback(universe);
    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));

    stubNodeManagerOkWithInstancesPresent();
    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    // Rollback subtask positions (no tag revert): ConsistencyCheck=0, Freeze=1, SetNodeState=2,
    // Destroy=3, RestoreUniverseDetailsFromDelta=4.
    setAbortPosition(4);
    UUID rollbackUuid;
    try {
      rollbackUuid = commissioner.submit(TaskType.RollbackEditUniverse, rollbackParams);
      CustomerTask.create(
          defaultCustomer,
          universe.getUniverseUUID(),
          rollbackUuid,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.RollbackEditUniverse,
          universe.getName());
      assertEquals(Aborted, waitForTask(rollbackUuid).getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertNotNull(universe.getStateTransitionDetails());
      assertTrue(universe.getStateTransitionDetails().isRollbackSafe());
    } finally {
      clearAbortOrPausePositions();
    }

    CustomerTask retryTask =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), rollbackUuid);
    assertEquals(Success, waitForTask(retryTask.getTaskUUID()).getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackEditUniverseRevertsInstanceTagsAfterFailedTagsEdit()
      throws InterruptedException {
    Map<String, String> beforeTags = ImmutableMap.of("q", "v", "q1", "v1", "q3", "v3");
    Map<String, String> newTags = ImmutableMap.of("q", "vq", "q2", "v2");
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  new HashMap<>(beforeTags);
            });
    enableManualEditRollback(universe);

    doAnswer(
            invocation -> {
              if (NodeManager.NodeCommandType.Tags.equals(invocation.getArgument(0))) {
                ShellResponse bad = new ShellResponse();
                bad.code = 100;
                bad.message = "tag update failed";
                return bad;
              }
              ShellResponse ok = new ShellResponse();
              ok.message = "";
              return ok;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.getPrimaryCluster().userIntent.instanceTags = new HashMap<>(newTags);
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));
    assertTrue(universe.getStateTransitionDetails().isRollbackSafe());

    clearInvocations(mockNodeManager);
    ShellResponse ok = new ShellResponse();
    ok.message = "";
    doAnswer(invocation -> ok).when(mockNodeManager).nodeCommand(any(), any());

    assertEquals(Success, submitRollbackEditUniverse(failedEdit, universe).getTaskState());

    ArgumentCaptor<NodeTaskParams> tagsCaptor = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, atLeastOnce())
        .nodeCommand(eq(NodeManager.NodeCommandType.Tags), tagsCaptor.capture());
    InstanceActions.Params tagsParams = (InstanceActions.Params) tagsCaptor.getValue();
    assertEquals(beforeTags, tagsParams.tags);
    assertEquals("q2", tagsParams.deleteTags);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        beforeTags,
        new HashMap<>(universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags));
    assertTrue(universe.getUniverseDetails().updateSucceeded);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackEditUniverseRevertsTagsAndDestroysNodesAfterFailedExpand()
      throws InterruptedException {
    Map<String, String> beforeTags = ImmutableMap.of("env", "prod");
    Map<String, String> newTags = ImmutableMap.of("env", "staging", "owner", "platform");
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  new HashMap<>(beforeTags);
            });
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    enableManualEditRollback(universe);

    doAnswer(
            invocation -> {
              NodeManager.NodeCommandType type = invocation.getArgument(0);
              if (NodeManager.NodeCommandType.List.equals(type)) {
                ShellResponse listResponse = new ShellResponse();
                listResponse.message = "";
                return listResponse;
              }
              if (NodeManager.NodeCommandType.Tags.equals(type)) {
                ShellResponse okTags = new ShellResponse();
                okTags.message = "";
                return okTags;
              }
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 100;
              shellResponse.message = "Nope";
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());

    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    taskParams.expectedUniverseVersion = -1;
    taskParams.getPrimaryCluster().userIntent.instanceTags = new HashMap<>(newTags);
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    TaskInfo failedEdit = submitTask(taskParams);
    assertEquals(Failure, failedEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));

    clearInvocations(mockNodeManager);
    stubNodeManagerOkWithInstancesPresent();

    assertEquals(Success, submitRollbackEditUniverse(failedEdit, universe).getTaskState());

    ArgumentCaptor<NodeTaskParams> tagsCaptor = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, atLeastOnce())
        .nodeCommand(eq(NodeManager.NodeCommandType.Tags), tagsCaptor.capture());
    InstanceActions.Params tagsParams = (InstanceActions.Params) tagsCaptor.getValue();
    assertEquals(beforeTags, tagsParams.tags);
    assertEquals("owner", tagsParams.deleteTags);
    // Tags only applied to pre-edit Live survivors (not ADDs).
    Set<String> taggedNodes =
        tagsCaptor.getAllValues().stream().map(p -> p.nodeName).collect(Collectors.toSet());
    for (String tagged : taggedNodes) {
      assertTrue(tagged.matches("host-n[123]"));
    }
    verify(mockNodeManager, atLeastOnce())
        .nodeCommand(eq(NodeManager.NodeCommandType.Destroy), any());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
    assertEquals(
        beforeTags,
        new HashMap<>(universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags));
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackEditUniverseOnPremSkipsInstanceTagActions() throws InterruptedException {
    Map<String, String> beforeTags = ImmutableMap.of("q", "v");
    Map<String, String> newTags = ImmutableMap.of("q1", "v1");
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);
    Universe universe =
        Universe.saveDetails(
            onPremUniverse.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  new HashMap<>(beforeTags);
            });
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    enableManualEditRollback(universe);

    UniverseDefinitionTaskParams taskParams = performExpand(universe, false /* move master */);
    taskParams.expectedUniverseVersion = -1;
    taskParams.getPrimaryCluster().userIntent.instanceTags = new HashMap<>(newTags);
    int freezePosition = UNIVERSE_EXPAND_TASK_SEQUENCE_ON_PREM.indexOf(TaskType.FreezeUniverse);
    assertTrue(freezePosition >= 0);
    setAbortPosition(freezePosition + 1);
    try {
      TaskInfo failedEdit = submitTask(taskParams);
      assertEquals(Aborted, failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertTrue(commissioner.canTaskRollbackDetailed(failedEdit));

      // Abort position was for EditUniverse (right after Freeze). Clear before rollback.
      clearAbortOrPausePositions();

      clearInvocations(mockNodeManager);
      ShellResponse ok = new ShellResponse();
      ok.message = "";
      // Abort is right after Freeze, so rollback may not invoke nodeCommand (no destroy/tags).
      lenient().doAnswer(invocation -> ok).when(mockNodeManager).nodeCommand(any(), any());

      assertEquals(Success, submitRollbackEditUniverse(failedEdit, universe).getTaskState());

      verify(mockNodeManager, never()).nodeCommand(eq(NodeManager.NodeCommandType.Tags), any());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(nodesBefore, universe.getUniverseDetails().nodeDetailsSet.size());
      assertEquals(
          beforeTags,
          new HashMap<>(universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags));
      assertNull(universe.getStateTransitionDetails());
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testAbortRollbackMatrixCloudExpand() throws Exception {
    Map<String, String> beforeTags =
        new HashMap<>(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags);
    runPreCheckpointAbortRollbackMatrix(
        "cloudExpand",
        defaultUniverse,
        u -> {
          enableManualEditRollback(u);
          factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
          UniverseDefinitionTaskParams params = performExpand(u, false /* move master */);
          params.expectedUniverseVersion = -1;
          return params;
        },
        beforeTags,
        true /* assertExpandMarkVsMblOrder */,
        null /* beforeReplicationFactor */);
  }

  @Test
  public void testAbortRollbackMatrixTagsOnly() throws Exception {
    Map<String, String> beforeTags = ImmutableMap.of("q", "v", "q1", "v1", "q3", "v3");
    Map<String, String> newTags = ImmutableMap.of("q", "vq", "q2", "v2");
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  new HashMap<>(beforeTags);
            });
    runPreCheckpointAbortRollbackMatrix(
        "tagsOnly",
        universe,
        u -> {
          enableManualEditRollback(u);
          factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
          UniverseDefinitionTaskParams params = u.getUniverseDetails();
          params.setUniverseUUID(u.getUniverseUUID());
          params.expectedUniverseVersion = -1;
          params.getPrimaryCluster().userIntent.instanceTags = new HashMap<>(newTags);
          return params;
        },
        beforeTags,
        false /* assertExpandMarkVsMblOrder */,
        null /* beforeReplicationFactor */);
  }

  @Test
  public void testAbortRollbackMatrixTagsAndExpand() throws Exception {
    Map<String, String> beforeTags = ImmutableMap.of("env", "prod");
    Map<String, String> newTags = ImmutableMap.of("env", "staging", "owner", "platform");
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  new HashMap<>(beforeTags);
            });
    runPreCheckpointAbortRollbackMatrix(
        "tagsAndExpand",
        universe,
        u -> {
          enableManualEditRollback(u);
          factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
          UniverseDefinitionTaskParams params = performExpand(u, false /* move master */);
          params.expectedUniverseVersion = -1;
          params.getPrimaryCluster().userIntent.instanceTags = new HashMap<>(newTags);
          return params;
        },
        beforeTags,
        true /* assertExpandMarkVsMblOrder */,
        null /* beforeReplicationFactor */);
  }

  @Test
  public void testAbortRollbackMatrixOnPremExpand() throws Exception {
    Map<String, String> beforeTags =
        new HashMap<>(
            onPremUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags);
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    String instanceType =
        onPremUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceType;
    runPreCheckpointAbortRollbackMatrix(
        "onPremExpand",
        onPremUniverse,
        u -> {
          enableManualEditRollback(u);
          factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
          // Expand needs 2 free hosts; replenish each cycle in case prior abort left USED rows.
          while (NodeInstance.listByZone(zone.getUuid(), instanceType).size() < 2) {
            createOnpremInstance(zone);
          }
          UniverseDefinitionTaskParams params = performExpand(u, false /* move master */);
          params.expectedUniverseVersion = -1;
          return params;
        },
        beforeTags,
        true /* assertExpandMarkVsMblOrder */,
        null /* beforeReplicationFactor */);
  }

  @Test
  public void testAbortRollbackMatrixFullMove() throws Exception {
    Map<String, String> beforeTags =
        new HashMap<>(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags);
    runPreCheckpointAbortRollbackMatrix(
        "fullMove",
        defaultUniverse,
        u -> {
          enableManualEditRollback(u);
          factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
          setDumpEntitiesMock(u, "", false);
          UniverseDefinitionTaskParams params = performFullMove(u);
          params.expectedUniverseVersion = -1;
          // performFullMove mocks ADD master peers; membership check expects Live-before or empty.
          resetEmptyMasterMembershipMocks();
          when(mockClient.getLeaderMasterHostAndPort())
              .thenReturn(HostAndPort.fromHost(u.getMasters().get(0).cloudInfo.private_ip));
          return params;
        },
        beforeTags,
        true /* assertExpandMarkVsMblOrder */,
        null /* beforeReplicationFactor */);
  }

  @Test
  public void testAbortRollbackMatrixIncreaseRf() throws Exception {
    Universe universe = prepareUniverseForRfIncrease();
    Map<String, String> beforeTags =
        new HashMap<>(universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags);
    int beforeRf = universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor;
    runPreCheckpointAbortRollbackMatrix(
        "increaseRf",
        universe,
        u -> {
          enableManualEditRollback(u);
          factory.globalRuntimeConf().setValue(GlobalConfKeys.enableRFChange.getKey(), "true");
          factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
          UniverseDefinitionTaskParams params = performIncreaseRf(u, 3);
          params.expectedUniverseVersion = -1;
          return params;
        },
        beforeTags,
        false /* assertExpandMarkVsMblOrder */,
        beforeRf);
  }

  /**
   * Captures runtime EditUniverse subtask types (pause past Freeze), then aborts that capture run
   * and rolls it back so the universe is clean for the abort matrix.
   */
  private List<TaskType> captureEditSubTaskTypes(
      Universe universe, UniverseDefinitionTaskParams taskParams) throws Exception {
    setPausePosition(0);
    UUID taskUuid = commissioner.submit(TaskType.EditUniverse, taskParams);
    try {
      waitForTaskPaused(taskUuid);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUuid);
      Optional<Integer> freezeOpt =
          taskInfo.getSubTasks().stream()
              .filter(t -> t.getTaskType() == TaskType.FreezeUniverse)
              .map(TaskInfo::getPosition)
              .findFirst();
      int abortAfterCapture = 0;
      if (freezeOpt.isPresent()) {
        abortAfterCapture = freezeOpt.get() + 1;
        setPausePosition(abortAfterCapture);
        commissioner.resumeTask(taskUuid);
        waitForTaskPaused(taskUuid);
        taskInfo = TaskInfo.getOrBadRequest(taskUuid);
      }
      Map<Integer, List<TaskInfo>> byPosition =
          taskInfo.getSubTasks().stream()
              .collect(
                  Collectors.groupingBy(TaskInfo::getPosition, TreeMap::new, Collectors.toList()));
      List<TaskType> types =
          byPosition.values().stream()
              .map(tasks -> tasks.get(0).getTaskType())
              .collect(Collectors.toList());
      // Abort via resume+abort-position (same pattern as verifyTaskRetries).
      setAbortPosition(abortAfterCapture);
      commissioner.resumeTask(taskUuid);
      TaskInfo aborted = waitForTask(taskUuid);
      assertEquals(
          "capture edit must abort cleanly after Freeze; error=" + aborted.getErrorMessage(),
          Aborted,
          aborted.getTaskState());
      // Freeze already captured delta; roll back so the matrix starts from a clean universe.
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      if (universe.getStateTransitionDetails() != null
          && universe.getStateTransitionDetails().isRollbackSafe()) {
        stubNodeManagerOkWithInstancesPresent();
        clearAbortOrPausePositions();
        TaskInfo captureRollback = submitRollbackEditUniverse(aborted, universe);
        assertEquals(
            "capture rollback failed: " + captureRollback.getErrorMessage(),
            Success,
            captureRollback.getTaskState());
      }
      return types;
    } finally {
      clearAbortOrPausePositions();
    }
  }

  private void runPreCheckpointAbortRollbackMatrix(
      String actionName,
      Universe universe,
      Function<Universe, UniverseDefinitionTaskParams> paramsBuilder,
      Map<String, String> beforeTags,
      boolean assertExpandMarkVsMblOrder,
      Integer beforeReplicationFactor)
      throws Exception {
    // Abort x rollback loops spam logs; mirror verifyTaskRetries to avoid OOM in CI.
    ch.qos.logback.classic.Logger rootLogger =
        (ch.qos.logback.classic.Logger)
            LoggerFactory.getLogger(ch.qos.logback.classic.Logger.ROOT_LOGGER_NAME);
    rootLogger.detachAppender("ASYNCSTDOUT");

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    int nodesBefore = universe.getUniverseDetails().nodeDetailsSet.size();
    UniverseDefinitionTaskParams captureParams = paramsBuilder.apply(universe);
    List<TaskType> subTaskTypes = captureEditSubTaskTypes(universe, captureParams);

    int freezeIdx = subTaskTypes.indexOf(TaskType.FreezeUniverse);
    int markIdx = subTaskTypes.indexOf(TaskType.MarkRollbackUnsafe);
    assertTrue(actionName + ": FreezeUniverse must be present", freezeIdx >= 0);
    assertTrue(actionName + ": MarkRollbackUnsafe must be present", markIdx > freezeIdx);
    if (assertExpandMarkVsMblOrder) {
      int firstMbl = subTaskTypes.indexOf(TaskType.ModifyBlackList);
      int swapMbl = subTaskTypes.lastIndexOf(TaskType.ModifyBlackList);
      assertTrue(actionName + ": expected ADD blacklist before Mark", firstMbl >= 0);
      assertTrue(actionName + ": ADD blacklist must be before Mark", firstMbl < markIdx);
      assertTrue(actionName + ": swap ModifyBlackList must be after Mark", swapMbl > markIdx);
    }

    int startPos = freezeIdx + 1;
    for (int pos = startPos; pos < markIdx; pos++) {
      TaskType atPos = subTaskTypes.get(pos);
      String label = actionName + "@" + pos + ":" + atPos;
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertNull(
          label + ": universe must be clean before abort", universe.getStateTransitionDetails());
      assertEquals(
          label + ": node count before abort",
          nodesBefore,
          universe.getUniverseDetails().nodeDetailsSet.size());

      UniverseDefinitionTaskParams params = paramsBuilder.apply(universe);
      setAbortPosition(pos);
      TaskInfo failedEdit;
      try {
        failedEdit = submitTask(params);
      } finally {
        clearAbortOrPausePositions();
      }
      assertEquals(
          label + ": edit aborted; error=" + failedEdit.getErrorMessage(),
          Aborted,
          failedEdit.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertNotNull(label + ": delta present", universe.getStateTransitionDetails());
      assertTrue(
          label + ": still rollbackSafe", universe.getStateTransitionDetails().isRollbackSafe());
      assertTrue(
          label + ": canTaskRollbackDetailed", commissioner.canTaskRollbackDetailed(failedEdit));

      stubNodeManagerOkWithInstancesPresent();
      TaskInfo rollbackInfo = submitRollbackEditUniverse(failedEdit, universe);
      assertEquals(label + ": rollback success", Success, rollbackInfo.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(
          label + ": nodes restored",
          nodesBefore,
          universe.getUniverseDetails().nodeDetailsSet.size());
      assertEquals(
          label + ": tags restored",
          new HashMap<>(beforeTags),
          new HashMap<>(universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags));
      if (beforeReplicationFactor != null) {
        assertEquals(
            label + ": RF restored",
            (int) beforeReplicationFactor,
            universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor);
      }
      assertTrue(label + ": updateSucceeded", universe.getUniverseDetails().updateSucceeded);
      assertNull(label + ": delta cleared", universe.getStateTransitionDetails());
    }

    // Post-checkpoint sample: abort after MarkRollbackUnsafe has committed.
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams unsafeParams = paramsBuilder.apply(universe);
    setAbortPosition(markIdx + 1);
    TaskInfo unsafeEdit;
    try {
      unsafeEdit = submitTask(unsafeParams);
    } finally {
      clearAbortOrPausePositions();
    }
    assertEquals(actionName + ": post-Mark aborted", Aborted, unsafeEdit.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertFalse(
        actionName + ": post-Mark not rollbackSafe",
        universe.getStateTransitionDetails().isRollbackSafe());
    assertFalse(
        actionName + ": post-Mark detailed canRollback",
        commissioner.canTaskRollbackDetailed(unsafeEdit));
    TaskInfo rejected = submitRollbackEditUniverse(unsafeEdit, universe);
    assertEquals(actionName + ": post-Mark rollback rejected", Failure, rejected.getTaskState());
    assertThat(rejected.getErrorMessage(), containsString("rollback checkpoint was crossed"));
    // Leave universe unlocked but with unsafe delta ownership from failed edit; clear for
    // isolation.
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().updateInProgress = false;
          u.getUniverseDetails().updatingTaskUUID = null;
          u.getUniverseDetails().placementModificationTaskUuid = null;
          u.setStateTransitionDetails(null);
        });
  }

  private void enableManualEditRollback(Universe universe) {
    factory.globalRuntimeConf().setValue("yb.task.enable_edit_auto_rollback", "false");
    factory.globalRuntimeConf().setValue("yb.task.allow_edit_universe_rollback", "true");
    factory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    factory.forUniverse(universe).setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
  }

  /**
   * Mirrors listing {@code canRollback}: builds the allow-retry/placement map from in-memory
   * universe details (H2 unit tests cannot run {@code getUniverseDetailsField} JSONB SQL).
   */
  private boolean listingCanRollback(
      CustomerTask customerTask, TaskInfo taskInfo, Universe universe) {
    // Re-fetch so the OneToOne TaskInfo join is populated (create() does not load it).
    CustomerTask loaded =
        CustomerTask.getOrBadRequest(customerTask.getCustomerUUID(), customerTask.getTaskUUID());
    Map<UUID, Set<String>> updatingTasks = new HashMap<>();
    Set<String> ids = new java.util.HashSet<>();
    if (universe.getUniverseDetails().updatingTaskUUID != null) {
      ids.add(universe.getUniverseDetails().updatingTaskUUID.toString());
    }
    if (universe.getUniverseDetails().placementModificationTaskUuid != null) {
      ids.add(universe.getUniverseDetails().placementModificationTaskUuid.toString());
    }
    updatingTasks.put(loaded.getTargetUUID(), ids);
    Map<UUID, CustomerTask> lastTaskByTarget = new HashMap<>();
    lastTaskByTarget.put(loaded.getTargetUUID(), loaded);
    ObjectNode status =
        commissioner
            .buildTaskStatus(loaded, taskInfo.getSubTasks(), updatingTasks, lastTaskByTarget)
            .orElseThrow(() -> new AssertionError("expected task status"));
    return status.get("canRollback").asBoolean();
  }

  private Set<String> privateIpsFromModifyBlackListRemoveNodes(TaskInfo taskInfo) {
    return taskInfo.getSubTasks().stream()
        .filter(t -> t.getTaskType() == TaskType.ModifyBlackList)
        .map(TaskInfo::getTaskParams)
        .map(params -> params.path("removeNodes"))
        .filter(JsonNode::isArray)
        .flatMap(arr -> com.google.common.collect.Streams.stream(arr.elements()))
        .map(node -> node.path("cloudInfo").path("private_ip").asText(null))
        .filter(Objects::nonNull)
        .filter(ip -> !ip.isBlank())
        .collect(Collectors.toCollection(HashSet::new));
  }

  private void resetEmptyMasterMembershipMocks() {
    try {
      ListMasterRaftPeersResponse emptyPeers = mock(ListMasterRaftPeersResponse.class);
      lenient().when(emptyPeers.getPeersList()).thenReturn(Collections.emptyList());
      lenient().when(mockClient.listMasterRaftPeers()).thenReturn(emptyPeers);
      ListLiveTabletServersResponse emptyTs = mock(ListLiveTabletServersResponse.class);
      lenient().when(emptyTs.getTabletServers()).thenReturn(new ArrayList<>());
      lenient().when(mockClient.listLiveTabletServers()).thenReturn(emptyTs);
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  private void stubNodeManagerOkWithInstancesPresent() {
    ShellResponse ok = new ShellResponse();
    ok.message = "";
    ShellResponse listWithInstance = new ShellResponse();
    listWithInstance.message = "[{\"id\":\"i-mock\"}]";
    doAnswer(
            invocation -> {
              // Keep Precheck working so subsequent matrix edit cycles (esp. on-prem) can run.
              if (NodeManager.NodeCommandType.Precheck.equals(invocation.getArgument(0))) {
                return preflightResponse;
              }
              if (NodeManager.NodeCommandType.List.equals(invocation.getArgument(0))) {
                return listWithInstance;
              }
              return ok;
            })
        .when(mockNodeManager)
        .nodeCommand(any(), any());
  }

  private TaskInfo submitRollbackEditUniverse(TaskInfo failedEdit, Universe universe)
      throws InterruptedException {
    UniverseDefinitionTaskParams rollbackParams =
        Json.fromJson(failedEdit.getTaskParams(), UniverseDefinitionTaskParams.class);
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    return waitForTask(commissioner.submit(TaskType.RollbackEditUniverse, rollbackParams));
  }
}
