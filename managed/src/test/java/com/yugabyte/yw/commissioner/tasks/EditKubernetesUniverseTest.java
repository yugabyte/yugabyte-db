// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_UPGRADE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.POD_INFO;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod.CommandType.WAIT_FOR_POD;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.contains;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class EditKubernetesUniverseTest extends CommissionerBaseTest {

  private EditKubernetesUniverse editUniverse;

  private Universe defaultUniverse;
  private YBClient mockClient;

  private static final String NODE_PREFIX = "demo-universe";
  private static final String YB_SOFTWARE_VERSION = "1.0.0";

  private Map<String, String> config = new HashMap<>();

  @Before
  public void setUp() {
    super.setUp();
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.editUniverse =
        new EditKubernetesUniverse(
            mockBaseTaskDependencies, null, mockOperatorStatusUpdaterFactory, mockYbcManager);
  }

  private void setup() {
    try {
      File jsonFile = new File("src/test/resources/testPod.json");
      InputStream jsonStream = new FileInputStream(jsonFile);

      Pod testPod = Serialization.unmarshal(jsonStream, Pod.class);
      when(mockKubernetesManager.getPodObject(any(), any(), any())).thenReturn(testPod);
    } catch (Exception e) {
    }
    mockClient = mock(YBClient.class);
    IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
    ChangeMasterClusterConfigResponse ccr = new ChangeMasterClusterConfigResponse(1111, "", null);
    try {
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(ccr);
      when(mockClient.isServerReady(any(), anyBoolean())).thenReturn(okReadyResp);
    } catch (Exception ex) {
    }
    mockWaits(mockClient, 3);
    GetLoadMovePercentResponse gpr = new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null);
    try {
      when(mockClient.getLoadMoveCompletion()).thenReturn(gpr);
    } catch (Exception e) {
    }
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "true");
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts("1.2.3.0", 1234));
  }

  private void setupUniverseSingleAZ(boolean setMasters) {
    setup();

    config.put("KUBECONFIG", "test");
    kubernetesProvider.setConfigMap(config);
    kubernetesProvider.save();
    editUniverse.setUserTaskUUID(UUID.randomUUID());
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 1;
    userIntent.dedicatedNodes = true;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION;
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdaterForK8sEdit(
            userIntent, NODE_PREFIX, setMasters /* setMasters */, false /* updateInProgress */));
    // Universe.saveDetails(
    //     defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithActivePods(1, 3));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    setDumpEntitiesMock(defaultUniverse, "", false);
    log.info("Universe: {}", defaultUniverse.getUniverseDetailsJson());
  }

  private void setupUniverseMultiAZ(boolean setMasters, int numTservers) {
    setup();

    config.put("KUBECONFIG", "test");
    kubernetesProvider.setConfigMap(config);
    kubernetesProvider.save();
    editUniverse.setUserTaskUUID(UUID.randomUUID());
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, null, numTservers, 100, 50);
    userIntent.dedicatedNodes = true;
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION;
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdaterForK8sEdit(
            userIntent, NODE_PREFIX, setMasters /* setMasters */, false /* updateInProgress */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    setDumpEntitiesMock(defaultUniverse, "", false);
    log.info("Universe: {}", defaultUniverse.getUniverseDetailsJson());
  }

  private static final List<TaskType> KUBERNETES_ADD_POD_TASKS =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCheckNumPod,
          TaskType.KubernetesCommandExecutor,
          TaskType.WaitForServer,
          TaskType.UpdatePlacementInfo,
          TaskType.KubernetesCommandExecutor,
          TaskType.InstallingThirdPartySoftware,
          TaskType.UpdateUniverseIntent,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private List<JsonNode> getExpectedAddPodTaskResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_PODS.name())),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private static final List<TaskType> KUBERNETES_REMOVE_POD_TASKS =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.UpdatePlacementInfo,
          TaskType.WaitForDataMove,
          TaskType.CheckNodeSafeToDelete,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCheckNumPod,
          TaskType.KubernetesCommandExecutor,
          TaskType.WaitForDuration,
          TaskType.ModifyBlackList,
          TaskType.KubernetesCommandExecutor,
          TaskType.UpdateUniverseIntent,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private List<JsonNode> getExpectedRemovePodTaskResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_PODS.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private static final List<TaskType> KUBERNETES_CHANGE_INSTANCE_TYPE_TASKS =
      ImmutableList.of(
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.UpdatePlacementInfo,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.CheckFollowerLag,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesWaitForPod,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.CheckFollowerLag,
          TaskType.KubernetesCommandExecutor,
          TaskType.UpdateUniverseIntent,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private List<JsonNode> getExpectedChangeInstaceTypeResults() {
    return ImmutableList.of(
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
        Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()),
        Json.toJson(ImmutableMap.of()));
  }

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      List<TaskType> taskList,
      List<JsonNode> resultList,
      String type) {
    int position = 0;
    for (TaskType task : taskList) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      // In the case of adding and wait for server, we need to ensure that
      // there are two waits queued (one for each added server).
      if (task == TaskType.WaitForServer && type.equals("add")) {
        assertEquals(2, tasks.size());
      } else {
        assertEquals(1, tasks.size());
      }

      assertEquals(task, tasks.get(0).getTaskType());
      JsonNode expectedResults = resultList.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(
      UniverseDefinitionTaskParams taskParams,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      PlacementInfo pi) {
    taskParams.upsertPrimaryCluster(userIntent, pi);
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.getPrimaryCluster().uuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  public JsonNode parseShellResponseAsJson(ShellResponse response) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.readTree(response.message);
    } catch (IOException e) {
      throw new RuntimeException("Shell Response message is not a valid Json.");
    }
  }

  @Test
  public void testAddNode() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "0");
    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    // After changing to 5 tservers.
    String podsString =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.4\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.5\"}, \"spec\": {\"hostname\": \"yb-tserver-3\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.6\"}, \"spec\": {\"hostname\": \"yb-tserver-4\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 5;
    newUserIntent.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 5;
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockKubernetesManager, times(1))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(3))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition, KUBERNETES_ADD_POD_TASKS, getExpectedAddPodTaskResults(), "add");
  }

  @Test
  public void testRemoveNode() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "0");
    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    String podsString =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.0\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts("yb-master-0.svc.cluster.local", 1234));
    setDumpEntitiesMock(defaultUniverse, "yb-tserver-2", false);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.ybEditWaitDurationBeforeBlacklistClear.getKey(), "1 ms");
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 2;
    newUserIntent.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 2;
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockKubernetesManager, times(1))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(2))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition,
        KUBERNETES_REMOVE_POD_TASKS,
        getExpectedRemovePodTaskResults(),
        "remove");
  }

  @Test
  public void testChangeInstanceType() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "0");
    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    String podsString =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.0\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.2.0\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.2.1\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.2.2\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    InstanceType.upsert(
        kubernetesProvider.getUuid(), "c5.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.instanceType = "c5.xlarge";
    newUserIntent.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.tserverK8SNodeResourceSpec.cpuCoreCount = 4.0;
    newUserIntent.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockKubernetesManager, times(3))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(3))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(1))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition,
        KUBERNETES_CHANGE_INSTANCE_TYPE_TASKS,
        getExpectedChangeInstaceTypeResults(),
        "change");
  }

  @Test
  public void testEditKubernetesUniverseRetry() {
    setupUniverseSingleAZ(/* Create Masters */ true);
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "0");
    String podsString =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-master-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-master-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.4\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + NODE_PREFIX
            + "\"}}]}";
    List<Pod> pods = TestUtils.deserialize(podsString, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.tserverK8SNodeResourceSpec = new K8SNodeResourceSpec();
    newUserIntent.masterK8SNodeResourceSpec = new K8SNodeResourceSpec();

    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 3;
    taskParams.upsertPrimaryCluster(newUserIntent, pi);
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.getPrimaryCluster().uuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Edit,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.EditKubernetesUniverse,
        taskParams);
  }

  @Test
  public void testCheckEditKubernetesRerunDisallowedVolumeSizeChange() throws IOException {
    setupUniverseSingleAZ(true /*set masters */);
    ObjectMapper mapper = new ObjectMapper();
    UniverseDefinitionTaskParams taskParams =
        mapper.readValue(
            defaultUniverse.getUniverseDetailsJson(), UniverseDefinitionTaskParams.class);
    taskParams.clusters.get(0).userIntent.deviceInfo.volumeSize =
        150; /* Changed volume size 100 -> 150 */
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = new TaskInfo(TaskType.EditKubernetesUniverse, UUID.randomUUID());
    taskInfo.setTaskParams(Json.toJson(taskParams));
    boolean canRerun = EditKubernetesUniverse.checkEditKubernetesRerunAllowed(taskInfo);
    assertFalse(canRerun);
  }

  @Test
  public void testCheckEditKubernetesRerunDisallowedNumNodesChange() throws IOException {
    setupUniverseSingleAZ(true /*set masters */);
    ObjectMapper mapper = new ObjectMapper();
    UniverseDefinitionTaskParams taskParams =
        mapper.readValue(
            defaultUniverse.getUniverseDetailsJson(), UniverseDefinitionTaskParams.class);
    taskParams
            .clusters
            .get(0)
            .placementInfo
            .cloudList
            .get(0)
            .regionList
            .get(0)
            .azList
            .get(0)
            .numNodesInAZ +=
        1; /* Changed num nodes */
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = new TaskInfo(TaskType.EditKubernetesUniverse, UUID.randomUUID());
    taskInfo.setTaskParams(Json.toJson(taskParams));
    boolean canRerun = EditKubernetesUniverse.checkEditKubernetesRerunAllowed(taskInfo);
    assertFalse(canRerun);
  }

  @Test
  public void testCheckEditKubernetesRerunAllowedResourceSpecChange() throws IOException {
    setupUniverseSingleAZ(true /*set masters */);
    ObjectMapper mapper = new ObjectMapper();
    UniverseDefinitionTaskParams taskParams =
        mapper.readValue(
            defaultUniverse.getUniverseDetailsJson(), UniverseDefinitionTaskParams.class);
    taskParams.clusters.get(0).userIntent.masterK8SNodeResourceSpec.memoryGib += 1.0;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = new TaskInfo(TaskType.EditKubernetesUniverse, UUID.randomUUID());
    taskInfo.setTaskParams(Json.toJson(taskParams));
    boolean canRerun = EditKubernetesUniverse.checkEditKubernetesRerunAllowed(taskInfo);
    assertTrue(canRerun);
  }

  @Test
  public void testShrinkUniverseDiskCheckTaskFails() {
    setupUniverseMultiAZ(true, 6);
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "100");
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(2).numNodesInAZ = 1;
    newUserIntent.numNodes = 3;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);
    mockMetrics(taskParams.nodeDetailsSet, ServerType.TSERVER, 546.0);
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Failure, taskInfo.getTaskState());
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.ValidateNodeDiskSize)
            .findFirst()
            .get();
    String expectedMsg =
        "Additional disk size of 273.00 GB is needed, but only 27.00 GB is available";
    assertTrue(subTaskInfo.getErrorMessage().contains(expectedMsg));
  }

  @Test
  public void testShrinkUniverseDiskCheckTaskSuccess() {
    setupUniverseMultiAZ(true, 6);
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "100");
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(2).numNodesInAZ = 1;
    newUserIntent.numNodes = 3;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);
    mockMetrics(taskParams.nodeDetailsSet, ServerType.TSERVER, 285.0);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts("yb-master-0_az-1.svc.cluster.local", 1234));
    String podsString_z1 =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.0\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + "demo-universe-az-1"
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.2.0\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + "demo-universe-az-1"
            + "\"}}]}";
    String podsString_z2 =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.0\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + "demo-universe-az-2"
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.2.0\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + "demo-universe-az-2"
            + "\"}}]}";
    String podsString_z3 =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.3.0\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + "demo-universe-az-3"
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"1.2.2.0\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + "demo-universe-az-3"
            + "\"}}]}";
    List<Pod> pods_z1 = TestUtils.deserialize(podsString_z1, PodList.class).getItems();
    List<Pod> pods_z2 = TestUtils.deserialize(podsString_z2, PodList.class).getItems();
    List<Pod> pods_z3 = TestUtils.deserialize(podsString_z3, PodList.class).getItems();
    when(mockKubernetesManager.getPodInfos(any(), any(), eq("demo-universe-az-1")))
        .thenReturn(pods_z1);
    when(mockKubernetesManager.getPodInfos(any(), any(), eq("demo-universe-az-2")))
        .thenReturn(pods_z2);
    when(mockKubernetesManager.getPodInfos(any(), any(), eq("demo-universe-az-3")))
        .thenReturn(pods_z3);
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testShrinkUniverseDiskCheckTaskFailsModifiedTargetUsage() {
    setupUniverseMultiAZ(true, 6);
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "120");
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    PlacementInfo pi = defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(2).numNodesInAZ = 1;
    newUserIntent.numNodes = 3;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), primaryCluster.uuid, EDIT);
    mockMetrics(taskParams.nodeDetailsSet, ServerType.TSERVER, 285.0);
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent, pi);
    assertEquals(Failure, taskInfo.getTaskState());
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.ValidateNodeDiskSize)
            .findFirst()
            .get();
    String expectedMsg =
        "Additional disk size of 199.50 GB is needed, but only 157.50 GB is available";
    assertTrue(subTaskInfo.getErrorMessage().contains(expectedMsg));
  }

  private void mockMetrics(
      Set<NodeDetails> nodeDetails, ServerType serverType, double totalUsedSizeGb) {
    List<MetricQueryResponse.Entry> sizeResponseList = new ArrayList<>();
    MetricQueryResponse.Entry entry = new MetricQueryResponse.Entry();
    entry.labels = new HashMap<>();
    entry.values = new ArrayList<>();
    entry.values.add(ImmutablePair.of(0.1, totalUsedSizeGb));
    sizeResponseList.add(entry);
    doReturn(sizeResponseList)
        .when(mockMetricQueryHelper)
        .queryDirect(contains(serverType.equals(ServerType.TSERVER) ? "yb-tserver" : "yb-master"));
  }
}
