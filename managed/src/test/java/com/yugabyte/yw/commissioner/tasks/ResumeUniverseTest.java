// Copyright (c) YugabyteDB, Inc.

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
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.subtasks.DoCapacityReservation;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResumeServer;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ResumeUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private KmsConfig testKMSConfig;
  private int expectedUniverseVersion = 2;

  @Before
  public void setUp() {
    YBClient mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      mockClockSyncResponse(mockNodeUniverseManager);
      IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean())).thenReturn(okReadyResp);
    } catch (Exception e) {
      fail();
    }
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    testKMSConfig =
        KmsConfig.createKMSConfig(
            defaultCustomer.getUuid(),
            KeyProvider.AWS,
            Json.newObject().put("test_key", "test_val"),
            "some config name");
    when(mockNodeAgentClient.isClientEnabled(any(), any())).thenReturn(true);
  }

  private void setupUniverse(boolean updateInProgress, int numOfNodes) {
    setupUniverse(defaultProvider, updateInProgress, numOfNodes);
  }

  private Pair<Region, InstanceType> setupProvider(Provider provider) {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    String instanceType =
        provider.getCloudCode() == Common.CloudType.azu ? "Standard_D4as_v4" : "c3.xlarge";
    InstanceType i =
        InstanceType.upsert(
            provider.getUuid(), instanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());
    return new Pair<>(r, i);
  }

  private void setupUniverse(Provider provider, boolean updateInProgress, int numOfNodes) {
    Pair<Region, InstanceType> s = setupProvider(provider);

    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(s.getFirst(), provider, s.getSecond(), numOfNodes);
    userIntent.replicationFactor = numOfNodes;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "universe-test";

    defaultUniverse =
        createUniverse(userIntent.universeName, defaultCustomer.getId(), provider.getCloudCode());
    String nodePrefix = "demo-universe";
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent, nodePrefix, true /* setMasters */, updateInProgress));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              u.getUniverseDetails()
                  .nodeDetailsSet
                  .forEach(n -> n.state = NodeDetails.NodeState.Stopped);
            });
  }

  private static final List<Pair<TaskType, JsonNode>> RESUME_UNIVERSE_TASKS =
      ImmutableList.of(
          new Pair<>(TaskType.FreezeUniverse, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.ResumeServer, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.InstallNodeAgent, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.WaitForNodeAgent, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.WaitForClockSync, Json.toJson(ImmutableMap.of())),
          new Pair<>(
              TaskType.AnsibleClusterServerCtl,
              Json.toJson(ImmutableMap.of("process", "master", "command", "start"))),
          new Pair<>(TaskType.WaitForServer, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.SetActiveUniverseKeys, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.WaitForServerReady, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.WaitForClockSync, Json.toJson(ImmutableMap.of())),
          new Pair<>(
              TaskType.AnsibleClusterServerCtl,
              Json.toJson(ImmutableMap.of("process", "tserver", "command", "start"))),
          new Pair<>(TaskType.WaitForServer, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.WaitForServerReady, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.SetNodeState, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.ManageAlertDefinitions, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.SwamperTargetsFileUpdate, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.MarkSourceMetric, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.UpdateUniverseFields, Json.toJson(ImmutableMap.of())),
          new Pair<>(TaskType.UniverseUpdateSucceeded, Json.toJson(ImmutableMap.of())));

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      List<Pair<TaskType, JsonNode>> expectedTaskSequence,
      Set<Integer> skipPositions) {
    int position = 0;
    for (Pair<TaskType, JsonNode> pair : expectedTaskSequence) {
      if (skipPositions.contains(position)) {
        continue;
      }
      TaskType taskType = pair.getFirst();
      JsonNode expectedResults = pair.getSecond();
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(taskType, tasks.get(0).getTaskType());
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(ResumeUniverse.Params taskParams) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = expectedUniverseVersion;
    try {
      UUID taskUUID = commissioner.submit(TaskType.ResumeUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testResumeUniverseSuccess() {
    setupUniverse(false, 1);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, RESUME_UNIVERSE_TASKS, ImmutableSet.of(2, 3, 7));
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testResumeUniverseWithCRAzureSuccess() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");
    setupUniverse(azuProvider, false, 3);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    Region region = Region.getByCode(azuProvider, "region-1");
    verifyCapacityReservationAZU(
        defaultUniverse.getUniverseUUID(),
        AzureReservationGroup.of(
            region,
            Map.of(
                defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", Arrays.asList("host-n1", "host-n2", "host-n3")))));

    verifyNodeInteractionsCapacityReservation(
        12,
        NodeManager.NodeCommandType.Resume,
        params -> ((ResumeServer.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                defaultUniverse.getUniverseUUID(),
                CommonUtils.getClusterType(region.getProvider(), defaultUniverse),
                region.getCode()),
            Arrays.asList("host-n1", "host-n2", "host-n3")));
  }

  @Test
  public void testResumeUniverseWithCRAzureRRSuccess() {
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");
    setupUniverse(azuProvider, false, 3);

    Provider azuProvider2 =
        Provider.create(defaultCustomer.getUuid(), Common.CloudType.azu, "AzureForRR");
    setupProvider(azuProvider2);

    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent rrUserIntent = curIntent.clone();
    rrUserIntent.provider = azuProvider2.getUuid().toString();
    rrUserIntent.replicationFactor = 3;
    AvailabilityZone az = AvailabilityZone.getByCode(azuProvider2, "az-1");

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), pi, 3, 3, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(
                        node -> {
                          node.state = NodeDetails.NodeState.Stopped;
                          if (!node.placementUuid.equals(
                              universe.getUniverseDetails().getPrimaryCluster().uuid)) {
                            node.nodeName += "-readonly";
                          }
                        }));

    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    Region region = Region.getByCode(azuProvider, "region-1");
    Region region1 = Region.getByCode(azuProvider2, "region-1");
    verifyCapacityReservationAZU(
        defaultUniverse.getUniverseUUID(),
        AzureReservationGroup.of(
            region,
            Map.of(
                defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of("1", Arrays.asList("host-n1", "host-n2", "host-n3")))),
        AzureReservationGroup.of(
            region1,
            Map.of(
                defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
                Map.of(
                    "1",
                    Arrays.asList("host-n4-readonly", "host-n5-readonly", "host-n6-readonly")))));

    verifyNodeInteractionsCapacityReservation(
        21,
        NodeManager.NodeCommandType.Resume,
        params -> ((ResumeServer.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                region.getCode()),
            Arrays.asList("host-n1", "host-n2", "host-n3"),
            DoCapacityReservation.getCapacityReservationGroupName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.ASYNC,
                region1.getCode()),
            Arrays.asList("host-n4-readonly", "host-n5-readonly", "host-n6-readonly")));
  }

  @Test
  public void testResumeUniverseWithCRAwsSuccess() {
    RuntimeConfigEntry.upsertGlobal(ProviderConfKeys.enableCapacityReservationAws.getKey(), "true");
    setupUniverse(defaultProvider, false, 3);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    Region.getByCode(defaultProvider, "region-1");
    verifyCapacityReservationAws(
        defaultUniverse.getUniverseUUID(),
        Map.of(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceType,
            Map.of("1", new ZoneData("region-1", Arrays.asList("host-n1", "host-n2", "host-n3")))));

    verifyNodeInteractionsCapacityReservation(
        9,
        NodeManager.NodeCommandType.Resume,
        params -> ((ResumeServer.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                "az-1",
                defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.instanceType),
            Arrays.asList("host-n1", "host-n2", "host-n3")));
  }

  @Test
  public void testResumeNodeStates() {
    setupUniverse(false, 3);

    AtomicReference<String> nodeWithStoppedProcesses = new AtomicReference<>();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          for (NodeDetails node : universe.getNodes()) {
            if (nodeWithStoppedProcesses.get() == null) {
              node.isMaster = false;
              node.isTserver = false;
              nodeWithStoppedProcesses.set(node.getNodeName());
            }
            node.state = NodeDetails.NodeState.Stopped;
          }
        });
    expectedUniverseVersion++;
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Map<NodeDetails.NodeState, List<NodeDetails>> byStates =
        universe.getNodes().stream().collect(Collectors.groupingBy(node -> node.state));
    assertEquals(1, byStates.get(NodeDetails.NodeState.Stopped).size());
    assertEquals(2, byStates.get(NodeDetails.NodeState.Live).size());

    String stoppedNodeName = byStates.get(NodeDetails.NodeState.Stopped).get(0).nodeName;
    assertEquals(nodeWithStoppedProcesses.get(), stoppedNodeName);
  }

  @Test
  public void testResumeUniverseWithUpdateInProgress() {
    setupUniverse(true, 1);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    PlatformServiceException thrown =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));
    assertThat(thrown.getMessage(), containsString("is already being updated"));
  }

  @Test
  public void testResumeUniverseWithEncyptionAtRestEnabled() {
    setupUniverse(false, 1);
    EncryptionAtRestUtil.addKeyRef(
        defaultUniverse.getUniverseUUID(),
        testKMSConfig.getConfigUUID(),
        "some_key_ref".getBytes());
    int numRotations = EncryptionAtRestUtil.getNumUniverseKeys(defaultUniverse.getUniverseUUID());
    assertEquals(1, numRotations);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, RESUME_UNIVERSE_TASKS, ImmutableSet.of(2, 3));
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testResumeUniverseRetries() {
    setupUniverse(false, 3);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Resume,
        CustomerTask.TargetType.Universe,
        taskParams.getUniverseUUID(),
        TaskType.ResumeUniverse,
        taskParams);
  }

  @Test
  public void testResumeUniverseWithNodeAgentInstallation() {
    setupUniverse(false, 3);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(), u -> u.getUniverseDetails().installNodeAgent = true);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, RESUME_UNIVERSE_TASKS, ImmutableSet.of(7));
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }
}
