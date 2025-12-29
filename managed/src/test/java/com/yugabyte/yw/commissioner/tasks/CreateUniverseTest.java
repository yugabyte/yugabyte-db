// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.DoCapacityReservation;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class CreateUniverseTest extends UniverseModifyBaseTest {

  private static final List<TaskType> UNIVERSE_CREATE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.PersistUseClockbound,
          TaskType.InstanceExistCheck,
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks, // PreNodeProvision
          TaskType.SetupYNP,
          TaskType.YNPProvisioning,
          TaskType.InstallNodeAgent,
          TaskType.SetNodeStatus,
          TaskType.RunHooks, // PostNodeProvision
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.ValidateGFlags,
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.AnsibleConfigureServers, // GFlags
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.WaitForMasterLeader,
          TaskType.AnsibleConfigureServers,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForServerReady,
          TaskType.WaitForServer, // wait for postgres
          TaskType.WaitForTServerHeartBeats,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.CreateAlertDefinitions,
          TaskType.CreateTable,
          TaskType.UpdateConsistencyCheck,
          TaskType.ChangeAdminPassword,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> UNIVERSE_CREATE_TASK_RETRY_SEQUENCE =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.PersistUseClockbound,
          TaskType.InstanceExistCheck,
          TaskType.ValidateGFlags,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl, // master
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl, // tserver
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.WaitForMasterLeader,
          TaskType.AnsibleConfigureServers,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForServerReady,
          TaskType.WaitForServer, // wait for postgres
          TaskType.WaitForTServerHeartBeats,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.CreateAlertDefinitions,
          TaskType.CreateTable,
          TaskType.UpdateConsistencyCheck,
          TaskType.ChangeAdminPassword,
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
    // ChangeConfigResponse mockChangeConfigResponse = mock(ChangeConfigResponse.class);
    ListTabletServersResponse mockListTabletServersResponse = mock(ListTabletServersResponse.class);
    when(mockListTabletServersResponse.getTabletServersCount()).thenReturn(10);

    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.listTabletServers()).thenReturn(mockListTabletServersResponse);
      IsServerReadyResponse mockServerReadyResponse = mock(IsServerReadyResponse.class);
      when(mockServerReadyResponse.getNumNotRunningTablets()).thenReturn(0);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockServerReadyResponse);
      mockClockSyncResponse(mockNodeUniverseManager);
      mockLocaleCheckResponse(mockNodeUniverseManager);
    } catch (Exception e) {
      fail();
    }
    mockWaits(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    doAnswer(
            inv -> {
              return null;
            })
        .when(mockYcqlQueryExecutor)
        .validateAdminPassword(any(), any());
    doAnswer(
            inv -> {
              return null;
            })
        .when(mockYsqlQueryExecutor)
        .validateAdminPassword(any(), any());
    ShellResponse successResponse = new ShellResponse();
    successResponse.message = "Command output:\nCREATE TABLE";
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), (any()), anyBoolean()))
        .thenReturn(successResponse);
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private UniverseDefinitionTaskParams getTaskParams(boolean enableAuth) {
    return getTaskParams(enableAuth, defaultUniverse);
  }

  private UniverseDefinitionTaskParams getTaskParams(boolean enableAuth, Universe universe) {
    Universe result =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              Cluster primaryCluster = universeDetails.getPrimaryCluster();
              primaryCluster.userIntent.enableYCQL = enableAuth;
              primaryCluster.userIntent.enableYCQLAuth = enableAuth;
              primaryCluster.userIntent.ycqlPassword = "Admin@123";
              primaryCluster.userIntent.enableYSQL = enableAuth;
              primaryCluster.userIntent.enableYSQLAuth = enableAuth;
              primaryCluster.userIntent.ysqlPassword = "Admin@123";
              primaryCluster.userIntent.enableYEDIS = false;
              primaryCluster.userIntent.useSystemd = true;
              for (NodeDetails node : universeDetails.nodeDetailsSet) {
                // Reset for creation.
                node.state = NodeDetails.NodeState.ToBeAdded;
                node.isMaster = false;
                node.nodeName = null;
              }
            });
    UniverseDefinitionTaskParams universeDetails = result.getUniverseDetails();
    universeDetails.creatingUser = defaultUser;
    universeDetails.setUniverseUUID(universe.getUniverseUUID());
    universeDetails.setPreviousTaskUUID(null);
    return universeDetails;
  }

  @Test
  public void testCreateUniverseSuccess() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_SEQUENCE, subTasksByPosition);
  }

  @Test
  public void testCreateUniverseWithCRAzureSuccess() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");

    Region region1 = Region.create(azuProvider, "region-1", "region-1", "yb-image");
    AvailabilityZone zone1 = AvailabilityZone.getOrCreate(region1, "zone-1", "zone-1", "subnet");
    Region region2 = Region.create(azuProvider, "region-2", "region-2", "yb-image");
    AvailabilityZone zone2 = AvailabilityZone.getOrCreate(region2, "zone-2", "zone-2", "subnet");
    AvailabilityZone zone3 = AvailabilityZone.getOrCreate(region2, "zone-3", "zone-3", "subnet");
    Universe universe = createUniverseForProvider("universe-test", azuProvider);
    List<AvailabilityZone> zones = Arrays.asList(zone1, zone2, zone3);
    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    PlacementInfo placementInfo = new PlacementInfo();
    for (AvailabilityZone zone : zones) {
      PlacementInfoUtil.addPlacementZone(zone.getUuid(), placementInfo);
    }
    taskParams.getPrimaryCluster().placementInfo = placementInfo;
    int i = 0;
    for (NodeDetails nodeDetails : taskParams.nodeDetailsSet) {
      AvailabilityZone zone = zones.get(i++ % zones.size());
      nodeDetails.cloudInfo.az = zone.getCode();
      nodeDetails.azUuid = zone.getUuid();
    }
    String overridenInstanceType = "Standard_D8as_v4";
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setInstanceType(overridenInstanceType);
    userIntentOverrides.setAzOverrides(new HashMap<>());
    userIntentOverrides.getAzOverrides().put(zone2.getUuid(), azOverrides);
    userIntentOverrides.getAzOverrides().put(zone3.getUuid(), azOverrides);
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    UUID universeUUID = taskParams.getUniverseUUID();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universeUUID);

    Map<UUID, List<String>> nodesByAZ = new HashMap<>();
    universe
        .getNodes()
        .forEach(
            n -> {
              nodesByAZ.computeIfAbsent(n.azUuid, x -> new ArrayList<>()).add(n.nodeName);
            });
    verifyCapacityReservationAZU(
        universe.getUniverseUUID(),
        AzureReservationGroup.of(
            region1,
            Map.of(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", nodesByAZ.get(zone1.getUuid())))),
        AzureReservationGroup.of(
            region2,
            Map.of(
                overridenInstanceType,
                Map.of("2", nodesByAZ.get(zone2.getUuid()), "3", nodesByAZ.get(zone3.getUuid())))));

    List<String> nonZ1Nodes = new ArrayList<>();
    nonZ1Nodes.addAll(nodesByAZ.get(zone2.getUuid()));
    nonZ1Nodes.addAll(nodesByAZ.get(zone3.getUuid()));

    verifyNodeInteractionsCapacityReservation(
        42,
        NodeManager.NodeCommandType.Create,
        param -> ((AnsibleCreateServer.Params) param).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region1.getCode()),
            nodesByAZ.get(zone1.getUuid()),
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region2.getCode()),
            nonZ1Nodes));
  }

  @Test
  public void testCreateUniverseWithCRAzureSameZoneN() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");

    Region region1 = Region.create(azuProvider, "us-west", "us-west", "yb-image");
    AvailabilityZone zone1 =
        AvailabilityZone.getOrCreate(region1, "us-west-1", "us-west1", "subnet");
    Region region2 = Region.create(azuProvider, "us-east", "us-east", "yb-image");
    AvailabilityZone zone2 =
        AvailabilityZone.getOrCreate(region2, "us-east-1", "us-east1", "subnet");
    Universe universe = createUniverseForProvider("universe-test", azuProvider);
    List<AvailabilityZone> zones = Arrays.asList(zone1, zone2);
    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    PlacementInfo placementInfo = new PlacementInfo();
    AtomicInteger idx = new AtomicInteger();
    for (AvailabilityZone zone : zones) {
      PlacementInfoUtil.addPlacementZone(
          zone.getUuid(), placementInfo, idx.incrementAndGet(), idx.get());
    }
    taskParams.getPrimaryCluster().placementInfo = placementInfo;
    int i = 0;
    for (NodeDetails nodeDetails : taskParams.nodeDetailsSet) {
      AvailabilityZone zone = zones.get(i++ % zones.size());
      nodeDetails.cloudInfo.az = zone.getCode();
      nodeDetails.azUuid = zone.getUuid();
    }
    UUID universeUUID = taskParams.getUniverseUUID();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universeUUID);

    Map<UUID, List<String>> nodesByAZ = new HashMap<>();
    universe
        .getNodes()
        .forEach(
            n -> {
              nodesByAZ.computeIfAbsent(n.azUuid, x -> new ArrayList<>()).add(n.nodeName);
            });
    verifyCapacityReservationAZU(
        universe.getUniverseUUID(),
        AzureReservationGroup.of(
            region1,
            Map.of(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", nodesByAZ.get(zone1.getUuid())))),
        AzureReservationGroup.of(
            region2,
            Map.of(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", nodesByAZ.get(zone2.getUuid())))));

    verifyNodeInteractionsCapacityReservation(
        42,
        NodeManager.NodeCommandType.Create,
        param -> ((AnsibleCreateServer.Params) param).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region1.getCode()),
            nodesByAZ.get(zone1.getUuid()),
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region2.getCode()),
            nodesByAZ.get(zone2.getUuid())));
  }

  @Test
  public void testCreateUniverseWithCRAzureFail() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");
    failsOnCapacityReservation = 1;

    Region region1 = Region.create(azuProvider, "region-1", "region-1", "yb-image");
    AvailabilityZone zone1 = AvailabilityZone.getOrCreate(region1, "zone-1", "zone-1", "subnet");
    Universe universe = createUniverseForProvider("universe-test", azuProvider);
    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());

    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.capacityReservationMaxRetries.getKey(), "2");

    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    taskParams = Json.fromJson(taskInfo.getTaskParams(), UniverseDefinitionTaskParams.class);
    taskParams.setPreviousTaskUUID(taskInfo.getUuid());
    // Retry the task.
    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCreateUniverseWithCRAzureUnsupportedTypeSuccess() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");

    Region region1 = Region.create(azuProvider, "region-1", "region-1", "yb-image");
    AvailabilityZone zone1 = AvailabilityZone.getOrCreate(region1, "zone-1", "zone-1", "subnet");
    Region region2 = Region.create(azuProvider, "region-2", "region-2", "yb-image");
    AvailabilityZone zone2 = AvailabilityZone.getOrCreate(region2, "zone-2", "zone-2", "subnet");
    AvailabilityZone zone3 = AvailabilityZone.getOrCreate(region2, "zone-3", "zone-3", "subnet");
    Universe universe = createUniverseForProvider("universe-test", azuProvider);
    List<AvailabilityZone> zones = Arrays.asList(zone1, zone2, zone3);
    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    PlacementInfo placementInfo = new PlacementInfo();
    for (AvailabilityZone zone : zones) {
      PlacementInfoUtil.addPlacementZone(zone.getUuid(), placementInfo);
    }
    taskParams.getPrimaryCluster().placementInfo = placementInfo;
    int i = 0;
    for (NodeDetails nodeDetails : taskParams.nodeDetailsSet) {
      AvailabilityZone zone = zones.get(i++ % zones.size());
      nodeDetails.cloudInfo.az = zone.getCode();
      nodeDetails.azUuid = zone.getUuid();
    }
    String overridenInstanceType = "Standard_D2"; // Unsupported type.
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setInstanceType(overridenInstanceType);
    userIntentOverrides.setAzOverrides(new HashMap<>());
    userIntentOverrides.getAzOverrides().put(zone2.getUuid(), azOverrides);
    userIntentOverrides.getAzOverrides().put(zone3.getUuid(), azOverrides);
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    UUID universeUUID = taskParams.getUniverseUUID();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universeUUID);

    Map<UUID, List<String>> nodesByAZ = new HashMap<>();
    universe
        .getNodes()
        .forEach(
            n -> {
              nodesByAZ.computeIfAbsent(n.azUuid, x -> new ArrayList<>()).add(n.nodeName);
            });
    verifyCapacityReservationAZU(
        universe.getUniverseUUID(),
        AzureReservationGroup.of(
            region1,
            Map.of(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", nodesByAZ.get(zone1.getUuid())))));

    String region2Group =
        DoCapacityReservation.getCapacityReservationGroupName(
            universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region2.getCode());
    verify(azuResourceGroupApiClient, times(0))
        .createCapacityReservation(
            Mockito.eq(region2Group),
            Mockito.eq(region2.getCode()),
            Mockito.anyString(),
            Mockito.anyString(),
            Mockito.anyString(),
            Mockito.anyInt(),
            Mockito.anyMap());

    verifyNodeInteractionsCapacityReservation(
        42,
        NodeManager.NodeCommandType.Create,
        param -> ((AnsibleCreateServer.Params) param).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region1.getCode()),
            nodesByAZ.get(zone1.getUuid())));
  }

  @Test
  public void testCreateUniverseWithCapacityReservationAwsSuccess() {

    RuntimeConfigEntry.upsertGlobal(ProviderConfKeys.enableCapacityReservationAws.getKey(), "true");
    Region region1 = Region.getByCode(defaultProvider, "region-1");
    AvailabilityZone zone1 = AvailabilityZone.getOrCreate(region1, "az-1", "az 1", "subnet");
    Region region2 = Region.create(defaultProvider, "region-2", "region-2", "yb-image");
    AvailabilityZone zone2 = AvailabilityZone.getOrCreate(region2, "az-4", "az 4", "subnet");
    AvailabilityZone zone3 = AvailabilityZone.getOrCreate(region2, "az-5", "az 5", "subnet");
    Universe universe = createUniverseForProvider("universe-test", defaultProvider);
    List<AvailabilityZone> zones = Arrays.asList(zone1, zone2, zone3);
    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    PlacementInfo placementInfo = new PlacementInfo();
    for (AvailabilityZone zone : zones) {
      PlacementInfoUtil.addPlacementZone(zone.getUuid(), placementInfo);
    }
    taskParams.getPrimaryCluster().placementInfo = placementInfo;
    int i = 0;
    for (NodeDetails nodeDetails : taskParams.nodeDetailsSet) {
      AvailabilityZone zone = zones.get(i++ % zones.size());
      nodeDetails.cloudInfo.az = zone.getCode();
      nodeDetails.azUuid = zone.getUuid();
    }
    String overridenInstanceType = "m5.large";
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setInstanceType(overridenInstanceType);
    userIntentOverrides.setAzOverrides(new HashMap<>());
    userIntentOverrides.getAzOverrides().put(zone2.getUuid(), azOverrides);
    userIntentOverrides.getAzOverrides().put(zone3.getUuid(), azOverrides);
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    UUID universeUUID = taskParams.getUniverseUUID();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universeUUID);

    Map<UUID, List<String>> nodesByAZ = new HashMap<>();
    universe
        .getNodes()
        .forEach(
            n -> {
              nodesByAZ.computeIfAbsent(n.azUuid, x -> new ArrayList<>()).add(n.nodeName);
            });

    verifyCapacityReservationAws(
        universe.getUniverseUUID(),
        Map.of(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
            Map.of("1", new ZoneData("region-1", nodesByAZ.get(zone1.getUuid())))),
        Map.of(
            overridenInstanceType,
            Map.of(
                "4", new ZoneData("region-2", nodesByAZ.get(zone2.getUuid())),
                "5", new ZoneData("region-2", nodesByAZ.get(zone3.getUuid())))));

    verifyNodeInteractionsCapacityReservation(
        42,
        NodeManager.NodeCommandType.Create,
        param -> ((AnsibleCreateServer.Params) param).capacityReservation,
        Map.of(
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-1",
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType),
            nodesByAZ.get(zone1.getUuid()),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-4",
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType),
            nodesByAZ.get(zone2.getUuid()),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-5",
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType),
            nodesByAZ.get(zone3.getUuid())));
  }

  @Test
  public void testCreateUniverseRRWithCRAzureSuccess() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");

    Region region1 = Region.create(azuProvider, "region-1", "region-1", "yb-image");
    AvailabilityZone zone1 = AvailabilityZone.getOrCreate(region1, "zone-1", "zone-1", "subnet");
    Region region2 = Region.create(azuProvider, "region-2", "region-2", "yb-image");
    AvailabilityZone zone2 = AvailabilityZone.getOrCreate(region2, "zone-2", "zone-2", "subnet");
    AvailabilityZone zone3 = AvailabilityZone.getOrCreate(region2, "zone-3", "zone-3", "subnet");
    AvailabilityZone zone4 = AvailabilityZone.getOrCreate(region2, "zone-4", "zone-4", "subnet");

    Universe universe = createUniverseForProvider("universe-test", azuProvider);
    String rrInstanceType = "Standard_D4as_v4";

    UniverseDefinitionTaskParams.UserIntent rrIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrIntent.instanceType = rrInstanceType;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(zone2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(zone3.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(zone4.getUuid(), pi, 1, 1, false);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, pi));

    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    UUID universeUUID = taskParams.getUniverseUUID();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universeUUID);

    Map<String, String> readonlyNodes = new HashMap<>();
    UUID rrClusterID = universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid;

    universe
        .getNodesInCluster(rrClusterID)
        .forEach(
            n -> {
              String zoneID =
                  String.valueOf(
                      DoCapacityReservation.extractZoneNumber(
                          AvailabilityZone.getOrBadRequest(n.azUuid).getCode()));
              readonlyNodes.put(zoneID, n.nodeName);
            });

    verifyCapacityReservationAZU(
        universe.getUniverseUUID(),
        AzureReservationGroup.of(
            region1,
            Map.of(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", Arrays.asList("host-n1", "host-n2", "host-n3")))),
        AzureReservationGroup.of(
            region2,
            Map.of(
                rrInstanceType,
                Map.of(
                    "2", Arrays.asList(readonlyNodes.get("2")),
                    "3", Arrays.asList(readonlyNodes.get("3")),
                    "4", Arrays.asList(readonlyNodes.get("4"))))));

    verifyNodeInteractionsCapacityReservation(
        75,
        NodeManager.NodeCommandType.Create,
        params -> ((AnsibleCreateServer.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region1.getCode()),
            Arrays.asList("host-n1", "host-n2", "host-n3"),
            DoCapacityReservation.getCapacityReservationGroupName(
                universeUUID, UniverseDefinitionTaskParams.ClusterType.PRIMARY, region2.getCode()),
            Arrays.asList("host-readonly1-n1", "host-readonly1-n2", "host-readonly1-n3")));
  }

  @Test
  public void testCreateUniverseRRWithCRAwsSuccess() {
    RuntimeConfigEntry.upsertGlobal(ProviderConfKeys.enableCapacityReservationAws.getKey(), "true");

    Region region1 = Region.getByCode(defaultProvider, "region-1");
    AvailabilityZone zone1 = AvailabilityZone.getOrCreate(region1, "az-1", "az 1", "subnet");
    Region region2 = Region.create(defaultProvider, "region-2", "region-2", "yb-image");
    AvailabilityZone zone2 = AvailabilityZone.getOrCreate(region2, "az-4", "az 4", "subnet");
    AvailabilityZone zone3 = AvailabilityZone.getOrCreate(region2, "az-5", "az 5", "subnet");
    AvailabilityZone zone4 = AvailabilityZone.getOrCreate(region2, "az-6", "az 6", "subnet");

    Universe universe = createUniverseForProvider("universe-test", defaultProvider);
    String rrInstanceType = "m5.large";

    UniverseDefinitionTaskParams.UserIntent rrIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrIntent.instanceType = rrInstanceType;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(zone2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(zone3.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(zone4.getUuid(), pi, 1, 1, false);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, pi));

    UniverseDefinitionTaskParams taskParams = getTaskParams(false, universe);
    UUID universeUUID = taskParams.getUniverseUUID();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universeUUID);

    Map<String, String> readonlyNodes = new HashMap<>();
    UUID rrClusterID = universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid;

    universe
        .getNodesInCluster(rrClusterID)
        .forEach(
            n -> {
              String zoneID =
                  String.valueOf(
                      DoCapacityReservation.extractZoneNumber(
                          AvailabilityZone.getOrBadRequest(n.azUuid).getCode()));
              readonlyNodes.put(zoneID, n.nodeName);
            });

    verifyCapacityReservationAws(
        universe.getUniverseUUID(),
        Map.of(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
            Map.of("1", new ZoneData("region-1", Arrays.asList("host-n3", "host-n2", "host-n1")))),
        Map.of(
            rrInstanceType,
            Map.of(
                "4", new ZoneData("region-2", Arrays.asList(readonlyNodes.get("4"))),
                "5", new ZoneData("region-2", Arrays.asList(readonlyNodes.get("5"))),
                "6", new ZoneData("region-2", Arrays.asList(readonlyNodes.get("6"))))));

    verifyNodeInteractionsCapacityReservation(
        75,
        NodeManager.NodeCommandType.Create,
        param -> ((AnsibleCreateServer.Params) param).capacityReservation,
        Map.of(
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-1",
                universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType),
            Arrays.asList("host-n1", "host-n2", "host-n3"),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-4",
                rrInstanceType),
            Arrays.asList(readonlyNodes.get("4")),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-5",
                rrInstanceType),
            Arrays.asList(readonlyNodes.get("5")),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                universe.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-6",
                rrInstanceType),
            Arrays.asList(readonlyNodes.get("6"))));
  }

  @Test
  public void testCreateUniverseRetrySuccess() {
    // Fail the task for the first run.
    when(mockClient.waitForServer(any(), anyLong()))
        .thenThrow(new RuntimeException())
        .thenReturn(true);
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_SEQUENCE, subTasksByPosition);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    taskParams = Json.fromJson(taskInfo.getTaskParams(), UniverseDefinitionTaskParams.class);
    taskParams.setPreviousTaskUUID(taskInfo.getUuid());
    // Retry the task.
    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    subTasks = taskInfo.getSubTasks();
    subTasksByPosition = subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_RETRY_SEQUENCE, subTasksByPosition);
  }

  @Test
  public void testCreateUniverseRetryFailure() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(UNIVERSE_CREATE_TASK_SEQUENCE, subTasksByPosition);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    taskParams = Json.fromJson(taskInfo.getTaskParams(), UniverseDefinitionTaskParams.class);
    taskParams.setPreviousTaskUUID(taskInfo.getUuid());
    primaryCluster.userIntent.enableYCQL = true;
    primaryCluster.userIntent.enableYCQLAuth = true;
    primaryCluster.userIntent.ycqlPassword = "Admin@123";
    primaryCluster.userIntent.enableYSQL = true;
    primaryCluster.userIntent.enableYSQLAuth = true;
    primaryCluster.userIntent.ysqlPassword = "Admin@123";
    primaryCluster.userIntent.useSystemd = true;
    taskInfo = submitTask(taskParams);
    // Task is already successful, so the passwords must have been cleared.
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testCreateDedicatedUniverseSuccess() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    taskParams.getPrimaryCluster().userIntent.dedicatedNodes = true;
    PlacementInfoUtil.SelectMastersResult selectMastersResult =
        PlacementInfoUtil.selectMasters(
            null, taskParams.nodeDetailsSet, n -> true, true, taskParams.clusters);
    selectMastersResult.addedMasters.forEach(taskParams.nodeDetailsSet::add);
    PlacementInfoUtil.dedicateNodes(taskParams.nodeDetailsSet);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Map<UniverseTaskBase.ServerType, List<NodeDetails>> byDedicatedType =
        defaultUniverse.getNodes().stream().collect(Collectors.groupingBy(n -> n.dedicatedTo));
    List<NodeDetails> masterNodes = byDedicatedType.get(UniverseTaskBase.ServerType.MASTER);
    List<NodeDetails> tserverNodes = byDedicatedType.get(UniverseTaskBase.ServerType.TSERVER);
    assertEquals(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor,
        masterNodes.size());
    assertEquals(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes,
        tserverNodes.size());
    for (NodeDetails masterNode : masterNodes) {
      assertTrue(masterNode.isMaster);
      assertFalse(masterNode.isTserver);
    }
    for (NodeDetails tserverNode : tserverNodes) {
      assertFalse(tserverNode.isMaster);
      assertTrue(tserverNode.isTserver);
    }
  }

  @Test
  public void testCreateUniverseWithReadReplicaSuccess() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    UniverseDefinitionTaskParams.UserIntent intent =
        taskParams.getPrimaryCluster().userIntent.clone();
    intent.replicationFactor = 1;
    intent.numNodes = 1;
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            UniverseDefinitionTaskParams.ClusterType.ASYNC,
            intent,
            1,
            null,
            Collections.emptyList());
    Universe updated =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(intent, placementInfo));
    taskParams.clusters.add(updated.getUniverseDetails().getReadOnlyClusters().get(0));
    taskParams.nodeDetailsSet = updated.getUniverseDetails().nodeDetailsSet;
    taskParams.nodeDetailsSet.forEach(
        node -> {
          node.nodeName = null;
          node.state = NodeDetails.NodeState.ToBeAdded;
        });
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    int tserversStarted =
        (int)
            taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskType() == TaskType.AnsibleClusterServerCtl)
                .map(t -> t.getTaskParams())
                .filter(t -> t.has("process") && t.get("process").asText().equals("tserver"))
                .filter(t -> t.has("command") && t.get("command").asText().equals("start"))
                .count();
    assertEquals(taskParams.getPrimaryCluster().userIntent.numNodes + 1, tserversStarted);
  }

  @Test
  public void testCreateUniverseRetries() {
    UniverseDefinitionTaskParams taskParams = getTaskParams(true);
    UniverseDefinitionTaskParams.UserIntent intent =
        taskParams.getPrimaryCluster().userIntent.clone();
    intent.replicationFactor = 1;
    intent.numNodes = 1;
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            UniverseDefinitionTaskParams.ClusterType.ASYNC,
            intent,
            1,
            null,
            Collections.emptyList());
    Universe updated =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(intent, placementInfo));
    taskParams.clusters.add(updated.getUniverseDetails().getReadOnlyClusters().get(0));
    taskParams.nodeDetailsSet = updated.getUniverseDetails().nodeDetailsSet;
    taskParams.nodeDetailsSet.forEach(
        node -> {
          node.nodeName = null;
          node.state = NodeDetails.NodeState.ToBeAdded;
        });
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Create,
        CustomerTask.TargetType.Universe,
        updated.getUniverseUUID(),
        TaskType.CreateUniverse,
        taskParams);
    checkUniverseNodesStates(updated.getUniverseUUID());
  }
}
